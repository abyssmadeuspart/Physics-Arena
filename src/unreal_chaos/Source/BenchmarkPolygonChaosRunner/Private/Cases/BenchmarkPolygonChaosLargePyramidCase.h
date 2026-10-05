#pragma once

#include "Cases/BenchmarkPolygonChaosCaseRegistry.h"
#include "Runner/BenchmarkPolygonChaosCli.h"
#include "Runtime/BenchmarkPolygonChaosConfig.h"
#include "Runtime/BenchmarkPolygonChaosRuntime.h"

#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/PBDRigidsSOAs.h"

namespace BenchmarkPolygonChaos
{
struct ChaosLargePyramidState
{
	CaseExecutionSpec CaseExecution;
	Chaos::FParticleUniqueIndicesMultithreaded UniqueIndices;
	Chaos::FPBDRigidsSOAs Particles;
	Chaos::THandleArray<Chaos::FChaosPhysicsMaterial> PhysicalMaterials;
	Chaos::FPBDRigidsEvolutionGBF Evolution;
	TUniquePtr<Chaos::FChaosPhysicsMaterial> Material;
	TArray<Chaos::FPBDRigidParticleHandle*> DynamicBodies;
	TArray<uint64> RawStepCycles;
	double PhysicsElapsedMs = 0.0;
	double LatestPhysicsStepMs = 0.0;
	int CompletedStepCount = 0;
	int CreatedDynamicBodyCount = 0;
	int CreatedStaticBodyCount = 0;

	ChaosLargePyramidState(const CaseExecutionSpec& InCaseExecution, const ThreadRuntimeState& RuntimeState);
};

const ChaosCaseDescriptor& LargePyramidDescriptor();
} // namespace BenchmarkPolygonChaos
