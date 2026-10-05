#include "jolt_ragdoll_stair_tumble_case.h"

#include "jolt_result_writer.h"
#include "jolt_runner_args.h"
#include <Jolt/Physics/Collision/GroupFilterTable.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <new>

JPH_SUPPRESS_WARNINGS

namespace jolt_benchmark
{
using namespace JPH;
using namespace JPH::literals;

Quat JoltRagdollRotation(const CaseExecutionRagdoll& fixture, int ragdollIndex)
{
	const float yaw =
	    DegreesToRadians(fixture.yawPatternDegrees[static_cast<std::uint32_t>(ragdollIndex) % fixture.yawPatternCount]);
	const float pitch = DegreesToRadians(fixture.pitchDegrees);
	return Quat::sRotation(Vec3::sAxisY(), yaw) * Quat::sRotation(Vec3::sAxisX(), pitch);
}

Vec3 JoltRagdollRotate(QuatArg rotation, Vec3Arg value)
{
	return rotation * value;
}

RVec3 JoltRagdollBase(const CaseExecutionRagdoll& fixture, int row, int column)
{
	return RVec3(
	    Real((static_cast<float>(column) - 0.5f * static_cast<float>(fixture.ragdollGrid[1] - 1)) *
		     fixture.columnSpacing),
	    Real(static_cast<float>(fixture.stairCount - 1 - row) * fixture.stairRise + fixture.baseHeightOffset),
	    Real((static_cast<float>(row) - 0.5f * static_cast<float>(fixture.stairCount - 1)) * fixture.rowSpacing));
}

Body* AddJoltRagdollStatic(JoltRagdollCaseState* state, const Shape* shape, RVec3Arg position)
{
	BodyInterface& bodyInterface = state->physicsSystem.GetBodyInterface();
	BodyCreationSettings settings(shape, position, Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING);
	settings.mFriction = state->config.caseExecution->friction;
	settings.mRestitution = state->config.caseExecution->restitution;
	Body* body = bodyInterface.CreateBody(settings);
	if (body == nullptr)
		return nullptr;
	bodyInterface.AddBody(body->GetID(), EActivation::DontActivate);
	state->staticBodies[state->createdStaticBodyCount++] = body->GetID();
	return body;
}

int CreateJoltRagdollFixture(JoltRagdollCaseState* state)
{
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	const CaseExecutionRagdoll& fixture = execution.ragdoll;
	RefConst<Shape> stepShape =
	    new BoxShape(Vec3(fixture.stairHalfWidth, fixture.stairHalfHeight, fixture.stairHalfDepth), 0.0f);
	for (std::uint32_t row = 0; row < fixture.stairCount; ++row)
	{
		if (AddJoltRagdollStatic(
		        state, stepShape,
		        RVec3(0.0_r,
				      Real(static_cast<float>(fixture.stairCount - 1 - row) * fixture.stairRise -
				           fixture.stairHalfHeight),
				      Real((static_cast<float>(row) - 0.5f * static_cast<float>(fixture.stairCount - 1)) *
				           fixture.stairDepth))) == nullptr)
			return 2;
	}
	for (std::uint16_t index = 0; index < fixture.extraStaticBoxCount; ++index)
	{
		const CaseExecutionBox& box = fixture.extraStaticBoxes[index];
		RefConst<Shape> shape = new BoxShape(Vec3(box.halfExtents.x, box.halfExtents.y, box.halfExtents.z), 0.0f);
		if (AddJoltRagdollStatic(state, shape, RVec3(Real(box.center.x), Real(box.center.y), Real(box.center.z))) ==
		    nullptr)
			return 2;
	}

	std::unique_ptr<RefConst<Shape>[]> partShapes(new (std::nothrow) RefConst<Shape>[fixture.partCount]);
	if (partShapes == nullptr)
		return 2;
	for (std::uint16_t partIndex = 0; partIndex < fixture.partCount; ++partIndex)
	{
		const CaseExecutionGeometry geometry = CaseExecutionPartGeometry(fixture.parts[partIndex]);
		std::uint16_t prior = 0;
		while (prior < partIndex &&
		       !CaseExecutionSameGeometry(geometry, CaseExecutionPartGeometry(fixture.parts[prior])))
			++prior;
		if (prior < partIndex)
			partShapes[partIndex] = partShapes[prior];
		else if (CreateJoltResolvedShape(geometry, execution, &partShapes[partIndex]) != 0)
			return 2;
	}

	std::unique_ptr<Body*[]> bodies(new (std::nothrow) Body*[execution.dynamicBodyCount]);
	if (bodies == nullptr)
		return 2;
	BodyInterface& bodyInterface = state->physicsSystem.GetBodyInterface();
	Ref<GroupFilterTable> linkedFilter;
	if (fixture.linkedCollisionMode == CaseExecutionToggle_Disabled)
	{
		linkedFilter = new GroupFilterTable(fixture.partCount);
		for (std::uint16_t linkIndex = 0; linkIndex < fixture.linkCount; ++linkIndex)
		{
			const CaseExecutionRagdollLink& link = fixture.links[linkIndex];
			linkedFilter->DisableCollision(link.parentPart, link.childPart);
		}
	}
	for (std::uint32_t row = 0; row < fixture.ragdollGrid[0]; ++row)
	{
		for (std::uint32_t column = 0; column < fixture.ragdollGrid[1]; ++column)
		{
			const std::uint32_t ragdollIndex = row * fixture.ragdollGrid[1] + column;
			const Quat rotation = JoltRagdollRotation(fixture, ragdollIndex);
			const RVec3 base = JoltRagdollBase(fixture, row, column);
			for (std::uint16_t partIndex = 0; partIndex < fixture.partCount; ++partIndex)
			{
				const CaseExecutionRagdollPart& part = fixture.parts[partIndex];
				const Vec3 offset = JoltRagdollRotate(rotation, Vec3(part.center.x, part.center.y, part.center.z));
				BodyCreationSettings settings(partShapes[partIndex], base + RVec3(offset),
				                              rotation * JoltShapeRotation(part.axis), EMotionType::Dynamic,
				                              Layers::MOVING);
				settings.mAllowSleeping = execution.sleepMode == CaseExecutionToggle_Enabled;
				settings.mFriction = execution.friction;
				settings.mRestitution = execution.restitution;
				settings.mMotionQuality = execution.continuousCollisionMode == CaseExecutionToggle_Enabled
				                              ? EMotionQuality::LinearCast
				                              : EMotionQuality::Discrete;
				settings.mLinearDamping = fixture.linearDamping;
				settings.mAngularDamping = fixture.angularDamping;
				if (fixture.linkedCollisionMode == CaseExecutionToggle_Disabled)
					settings.mCollisionGroup = CollisionGroup(linkedFilter, ragdollIndex, partIndex);
				settings.mLinearVelocity =
				    Vec3(0.0f, 0.0f, row == 0 ? fixture.triggerRowSpeed : fixture.followerRowSpeed);
				settings.mAngularVelocity = Vec3::sZero();
				settings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
				settings.mMassPropertiesOverride.mMass = fixture.partMass;
				Body* body = bodyInterface.CreateBody(settings);
				if (body == nullptr)
					return 2;
				bodyInterface.AddBody(body->GetID(), EActivation::Activate);
				const int bodyIndex = state->createdDynamicBodyCount++;
				bodies[bodyIndex] = body;
				state->dynamicBodies[bodyIndex] = body->GetID();
			}
		}
	}

	const std::uint32_t ragdollCount = fixture.ragdollGrid[0] * fixture.ragdollGrid[1];
	for (std::uint32_t ragdollIndex = 0; ragdollIndex < ragdollCount; ++ragdollIndex)
	{
		for (std::uint16_t linkIndex = 0; linkIndex < fixture.linkCount; ++linkIndex)
		{
			const CaseExecutionRagdollLink& link = fixture.links[linkIndex];
			PointConstraintSettings settings;
			settings.mSpace = EConstraintSpace::LocalToBodyCOM;
			settings.mPoint1 = RVec3(link.parentLocalAnchor.x, link.parentLocalAnchor.y, link.parentLocalAnchor.z) -
			                   RVec3(partShapes[link.parentPart]->GetCenterOfMass());
			settings.mPoint2 = RVec3(link.childLocalAnchor.x, link.childLocalAnchor.y, link.childLocalAnchor.z) -
			                   RVec3(partShapes[link.childPart]->GetCenterOfMass());
			const int parentIndex = ragdollIndex * fixture.partCount + link.parentPart;
			const int childIndex = ragdollIndex * fixture.partCount + link.childPart;
			Ref<Constraint> constraint = settings.Create(*bodies[parentIndex], *bodies[childIndex]);
			if (constraint == nullptr)
				return 2;
			state->physicsSystem.AddConstraint(constraint);
			state->constraints[state->createdConstraintCount++] = constraint;
		}
	}

	state->physicsSystem.OptimizeBroadPhase();
	return state->createdStaticBodyCount == static_cast<int>(execution.staticBodyCount) &&
	               state->createdDynamicBodyCount == static_cast<int>(execution.dynamicBodyCount) &&
	               state->createdConstraintCount == static_cast<int>(execution.constraintCount) &&
	               state->physicsSystem.GetNumBodies() == execution.bodyCount &&
	               state->physicsSystem.GetConstraints().size() == execution.constraintCount
	           ? 0
			   : 2;
}

int ConfigureJoltRagdollPhysicsSystem(JoltRagdollCaseState* state)
{
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	state->physicsSystem.Init(execution.bodyCount, 0, execution.bodyCount * 24, execution.bodyCount * 24,
	                          state->broadPhaseLayerInterface, state->objectVsBroadPhaseLayerFilter,
	                          state->objectVsObjectLayerFilter);
	state->physicsSystem.SetGravity(Vec3(execution.gravity.x, execution.gravity.y, execution.gravity.z));
	PhysicsSettings settings = state->physicsSystem.GetPhysicsSettings();
	settings.mNumVelocitySteps = execution.nativeSolver.values[CaseSolverField_VelocityIterations];
	settings.mNumPositionSteps = execution.nativeSolver.values[CaseSolverField_PositionIterations];
	settings.mAllowSleeping = execution.sleepMode == CaseExecutionToggle_Enabled;
	state->physicsSystem.SetPhysicsSettings(settings);
	return CreateJoltRagdollFixture(state);
}

int CreateJoltRagdollCaseState(const JoltCaseConfig& config, JoltRagdollCaseState* state, VerificationMode verificationMode)
{
	if (state == nullptr || config.caseExecution == nullptr ||
	    config.caseExecution->fixtureKind != CaseFixtureKind_RagdollStairTumble ||
	    config.stepCount != static_cast<int>(config.caseExecution->measuredWorkUnitCount) ||
	    config.warmupSteps != static_cast<int>(config.caseExecution->warmupWorkUnitCount))
		return 2;
	const CaseExecutionSpec& execution = *config.caseExecution;
	state->config = config;
	state->verificationMode = verificationMode;
	state->tempAllocator = new TempAllocatorImpl(128 * 1024 * 1024);
	state->singleThreaded = nullptr;
	state->threadPool = nullptr;
	if (config.threadCount <= 1)
	{
		state->singleThreaded = new JobSystemSingleThreaded(cMaxPhysicsJobs);
		state->selectedJobSystem = state->singleThreaded;
	}
	else
	{
		state->threadPool =
		    new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, static_cast<uint>(config.threadCount - 1));
		state->selectedJobSystem = state->threadPool;
	}
	state->staticBodies.reset(new (std::nothrow) BodyID[execution.staticBodyCount]);
	state->dynamicBodies.reset(new (std::nothrow) BodyID[execution.dynamicBodyCount]);
	state->constraints.reset(new (std::nothrow) Ref<Constraint>[execution.constraintCount]);
	if (verificationMode == VerificationMode_On)
		state->qualityTransforms.reset(new (std::nothrow)
	                                   benchmark_visual::VisualStableTransform[execution.dynamicBodyCount]);
	state->quality = {};
	if (state->tempAllocator == nullptr || state->selectedJobSystem == nullptr || state->staticBodies == nullptr ||
	    state->dynamicBodies == nullptr || state->constraints == nullptr || (verificationMode == VerificationMode_On && state->qualityTransforms == nullptr))
	{
		DestroyJoltRagdollCaseState(state);
		return 2;
	}
	if (state->selectedJobSystem->GetMaxConcurrency() != config.threadCount)
	{
		DestroyJoltRagdollCaseState(state);
		return 2;
	}
	state->createdStaticBodyCount = 0;
	state->createdDynamicBodyCount = 0;
	state->createdConstraintCount = 0;
	state->completedStepCount = 0;
	if (ConfigureJoltRagdollPhysicsSystem(state) != 0)
	{
		DestroyJoltRagdollCaseState(state);
		return 2;
	}
	return 0;
}

