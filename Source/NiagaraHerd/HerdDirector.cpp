#include "HerdDirector.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraDataSetAccessor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpectatorPawn.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "CanvasItem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "Misc/App.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Particles/ParticlePerfStatsManager.h"
#include "FrameGrabber.h"
#include "ImageWriteQueue.h"
#include "ImageWriteTask.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SViewport.h"

static TAutoConsoleVariable<int32> CMode(TEXT("herd.Mode"),0,TEXT("0=CPU meshes; 1=GPU sprites. Changing mode restarts the round."));
static TAutoConsoleVariable<int32> CCount(TEXT("herd.Count"),500,TEXT("Runtime particle count; changing count restarts the round (CPU cap 5000, GPU cap 4000000)."));
static TAutoConsoleVariable<int32> CHash(TEXT("herd.SpatialHash"),1,TEXT("CPU neighbor search: 0=all pairs baseline, 1=spatial hash. Identical rules."));
static TAutoConsoleVariable<int32> CAutopilot(TEXT("herd.Auto"),0,TEXT("Deterministic attraction route for repeatable tests. 0=off, 1=on."));
static TAutoConsoleVariable<int32> CBenchmark(TEXT("herd.Benchmark"),0,TEXT("Set 1 to run the automated sweep. Results in Saved/Benchmarks."));
static TAutoConsoleVariable<int32> CTestGoal(TEXT("herd.TestGoalAtSpawn"),0,TEXT("TEST ONLY: move goal to spawn to verify simulation-to-score callbacks. 0=normal."));
static TAutoConsoleVariable<int32> CForceWin(TEXT("herd.TestWinThreshold"),70,TEXT("Rescue percentage required to win, default 70. Debug test setting."));
static TAutoConsoleVariable<float> CTime(TEXT("herd.RoundSeconds"),120.f,TEXT("Round duration (seconds)."));

