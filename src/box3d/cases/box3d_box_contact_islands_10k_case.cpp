#include "box3d_visual_snapshot.h"
#include "box3d_box_contact_islands_10k_case.h"

#include "box3d_result_writer.h"
#include "box3d_runner_args.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <new>

namespace box3d_benchmark
{
namespace
{
float IslandOrigin(float spacing, std::uint32_t coordinate, std::uint32_t count)
{
	return spacing * (static_cast<float>(coordinate) - 0.5f * static_cast<float>(count - 1));
}

b3BodyId AddContactIslandStaticBox(b3WorldId worldId, b3ShapeDef* shapeDef, float x, float y, float z, float hx,
                                   float hy, float hz)
{
	b3BodyDef bodyDef = b3DefaultBodyDef();
	bodyDef.position = {x, y, z};
	b3BodyId body = b3CreateBody(worldId, &bodyDef);
	b3BoxHull hull = b3MakeBoxHull(hx, hy, hz);
	b3CreateHullShape(body, shapeDef, &hull.base);
	return body;
}

int CreateContactIslandFixture(Box3DContactIslandsState* state)
{
	if (state == nullptr || state->config.caseExecution == nullptr)
		return 2;
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	const CaseExecutionContactIslands& fixture = execution.contactIslands;
	b3ShapeDef shapeDef = b3DefaultShapeDef();
	shapeDef.baseMaterial.friction = execution.friction;
	shapeDef.baseMaterial.restitution = execution.restitution;
	shapeDef.density = fixture.density;
	b3BodyDef bodyDef = b3DefaultBodyDef();
	bodyDef.type = b3_dynamicBody;
	bodyDef.enableSleep = execution.sleepMode == CaseExecutionToggle_Enabled;
	Box3DResolvedShape shape = {};
	if (CreateBox3DResolvedShape(execution.selectedGeometry, execution, &shape) != 0)
		return 2;
	bodyDef.rotation = shape.rotation;
	int slot = 0;
	for (std::uint32_t groupZ = 0; groupZ < fixture.islandGrid[1]; ++groupZ)
		for (std::uint32_t groupX = 0; groupX < fixture.islandGrid[0]; ++groupX)
			AddContactIslandStaticBox(
			    state->worldId, &shapeDef, IslandOrigin(fixture.islandSpacing[0], groupX, fixture.islandGrid[0]),
			    -fixture.floorHalfExtents.y, IslandOrigin(fixture.islandSpacing[1], groupZ, fixture.islandGrid[1]),
			    fixture.floorHalfExtents.x, fixture.floorHalfExtents.y, fixture.floorHalfExtents.z);
	for (std::uint32_t groupZ = 0; groupZ < fixture.islandGrid[1]; ++groupZ)
	{
		for (std::uint32_t groupX = 0; groupX < fixture.islandGrid[0]; ++groupX)
		{
			const float originX = IslandOrigin(fixture.islandSpacing[0], groupX, fixture.islandGrid[0]);
			const float originZ = IslandOrigin(fixture.islandSpacing[1], groupZ, fixture.islandGrid[1]);
			for (std::uint32_t y = 0; y < fixture.bodyGrid[1]; ++y)
			{
				for (std::uint32_t z = 0; z < fixture.bodyGrid[2]; ++z)
				{
					for (std::uint32_t x = 0; x < fixture.bodyGrid[0]; ++x)
					{
						bodyDef.position = {
						    originX + (static_cast<float>(x) - 0.5f * static_cast<float>(fixture.bodyGrid[0] - 1)) *
						                  fixture.bodySpacing.x,
						    fixture.bodyInitialY + static_cast<float>(y) * fixture.bodySpacing.y,
						    originZ + (static_cast<float>(z) - 0.5f * static_cast<float>(fixture.bodyGrid[2] - 1)) *
						                  fixture.bodySpacing.z,
						};
						const b3BodyId body = b3CreateBody(state->worldId, &bodyDef);
						if (AttachBox3DResolvedShape(body, shapeDef, shape) != 0)
						{
							DestroyBox3DResolvedShape(&shape);
							return 2;
						}
						state->dynamicBodies[slot++] = body;
					}
				}
			}
		}
	}
	DestroyBox3DResolvedShape(&shape);
	return slot == static_cast<int>(execution.dynamicBodyCount) ? 0 : 2;
}

std::chrono::steady_clock::duration::rep StepContactIslandWorldTimed(b3WorldId worldId, float timestep, int substeps)
{
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	b3World_Step(worldId, timestep, substeps);
	const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	return (end - start).count();
}
}

int CreateBox3DContactIslandsState(const Box3DCaseConfig& config, Box3DContactIslandsState* state)
{
	if (state == nullptr || config.caseExecution == nullptr ||
	    config.caseExecution->fixtureKind != CaseFixtureKind_BoxContactIslands ||
	    config.stepCount != static_cast<int>(config.caseExecution->measuredWorkUnitCount) ||
	    config.warmupSteps != static_cast<int>(config.caseExecution->warmupWorkUnitCount))
		return 2;
	*state = {};
	state->config = config;
	const CaseExecutionSpec& execution = *config.caseExecution;
	state->dynamicBodies = new (std::nothrow) b3BodyId[execution.dynamicBodyCount]();
	state->rawWorkUnitDurations =
	    new (std::nothrow) std::chrono::steady_clock::duration::rep[execution.measuredWorkUnitCount]();
	if (state->dynamicBodies == nullptr || state->rawWorkUnitDurations == nullptr)
	{
		DestroyBox3DContactIslandsState(state);
		return 2;
	}
	b3WorldDef worldDef = b3DefaultWorldDef();
	worldDef.gravity = {execution.gravity.x, execution.gravity.y, execution.gravity.z};
	worldDef.enableContinuous = execution.continuousCollisionMode == CaseExecutionToggle_Enabled;
	worldDef.workerCount = config.threadCount;
	worldDef.capacity.staticShapeCount = static_cast<int>(execution.staticBodyCount);
	worldDef.capacity.dynamicShapeCount = static_cast<int>(execution.dynamicBodyCount);
	worldDef.capacity.staticBodyCount = static_cast<int>(execution.staticBodyCount);
	worldDef.capacity.dynamicBodyCount = static_cast<int>(execution.dynamicBodyCount);
	worldDef.capacity.contactCount = static_cast<int>(execution.dynamicBodyCount) * 16;
	state->worldId = b3CreateWorld(&worldDef);
	if (b3World_GetWorkerCount(state->worldId) != config.threadCount)
	{
		DestroyBox3DContactIslandsState(state);
		return 2;
	}
	if (CreateContactIslandFixture(state) != 0)
	{
		DestroyBox3DContactIslandsState(state);
		return 2;
	}
	return 0;
}

int WarmupBox3DContactIslands(Box3DContactIslandsState* state, int workUnitCount)
{
	if (state == nullptr || state->config.caseExecution == nullptr ||
	    (workUnitCount < 0 || workUnitCount > static_cast<int>(state->config.caseExecution->warmupWorkUnitCount)))
		return 2;
	const float timestep = 1.0f / static_cast<float>(state->config.caseExecution->timestepHz);
	for (int workUnit = 0; workUnit < workUnitCount; ++workUnit)
		b3World_Step(state->worldId, timestep,
		             static_cast<int>(state->config.caseExecution->nativeSolver.values[CaseSolverField_Substeps]));
	return 0;
}

int StepBox3DContactIslands(Box3DContactIslandsState* state, int workUnitCount)
{
	if (state == nullptr || workUnitCount < 0 || state->config.caseExecution == nullptr ||
	    workUnitCount >
	        static_cast<int>(state->config.caseExecution->measuredWorkUnitCount) - state->completedWorkUnitCount)
		return 2;
	const int firstWorkUnit = state->completedWorkUnitCount;
	const float timestep = 1.0f / static_cast<float>(state->config.caseExecution->timestepHz);
	for (int workUnit = 0; workUnit < workUnitCount; ++workUnit)
		state->rawWorkUnitDurations[firstWorkUnit + workUnit] = StepContactIslandWorldTimed(
		    state->worldId, timestep,
		    static_cast<int>(state->config.caseExecution->nativeSolver.values[CaseSolverField_Substeps]));
	for (int workUnit = 0; workUnit < workUnitCount; ++workUnit)
	{
		const double milliseconds =
		    std::chrono::duration<double, std::milli>(
		        std::chrono::steady_clock::duration(state->rawWorkUnitDurations[firstWorkUnit + workUnit]))
		        .count();
		state->workloadElapsedMs += milliseconds;
		state->latestWorkUnitElapsedMs = milliseconds;
	}
	state->completedWorkUnitCount += workUnitCount;
	return 0;
}

void DestroyBox3DContactIslandsState(Box3DContactIslandsState* state)
{
	if (state == nullptr)
		return;
	if (B3_IS_NON_NULL(state->worldId))
		b3DestroyWorld(state->worldId);
	delete[] state->dynamicBodies;
	delete[] state->rawWorkUnitDurations;
	*state = {};
}

int SampleBox3DContactIslandsTransforms(const Box3DContactIslandsState& state,
                                        benchmark_visual::VisualStableTransform* transforms, int transformCapacity)
{
	if (state.config.caseExecution == nullptr || transforms == nullptr ||
	    transformCapacity < static_cast<int>(state.config.caseExecution->dynamicBodyCount))
		return 2;
	for (std::uint32_t index = 0; index < state.config.caseExecution->dynamicBodyCount; ++index)
	{
		const b3Pos position = b3Body_GetPosition(state.dynamicBodies[index]);
		const b3Quat rotation = b3Body_GetRotation(state.dynamicBodies[index]);
		transforms[index] = {
		    static_cast<std::uint32_t>(index),
		    {position.x, position.y, position.z, rotation.v.x, rotation.v.y, rotation.v.z, rotation.s}};
	}
	return 0;
}

int ValidateBox3DContactIslands(const Box3DContactIslandsState& state, std::uint64_t* invalidTransformCount)
{
	if (invalidTransformCount == nullptr)
		return 2;
	*invalidTransformCount = 0;
	if (state.config.caseExecution == nullptr)
		return 2;
	for (std::uint32_t slot = 0; slot < state.config.caseExecution->dynamicBodyCount; ++slot)
	{
		const b3Pos position = b3Body_GetPosition(state.dynamicBodies[slot]);
		const b3Quat rotation = b3Body_GetRotation(state.dynamicBodies[slot]);
		if (std::isfinite(position.x) == 0 || std::isfinite(position.y) == 0 || std::isfinite(position.z) == 0 ||
		    std::isfinite(rotation.v.x) == 0 || std::isfinite(rotation.v.y) == 0 || std::isfinite(rotation.v.z) == 0 ||
		    std::isfinite(rotation.s) == 0)
			*invalidTransformCount += 1;
	}
	return 0;
}

int RunBox3DContactIslandsHeadless(const Box3DRunRequest& request)
{
	const Box3DCaseConfig config = {&request.caseExecution, request.threadCount, request.repeatIndex, request.stepCount,
	                                request.warmupSteps};
	Box3DContactIslandsState state = {};
	if (CreateBox3DContactIslandsState(config, &state) != 0)
	{
		std::fprintf(stderr, "run_failed reason=create_fixture\n");
		return 2;
	}
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
			status = WarmupBox3DContactIslands(&state, 1);
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
		std::uint64_t invalidTransformCount = 0;
		const int validationStatus = ValidateBox3DContactIslands(state, &invalidTransformCount);
		const b3Counters counters = b3World_GetCounters(state.worldId);
		const int caseValid = validationStatus == 0 && invalidTransformCount == 0 &&
		                      counters.bodyCount == static_cast<int>(request.caseExecution.bodyCount) &&
		                      counters.shapeCount == static_cast<int>(request.caseExecution.shapeCount);
		const int metricValid = state.completedWorkUnitCount == request.stepCount && state.workloadElapsedMs > 0.0 &&
		                        std::isfinite(state.workloadElapsedMs) != 0;
		std::array<char, 256> physicsSettings = {};
		FormatBox3DContactIslandsPhysicsSettings(request.caseExecution, request.threadCount, physicsSettings.data(),
		                                         physicsSettings.size());
		const Box3DResult result = {
		    request.caseExecution.fixtureSemantic,
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
		    RequestedWorkerCount(request.threadCount),
		    state.completedWorkUnitCount,
		    state.workloadElapsedMs,
		    state.rawWorkUnitDurations,
		};
		status = WriteBox3DResult(request, result);
	}
	DestroyBox3DContactIslandsState(&state);
	return status;
}

