#include "box3d_visual_snapshot.h"
#include "box3d_ragdoll_stair_tumble_case.h"

#include "box3d_result_writer.h"
#include "box3d_runner_args.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <new>

namespace box3d_benchmark
{
constexpr float kPi = 3.14159265358979323846f;

b3Quat Box3DRagdollRotation(const CaseExecutionRagdoll& fixture, int ragdollIndex)
{
	const float yaw =
	    fixture.yawPatternDegrees[static_cast<std::uint32_t>(ragdollIndex) % fixture.yawPatternCount] * kPi / 180.0f;
	const float pitch = fixture.pitchDegrees * kPi / 180.0f;
	return b3MulQuat(b3MakeQuatFromAxisAngle({0.0f, 1.0f, 0.0f}, yaw),
	                 b3MakeQuatFromAxisAngle({1.0f, 0.0f, 0.0f}, pitch));
}

b3Vec3 Box3DRagdollRotate(b3Quat rotation, b3Vec3 value)
{
	return b3RotateVector(rotation, value);
}

b3Pos Box3DRagdollBase(const CaseExecutionRagdoll& fixture, int row, int column)
{
	return {
	    (static_cast<float>(column) - 0.5f * static_cast<float>(fixture.ragdollGrid[1] - 1)) * fixture.columnSpacing,
	    static_cast<float>(fixture.stairCount - 1 - row) * fixture.stairRise + fixture.baseHeightOffset,
	    (static_cast<float>(row) - 0.5f * static_cast<float>(fixture.stairCount - 1)) * fixture.rowSpacing,
	};
}

b3BodyId AddBox3DRagdollStatic(b3WorldId worldId, const CaseExecutionSpec& execution, const CaseExecutionBox& box)
{
	b3BodyDef bodyDef = b3DefaultBodyDef();
	bodyDef.position = {box.center.x, box.center.y, box.center.z};
	b3BodyId body = b3CreateBody(worldId, &bodyDef);
	b3ShapeDef shapeDef = b3DefaultShapeDef();
	shapeDef.baseMaterial.friction = execution.friction;
	shapeDef.baseMaterial.restitution = execution.restitution;
	b3BoxHull hull = b3MakeBoxHull(box.halfExtents.x, box.halfExtents.y, box.halfExtents.z);
	b3CreateHullShape(body, &shapeDef, &hull.base);
	return body;
}

int CreateBox3DRagdollFixture(Box3DRagdollCaseState* state)
{
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	const CaseExecutionRagdoll& fixture = execution.ragdoll;
	for (std::uint32_t row = 0; row < fixture.stairCount; ++row)
	{
		const CaseExecutionBox stair = {
		    {0.0f, static_cast<float>(fixture.stairCount - 1 - row) * fixture.stairRise - fixture.stairHalfHeight,
			 (static_cast<float>(row) - 0.5f * static_cast<float>(fixture.stairCount - 1)) * fixture.stairDepth},
		    {fixture.stairHalfWidth, fixture.stairHalfHeight, fixture.stairHalfDepth}};
		AddBox3DRagdollStatic(state->worldId, execution, stair);
	}
	for (std::uint16_t index = 0; index < fixture.extraStaticBoxCount; ++index)
		AddBox3DRagdollStatic(state->worldId, execution, fixture.extraStaticBoxes[index]);

	std::array<Box3DResolvedShape, kCaseExecutionRagdollPartCapacity> shapes = {};
	std::array<std::uint16_t, kCaseExecutionRagdollPartCapacity> shapeIndexes = {};
	std::uint16_t shapeCount = 0;
	for (std::uint16_t partIndex = 0; partIndex < fixture.partCount; ++partIndex)
	{
		const CaseExecutionGeometry geometry = CaseExecutionPartGeometry(fixture.parts[partIndex]);
		std::uint16_t prior = 0;
		while (prior < partIndex &&
		       !CaseExecutionSameGeometry(geometry, CaseExecutionPartGeometry(fixture.parts[prior])))
			++prior;
		if (prior < partIndex)
		{
			shapeIndexes[partIndex] = shapeIndexes[prior];
			continue;
		}
		if (CreateBox3DResolvedShape(geometry, execution, &shapes[shapeCount]) != 0)
		{
			for (std::uint16_t index = 0; index < shapeCount; ++index)
				DestroyBox3DResolvedShape(&shapes[index]);
			return 2;
		}
		shapeIndexes[partIndex] = shapeCount++;
	}

	for (std::uint32_t row = 0; row < fixture.ragdollGrid[0]; ++row)
	{
		for (std::uint32_t column = 0; column < fixture.ragdollGrid[1]; ++column)
		{
			const std::uint32_t ragdollIndex = row * fixture.ragdollGrid[1] + column;
			const b3Quat rotation = Box3DRagdollRotation(fixture, ragdollIndex);
			const b3Pos base = Box3DRagdollBase(fixture, row, column);
			for (std::uint16_t partIndex = 0; partIndex < fixture.partCount; ++partIndex)
			{
				const CaseExecutionRagdollPart& part = fixture.parts[partIndex];
				const Box3DResolvedShape& shape = shapes[shapeIndexes[partIndex]];
				const b3Vec3 offset = Box3DRagdollRotate(rotation, {part.center.x, part.center.y, part.center.z});
				b3BodyDef bodyDef = b3DefaultBodyDef();
				bodyDef.type = b3_dynamicBody;
				bodyDef.position = {base.x + offset.x, base.y + offset.y, base.z + offset.z};
				bodyDef.rotation = b3MulQuat(rotation, shape.rotation);
				bodyDef.linearVelocity = {0.0f, 0.0f, row == 0 ? fixture.triggerRowSpeed : fixture.followerRowSpeed};
				bodyDef.angularVelocity = {0.0f, 0.0f, 0.0f};
				bodyDef.linearDamping = fixture.linearDamping;
				bodyDef.angularDamping = fixture.angularDamping;
				bodyDef.enableSleep = execution.sleepMode == CaseExecutionToggle_Enabled;
				b3BodyId body = b3CreateBody(state->worldId, &bodyDef);
				b3ShapeDef shapeDef = b3DefaultShapeDef();
				shapeDef.baseMaterial.friction = execution.friction;
				shapeDef.baseMaterial.restitution = execution.restitution;
				shapeDef.density = fixture.partMass / shape.unitMass;
				if (AttachBox3DResolvedShape(body, shapeDef, shape) != 0)
				{
					for (std::uint16_t index = 0; index < shapeCount; ++index)
						DestroyBox3DResolvedShape(&shapes[index]);
					return 2;
				}
				state->dynamicBodies[state->createdDynamicBodyCount++] = body;
			}
		}
	}

	for (std::uint16_t index = 0; index < shapeCount; ++index)
		DestroyBox3DResolvedShape(&shapes[index]);
	const std::uint32_t ragdollCount = fixture.ragdollGrid[0] * fixture.ragdollGrid[1];
	for (std::uint32_t ragdollIndex = 0; ragdollIndex < ragdollCount; ++ragdollIndex)
	{
		for (std::uint16_t linkIndex = 0; linkIndex < fixture.linkCount; ++linkIndex)
		{
			const CaseExecutionRagdollLink& link = fixture.links[linkIndex];
			b3SphericalJointDef jointDef = b3DefaultSphericalJointDef();
			jointDef.base.bodyIdA = state->dynamicBodies[ragdollIndex * fixture.partCount + link.parentPart];
			jointDef.base.bodyIdB = state->dynamicBodies[ragdollIndex * fixture.partCount + link.childPart];
			jointDef.base.localFrameA = {
			    {link.parentLocalAnchor.x, link.parentLocalAnchor.y, link.parentLocalAnchor.z},
			    b3Quat_identity,
			};
			jointDef.base.localFrameB = {
			    {link.childLocalAnchor.x, link.childLocalAnchor.y, link.childLocalAnchor.z},
			    b3Quat_identity,
			};
			jointDef.base.collideConnected = fixture.linkedCollisionMode == CaseExecutionToggle_Enabled;
			b3JointId joint = b3CreateSphericalJoint(state->worldId, &jointDef);
			if (b3Joint_IsValid(joint) == false)
				return 2;
			state->joints[state->createdJointCount++] = joint;
		}
	}

	const b3Counters counters = b3World_GetCounters(state->worldId);
	return state->createdDynamicBodyCount == static_cast<int>(execution.dynamicBodyCount) &&
	               state->createdJointCount == static_cast<int>(execution.constraintCount) &&
	               counters.bodyCount == static_cast<int>(execution.bodyCount) &&
	               counters.shapeCount == static_cast<int>(execution.shapeCount) &&
	               counters.jointCount == static_cast<int>(execution.constraintCount)
	           ? 0
			   : 2;
}

b3WorldId CreateBox3DRagdollWorld(const Box3DCaseConfig& config)
{
	b3WorldDef worldDef = b3DefaultWorldDef();
	const CaseExecutionSpec& execution = *config.caseExecution;
	worldDef.gravity = {execution.gravity.x, execution.gravity.y, execution.gravity.z};
	worldDef.enableContinuous = execution.continuousCollisionMode == CaseExecutionToggle_Enabled;
	worldDef.workerCount = config.threadCount;
	worldDef.capacity.staticShapeCount = execution.staticBodyCount;
	worldDef.capacity.dynamicShapeCount = execution.dynamicBodyCount;
	worldDef.capacity.staticBodyCount = execution.staticBodyCount;
	worldDef.capacity.dynamicBodyCount = execution.dynamicBodyCount;
	worldDef.capacity.contactCount = execution.dynamicBodyCount * 16;
	return b3CreateWorld(&worldDef);
}

int CreateBox3DRagdollCaseState(const Box3DCaseConfig& config, Box3DRagdollCaseState* state, VerificationMode verificationMode)
{
	if (state == nullptr || config.caseExecution == nullptr ||
	    config.caseExecution->fixtureKind != CaseFixtureKind_RagdollStairTumble ||
	    config.stepCount != static_cast<int>(config.caseExecution->measuredWorkUnitCount) ||
	    config.warmupSteps != static_cast<int>(config.caseExecution->warmupWorkUnitCount))
		return 2;
	const CaseExecutionSpec& execution = *config.caseExecution;
	*state = {};
	state->config = config;
	state->verificationMode = verificationMode;
	state->dynamicBodies.reset(new (std::nothrow) b3BodyId[execution.dynamicBodyCount]);
	state->joints.reset(new (std::nothrow) b3JointId[execution.constraintCount]);
	if (verificationMode == VerificationMode_On)
		state->qualityTransforms.reset(new (std::nothrow)
	                                   benchmark_visual::VisualStableTransform[execution.dynamicBodyCount]);
	state->quality = {};
	if (state->dynamicBodies == nullptr || state->joints == nullptr || (verificationMode == VerificationMode_On && state->qualityTransforms == nullptr))
		return 2;
	state->createdDynamicBodyCount = 0;
	state->createdJointCount = 0;
	state->completedStepCount = 0;
	state->worldId = CreateBox3DRagdollWorld(config);
	if (b3World_GetWorkerCount(state->worldId) != config.threadCount)
	{
		DestroyBox3DRagdollCaseState(state);
		return 2;
	}
	if (CreateBox3DRagdollFixture(state) != 0)
	{
		DestroyBox3DRagdollCaseState(state);
		return 2;
	}
	return 0;
}

int RunBox3DRagdollWarmup(const Box3DCaseConfig& config, VerificationMode verificationMode)
{
	Box3DRagdollCaseState state = {};
	if (CreateBox3DRagdollCaseState(config, &state, verificationMode) != 0)
		return 2;
	for (int step = 0; step < config.warmupSteps; ++step)
	{
		b3World_Step(state.worldId, 1.0f / static_cast<float>(config.caseExecution->timestepHz),
		             static_cast<int>(config.caseExecution->nativeSolver.values[CaseSolverField_Substeps]));
	}
	DestroyBox3DRagdollCaseState(&state);
	return 0;
}

int StepBox3DRagdollCase(Box3DRagdollCaseState* state, int stepCount)
{
	if (state == nullptr || stepCount < 0 || stepCount > state->config.stepCount - state->completedStepCount)
		return 2;
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	for (int step = 0; step < stepCount; ++step)
	{
		b3World_Step(state->worldId, 1.0f / static_cast<float>(execution.timestepHz),
		             static_cast<int>(execution.nativeSolver.values[CaseSolverField_Substeps]));
		++state->completedStepCount;
		if (state->verificationMode == VerificationMode_On)
		{
			if (SampleBox3DRagdollTransforms(*state, state->qualityTransforms.get(),
			                                 static_cast<int>(execution.dynamicBodyCount)) != 0)
				return 2;
			AccumulateRagdollQuality(execution, state->qualityTransforms.get(), execution.dynamicBodyCount,
			                         state->completedStepCount, &state->quality);
		}
	}
	return 0;
}

void DestroyBox3DRagdollCaseState(Box3DRagdollCaseState* state)
{
	if (state == nullptr)
		return;
	for (int index = state->createdJointCount - 1; index >= 0; --index)
	{
		if (b3Joint_IsValid(state->joints[index]))
			b3DestroyJoint(state->joints[index], false);
	}
	state->createdJointCount = 0;
	if (B3_IS_NON_NULL(state->worldId))
		b3DestroyWorld(state->worldId);
	state->worldId = {};
	state->dynamicBodies.reset();
	state->joints.reset();
	state->qualityTransforms.reset();
}

int SampleBox3DRagdollTransforms(const Box3DRagdollCaseState& state,
                                 benchmark_visual::VisualStableTransform* transforms, int transformCapacity)
{
	const int dynamicBodyCount = static_cast<int>(state.config.caseExecution->dynamicBodyCount);
	if (transforms == nullptr || transformCapacity < dynamicBodyCount)
		return 2;
	for (int index = 0; index < dynamicBodyCount; ++index)
	{
		if (!b3Body_IsValid(state.dynamicBodies[index]))
		{
			transforms[index].stableSlot = UINT32_MAX;
			continue;
		}
		const b3Pos position = b3Body_GetPosition(state.dynamicBodies[index]);
		const b3Quat rotation = b3Body_GetRotation(state.dynamicBodies[index]);
		transforms[index] = {static_cast<std::uint32_t>(index),
		                     {
		                         position.x,
		                         position.y,
		                         position.z,
		                         rotation.v.x,
		                         rotation.v.y,
		                         rotation.v.z,
		                         rotation.s,
		                     }};
	}
	return 0;
}

std::uint64_t CountBox3DRagdollInvalidTransforms(const Box3DRagdollCaseState& state)
{
	std::uint64_t invalidCount = 0;
	const int dynamicBodyCount = static_cast<int>(state.config.caseExecution->dynamicBodyCount);
	for (int index = 0; index < dynamicBodyCount; ++index)
	{
		if (!b3Body_IsValid(state.dynamicBodies[index]))
		{
			++invalidCount;
			continue;
		}
		const b3Pos position = b3Body_GetPosition(state.dynamicBodies[index]);
		const b3Quat rotation = b3Body_GetRotation(state.dynamicBodies[index]);
		if (std::isfinite(position.x) == 0 || std::isfinite(position.y) == 0 || std::isfinite(position.z) == 0 ||
		    std::isfinite(rotation.v.x) == 0 || std::isfinite(rotation.v.y) == 0 || std::isfinite(rotation.v.z) == 0 ||
		    std::isfinite(rotation.s) == 0)
			++invalidCount;
	}
	return invalidCount;
}

int RunBox3DRagdollHeadless(const Box3DRunRequest& request)
{
	const Box3DCaseConfig config = {&request.caseExecution, request.threadCount, request.repeatIndex, request.stepCount,
	                                request.warmupSteps};
	if (RunBox3DRagdollWarmup(config, request.verificationMode) != 0)
	{
		std::fprintf(stderr, "run_failed reason=create_warmup_fixture\n");
		return 2;
	}
	Box3DRagdollCaseState state = {};
	if (CreateBox3DRagdollCaseState(config, &state, request.verificationMode) != 0)
	{
		std::fprintf(stderr, "run_failed reason=create_fixture\n");
		return 2;
	}
	Box3DCaseView recordingState = {&state};
	int status = RecordBox3DCase(request, &recordingState);
	if (status == 0)
	{
		const std::uint64_t invalidTransformCount = CountBox3DRagdollInvalidTransforms(state);
		const b3Counters counters = b3World_GetCounters(state.worldId);
		const CaseExecutionSpec& execution = request.caseExecution;
		const int caseValid = counters.bodyCount == static_cast<int>(execution.bodyCount) &&
		                      counters.shapeCount == static_cast<int>(execution.shapeCount) &&
		                      counters.jointCount == static_cast<int>(execution.constraintCount);
		const int metricValid = state.completedStepCount == request.stepCount && (request.verificationMode == VerificationMode_Off || state.quality.jointSampleCount != 0);
		std::array<char, 256> physicsSettings = {};
		FormatBox3DRagdollPhysicsSettings(request.caseExecution, request.threadCount, physicsSettings.data(),
		                                  physicsSettings.size());
		std::array<Box3DObservationRow, kRagdollQualityObservationCount> observations = {};
		for (std::uint32_t index = 0; request.verificationMode == VerificationMode_On && index < observations.size(); ++index)
		{
			const RagdollQualityObservation value = RagdollQualityValue(state.quality, index);
			observations[index] = {value.id, "final", execution.measuredWorkUnitCount,
			                       static_cast<Box3DObservationValueType>(value.valueType), value.bits};
		}
		const Box3DResult result = {
		    execution.fixtureSemantic,
		    execution.fixtureRevision,
		    physicsSettings.data(),
		    static_cast<int>(execution.bodyCount),
		    static_cast<int>(execution.shapeCount),
		    static_cast<int>(execution.queryCount),
		    static_cast<int>(execution.constraintCount),
		    invalidTransformCount,
		    caseValid != 0 ? "ok" : "invalid_result",
		    caseValid != 0 && metricValid != 0 ? "ok" : "invalid_result",
		    request.threadCount,
		    RequestedWorkerCount(request.threadCount),
		    state.completedStepCount,
		    std::numeric_limits<double>::quiet_NaN(),
		    nullptr,
		    observations.data(),
		    request.verificationMode == VerificationMode_On ? static_cast<std::uint32_t>(observations.size()) : 0u,
		};
		status = WriteBox3DResult(request, result);
	}
	DestroyBox3DRagdollCaseState(&state);
	return status;
}

int FormatBox3DRagdollPhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                      std::size_t settingsCapacity)
{
	if (settings == nullptr || settingsCapacity == 0)
		return 2;
	const int size = std::snprintf(
	    settings, settingsCapacity,
	    "substeps=%u; sleep=%s; ccd=%s; linked_collision=%s; linear_damping=%.9g; angular_damping=%.9g; worker_count=%d",
	    execution.nativeSolver.values[CaseSolverField_Substeps],
	    execution.sleepMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.continuousCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.ragdoll.linkedCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.ragdoll.linearDamping, execution.ragdoll.angularDamping, RequestedWorkerCount(threadCount));
	return size > 0 && static_cast<std::size_t>(size) < settingsCapacity ? 0 : 2;
}

