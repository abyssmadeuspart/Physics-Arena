#include "box3d_pyramid_wall_case.h"
#include "box3d_result_writer.h"
#include "box3d_runner_args.h"
#include "box3d_visual_snapshot.h"

#include <cstring>

namespace box3d_benchmark
{
int CreateBox3DPyramidWall(Box3DPyramidWallState* state, const Box3DCaseConfig& config, VerificationMode verificationMode = VerificationMode_On)
{
	state->config = config;
	state->verificationMode = verificationMode;
	const CaseExecutionSpec& execution = *config.caseExecution;
	const CaseExecutionPyramidWall& wall = execution.pyramidWall;
	state->bodies.reserve(execution.dynamicBodyCount);
	state->durations.resize(execution.measuredWorkUnitCount);
	for (std::uint32_t ordinal = 0; verificationMode == VerificationMode_On && ordinal < std::min(4u, execution.measuredWorkUnitCount); ++ordinal)
		state->observationInputs[ordinal].resize(execution.dynamicBodyCount);
	b3WorldDef world = b3DefaultWorldDef();
	world.gravity = {execution.gravity.x, execution.gravity.y, execution.gravity.z};
	world.enableSleep = execution.sleepMode == CaseExecutionToggle_Enabled;
	world.enableContinuous = execution.continuousCollisionMode == CaseExecutionToggle_Enabled;
	world.workerCount = config.threadCount;
	world.capacity.dynamicBodyCount = static_cast<int>(execution.dynamicBodyCount);
	world.capacity.dynamicShapeCount = static_cast<int>(execution.dynamicBodyCount);
	world.capacity.staticBodyCount = 1;
	world.capacity.staticShapeCount = 1;
	world.capacity.contactCount = static_cast<int>(execution.dynamicBodyCount) * 16;
	state->world = b3CreateWorld(&world);
	if (!b3World_IsValid(state->world) || b3World_GetWorkerCount(state->world) != config.threadCount)
		return 2;
	b3ShapeDef shape = b3DefaultShapeDef();
	shape.baseMaterial.friction = execution.friction;
	shape.baseMaterial.restitution = execution.restitution;
	shape.density = wall.density;
	const b3BoxHull cube = b3MakeBoxHull(wall.halfExtent, wall.halfExtent, wall.halfExtent);
	b3BodyDef body = b3DefaultBodyDef();
	body.type = b3_dynamicBody;
	body.enableSleep = world.enableSleep;
	body.linearDamping = 0.0f;
	body.angularDamping = 0.0f;
	for (std::uint32_t index = 0; index < execution.dynamicBodyCount; ++index)
	{
		const CaseExecutionVector3 position = PyramidWallPosition(wall, index);
		body.position = {position.x, position.y, position.z};
		const b3BodyId id = b3CreateBody(state->world, &body);
		if (!b3Body_IsValid(id) || !b3Shape_IsValid(b3CreateHullShape(id, &shape, &cube.base)))
			return 2;
		state->bodies.push_back(id);
		const b3Pos nativePosition = b3Body_GetPosition(id);
		const b3Quat rotation = b3Body_GetRotation(id);
		const b3Vec3 linear = b3Body_GetLinearVelocity(id);
		const b3Vec3 angular = b3Body_GetAngularVelocity(id);
		const float mass = b3Body_GetMass(id);
		if (nativePosition.x != position.x || nativePosition.y != position.y || nativePosition.z != position.z ||
		    rotation.v.x != 0.0f || rotation.v.y != 0.0f || rotation.v.z != 0.0f || rotation.s != 1.0f ||
		    b3Dot(linear, linear) != 0.0f || b3Dot(angular, angular) != 0.0f ||
		    std::abs(mass - 8.0f * wall.halfExtent * wall.halfExtent * wall.halfExtent * wall.density) >
		        1e-5f * mass || b3Body_IsSleepEnabled(id) != world.enableSleep)
			return 2;
		if (state->verificationMode == VerificationMode_On)
			state->initialPotentialEnergy -= static_cast<double>(mass) *
		    (execution.gravity.x * position.x + execution.gravity.y * position.y + execution.gravity.z * position.z);
	}
	body = b3DefaultBodyDef();
	body.position = {0.0f, -wall.floorHalfExtents.y, 0.0f};
	const b3BodyId floor = b3CreateBody(state->world, &body);
	const b3BoxHull floorHull = b3MakeBoxHull(wall.floorHalfExtents.x, wall.floorHalfExtents.y, wall.floorHalfExtents.z);
	if (!b3Body_IsValid(floor) || !b3Shape_IsValid(b3CreateHullShape(floor, &shape, &floorHull.base)))
		return 2;
	const b3Counters counters = b3World_GetCounters(state->world);
	return counters.bodyCount == static_cast<int>(execution.bodyCount) &&
	    counters.shapeCount == static_cast<int>(execution.shapeCount) ? 0 : 2;
}

void CaptureBox3DPyramidWallObservation(Box3DPyramidWallState* state, int ordinal)
{
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	for (std::uint32_t index = 0; index < state->bodies.size(); ++index)
	{
		const b3BodyId body = state->bodies[index];
		state->observationInputs[ordinal][index] = {b3Body_GetPosition(body), b3Body_GetRotation(body),
		    b3Body_GetLinearVelocity(body), b3Body_GetAngularVelocity(body), b3Body_GetLocalRotationalInertia(body),
		    b3Body_GetMass(body), b3Body_IsSleepEnabled(body) ? 1u : 0u};
	}
	state->observations[ordinal].elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void ReduceBox3DPyramidWallObservation(Box3DPyramidWallState* state, int ordinal)
{
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	PyramidWallObservation& sample = state->observations[ordinal];
	for (std::uint32_t index = 0; index < state->bodies.size(); ++index)
	{
		const Box3DWallBodyInput& input = state->observationInputs[ordinal][index];
		const b3Pos position = input.position;
		const b3Quat rotation = input.rotation;
		const b3Vec3 linear = input.linear;
		const b3Vec3 angular = input.angular;
		const b3Vec3 localAngular = b3InvRotateVector(rotation, angular);
		const b3Matrix3 inertia = input.inertia;
		const double rotationalEnergy = 0.5 * static_cast<double>(b3Dot(localAngular, b3MulMV(inertia, localAngular)));
		AccumulatePyramidWallObservation(state->config.caseExecution->pyramidWall, index,
		                                 {position.x, position.y, position.z},
		                                 {rotation.v.x, rotation.v.y, rotation.v.z, rotation.s},
		                                 {linear.x, linear.y, linear.z}, {angular.x, angular.y, angular.z},
		                                 input.mass, rotationalEnergy,
		                                 input.sleepFlags == (state->config.caseExecution->sleepMode == CaseExecutionToggle_Enabled ? 1u : 0u) ? 1 : 0,
		                                 state->config.caseExecution->gravity, &sample);
	}
	FinishPyramidWallObservation(static_cast<std::uint32_t>(state->bodies.size()), &sample);
	sample.elapsedMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

int StepBox3DPyramidWall(Box3DCaseView* view, int count)
{
	Box3DPyramidWallState* state = static_cast<Box3DPyramidWallState*>(view->value);
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	if (count < 0 || static_cast<std::uint32_t>(count) > execution.measuredWorkUnitCount - state->completed)
		return 2;
	for (int index = 0; index < count; ++index)
	{
		const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		b3World_Step(state->world, 1.0f / execution.timestepHz,
		             static_cast<int>(execution.nativeSolver.values[CaseSolverField_Substeps]));
		const std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - start;
		state->durations[state->completed++] = elapsed.count();
		state->elapsedMs += std::chrono::duration<double, std::milli>(elapsed).count();
		const int observation = PyramidWallObservationIndex(state->completed, execution.measuredWorkUnitCount);
		if (state->verificationMode == VerificationMode_On && observation >= 0)
			CaptureBox3DPyramidWallObservation(state, observation);
	}
	return 0;
}

int PyramidWallBox3DScene(const Box3DCaseView& view, benchmark_visual::VisualGeometry* geometries,
                         benchmark_visual::VisualMeshStorage*, int geometryCapacity,
                         benchmark_visual::VisualInstance* instances, int instanceCapacity,
                         int* geometryCount, int* instanceCount)
{
	using namespace benchmark_visual;
	const Box3DPyramidWallState& state = *static_cast<const Box3DPyramidWallState*>(view.value);
	const CaseExecutionSpec& execution = *state.config.caseExecution;
	const CaseExecutionPyramidWall& wall = execution.pyramidWall;
	if (geometryCapacity < 2 || instanceCapacity < static_cast<int>(execution.visualInstanceCount))
		return 2;
	geometries[0] = {VisualGeometryKind_Box, wall.halfExtent, wall.halfExtent, wall.halfExtent, 0, 0, 0, 0, 0, 0};
	geometries[1] = {VisualGeometryKind_Box, wall.floorHalfExtents.x, wall.floorHalfExtents.y, wall.floorHalfExtents.z, 0, 0, 0, 0, 0, 0};
	for (std::uint32_t index = 0; index < state.bodies.size(); ++index)
	{
		const b3Pos p = b3Body_GetPosition(state.bodies[index]);
		const b3Quat q = b3Body_GetRotation(state.bodies[index]);
		instances[index] = {};
		instances[index].stableSlot = index;
		instances[index].transformSlot = index;
		instances[index].initialTransform = {p.x, p.y, p.z, q.v.x, q.v.y, q.v.z, q.s};
	}
	instances[execution.dynamicBodyCount] = {};
	instances[execution.dynamicBodyCount].geometryIndex = 1;
	instances[execution.dynamicBodyCount].stableSlot = execution.dynamicBodyCount;
	instances[execution.dynamicBodyCount].transformSlot = UINT32_MAX;
	instances[execution.dynamicBodyCount].initialTransform = {0, -wall.floorHalfExtents.y, 0, 0, 0, 0, 1};
	*geometryCount = 2;
	*instanceCount = static_cast<int>(execution.visualInstanceCount);
	return 0;
}

int SampleBox3DPyramidWall(const Box3DCaseView& view, benchmark_visual::VisualStableTransform* transforms, int capacity)
{
	const Box3DPyramidWallState& state = *static_cast<const Box3DPyramidWallState*>(view.value);
	if (capacity < static_cast<int>(state.bodies.size()))
		return 2;
	for (std::uint32_t index = 0; index < state.bodies.size(); ++index)
	{
		const b3Pos p = b3Body_GetPosition(state.bodies[index]);
		const b3Quat q = b3Body_GetRotation(state.bodies[index]);
		transforms[index] = {index, {p.x, p.y, p.z, q.v.x, q.v.y, q.v.z, q.s}};
	}
	return 0;
}

int RunBox3DPyramidWall(const Box3DRunRequest& request)
{
	Box3DPyramidWallState state = {};
	const Box3DCaseConfig config = {&request.caseExecution, request.threadCount, request.repeatIndex,
	                               request.stepCount, request.warmupSteps};
	int status = CreateBox3DPyramidWall(&state, config, request.verificationMode);
	benchmark_stack::Capture capture = {};
	if (status == 0)
	{
		status = request.verificationMode == VerificationMode_On ? benchmark_stack::OpenCapture(request.stackStream, request.caseExecution,
		    "box3d", request.threadCount, request.repeatIndex, &capture) : 0;
		std::vector<benchmark_visual::VisualStableTransform> transforms(request.verificationMode == VerificationMode_On ? request.caseExecution.dynamicBodyCount : 0);
		Box3DCaseView view = {&state};
		for (std::uint32_t step = 0; status == 0 && step <= request.caseExecution.warmupWorkUnitCount; ++step)
		{
			if (step != 0)
				b3World_Step(state.world, 1.0f / request.caseExecution.timestepHz,
				             static_cast<int>(request.caseExecution.nativeSolver.values[CaseSolverField_Substeps]));
			if (request.verificationMode == VerificationMode_On)
			{
				benchmark_stack::BeginFrame(&capture);
				status = SampleBox3DPyramidWall(view, transforms.data(), static_cast<int>(transforms.size()));
				if (status == 0)
					status = benchmark_stack::AppendTransforms(&capture, step == 0 ? benchmark_stack::Phase_Construction : benchmark_stack::Phase_Warmup,
					    0, step, transforms.data());
			}
		}
		if (status == 0)
			status = RecordBox3DCase(request, &view, request.verificationMode == VerificationMode_On ? &capture : nullptr);
	}
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
			ReduceBox3DPyramidWallObservation(&state, static_cast<int>(ordinal));
		std::array<Box3DObservationRow, 37> rows = {};
		std::uint64_t invalid = 0;
		if (request.verificationMode == VerificationMode_Off)
			for (const b3BodyId body : state.bodies)
			{
				const b3Pos position = b3Body_GetPosition(body);
				const b3Quat rotation = b3Body_GetRotation(body);
				if (!b3Body_IsValid(body) || !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
				    !std::isfinite(rotation.v.x) || !std::isfinite(rotation.v.y) || !std::isfinite(rotation.v.z) || !std::isfinite(rotation.s))
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
				Box3DObservationRow& row = rows[(field < 8 ? field * sampleCount : 8 * sampleCount + 1) + sampleIndex];
				row = {ids[field], "observation", PyramidWallObservationStep(request.caseExecution.measuredWorkUnitCount, sampleIndex), Box3DObservationValueType_Float64, 0};
				std::memcpy(&row.valueBits, &values[field], sizeof(double));
				if (field == 6 || field == 7)
				{
					row.valueType = Box3DObservationValueType_Uint64;
					row.valueBits = field == 6 ? sample.escapedBodies : sample.invalidBodies;
				}
			}
		}
		rows[8 * sampleCount] = {"initial_potential_energy", "construction", 0, Box3DObservationValueType_Float64, 0};
		std::memcpy(&rows[8 * sampleCount].valueBits, &state.initialPotentialEnergy, sizeof(double));
		char settings[256] = {};
		std::snprintf(settings, sizeof(settings), "substeps=%u; sleep=%s; ccd=%s; damping=0; friction=%.6g; restitution=%.6g; worker_count=%d",
		    request.caseExecution.nativeSolver.values[CaseSolverField_Substeps],
		    request.caseExecution.sleepMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
		    request.caseExecution.continuousCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
		    request.caseExecution.friction, request.caseExecution.restitution, request.threadCount - 1);
		const char* validity = invalid == 0 && state.completed == request.caseExecution.measuredWorkUnitCount ? "ok" : "invalid_result";
		const Box3DResult result = {"pyramid_wall", 1, settings, static_cast<int>(request.caseExecution.bodyCount), static_cast<int>(request.caseExecution.shapeCount),
		                            0, 0, invalid, validity, validity, request.threadCount, request.threadCount - 1,
		                            static_cast<int>(state.completed), state.elapsedMs, state.durations.data(), rows.data(), request.verificationMode == VerificationMode_On ? 9 * sampleCount + 1 : 0u};
		status = WriteBox3DResult(request, result);
	}
	if (b3World_IsValid(state.world))
		b3DestroyWorld(state.world);
	return status;
}

const Box3DCaseDescriptor& Box3DPyramidWallCaseDescriptor()
{
	static const Box3DCaseDescriptor descriptor = {"box3d", RunBox3DPyramidWall, StepBox3DPyramidWall,
	                                             PyramidWallBox3DScene, SampleBox3DPyramidWall, nullptr};
	return descriptor;
}
}
