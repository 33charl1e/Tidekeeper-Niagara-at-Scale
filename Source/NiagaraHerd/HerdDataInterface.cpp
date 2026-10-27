#include "HerdDataInterface.h"
#include "HerdSimulation.h"
#include "HerdDirector.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraShaderParametersBuilder.h"
#include "NiagaraCompileHashVisitor.h"
#include "NiagaraGpuComputeDispatchInterface.h"
#include "NiagaraGpuReadbackManager.h"
#include "NiagaraWorldManager.h"
#include "NiagaraDataInterfaceUtilities.h"
#include "NiagaraParameterStore.h"
#include "EngineUtils.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "VectorVM.h"

namespace HerdDI
{
DECLARE_GPU_STAT_NAMED(HerdBoids,TEXT("HerdBoids"));
BEGIN_SHADER_PARAMETER_STRUCT(FReadParameters, )
    SHADER_PARAMETER(uint32, Count)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FHerdReadAgent>, AgentData)
END_SHADER_PARAMETER_STRUCT()

class FHerdCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FHerdCS);
    SHADER_USE_PARAMETER_STRUCT(FHerdCS, FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, AgentCount)
        SHADER_PARAMETER(float, DeltaTime)
        SHADER_PARAMETER(float, SimTime)
        SHADER_PARAMETER(FVector3f, Player)
        SHADER_PARAMETER(FVector3f, Goal)
        SHADER_PARAMETER(float, Attract)
        SHADER_PARAMETER(float, Pulse)
        SHADER_PARAMETER(uint32, Frozen)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FHerdAgent>, InputAgents)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FHerdAgent>, OutputAgents)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, GridCounts)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, GridIndices)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, Summary)
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
class FHerdInitializeCS : public FHerdCS { DECLARE_GLOBAL_SHADER(FHerdInitializeCS); SHADER_USE_PARAMETER_STRUCT(FHerdInitializeCS, FHerdCS); };
class FHerdBuildGridCS : public FHerdCS { DECLARE_GLOBAL_SHADER(FHerdBuildGridCS); SHADER_USE_PARAMETER_STRUCT(FHerdBuildGridCS, FHerdCS); };
class FHerdSimulateCS : public FHerdCS { DECLARE_GLOBAL_SHADER(FHerdSimulateCS); SHADER_USE_PARAMETER_STRUCT(FHerdSimulateCS, FHerdCS); };
IMPLEMENT_GLOBAL_SHADER(FHerdInitializeCS, "/NiagaraHerd/Herd.usf", "InitializeCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FHerdCS, "/NiagaraHerd/Herd.usf", "InitializeCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FHerdBuildGridCS, "/NiagaraHerd/Herd.usf", "BuildGridCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FHerdSimulateCS, "/NiagaraHerd/Herd.usf", "SimulateCS", SF_Compute);

struct FCPUState
{
    uint32 Serial=MAX_uint32;
    TArray<FHerdAgent> Agents;
};
static TMap<FNiagaraSystemInstanceID,TWeakPtr<FCPUState>> CPUStates;
struct FGT
{
    FHerdFrame Frame;
    TSharedPtr<FCPUState> CPU;
};
struct FRT
{
    FHerdFrame Frame;
    int32 Allocated = 0;
    uint32 LastSerial = MAX_uint32;
    TRefCountPtr<FRDGPooledBuffer> Persistent;
    FRDGBufferRef Current = nullptr;
};
static TMap<FNiagaraSystemInstanceID,TWeakPtr<FRT>> GPUStates;

struct FProxy : FNiagaraDataInterfaceProxy
{
    TMap<FNiagaraSystemInstanceID, TSharedPtr<FRT>> Instances;
    virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return 0; }
    virtual void ConsumePerInstanceDataFromGameThread(void*, const FNiagaraSystemInstanceID&) override {}

    virtual void PreStage(const FNDIGpuComputePreStageContext& Context) override
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Herd_GPU_PreStage);
        auto Ref=Instances.FindRef(Context.GetSystemInstanceID()); FRT* R=Ref.Get();
        if (!R || !R->Frame.bGPU || (R->Current && R->LastSerial == R->Frame.Serial)) return;
        FRDGBuilder& G = Context.GetGraphBuilder();
        RDG_GPU_STAT_SCOPE(G,HerdBoids);
        const FHerdFrame F = R->Frame;
        const uint32 Count = FMath::Clamp(F.Count, 1, 4000000);
        const FIntVector Groups(FMath::DivideAndRoundUp(Count, 64u), 1, 1);
        FRDGBufferRef Previous = nullptr;
        if (!R->Persistent || R->Allocated != int32(Count))
        {
            Previous = G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FHerdAgent), Count), TEXT("Herd.InitialAgents"));
            auto* P = G.AllocParameters<FHerdCS::FParameters>();
            P->AgentCount = Count;
            P->OutputAgents = G.CreateUAV(Previous);
            TShaderMapRef<FHerdInitializeCS> S(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            FComputeShaderUtils::AddPass(G, RDG_EVENT_NAME("Herd Initialize %u", Count), S, P, Groups);
            R->Allocated = Count;
        }
        else { Previous = G.RegisterExternalBuffer(R->Persistent); }
        FRDGBufferRef Output = G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FHerdAgent), Count), TEXT("Herd.NextAgents"));
        FRDGBufferRef Counts = G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(4, HerdSim::GridX * HerdSim::GridY), TEXT("Herd.NeighborGridCounts"));
        FRDGBufferRef Indices = G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(4, HerdSim::GridX * HerdSim::GridY * 64), TEXT("Herd.NeighborGridIndices"));
        FRDGBufferRef Summary = G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(4, 2), TEXT("Herd.ScoreSummary"));
        AddClearUAVPass(G, G.CreateUAV(Counts), 0u);
        AddClearUAVPass(G, G.CreateUAV(Summary), 0u);
        auto Params = [&]()
        {
            auto* P = G.AllocParameters<FHerdCS::FParameters>();
            P->AgentCount = Count; P->DeltaTime = FMath::Clamp(F.DeltaTime, 0.f, 1.f / 30.f);
            P->Player = F.Player; P->Goal = F.Goal; P->SimTime = F.Time;
            P->Attract = F.Attract; P->Pulse = F.Pulse; P->Frozen = F.bFrozen;
            P->InputAgents = G.CreateSRV(Previous);
            P->OutputAgents = G.CreateUAV(Output);
            P->GridCounts = G.CreateUAV(Counts); P->GridIndices = G.CreateUAV(Indices); P->Summary = G.CreateUAV(Summary);
            return P;
        };
        TShaderMapRef<FHerdBuildGridCS> Build(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FComputeShaderUtils::AddPass(G, RDG_EVENT_NAME("Herd Neighbor Grid %u", Count), Build, Params(), Groups);
        TShaderMapRef<FHerdSimulateCS> Simulate(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FComputeShaderUtils::AddPass(G, RDG_EVENT_NAME("Herd GPU Boids %u", Count), Simulate, Params(), Groups);
        R->Current = Output; R->LastSerial = F.Serial;
        G.QueueBufferExtraction(Output, &R->Persistent);
        // Read back exactly two uint32s every 6 simulation ticks, asynchronously.
        if (F.Serial % 6 == 0)
        {
            Context.GetComputeDispatchInterface().GetGpuReadbackManager()->EnqueueReadback(G, Summary,
                [F](TConstArrayView<TPair<void*, uint32>> Data)
                {
                    const uint32* Values = reinterpret_cast<const uint32*>(Data[0].Key);
                    const int32 Rescued = Values[0], Overflow = Values[1];
                    FNiagaraWorldManager::EnqueueGlobalDeferredCallback([F, Rescued, Overflow]()
                    {
                        if (AHerdDirector* Owner = F.Owner.Get()) Owner->ReceiveSimulationResult(Rescued, F.Run, F.Serial, Overflow, 0);
                    });
                });
        }
    }
    virtual void PostSimulate(const FNDIGpuComputePostSimulateContext& Context) override
    {
        if (Context.IsFinalPostSimulate())
            if (auto R = Instances.FindRef(Context.GetSystemInstanceID())) R->Current = nullptr;
    }
};
}

