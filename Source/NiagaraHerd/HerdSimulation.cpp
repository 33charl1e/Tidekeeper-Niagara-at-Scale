#include "HerdSimulation.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace HerdSim
{
static FVector3f Unit(FVector3f V) { return V.GetSafeNormal(); }
static int32 CellIndex(const FVector4f& P)
{
    return FMath::Clamp(int32((P.X + HalfX) / Cell), 0, GridX - 1)
        + GridX * FMath::Clamp(int32((P.Y + HalfY) / Cell), 0, GridY - 1);
}

void Initialize(TArray<FHerdAgent>& Agents, int32 Count)
{
    Agents.SetNum(FMath::Clamp(Count, 1, 4000000));
    FRandomStream Random(731);
    for (FHerdAgent& A : Agents)
    {
        A.Position = FVector4f(Random.FRandRange(-1250, -650), Random.FRandRange(-650, 650), 35, 0);
        float Angle = Random.FRandRange(-PI, PI);
        A.Velocity = FVector4f(FMath::Cos(Angle) * 115, FMath::Sin(Angle) * 115, 0, 0);
    }
}

int32 Step(TArray<FHerdAgent>& Agents, const FHerdFrame& F)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(Herd_CPU_Simulate);
    const int32 Count = Agents.Num();
    if (!Count) return 0;
    const float Dt = FMath::Clamp(F.DeltaTime, 0.f, 1.f / 30.f);
    TArray<int32> Heads, Next;
    if (F.bSpatialHash)
    {
        Heads.Init(INDEX_NONE, GridX * GridY);
        Next.SetNumUninitialized(Count);
        for (int32 I = 0; I < Count; ++I)
        {
            int32 C = CellIndex(Agents[I].Position);
            Next[I] = Heads[C]; Heads[C] = I;
        }
    }
    TArray<FHerdAgent> Result;
    Result.SetNumUninitialized(Count);
    int32 Rescued = 0;
    for (int32 I = 0; I < Count; ++I)
    {
        const FHerdAgent& A = Agents[I];
        FVector3f P(A.Position), V(A.Velocity);
        if (A.Position.W > .5f)
        {
            Result[I] = A; ++Rescued; continue;
        }
        FVector3f Separation(0), Alignment(0), Center(0);
        int32 Neighbors = 0;
        const FVector3f Forward = Unit(V);
        auto Visit = [&](int32 J)
        {
            if (J == I || Agents[J].Position.W > .5f) return;
            FVector3f D = FVector3f(Agents[J].Position) - P;
            const float D2 = D.SizeSquared();
            if (D2 < 1.f || D2 > 110.f * 110.f) return;
            Separation -= D / FMath::Max(D2, 16.f);
            if (FVector3f::DotProduct(Forward, Unit(D)) > -.3f)
            {
                Alignment += FVector3f(Agents[J].Velocity);
                Center += FVector3f(Agents[J].Position); ++Neighbors;
            }
        };
        if (F.bSpatialHash)
        {
            const int32 C = CellIndex(A.Position), X = C % GridX, Y = C / GridX;
            for (int32 DY = -1; DY <= 1; ++DY)
                for (int32 DX = -1; DX <= 1; ++DX)
                {
                    const int32 XX = X + DX, YY = Y + DY;
                    if (XX < 0 || XX >= GridX || YY < 0 || YY >= GridY) continue;
                    for (int32 J = Heads[XX + YY * GridX]; J != INDEX_NONE; J = Next[J]) Visit(J);
                }
        }
        else { for (int32 J = 0; J < Count; ++J) Visit(J); }
        FVector3f Acceleration = Unit(Separation) * 135.f;
        if (Neighbors)
        {
            Acceleration += (Unit(Alignment / float(Neighbors)) * 145.f - V) * .7f;
            Acceleration += Unit(Center / float(Neighbors) - P) * 65.f;
        }
        FVector3f Away = P - F.Player;
        const float Distance = Away.Size();
        int32 State = 0; // graze, flee, follow
        float Speed = 130.f;
        if (F.Attract > .5f && Distance < 850.f)
        {
            State = 2; Speed = 210;
            if (Distance > 100) Acceleration -= Unit(Away) * 440.f;
        }
        else if (Distance < 340.f + F.Pulse * 260.f)
        {
            State = 1; Speed = 270;
            Acceleration += Unit(Away) * (650.f + F.Pulse * 950.f);
        }
        // Mesh agents have perception, states, leader roles and obstacle avoidance.
        if (I % 17 == 0) { Speed *= 1.12f; Acceleration.Y += FMath::Sin(F.Time * .7f + I) * 45.f; }
        for (FVector3f Obstacle : {FVector3f(-180, 290, 35), FVector3f(260, -320, 35)})
        {
            FVector3f D = P - Obstacle;
            if (D.SizeSquared() < 240.f * 240.f) Acceleration += Unit(D) * 850.f;
        }
        if (P.X < -1360) Acceleration.X += 700;
        if (P.X > 1360) Acceleration.X -= 700;
        if (P.Y < -760) Acceleration.Y += 700;
        if (P.Y > 760) Acceleration.Y -= 700;
        if (!F.bFrozen)
        {
            V += Acceleration * Dt;
            V = Unit(V) * FMath::Clamp(V.Size(), 70.f, Speed);
            P += V * Dt;
        }
        P.X = FMath::Clamp(P.X, -1470.f, 1470.f);
        P.Y = FMath::Clamp(P.Y, -870.f, 870.f); P.Z = 35;
        const bool Captured = FVector3f::DistSquared(P, F.Goal) < FMath::Square(GoalRadius);
        if (Captured) { ++Rescued; V = FVector3f(0); }
        Result[I].Position = FVector4f(P, Captured ? 1.f : 0.f);
        Result[I].Velocity = FVector4f(V, float(State));
    }
    Agents = MoveTemp(Result);
    return Rescued;
}
}