int RunJoltRagdollWarmup(const JoltCaseConfig& config, VerificationMode verificationMode)
{
	JoltRagdollCaseState state = {};
	if (CreateJoltRagdollCaseState(config, &state, verificationMode) != 0)
		return 2;
	int status = 0;
	for (int step = 0; step < config.warmupSteps; ++step)
	{
		if (state.physicsSystem.Update(
		        1.0f / static_cast<float>(config.caseExecution->timestepHz),
		        static_cast<int>(config.caseExecution->nativeSolver.values[CaseSolverField_CollisionSteps]),
		        state.tempAllocator, state.selectedJobSystem) != EPhysicsUpdateError::None)
		{
			status = 2;
			break;
		}
	}
	DestroyJoltRagdollCaseState(&state);
	return status;
}

int StepJoltRagdollCase(JoltRagdollCaseState* state, int stepCount)
{
	if (state == nullptr || stepCount < 0 || stepCount > state->config.stepCount - state->completedStepCount)
		return 2;
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	for (int step = 0; step < stepCount; ++step)
	{
		if (state->physicsSystem.Update(1.0f / static_cast<float>(execution.timestepHz),
		                                static_cast<int>(execution.nativeSolver.values[CaseSolverField_CollisionSteps]),
		                                state->tempAllocator, state->selectedJobSystem) != EPhysicsUpdateError::None)
			return 2;
		++state->completedStepCount;
		if (state->verificationMode == VerificationMode_On)
		{
			if (SampleJoltRagdollTransforms(*state, state->qualityTransforms.get(),
			                                static_cast<int>(execution.dynamicBodyCount)) != 0)
				return 2;
			AccumulateRagdollQuality(execution, state->qualityTransforms.get(), execution.dynamicBodyCount,
			                         state->completedStepCount, &state->quality);
		}
	}
	return 0;
}

