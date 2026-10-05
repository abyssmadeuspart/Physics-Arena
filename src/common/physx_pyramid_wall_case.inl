#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <new>
#include <cstring>

namespace PHYSICS_ARENA_PHYSX_NAMESPACE
{
int AcceptPhysXPyramidWallConfig(const PhysXCaseConfig& config)
{
	return config.caseExecution != nullptr && config.caseExecution->fixtureKind == CaseFixtureKind_PyramidWall &&
	               config.stepCount == static_cast<int>(config.caseExecution->measuredWorkUnitCount) &&
	               config.warmupSteps == static_cast<int>(config.caseExecution->warmupWorkUnitCount)
	           ? 0
			   : 2;
}

physx::PxShape* CreatePhysXPyramidWallBoxShape(physx::PxRigidActor* actor, const physx::PxBoxGeometry& geometry,
                                                physx::PxMaterial& material)
{
#if defined(PHYSICS_ARENA_PHYSX5_API)
	return physx::PxRigidActorExt::createExclusiveShape(*actor, geometry, material);
#else
	return actor->createShape(geometry, material);
#endif
}

int AddPhysXPyramidWallDynamic(PhysXPyramidWallCaseState* state, const physx::PxTransform& transform,
                                const CaseExecutionGeometry& geometry, float density, int dynamicIndex)
{
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	physx::PxRigidDynamic* actor = state->context.physics->createRigidDynamic(transform);
	if (actor == nullptr)
		return 2;
	physx::PxShape* shape = AttachPhysXResolvedShape(&state->context, actor, geometry);
	if (shape == nullptr || physx::PxRigidBodyExt::updateMassAndInertia(*actor, density) == false)
	{
		actor->release();
		return 2;
	}
	benchmark_physx::ApplyEngineBodySettings(actor, execution);
	actor->setLinearDamping(0.0f);
	actor->setAngularDamping(0.0f);
	state->context.scene->addActor(*actor);
	actor->wakeUp();
	state->dynamicBodies[dynamicIndex] = actor;
	state->createdDynamicBodyCount += 1;
	return 0;
}

int CreatePhysXPyramidWallFixture(PhysXPyramidWallCaseState* state)
{
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	const CaseExecutionPyramidWall& fixture = execution.pyramidWall;
	state->context.material->setFrictionCombineMode(physx::PxCombineMode::eAVERAGE);
	state->context.material->setRestitutionCombineMode(physx::PxCombineMode::eAVERAGE);
	const float expectedMass = 8.0f * fixture.halfExtent * fixture.halfExtent * fixture.halfExtent * fixture.density;
	const float expectedInertia = (2.0f / 3.0f) * expectedMass * fixture.halfExtent * fixture.halfExtent;
	// reconstructing the actor pose from the mass frame rounds quaternion components by a few float ULPs
	constexpr float rotationTolerance = 8.0f * std::numeric_limits<float>::epsilon();
	for (std::uint32_t index = 0; index < execution.dynamicBodyCount; ++index)
	{
		const CaseExecutionVector3 position = PyramidWallPosition(fixture, index);
		const physx::PxTransform transform(physx::PxVec3(position.x, position.y, position.z));
		if (AddPhysXPyramidWallDynamic(state, transform, execution.selectedGeometry, fixture.density, static_cast<int>(index)) != 0)
			return 2;
		const physx::PxRigidDynamic& body = *state->dynamicBodies[index];
		const physx::PxVec3 inertia = body.getMassSpaceInertiaTensor();
		const physx::PxTransform native = body.getGlobalPose();
		const float mass = body.getMass();
		if (native.p != transform.p || !native.q.isFinite() ||
		    std::abs(native.q.x) > rotationTolerance || std::abs(native.q.y) > rotationTolerance ||
		    std::abs(native.q.z) > rotationTolerance || std::abs(std::abs(native.q.w) - 1.0f) > rotationTolerance ||
		    body.getLinearVelocity().magnitudeSquared() != 0.0f ||
		    body.getAngularVelocity().magnitudeSquared() != 0.0f || !std::isfinite(mass) || mass <= 0.0f ||
		    !inertia.isFinite() ||
		    std::abs(mass - expectedMass) > 1e-5f * expectedMass ||
		    std::abs(inertia.x - expectedInertia) > 1e-5f * expectedInertia ||
		    std::abs(inertia.y - expectedInertia) > 1e-5f * expectedInertia ||
		    std::abs(inertia.z - expectedInertia) > 1e-5f * expectedInertia ||
		    body.getLinearDamping() != 0.0f || body.getAngularDamping() != 0.0f ||
		    body.getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eENABLE_CCD) !=
		        (execution.continuousCollisionMode == CaseExecutionToggle_Enabled) || body.isSleeping() ||
		    (body.getSleepThreshold() == 0.0f) != (execution.sleepMode == CaseExecutionToggle_Disabled))
		{
			std::fprintf(stderr,
			             "run_failed reason=pyramid_wall_body_contract index=%u "
			             "position=%.9g,%.9g,%.9g expected_position=%.9g,%.9g,%.9g "
			             "rotation=%.9g,%.9g,%.9g,%.9g mass=%.9g expected_mass=%.9g "
			             "inertia=%.9g,%.9g,%.9g expected_inertia=%.9g "
			             "linear_speed_squared=%.9g angular_speed_squared=%.9g "
			             "linear_damping=%.9g angular_damping=%.9g ccd=%d sleeping=%d sleep_threshold=%.9g\n",
			             index, native.p.x, native.p.y, native.p.z, transform.p.x, transform.p.y, transform.p.z,
			             native.q.x, native.q.y, native.q.z, native.q.w, mass, expectedMass,
			             inertia.x, inertia.y, inertia.z, expectedInertia,
			             body.getLinearVelocity().magnitudeSquared(), body.getAngularVelocity().magnitudeSquared(),
			             body.getLinearDamping(), body.getAngularDamping(),
			             static_cast<int>(body.getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eENABLE_CCD)),
			             static_cast<int>(body.isSleeping()), body.getSleepThreshold());
			return 2;
		}
		if (state->verificationMode == VerificationMode_On)
			state->initialPotentialEnergy -= static_cast<double>(mass) *
		    (static_cast<double>(execution.gravity.x) * native.p.x + static_cast<double>(execution.gravity.y) * native.p.y +
		     static_cast<double>(execution.gravity.z) * native.p.z);
	}

	state->floorBody = state->context.physics->createRigidStatic(
	    physx::PxTransform(physx::PxVec3(0.0f, -fixture.floorHalfExtents.y, 0.0f)));
	if (state->floorBody == nullptr)
		return 2;
	physx::PxShape* floorShape = CreatePhysXPyramidWallBoxShape(
	    state->floorBody,
	    physx::PxBoxGeometry(fixture.floorHalfExtents.x, fixture.floorHalfExtents.y, fixture.floorHalfExtents.z),
	    *state->context.material);
	if (floorShape == nullptr)
		return 2;
	state->context.scene->addActor(*state->floorBody);
	if (state->floorBody->getGlobalPose().p != physx::PxVec3(0.0f, -fixture.floorHalfExtents.y, 0.0f) ||
	    state->context.material->getStaticFriction() != execution.friction ||
	    state->context.material->getDynamicFriction() != execution.friction ||
	    state->context.material->getRestitution() != execution.restitution)
		return 2;
	int shapeCount = static_cast<int>(state->floorBody->getNbShapes());
	for (int index = 0; index < state->createdDynamicBodyCount; ++index)
		shapeCount += static_cast<int>(state->dynamicBodies[index]->getNbShapes());
	return state->createdDynamicBodyCount == static_cast<int>(execution.dynamicBodyCount) &&
	               state->context.scene->getNbActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC) ==
	                   execution.dynamicBodyCount &&
	               state->context.scene->getNbActors(physx::PxActorTypeFlag::eRIGID_STATIC) ==
	                   execution.staticBodyCount &&
	               shapeCount == static_cast<int>(execution.shapeCount)
	           ? 0
			   : 2;
}

