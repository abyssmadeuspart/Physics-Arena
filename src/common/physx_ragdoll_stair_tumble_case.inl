#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace PHYSICS_ARENA_PHYSX_NAMESPACE
{
namespace
{
constexpr float kPhysXRagdollPi = 3.14159265358979323846f;

const CaseExecutionSpec* RagdollExecution(const PhysXRagdollCaseState* state)
{
	return state == nullptr ? nullptr : state->config.caseExecution;
}

physx::PxQuat PhysXRagdollRotation(const CaseExecutionRagdoll& fixture, std::uint32_t ragdollIndex)
{
	const float yaw = fixture.yawPatternDegrees[ragdollIndex % fixture.yawPatternCount] * kPhysXRagdollPi / 180.0f;
	return physx::PxQuat(yaw, physx::PxVec3(0.0f, 1.0f, 0.0f)) *
	       physx::PxQuat(fixture.pitchDegrees * kPhysXRagdollPi / 180.0f, physx::PxVec3(1.0f, 0.0f, 0.0f));
}

physx::PxVec3 PhysXRagdollBase(const CaseExecutionRagdoll& fixture, std::uint32_t row, std::uint32_t column)
{
	return {
	    (static_cast<float>(column) - 0.5f * static_cast<float>(fixture.ragdollGrid[1] - 1)) * fixture.columnSpacing,
	    static_cast<float>(fixture.stairCount - 1 - row) * fixture.stairRise + fixture.baseHeightOffset,
	    (static_cast<float>(row) - 0.5f * static_cast<float>(fixture.stairCount - 1)) * fixture.rowSpacing,
	};
}

int SameRagdollGeometry(const CaseExecutionRagdollPart& left, const CaseExecutionRagdollPart& right)
{
	if (left.shape != right.shape || left.halfSegment != right.halfSegment || left.axis != right.axis)
		return 0;
	if (left.shape == CaseExecutionShape_Sphere || left.shape == CaseExecutionShape_Capsule)
		return left.radius == right.radius ? 1 : 0;
	return left.halfExtents.x == right.halfExtents.x && left.halfExtents.y == right.halfExtents.y &&
	               left.halfExtents.z == right.halfExtents.z
	           ? 1
			   : 0;
}

std::uint32_t RagdollGeometryIndex(const CaseExecutionRagdoll& fixture, std::uint16_t partIndex)
{
	std::uint32_t geometryIndex = 0;
	for (std::uint16_t sourceIndex = 0; sourceIndex < fixture.partCount; ++sourceIndex)
	{
		int duplicate = 0;
		for (std::uint16_t previous = 0; previous < sourceIndex; ++previous)
			if (SameRagdollGeometry(fixture.parts[sourceIndex], fixture.parts[previous]) != 0)
			{
				duplicate = 1;
				break;
			}
		if (duplicate != 0)
			continue;
		if (SameRagdollGeometry(fixture.parts[partIndex], fixture.parts[sourceIndex]) != 0)
			return geometryIndex;
		++geometryIndex;
	}
	return UINT32_MAX;
}

int RagdollPartGeometryCount(const CaseExecutionRagdoll& fixture)
{
	int count = 0;
	for (std::uint16_t index = 0; index < fixture.partCount; ++index)
		if (RagdollGeometryIndex(fixture, index) == static_cast<std::uint32_t>(count))
			++count;
	return count;
}

void ReleasePhysXRagdollContext(PhysXContext* context)
{
	if (context->scene != nullptr)
	{
		context->scene->release();
		context->scene = nullptr;
	}
	if (context->material != nullptr)
	{
		context->material->release();
		context->material = nullptr;
	}
	if (context->dispatcher != nullptr)
	{
		context->dispatcher->release();
		context->dispatcher = nullptr;
	}
	if (context->physics != nullptr)
	{
		if (context->convexMesh != nullptr)
		{
			context->convexMesh->release();
			context->convexMesh = nullptr;
		}
		context->physics->release();
		context->physics = nullptr;
	}
	if (context->foundation != nullptr)
	{
		context->foundation->release();
		context->foundation = nullptr;
	}
}

int InitializePhysXRagdollContext(const PhysXCaseConfig& config, PhysXContext* context)
{
	if (config.caseExecution == nullptr)
		return 2;
	const CaseExecutionSpec& execution = *config.caseExecution;
#if defined(PHYSICS_ARENA_PHYSX5_API)
	context->foundation = PxCreateFoundation(PX_PHYSICS_VERSION, context->allocator, context->errorCallback);
#else
	context->foundation = PxCreateFoundation(PX_FOUNDATION_VERSION, context->allocator, context->errorCallback);
#endif
	if (context->foundation == nullptr)
		return 2;
	physx::PxTolerancesScale scale;
	context->physics = PxCreatePhysics(PX_PHYSICS_VERSION, *context->foundation, scale, false, nullptr);
	if (context->physics == nullptr)
		return 2;
	if (InitializePhysXResolvedShape(context, execution) != 0)
		return 2;
	const int workerCount = RequestedWorkerCount(config.threadCount);
	context->dispatcher = physx::PxDefaultCpuDispatcherCreate(static_cast<physx::PxU32>(workerCount));
	if (context->dispatcher == nullptr)
		return 2;
	if (context->dispatcher->getWorkerCount() != static_cast<physx::PxU32>(workerCount))
	{
		std::fprintf(stderr, "run_failed reason=dispatcher_worker_count_mismatch requested=%d effective=%u\n",
		             workerCount, static_cast<unsigned int>(context->dispatcher->getWorkerCount()));
		return 2;
	}
	physx::PxSceneDesc sceneDesc(context->physics->getTolerancesScale());
	sceneDesc.gravity = physx::PxVec3(execution.gravity.x, execution.gravity.y, execution.gravity.z);
	sceneDesc.cpuDispatcher = context->dispatcher;
	benchmark_physx::ApplyEngineSceneSettings(&sceneDesc, execution);
	context->scene = context->physics->createScene(sceneDesc);
	if (context->scene == nullptr)
		return 2;
	context->material = context->physics->createMaterial(execution.friction, execution.friction, execution.restitution);
	return context->material != nullptr ? 0 : 2;
}

int AddPhysXRagdollStatic(PhysXRagdollCaseState* state, const CaseExecutionBox& box)
{
	physx::PxRigidStatic* actor = state->context.physics->createRigidStatic(
	    physx::PxTransform(physx::PxVec3(box.center.x, box.center.y, box.center.z)));
	if (actor == nullptr)
		return 2;
#if defined(PHYSICS_ARENA_PHYSX5_API)
	physx::PxShape* shape = physx::PxRigidActorExt::createExclusiveShape(
	    *actor, physx::PxBoxGeometry(box.halfExtents.x, box.halfExtents.y, box.halfExtents.z),
	    *state->context.material);
#else
	physx::PxShape* shape = actor->createShape(
	    physx::PxBoxGeometry(box.halfExtents.x, box.halfExtents.y, box.halfExtents.z), *state->context.material);
#endif
	if (shape == nullptr)
	{
		actor->release();
		return 2;
	}
	state->context.scene->addActor(*actor);
	state->staticBodies[state->createdStaticBodyCount++] = actor;
	return 0;
}

int AddPhysXRagdollDynamic(PhysXRagdollCaseState* state, int bodyIndex, const CaseExecutionRagdollPart& part,
                           const physx::PxTransform& transform, float linearVelocityZ)
{
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	const CaseExecutionRagdoll& fixture = execution.ragdoll;
	physx::PxRigidDynamic* actor = state->context.physics->createRigidDynamic(transform);
	if (actor == nullptr)
		return 2;
	physx::PxShape* shape = AttachPhysXResolvedShape(&state->context, actor, CaseExecutionPartGeometry(part));
	if (shape == nullptr)
	{
		actor->release();
		return 2;
	}
	if (physx::PxRigidBodyExt::setMassAndUpdateInertia(*actor, fixture.partMass) == false)
	{
		actor->release();
		return 2;
	}
	actor->setLinearVelocity(physx::PxVec3(0.0f, 0.0f, linearVelocityZ));
	actor->setAngularVelocity(physx::PxVec3(0.0f));
	actor->setLinearDamping(fixture.linearDamping);
	actor->setAngularDamping(fixture.angularDamping);
	benchmark_physx::ApplyEngineBodySettings(actor, execution);
	state->context.scene->addActor(*actor);
	actor->wakeUp();
	state->dynamicBodies[bodyIndex] = actor;
	state->createdDynamicBodyCount += 1;
	return 0;
}

int CreatePhysXRagdollFixture(PhysXRagdollCaseState* state)
{
	if (state == nullptr || state->config.caseExecution == nullptr)
		return 2;
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	const CaseExecutionRagdoll& fixture = execution.ragdoll;
	for (std::uint32_t row = 0; row < fixture.stairCount; ++row)
	{
		const CaseExecutionBox stair = {
		    {0.0f, static_cast<float>(fixture.stairCount - 1 - row) * fixture.stairRise - fixture.stairHalfHeight,
			 (static_cast<float>(row) - 0.5f * static_cast<float>(fixture.stairCount - 1)) * fixture.stairDepth},
		    {fixture.stairHalfWidth, fixture.stairHalfHeight, fixture.stairHalfDepth}};
		if (AddPhysXRagdollStatic(state, stair) != 0)
			return 2;
	}
	for (std::uint16_t index = 0; index < fixture.extraStaticBoxCount; ++index)
		if (AddPhysXRagdollStatic(state, fixture.extraStaticBoxes[index]) != 0)
			return 2;

	for (std::uint32_t row = 0; row < fixture.ragdollGrid[0]; ++row)
		for (std::uint32_t column = 0; column < fixture.ragdollGrid[1]; ++column)
		{
			const std::uint32_t ragdollIndex = row * fixture.ragdollGrid[1] + column;
			const physx::PxQuat rotation = PhysXRagdollRotation(fixture, ragdollIndex);
			const physx::PxVec3 base = PhysXRagdollBase(fixture, row, column);
			for (std::uint16_t partIndex = 0; partIndex < fixture.partCount; ++partIndex)
			{
				const CaseExecutionRagdollPart& part = fixture.parts[partIndex];
				const physx::PxVec3 center(part.center.x, part.center.y, part.center.z);
				const int bodyIndex = static_cast<int>(ragdollIndex * fixture.partCount + partIndex);
				if (AddPhysXRagdollDynamic(
				        state, bodyIndex, part,
				        physx::PxTransform(base + rotation.rotate(center), rotation * PhysXShapeRotation(part.axis)),
				        row == 0 ? fixture.triggerRowSpeed : fixture.followerRowSpeed) != 0)
					return 2;
			}
		}

	const std::uint32_t ragdollCount = fixture.ragdollGrid[0] * fixture.ragdollGrid[1];
	for (std::uint32_t ragdollIndex = 0; ragdollIndex < ragdollCount; ++ragdollIndex)
		for (std::uint16_t linkIndex = 0; linkIndex < fixture.linkCount; ++linkIndex)
		{
			const CaseExecutionRagdollLink& link = fixture.links[linkIndex];
			physx::PxSphericalJoint* joint = physx::PxSphericalJointCreate(
			    *state->context.physics, state->dynamicBodies[ragdollIndex * fixture.partCount + link.parentPart],
			    physx::PxTransform(
			        physx::PxVec3(link.parentLocalAnchor.x, link.parentLocalAnchor.y, link.parentLocalAnchor.z)),
			    state->dynamicBodies[ragdollIndex * fixture.partCount + link.childPart],
			    physx::PxTransform(
			        physx::PxVec3(link.childLocalAnchor.x, link.childLocalAnchor.y, link.childLocalAnchor.z)));
			if (joint == nullptr)
				return 2;
			joint->setConstraintFlag(physx::PxConstraintFlag::eCOLLISION_ENABLED,
			                         fixture.linkedCollisionMode == CaseExecutionToggle_Enabled);
			state->joints[state->createdJointCount++] = joint;
		}

	int shapeCount = 0;
	for (int index = 0; index < state->createdDynamicBodyCount; ++index)
		shapeCount += static_cast<int>(state->dynamicBodies[index]->getNbShapes());
	for (int index = 0; index < state->createdStaticBodyCount; ++index)
		shapeCount += static_cast<int>(state->staticBodies[index]->getNbShapes());
	return state->createdDynamicBodyCount == static_cast<int>(execution.dynamicBodyCount) &&
	               state->createdStaticBodyCount == static_cast<int>(execution.staticBodyCount) &&
	               state->createdJointCount == static_cast<int>(execution.constraintCount) &&
	               state->context.scene->getNbActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC) ==
	                   execution.dynamicBodyCount &&
	               state->context.scene->getNbActors(physx::PxActorTypeFlag::eRIGID_STATIC) ==
	                   execution.staticBodyCount &&
	               state->context.scene->getNbConstraints() == execution.constraintCount &&
	               shapeCount == static_cast<int>(execution.shapeCount)
	           ? 0
			   : 2;
}

void DestroyPhysXRagdollCaseState(PhysXRagdollCaseState* state)
{
	if (state == nullptr)
		return;
	for (int index = state->createdJointCount - 1; index >= 0; --index)
		if (state->joints[index] != nullptr)
			state->joints[index]->release();
	state->createdJointCount = 0;
	for (int index = state->createdDynamicBodyCount - 1; index >= 0; --index)
		if (state->dynamicBodies[index] != nullptr)
			state->dynamicBodies[index]->release();
	state->createdDynamicBodyCount = 0;
	for (int index = state->createdStaticBodyCount - 1; index >= 0; --index)
		if (state->staticBodies[index] != nullptr)
			state->staticBodies[index]->release();
	state->createdStaticBodyCount = 0;
	ReleasePhysXRagdollContext(&state->context);
	state->dynamicBodies.reset();
	state->staticBodies.reset();
	state->joints.reset();
	state->qualityTransforms.reset();
}

int AcceptPhysXRagdollVisualConfig(const PhysXCaseConfig& config)
{
	return config.caseExecution != nullptr && config.caseExecution->fixtureKind == CaseFixtureKind_RagdollStairTumble &&
	               config.stepCount == static_cast<int>(config.caseExecution->measuredWorkUnitCount) &&
	               config.warmupSteps == static_cast<int>(config.caseExecution->warmupWorkUnitCount)
	           ? 0
			   : 2;
}

int CreatePhysXRagdollCaseState(const PhysXCaseConfig& config, PhysXRagdollCaseState* state, VerificationMode verificationMode)
{
	if (state == nullptr || AcceptPhysXRagdollVisualConfig(config) != 0)
		return 2;
	const CaseExecutionSpec& execution = *config.caseExecution;
	state->config = config;
	state->verificationMode = verificationMode;
	state->context.foundation = nullptr;
	state->context.physics = nullptr;
	state->context.dispatcher = nullptr;
	state->context.scene = nullptr;
	state->context.material = nullptr;
	state->context.convexMesh = nullptr;
	state->dynamicBodies.reset(new (std::nothrow) physx::PxRigidDynamic*[execution.dynamicBodyCount]());
	state->staticBodies.reset(new (std::nothrow) physx::PxRigidStatic*[execution.staticBodyCount]());
	state->joints.reset(new (std::nothrow) physx::PxSphericalJoint*[execution.constraintCount]());
	if (verificationMode == VerificationMode_On)
		state->qualityTransforms.reset(new (std::nothrow)
	                                   benchmark_visual::VisualStableTransform[execution.dynamicBodyCount]);
	state->quality = {};
	state->createdDynamicBodyCount = 0;
	state->createdStaticBodyCount = 0;
	state->createdJointCount = 0;
	state->completedStepCount = 0;
	if (state->dynamicBodies == nullptr || state->staticBodies == nullptr || state->joints == nullptr ||
	    (verificationMode == VerificationMode_On && state->qualityTransforms == nullptr) || InitializePhysXRagdollContext(config, &state->context) != 0 ||
	    CreatePhysXRagdollFixture(state) != 0)
	{
		DestroyPhysXRagdollCaseState(state);
		return 2;
	}
	return 0;
}

int RunPhysXRagdollWarmup(const PhysXCaseConfig& config, VerificationMode verificationMode)
{
	PhysXRagdollCaseState state = {};
	if (CreatePhysXRagdollCaseState(config, &state, verificationMode) != 0)
		return 2;
	const float timestep = 1.0f / static_cast<float>(config.caseExecution->timestepHz);
	for (int step = 0; step < config.warmupSteps; ++step)
	{
		state.context.scene->simulate(timestep);
		if (state.context.scene->fetchResults(true) == false)
		{
			DestroyPhysXRagdollCaseState(&state);
			return 2;
		}
	}
	DestroyPhysXRagdollCaseState(&state);
	return 0;
}

int StepPhysXRagdollCase(PhysXRagdollCaseState* state, int stepCount)
{
	const CaseExecutionSpec* execution = RagdollExecution(state);
	if (execution == nullptr || stepCount < 0 ||
	    stepCount > static_cast<int>(execution->measuredWorkUnitCount) - state->completedStepCount)
		return 2;
	const float timestep = 1.0f / static_cast<float>(execution->timestepHz);
	for (int step = 0; step < stepCount; ++step)
	{
		state->context.scene->simulate(timestep);
		if (state->context.scene->fetchResults(true) == false)
			return 2;
		++state->completedStepCount;
		if (state->verificationMode == VerificationMode_On)
		{
			for (std::uint32_t body = 0; body < execution->dynamicBodyCount; ++body)
			{
				if (state->dynamicBodies[body] == nullptr)
				{
					state->qualityTransforms[body].stableSlot = UINT32_MAX;
					continue;
				}
				const physx::PxTransform pose = state->dynamicBodies[body]->getGlobalPose();
				state->qualityTransforms[body] = {body,
				                                  {pose.p.x, pose.p.y, pose.p.z, pose.q.x, pose.q.y, pose.q.z, pose.q.w}};
			}
			AccumulateRagdollQuality(*execution, state->qualityTransforms.get(), execution->dynamicBodyCount,
			                         state->completedStepCount, &state->quality);
		}
	}
	return 0;
}

std::uint64_t CountPhysXRagdollInvalidTransforms(const PhysXRagdollCaseState& state)
{
	std::uint64_t invalidCount = 0;
	for (std::uint32_t index = 0; index < state.config.caseExecution->dynamicBodyCount; ++index)
		if (state.dynamicBodies[index] == nullptr || state.dynamicBodies[index]->getGlobalPose().isFinite() == false)
			++invalidCount;
	return invalidCount;
}

int FormatPhysXRagdollSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                               std::size_t settingsCapacity)
{
	if (settings == nullptr || settingsCapacity == 0)
		return 2;
	const int size = std::snprintf(
	    settings, settingsCapacity,
	    "position_iterations=%u; velocity_iterations=%u; sleep=%s; ccd=%s; linked_collision=%s; linear_damping=%.9g; angular_damping=%.9g; worker_count=%d",
	    execution.nativeSolver.values[CaseSolverField_PositionIterations],
	    execution.nativeSolver.values[CaseSolverField_VelocityIterations],
	    execution.sleepMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.continuousCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.ragdoll.linkedCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.ragdoll.linearDamping, execution.ragdoll.angularDamping, RequestedWorkerCount(threadCount));
	return size > 0 && static_cast<std::size_t>(size) < settingsCapacity ? 0 : 2;
}

