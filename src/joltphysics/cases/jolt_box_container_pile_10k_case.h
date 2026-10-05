#pragma once

#include "stack_state_capture.h"

#include "jolt_case_registry.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <chrono>
#include <cstddef>
#include <vector>

namespace jolt_benchmark
{
struct JoltRunRequest;

constexpr const char* kEngineId = "joltphysics";

namespace Layers
{
static constexpr JPH::ObjectLayer NON_MOVING = 0;
static constexpr JPH::ObjectLayer MOVING = 1;
static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
{
  public:
	bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override;
};

namespace BroadPhaseLayers
{
static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
static constexpr JPH::BroadPhaseLayer MOVING(1);
static constexpr JPH::uint NUM_LAYERS(2);
}

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
  public:
	BPLayerInterfaceImpl();
	JPH::uint GetNumBroadPhaseLayers() const override;
	JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override;
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override;
#endif
	JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
{
  public:
	bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override;
};

struct JoltCaseState
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

int CreateJoltCaseState(const JoltCaseConfig& config, JoltCaseState* state);
int RunJoltCaseWarmup(const JoltCaseConfig& config, benchmark_stack::Capture* capture);
int StepJoltCase(JoltCaseState* state, int stepCount);
void DestroyJoltCaseState(JoltCaseState* state);
int SampleJoltTransforms(const JoltCaseState& state, benchmark_visual::VisualStableTransform* transforms,
                         int transformCapacity);
std::uint64_t CountJoltContainerPileInvalidTransforms(const JoltCaseState& state);
int RunJoltContainerPileHeadless(const JoltRunRequest& request);
int FormatJoltContainerPilePhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                           std::size_t settingsCapacity);
const JoltCaseDescriptor& JoltContainerPileCaseDescriptor();
}
