#include "Cases/BenchmarkPolygonChaosCase.h"

#include "Output/BenchmarkPolygonChaosOutput.h"

#include "Chaos/Box.h"
#include "Chaos/CollisionFilterData.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/ShapeInstance.h"
#include "HAL/PlatformTime.h"

#include <cstdio>

namespace BenchmarkPolygonChaos
{

    ChaosCaseState::ChaosCaseState(const ThreadRuntimeState& RuntimeState)
        : Particles(UniqueIndices),
          Evolution(Particles,
                    PhysicalMaterials,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr,
                    ThreadExecutionModeForThreadCount(RuntimeState.RequestedThreadCount) ==
                        ThreadExecutionMode::SingleThreaded)
    {
        DynamicBodies.Reserve(kDynamicBodyCount);
    }

    void SetShapeFilterToCollide(Chaos::FPerShapeData& Shape)
    {
        const Chaos::Filter::FShapeFilterData ShapeFilterData =
            Chaos::Filter::FShapeFilterBuilder::BuildBlockAll(Chaos::EFilterFlags::SimpleCollision);
        Shape.SetShapeFilterData(ShapeFilterData);
    }

    void SetParticleShapesToCollide(Chaos::FGeometryParticleHandle* Particle)
    {
        for (const TUniquePtr<Chaos::FPerShapeData>& Shape : Particle->ShapesArray())
        {
            SetShapeFilterToCollide(*Shape);
        }
    }

    void InitEvolutionSettings(Chaos::FPBDRigidsEvolutionGBF& Evolution)
    {
        Evolution.SetNumPositionIterations(kSolverPositionIterations);
        Evolution.SetNumVelocityIterations(kSolverVelocityIterations);
        Evolution.SetNumProjectionIterations(kSolverProjectionIterations);

        Chaos::FCollisionDetectorSettings DetectorSettings =
            Evolution.GetCollisionConstraints().GetDetectorSettings();
        DetectorSettings.BoundsExpansion = 3.0f;
        DetectorSettings.bDeferNarrowPhase = false;
        DetectorSettings.bAllowManifolds = true;
        Evolution.GetCollisionConstraints().SetDetectorSettings(DetectorSettings);
        Evolution.GetGravityForces().SetAcceleration(Chaos::FVec3(0.0, -10.0, 0.0), 0);
    }

    void SetParticleBounds(Chaos::FGeometryParticleHandle* Particle,
                           const Chaos::FVec3& HalfExtents)
    {
        Particle->SetLocalBounds(Chaos::FAABB3(-HalfExtents, HalfExtents));
        Particle->UpdateWorldSpaceState(Chaos::FRigidTransform3(Particle->GetX(), Particle->GetR()),
                                        Chaos::FVec3(0));
        Particle->SetHasBounds(true);
    }

    void AddStaticBox(ChaosCaseState* State, const StaticBox& Box)
    {
        Chaos::FGeometryParticleHandle* Particle = State->Evolution.CreateStaticParticles(1)[0];
        const Chaos::FVec3 HalfExtents(Box.HalfExtentX, Box.HalfExtentY, Box.HalfExtentZ);
        Particle->SetGeometry(
            Chaos::FImplicitObjectPtr(new Chaos::TBox<Chaos::FReal, 3>(-HalfExtents, HalfExtents)));
        Particle->SetX(Chaos::FVec3(Box.PositionX, Box.PositionY, Box.PositionZ));
        Particle->SetR(Chaos::FRotation3::FromIdentity());
        SetParticleBounds(Particle, HalfExtents);
        SetParticleShapesToCollide(Particle);
        State->Evolution.EnableParticle(Particle);
    }

