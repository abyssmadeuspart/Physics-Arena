#pragma once

#include "jolt_box_container_pile_10k_case.h"

#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <chrono>
#include <cstddef>
#include <vector>

namespace jolt_benchmark
{
struct JoltLargePyramidState
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
	std::vector<JPH::BodyID> dynamicBodies;
	std::vector<std::chrono::steady_clock::duration::rep> rawStepDurations;
	double physicsElapsedMs;
	double latestPhysicsStepMs;
	int completedStepCount;
};

int CreateJoltLargePyramidState(const JoltCaseConfig& config, JoltLargePyramidState* state);
int WarmupJoltLargePyramidState(JoltLargePyramidState* state, int workUnitCount);
int StepJoltLargePyramidState(JoltLargePyramidState* state, int workUnitCount);
void DestroyJoltLargePyramidState(JoltLargePyramidState* state);
int SampleJoltLargePyramidTransforms(const JoltLargePyramidState& state,
                                     benchmark_visual::VisualStableTransform* transforms, int transformCapacity);
std::uint64_t CountJoltLargePyramidInvalidTransforms(const JoltLargePyramidState& state);
int RunJoltLargePyramidHeadless(const JoltRunRequest& request);
int FormatJoltLargePyramidPhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                          std::size_t settingsCapacity);
const JoltCaseDescriptor& JoltLargePyramidCaseDescriptor();
}