void DestroyJoltRagdollCaseState(JoltRagdollCaseState* state)
{
	if (state == nullptr)
		return;
	for (int index = state->createdConstraintCount - 1; index >= 0; --index)
	{
		state->physicsSystem.RemoveConstraint(state->constraints[index]);
		state->constraints[index] = nullptr;
	}
	state->createdConstraintCount = 0;
	BodyInterface& bodyInterface = state->physicsSystem.GetBodyInterface();
	for (int index = state->createdDynamicBodyCount - 1; index >= 0; --index)
	{
		bodyInterface.RemoveBody(state->dynamicBodies[index]);
		bodyInterface.DestroyBody(state->dynamicBodies[index]);
	}
	state->createdDynamicBodyCount = 0;
	for (int index = state->createdStaticBodyCount - 1; index >= 0; --index)
	{
		bodyInterface.RemoveBody(state->staticBodies[index]);
		bodyInterface.DestroyBody(state->staticBodies[index]);
	}
	state->createdStaticBodyCount = 0;
	state->constraints.reset();
	state->dynamicBodies.reset();
	state->staticBodies.reset();
	state->qualityTransforms.reset();
	delete state->threadPool;
	delete state->singleThreaded;
	delete state->tempAllocator;
	state->threadPool = nullptr;
	state->singleThreaded = nullptr;
	state->tempAllocator = nullptr;
	state->selectedJobSystem = nullptr;
}