    void AddDynamicBox(ChaosCaseState* State, const Chaos::FVec3& Position)
    {
        Chaos::FPBDRigidParticleHandle* Particle = State->Evolution.CreateDynamicParticles(1)[0];
        const Chaos::FVec3 HalfExtents(kHalfExtent, kHalfExtent, kHalfExtent);
        Particle->SetGeometry(
            Chaos::FImplicitObjectPtr(new Chaos::TBox<Chaos::FReal, 3>(-HalfExtents, HalfExtents)));
        Particle->SetX(Position);
        Particle->SetP(Position);
        Particle->SetR(Chaos::FRotation3::FromIdentity());
        Particle->SetQ(Chaos::FRotation3::FromIdentity());
        Particle->SetV(Chaos::FVec3(0));
        Particle->SetW(Chaos::FVec3(0));
        Particle->M() = 1.0;
        Particle->InvM() = 1.0;
        Particle->I() = Chaos::TVec3<Chaos::FRealSingle>(1.0f / 6.0f);
        Particle->InvI() = Chaos::TVec3<Chaos::FRealSingle>(6.0f);
        Particle->SetGravityEnabled(true);
        Particle->SetSleepType(Chaos::ESleepType::NeverSleep);
        State->Evolution.SetPhysicsMaterial(Particle, MakeSerializable(State->Material));
        SetParticleBounds(Particle, HalfExtents);
        SetParticleShapesToCollide(Particle);
        State->Evolution.EnableParticle(Particle);
        State->DynamicBodies.Add(Particle);
    }

    int CreateChaosCaseState(ChaosCaseState* State)
    {
        InitEvolutionSettings(State->Evolution);
        State->Material = MakeUnique<Chaos::FChaosPhysicsMaterial>();
        State->Material->Friction = 0.5f;
        State->Material->Restitution = 0.0f;

        for (const StaticBox& Box : kStaticBoxes)
        {
            AddStaticBox(State, Box);
        }

        const double OriginX = -0.5 * static_cast<double>(kPileXCount - 1) * kSpacing;
        const double OriginZ = -0.5 * static_cast<double>(kPileZCount - 1) * kSpacing;
        for (int Y = 0; Y < kPileYCount; ++Y)
        {
            for (int Z = 0; Z < kPileZCount; ++Z)
            {
                for (int X = 0; X < kPileXCount; ++X)
                {
                    AddDynamicBox(State,
                                  Chaos::FVec3(OriginX + static_cast<double>(X) * kSpacing,
                                               kInitialY + static_cast<double>(Y) * kSpacing,
                                               OriginZ + static_cast<double>(Z) * kSpacing));
                }
            }
        }

        return State->DynamicBodies.Num() == kDynamicBodyCount ? 0 : 2;
    }

    void StepChaosCase(ChaosCaseState* State, int StepCount, StepTimingMode TimingMode)
    {
        for (int Step = 0; Step < StepCount; ++Step)
        {
            const double Start = FPlatformTime::Seconds();
            State->Evolution.AdvanceOneTimeStep(kTimestep);
            State->Evolution.EndFrame(kTimestep);
            const double End = FPlatformTime::Seconds();
            if (TimingMode == StepTimingMode::Timed)
            {
                State->PhysicsElapsedMs += (End - Start) * 1000.0;
            }
            State->CompletedStepCount += 1;
        }
    }

    int RunWarmup(int WarmupSteps, const ThreadRuntimeState& RuntimeState)
    {
        if (WarmupSteps <= 0)
        {
            return 0;
        }

        ChaosCaseState WarmupState(RuntimeState);
        if (CreateChaosCaseState(&WarmupState) != 0)
        {
            return 2;
        }

        StepChaosCase(&WarmupState, WarmupSteps, StepTimingMode::Untimed);
        return 0;
    }

    int RunHeadless(const RunnerArgs& Args, const ThreadRuntimeState& RuntimeState)
    {
        if (RunWarmup(Args.WarmupSteps, RuntimeState) != 0)
        {
            std::fprintf(stderr, "run_failed reason=create_warmup_fixture\n");
            return 2;
        }

        ChaosCaseState State(RuntimeState);
        if (CreateChaosCaseState(&State) != 0)
        {
            std::fprintf(stderr, "run_failed reason=create_fixture\n");
            return 2;
        }

        StepChaosCase(&State, Args.StepCount, StepTimingMode::Timed);
        return WriteResult(Args, RuntimeState, State);
    }

} // namespace BenchmarkPolygonChaos