UHerdDataInterface::UHerdDataInterface(const FObjectInitializer& O) : Super(O) { Proxy.Reset(new HerdDI::FProxy); }
void UHerdDataInterface::PostInitProperties()
{
    Super::PostInitProperties();
    if (HasAnyFlags(RF_ClassDefaultObject))
        FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter);
}
bool UHerdDataInterface::Equals(const UNiagaraDataInterface* Other) const { return Super::Equals(Other); }
bool UHerdDataInterface::CopyToInternal(UNiagaraDataInterface* D) const { return Super::CopyToInternal(D); }
int32 UHerdDataInterface::PerInstanceDataSize() const { return sizeof(HerdDI::FGT); }
bool UHerdDataInterface::InitPerInstanceData(void* Data, FNiagaraSystemInstance* Instance)
{
    auto* D = new(Data) HerdDI::FGT;
    auto& Weak=HerdDI::CPUStates.FindOrAdd(Instance->GetId()); D->CPU=Weak.Pin();
    if(!D->CPU) { D->CPU=MakeShared<HerdDI::FCPUState>(); Weak=D->CPU; }
    for (TActorIterator<AHerdDirector> It(Instance->GetWorld()); It; ++It) { D->Frame.Owner = *It; break; }
    UE_LOG(LogTemp,Display,TEXT("HERD_DI_INIT %s instance=%p"),*GetPathName(),Instance);
    ENQUEUE_RENDER_COMMAND(HerdAdd)([P=GetProxyAs<HerdDI::FProxy>(), ID=Instance->GetId()](FRHICommandListImmediate&)
    {
        auto& WeakRT=HerdDI::GPUStates.FindOrAdd(ID); auto State=WeakRT.Pin();
        if(!State) { State=MakeShared<HerdDI::FRT>(); WeakRT=State; } P->Instances.Add(ID,State);
    });
    return true;
}
void UHerdDataInterface::DestroyPerInstanceData(void* Data, FNiagaraSystemInstance* Instance)
{
    static_cast<HerdDI::FGT*>(Data)->~FGT();
    if(!HerdDI::CPUStates.FindRef(Instance->GetId()).IsValid()) HerdDI::CPUStates.Remove(Instance->GetId());
    ENQUEUE_RENDER_COMMAND(HerdRemove)([P=GetProxyAs<HerdDI::FProxy>(), ID=Instance->GetId()](FRHICommandListImmediate&)
    { P->Instances.Remove(ID); if(!HerdDI::GPUStates.FindRef(ID).IsValid()) HerdDI::GPUStates.Remove(ID); });
}
bool UHerdDataInterface::PerInstanceTick(void* Data, FNiagaraSystemInstance* Instance, float Dt)
{
    auto* D = static_cast<HerdDI::FGT*>(Data);
    AHerdDirector* Owner = D->Frame.Owner.Get();
    if (!Owner) return false;
    D->Frame = Owner->GetSimulationFrame(Dt);
    if (!D->Frame.bGPU)
    {
        if(D->CPU->Serial==D->Frame.Serial) return false;
        D->CPU->Serial=D->Frame.Serial;
        const double Start = FPlatformTime::Seconds();
        if (D->CPU->Agents.Num() != D->Frame.Count) HerdSim::Initialize(D->CPU->Agents, D->Frame.Count);
        int32 Rescued = HerdSim::Step(D->CPU->Agents, D->Frame);
        Owner->ReceiveSimulationResult(Rescued, D->Frame.Run, D->Frame.Serial, 0, (FPlatformTime::Seconds() - Start) * 1000.0);
    }
    else
    {
        ENQUEUE_RENDER_COMMAND(HerdFrame)([P=GetProxyAs<HerdDI::FProxy>(), ID=Instance->GetId(), F=D->Frame](FRHICommandListImmediate&)
        { if (auto R = P->Instances.FindRef(ID)) R->Frame = F; });
    }
    return false;
}

