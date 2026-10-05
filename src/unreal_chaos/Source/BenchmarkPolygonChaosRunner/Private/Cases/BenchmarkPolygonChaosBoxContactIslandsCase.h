#pragma once

#include "Runner/BenchmarkPolygonChaosCli.h"
#include "Cases/BenchmarkPolygonChaosCaseRegistry.h"
#include "Runtime/BenchmarkPolygonChaosConfig.h"
#include "Runtime/BenchmarkPolygonChaosRuntime.h"

#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/PBDRigidsSOAs.h"

namespace BenchmarkPolygonChaos
{

struct ChaosContactIslandsState
{
	CaseExecutionSpec CaseExecution;
	Chaos::FParticleUniqueIndicesMultithreaded UniqueIndices;
	Chaos::FPBDRigidsSOAs Particles;
	Chaos::THandleArray<Chaos::FChaosPhysicsMaterial> PhysicalMaterials;
	Chaos::FPBDRigidsEvolutionGBF Evolution;
	TUniquePtr<Chaos::FChaosPhysicsMaterial> Material;
	TArray<Chaos::FPBDRigidParticleHandle*> DynamicBodies;
	TArray<uint64> RawWorkUnitCycles;
	double WorkloadElapsedMs = 0.0;
	double LatestWorkUnitElapsedMs = 0.0;
	int CompletedWorkUnitCount = 0;

	ChaosContactIslandsState(const CaseExecutionSpec& InCaseExecution, const ThreadRuntimeState& RuntimeState);
};

int CreateContactIslandsState(ChaosContactIslandsState* State);
int WarmupContactIslands(ChaosContactIslandsState* State, int WorkUnitCount);
int StepContactIslandsTimed(ChaosContactIslandsState* State, int WorkUnitCount);
int ValidateContactIslands(const ChaosContactIslandsState& State, uint64* InvalidTransformCount);
int RunContactIslandsHeadless(const RunnerArgs& Args, const ThreadRuntimeState& RuntimeState);
int BuildContactIslandsVisualScene(const ChaosCaseView& State, ChaosVisualGeometry* Geometries,

                                   benchmark_visual::VisualMeshStorage* Meshes, int GeometryCapacity,
                                   ChaosVisualInstance* Instances, int InstanceCapacity, int* GeometryCount,
                                   int* InstanceCount);
int SampleContactIslandsVisualTransforms(const ChaosCaseView& State, ChaosVisualStableTransform* Transforms,
                                         int Capacity);
int FormatContactIslandsVisualPhysicsSettings(const CaseExecutionSpec& CaseExecution,
                                              const ThreadRuntimeState& RuntimeState, char* Settings, int Capacity);
const ChaosCaseDescriptor& ContactIslandsDescriptor();

} // namespace BenchmarkPolygonChaos
