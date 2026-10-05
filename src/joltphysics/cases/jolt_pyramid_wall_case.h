#pragma once

#include "jolt_runner_args.h"

#include "jolt_box_container_pile_10k_case.h"

#include "pyramid_wall.h"
#include <array>
#include <chrono>
#include <cstddef>
#include <vector>

namespace jolt_benchmark
{
struct JoltWallBodyInput
{
	JPH::RVec3 position;
	JPH::Quat rotation;
	JPH::Quat inertiaRotation;
	JPH::Vec3 linear;
	JPH::Vec3 angular;
	JPH::Vec3 inverseInertia;
	float inverseMass;
	std::uint32_t sleepFlags;
	std::uint32_t readStatus;
};
struct JoltPyramidWallState
{
	VerificationMode verificationMode = VerificationMode_On;
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
	std::array<PyramidWallObservation, 4> observations;
	std::array<std::vector<JoltWallBodyInput>, 4> observationInputs;
	double initialPotentialEnergy;
	double physicsElapsedMs;
	double latestPhysicsStepMs;
	int completedStepCount;
};

int CreateJoltPyramidWallState(const JoltCaseConfig& config, JoltPyramidWallState* state, VerificationMode verificationMode = VerificationMode_On);
int WarmupJoltPyramidWallState(JoltPyramidWallState* state, int workUnitCount);
int StepJoltPyramidWallState(JoltPyramidWallState* state, int workUnitCount);
void DestroyJoltPyramidWallState(JoltPyramidWallState* state);
int SampleJoltPyramidWallTransforms(const JoltPyramidWallState& state,
                                     benchmark_visual::VisualStableTransform* transforms, int transformCapacity);
int RunJoltPyramidWallHeadless(const JoltRunRequest& request);
int FormatJoltPyramidWallPhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                          std::size_t settingsCapacity);
const JoltCaseDescriptor& JoltPyramidWallCaseDescriptor();
}
