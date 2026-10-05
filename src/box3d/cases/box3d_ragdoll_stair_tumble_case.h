#pragma once

#include "box3d_runner_args.h"

#include "box3d_case_registry.h"
#include "box3d/box3d.h"

#include "ragdoll_quality.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace box3d_benchmark
{
struct Box3DRunRequest;

constexpr const char* kRagdollEngineId = "box3d";

struct Box3DRagdollCaseState
{
	VerificationMode verificationMode = VerificationMode_On;
	Box3DCaseConfig config;
	b3WorldId worldId;
	std::unique_ptr<b3BodyId[]> dynamicBodies;
	std::unique_ptr<b3JointId[]> joints;
	std::unique_ptr<benchmark_visual::VisualStableTransform[]> qualityTransforms;
	RagdollQualityAccumulator quality;
	int createdDynamicBodyCount;
	int createdJointCount;
	int completedStepCount;
};

b3Quat Box3DRagdollRotation(const CaseExecutionRagdoll& fixture, int ragdollIndex);
b3Vec3 Box3DRagdollRotate(b3Quat rotation, b3Vec3 value);
b3Pos Box3DRagdollBase(const CaseExecutionRagdoll& fixture, int row, int column);
float Box3DRagdollDensity(const CaseExecutionRagdollPart& part, float partMass);
b3BodyId AddBox3DRagdollStatic(b3WorldId worldId, const CaseExecutionSpec& execution, const CaseExecutionBox& box);
int CreateBox3DRagdollFixture(Box3DRagdollCaseState* state);
b3WorldId CreateBox3DRagdollWorld(const Box3DCaseConfig& config);
int CreateBox3DRagdollCaseState(const Box3DCaseConfig& config, Box3DRagdollCaseState* state, VerificationMode verificationMode = VerificationMode_On);
int RunBox3DRagdollWarmup(const Box3DCaseConfig& config, VerificationMode verificationMode = VerificationMode_On);
int StepBox3DRagdollCase(Box3DRagdollCaseState* state, int stepCount);
void DestroyBox3DRagdollCaseState(Box3DRagdollCaseState* state);
int SampleBox3DRagdollTransforms(const Box3DRagdollCaseState& state,
                                 benchmark_visual::VisualStableTransform* transforms, int transformCapacity);
std::uint64_t CountBox3DRagdollInvalidTransforms(const Box3DRagdollCaseState& state);
int RunBox3DRagdollHeadless(const Box3DRunRequest& request);
int FormatBox3DRagdollPhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                      std::size_t settingsCapacity);
int StepBox3DRagdollVisual(Box3DCaseView* state, int workUnitCount);
int BuildBox3DRagdollVisualScene(const Box3DCaseView& state, benchmark_visual::VisualGeometry* geometries,
                                 benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                                 benchmark_visual::VisualInstance* instances, int instanceCapacity, int* geometryCount,
                                 int* instanceCount);
int SampleBox3DRagdollVisualTransforms(const Box3DCaseView& state, benchmark_visual::VisualStableTransform* transforms,
                                       int capacity);
int BuildBox3DRagdollVisualDebugPrimitives(const Box3DCaseView& state,
                                           benchmark_visual::VisualDebugPrimitive* primitives, int primitiveCapacity);
const Box3DCaseDescriptor& Box3DRagdollStairTumbleCaseDescriptor();
}
