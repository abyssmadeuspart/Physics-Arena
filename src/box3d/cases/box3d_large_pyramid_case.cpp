#include "box3d_visual_snapshot.h"
#include "box3d_large_pyramid_case.h"

#include "box3d_result_writer.h"
#include "box3d_runner_args.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <new>

namespace box3d_benchmark
{
int Box3DLargePyramidWorkerCount(int threadCount)
{
	return threadCount > 1 ? threadCount - 1 : 0;
}

b3WorldId CreateBox3DLargePyramidWorld(const Box3DCaseConfig& config)
{
	const CaseExecutionSpec& execution = *config.caseExecution;
	b3WorldDef worldDef = b3DefaultWorldDef();
	worldDef.gravity = {execution.gravity.x, execution.gravity.y, execution.gravity.z};
	worldDef.enableContinuous = execution.continuousCollisionMode == CaseExecutionToggle_Enabled;
	worldDef.workerCount = config.threadCount;
	worldDef.capacity.staticShapeCount = static_cast<int>(execution.staticBodyCount);
	worldDef.capacity.dynamicShapeCount = static_cast<int>(execution.dynamicBodyCount);
	worldDef.capacity.staticBodyCount = static_cast<int>(execution.staticBodyCount);
	worldDef.capacity.dynamicBodyCount = static_cast<int>(execution.dynamicBodyCount);
	worldDef.capacity.contactCount = static_cast<int>(execution.dynamicBodyCount) * 16;
	return b3CreateWorld(&worldDef);
}

int CreateBox3DLargePyramidFixture(Box3DLargePyramidState* state)
{
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	if (execution.fixtureKind != CaseFixtureKind_LargePyramid)
		return 2;
	const CaseExecutionLargePyramid& fixture = execution.largePyramid;
	b3ShapeDef boxShapeDef = b3DefaultShapeDef();
	boxShapeDef.baseMaterial.friction = execution.friction;
	boxShapeDef.baseMaterial.restitution = execution.restitution;
	boxShapeDef.density = fixture.boxDensity;
	Box3DResolvedShape shape = {};
	if (CreateBox3DResolvedShape(execution.selectedGeometry, execution, &shape) != 0)
		return 2;
	b3BodyDef dynamicBodyDef = b3DefaultBodyDef();
	dynamicBodyDef.type = b3_dynamicBody;
	dynamicBodyDef.rotation = shape.rotation;
	dynamicBodyDef.enableSleep = execution.sleepMode == CaseExecutionToggle_Enabled;
	state->dynamicBodies.clear();
	state->dynamicBodies.reserve(execution.dynamicBodyCount);
	for (std::uint32_t layer = 0; layer < fixture.rowCount; ++layer)
	{
		const std::uint32_t layerSide = fixture.rowCount - layer;
		for (std::uint32_t depth = 0; depth < layerSide; ++depth)
		{
			for (std::uint32_t column = 0; column < layerSide; ++column)
			{
				dynamicBodyDef.position = {
				    fixture.baseCenter.x +
				        (static_cast<float>(column) - 0.5f * static_cast<float>(layerSide - 1u)) * fixture.boxSpacing.x,
				    fixture.baseCenter.y + static_cast<float>(layer) * fixture.boxSpacing.y,
				    fixture.baseCenter.z +
				        (static_cast<float>(depth) - 0.5f * static_cast<float>(layerSide - 1u)) * fixture.boxSpacing.z,
				};
				const b3BodyId body = b3CreateBody(state->worldId, &dynamicBodyDef);
				if (AttachBox3DResolvedShape(body, boxShapeDef, shape) != 0)
				{
					DestroyBox3DResolvedShape(&shape);
					return 2;
				}
				state->dynamicBodies.push_back(body);
			}
		}
	}
	b3ShapeDef projectileShapeDef = boxShapeDef;
	DestroyBox3DResolvedShape(&shape);
	dynamicBodyDef.rotation = b3Quat_identity;
	projectileShapeDef.density = fixture.projectileDensity;
	const b3Sphere sphere = {{0.0f, 0.0f, 0.0f}, fixture.projectileRadius};
	for (std::uint32_t index = 0; index < fixture.projectileCount; ++index)
	{
		dynamicBodyDef.position = {
		    fixture.projectileInitialCenter.x + index * fixture.projectileCenterSpacing.x,
		    fixture.projectileInitialCenter.y + index * fixture.projectileCenterSpacing.y,
		    fixture.projectileInitialCenter.z + index * fixture.projectileCenterSpacing.z,
		};
		const b3BodyId projectile = b3CreateBody(state->worldId, &dynamicBodyDef);
		b3CreateSphereShape(projectile, &projectileShapeDef, &sphere);
		state->dynamicBodies.push_back(projectile);
	}

	b3BodyDef floorBodyDef = b3DefaultBodyDef();
	floorBodyDef.position = {0.0f, -fixture.floorHalfExtents.y, 0.0f};
	const b3BodyId floor = b3CreateBody(state->worldId, &floorBodyDef);
	const b3BoxHull floorHull =
	    b3MakeBoxHull(fixture.floorHalfExtents.x, fixture.floorHalfExtents.y, fixture.floorHalfExtents.z);
	b3CreateHullShape(floor, &boxShapeDef, &floorHull.base);
	return state->dynamicBodies.size() == execution.dynamicBodyCount ? 0 : 2;
}

int CreateBox3DLargePyramidState(const Box3DCaseConfig& config, Box3DLargePyramidState* state)
{
	if (state == nullptr || config.caseExecution == nullptr || config.caseExecution->timestepHz == 0)
		return 2;
	state->config = config;
	state->physicsElapsedMs = 0.0;
	state->latestPhysicsStepMs = 0.0;
	state->completedStepCount = 0;
	state->rawStepDurations.resize(static_cast<std::size_t>(config.stepCount));
	state->worldId = CreateBox3DLargePyramidWorld(config);
	if (b3World_GetWorkerCount(state->worldId) != config.threadCount)
	{
		b3DestroyWorld(state->worldId);
		state->worldId = {};
		return 2;
	}
	if (CreateBox3DLargePyramidFixture(state) != 0)
	{
		b3DestroyWorld(state->worldId);
		state->worldId = {};
		return 2;
	}
	return 0;
}

int WarmupBox3DLargePyramidState(Box3DLargePyramidState* state, int workUnitCount)
{
	if (state == nullptr || workUnitCount < 0)
		return 2;
	const float timestep = 1.0f / static_cast<float>(state->config.caseExecution->timestepHz);
	for (int step = 0; step < workUnitCount; ++step)
		b3World_Step(state->worldId, timestep,
		             static_cast<int>(state->config.caseExecution->nativeSolver.values[CaseSolverField_Substeps]));
	return 0;
}

int StepBox3DLargePyramidState(Box3DLargePyramidState* state, int workUnitCount)
{
	if (state == nullptr || workUnitCount < 0 ||
	    workUnitCount > static_cast<int>(state->rawStepDurations.size()) - state->completedStepCount)
		return 2;
	const int firstStep = state->completedStepCount;
	const CaseExecutionLargePyramid& fixture = state->config.caseExecution->largePyramid;
	const float timestep = 1.0f / static_cast<float>(state->config.caseExecution->timestepHz);
	for (int step = 0; step < workUnitCount; ++step)
	{
		if (firstStep + step == static_cast<int>(fixture.projectileLaunchAfterWorkUnits))
		{
			const std::size_t firstProjectile = state->dynamicBodies.size() - fixture.projectileCount;
			for (std::size_t index = firstProjectile; index < state->dynamicBodies.size(); ++index)
			{
				b3Body_SetLinearVelocity(state->dynamicBodies[index],
				                         {fixture.projectileLaunchVelocity.x, fixture.projectileLaunchVelocity.y,
				                          fixture.projectileLaunchVelocity.z});
				b3Body_SetAwake(state->dynamicBodies[index], true);
			}
		}
		const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		b3World_Step(state->worldId, timestep,
		             static_cast<int>(state->config.caseExecution->nativeSolver.values[CaseSolverField_Substeps]));
		const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
		state->rawStepDurations[firstStep + step] = (end - start).count();
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

void DestroyBox3DLargePyramidState(Box3DLargePyramidState* state)
{
	if (state == nullptr)
		return;
	b3DestroyWorld(state->worldId);
	state->worldId = {};
	state->dynamicBodies = {};
	state->rawStepDurations = {};
}

int SampleBox3DLargePyramidTransforms(const Box3DLargePyramidState& state,
                                      benchmark_visual::VisualStableTransform* transforms, int transformCapacity)
{
	const int dynamicBodyCount = static_cast<int>(state.dynamicBodies.size());
	if (transforms == nullptr || transformCapacity < dynamicBodyCount)
		return 2;
	for (int index = 0; index < dynamicBodyCount; ++index)
	{
		const b3Pos position = b3Body_GetPosition(state.dynamicBodies[index]);
		const b3Quat rotation = b3Body_GetRotation(state.dynamicBodies[index]);
		transforms[index] = {
		    static_cast<std::uint32_t>(index),
		    {position.x, position.y, position.z, rotation.v.x, rotation.v.y, rotation.v.z, rotation.s}};
	}
	return 0;
}

std::uint64_t CountBox3DLargePyramidInvalidTransforms(const Box3DLargePyramidState& state)
{
	std::uint64_t invalidTransformCount = 0;
	for (std::size_t index = 0; index < state.dynamicBodies.size(); ++index)
	{
		const b3Pos position = b3Body_GetPosition(state.dynamicBodies[index]);
		const b3Quat rotation = b3Body_GetRotation(state.dynamicBodies[index]);
		if (std::isfinite(position.x) == 0 || std::isfinite(position.y) == 0 || std::isfinite(position.z) == 0 ||
		    std::isfinite(rotation.v.x) == 0 || std::isfinite(rotation.v.y) == 0 || std::isfinite(rotation.v.z) == 0 ||
		    std::isfinite(rotation.s) == 0)
			++invalidTransformCount;
	}
	return invalidTransformCount;
}

int FormatBox3DLargePyramidPhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                           std::size_t settingsCapacity)
{
	if (settings == nullptr || settingsCapacity == 0)
		return 2;
	const int size =
	    std::snprintf(settings, settingsCapacity, "substeps=%u; sleep=%s; ccd=%s; worker_count=%d",
		              execution.nativeSolver.values[CaseSolverField_Substeps],
		              execution.sleepMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
		              execution.continuousCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
		              Box3DLargePyramidWorkerCount(threadCount));
	return size > 0 && static_cast<std::size_t>(size) < settingsCapacity ? 0 : 2;
}

int RunBox3DLargePyramidHeadless(const Box3DRunRequest& request)
{
	const Box3DCaseConfig config = {&request.caseExecution, request.threadCount, request.repeatIndex, request.stepCount,
	                                request.warmupSteps};
	Box3DLargePyramidState state = {};
	if (CreateBox3DLargePyramidState(config, &state) != 0)
		return 2;
	benchmark_stack::Capture capture = {};
	std::vector<benchmark_visual::VisualStableTransform> capturedTransforms(request.verificationMode == VerificationMode_On ? request.caseExecution.dynamicBodyCount : 0);
	const Box3DCaseDescriptor* captureDescriptor = nullptr;
	int status = ResolveBox3DCase(request.caseExecution, &captureDescriptor);
	if (status == 0)
		status = request.verificationMode == VerificationMode_On ? benchmark_stack::OpenCapture(request.stackStream, request.caseExecution,
		    captureDescriptor->engineId, request.threadCount, request.repeatIndex, &capture) : 0;
	Box3DCaseView recordingState = {&state};
	for (std::uint32_t ordinal = 0; status == 0 && ordinal <= request.caseExecution.warmupWorkUnitCount; ++ordinal)
	{
		if (ordinal != 0)
			status = WarmupBox3DLargePyramidState(&state, 1);
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
		status = RecordBox3DCase(request, &recordingState, request.verificationMode == VerificationMode_On ? &capture : nullptr);
	const int captureStatus = request.verificationMode == VerificationMode_On ? benchmark_stack::CloseCapture(&capture) : 0;
	if (status == 0)
		status = captureStatus;
	if (status == 0)
	{
		const std::uint64_t invalidTransformCount = CountBox3DLargePyramidInvalidTransforms(state);
		const b3Counters counters = b3World_GetCounters(state.worldId);
		const int caseValid = invalidTransformCount == 0 &&
		                      counters.bodyCount == static_cast<int>(request.caseExecution.bodyCount) &&
		                      counters.shapeCount == static_cast<int>(request.caseExecution.shapeCount);
		const int metricValid = state.completedStepCount == request.stepCount && state.physicsElapsedMs > 0.0 &&
		                        std::isfinite(state.physicsElapsedMs) != 0;
		std::array<char, 256> physicsSettings = {};
		FormatBox3DLargePyramidPhysicsSettings(request.caseExecution, request.threadCount, physicsSettings.data(),
		                                       physicsSettings.size());
		const Box3DResult result = {request.caseExecution.fixtureSemantic,
		                            request.caseExecution.fixtureRevision,
		                            physicsSettings.data(),
		                            counters.bodyCount,
		                            counters.shapeCount,
		                            0,
		                            0,
		                            invalidTransformCount,
		                            caseValid != 0 ? "ok" : "invalid_result",
		                            caseValid != 0 && metricValid != 0 ? "ok" : "invalid_result",
		                            request.threadCount,
		                            Box3DLargePyramidWorkerCount(request.threadCount),
		                            state.completedStepCount,
		                            state.physicsElapsedMs,
		                            state.rawStepDurations.data()};
		status = WriteBox3DResult(request, result);
	}
	DestroyBox3DLargePyramidState(&state);
	return status;
}

int StepBox3DLargePyramidVisual(Box3DCaseView* state, int workUnitCount)
{
	return state == nullptr || state->value == nullptr
	           ? 2
			   : StepBox3DLargePyramidState(static_cast<Box3DLargePyramidState*>(state->value), workUnitCount);
}

int BuildBox3DLargePyramidVisualScene(const Box3DCaseView& state, benchmark_visual::VisualGeometry* geometries,
                                      benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                                      benchmark_visual::VisualInstance* instances, int instanceCapacity,
                                      int* geometryCount, int* instanceCount)
{
	if (state.value == nullptr || geometries == nullptr || geometryCapacity < 3 || instances == nullptr ||
	    geometryCount == nullptr || instanceCount == nullptr)
		return 2;
	const Box3DLargePyramidState& value = *static_cast<const Box3DLargePyramidState*>(state.value);
	const CaseExecutionSpec& execution = *value.config.caseExecution;
	const CaseExecutionLargePyramid& fixture = execution.largePyramid;
	if (instanceCapacity < static_cast<int>(execution.visualInstanceCount))
		return 2;
	if (BuildResolvedVisualGeometry(execution, execution.selectedGeometry, meshes, &geometries[0]) != 0)
		return 2;
	geometries[1] = {
	    benchmark_visual::VisualGeometryKind_Sphere, fixture.projectileRadius, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0};
	geometries[2] = {benchmark_visual::VisualGeometryKind_Box,
	                 fixture.floorHalfExtents.x,
	                 fixture.floorHalfExtents.y,
	                 fixture.floorHalfExtents.z,
	                 0,
	                 0,
	                 0,
	                 0,
	                 0,
	                 0};
	const int dynamicBodyCount = static_cast<int>(value.dynamicBodies.size());
	const int firstProjectile = dynamicBodyCount - static_cast<int>(fixture.projectileCount);
	for (int index = 0; index < dynamicBodyCount; ++index)
	{
		const b3Pos position = b3Body_GetPosition(value.dynamicBodies[index]);
		const b3Quat rotation = b3Body_GetRotation(value.dynamicBodies[index]);
		instances[index] = {};
		instances[index].geometryIndex = index >= firstProjectile ? 1u : 0u;
		instances[index].stableSlot = static_cast<std::uint32_t>(index);
		instances[index].transformSlot = static_cast<std::uint32_t>(index);
		instances[index].initialTransform = {position.x,   position.y,   position.z, rotation.v.x,
		                                     rotation.v.y, rotation.v.z, rotation.s};
	}
	benchmark_visual::VisualInstance& floor = instances[dynamicBodyCount];
	floor = {};
	floor.geometryIndex = 2u;
	floor.stableSlot = static_cast<std::uint32_t>(dynamicBodyCount);
	floor.transformSlot = UINT32_MAX;
	floor.initialTransform = {0.0f, -fixture.floorHalfExtents.y, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
	*geometryCount = 3;
	*instanceCount = static_cast<int>(execution.visualInstanceCount);
	return 0;
}

int SampleBox3DLargePyramidVisualTransforms(const Box3DCaseView& state,
                                            benchmark_visual::VisualStableTransform* transforms, int capacity)
{
	return state.value == nullptr ? 2
	                              : SampleBox3DLargePyramidTransforms(
	                                    *static_cast<const Box3DLargePyramidState*>(state.value), transforms, capacity);
}

const Box3DCaseDescriptor& Box3DLargePyramidCaseDescriptor()
{
	static const Box3DCaseDescriptor descriptor = {
	    "box3d",
	    RunBox3DLargePyramidHeadless,
	    StepBox3DLargePyramidVisual,
	    BuildBox3DLargePyramidVisualScene,
	    SampleBox3DLargePyramidVisualTransforms,
	    nullptr,
	};
	return descriptor;
}
}
