#pragma once

#include "Runner/BenchmarkPolygonChaosCli.h"
#include "Runtime/BenchmarkPolygonChaosConfig.h"
#include "Runtime/BenchmarkPolygonChaosRuntime.h"

#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/PBDRigidsSOAs.h"

namespace BenchmarkPolygonChaos
{

    struct ChaosCaseState
    {
        Chaos::FParticleUniqueIndicesMultithreaded UniqueIndices;
        Chaos::FPBDRigidsSOAs Particles;
        Chaos::THandleArray<Chaos::FChaosPhysicsMaterial> PhysicalMaterials;
        Chaos::FPBDRigidsEvolutionGBF Evolution;
        TUniquePtr<Chaos::FChaosPhysicsMaterial> Material;
        TArray<Chaos::FPBDRigidParticleHandle*> DynamicBodies;
        double PhysicsElapsedMs = 0.0;
        int CompletedStepCount = 0;

        ChaosCaseState(const ThreadRuntimeState& RuntimeState);
    };

    int CreateChaosCaseState(ChaosCaseState* State);
    void StepChaosCase(ChaosCaseState* State, int StepCount, StepTimingMode TimingMode);
    int RunWarmup(int WarmupSteps, const ThreadRuntimeState& RuntimeState);
    int RunHeadless(const RunnerArgs& Args, const ThreadRuntimeState& RuntimeState);

} // namespace BenchmarkPolygonChaos