void UHerdDataInterface::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& Info, void*, FVMExternalFunction& Out)
{
    const FName Name = Info.Name;
    if (Name == TEXT("ReadPosition") || Name == TEXT("ReadVelocity"))
    {
        Out = FVMExternalFunction::CreateLambda([Name](FVectorVMExternalFunctionContext& C)
        {
            VectorVM::FUserPtrHandler<HerdDI::FGT> D(C);
            FNDIInputParam<int32> Index(C); FNDIOutputParam<FVector3f> Value(C);
            int32 MaxIndex=0;
            for (int32 I=0; I<C.GetNumInstances(); ++I)
            {
                int32 J=Index.GetAndAdvance(); FVector3f V(0);
                MaxIndex=FMath::Max(MaxIndex,J);
                if (D->CPU->Agents.IsValidIndex(J)) V=FVector3f(Name==TEXT("ReadPosition") ? D->CPU->Agents[J].Position : D->CPU->Agents[J].Velocity);
                if(Name==TEXT("ReadVelocity") && D->CPU->Agents.IsValidIndex(J) && D->CPU->Agents[J].Position.W>.5f) V=FVector3f(1,0,0);
                Value.SetAndAdvance(V);
            }
            if(Name==TEXT("ReadPosition") && D->Frame.Serial<3) UE_LOG(LogTemp,Display,TEXT("HERD_VM batch=%d sim_count=%d max_index=%d"),C.GetNumInstances(),D->Frame.Count,MaxIndex);
        });
    }
    else if (Name == TEXT("ReadColor"))
    {
        Out = FVMExternalFunction::CreateLambda([](FVectorVMExternalFunctionContext& C)
        {
            VectorVM::FUserPtrHandler<HerdDI::FGT> D(C);
            FNDIInputParam<int32> Index(C); FNDIOutputParam<FLinearColor> Value(C);
            for (int32 I=0; I<C.GetNumInstances(); ++I)
            {
                int32 J=Index.GetAndAdvance(); FLinearColor Color(.15,.75,1,1);
                if (D->CPU->Agents.IsValidIndex(J))
                {
                    const FHerdAgent& A=D->CPU->Agents[J];
                    if (A.Position.W>.5f) Color=FLinearColor(.15,1,.6,1);
                    else if (A.Velocity.W==1) Color=FLinearColor(1,.4,.18,1);
                    else if (A.Velocity.W==2) Color=FLinearColor(.75,.55,1,1);
                    else if (J%17==0) Color=FLinearColor(1,.82,.3,1);
                }
                Value.SetAndAdvance(Color);
            }
        });
    }
}
#if WITH_EDITORONLY_DATA
void UHerdDataInterface::GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& Out) const
{
    for (FName Name : {FName("ReadPosition"), FName("ReadVelocity"), FName("ReadColor")})
    {
        FNiagaraFunctionSignature S; S.Name=Name; S.bMemberFunction=true; S.bRequiresContext=false; S.bSupportsGPU=true;
        S.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Simulation")));
        S.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
        const auto Type = Name==TEXT("ReadColor") ? FNiagaraTypeDefinition::GetColorDef() : Name==TEXT("ReadPosition") ? FNiagaraTypeDefinition::GetPositionDef() : FNiagaraTypeDefinition::GetVec3Def();
        S.Outputs.Add(FNiagaraVariable(Type,TEXT("Value"))); Out.Add(S);
    }
}
bool UHerdDataInterface::AppendCompileHash(FNiagaraCompileHashVisitor* V) const
{
    return Super::AppendCompileHash(V) && V->UpdateString(TEXT("HerdDI"), TEXT("v5-rescue-heading")) && V->UpdateShaderParameters<HerdDI::FReadParameters>();
}
void UHerdDataInterface::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& I, FString& Out)
{
    Out += FString::Printf(TEXT("struct %s_Agent { float4 P; float4 V; }; uint %s_Count; StructuredBuffer<%s_Agent> %s_AgentData;\n"), *I.DataInterfaceHLSLSymbol,*I.DataInterfaceHLSLSymbol,*I.DataInterfaceHLSLSymbol,*I.DataInterfaceHLSLSymbol);
}
bool UHerdDataInterface::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& I, const FNiagaraDataInterfaceGeneratedFunction& F, int, FString& Out)
{
    const FString N=I.DataInterfaceHLSLSymbol;
    if(F.DefinitionName==TEXT("ReadPosition") || F.DefinitionName==TEXT("ReadVelocity"))
    {
        const TCHAR* Member=F.DefinitionName==TEXT("ReadPosition")?TEXT("P"):TEXT("V");
        Out+=FString::Printf(TEXT("void %s(int Index, out float3 Value) { Index=clamp(ExecIndex(),0,int(%s_Count)-1); Value=%s_AgentData[Index].%s.xyz; %s }\n"),*F.InstanceName,*N,*N,Member,F.DefinitionName==TEXT("ReadVelocity")?*FString::Printf(TEXT("if(%s_AgentData[Index].P.w>0.5) Value=float3(1,0,0);"),*N):TEXT("")); return true;
    }
    if(F.DefinitionName==TEXT("ReadColor"))
    {
        Out+=FString::Printf(TEXT("void %s(int Index, out float4 Value) { Index=ExecIndex(); float Saved=%s_AgentData[clamp(Index,0,int(%s_Count)-1)].P.w; Value=Saved>0.5?float4(0.15,1,0.6,1):float4(0.15,0.75,1,1); }\n"),*F.InstanceName,*N,*N); return true;
    }
    return false;
}
#endif
void UHerdDataInterface::BuildShaderParameters(FNiagaraShaderParametersBuilder& B) const { B.AddNestedStruct<HerdDI::FReadParameters>(); }
void UHerdDataInterface::SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& C) const
{
    auto& ProxyData=C.GetProxy<HerdDI::FProxy>();
    auto D=ProxyData.Instances.FindRef(C.GetSystemInstanceID());
    auto* P=C.GetParameterNestedStruct<HerdDI::FReadParameters>();
    P->Count=D?FMath::Max(D->Frame.Count,1):1;
    if(D && D->Current) P->AgentData=C.GetGraphBuilder().CreateSRV(D->Current);
    else
    {
        const FHerdAgent Zero;
        FRDGBufferRef Fallback=CreateStructuredBuffer(C.GetGraphBuilder(),TEXT("Herd.Fallback"),sizeof(FHerdAgent),1,&Zero,sizeof(FHerdAgent));
        P->AgentData=C.GetGraphBuilder().CreateSRV(Fallback);
    }
}