int SampleJoltRagdollTransforms(const JoltRagdollCaseState& state, benchmark_visual::VisualStableTransform* transforms,
                                int transformCapacity)
{
	const int dynamicBodyCount = static_cast<int>(state.config.caseExecution->dynamicBodyCount);
	if (transforms == nullptr || transformCapacity < dynamicBodyCount)
		return 2;
	const BodyLockInterface& locks = state.physicsSystem.GetBodyLockInterface();
	for (int index = 0; index < dynamicBodyCount; ++index)
	{
		BodyLockRead lock(locks, state.dynamicBodies[index]);
		if (!lock.Succeeded())
		{
			transforms[index].stableSlot = UINT32_MAX;
			continue;
		}
		const Body& body = lock.GetBody();
		const RVec3 position = body.GetPosition();
		const Quat rotation = body.GetRotation();
		transforms[index] = {static_cast<std::uint32_t>(index),
		                     {
		                         static_cast<float>(position.GetX()),
		                         static_cast<float>(position.GetY()),
		                         static_cast<float>(position.GetZ()),
		                         rotation.GetX(),
		                         rotation.GetY(),
		                         rotation.GetZ(),
		                         rotation.GetW(),
		                     }};
	}
	return 0;
}

std::uint64_t CountJoltRagdollInvalidTransforms(const JoltRagdollCaseState& state)
{
	std::uint64_t invalidCount = 0;
	const int dynamicBodyCount = static_cast<int>(state.config.caseExecution->dynamicBodyCount);
	const BodyLockInterface& locks = state.physicsSystem.GetBodyLockInterface();
	for (int index = 0; index < dynamicBodyCount; ++index)
	{
		BodyLockRead lock(locks, state.dynamicBodies[index]);
		if (!lock.Succeeded())
			return dynamicBodyCount;
		const Body& body = lock.GetBody();
		const RVec3 position = body.GetPosition();
		const Quat rotation = body.GetRotation();
		if (std::isfinite(position.GetX()) == 0 || std::isfinite(position.GetY()) == 0 ||
		    std::isfinite(position.GetZ()) == 0 || std::isfinite(rotation.GetX()) == 0 ||
		    std::isfinite(rotation.GetY()) == 0 || std::isfinite(rotation.GetZ()) == 0 ||
		    std::isfinite(rotation.GetW()) == 0)
			++invalidCount;
	}
	return invalidCount;
}