void DestroyPhysXPyramidWallCaseState(PhysXPyramidWallCaseState* state)
{
	if (state == nullptr)
		return;
	for (int index = state->createdDynamicBodyCount - 1; index >= 0; --index)
		if (state->dynamicBodies[index] != nullptr)
			state->dynamicBodies[index]->release();
	state->createdDynamicBodyCount = 0;
	if (state->floorBody != nullptr)
	{
		state->floorBody->release();
		state->floorBody = nullptr;
	}
	ReleaseContext(&state->context);
	state->dynamicBodies.reset();
	state->rawStepDurations.reset();
}

int CreatePhysXPyramidWallCaseState(const PhysXCaseConfig& config, PhysXPyramidWallCaseState* state, VerificationMode verificationMode)
{
	if (state == nullptr || AcceptPhysXPyramidWallConfig(config) != 0)
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
	state->rawStepDurations.reset(new (std::nothrow)
	                                  std::chrono::steady_clock::duration::rep[execution.measuredWorkUnitCount]);
	for (std::uint32_t ordinal = 0; verificationMode == VerificationMode_On && ordinal < std::min(4u, execution.measuredWorkUnitCount); ++ordinal)
		state->observationInputs[ordinal].resize(execution.dynamicBodyCount);
	state->floorBody = nullptr;
	state->createdDynamicBodyCount = 0;
	state->completedStepCount = 0;
	state->physicsElapsedMs = 0.0;
	state->latestPhysicsStepMs = 0.0;
	if (state->dynamicBodies == nullptr || state->rawStepDurations == nullptr ||
	    InitializeContext(config, &state->context) != 0 || CreatePhysXPyramidWallFixture(state) != 0)
	{
		DestroyPhysXPyramidWallCaseState(state);
		return 2;
	}
	return 0;
}