AHerdDirector::AHerdDirector()
{
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Flock=CreateDefaultSubobject<UNiagaraComponent>(TEXT("Flock"));
    Flock->SetupAttachment(RootComponent); Flock->SetAutoActivate(false);
    Beacon=CreateDefaultSubobject<USceneComponent>(TEXT("Beacon")); Beacon->SetupAttachment(RootComponent);
}
void AHerdDirector::BeginPlay()
{
    Super::BeginPlay();
#if WITH_PARTICLE_PERF_STATS
    PerformanceListener=MakeShared<FParticlePerfStatsListener_GatherAll,ESPMode::ThreadSafe>(false,false,true);
    FParticlePerfStatsManager::AddListener(PerformanceListener);
#endif
    bCapture=FParse::Value(FCommandLine::Get(),TEXT("HerdCapture="),CaptureName);
    if(bCapture)
    {
        FApp::SetUseFixedTimeStep(true); FApp::SetFixedDeltaTime(1.0/30.0);
        CAutopilot->Set(1,ECVF_SetByConsole);
        IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("Capture")/CaptureName),true);
    }
    CPUAsset=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Herd/NS_HerdCPU.NS_HerdCPU"));
    GPUAsset=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Herd/NS_HerdGPU.NS_HerdGPU"));
    CreateArena();
    if (APlayerController* PC=GetWorld()->GetFirstPlayerController())
    {
        PC->bShowMouseCursor=true; PC->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false));
        auto* Camera=GetWorld()->SpawnActor<ACameraActor>(FVector(0,0,2700),FRotator(-90,-90,0));
        Camera->GetCameraComponent()->ProjectionMode=ECameraProjectionMode::Orthographic;
        Camera->GetCameraComponent()->OrthoWidth=4400;
        Camera->GetCameraComponent()->bConstrainAspectRatio=false;
        Camera->GetCameraComponent()->bOverrideAspectRatioAxisConstraint=true;
        Camera->GetCameraComponent()->AspectRatioAxisConstraint=AspectRatio_MaintainXFOV;
        PC->SetViewTarget(Camera);
    }
    Restart(CMode.GetValueOnGameThread()!=0,CCount.GetValueOnGameThread());
}
void AHerdDirector::EndPlay(const EEndPlayReason::Type Reason)
{
    if(CaptureGrabber) { CaptureGrabber->StopCapturingFrames(); CaptureGrabber->Shutdown(); CaptureGrabber.Reset(); }
#if WITH_PARTICLE_PERF_STATS
    if(PerformanceListener) FParticlePerfStatsManager::RemoveListener(PerformanceListener);
    PerformanceListener.Reset();
#endif
    Super::EndPlay(Reason);
}
void AHerdDirector::CreateArena()
{
    UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    UStaticMesh* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    UStaticMesh* Cylinder=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    UMaterialInterface* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Herd/M_Arena.M_Arena"));
    auto Add=[&](FString Name,UStaticMesh* Mesh,FVector Position,FVector Scale,FLinearColor Color,USceneComponent* Parent=nullptr)
    {
        auto* C=NewObject<UStaticMeshComponent>(this,*Name);
        C->SetupAttachment(Parent?Parent:RootComponent.Get()); C->SetStaticMesh(Mesh); C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetRelativeLocation(Position); C->SetRelativeScale3D(Scale); C->SetCastShadow(false);
        if(Base)
        {
            auto* M=UMaterialInstanceDynamic::Create(Base,this); M->SetVectorParameterValue(TEXT("Tint"),Color);
            C->SetMaterial(0,M); Materials.Add(M);
        }
        C->RegisterComponent(); return C;
    };
    Add(TEXT("Water"),Cube,FVector(0,0,-50),FVector(32,20,.5),FLinearColor(.018,.065,.11));
    Add(TEXT("Background"),Cube,FVector(0,0,-120),FVector(150,100,.1),FLinearColor(.008,.025,.045));
    for(int32 X=-1400;X<=1400;X+=200)
        Add(FString::Printf(TEXT("GridX%d"),X),Cube,FVector(X,0,-22),FVector(.008,18,.005),FLinearColor(.025,.09,.14));
    for(int32 Y=-800;Y<=800;Y+=200)
        Add(FString::Printf(TEXT("GridY%d"),Y),Cube,FVector(0,Y,-22),FVector(30,.008,.005),FLinearColor(.025,.09,.14));
    Add(TEXT("Top"),Cube,FVector(0,-930,-15),FVector(31,.05,.05),FLinearColor(.10,.30,.40));
    Add(TEXT("Bottom"),Cube,FVector(0,930,-15),FVector(31,.05,.05),FLinearColor(.10,.30,.40));
    Add(TEXT("Left"),Cube,FVector(-1530,0,-15),FVector(.05,18.6,.05),FLinearColor(.10,.30,.40));
    Add(TEXT("Right"),Cube,FVector(1530,0,-15),FVector(.05,18.6,.05),FLinearColor(.10,.30,.40));
    Add(TEXT("Sanctuary"),Cylinder,FVector(1160,0,-18),FVector(4.8,4.8,.03),FLinearColor(.025,.16,.13));
    for(int32 I=0;I<48;++I)
    {
        float A=I*2*PI/48;
        auto* Segment=Add(FString::Printf(TEXT("GoalRing%d"),I),Cube,FVector(1160+240*FMath::Cos(A),240*FMath::Sin(A),-10),FVector(.045,.34,.025),FLinearColor(.1,.85,.56));
        Segment->SetRelativeRotation(FRotator(0,FMath::RadiansToDegrees(A),0));
    }
    Add(TEXT("ReefA"),Cylinder,FVector(-180,290,-5),FVector(2.5,2.5,.2),FLinearColor(.07,.15,.20));
    Add(TEXT("ReefB"),Cylinder,FVector(260,-320,-5),FVector(2.5,2.5,.2),FLinearColor(.07,.15,.20));
    Add(TEXT("BeaconCore"),Sphere,FVector(0,0,0),FVector(.65,.65,.18),FLinearColor(1,.78,.2),Beacon);
    Add(TEXT("BeaconCrossX"),Cube,FVector(0,0,6),FVector(1.1,.045,.02),FLinearColor(1,.86,.4),Beacon);
    Add(TEXT("BeaconCrossY"),Cube,FVector(0,0,6),FVector(.045,1.1,.02),FLinearColor(1,.86,.4),Beacon);
}
void AHerdDirector::Restart(bool GPU,int32 Count)
{
    Flock->DeactivateImmediate();
    bGPU=GPU; ParticleCount=FMath::Clamp(Count,1,GPU?4000000:5000);
    CMode->Set(int32(GPU),ECVF_SetByConsole); CCount->Set(ParticleCount,ECVF_SetByConsole);
    ++RunID; SimulationSerial=0; ResultSerial=0; ResultUpdates=0; SavedCount=0; Overflow=0; CpuSimulationMs=0;
    bSpawnLocked=false;
    bWon=false; bLost=false; bPaused=false; Elapsed=0; Remaining=FMath::Max(CTime.GetValueOnGameThread(),1.f);
    PulseUntil=-10; PulseReady=0; PlayerPosition=FVector(-1320,0,35);
    UNiagaraSystem* Asset=GPU?GPUAsset:CPUAsset;
    bReady=Asset!=nullptr;
    if(Asset)
    {
        Flock->SetAsset(Asset);
        Flock->SetVariableInt(TEXT("User.Count"),ParticleCount);
        Flock->ReinitializeSystem(); Flock->Activate(true);
        Status=TEXT("Guide the school into the green sanctuary.");
    }
    else Status=TEXT("Niagara assets missing. Run Scripts/BuildAssets.ps1.");
    UE_LOG(LogTemp,Display,TEXT("HERD_ROUND run=%d mode=%s count=%d assets=%s"),RunID,GPU?TEXT("GPU"):TEXT("CPU"),ParticleCount,bReady?TEXT("OK"):TEXT("MISSING"));
    if(Asset)
    {
        UE_LOG(LogTemp,Display,TEXT("HERD_ASSET_READY valid=%d ready=%d spawn=%d update=%d"),Asset->IsValid(),Asset->IsReadyToRun(),Asset->GetSystemSpawnScript()->IsReadyToRun(ENiagaraSimTarget::CPUSim),Asset->GetSystemUpdateScript()->IsReadyToRun(ENiagaraSimTarget::CPUSim));
        for(const auto& Handle:Asset->GetEmitterHandles()) if(const auto* Data=Handle.GetEmitterData())
            UE_LOG(LogTemp,Display,TEXT("HERD_EMITTER_READY enabled=%d valid=%d ready=%d target=%d gpu_script=%d"),Handle.GetIsEnabled(),Data->IsValid(),Data->IsReadyToRun(),int32(Data->SimTarget),Data->GetGPUComputeScript()?Data->GetGPUComputeScript()->IsReadyToRun(ENiagaraSimTarget::GPUComputeSim):-1);
    }
    TRACE_BOOKMARK(TEXT("Herd round %d %s count %d"),RunID,GPU?TEXT("GPU"):TEXT("CPU"),ParticleCount);
}
FHerdFrame AHerdDirector::GetSimulationFrame(float Dt) const
{
    FHerdFrame F; F.Owner=const_cast<AHerdDirector*>(this); F.Player=FVector3f(PlayerPosition);
    F.Count=ParticleCount; F.Run=RunID; F.Serial=SimulationSerial; F.Time=Elapsed;
    F.DeltaTime=Dt; F.bGPU=bGPU; F.bSpatialHash=CHash.GetValueOnGameThread()!=0;
    F.Attract=bAttract?1.f:0.f; F.Pulse=Elapsed<PulseUntil?1.f:0.f;
    F.bFrozen=bPaused||bWon||bLost;
    if(CTestGoal.GetValueOnGameThread()!=0) F.Goal=FVector3f(-950,0,35);
    return F;
}
void AHerdDirector::ReceiveSimulationResult(int32 Count,int32 Run,uint32 Serial,int32 GridOverflow,double CpuMs)
{
    if(Run!=RunID || Serial<ResultSerial) return;
    SavedCount=FMath::Clamp(Count,0,ParticleCount); ResultSerial=Serial; ++ResultUpdates;
    Overflow=GridOverflow; CpuSimulationMs=CpuMs;
    if(!bWon && !bLost && !bBenchmark && SavedCount>=FMath::CeilToInt(ParticleCount*FMath::Clamp(CForceWin.GetValueOnGameThread(),1,100)/100.f))
    { bWon=true; Status=TEXT("Sanctuary secured. Press R to play again."); UE_LOG(LogTemp,Display,TEXT("HERD_WIN rescued=%d total=%d"),SavedCount,ParticleCount); }
}
void AHerdDirector::SetCountFromKeys(int32 D)
{
    const TArray<int32> Counts=bGPU?TArray<int32>{10000,25000,50000,100000,200000,400000,800000,1600000,3200000,4000000}:TArray<int32>{500,1000,2000,3500,5000};
    int32 Index=0;
    for(int32 I=0;I<Counts.Num();++I) if(Counts[I]<=ParticleCount) Index=I;
    Restart(bGPU,Counts[FMath::Clamp(Index+D,0,Counts.Num()-1)]);
}
void AHerdDirector::Tick(float Dt)
{
    Super::Tick(Dt); ++SimulationSerial;
    RealRuntime+=Dt;
    FrameMs=FrameMs*.92f+Dt*1000*.08f;
    auto* PC=GetWorld()->GetFirstPlayerController();
    if(!PC) return;
    if(!bBenchmark)
    {
        if(CMode.GetValueOnGameThread()!=int32(bGPU)) Restart(CMode.GetValueOnGameThread()!=0,CCount.GetValueOnGameThread()!=ParticleCount?CCount.GetValueOnGameThread():(CMode.GetValueOnGameThread()?100000:500));
        else if(CCount.GetValueOnGameThread()!=ParticleCount) Restart(bGPU,CCount.GetValueOnGameThread());
        if(PC->WasInputKeyJustPressed(EKeys::Tab)) Restart(!bGPU,bGPU?500:100000);
        if(PC->WasInputKeyJustPressed(EKeys::R)) Restart(bGPU,ParticleCount);
        if(PC->WasInputKeyJustPressed(EKeys::P)) bPaused=!bPaused;
        if(PC->WasInputKeyJustPressed(EKeys::Equals)||PC->WasInputKeyJustPressed(EKeys::Add)) SetCountFromKeys(1);
        if(PC->WasInputKeyJustPressed(EKeys::Hyphen)||PC->WasInputKeyJustPressed(EKeys::Subtract)) SetCountFromKeys(-1);
        if(PC->WasInputKeyJustPressed(EKeys::F6)) CHash->Set(1-CHash.GetValueOnGameThread(),ECVF_SetByConsole);
        if(PC->WasInputKeyJustPressed(EKeys::F7)) CAutopilot->Set(1-CAutopilot.GetValueOnGameThread(),ECVF_SetByConsole);
        if(PC->WasInputKeyJustPressed(EKeys::F9)||CBenchmark.GetValueOnGameThread()!=0) BeginBenchmark();
    }
    bAuto=bBenchmark||CAutopilot.GetValueOnGameThread()!=0;
    if(!bPaused && !bWon && !bLost)
    {
        Elapsed+=Dt; Remaining=FMath::Max(0.f,Remaining-Dt);
        bAttract=PC->IsInputKeyDown(EKeys::RightMouseButton);
        if(bAuto)
        {
            float T=FMath::Fmod(Elapsed,28.f);
            PlayerPosition=FVector(FMath::Lerp(-1150.f,1150.f,FMath::Min(T/22.f,1.f)),FMath::Sin(T*.36f)*300,35);
            bAttract=true;
        }
        else
        {
            FVector Move((PC->IsInputKeyDown(EKeys::D)?1:0)-(PC->IsInputKeyDown(EKeys::A)?1:0),(PC->IsInputKeyDown(EKeys::S)?1:0)-(PC->IsInputKeyDown(EKeys::W)?1:0),0);
            // Camera right is world +X; camera up is world -Y.
            PlayerPosition+=Move.GetSafeNormal()*650*Dt;
            if(PC->IsInputKeyDown(EKeys::LeftMouseButton))
            {
                FVector Origin,Direction;
                if(PC->DeprojectMousePositionToWorld(Origin,Direction) && FMath::Abs(Direction.Z)>.001f)
                {
                    FVector Aim=Origin+Direction*((35-Origin.Z)/Direction.Z);
                    PlayerPosition=FMath::VInterpConstantTo(PlayerPosition,Aim,Dt,900);
                }
            }
            PlayerPosition.X=FMath::Clamp(PlayerPosition.X,-1460.,1460.); PlayerPosition.Y=FMath::Clamp(PlayerPosition.Y,-860.,860.);
        }
        if(PC->WasInputKeyJustPressed(EKeys::SpaceBar)&&Elapsed>=PulseReady) { PulseUntil=Elapsed+.45f; PulseReady=Elapsed+2.5f; }
        if(Remaining<=0 && !bBenchmark) { bLost=true; Status=TEXT("Time expired. Press R to retry."); UE_LOG(LogTemp,Display,TEXT("HERD_LOSE rescued=%d total=%d"),SavedCount,ParticleCount); }
    }
    Beacon->SetRelativeLocation(PlayerPosition);
    Beacon->SetRelativeScale3D(FVector(Elapsed<PulseUntil?1.6:1));
    if(bBenchmark) UpdateBenchmark(Dt);
    if(bCapture)
    {
        if(!bDemoGPU && CaptureFrame>=960) { bDemoGPU=true; Restart(true,100000); }
        if(!bDemoStats && CaptureFrame>=1920) { bDemoStats=true; PC->ConsoleCommand(TEXT("stat Niagara")); PC->ConsoleCommand(TEXT("stat GPU")); }
        if(!CaptureGrabber && GEngine->GameViewport)
        {
            if(auto Widget=GEngine->GameViewport->GetGameViewportWidget())
                if(auto View=StaticCastSharedPtr<FSceneViewport>(Widget->GetViewportInterface().Pin()))
                {
                    CaptureGrabber=MakeShared<FFrameGrabber>(View.ToSharedRef(),FIntPoint(1920,1080));
                    CaptureGrabber->StartCapturingFrames();
                }
        }
        if(CaptureGrabber)
        {
            auto& Queue=FModuleManager::LoadModuleChecked<IImageWriteQueueModule>(TEXT("ImageWriteQueue")).GetWriteQueue();
            for(auto& Frame:CaptureGrabber->GetCapturedFrames())
            {
                if(CaptureFrame>=2250) break;
                auto Task=MakeUnique<FImageWriteTask>();
                Task->Filename=FPaths::ProjectSavedDir()/TEXT("Capture")/CaptureName/FString::Printf(TEXT("Frame%06d.png"),CaptureFrame++);
                Task->Format=EImageFormat::PNG; Task->CompressionQuality=0;
                TArray64<FColor> Pixels; Pixels.Append(Frame.ColorBuffer.GetData(),Frame.ColorBuffer.Num());
                Task->PixelData=MakeUnique<TImagePixelData<FColor>>(Frame.BufferSize,MoveTemp(Pixels));
                Task->AddPreProcessorToSetAlphaOpaque();
                Task->OnCompleted=[](bool Success){ if(!Success) UE_LOG(LogTemp,Error,TEXT("HERD_CAPTURE_WRITE_FAILED")); };
                Queue.Enqueue(MoveTemp(Task));
            }
            if(CaptureFrame<2250) CaptureGrabber->CaptureThisFrame(FFramePayloadPtr());
            else
            {
                CaptureGrabber->StopCapturingFrames(); CaptureGrabber->Shutdown(); CaptureGrabber.Reset();
                Queue.CreateFence().Wait(); UE_LOG(LogTemp,Display,TEXT("HERD_CAPTURE_COMPLETE frames=%d"),CaptureFrame); FPlatformMisc::RequestExit(false);
            }
        }
    }
    if(!bSpawnLocked)
        if(auto Controller=Flock->GetSystemInstanceController()) if(auto* Instance=Controller->GetSystemInstance_Unsafe())
            for(const auto& Emitter:Instance->GetEmitters()) if(Emitter->GetNumParticles()>=ParticleCount)
            {
                Flock->SetVariableInt(TEXT("User.Count"),0); bSpawnLocked=true;
                UE_LOG(LogTemp,Display,TEXT("HERD_SPAWN_LOCK rendered=%d requested=%d"),Emitter->GetNumParticles(),ParticleCount);
            }
    if(RealRuntime>=DiagnosticAt)
    {
        DiagnosticAt=RealRuntime+2;
        int32 RenderCount=0;
        if(auto Controller=Flock->GetSystemInstanceController()) if(auto* Instance=Controller->GetSystemInstance_Unsafe()) for(const auto& Emitter:Instance->GetEmitters())
        {
            RenderCount+=Emitter->GetNumParticles();
            if(!bGPU && Emitter->GetNumParticles()>0 && RealRuntime<10)
            {
                auto& Data=Emitter->GetParticleData();
                auto Pos=FNiagaraDataSetAccessor<FNiagaraPosition>(Data,TEXT("Position")).GetReader(Data.GetCurrentData()).GetSafe(0,FNiagaraPosition());
                auto Size=FNiagaraDataSetAccessor<FVector2f>(Data,TEXT("SpriteSize")).GetReader(Data.GetCurrentData()).GetSafe(0,FVector2f(-1));
                auto Scale=FNiagaraDataSetAccessor<FVector3f>(Data,TEXT("Scale")).GetReader(Data.GetCurrentData()).GetSafe(0,FVector3f(-1));
                auto Color=FNiagaraDataSetAccessor<FLinearColor>(Data,TEXT("Color")).GetReader(Data.GetCurrentData()).GetSafe(0,FLinearColor::Black);
                UE_LOG(LogTemp,Display,TEXT("HERD_ATTRIBUTES pos=(%.1f,%.1f,%.1f) size=(%.1f,%.1f) scale=(%.3f,%.3f,%.3f) color=(%.2f,%.2f,%.2f,%.2f)"),Pos.X,Pos.Y,Pos.Z,Size.X,Size.Y,Scale.X,Scale.Y,Scale.Z,Color.R,Color.G,Color.B,Color.A);
            }
        }
        RenderedCount=RenderCount;
        UE_LOG(LogTemp,Display,TEXT("HERD_STATE real=%.1f run=%d mode=%s count=%d rendered=%d rescued=%d callbacks=%d cpu_ms=%.3f frame_ms=%.3f serial=%u result=%u ortho=%.1f"),RealRuntime,RunID,bGPU?TEXT("GPU"):TEXT("CPU"),ParticleCount,RenderCount,SavedCount,ResultUpdates,CpuSimulationMs,FrameMs,SimulationSerial,ResultSerial,PC->PlayerCameraManager->GetCameraCacheView().OrthoWidth);
    }
    FString ShotName;
    if(!bTestScreenshot && RealRuntime>6 && FParse::Value(FCommandLine::Get(),TEXT("HerdShot="),ShotName))
    {
        bTestScreenshot=true;
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(ShotName+TEXT(".png")),false,false);
    }
    if(!bTestSwitched && RealRuntime>8 && FParse::Param(FCommandLine::Get(),TEXT("HerdSwitchTest")))
    { bTestSwitched=true; Restart(!bGPU,bGPU?500:100000); }
    float TestExit=0;
    if(FParse::Value(FCommandLine::Get(),TEXT("HerdTestExit="),TestExit)&&TestExit>0&&RealRuntime>TestExit)
    { UE_LOG(LogTemp,Display,TEXT("HERD_TEST_COMPLETE")); FPlatformMisc::RequestExit(false); }
}
void AHerdDirector::BeginBenchmark()
{
    if(bBenchmark) return;
    bBenchmark=true; CBenchmark->Set(0,ECVF_SetByConsole); BenchmarkStage=-1; StageTime=100;
    BenchmarkRows={TEXT("mode,count,spatial_hash,samples,mean_frame_ms,p95_frame_ms,mean_cpu_sim_ms,rescued,grid_overflow,result_updates,meets_frame_budget,rendered_count,spawn_locked,niagara_gt_ms,niagara_worker_ms,niagara_rt_ms,niagara_builtin_gpu_ms")};
    if(auto* V=IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))) V->Set(0,ECVF_SetByConsole);
    if(auto* V=IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"))) V->Set(0,ECVF_SetByConsole);
    UE_LOG(LogTemp,Display,TEXT("HERD_BENCHMARK_BEGIN"));
}
void AHerdDirector::UpdateBenchmark(float Dt)
{
    StageTime+=Dt;
    if(BenchmarkStage>=0 && StageTime>4 && StageTime<9)
    {
        if(FrameSamples.IsEmpty())
        {
#if WITH_PARTICLE_PERF_STATS
            if(auto* Stats=PerformanceListener->GetStats(Flock.Get())) Stats->Reset(true);
#endif
            BenchmarkRegion=FString::Printf(TEXT("%s_%d_hash%d"),bGPU?TEXT("GPU"):TEXT("CPU"),ParticleCount,CHash.GetValueOnGameThread());
            TRACE_BEGIN_REGION(*BenchmarkRegion);
        }
        FrameSamples.Add(Dt*1000); CpuSamples.Add(CpuSimulationMs);
    }
    if(StageTime<9) return;
    if(BenchmarkStage>=0 && FrameSamples.Num())
    {
        TRACE_END_REGION(*BenchmarkRegion);
        float Sum=0,Cpu=0; for(float V:FrameSamples) Sum+=V; for(float V:CpuSamples) Cpu+=V;
        FrameSamples.Sort(); float Mean=Sum/FrameSamples.Num(),P95=FrameSamples[FMath::Min(FrameSamples.Num()-1,FMath::FloorToInt(FrameSamples.Num()*.95f))];
        float GT=-1,Worker=-1,RT=-1,BuiltinGPU=-1;
#if WITH_PARTICLE_PERF_STATS
        if(auto* Stats=PerformanceListener->GetStats(Flock.Get()))
        {
            GT=Stats->GetGameThreadStats().GetPerFrameAvg_GTOnly()/1000.f;
            Worker=(Stats->GetGameThreadStats().GetPerFrameAvg()-Stats->GetGameThreadStats().GetPerFrameAvg_GTOnly())/1000.f;
            RT=Stats->GetRenderThreadStats(true).GetPerFrameAvg()/1000.f;
            BuiltinGPU=Stats->GetGPUStats(true).GetPerFrameAvgMicroseconds()/1000.f;
        }
#endif
        FString Row=FString::Printf(TEXT("%s,%d,%d,%d,%.4f,%.4f,%.4f,%d,%d,%d,%s,%d,%s,%.4f,%.4f,%.4f,%.4f"),bGPU?TEXT("GPU"):TEXT("CPU"),ParticleCount,CHash.GetValueOnGameThread(),FrameSamples.Num(),Mean,P95,Cpu/CpuSamples.Num(),SavedCount,Overflow,ResultUpdates,P95<=16.6667?TEXT("true"):TEXT("false"),RenderedCount,bSpawnLocked?TEXT("true"):TEXT("false"),GT,Worker,RT,BuiltinGPU);
        BenchmarkRows.Add(Row); UE_LOG(LogTemp,Display,TEXT("HERD_BENCH %s"),*Row);
    }
    ++BenchmarkStage; FrameSamples.Reset(); CpuSamples.Reset(); StageTime=0;
    if(BenchmarkStage>=20)
    {
        const FString Directory=FPaths::ProjectSavedDir()/TEXT("Benchmarks"); IFileManager::Get().MakeDirectory(*Directory,true);
        FString File=Directory/FString::Printf(TEXT("HerdSweep-%s.csv"),*FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
        FFileHelper::SaveStringArrayToFile(BenchmarkRows,*File);
        bBenchmark=false; CHash->Set(1,ECVF_SetByConsole); Status=TEXT("Benchmark complete. CSV saved in Saved/Benchmarks.");
        UE_LOG(LogTemp,Display,TEXT("HERD_BENCHMARK_COMPLETE %s"),*File); return;
    }
    const int32 CPUCounts[]={500,1000,2000,3500,5000};
    const int32 GPUCounts[]={10000,25000,50000,100000,200000,400000,800000,1600000,3200000,4000000};
    if(BenchmarkStage<10) { CHash->Set(BenchmarkStage>=5?1:0,ECVF_SetByConsole); Restart(false,CPUCounts[BenchmarkStage%5]); }
    else Restart(true,GPUCounts[BenchmarkStage-10]);
}

AHerdGameMode::AHerdGameMode() { DefaultPawnClass=nullptr; HUDClass=AHerdHUD::StaticClass(); }
void AHerdGameMode::StartPlay()
{
    Super::StartPlay();
    if(!TActorIterator<AHerdDirector>(GetWorld())) GetWorld()->SpawnActor<AHerdDirector>();
}
void AHerdHUD::DrawHUD()
{
    Super::DrawHUD();
    if(!Canvas) return;
    AHerdDirector* D=nullptr; for(TActorIterator<AHerdDirector> It(GetWorld());It;++It){D=*It;break;} if(!D) return;
    const float SX=Canvas->SizeX/1920.f,SY=Canvas->SizeY/1080.f;
    auto Box=[&](float X,float Y,float W,float H,FLinearColor Color){ FCanvasTileItem Item(FVector2D(X*SX,Y*SY),FVector2D(W*SX,H*SY),Color); Item.BlendMode=SE_BLEND_Translucent; Canvas->DrawItem(Item); };
    auto Text=[&](FString S,float X,float Y,float Scale,FLinearColor Color=FLinearColor::White)
    {
        FCanvasTextItem Item(FVector2D(X*SX,Y*SY),FText::FromString(S),GEngine->GetMediumFont(),Color);
        Item.Scale=FVector2D(Scale*SX,Scale*SY); Item.EnableShadow(FLinearColor(0,0,0,.25)); Canvas->DrawItem(Item);
    };
    Box(0,0,1920,128,FLinearColor(.008,.025,.045,.96));
    Text(TEXT("T I D E K E E P E R"),48,25,1.45);
    Text(TEXT("Guide the school. Secure the sanctuary."),48,69,.8,FLinearColor(.5,.72,.82));
    Text(D->bGPU?TEXT("GPU  /  SIMPLE + MANY"):TEXT("CPU  /  COMPLEX + FEW"),735,28,1.0,FLinearColor(.2,.85,1));
    Text(FString::Printf(TEXT("%s particles  |  %s"),*FText::AsNumber(D->ParticleCount).ToString(),D->bGPU?TEXT("SPRITES"):TEXT("MESHES")),735,67,.8);
    Text(FString::Printf(TEXT("RESCUED  %d / %d"),D->SavedCount,FMath::CeilToInt(D->ParticleCount*.7f)),1320,27,1.05,FLinearColor(.2,1,.65));
    Text(FString::Printf(TEXT("TIME  %02d:%02d"),int32(D->Remaining)/60,int32(D->Remaining)%60),1680,65,.8);
    Box(1320,72,300,7,FLinearColor(.05,.18,.19,1));
    Box(1320,72,300*FMath::Clamp(D->SavedCount/(D->ParticleCount*.7f),0.f,1.f),7,FLinearColor(.12,.9,.55,1));
    Box(0,946,1920,134,FLinearColor(.008,.025,.045,.96));
    Text(TEXT("WASD / LMB  Move beacon     RMB  Hold to attract     SPACE  Repel pulse     TAB  Switch mode     R  Restart"),48,966,.83);
    Text(TEXT("+ / -  Particle count     P  Pause     F6  CPU hash / all-pairs     F7  Test route     F9  Benchmark sweep"),48,1000,.73,FLinearColor(.48,.67,.78));
    Text(FString::Printf(TEXT("%.1f ms  |  CPU sim %.2f ms  |  %s  |  callbacks %d"),D->FrameMs,D->CpuSimulationMs,CHash.GetValueOnGameThread()?TEXT("spatial hash"):TEXT("all pairs"),D->ResultUpdates),48,1035,.72,FLinearColor(.6,.8,.85));
    if(D->bGPU) Text(FString::Printf(TEXT("grid overflow: %d  |  async score age: %u ticks"),D->Overflow,D->SimulationSerial-D->ResultSerial),1050,1035,.72,FLinearColor(.6,.8,.85));
    Text(D->bAttract?TEXT("ATTRACTING"):TEXT("DRIVING"),48,152,.8,FLinearColor(1,.8,.3));
    Text(D->Elapsed<D->PulseReady?TEXT("Pulse recharging"):TEXT("Pulse ready"),48,182,.65,FLinearColor(.6,.75,.8));
    if(D->bBenchmark||D->bAuto) Text(TEXT("AUTOMATED TEST ROUTE"),48,213,.75,FLinearColor(1,.6,.3));
    if(D->bCapture) Text(TEXT("DEMO CAPTURE  /  FIXED 30 FPS  /  PERFORMANCE MEASURED SEPARATELY"),48,244,.65,FLinearColor(.55,.75,.82));
    if(!D->bReady||D->bWon||D->bLost||D->bPaused)
    {
        Box(460,400,1000,180,FLinearColor(.008,.025,.045,.94));
        Text(D->bPaused?TEXT("PAUSED"):D->bWon?TEXT("SANCTUARY SECURED"):D->bLost?TEXT("TIME EXPIRED"):TEXT("SETUP REQUIRED"),520,432,1.7,D->bWon?FLinearColor(.2,1,.65):FLinearColor::White);
        Text(D->bPaused?TEXT("Press P to resume."):D->Status,520,505,.8);
    }
}
