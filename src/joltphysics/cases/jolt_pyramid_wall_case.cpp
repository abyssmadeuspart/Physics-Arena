#include "jolt_pyramid_wall_case.h"

#include "jolt_result_writer.h"
#include "jolt_runner_args.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <new>
#include <cstring>

JPH_SUPPRESS_WARNINGS

namespace jolt_benchmark
{
using namespace JPH;

int CreateJoltPyramidWallFixture(JoltPyramidWallState* state)
{
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	if (execution.fixtureKind != CaseFixtureKind_PyramidWall)
		return 2;
	const CaseExecutionPyramidWall& fixture = execution.pyramidWall;
	BodyInterface& bodyInterface = state->physicsSystem.GetBodyInterface();
	RefConst<Shape> boxShape;
	if (CreateJoltResolvedShape(execution.selectedGeometry, execution, &boxShape) != 0)
		return 2;
	const float mass = 8.0f * fixture.halfExtent * fixture.halfExtent * fixture.halfExtent * fixture.density;
	const float inertia = (2.0f / 3.0f) * mass * fixture.halfExtent * fixture.halfExtent;
	state->dynamicBodies.reserve(execution.dynamicBodyCount);
	for (std::uint32_t index = 0; index < execution.dynamicBodyCount; ++index)
	{
		const CaseExecutionVector3 position = PyramidWallPosition(fixture, index);
		BodyCreationSettings settings(boxShape, RVec3(position.x, position.y, position.z), Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING);
		settings.mAllowSleeping = execution.sleepMode == CaseExecutionToggle_Enabled;
		settings.mFriction = execution.friction;
		settings.mRestitution = execution.restitution;
		settings.mLinearDamping = 0.0f;
		settings.mAngularDamping = 0.0f;
		settings.mMotionQuality = execution.continuousCollisionMode == CaseExecutionToggle_Enabled
		                              ? EMotionQuality::LinearCast : EMotionQuality::Discrete;
		settings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
		settings.mMassPropertiesOverride.mMass = mass;
		const BodyID bodyId = bodyInterface.CreateAndAddBody(settings, EActivation::Activate);
		if (bodyId.IsInvalid())
			return 2;
		state->dynamicBodies.push_back(bodyId);
		BodyLockRead lock(state->physicsSystem.GetBodyLockInterface(), bodyId);
		if (!lock.Succeeded())
			return 2;
		const Body& body = lock.GetBody();
		const MotionProperties& motion = *body.GetMotionProperties();
		const float nativeMass = 1.0f / motion.GetInverseMass();
		const Vec3 inverseInertia = motion.GetInverseInertiaDiagonal();
		if (body.GetPosition() != RVec3(position.x, position.y, position.z) || body.GetRotation() != Quat::sIdentity() ||
		    body.GetLinearVelocity().LengthSq() != 0.0f || body.GetAngularVelocity().LengthSq() != 0.0f ||
		    !std::isfinite(nativeMass) || std::abs(nativeMass - mass) > 1e-5f * mass ||
		    !std::isfinite(inverseInertia.GetX()) || !std::isfinite(inverseInertia.GetY()) || !std::isfinite(inverseInertia.GetZ()) ||
		    std::abs(1.0f / inverseInertia.GetX() - inertia) > 1e-5f * inertia ||
		    std::abs(1.0f / inverseInertia.GetY() - inertia) > 1e-5f * inertia ||
		    std::abs(1.0f / inverseInertia.GetZ() - inertia) > 1e-5f * inertia ||
		    motion.GetLinearDamping() != 0.0f || motion.GetAngularDamping() != 0.0f ||
		    motion.GetMotionQuality() != settings.mMotionQuality ||
		    body.GetFriction() != execution.friction || body.GetRestitution() != execution.restitution ||
		    body.GetAllowSleeping() != settings.mAllowSleeping || !body.IsActive() ||
		    body.GetShape()->GetSubType() != EShapeSubType::Box)
			return 2;
		const RVec3 nativePosition = body.GetPosition();
		if (state->verificationMode == VerificationMode_On)
			state->initialPotentialEnergy -= static_cast<double>(nativeMass) *
		    (static_cast<double>(execution.gravity.x) * nativePosition.GetX() +
		     static_cast<double>(execution.gravity.y) * nativePosition.GetY() +
		     static_cast<double>(execution.gravity.z) * nativePosition.GetZ());
	}

	const RefConst<Shape> floorShape =
	    new BoxShape(Vec3(fixture.floorHalfExtents.x, fixture.floorHalfExtents.y, fixture.floorHalfExtents.z), 0.0f);
	BodyCreationSettings floorSettings(floorShape, RVec3(0.0f, -fixture.floorHalfExtents.y, 0.0f), Quat::sIdentity(),
	                                   EMotionType::Static, Layers::NON_MOVING);
	floorSettings.mFriction = execution.friction;
	floorSettings.mRestitution = execution.restitution;
	const BodyID floorId = bodyInterface.CreateAndAddBody(floorSettings, EActivation::DontActivate);
	if (floorId.IsInvalid())
		return 2;
	{
		BodyLockRead lock(state->physicsSystem.GetBodyLockInterface(), floorId);
		if (!lock.Succeeded())
			return 2;
		const Body& floor = lock.GetBody();
		if (!floor.IsStatic() || floor.GetPosition() != floorSettings.mPosition ||
		    floor.GetRotation() != Quat::sIdentity() || floor.GetShape()->GetSubType() != EShapeSubType::Box ||
		    floor.GetFriction() != execution.friction || floor.GetRestitution() != execution.restitution)
			return 2;
	}
	state->physicsSystem.OptimizeBroadPhase();
	return state->dynamicBodies.size() == execution.dynamicBodyCount &&
	               state->physicsSystem.GetNumBodies() == execution.bodyCount && execution.shapeCount == execution.bodyCount
	           ? 0
			   : 2;
}

int CreateJoltPyramidWallState(const JoltCaseConfig& config, JoltPyramidWallState* state, VerificationMode verificationMode)
{
	if (state == nullptr || config.caseExecution == nullptr || config.caseExecution->timestepHz == 0)
		return 2;
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
	if (state->selectedJobSystem == nullptr || state->selectedJobSystem->GetMaxConcurrency() != config.threadCount)
	{
		DestroyJoltPyramidWallState(state);
		return 2;
	}
	state->physicsElapsedMs = 0.0;
	state->latestPhysicsStepMs = 0.0;
	state->completedStepCount = 0;
	state->rawStepDurations.resize(static_cast<std::size_t>(config.stepCount));
	for (std::uint32_t ordinal = 0; verificationMode == VerificationMode_On && ordinal < std::min(4u, config.caseExecution->measuredWorkUnitCount); ++ordinal)
		state->observationInputs[ordinal].resize(config.caseExecution->dynamicBodyCount);
	const CaseExecutionSpec& execution = *config.caseExecution;
	state->physicsSystem.Init(execution.bodyCount, 0, execution.dynamicBodyCount * 16, execution.dynamicBodyCount * 16,
	                          state->broadPhaseLayerInterface, state->objectVsBroadPhaseLayerFilter,
	                          state->objectVsObjectLayerFilter);
	state->physicsSystem.SetGravity(Vec3(execution.gravity.x, execution.gravity.y, execution.gravity.z));
	PhysicsSettings settings = state->physicsSystem.GetPhysicsSettings();
	settings.mNumVelocitySteps = execution.nativeSolver.values[CaseSolverField_VelocityIterations];
	settings.mNumPositionSteps = execution.nativeSolver.values[CaseSolverField_PositionIterations];
	settings.mAllowSleeping = execution.sleepMode == CaseExecutionToggle_Enabled;
	state->physicsSystem.SetPhysicsSettings(settings);
	if (CreateJoltPyramidWallFixture(state) != 0)
	{
		DestroyJoltPyramidWallState(state);
		return 2;
	}
	return 0;
}

int WarmupJoltPyramidWallState(JoltPyramidWallState* state, int workUnitCount)
{
	if (state == nullptr || workUnitCount < 0)
		return 2;
	const float timestep = 1.0f / static_cast<float>(state->config.caseExecution->timestepHz);
	for (int step = 0; step < workUnitCount; ++step)
		if (state->physicsSystem.Update(
		        timestep,
		        static_cast<int>(state->config.caseExecution->nativeSolver.values[CaseSolverField_CollisionSteps]),
		        state->tempAllocator, state->selectedJobSystem) != EPhysicsUpdateError::None)
			return 2;
	return 0;
}

void CaptureJoltPyramidWallObservation(JoltPyramidWallState* state, int ordinal)
{
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	for (std::uint32_t index = 0; index < state->dynamicBodies.size(); ++index)
	{
		JoltWallBodyInput& input = state->observationInputs[ordinal][index];
		BodyLockRead lock(state->physicsSystem.GetBodyLockInterface(), state->dynamicBodies[index]);
		input.readStatus = lock.Succeeded() ? 1u : 0u;
		if (input.readStatus == 0)
			continue;
		const Body& body = lock.GetBody();
		const MotionProperties& motion = *body.GetMotionProperties();
		input.position = body.GetPosition();
		input.rotation = body.GetRotation();
		input.inertiaRotation = motion.GetInertiaRotation();
		input.linear = body.GetLinearVelocity();
		input.angular = body.GetAngularVelocity();
		input.inverseInertia = motion.GetInverseInertiaDiagonal();
		input.inverseMass = motion.GetInverseMass();
		input.sleepFlags = (body.GetAllowSleeping() ? 1u : 0u) | (body.IsActive() ? 2u : 0u);
	}
	state->observations[ordinal].elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void ReduceJoltPyramidWallObservation(JoltPyramidWallState* state, int ordinal)
{
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	PyramidWallObservation& sample = state->observations[ordinal];
	for (std::uint32_t index = 0; index < state->dynamicBodies.size(); ++index)
	{
		const JoltWallBodyInput& input = state->observationInputs[ordinal][index];
		if (input.readStatus == 0)
		{
			++sample.invalidBodies;
			continue;
		}
		const RVec3 p = input.position;
		const Quat q = input.rotation;
		const Vec3 linear = input.linear;
		const Vec3 angular = input.angular;
		const Vec3 localAngular = input.inertiaRotation.Conjugated() * (q.Conjugated() * angular);
		const Vec3 inverse = input.inverseInertia;
		const double energy = 0.5 * (static_cast<double>(localAngular.GetX()) * localAngular.GetX() / inverse.GetX() +
		    static_cast<double>(localAngular.GetY()) * localAngular.GetY() / inverse.GetY() +
		    static_cast<double>(localAngular.GetZ()) * localAngular.GetZ() / inverse.GetZ());
		AccumulatePyramidWallObservation(execution.pyramidWall, index,
		    {static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ())},
		    {q.GetX(), q.GetY(), q.GetZ(), q.GetW()},
		    {linear.GetX(), linear.GetY(), linear.GetZ()}, {angular.GetX(), angular.GetY(), angular.GetZ()},
		    1.0 / input.inverseMass, energy,
		    (input.sleepFlags & 1u) == (execution.sleepMode == CaseExecutionToggle_Enabled ? 1u : 0u) &&
		        (execution.sleepMode == CaseExecutionToggle_Enabled || (input.sleepFlags & 2u) != 0) ? 1 : 0,
		    execution.gravity, &sample);
	}
	FinishPyramidWallObservation(execution.dynamicBodyCount, &sample);
	sample.elapsedMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

int StepJoltPyramidWallState(JoltPyramidWallState* state, int workUnitCount)
{
	if (state == nullptr || workUnitCount < 0 ||
	    workUnitCount > static_cast<int>(state->rawStepDurations.size()) - state->completedStepCount)
		return 2;
	const int firstStep = state->completedStepCount;
	const float timestep = 1.0f / static_cast<float>(state->config.caseExecution->timestepHz);
	for (int step = 0; step < workUnitCount; ++step)
	{
		const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		const EPhysicsUpdateError updateError = state->physicsSystem.Update(
		    timestep,
		    static_cast<int>(state->config.caseExecution->nativeSolver.values[CaseSolverField_CollisionSteps]),
		    state->tempAllocator, state->selectedJobSystem);
		const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
		if (updateError != EPhysicsUpdateError::None)
			return 2;
		state->rawStepDurations[firstStep + step] = (end - start).count();
		const int observation = PyramidWallObservationIndex(firstStep + step + 1, state->config.caseExecution->measuredWorkUnitCount);
		if (state->verificationMode == VerificationMode_On && observation >= 0)
			CaptureJoltPyramidWallObservation(state, observation);
	}
	for (int step = 0; step < workUnitCount; ++step)
	{
		const double milliseconds = std::chrono::duration<double, std::milli>(
		                                std::chrono::steady_clock::duration(state->rawStepDurations[firstStep + step]))
		                                .count();
		state->physicsElapsedMs += milliseconds;
		state->latestPhysicsStepMs = milliseconds;
	}
	state->completedStepCount += workUnitCount;
	return 0;
}

void DestroyJoltPyramidWallState(JoltPyramidWallState* state)
{
	if (state == nullptr)
		return;
	delete state->threadPool;
	delete state->singleThreaded;
	delete state->tempAllocator;
	state->threadPool = nullptr;
	state->singleThreaded = nullptr;
	state->tempAllocator = nullptr;
	state->selectedJobSystem = nullptr;
	state->dynamicBodies = {};
	state->rawStepDurations = {};
}

int SampleJoltPyramidWallTransforms(const JoltPyramidWallState& state,
                                     benchmark_visual::VisualStableTransform* transforms, int transformCapacity)
{
	const int dynamicBodyCount = static_cast<int>(state.dynamicBodies.size());
	if (transforms == nullptr || transformCapacity < dynamicBodyCount)
		return 2;
	const BodyLockInterface& locks = state.physicsSystem.GetBodyLockInterface();
	for (int index = 0; index < dynamicBodyCount; ++index)
	{
		BodyLockRead lock(locks, state.dynamicBodies[index]);
		if (!lock.Succeeded())
			return 2;
		const RVec3 position = lock.GetBody().GetPosition();
		const Quat rotation = lock.GetBody().GetRotation();
		transforms[index] = {static_cast<std::uint32_t>(index),
		                     {static_cast<float>(position.GetX()), static_cast<float>(position.GetY()),
		                      static_cast<float>(position.GetZ()), rotation.GetX(), rotation.GetY(), rotation.GetZ(),
		                      rotation.GetW()}};
	}
	return 0;
}

int FormatJoltPyramidWallPhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                          std::size_t settingsCapacity)
{
	if (settings == nullptr || settingsCapacity == 0)
		return 2;
	const int size = std::snprintf(
	    settings, settingsCapacity,
	    "velocity_iterations=%u; position_iterations=%u; collision_steps=%u; sleep=%s; ccd=%s; linear_damping=0; angular_damping=0; worker_count=%d",
	    execution.nativeSolver.values[CaseSolverField_VelocityIterations],
	    execution.nativeSolver.values[CaseSolverField_PositionIterations],
	    execution.nativeSolver.values[CaseSolverField_CollisionSteps],
	    execution.sleepMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.continuousCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    RequestedWorkerCount(threadCount));
	return size > 0 && static_cast<std::size_t>(size) < settingsCapacity ? 0 : 2;
}

int RunJoltPyramidWallHeadless(const JoltRunRequest& request)
{
	const JoltCaseConfig config = {&request.caseExecution, request.threadCount, request.repeatIndex, request.stepCount,
	                               request.warmupSteps};
	JoltPyramidWallState state = {};
	if (CreateJoltPyramidWallState(config, &state, request.verificationMode) != 0)
		return 2;
	benchmark_stack::Capture capture = {};
	std::vector<benchmark_visual::VisualStableTransform> capturedTransforms(request.verificationMode == VerificationMode_On ? request.caseExecution.dynamicBodyCount : 0);
	const JoltCaseDescriptor* captureDescriptor = nullptr;
	int status = ResolveJoltCase(request.caseExecution, &captureDescriptor);
	if (status == 0)
		status = request.verificationMode == VerificationMode_On ? benchmark_stack::OpenCapture(request.stackStream, request.caseExecution,
		    captureDescriptor->engineId, request.threadCount, request.repeatIndex, &capture) : 0;
	JoltCaseView recordingState = {&state};
	for (std::uint32_t ordinal = 0; status == 0 && ordinal <= request.caseExecution.warmupWorkUnitCount; ++ordinal)
	{
		if (ordinal != 0)
			status = WarmupJoltPyramidWallState(&state, 1);
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
		status = RecordJoltCase(request, &recordingState, request.verificationMode == VerificationMode_On ? &capture : nullptr);
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
			ReduceJoltPyramidWallObservation(&state, static_cast<int>(ordinal));
		std::array<JoltObservationRow, 37> rows = {};
		std::uint64_t invalid = 0;
		if (request.verificationMode == VerificationMode_Off)
			for (const JPH::BodyID body : state.dynamicBodies)
			{
				JPH::BodyLockRead lock(state.physicsSystem.GetBodyLockInterface(), body);
				if (!lock.Succeeded())
				{
					++invalid;
					continue;
				}
				const JPH::RVec3 position = lock.GetBody().GetPosition();
				const JPH::Quat rotation = lock.GetBody().GetRotation();
				if (!std::isfinite(position.GetX()) || !std::isfinite(position.GetY()) || !std::isfinite(position.GetZ()) ||
				    !std::isfinite(rotation.GetX()) || !std::isfinite(rotation.GetY()) || !std::isfinite(rotation.GetZ()) || !std::isfinite(rotation.GetW()))
					++invalid;
			}
		for (std::uint32_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
		{
			const PyramidWallObservation& sample = state.observations[sampleIndex];
			invalid += sample.invalidBodies;
			const double values[] = {sample.centreOfMassHeight, sample.lateralRms, sample.translationalEnergy,
			                         sample.rotationalEnergy, sample.potentialEnergy, sample.floorPenetration,
			                         0.0, 0.0, sample.elapsedMs};
			for (std::uint32_t field = 0; field < 9; ++field)
			{
				JoltObservationRow& row = rows[(field < 8 ? field * sampleCount : 8 * sampleCount + 1) + sampleIndex];
				row = {ids[field], "observation", PyramidWallObservationStep(request.caseExecution.measuredWorkUnitCount, sampleIndex), JoltObservationValueType_Float64, 0};
				std::memcpy(&row.valueBits, &values[field], sizeof(double));
				if (field == 6 || field == 7)
				{
					row.valueType = JoltObservationValueType_Uint64;
					row.valueBits = field == 6 ? sample.escapedBodies : sample.invalidBodies;
				}
			}
		}
		rows[8 * sampleCount] = {"initial_potential_energy", "construction", 0, JoltObservationValueType_Float64, 0};
		std::memcpy(&rows[8 * sampleCount].valueBits, &state.initialPotentialEnergy, sizeof(double));
		const std::uint64_t invalidTransformCount = invalid;
		const int caseValid = invalidTransformCount == 0;
		const int metricValid = state.completedStepCount == request.stepCount && state.physicsElapsedMs > 0.0 &&
		                        std::isfinite(state.physicsElapsedMs) != 0;
		std::array<char, 256> physicsSettings = {};
		FormatJoltPyramidWallPhysicsSettings(request.caseExecution, request.threadCount, physicsSettings.data(),
		                                      physicsSettings.size());
		const JoltResult result = {request.caseExecution.fixtureSemantic,
		                           request.caseExecution.fixtureRevision,
		                           physicsSettings.data(),
		                           static_cast<int>(request.caseExecution.bodyCount),
		                           static_cast<int>(request.caseExecution.shapeCount),
		                           0,
		                           0,
		                           invalidTransformCount,
		                           caseValid != 0 ? "ok" : "invalid_result",
		                           caseValid != 0 && metricValid != 0 ? "ok" : "invalid_result",
		                           request.threadCount,
		                           RequestedWorkerCount(request.threadCount),
		                           state.completedStepCount,
		                           state.physicsElapsedMs,
		                           state.rawStepDurations.data(),
		                           rows.data(),
		                           request.verificationMode == VerificationMode_On ? 9 * sampleCount + 1 : 0u};
		status = WriteJoltResult(request, result);
	}
	DestroyJoltPyramidWallState(&state);
	return status;
}

int StepJoltPyramidWallVisual(JoltCaseView* state, int workUnitCount)
{
	return state == nullptr || state->value == nullptr
	           ? 2
			   : StepJoltPyramidWallState(static_cast<JoltPyramidWallState*>(state->value), workUnitCount);
}

int BuildJoltPyramidWallVisualScene(const JoltCaseView& state, benchmark_visual::VisualGeometry* geometries,
                                     benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                                     benchmark_visual::VisualInstance* instances, int instanceCapacity,
                                     int* geometryCount, int* instanceCount)
{
	if (state.value == nullptr || geometries == nullptr || geometryCapacity < 2 || instances == nullptr ||
	    geometryCount == nullptr || instanceCount == nullptr)
		return 2;
	const JoltPyramidWallState& value = *static_cast<const JoltPyramidWallState*>(state.value);
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
	const BodyLockInterface& locks = value.physicsSystem.GetBodyLockInterface();
	const int dynamicBodyCount = static_cast<int>(value.dynamicBodies.size());
	for (int index = 0; index < dynamicBodyCount; ++index)
	{
		BodyLockRead lock(locks, value.dynamicBodies[index]);
		if (!lock.Succeeded())
			return 2;
		const RVec3 position = lock.GetBody().GetPosition();
		const Quat rotation = lock.GetBody().GetRotation();
		instances[index] = {};
		instances[index].geometryIndex = 0u;
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
	benchmark_visual::VisualInstance& floor = instances[dynamicBodyCount];
	floor = {};
	floor.geometryIndex = 1u;
	floor.stableSlot = static_cast<std::uint32_t>(dynamicBodyCount);
	floor.transformSlot = UINT32_MAX;
	floor.initialTransform = {0.0f, -fixture.floorHalfExtents.y, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
	*geometryCount = 2;
	*instanceCount = static_cast<int>(execution.visualInstanceCount);
	return 0;
}

int SampleJoltPyramidWallVisualTransforms(const JoltCaseView& state,
                                           benchmark_visual::VisualStableTransform* transforms, int capacity)
{
	return state.value == nullptr ? 2
	                              : SampleJoltPyramidWallTransforms(
	                                    *static_cast<const JoltPyramidWallState*>(state.value), transforms, capacity);
}

const JoltCaseDescriptor& JoltPyramidWallCaseDescriptor()
{
	static const JoltCaseDescriptor descriptor = {
	    "joltphysics",
	    RunJoltPyramidWallHeadless,
	    StepJoltPyramidWallVisual,
	    BuildJoltPyramidWallVisualScene,
	    SampleJoltPyramidWallVisualTransforms,
	    nullptr,
	};
	return descriptor;
}
}