int StepBox3DRagdollVisual(Box3DCaseView* state, int workUnitCount)
{
	return state == nullptr || state->value == nullptr
	           ? 2
			   : StepBox3DRagdollCase(static_cast<Box3DRagdollCaseState*>(state->value), workUnitCount);
}

enum class RagdollGeometryMatch
{
	Different,
	Equal,
};

RagdollGeometryMatch SameRagdollGeometry(const CaseExecutionRagdollPart& left, const CaseExecutionRagdollPart& right)
{
	return left.shape == right.shape && left.radius == right.radius && left.halfSegment == right.halfSegment &&
	               left.axis == right.axis && left.halfExtents.x == right.halfExtents.x &&
	               left.halfExtents.y == right.halfExtents.y && left.halfExtents.z == right.halfExtents.z
	           ? RagdollGeometryMatch::Equal
			   : RagdollGeometryMatch::Different;
}

std::uint32_t RagdollGeometryIndex(const CaseExecutionRagdoll& fixture, std::uint16_t partIndex)
{
	std::uint32_t geometryIndex = 0;
	for (std::uint16_t index = 0; index < partIndex; ++index)
	{
		if (SameRagdollGeometry(fixture.parts[index], fixture.parts[partIndex]) == RagdollGeometryMatch::Equal)
			return RagdollGeometryIndex(fixture, index);
		std::uint32_t previousMatches = 0;
		for (std::uint16_t prior = 0; prior < index; ++prior)
			if (SameRagdollGeometry(fixture.parts[prior], fixture.parts[index]) == RagdollGeometryMatch::Equal)
				++previousMatches;
		if (previousMatches == 0)
			++geometryIndex;
	}
	return geometryIndex;
}

