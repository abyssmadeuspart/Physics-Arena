#pragma once

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

namespace jolt_benchmark
{
constexpr int kPileXCount = 25;
constexpr int kPileYCount = 16;
constexpr int kPileZCount = 25;
constexpr int kDynamicBodyCount = kPileXCount * kPileYCount * kPileZCount;
constexpr int kStaticBodyCount = 5;
constexpr int kBodyCount = kDynamicBodyCount + kStaticBodyCount;
constexpr float kHalfExtent = 0.5f;
constexpr float kSpacing = 1.02f;
constexpr float kInitialY = 24.51f;
constexpr float kTimestep = 1.0f / 60.0f;
constexpr float kOpenContainerLateralEscape = 15.5f;
constexpr float kOpenContainerMaxY = 96.0f;
constexpr const char* kCaseId = "box_container_pile_10k";
constexpr const char* kEngineId = "joltphysics";
constexpr const char* kEngineRef = "3b47ec390cb9b933769183eff599f72195c5873c";
constexpr const char* kFixtureSemantic = "open_container_falling_pile";
constexpr const char* kFixtureVersion = "jolt_open_container_v1";

struct JoltTransform
{
	float positionX;
	float positionY;
	float positionZ;
	float rotationX;
	float rotationY;
	float rotationZ;
	float rotationW;
};

struct JoltStaticBox
{
	float positionX;
	float positionY;
	float positionZ;
	float halfExtentX;
	float halfExtentY;
	float halfExtentZ;
};

struct JoltCaseConfig
{
	int threadCount;
	int repeatIndex;
	int stepCount;
	int warmupSteps;
};

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
	JPH::BodyID dynamicBodies[kDynamicBodyCount];
	double physicsElapsedMs;
	int completedStepCount;
};

void InitializeJoltRuntime();
void ShutdownJoltRuntime();
int RequestedWorkerCount(int threadCount);
int CreateJoltCaseState(const JoltCaseConfig& config, JoltCaseState* state);
int RunJoltCaseWarmup(const JoltCaseConfig& config);
int StepJoltCase(JoltCaseState* state, int stepCount);
void DestroyJoltCaseState(JoltCaseState* state);
int SampleJoltTransforms(const JoltCaseState& state, JoltTransform* transforms, int transformCapacity);
void CopyJoltStaticBoxes(JoltStaticBox* boxes, int boxCapacity);
}
