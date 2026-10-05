#pragma once

#include "jolt_runner_args.h"

#include "jolt_box_container_pile_10k_case.h"

#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>

#include "ragdoll_quality.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace jolt_benchmark
{
struct JoltRunRequest;

constexpr const char* kJoltRagdollEngineId = "joltphysics";

struct JoltRagdollCaseState
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
	std::unique_ptr<JPH::BodyID[]> staticBodies;
	std::unique_ptr<JPH::BodyID[]> dynamicBodies;
	std::unique_ptr<JPH::Ref<JPH::Constraint>[]> constraints;
	std::unique_ptr<benchmark_visual::VisualStableTransform[]> qualityTransforms;
	RagdollQualityAccumulator quality;
	int createdStaticBodyCount;
	int createdDynamicBodyCount;
	int createdConstraintCount;
	int completedStepCount;
};

JPH::Quat JoltRagdollRotation(const CaseExecutionRagdoll& fixture, int ragdollIndex);
JPH::Vec3 JoltRagdollRotate(JPH::QuatArg rotation, JPH::Vec3Arg value);
JPH::RVec3 JoltRagdollBase(const CaseExecutionRagdoll& fixture, int row, int column);
JPH::Body* AddJoltRagdollStatic(JoltRagdollCaseState* state, const JPH::Shape* shape, JPH::RVec3Arg position);
int CreateJoltRagdollFixture(JoltRagdollCaseState* state);
int ConfigureJoltRagdollPhysicsSystem(JoltRagdollCaseState* state);
int CreateJoltRagdollCaseState(const JoltCaseConfig& config, JoltRagdollCaseState* state, VerificationMode verificationMode = VerificationMode_On);
int RunJoltRagdollWarmup(const JoltCaseConfig& config, VerificationMode verificationMode = VerificationMode_On);
int StepJoltRagdollCase(JoltRagdollCaseState* state, int stepCount);
void DestroyJoltRagdollCaseState(JoltRagdollCaseState* state);
int SampleJoltRagdollTransforms(const JoltRagdollCaseState& state, benchmark_visual::VisualStableTransform* transforms,
                                int transformCapacity);
std::uint64_t CountJoltRagdollInvalidTransforms(const JoltRagdollCaseState& state);
int RunJoltRagdollHeadless(const JoltRunRequest& request);
int FormatJoltRagdollPhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                     std::size_t settingsCapacity);
int StepJoltRagdollVisual(JoltCaseView* state, int workUnitCount);
int BuildJoltRagdollVisualScene(const JoltCaseView& state, benchmark_visual::VisualGeometry* geometries,
                                benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                                benchmark_visual::VisualInstance* instances, int instanceCapacity, int* geometryCount,
                                int* instanceCount);
int SampleJoltRagdollVisualTransforms(const JoltCaseView& state, benchmark_visual::VisualStableTransform* transforms,
                                      int capacity);
int BuildJoltRagdollVisualDebugPrimitives(const JoltCaseView& state, benchmark_visual::VisualDebugPrimitive* primitives,
                                          int primitiveCapacity);
const JoltCaseDescriptor& JoltRagdollStairTumbleCaseDescriptor();
}
