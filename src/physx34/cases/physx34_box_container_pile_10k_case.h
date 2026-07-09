#pragma once

#include "PxPhysicsAPI.h"

namespace physx34_benchmark
{
constexpr const char* kEngineId = "physx34";
constexpr const char* kEngineRef = "2b13cae09734616d07d09ecf645326fa0bf43ef7";
constexpr const char* kCaseId = "box_container_pile_10k";
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
constexpr const char* kFixtureSemantic = "open_container_falling_pile";
constexpr const char* kFixtureVersion = "physx34_open_container_v1";

struct PhysXCaseConfig
{
	int threadCount;
	int repeatIndex;
	int stepCount;
	int warmupSteps;
};

struct PhysXTransform
{
	float positionX;
	float positionY;
	float positionZ;
	float rotationX;
	float rotationY;
	float rotationZ;
	float rotationW;
};

struct PhysXStaticBox
{
	float positionX;
	float positionY;
	float positionZ;
	float halfExtentX;
	float halfExtentY;
	float halfExtentZ;
};

struct PhysXContext
{
	physx::PxDefaultAllocator allocator;
	physx::PxDefaultErrorCallback errorCallback;
	physx::PxFoundation* foundation;
	physx::PxPhysics* physics;
	physx::PxDefaultCpuDispatcher* dispatcher;
	physx::PxScene* scene;
	physx::PxMaterial* material;
};

struct PhysXCaseState
{
	PhysXCaseConfig config;
	PhysXContext context;
	physx::PxRigidDynamic* bodies[kDynamicBodyCount];
	int completedStepCount;
	double physicsElapsedMs;
};

int RequestedWorkerCount(int threadCount);
int CreatePhysXCaseState(const PhysXCaseConfig& config, PhysXCaseState* state);
int RunPhysXCaseWarmup(const PhysXCaseConfig& config);
int StepPhysXCase(PhysXCaseState* state, int stepCount);
void DestroyPhysXCaseState(PhysXCaseState* state);
int SamplePhysXTransforms(const PhysXCaseState& state, PhysXTransform* transforms, int transformCapacity);
int CopyPhysXStaticBoxes(PhysXStaticBox* boxes, int boxCapacity);
}
