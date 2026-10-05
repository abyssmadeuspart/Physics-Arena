#pragma once

#include "Cases/BenchmarkPolygonChaosCaseRegistry.h"
#include "Runner/BenchmarkPolygonChaosCli.h"
#include "Runtime/BenchmarkPolygonChaosConfig.h"
#include "Runtime/BenchmarkPolygonChaosRuntime.h"

#include "ragdoll_quality.h"

#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/PBDRigidsSOAs.h"

namespace BenchmarkPolygonChaos
{
struct ChaosRagdollCaseState
{
	CaseExecutionSpec CaseExecution;
	VerificationMode Verification = VerificationMode_On;
	Chaos::FParticleUniqueIndicesMultithreaded UniqueIndices;
	Chaos::FPBDRigidsSOAs Particles;
	Chaos::THandleArray<Chaos::FChaosPhysicsMaterial> PhysicalMaterials;
	Chaos::FPBDRigidsEvolutionGBF Evolution;
	TUniquePtr<Chaos::FChaosPhysicsMaterial> Material;
	TArray<Chaos::FPBDRigidParticleHandle*> DynamicBodies;
	TArray<benchmark_visual::VisualStableTransform> QualityTransforms;
	RagdollQualityAccumulator Quality = {};
	int CompletedStepCount = 0;
	int CreatedDynamicBodyCount = 0;
	int CreatedStaticBodyCount = 0;

	ChaosRagdollCaseState(const CaseExecutionSpec& InCaseExecution, const ThreadRuntimeState& RuntimeState);
};

const ChaosCaseDescriptor& RagdollStairTumbleDescriptor();

} // namespace BenchmarkPolygonChaos
