#pragma once

#include "jolt_box_container_pile_10k_case.h"

#include <cstddef>
#include <cstdint>

namespace jolt_benchmark
{
struct JoltRunRequest;

struct JoltContactIslandsState
{
	JoltCaseConfig config;
	JPH::TempAllocatorImpl* tempAllocator;
	JPH::JobSystemSingleThreaded* singleThreaded;
	JPH::JobSystemThreadPool* threadPool;
	JPH::JobSystem* selectedJobSystem;
	BPLayerInterfaceImpl broadPhaseLayerInterface;
	ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
	ObjectLayerPairFilterImpl objectVsObjectLayerFilter;
	JPH::PhysicsSystem physicsSystem;
	JPH::BodyID* dynamicBodies;
	std::chrono::steady_clock::duration::rep* rawWorkUnitDurations;
	double workloadElapsedMs;
	double latestWorkUnitElapsedMs;
	int completedWorkUnitCount;
};

int CreateJoltContactIslandsState(const JoltCaseConfig& config, JoltContactIslandsState* state);
int WarmupJoltContactIslands(JoltContactIslandsState* state, int workUnitCount);
int StepJoltContactIslands(JoltContactIslandsState* state, int workUnitCount);
void DestroyJoltContactIslandsState(JoltContactIslandsState* state);
int SampleJoltContactIslandsTransforms(const JoltContactIslandsState& state,
                                       benchmark_visual::VisualStableTransform* transforms, int transformCapacity);
int ValidateJoltContactIslands(const JoltContactIslandsState& state, std::uint64_t* invalidTransformCount);
int RunJoltContactIslandsHeadless(const JoltRunRequest& request);
int FormatJoltContactIslandsPhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                            std::size_t settingsCapacity);
const JoltCaseDescriptor& JoltContactIslandsCaseDescriptor();
}
