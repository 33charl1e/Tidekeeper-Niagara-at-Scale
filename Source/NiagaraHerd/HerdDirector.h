#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "HerdSimulation.h"
#include "HerdDirector.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UMaterialInstanceDynamic;
class FParticlePerfStatsListener_GatherAll;
class FFrameGrabber;

UCLASS()
class NIAGARAHERD_API AHerdDirector : public AActor
{
    GENERATED_BODY()
public:
    AHerdDirector();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;
    FHerdFrame GetSimulationFrame(float Dt) const;
    void ReceiveSimulationResult(int32 Count, int32 Run, uint32 Serial, int32 Overflow, double CpuMs);
    void Restart(bool bGPU, int32 Count);
    void BeginBenchmark();

    UPROPERTY(VisibleAnywhere) TObjectPtr<UNiagaraComponent> Flock;
    UPROPERTY() TObjectPtr<UNiagaraSystem> CPUAsset;
    UPROPERTY() TObjectPtr<UNiagaraSystem> GPUAsset;
    UPROPERTY() TObjectPtr<USceneComponent> Beacon;
    bool bGPU = false;
    bool bWon = false;
    bool bLost = false;
    bool bAttract = false;
    bool bPaused = false;
    bool bAuto = false;
    bool bBenchmark = false;
    bool bReady = false;
    int32 ParticleCount = 1000;
    int32 SavedCount = 0;
    int32 RunID = 0;
    int32 Overflow = 0;
    uint32 SimulationSerial = 0;
    uint32 ResultSerial = 0;
    int32 ResultUpdates = 0;
    double CpuSimulationMs = 0;
    float Remaining = 120;
    float Elapsed = 0;
    float PulseUntil = -10;
    float PulseReady = 0;
    float FrameMs = 0;
    FString Status;
    bool bCapture = false;
    int32 RenderedCount = 0;
    FVector PlayerPosition = FVector(-1320, 0, 35);
private:
    void CreateArena();
    void SetCountFromKeys(int32 Direction);
    void UpdateBenchmark(float Dt);
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    int32 BenchmarkStage = -1;
    float StageTime = 0;
    TArray<FString> BenchmarkRows;
    TArray<float> FrameSamples;
    TArray<float> CpuSamples;
    float RealRuntime = 0;
    float DiagnosticAt = 0;
    bool bTestScreenshot = false;
    bool bTestSwitched = false;
    bool bSpawnLocked = false;
    FString BenchmarkRegion;
    FString CaptureName;
    int32 CaptureFrame = 0;
    bool bDemoGPU = false;
    bool bDemoStats = false;
    TSharedPtr<FParticlePerfStatsListener_GatherAll,ESPMode::ThreadSafe> PerformanceListener;
    TSharedPtr<FFrameGrabber> CaptureGrabber;
};

UCLASS()
class NIAGARAHERD_API AHerdGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AHerdGameMode();
    virtual void StartPlay() override;
};

UCLASS()
class NIAGARAHERD_API AHerdHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