int BuildBox3DRagdollVisualScene(const Box3DCaseView& state, benchmark_visual::VisualGeometry* geometries,
                                 benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                                 benchmark_visual::VisualInstance* instances, int instanceCapacity, int* geometryCount,
                                 int* instanceCount)
{
	if (state.value == nullptr || geometries == nullptr || instances == nullptr || geometryCount == nullptr ||
	    instanceCount == nullptr)
		return 2;
	const Box3DRagdollCaseState& value = *static_cast<const Box3DRagdollCaseState*>(state.value);
	const CaseExecutionSpec& execution = *value.config.caseExecution;
	const CaseExecutionRagdoll& fixture = execution.ragdoll;
	std::uint32_t partGeometryCount = 0;
	for (std::uint16_t partIndex = 0; partIndex < fixture.partCount; ++partIndex)
	{
		const std::uint32_t index = RagdollGeometryIndex(fixture, partIndex);
		if (index < partGeometryCount)
			continue;
		if (index != partGeometryCount)
			return 2;
		++partGeometryCount;
	}
	const int totalGeometryCount = static_cast<int>(partGeometryCount + 2);
	if (geometryCapacity < totalGeometryCount || instanceCapacity < static_cast<int>(execution.visualInstanceCount))
		return 2;
	for (std::uint16_t partIndex = 0; partIndex < fixture.partCount; ++partIndex)
	{
		const std::uint32_t index = RagdollGeometryIndex(fixture, partIndex);
		std::uint32_t previousMatches = 0;
		for (std::uint16_t prior = 0; prior < partIndex; ++prior)
			if (SameRagdollGeometry(fixture.parts[prior], fixture.parts[partIndex]) == RagdollGeometryMatch::Equal)
				++previousMatches;
		if (previousMatches != 0)
			continue;
		const CaseExecutionRagdollPart& part = fixture.parts[partIndex];
		if (BuildResolvedVisualGeometry(execution, CaseExecutionPartGeometry(part), meshes, &geometries[index]) != 0)
			return 2;
	}
	geometries[partGeometryCount] = {benchmark_visual::VisualGeometryKind_Box,
	                                 fixture.stairHalfWidth,
	                                 fixture.stairHalfHeight,
	                                 fixture.stairHalfDepth,
	                                 0,
	                                 0,
	                                 0,
	                                 0,
	                                 0,
	                                 0};
	const CaseExecutionBox& floorBox = fixture.extraStaticBoxes[0];
	geometries[partGeometryCount + 1] = {benchmark_visual::VisualGeometryKind_Box,
	                                     floorBox.halfExtents.x,
	                                     floorBox.halfExtents.y,
	                                     floorBox.halfExtents.z,
	                                     0,
	                                     0,
	                                     0,
	                                     0,
	                                     0,
	                                     0};
	for (std::uint32_t index = 0; index < execution.dynamicBodyCount; ++index)
	{
		const b3Pos position = b3Body_GetPosition(value.dynamicBodies[index]);
		const b3Quat rotation = b3Body_GetRotation(value.dynamicBodies[index]);
		instances[index] = {};
		instances[index].geometryIndex =
		    RagdollGeometryIndex(fixture, static_cast<std::uint16_t>(index % fixture.partCount));
		instances[index].stableSlot = static_cast<std::uint32_t>(index);
		instances[index].transformSlot = static_cast<std::uint32_t>(index);
		instances[index].initialTransform = {position.x,   position.y,   position.z, rotation.v.x,
		                                     rotation.v.y, rotation.v.z, rotation.s};
	}
	for (std::uint32_t row = 0; row < fixture.stairCount; ++row)
	{
		benchmark_visual::VisualInstance& instance = instances[execution.dynamicBodyCount + row];
		instance = {};
		instance.geometryIndex = partGeometryCount;
		instance.stableSlot = execution.dynamicBodyCount + row;
		instance.transformSlot = UINT32_MAX;
		instance.initialTransform = {
		    0.0f,
		    static_cast<float>(fixture.stairCount - 1 - row) * fixture.stairRise - fixture.stairHalfHeight,
		    (static_cast<float>(row) - 0.5f * static_cast<float>(fixture.stairCount - 1)) * fixture.stairDepth,
		    0.0f,
		    0.0f,
		    0.0f,
		    1.0f};
	}
	benchmark_visual::VisualInstance& floor = instances[execution.visualInstanceCount - 1];
	floor = {};
	floor.geometryIndex = partGeometryCount + 1;
	floor.stableSlot = execution.visualInstanceCount - 1;
	floor.transformSlot = UINT32_MAX;
	floor.initialTransform = {floorBox.center.x, floorBox.center.y, floorBox.center.z, 0.0f, 0.0f, 0.0f, 1.0f};
	*geometryCount = totalGeometryCount;
	*instanceCount = static_cast<int>(execution.visualInstanceCount);
	return 0;
}

