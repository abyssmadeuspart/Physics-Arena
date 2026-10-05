#pragma once

#include "Runner/BenchmarkPolygonChaosCli.h"
#include "Cases/BenchmarkPolygonChaosCaseRegistry.h"
#include "Runtime/BenchmarkPolygonChaosConfig.h"
#include "Runtime/BenchmarkPolygonChaosRuntime.h"

#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/PBDRigidsSOAs.h"

namespace BenchmarkPolygonChaos
{

struct ChaosCaseState
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

	ChaosCaseState(const CaseExecutionSpec& InCaseExecution, const ThreadRuntimeState& RuntimeState);
};

int CreateChaosCaseState(ChaosCaseState* State);
void StepChaosCaseUntimed(ChaosCaseState* State, int StepCount);
int StepChaosCaseTimed(ChaosCaseState* State, int StepCount);
uint64 CountContainerInvalidTransforms(const ChaosCaseState& State);
int RunWarmup(const RunnerArgs& Args, const ThreadRuntimeState& RuntimeState,
              benchmark_stack::Capture* Capture, ChaosVisualStableTransform* Transforms);
int RunHeadless(const RunnerArgs& Args, const ThreadRuntimeState& RuntimeState);
int BuildContainerVisualScene(const ChaosCaseView& State, ChaosVisualGeometry* Geometries,

                              benchmark_visual::VisualMeshStorage* Meshes, int GeometryCapacity,
                              ChaosVisualInstance* Instances, int InstanceCapacity, int* GeometryCount,
                              int* InstanceCount);
int SampleContainerVisualTransforms(const ChaosCaseView& State, ChaosVisualStableTransform* Transforms, int Capacity);
int FormatContainerVisualPhysicsSettings(const CaseExecutionSpec& CaseExecution, const ThreadRuntimeState& RuntimeState,
                                         char* Settings, int Capacity);
const ChaosCaseDescriptor& ContainerPileDescriptor();

} // namespace BenchmarkPolygonChaos
