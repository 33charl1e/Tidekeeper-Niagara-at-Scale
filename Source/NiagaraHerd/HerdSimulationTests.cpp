#include "HerdSimulation.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHerdHashTest,"NiagaraHerd.Simulation.HashMatchesAllPairs",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHerdHashTest::RunTest(const FString&)
{
    TArray<FHerdAgent> A,B; HerdSim::Initialize(A,500); B=A;
    FHerdFrame F; F.DeltaTime=1.f/60; F.Player=FVector3f(-800,150,35); F.Attract=1;
    for(int32 Tick=0;Tick<8;++Tick)
    {
        F.bSpatialHash=true; const int32 CA=HerdSim::Step(A,F);
        F.bSpatialHash=false; const int32 CB=HerdSim::Step(B,F);
        TestEqual(TEXT("Capture counts match"),CA,CB);
    }
    float MaxError=0;
    for(int32 I=0;I<A.Num();++I) MaxError=FMath::Max(MaxError,FVector3f::Dist(FVector3f(A[I].Position),FVector3f(B[I].Position)));
    TestTrue(TEXT("Spatial hash preserves trajectories within floating point summation tolerance"),MaxError<.01f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHerdCaptureTest,"NiagaraHerd.Simulation.CaptureIsAbsorbing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHerdCaptureTest::RunTest(const FString&)
{
    TArray<FHerdAgent> A; HerdSim::Initialize(A,1); FHerdFrame F; F.DeltaTime=1.f/60;
    A[0].Position=FVector4f(F.Goal,0);
    TestEqual(TEXT("An actual agent entering the goal counts"),HerdSim::Step(A,F),1);
    const FVector4f Captured=A[0].Position;
    F.Player=F.Goal; F.Pulse=1;
    TestEqual(TEXT("Captured agent remains counted"),HerdSim::Step(A,F),1);
    TestTrue(TEXT("Captured agent does not escape"),A[0].Position.Equals(Captured));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHerdInputTest,"NiagaraHerd.Simulation.InputChangesTrajectory",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHerdInputTest::RunTest(const FString&)
{
    TArray<FHerdAgent> A,B; HerdSim::Initialize(A,200); B=A;
    FHerdFrame F; F.Player=FVector3f(-950,0,35); F.DeltaTime=1.f/60;
    F.Attract=1; HerdSim::Step(A,F); F.Attract=0; F.Pulse=1; HerdSim::Step(B,F);
    int32 Different=0;
    for(int32 I=0;I<A.Num();++I) if(!A[I].Velocity.Equals(B[I].Velocity,.01f)) ++Different;
    TestTrue(TEXT("Attract and repel produce different simulation results"),Different>0);
    return true;
}
#endif
