#include "box3d_visual_snapshot.h"
#include "box3d_box_container_pile_10k_case.h"

#include "box3d_result_writer.h"
#include "box3d_runner_args.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace box3d_benchmark
{
int RequestedWorkerCount(int threadCount)
{
	return threadCount > 1 ? threadCount - 1 : 0;
}

b3BodyId AddStaticBox(b3WorldId worldId, b3ShapeDef* shapeDef, b3Pos position, float hx, float hy, float hz)
{
	b3BodyDef bodyDef = b3DefaultBodyDef();
	bodyDef.position = position;
	b3BodyId body = b3CreateBody(worldId, &bodyDef);
	b3BoxHull hull = b3MakeBoxHull(hx, hy, hz);
	b3CreateHullShape(body, shapeDef, &hull.base);
	return body;
}

int CreateFixture(b3WorldId worldId, const CaseExecutionSpec& execution, std::vector<b3BodyId>* bodies)
{
	if (bodies == nullptr || execution.fixtureKind != CaseFixtureKind_OpenContainerFallingPile)
		return 2;
	const CaseExecutionOpenContainer& fixture = execution.openContainer;
	b3ShapeDef shapeDef = b3DefaultShapeDef();
	shapeDef.baseMaterial.friction = execution.friction;
	shapeDef.baseMaterial.restitution = execution.restitution;
	shapeDef.density = fixture.density;
	for (std::uint16_t index = 0; index < fixture.staticBoxCount; ++index)
	{
		const CaseExecutionBox& box = fixture.staticBoxes[index];
		AddStaticBox(worldId, &shapeDef, {box.center.x, box.center.y, box.center.z}, box.halfExtents.x,
		             box.halfExtents.y, box.halfExtents.z);
	}

	b3BodyDef bodyDef = b3DefaultBodyDef();
	bodyDef.type = b3_dynamicBody;
	bodyDef.enableSleep = execution.sleepMode == CaseExecutionToggle_Enabled;
	Box3DResolvedShape shape = {};
	if (CreateBox3DResolvedShape(execution.selectedGeometry, execution, &shape) != 0)
		return 2;
	bodyDef.rotation = shape.rotation;
	const float originX = -0.5f * static_cast<float>(fixture.dynamicGrid[0] - 1) * fixture.dynamicSpacing.x;
	const float originZ = -0.5f * static_cast<float>(fixture.dynamicGrid[2] - 1) * fixture.dynamicSpacing.z;
	bodies->clear();
	bodies->reserve(execution.dynamicBodyCount);

	for (std::uint32_t y = 0; y < fixture.dynamicGrid[1]; ++y)
	{
		for (std::uint32_t z = 0; z < fixture.dynamicGrid[2]; ++z)
		{
			for (std::uint32_t x = 0; x < fixture.dynamicGrid[0]; ++x)
			{
				bodyDef.position = {
				    originX + static_cast<float>(x) * fixture.dynamicSpacing.x,
				    fixture.dynamicInitialY + static_cast<float>(y) * fixture.dynamicSpacing.y,
				    originZ + static_cast<float>(z) * fixture.dynamicSpacing.z,
				};
				b3BodyId bodyId = b3CreateBody(worldId, &bodyDef);
				if (AttachBox3DResolvedShape(bodyId, shapeDef, shape) != 0)
				{
					DestroyBox3DResolvedShape(&shape);
					return 2;
				}
				bodies->push_back(bodyId);
			}
		}
	}

	DestroyBox3DResolvedShape(&shape);
	return bodies->size() == execution.dynamicBodyCount ? 0 : 2;
}

b3WorldId CreateBenchmarkWorld(const Box3DCaseConfig& config)
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

std::chrono::steady_clock::duration::rep StepWorldTimed(b3WorldId worldId, float timestep, int substeps)
{
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	b3World_Step(worldId, timestep, substeps);
	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	return (end - start).count();
}

int CreateBox3DCaseState(const Box3DCaseConfig& config, Box3DCaseState* state)
{
	if (state == nullptr)
	{
		return 2;
	}
	state->config = config;
	state->physicsElapsedMs = 0.0;
	state->latestPhysicsStepMs = 0.0;
	state->completedStepCount = 0;
	state->rawStepDurations.resize(static_cast<std::size_t>(config.stepCount));
	if (config.caseExecution == nullptr || config.caseExecution->timestepHz == 0)
		return 2;
	state->worldId = CreateBenchmarkWorld(config);
	if (b3World_GetWorkerCount(state->worldId) != config.threadCount)
	{
		b3DestroyWorld(state->worldId);
		state->worldId = {};
		return 2;
	}
	if (CreateFixture(state->worldId, *config.caseExecution, &state->dynamicBodies) != 0)
	{
		b3DestroyWorld(state->worldId);
		state->worldId = {};
		return 2;
	}
	return 0;
}

int RunBox3DCaseWarmup(const Box3DCaseConfig& config, benchmark_stack::Capture* capture)
{
	if (config.warmupSteps <= 0)
	{
		return 0;
	}
	Box3DCaseState warmupState = {};
	int createStatus = CreateBox3DCaseState(config, &warmupState);
	if (createStatus != 0)
	{
		return createStatus;
	}
	std::vector<benchmark_visual::VisualStableTransform> transforms(capture != nullptr ? config.caseExecution->dynamicBodyCount : 0);
	int status = 0;
	for (int step = 0; status == 0 && step <= config.warmupSteps; ++step)
	{
		if (step != 0)
			b3World_Step(warmupState.worldId, 1.0f / static_cast<float>(config.caseExecution->timestepHz),
			    static_cast<int>(config.caseExecution->nativeSolver.values[CaseSolverField_Substeps]));
		if (capture != nullptr)
		{
			benchmark_stack::BeginFrame(capture);
			status = SampleBox3DTransforms(warmupState, transforms.data(), static_cast<int>(transforms.size()));
			if (status == 0)
				status = benchmark_stack::AppendTransforms(capture, step == 0 ? benchmark_stack::Phase_Construction : benchmark_stack::Phase_Warmup,
				    0, step, transforms.data());
		}
	}
	DestroyBox3DCaseState(&warmupState);
	return status;
}

int StepBox3DCase(Box3DCaseState* state, int stepCount)
{
	if (state == nullptr || stepCount < 0 ||
	    stepCount > static_cast<int>(state->rawStepDurations.size()) - state->completedStepCount)
	{
		return 2;
	}
	const int firstStep = state->completedStepCount;
	for (int step = 0; step < stepCount; ++step)
	{
		state->rawStepDurations[firstStep + step] = StepWorldTimed(
		    state->worldId, 1.0f / static_cast<float>(state->config.caseExecution->timestepHz),
		    static_cast<int>(state->config.caseExecution->nativeSolver.values[CaseSolverField_Substeps]));
	}
	for (int step = 0; step < stepCount; ++step)
	{
		const double milliseconds = std::chrono::duration<double, std::milli>(
		                                std::chrono::steady_clock::duration(state->rawStepDurations[firstStep + step]))
		                                .count();
		state->physicsElapsedMs += milliseconds;
		state->latestPhysicsStepMs = milliseconds;
	}
	state->completedStepCount += stepCount;
	return 0;
}

void DestroyBox3DCaseState(Box3DCaseState* state)
{
	if (state == nullptr)
	{
		return;
	}
	b3DestroyWorld(state->worldId);
	state->worldId = {};
}

int SampleBox3DTransforms(const Box3DCaseState& state, benchmark_visual::VisualStableTransform* transforms,
                          int transformCapacity)
{
	const int dynamicBodyCount = static_cast<int>(state.dynamicBodies.size());
	if (transforms == nullptr || transformCapacity < dynamicBodyCount)
	{
		return 2;
	}
	for (int index = 0; index < dynamicBodyCount; ++index)
	{
		b3Pos position = b3Body_GetPosition(state.dynamicBodies[index]);
		b3Quat rotation = b3Body_GetRotation(state.dynamicBodies[index]);
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

std::uint64_t CountBox3DContainerPileInvalidTransforms(const Box3DCaseState& state)
{
	std::uint64_t invalidTransformCount = 0;
	for (std::size_t index = 0; index < state.dynamicBodies.size(); ++index)
	{
		const b3Pos position = b3Body_GetPosition(state.dynamicBodies[index]);
		const b3Quat rotation = b3Body_GetRotation(state.dynamicBodies[index]);
		if (std::isfinite(position.x) == 0 || std::isfinite(position.y) == 0 || std::isfinite(position.z) == 0 ||
		    std::isfinite(rotation.v.x) == 0 || std::isfinite(rotation.v.y) == 0 || std::isfinite(rotation.v.z) == 0 ||
		    std::isfinite(rotation.s) == 0)
			invalidTransformCount += 1;
	}
	return invalidTransformCount;
}

int RunBox3DContainerPileHeadless(const Box3DRunRequest& request)
{
	const Box3DCaseConfig config = {&request.caseExecution, request.threadCount, request.repeatIndex, request.stepCount,
	                                request.warmupSteps};
	Box3DCaseState state = {};
	if (CreateBox3DCaseState(config, &state) != 0)
	{
		std::fprintf(stderr, "run_failed reason=create_fixture\n");
		return 2;
	}
	benchmark_stack::Capture capture = {};
	int status = request.verificationMode == VerificationMode_On ? benchmark_stack::OpenCapture(request.stackStream, request.caseExecution,
	    kEngineId, request.threadCount, request.repeatIndex, &capture) : 0;
	if (status == 0) status = RunBox3DCaseWarmup(config, request.verificationMode == VerificationMode_On ? &capture : nullptr);
	capture.segment = config.warmupSteps > 0 ? 1 : 0;
	std::vector<benchmark_visual::VisualStableTransform> transforms(request.verificationMode == VerificationMode_On ? request.caseExecution.dynamicBodyCount : 0);
	if (request.verificationMode == VerificationMode_On)
	{
		benchmark_stack::BeginFrame(&capture);
		if (status == 0) status = SampleBox3DTransforms(state, transforms.data(), static_cast<int>(transforms.size()));
		if (status == 0) status = benchmark_stack::AppendTransforms(&capture, benchmark_stack::Phase_Construction, capture.segment, 0, transforms.data());
	}
	Box3DCaseView recordingState = {&state};
	if (status == 0) status = RecordBox3DCase(request, &recordingState, request.verificationMode == VerificationMode_On ? &capture : nullptr);
	const int closed = request.verificationMode == VerificationMode_On ? benchmark_stack::CloseCapture(&capture) : 0;
	if (status == 0) status = closed;
	if (status == 0)
	{
		const std::uint64_t invalidTransformCount = CountBox3DContainerPileInvalidTransforms(state);
		const b3Counters counters = b3World_GetCounters(state.worldId);
		const int caseValid = invalidTransformCount == 0;
		const int metricValid = state.completedStepCount == request.stepCount && state.physicsElapsedMs > 0.0 &&
		                        std::isfinite(state.physicsElapsedMs) != 0 &&
		                        state.rawStepDurations.size() == static_cast<std::size_t>(request.stepCount);
		std::array<char, 256> physicsSettings = {};
		FormatBox3DContainerPilePhysicsSettings(request.caseExecution, request.threadCount, physicsSettings.data(),
		                                        physicsSettings.size());
		const Box3DResult result = {
		    request.caseExecution.fixtureSemantic,
		    request.caseExecution.fixtureRevision,
		    physicsSettings.data(),
		    counters.bodyCount,
		    counters.shapeCount,
		    static_cast<int>(request.caseExecution.queryCount),
		    static_cast<int>(request.caseExecution.constraintCount),
		    invalidTransformCount,
		    caseValid != 0 ? "ok" : "invalid_result",
		    caseValid != 0 && metricValid != 0 ? "ok" : "invalid_result",
		    request.threadCount,
		    RequestedWorkerCount(request.threadCount),
		    state.completedStepCount,
		    state.physicsElapsedMs,
		    state.rawStepDurations.data(),
		};
		status = WriteBox3DResult(request, result);
	}
	DestroyBox3DCaseState(&state);
	return status;
}

int FormatBox3DContainerPilePhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                            std::size_t settingsCapacity)
{
	if (settings == nullptr || settingsCapacity == 0)
		return 2;
	const int size =
	    std::snprintf(settings, settingsCapacity, "substeps=%u; sleep=%s; ccd=%s; worker_count=%d",
		              execution.nativeSolver.values[CaseSolverField_Substeps],
		              execution.sleepMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
		              execution.continuousCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
		              RequestedWorkerCount(threadCount));
	return size > 0 && static_cast<std::size_t>(size) < settingsCapacity ? 0 : 2;
}

namespace
{
int StepContainerPileVisual(Box3DCaseView* state, int workUnitCount)
{
	return state == nullptr || state->value == nullptr
	           ? 2
			   : StepBox3DCase(static_cast<Box3DCaseState*>(state->value), workUnitCount);
}

int BuildContainerPileVisualScene(const Box3DCaseView& state, benchmark_visual::VisualGeometry* geometries,
                                  benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                                  benchmark_visual::VisualInstance* instances, int instanceCapacity, int* geometryCount,
                                  int* instanceCount)
{
	if (state.value == nullptr || geometries == nullptr || geometryCapacity < 4 || instances == nullptr ||
	    geometryCount == nullptr || instanceCount == nullptr)
		return 2;
	const Box3DCaseState& value = *static_cast<const Box3DCaseState*>(state.value);
	const CaseExecutionSpec& execution = *value.config.caseExecution;
	const CaseExecutionOpenContainer& fixture = execution.openContainer;
	if (geometryCapacity < 1 + fixture.staticBoxCount || instanceCapacity < static_cast<int>(execution.bodyCount))
		return 2;
	if (BuildResolvedVisualGeometry(execution, execution.selectedGeometry, meshes, &geometries[0]) != 0)
		return 2;
	for (std::uint16_t index = 0; index < fixture.staticBoxCount; ++index)
	{
		const CaseExecutionVector3 halfExtents = fixture.staticBoxes[index].halfExtents;
		geometries[index + 1] = {
		    benchmark_visual::VisualGeometryKind_Box, halfExtents.x, halfExtents.y, halfExtents.z, 0, 0, 0, 0, 0, 0};
	}
	const int dynamicBodyCount = static_cast<int>(value.dynamicBodies.size());
	for (int index = 0; index < dynamicBodyCount; ++index)
	{
		const b3Pos position = b3Body_GetPosition(value.dynamicBodies[index]);
		const b3Quat rotation = b3Body_GetRotation(value.dynamicBodies[index]);
		instances[index] = {};
		instances[index].geometryIndex = 0;
		instances[index].stableSlot = static_cast<std::uint32_t>(index);
		instances[index].transformSlot = static_cast<std::uint32_t>(index);
		instances[index].initialTransform = {position.x,   position.y,   position.z, rotation.v.x,
		                                     rotation.v.y, rotation.v.z, rotation.s};
	}
	for (std::uint16_t index = 0; index < fixture.staticBoxCount; ++index)
	{
		const CaseExecutionBox& box = fixture.staticBoxes[index];
		benchmark_visual::VisualInstance& instance = instances[dynamicBodyCount + index];
		instance = {};
		instance.geometryIndex = index + 1;
		instance.stableSlot = static_cast<std::uint32_t>(dynamicBodyCount + index);
		instance.transformSlot = UINT32_MAX;
		instance.initialTransform = {box.center.x, box.center.y, box.center.z, 0.0f, 0.0f, 0.0f, 1.0f};
	}
	*geometryCount = 1 + fixture.staticBoxCount;
	*instanceCount = static_cast<int>(execution.bodyCount);
	return 0;
}

int SampleContainerPileVisualTransforms(const Box3DCaseView& state, benchmark_visual::VisualStableTransform* transforms,
                                        int capacity)
{
	return SampleBox3DTransforms(*static_cast<const Box3DCaseState*>(state.value), transforms, capacity);
}

}

const Box3DCaseDescriptor& Box3DContainerPileCaseDescriptor()
{
	static const Box3DCaseDescriptor descriptor = {
	    kEngineId,
	    RunBox3DContainerPileHeadless,
	    StepContainerPileVisual,
	    BuildContainerPileVisualScene,
	    SampleContainerPileVisualTransforms,
	    nullptr,
	};
	return descriptor;
}

}
