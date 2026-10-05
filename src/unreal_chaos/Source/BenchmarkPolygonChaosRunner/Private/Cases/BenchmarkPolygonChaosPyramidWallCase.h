#pragma once

#include "pyramid_wall.h"
#include <array>
#include <vector>

#include "Cases/BenchmarkPolygonChaosCaseRegistry.h"
#include "Runner/BenchmarkPolygonChaosCli.h"
#include "Runtime/BenchmarkPolygonChaosConfig.h"
#include "Runtime/BenchmarkPolygonChaosRuntime.h"

#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/PBDRigidsSOAs.h"

namespace BenchmarkPolygonChaos
{
struct ChaosWallBodyInput
{
	Chaos::FVec3 Position;
	Chaos::FRotation3 Rotation;
	Chaos::FVec3 Linear;
	Chaos::FVec3 Angular;
	Chaos::FRotation3 InertiaRotation;
	Chaos::FVec3 Inertia;
	double Mass;
	uint32 SleepFlags;
};

struct ChaosPyramidWallState
{
	CaseExecutionSpec CaseExecution;
	VerificationMode Verification = VerificationMode_On;
	Chaos::FParticleUniqueIndicesMultithreaded UniqueIndices;
	Chaos::FPBDRigidsSOAs Particles;
	Chaos::THandleArray<Chaos::FChaosPhysicsMaterial> PhysicalMaterials;
	TUniquePtr<Chaos::FChaosPhysicsMaterial> Material;
	Chaos::FPBDRigidsEvolutionGBF Evolution;
	TArray<Chaos::FPBDRigidParticleHandle*> DynamicBodies;
	TArray<uint64> RawStepCycles;
	std::array<PyramidWallObservation, 4> Observations = {};
	std::array<std::vector<ChaosWallBodyInput>, 4> ObservationInputs;
	double InitialPotentialEnergy = 0.0;
	double PhysicsElapsedMs = 0.0;
	double LatestPhysicsStepMs = 0.0;
	int CompletedStepCount = 0;
	int CreatedDynamicBodyCount = 0;
	int CreatedStaticBodyCount = 0;

	ChaosPyramidWallState(const CaseExecutionSpec& InCaseExecution, const ThreadRuntimeState& RuntimeState);
};

const ChaosCaseDescriptor& PyramidWallDescriptor();
} // namespace BenchmarkPolygonChaos