int RunJoltRagdollHeadless(const JoltRunRequest& request)
{
	const JoltCaseConfig config = {&request.caseExecution, request.threadCount, request.repeatIndex, request.stepCount,
	                               request.warmupSteps};
	if (RunJoltRagdollWarmup(config, request.verificationMode) != 0)
	{
		std::cerr << "run_failed reason=create_warmup_fixture\n";
		return 2;
	}
	JoltRagdollCaseState state = {};
	if (CreateJoltRagdollCaseState(config, &state, request.verificationMode) != 0)
	{
		std::cerr << "run_failed reason=create_fixture\n";
		return 2;
	}
	JoltCaseView recordingState = {&state};
	int status = RecordJoltCase(request, &recordingState);
	if (status == 0)
	{
		const std::uint64_t invalidTransformCount = CountJoltRagdollInvalidTransforms(state);
		const CaseExecutionSpec& execution = request.caseExecution;
		const int caseValid = state.physicsSystem.GetNumBodies() == execution.bodyCount &&
		                      state.physicsSystem.GetConstraints().size() == execution.constraintCount;
		const int metricValid = state.completedStepCount == request.stepCount && (request.verificationMode == VerificationMode_Off || state.quality.jointSampleCount != 0);
		std::array<char, 256> physicsSettings = {};
		FormatJoltRagdollPhysicsSettings(request.caseExecution, request.threadCount, physicsSettings.data(),
		                                 physicsSettings.size());
		std::array<JoltObservationRow, kRagdollQualityObservationCount> observations = {};
		for (std::uint32_t index = 0; request.verificationMode == VerificationMode_On && index < observations.size(); ++index)
		{
			const RagdollQualityObservation value = RagdollQualityValue(state.quality, index);
			observations[index] = {value.id, "final", execution.measuredWorkUnitCount,
			                       static_cast<JoltObservationValueType>(value.valueType), value.bits};
		}
		const JoltResult result = {
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
		status = WriteJoltResult(request, result);
	}
	DestroyJoltRagdollCaseState(&state);
	return status;
}

int FormatJoltRagdollPhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                     std::size_t settingsCapacity)
{
	if (settings == nullptr || settingsCapacity == 0)
		return 2;
	const int size = std::snprintf(
	    settings, settingsCapacity,
	    "velocity_iterations=%u; position_iterations=%u; collision_steps=%u; sleep=%s; ccd=%s; linked_collision=%s; linear_damping=%.9g; angular_damping=%.9g; worker_count=%d",
	    execution.nativeSolver.values[CaseSolverField_VelocityIterations],
	    execution.nativeSolver.values[CaseSolverField_PositionIterations],
	    execution.nativeSolver.values[CaseSolverField_CollisionSteps],
	    execution.sleepMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.continuousCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.ragdoll.linkedCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.ragdoll.linearDamping, execution.ragdoll.angularDamping, RequestedWorkerCount(threadCount));
	return size > 0 && static_cast<std::size_t>(size) < settingsCapacity ? 0 : 2;
}

