#pragma once

#include "stack_state_capture.h"

#include "physx34_case_registry.h"
#include "PxPhysicsAPI.h"

#include <chrono>
#include <cstddef>
#include <vector>

namespace physx34_benchmark
{
struct PhysXRunRequest;
struct PhysXCaseDescriptor;

constexpr const char* kEngineId = "physx34";

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
	float centerX;
	float centerY;
	float centerZ;
	float halfX;
	float halfY;
	float halfZ;
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
	physx::PxConvexMesh* convexMesh;
};

int InitializePhysXResolvedShape(PhysXContext* context, const CaseExecutionSpec& execution);
physx::PxQuat PhysXShapeRotation(CaseExecutionAxis axis);
physx::PxShape* AttachPhysXResolvedShape(PhysXContext* context, physx::PxRigidActor* actor,
                                         const CaseExecutionGeometry& geometry);

struct PhysXCaseState
{
	PhysXCaseConfig config;
	PhysXContext context;
	std::vector<physx::PxRigidDynamic*> bodies;
	std::vector<std::chrono::steady_clock::duration::rep> rawStepDurations;
	int completedStepCount;
	double physicsElapsedMs;
	double latestPhysicsStepMs;
};

int RequestedWorkerCount(int threadCount);
int CreatePhysXCaseState(const PhysXCaseConfig& config, PhysXCaseState* state);
int RunPhysXCaseWarmup(PhysXCaseState* state, int warmupStepCount, benchmark_stack::Capture* capture);
int StepPhysXCase(PhysXCaseState* state, int stepCount);
void DestroyPhysXCaseState(PhysXCaseState* state);
int SamplePhysXTransforms(const PhysXCaseState& state, PhysXTransform* transforms, int transformCapacity);
std::uint64_t CountPhysXContainerPileInvalidTransforms(const PhysXCaseState& state);
int RunPhysXContainerPileHeadless(const PhysXRunRequest& request);
int FormatPhysXContainerPilePhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                            std::size_t settingsCapacity);
const PhysXCaseDescriptor& PhysXContainerPileCaseDescriptor();
}