int RunPhysXRagdollHeadless(const PhysXRunRequest& request)
{
	const PhysXCaseConfig config = {&request.caseExecution, request.threadCount, request.repeatIndex, request.stepCount,
	                                request.warmupSteps};
	if (AcceptPhysXRagdollVisualConfig(config) != 0 || RunPhysXRagdollWarmup(config, request.verificationMode) != 0)
		return 2;
	PhysXRagdollCaseState state = {};
	if (CreatePhysXRagdollCaseState(config, &state, request.verificationMode) != 0)
		return 2;
	PhysXCaseView recordingState = {&state};
	int status = RecordPhysXCase(request, &recordingState);
	if (status == 0)
	{
		const CaseExecutionSpec& execution = request.caseExecution;
		const std::uint64_t invalidCount = CountPhysXRagdollInvalidTransforms(state);
		std::array<char, 256> settings = {};
		status =
		    FormatPhysXRagdollSettings(request.caseExecution, request.threadCount, settings.data(), settings.size());
		const int metricValid = state.completedStepCount == static_cast<int>(execution.measuredWorkUnitCount) &&
		                        (request.verificationMode == VerificationMode_Off || state.quality.jointSampleCount != 0);
		std::array<PhysXObservationRow, kRagdollQualityObservationCount> observations = {};
		for (std::uint32_t index = 0; request.verificationMode == VerificationMode_On && index < observations.size(); ++index)
		{
			const RagdollQualityObservation value = RagdollQualityValue(state.quality, index);
			observations[index] = {value.id, "final", execution.measuredWorkUnitCount,
			                       static_cast<PhysXObservationValueType>(value.valueType), value.bits};
		}
		const PhysXResult result = {execution.fixtureSemantic,
		                            execution.fixtureRevision,
		                            settings.data(),
		                            static_cast<int>(execution.bodyCount),
		                            static_cast<int>(execution.shapeCount),
		                            static_cast<int>(execution.queryCount),
		                            static_cast<int>(execution.constraintCount),
		                            invalidCount,
		                            "ok",
		                            metricValid != 0 ? "ok" : "invalid_result",
		                            request.threadCount,
		                            RequestedWorkerCount(request.threadCount),
		                            state.completedStepCount,
		                            std::numeric_limits<double>::quiet_NaN(),
		                            nullptr,
		                            observations.data(),
		                            request.verificationMode == VerificationMode_On ? static_cast<std::uint32_t>(observations.size()) : 0u};
		if (status == 0)
			status = WritePhysXResult(request, result);
	}
	DestroyPhysXRagdollCaseState(&state);
	return status;
}