int StepJoltRagdollVisual(JoltCaseView* state, int workUnitCount)
{
	return state == nullptr || state->value == nullptr
	           ? 2
			   : StepJoltRagdollCase(static_cast<JoltRagdollCaseState*>(state->value), workUnitCount);
}

enum class JoltRagdollGeometryMatch
{
	Different,
	Equal,
};

JoltRagdollGeometryMatch SameJoltRagdollGeometry(const CaseExecutionRagdollPart& left,
                                                 const CaseExecutionRagdollPart& right)
{
	return left.shape == right.shape && left.radius == right.radius && left.halfSegment == right.halfSegment &&
	               left.axis == right.axis && left.halfExtents.x == right.halfExtents.x &&
	               left.halfExtents.y == right.halfExtents.y && left.halfExtents.z == right.halfExtents.z
	           ? JoltRagdollGeometryMatch::Equal
			   : JoltRagdollGeometryMatch::Different;
}

std::uint32_t JoltRagdollGeometryIndex(const CaseExecutionRagdoll& fixture, std::uint16_t partIndex)
{
	std::uint32_t geometryIndex = 0;
	for (std::uint16_t index = 0; index < partIndex; ++index)
	{
		if (SameJoltRagdollGeometry(fixture.parts[index], fixture.parts[partIndex]) == JoltRagdollGeometryMatch::Equal)
			return JoltRagdollGeometryIndex(fixture, index);
		std::uint32_t previousMatches = 0;
		for (std::uint16_t prior = 0; prior < index; ++prior)
			if (SameJoltRagdollGeometry(fixture.parts[prior], fixture.parts[index]) == JoltRagdollGeometryMatch::Equal)
				++previousMatches;
		if (previousMatches == 0)
			++geometryIndex;
	}
	return geometryIndex;
}