int SampleBox3DRagdollVisualTransforms(const Box3DCaseView& state, benchmark_visual::VisualStableTransform* transforms,
                                       int capacity)
{
	return SampleBox3DRagdollTransforms(*static_cast<const Box3DRagdollCaseState*>(state.value), transforms, capacity);
}

int BuildBox3DRagdollVisualDebugPrimitives(const Box3DCaseView& state,
                                           benchmark_visual::VisualDebugPrimitive* primitives, int primitiveCapacity)
{
	if (state.value == nullptr || primitives == nullptr)
		return 2;
	const Box3DRagdollCaseState& value = *static_cast<const Box3DRagdollCaseState*>(state.value);
	const CaseExecutionSpec& execution = *value.config.caseExecution;
	const CaseExecutionRagdoll& fixture = execution.ragdoll;
	if (primitiveCapacity < static_cast<int>(execution.visualDebugPrimitiveCount) ||
	    execution.visualDebugPrimitiveCount > fixture.extraStaticBoxCount)
		return 2;
	const std::uint16_t first =
	    static_cast<std::uint16_t>(fixture.extraStaticBoxCount - execution.visualDebugPrimitiveCount);
	for (std::uint32_t index = 0; index < execution.visualDebugPrimitiveCount; ++index)
	{
		const CaseExecutionBox& box = fixture.extraStaticBoxes[first + index];
		primitives[index] = {benchmark_visual::VisualDebugPrimitiveKind_AabbOverlap,
		                     6u,
		                     box.center.x,
		                     box.center.y,
		                     box.center.z,
		                     box.halfExtents.x,
		                     box.halfExtents.y,
		                     box.halfExtents.z,
		                     0.0f,
		                     0u};
	}
	return 0;
}

const Box3DCaseDescriptor& Box3DRagdollStairTumbleCaseDescriptor()
{
	static const Box3DCaseDescriptor descriptor = {
	    kRagdollEngineId,
	    RunBox3DRagdollHeadless,
	    StepBox3DRagdollVisual,
	    BuildBox3DRagdollVisualScene,
	    SampleBox3DRagdollVisualTransforms,
	    BuildBox3DRagdollVisualDebugPrimitives,
	};
	return descriptor;
}
}