int FormatBox3DContactIslandsPhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                             std::size_t settingsCapacity)
{
	if (settings == nullptr || settingsCapacity == 0)
		return 2;
	const CaseExecutionContactIslands& fixture = execution.contactIslands;
	const std::uint32_t islandCount = fixture.islandGrid[0] * fixture.islandGrid[1];
	const int size =
	    std::snprintf(settings, settingsCapacity, "substeps=%u; sleep=%s; ccd=%s; worker_count=%d; islands=%u",
		              execution.nativeSolver.values[CaseSolverField_Substeps],
		              execution.sleepMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
		              execution.continuousCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
		              RequestedWorkerCount(threadCount), islandCount);
	return size > 0 && static_cast<std::size_t>(size) < settingsCapacity ? 0 : 2;
}

namespace
{
int StepContactIslandsVisual(Box3DCaseView* state, int workUnitCount)
{
	return state == nullptr || state->value == nullptr
	           ? 2
			   : StepBox3DContactIslands(static_cast<Box3DContactIslandsState*>(state->value), workUnitCount);
}

int BuildContactIslandsVisualScene(const Box3DCaseView& state, benchmark_visual::VisualGeometry* geometries,
                                   benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                                   benchmark_visual::VisualInstance* instances, int instanceCapacity,
                                   int* geometryCount, int* instanceCount)
{
	if (state.value == nullptr || geometries == nullptr || instances == nullptr || geometryCount == nullptr ||
	    instanceCount == nullptr)
		return 2;
	const Box3DContactIslandsState& value = *static_cast<const Box3DContactIslandsState*>(state.value);
	const CaseExecutionSpec& execution = *value.config.caseExecution;
	const CaseExecutionContactIslands& fixture = execution.contactIslands;
	if (geometryCapacity < 2 || instanceCapacity < static_cast<int>(execution.bodyCount))
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
		const b3Pos position = b3Body_GetPosition(value.dynamicBodies[index]);
		const b3Quat rotation = b3Body_GetRotation(value.dynamicBodies[index]);
		instances[index] = {};
		instances[index].geometryIndex = 0;
		instances[index].stableSlot = static_cast<std::uint32_t>(index);
		instances[index].transformSlot = static_cast<std::uint32_t>(index);
		instances[index].initialTransform = {position.x,   position.y,   position.z, rotation.v.x,
		                                     rotation.v.y, rotation.v.z, rotation.s};
	}
	int staticIndex = 0;
	for (std::uint32_t groupZ = 0; groupZ < fixture.islandGrid[1]; ++groupZ)
		for (std::uint32_t groupX = 0; groupX < fixture.islandGrid[0]; ++groupX)
		{
			benchmark_visual::VisualInstance& instance = instances[execution.dynamicBodyCount + staticIndex];
			instance = {};
			instance.geometryIndex = 1;
			instance.stableSlot = static_cast<std::uint32_t>(execution.dynamicBodyCount + staticIndex);
			instance.transformSlot = UINT32_MAX;
			instance.initialTransform = {IslandOrigin(fixture.islandSpacing[0], groupX, fixture.islandGrid[0]),
			                             -fixture.floorHalfExtents.y,
			                             IslandOrigin(fixture.islandSpacing[1], groupZ, fixture.islandGrid[1]),
			                             0.0f,
			                             0.0f,
			                             0.0f,
			                             1.0f};
			++staticIndex;
		}
	*geometryCount = 2;
	*instanceCount = static_cast<int>(execution.bodyCount);
	return 0;
}

int SampleContactIslandsVisualTransforms(const Box3DCaseView& state,
                                         benchmark_visual::VisualStableTransform* transforms, int capacity)
{
	return SampleBox3DContactIslandsTransforms(*static_cast<const Box3DContactIslandsState*>(state.value), transforms,
	                                           capacity);
}

}

const Box3DCaseDescriptor& Box3DContactIslandsCaseDescriptor()
{
	static const Box3DCaseDescriptor descriptor = {
	    kEngineId,
	    RunBox3DContactIslandsHeadless,
	    StepContactIslandsVisual,
	    BuildContactIslandsVisualScene,
	    SampleContactIslandsVisualTransforms,
	    nullptr,
	};
	return descriptor;
}
}