int BuildJoltRagdollVisualScene(const JoltCaseView& state, benchmark_visual::VisualGeometry* geometries,
                                benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                                benchmark_visual::VisualInstance* instances, int instanceCapacity, int* geometryCount,
                                int* instanceCount)
{
	if (state.value == nullptr || geometries == nullptr || instances == nullptr || geometryCount == nullptr ||
	    instanceCount == nullptr)
		return 2;
	const JoltRagdollCaseState& value = *static_cast<const JoltRagdollCaseState*>(state.value);
	const CaseExecutionSpec& execution = *value.config.caseExecution;
	const CaseExecutionRagdoll& fixture = execution.ragdoll;
	std::uint32_t partGeometryCount = 0;
	for (std::uint16_t partIndex = 0; partIndex < fixture.partCount; ++partIndex)
	{
		const std::uint32_t index = JoltRagdollGeometryIndex(fixture, partIndex);
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
		std::uint32_t previousMatches = 0;
		for (std::uint16_t prior = 0; prior < partIndex; ++prior)
			if (SameJoltRagdollGeometry(fixture.parts[prior], fixture.parts[partIndex]) ==
			    JoltRagdollGeometryMatch::Equal)
				++previousMatches;
		if (previousMatches != 0)
			continue;
		const CaseExecutionRagdollPart& part = fixture.parts[partIndex];
		const std::uint32_t index = JoltRagdollGeometryIndex(fixture, partIndex);
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
	const BodyLockInterface& locks = value.physicsSystem.GetBodyLockInterface();
	for (std::uint32_t index = 0; index < execution.dynamicBodyCount; ++index)
	{
		BodyLockRead lock(locks, value.dynamicBodies[index]);
		if (!lock.Succeeded())
			return 2;
		const RVec3 position = lock.GetBody().GetPosition();
		const Quat rotation = lock.GetBody().GetRotation();
		instances[index] = {};
		instances[index].geometryIndex =
		    JoltRagdollGeometryIndex(fixture, static_cast<std::uint16_t>(index % fixture.partCount));
		instances[index].stableSlot = static_cast<std::uint32_t>(index);
		instances[index].transformSlot = static_cast<std::uint32_t>(index);
		instances[index].initialTransform = {static_cast<float>(position.GetX()),
		                                     static_cast<float>(position.GetY()),
		                                     static_cast<float>(position.GetZ()),
		                                     rotation.GetX(),
		                                     rotation.GetY(),
		                                     rotation.GetZ(),
		                                     rotation.GetW()};
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

int SampleJoltRagdollVisualTransforms(const JoltCaseView& state, benchmark_visual::VisualStableTransform* transforms,
                                      int capacity)
{
	return SampleJoltRagdollTransforms(*static_cast<const JoltRagdollCaseState*>(state.value), transforms, capacity);
}

int BuildJoltRagdollVisualDebugPrimitives(const JoltCaseView& state, benchmark_visual::VisualDebugPrimitive* primitives,
                                          int primitiveCapacity)
{
	if (state.value == nullptr || primitives == nullptr)
		return 2;
	const JoltRagdollCaseState& value = *static_cast<const JoltRagdollCaseState*>(state.value);
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

const JoltCaseDescriptor& JoltRagdollStairTumbleCaseDescriptor()
{
	static const JoltCaseDescriptor descriptor = {
	    kJoltRagdollEngineId,
	    RunJoltRagdollHeadless,
	    StepJoltRagdollVisual,
	    BuildJoltRagdollVisualScene,
	    SampleJoltRagdollVisualTransforms,
	    BuildJoltRagdollVisualDebugPrimitives,
	};
	return descriptor;
}
}