int StepPhysXRagdollVisual(PhysXCaseView* state, int workUnitCount)
{
	return state == nullptr || state->value == nullptr
	           ? 2
			   : StepPhysXRagdollCase(static_cast<PhysXRagdollCaseState*>(state->value), workUnitCount);
}

int BuildPhysXRagdollVisualScene(const PhysXCaseView& state, benchmark_visual::VisualGeometry* geometries,
                                 benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                                 benchmark_visual::VisualInstance* instances, int instanceCapacity, int* geometryCount,
                                 int* instanceCount)
{
	if (state.value == nullptr || geometries == nullptr || instances == nullptr || geometryCount == nullptr ||
	    instanceCount == nullptr)
		return 2;
	const PhysXRagdollCaseState& value = *static_cast<const PhysXRagdollCaseState*>(state.value);
	const CaseExecutionSpec& execution = *value.config.caseExecution;
	const CaseExecutionRagdoll& fixture = execution.ragdoll;
	const int partGeometryCount = RagdollPartGeometryCount(fixture);
	if (fixture.extraStaticBoxCount == 0 || geometryCapacity < partGeometryCount + 2 ||
	    instanceCapacity < static_cast<int>(execution.visualInstanceCount))
		return 2;
	int nextGeometry = 0;
	for (std::uint16_t partIndex = 0; partIndex < fixture.partCount; ++partIndex)
	{
		const std::uint32_t index = RagdollGeometryIndex(fixture, partIndex);
		if (index != static_cast<std::uint32_t>(nextGeometry))
			continue;
		const CaseExecutionRagdollPart& part = fixture.parts[partIndex];
		if (BuildResolvedVisualGeometry(execution, CaseExecutionPartGeometry(part), meshes,
		                                &geometries[nextGeometry++]) != 0)
			return 2;
	}
	const std::uint32_t stairGeometryIndex = static_cast<std::uint32_t>(nextGeometry);
	geometries[nextGeometry++] = {benchmark_visual::VisualGeometryKind_Box,
	                              fixture.stairHalfWidth,
	                              fixture.stairHalfHeight,
	                              fixture.stairHalfDepth,
	                              0,
	                              0,
	                              0,
	                              0,
	                              0,
	                              0};
	const std::uint32_t runoutGeometryIndex = static_cast<std::uint32_t>(nextGeometry);
	const CaseExecutionBox& runout = fixture.extraStaticBoxes[0];
	geometries[nextGeometry++] = {benchmark_visual::VisualGeometryKind_Box,
	                              runout.halfExtents.x,
	                              runout.halfExtents.y,
	                              runout.halfExtents.z,
	                              0,
	                              0,
	                              0,
	                              0,
	                              0,
	                              0};
	for (std::uint32_t index = 0; index < execution.dynamicBodyCount; ++index)
	{
		const physx::PxTransform transform = value.dynamicBodies[index]->getGlobalPose();
		instances[index] = {};
		instances[index].geometryIndex =
		    RagdollGeometryIndex(fixture, static_cast<std::uint16_t>(index % fixture.partCount));
		instances[index].stableSlot = index;
		instances[index].transformSlot = index;
		instances[index].initialTransform = {transform.p.x, transform.p.y, transform.p.z, transform.q.x,
		                                     transform.q.y, transform.q.z, transform.q.w};
	}
	for (std::uint32_t row = 0; row < fixture.stairCount; ++row)
	{
		const std::uint32_t slot = execution.dynamicBodyCount + row;
		instances[slot] = {};
		instances[slot].geometryIndex = stairGeometryIndex;
		instances[slot].stableSlot = slot;
		instances[slot].transformSlot = UINT32_MAX;
		instances[slot].initialTransform = {
		    0.0f,
		    static_cast<float>(fixture.stairCount - 1 - row) * fixture.stairRise - fixture.stairHalfHeight,
		    (static_cast<float>(row) - 0.5f * static_cast<float>(fixture.stairCount - 1)) * fixture.stairDepth,
		    0,
		    0,
		    0,
		    1};
	}
	const std::uint32_t runoutSlot = execution.visualInstanceCount - 1;
	instances[runoutSlot] = {};
	instances[runoutSlot].geometryIndex = runoutGeometryIndex;
	instances[runoutSlot].stableSlot = runoutSlot;
	instances[runoutSlot].transformSlot = UINT32_MAX;
	instances[runoutSlot].initialTransform = {runout.center.x, runout.center.y, runout.center.z, 0, 0, 0, 1};
	*geometryCount = nextGeometry;
	*instanceCount = static_cast<int>(execution.visualInstanceCount);
	return 0;
}