int WarmupPhysXPyramidWallCase(PhysXPyramidWallCaseState* state, int workUnitCount)
{
	if (state == nullptr || state->config.caseExecution == nullptr || workUnitCount < 0)
		return 2;
	const float timestep = 1.0f / static_cast<float>(state->config.caseExecution->timestepHz);
	for (int step = 0; step < workUnitCount; ++step)
	{
		state->context.scene->simulate(timestep);
		if (state->context.scene->fetchResults(true) == false)
			return 2;
	}
	return 0;
}

void CapturePhysXPyramidWallObservation(PhysXPyramidWallCaseState* state, int ordinal)
{
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	for (std::uint32_t index = 0; index < execution.dynamicBodyCount; ++index)
	{
		const physx::PxRigidDynamic& body = *state->dynamicBodies[index];
		state->observationInputs[ordinal][index] = {body.getGlobalPose(), body.getCMassLocalPose().q,
		    body.getLinearVelocity(), body.getAngularVelocity(), body.getMassSpaceInertiaTensor(),
		    body.getMass(), body.getSleepThreshold(), body.isSleeping() ? 1u : 0u};
	}
	state->observations[ordinal].elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void ReducePhysXPyramidWallObservation(PhysXPyramidWallCaseState* state, int ordinal)
{
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	PyramidWallObservation& sample = state->observations[ordinal];
	for (std::uint32_t index = 0; index < execution.dynamicBodyCount; ++index)
	{
		const PhysXWallBodyInput& input = state->observationInputs[ordinal][index];
		const physx::PxTransform pose = input.pose;
		const physx::PxVec3 linear = input.linear;
		const physx::PxVec3 angular = input.angular;
		const physx::PxVec3 local = (pose.q * input.inertiaRotation).rotateInv(angular);
		const physx::PxVec3 inertia = input.inertia;
		const double energy = 0.5 * (static_cast<double>(local.x) * local.x * inertia.x +
		    static_cast<double>(local.y) * local.y * inertia.y + static_cast<double>(local.z) * local.z * inertia.z);
		AccumulatePyramidWallObservation(execution.pyramidWall, index, {pose.p.x, pose.p.y, pose.p.z},
		    {pose.q.x, pose.q.y, pose.q.z, pose.q.w}, {linear.x, linear.y, linear.z}, {angular.x, angular.y, angular.z},
		    input.mass, energy, execution.sleepMode == CaseExecutionToggle_Enabled ? input.sleepThreshold > 0.0f :
		        input.sleepThreshold == 0.0f && input.sleepFlags == 0, execution.gravity, &sample);
	}
	FinishPyramidWallObservation(execution.dynamicBodyCount, &sample);
	sample.elapsedMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

int StepPhysXPyramidWallCase(PhysXPyramidWallCaseState* state, int workUnitCount)
{
	if (state == nullptr || state->config.caseExecution == nullptr || workUnitCount < 0 ||
	    workUnitCount >
	        static_cast<int>(state->config.caseExecution->measuredWorkUnitCount) - state->completedStepCount)
		return 2;
	const int firstStep = state->completedStepCount;
	const float timestep = 1.0f / static_cast<float>(state->config.caseExecution->timestepHz);
	for (int step = 0; step < workUnitCount; ++step)
	{
		const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		state->context.scene->simulate(timestep);
		if (state->context.scene->fetchResults(true) == false)
			return 2;
		const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
		state->rawStepDurations[firstStep + step] = (end - start).count();
		const int observation = PyramidWallObservationIndex(firstStep + step + 1, state->config.caseExecution->measuredWorkUnitCount);
		if (state->verificationMode == VerificationMode_On && observation >= 0)
			CapturePhysXPyramidWallObservation(state, observation);
	}
	for (int step = 0; step < workUnitCount; ++step)
	{
		const double milliseconds = std::chrono::duration<double, std::milli>(
		                                std::chrono::steady_clock::duration(state->rawStepDurations[firstStep + step]))
		                                .count();
		if (std::isfinite(milliseconds) == 0 || milliseconds <= 0.0)
			return 2;
		state->physicsElapsedMs += milliseconds;
		state->latestPhysicsStepMs = milliseconds;
	}
	state->completedStepCount += workUnitCount;
	return 0;
}

int FormatPhysXPyramidWallSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                    std::size_t settingsCapacity)
{
	if (settings == nullptr || settingsCapacity == 0)
		return 2;
	const int size = std::snprintf(
	    settings, settingsCapacity, "position_iterations=%u; velocity_iterations=%u; sleep=%s; ccd=%s; linear_damping=0; angular_damping=0; worker_count=%d",
	    execution.nativeSolver.values[CaseSolverField_PositionIterations],
	    execution.nativeSolver.values[CaseSolverField_VelocityIterations],
	    execution.sleepMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.continuousCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    RequestedWorkerCount(threadCount));
	return size > 0 && static_cast<std::size_t>(size) < settingsCapacity ? 0 : 2;
}

int RunPhysXPyramidWallHeadless(const PhysXRunRequest& request)
{
	const PhysXCaseConfig config = {&request.caseExecution, request.threadCount, request.repeatIndex, request.stepCount,
	                                request.warmupSteps};
	PhysXPyramidWallCaseState state = {};
	if (CreatePhysXPyramidWallCaseState(config, &state, request.verificationMode) != 0)
		return 2;
	benchmark_stack::Capture capture = {};
	std::vector<benchmark_visual::VisualStableTransform> capturedTransforms(request.verificationMode == VerificationMode_On ? request.caseExecution.dynamicBodyCount : 0);
	const PhysXCaseDescriptor* captureDescriptor = nullptr;
	int status = ResolvePhysXCase(request.caseExecution, &captureDescriptor);
	if (status == 0)
		status = request.verificationMode == VerificationMode_On ? benchmark_stack::OpenCapture(request.stackStream, request.caseExecution,
		    captureDescriptor->engineId, request.threadCount, request.repeatIndex, &capture) : 0;
	PhysXCaseView recordingState = {&state};
	for (std::uint32_t ordinal = 0; status == 0 && ordinal <= request.caseExecution.warmupWorkUnitCount; ++ordinal)
	{
		if (ordinal != 0)
			status = WarmupPhysXPyramidWallCase(&state, 1);
		if (request.verificationMode == VerificationMode_On)
		{
			benchmark_stack::BeginFrame(&capture);
			if (status == 0)
				status = captureDescriptor->sampleTransforms(recordingState, capturedTransforms.data(), static_cast<int>(capturedTransforms.size()));
			if (status == 0)
				status = benchmark_stack::AppendTransforms(&capture, ordinal == 0 ? benchmark_stack::Phase_Construction : benchmark_stack::Phase_Warmup,
				    0, ordinal, capturedTransforms.data());
		}
	}
	if (status == 0)
		status = RecordPhysXCase(request, &recordingState, request.verificationMode == VerificationMode_On ? &capture : nullptr);
	const int captureStatus = request.verificationMode == VerificationMode_On ? benchmark_stack::CloseCapture(&capture) : 0;
	if (status == 0)
		status = captureStatus;
	if (status == 0)
	{
		constexpr const char* ids[] = {"centre_of_mass_height", "lateral_rms", "translational_energy", "rotational_energy",
		                               "potential_energy", "floor_penetration", "escaped_body_count", "invalid_body_count",
		                               "observation_elapsed_ms"};
		const std::uint32_t sampleCount = request.verificationMode == VerificationMode_On ? std::min(4u, request.caseExecution.measuredWorkUnitCount) : 0u;
		for (std::uint32_t ordinal = 0; ordinal < sampleCount; ++ordinal)
			ReducePhysXPyramidWallObservation(&state, static_cast<int>(ordinal));
		std::array<PhysXObservationRow, 37> rows = {};
		std::uint64_t invalid = 0;
		if (request.verificationMode == VerificationMode_Off)
			for (std::uint32_t index = 0; index < request.caseExecution.dynamicBodyCount; ++index)
				if (state.dynamicBodies[index] == nullptr || !state.dynamicBodies[index]->getGlobalPose().isFinite())
					++invalid;
		for (std::uint32_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
		{
			const PyramidWallObservation& sample = state.observations[sampleIndex];
			invalid += sample.invalidBodies;
			const double values[] = {sample.centreOfMassHeight, sample.lateralRms, sample.translationalEnergy,
			                         sample.rotationalEnergy, sample.potentialEnergy, sample.floorPenetration,
			                         0.0, 0.0, sample.elapsedMs};
			for (std::uint32_t field = 0; field < 9; ++field)
			{
				PhysXObservationRow& row = rows[(field < 8 ? field * sampleCount : 8 * sampleCount + 1) + sampleIndex];
				row = {ids[field], "observation", PyramidWallObservationStep(request.caseExecution.measuredWorkUnitCount, sampleIndex), PhysXObservationValueType_Float64, 0};
				std::memcpy(&row.valueBits, &values[field], sizeof(double));
				if (field == 6 || field == 7)
				{
					row.valueType = PhysXObservationValueType_Uint64;
					row.valueBits = field == 6 ? sample.escapedBodies : sample.invalidBodies;
				}
			}
		}
		rows[8 * sampleCount] = {"initial_potential_energy", "construction", 0, PhysXObservationValueType_Float64, 0};
		std::memcpy(&rows[8 * sampleCount].valueBits, &state.initialPotentialEnergy, sizeof(double));
		const std::uint64_t invalidCount = invalid;
		std::array<char, 256> settings = {};
		status = FormatPhysXPyramidWallSettings(request.caseExecution, request.threadCount, settings.data(),
		                                         settings.size());
		const int metricValid = state.completedStepCount == request.stepCount && state.physicsElapsedMs > 0.0 &&
		                        std::isfinite(state.physicsElapsedMs) != 0;
		const PhysXResult result = {request.caseExecution.fixtureSemantic,
		                            request.caseExecution.fixtureRevision,
		                            settings.data(),
		                            static_cast<int>(request.caseExecution.bodyCount),
		                            static_cast<int>(request.caseExecution.shapeCount),
		                            0,
		                            0,
		                            invalidCount,
		                            invalidCount == 0 ? "ok" : "invalid_result",
		                            invalidCount == 0 && metricValid != 0 ? "ok" : "invalid_result",
		                            request.threadCount,
		                            RequestedWorkerCount(request.threadCount),
		                            state.completedStepCount,
		                            state.physicsElapsedMs,
		                            state.rawStepDurations.get(), rows.data(), request.verificationMode == VerificationMode_On ? 9 * sampleCount + 1 : 0u};
		if (status == 0)
			status = WritePhysXResult(request, result);
	}
	DestroyPhysXPyramidWallCaseState(&state);
	return status;
}

int StepPhysXPyramidWallVisual(PhysXCaseView* state, int workUnitCount)
{
	return state == nullptr || state->value == nullptr
	           ? 2
			   : StepPhysXPyramidWallCase(static_cast<PhysXPyramidWallCaseState*>(state->value), workUnitCount);
}

int BuildPhysXPyramidWallVisualScene(const PhysXCaseView& state, benchmark_visual::VisualGeometry* geometries,
                                      benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                                      benchmark_visual::VisualInstance* instances, int instanceCapacity,
                                      int* geometryCount, int* instanceCount)
{
	if (state.value == nullptr || geometries == nullptr || geometryCapacity < 2 || instances == nullptr ||
	    geometryCount == nullptr || instanceCount == nullptr)
		return 2;
	const PhysXPyramidWallCaseState& value = *static_cast<const PhysXPyramidWallCaseState*>(state.value);
	const CaseExecutionSpec& execution = *value.config.caseExecution;
	const CaseExecutionPyramidWall& fixture = execution.pyramidWall;
	if (instanceCapacity < static_cast<int>(execution.visualInstanceCount))
		return 2;
	if (BuildResolvedVisualGeometry(execution, execution.selectedGeometry, meshes, &geometries[0]) != 0)
		return 2;
	geometries[1] = {benchmark_visual::VisualGeometryKind_Box,
	                 fixture.floorHalfExtents.x,
	                 fixture.floorHalfExtents.y,
	                 fixture.floorHalfExtents.z,
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
		instances[index].geometryIndex = 0u;
		instances[index].stableSlot = index;
		instances[index].transformSlot = index;
		instances[index].initialTransform = {transform.p.x, transform.p.y, transform.p.z, transform.q.x,
		                                     transform.q.y, transform.q.z, transform.q.w};
	}
	benchmark_visual::VisualInstance& floor = instances[execution.dynamicBodyCount];
	floor = {};
	floor.geometryIndex = 1u;
	floor.stableSlot = execution.dynamicBodyCount;
	floor.transformSlot = UINT32_MAX;
	floor.initialTransform = {0.0f, -fixture.floorHalfExtents.y, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
	*geometryCount = 2;
	*instanceCount = static_cast<int>(execution.visualInstanceCount);
	return 0;
}

int SamplePhysXPyramidWallVisualTransforms(const PhysXCaseView& state,
                                            benchmark_visual::VisualStableTransform* transforms, int capacity)
{
	if (state.value == nullptr || transforms == nullptr)
		return 2;
	const PhysXPyramidWallCaseState& value = *static_cast<const PhysXPyramidWallCaseState*>(state.value);
	const std::uint32_t count = value.config.caseExecution->dynamicBodyCount;
	if (capacity < static_cast<int>(count))
		return 2;
	for (std::uint32_t index = 0; index < count; ++index)
	{
		const physx::PxTransform transform = value.dynamicBodies[index]->getGlobalPose();
		transforms[index] = {
		    index,
		    {transform.p.x, transform.p.y, transform.p.z, transform.q.x, transform.q.y, transform.q.z, transform.q.w}};
	}
	return 0;
}

const PhysXCaseDescriptor& PhysXPyramidWallCaseDescriptor()
{
	static const PhysXCaseDescriptor descriptor = {
	    kEngineId,
	    RunPhysXPyramidWallHeadless,
	    StepPhysXPyramidWallVisual,
	    BuildPhysXPyramidWallVisualScene,
	    SamplePhysXPyramidWallVisualTransforms,
	    nullptr,
	};
	return descriptor;
}
} // namespace PHYSICS_ARENA_PHYSX_NAMESPACE
