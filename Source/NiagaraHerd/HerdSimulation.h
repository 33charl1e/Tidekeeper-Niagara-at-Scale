#pragma once
#include "CoreMinimal.h"

class AHerdDirector;

// Identical 32-byte layout in C++ and HLSL. Position.W is an absorbing rescue flag.
struct FHerdAgent
{
    FVector4f Position = FVector4f(0, 0, 35, 0);
    FVector4f Velocity = FVector4f(0, 0, 0, 0);
};
static_assert(sizeof(FHerdAgent) == 32);

struct FHerdFrame
{
    FVector3f Player = FVector3f(-1300, 0, 35);
    FVector3f Goal = FVector3f(1160, 0, 35);
    int32 Count = 1000;
    int32 Run = 0;
    uint32 Serial = 0;
    float DeltaTime = 0;
    float Time = 0;
    float Attract = 0;
    float Pulse = 0;
    bool bSpatialHash = true;
    bool bGPU = false;
    bool bFrozen = false;
    TWeakObjectPtr<AHerdDirector> Owner;
};

namespace HerdSim
{
    constexpr float HalfX = 1500.f;
    constexpr float HalfY = 900.f;
    constexpr float Cell = 120.f;
    constexpr int32 GridX = 25;
    constexpr int32 GridY = 15;
    constexpr float GoalRadius = 240.f;
    NIAGARAHERD_API void Initialize(TArray<FHerdAgent>& Agents, int32 Count);
    NIAGARAHERD_API int32 Step(TArray<FHerdAgent>& Agents, const FHerdFrame& Frame);
}