int SamplePhysXRagdollVisualTransforms(const PhysXCaseView& state, benchmark_visual::VisualStableTransform* transforms,
                                       int capacity)
{
	if (state.value == nullptr || transforms == nullptr)
		return 2;
	const PhysXRagdollCaseState& value = *static_cast<const PhysXRagdollCaseState*>(state.value);
	const std::uint32_t dynamicBodyCount = value.config.caseExecution->dynamicBodyCount;
	if (capacity < static_cast<int>(dynamicBodyCount))
		return 2;
	for (std::uint32_t index = 0; index < dynamicBodyCount; ++index)
	{
		const physx::PxTransform transform = value.dynamicBodies[index]->getGlobalPose();
		transforms[index] = {
		    index,
		    {transform.p.x, transform.p.y, transform.p.z, transform.q.x, transform.q.y, transform.q.z, transform.q.w}};
	}
	return 0;
}

int BuildPhysXRagdollVisualDebugPrimitives(const PhysXCaseView& state,
                                           benchmark_visual::VisualDebugPrimitive* primitives, int primitiveCapacity)
{
	if (state.value == nullptr || primitives == nullptr)
		return 2;
	const PhysXRagdollCaseState& value = *static_cast<const PhysXRagdollCaseState*>(state.value);
	const CaseExecutionSpec& execution = *value.config.caseExecution;
	const CaseExecutionRagdoll& fixture = execution.ragdoll;
	const int debugCount = static_cast<int>(execution.visualDebugPrimitiveCount);
	if (debugCount > fixture.extraStaticBoxCount || primitiveCapacity < debugCount)
		return 2;
	const int firstDebugBox = fixture.extraStaticBoxCount - debugCount;
	for (int index = 0; index < debugCount; ++index)
	{
		const CaseExecutionBox& box = fixture.extraStaticBoxes[firstDebugBox + index];
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

} // namespace

const PhysXCaseDescriptor& PhysXRagdollStairTumbleCaseDescriptor()
{
	static const PhysXCaseDescriptor descriptor = {
	    kEngineId,
	    RunPhysXRagdollHeadless,
	    StepPhysXRagdollVisual,
	    BuildPhysXRagdollVisualScene,
	    SamplePhysXRagdollVisualTransforms,
	    BuildPhysXRagdollVisualDebugPrimitives,
	};
	return descriptor;
}
} // namespace PHYSICS_ARENA_PHYSX_NAMESPACE
