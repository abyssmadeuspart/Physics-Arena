#include "box3d_visual_snapshot.h"
#include "box3d_spatial_query_trace_case.h"

#include "box3d_result_writer.h"
#include "box3d_runner_args.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace box3d_benchmark
{
enum class Box3DSpatialQueryFamily : std::uint8_t
{
	Idle,
	Ray,
	SphereCast,
	Overlap,
	Shutdown,
};


namespace
{
void DestroySpatialQueryState(Box3DSpatialQueryState* state);

float CenteredGridCoordinate(float base, float spacing, std::uint32_t coordinate, std::uint32_t count)
{
	return base + spacing * (static_cast<float>(coordinate) - 0.5f * static_cast<float>(count - 1));
}

float UncenteredGridCoordinate(float base, float spacing, std::uint32_t coordinate)
{
	return base + spacing * static_cast<float>(coordinate);
}

void GenerateSpatialQuery(const CaseExecutionSpec& execution, int index, Box3DSpatialQuery* query)
{
	const CaseExecutionSpatialQuery& fixture = execution.spatialQuery;
	int familyIndex = index;
	if (index >= static_cast<int>(fixture.rayCount + fixture.sphereCastCount))
		familyIndex -= static_cast<int>(fixture.rayCount + fixture.sphereCastCount);
	else if (index >= static_cast<int>(fixture.rayCount))
		familyIndex -= static_cast<int>(fixture.rayCount);
	const int sample = familyIndex / 2;
	const int intendedHit = (familyIndex & 1) == 0 ? 1 : 0;
	const int slot = sample % static_cast<int>(execution.staticBodyCount);
	const std::uint32_t ix = static_cast<std::uint32_t>(slot) % fixture.staticGrid[0];
	const std::uint32_t iz = (static_cast<std::uint32_t>(slot) / fixture.staticGrid[0]) % fixture.staticGrid[2];
	const std::uint32_t iy = static_cast<std::uint32_t>(slot) / (fixture.staticGrid[0] * fixture.staticGrid[2]);
	const float center[3] = {
	    CenteredGridCoordinate(fixture.staticBaseCenter.x, fixture.staticSpacing.x, ix, fixture.staticGrid[0]),
	    UncenteredGridCoordinate(fixture.staticBaseCenter.y, fixture.staticSpacing.y, iy),
	    CenteredGridCoordinate(fixture.staticBaseCenter.z, fixture.staticSpacing.z, iz, fixture.staticGrid[2]),
	};
	const float sceneMinimum[3] = {
	    CenteredGridCoordinate(fixture.staticBaseCenter.x, fixture.staticSpacing.x, 0, fixture.staticGrid[0]) -
	        fixture.staticHalfExtents.x,
	    fixture.staticBaseCenter.y - fixture.staticHalfExtents.y,
	    CenteredGridCoordinate(fixture.staticBaseCenter.z, fixture.staticSpacing.z, 0, fixture.staticGrid[2]) -
	        fixture.staticHalfExtents.z,
	};
	const float sceneMaximum[3] = {
	    CenteredGridCoordinate(fixture.staticBaseCenter.x, fixture.staticSpacing.x, fixture.staticGrid[0] - 1,
		                       fixture.staticGrid[0]) +
	        fixture.staticHalfExtents.x,
	    UncenteredGridCoordinate(fixture.staticBaseCenter.y, fixture.staticSpacing.y, fixture.staticGrid[1] - 1) +
	        fixture.staticHalfExtents.y,
	    CenteredGridCoordinate(fixture.staticBaseCenter.z, fixture.staticSpacing.z, fixture.staticGrid[2] - 1,
		                       fixture.staticGrid[2]) +
	        fixture.staticHalfExtents.z,
	};
	const int face = sample % 6;
	const int axis = face / 2;
	if (index >= static_cast<int>(fixture.rayCount + fixture.sphereCastCount))
	{
		float overlapCenter[3] = {center[0], center[1], center[2]};
		if (intendedHit == 0)
			overlapCenter[axis] = sceneMaximum[axis] + fixture.missOffset;
		*query = {overlapCenter[0], overlapCenter[1], overlapCenter[2], 0.0f, 0.0f, 0.0f};
		return;
	}
	const int positiveFace = face & 1;
	const int firstTransverse = axis == 0 ? 1 : 0;
	float origin[3] = {};
	float direction[3] = {};
	for (int component = 0; component < 3; ++component)
	{
		if (component == axis)
		{
			origin[component] = positiveFace != 0 ? sceneMaximum[component] + 5.0f : sceneMinimum[component] - 5.0f;
			direction[component] = positiveFace != 0 ? -1.0f : 1.0f;
		}
		else
			origin[component] = intendedHit == 0 && component == firstTransverse
			                        ? sceneMaximum[component] + fixture.missOffset
			                        : center[component];
	}
	*query = {origin[0], origin[1], origin[2], direction[0], direction[1], direction[2]};
}

bool AnyOverlap(b3ShapeId, void* context)
{
	*static_cast<std::uint8_t*>(context) = 1;
	return false;
}

struct ClosestCast
{
	std::uint8_t hit;
	float fraction;
};

float ClosestShapeCast(b3ShapeId, b3Pos, b3Vec3, float fraction, std::uint64_t, int, int, void* context)
{
	ClosestCast* closest = static_cast<ClosestCast*>(context);
	closest->hit = 1;
	closest->fraction = fraction;
	return fraction;
}

std::uint64_t ExecuteSpatialQueryLane(Box3DSpatialQueryState* state, Box3DSpatialQueryFamily family, int laneIndex)
{
	const CaseExecutionSpatialQuery& fixture = state->config.caseExecution->spatialQuery;
	const std::uint32_t queryCount = family == Box3DSpatialQueryFamily::Ray          ? fixture.rayCount
	                                 : family == Box3DSpatialQueryFamily::SphereCast ? fixture.sphereCastCount
	                                                                                 : fixture.overlapCount;
	const std::uint32_t start =
	    static_cast<std::uint32_t>((static_cast<std::uint64_t>(queryCount) * static_cast<std::uint32_t>(laneIndex)) /
		                           static_cast<std::uint32_t>(state->config.threadCount));
	const std::uint32_t end = static_cast<std::uint32_t>(
	    (static_cast<std::uint64_t>(queryCount) * static_cast<std::uint32_t>(laneIndex + 1)) /
	    static_cast<std::uint32_t>(state->config.threadCount));
	std::uint64_t hitCount = 0;
	if (family == Box3DSpatialQueryFamily::Ray)
	{
		for (std::uint32_t index = start; index < end; ++index)
		{
			const Box3DRayInput& input = state->rayInputs[index];
			const b3RayResult result =
			    b3World_CastRayClosest(state->worldId, input.origin, input.translation, state->filter);
			hitCount += result.hit ? 1U : 0U;
		}
	}
	else if (family == Box3DSpatialQueryFamily::SphereCast)
	{
		for (std::uint32_t index = start; index < end; ++index)
		{
			const Box3DSphereCastInput& input = state->sphereCastInputs[index];
			ClosestCast closest = {0, 1.0f};
			b3World_CastShape(state->worldId, input.origin, &state->sphere, input.translation, state->filter,
			                  ClosestShapeCast, &closest);
			hitCount += closest.hit;
		}
	}
	else
	{
		for (std::uint32_t index = start; index < end; ++index)
		{
			std::uint8_t hit = 0;
			b3World_OverlapAABB(state->worldId, state->overlapInputs[index], state->filter, AnyOverlap, &hit);
			hitCount += hit;
		}
	}
	return hitCount;
}

std::uint64_t ExecuteSpatialQueryWork(void* context, int work, int lane)
{
	return ExecuteSpatialQueryLane(static_cast<Box3DSpatialQueryState*>(context), static_cast<Box3DSpatialQueryFamily>(work), lane);
}

int CreateSpatialQueryExecutor(Box3DSpatialQueryState* state)
{
	return CreateBox3DQueryExecutor(state->config.threadCount, ExecuteSpatialQueryWork, state, &state->executor);
}

std::uint64_t DispatchSpatialQuery(Box3DSpatialQueryState* state, Box3DSpatialQueryFamily family)
{
	return DispatchBox3DQuery(state->executor, static_cast<int>(family));
}

int CreateSpatialQueryFixture(Box3DSpatialQueryState* state)
{
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	const CaseExecutionSpatialQuery& fixture = execution.spatialQuery;
	b3ShapeDef shapeDef = b3DefaultShapeDef();
	shapeDef.density = 0.0f;
	shapeDef.baseMaterial.friction = execution.friction;
	shapeDef.baseMaterial.restitution = execution.restitution;
	Box3DResolvedShape shape = {};
	if (CreateBox3DResolvedShape(execution.selectedGeometry, execution, &shape) != 0)
		return 2;
	for (std::uint32_t iy = 0; iy < fixture.staticGrid[1]; ++iy)
		for (std::uint32_t iz = 0; iz < fixture.staticGrid[2]; ++iz)
			for (std::uint32_t ix = 0; ix < fixture.staticGrid[0]; ++ix)
			{
				b3BodyDef bodyDef = b3DefaultBodyDef();
				bodyDef.rotation = shape.rotation;
				bodyDef.position = {CenteredGridCoordinate(fixture.staticBaseCenter.x, fixture.staticSpacing.x, ix,
				                                           fixture.staticGrid[0]),
				                    UncenteredGridCoordinate(fixture.staticBaseCenter.y, fixture.staticSpacing.y, iy),
				                    CenteredGridCoordinate(fixture.staticBaseCenter.z, fixture.staticSpacing.z, iz,
				                                           fixture.staticGrid[2])};
				const b3BodyId body = b3CreateBody(state->worldId, &bodyDef);
				if (AttachBox3DResolvedShape(body, shapeDef, shape) != 0)
				{
					DestroyBox3DResolvedShape(&shape);
					return 2;
				}
			}
	DestroyBox3DResolvedShape(&shape);
	return 0;
}

void CaptureSpatialQueryDebugSamples(Box3DSpatialQueryState* state)
{
	const int samples = static_cast<int>(state->config.caseExecution->spatialQuery.debugSamplesPerFamily);
	for (int index = 0; index < samples; ++index)
	{
		const Box3DRayInput& input = state->rayInputs[index];
		const b3RayResult result =
		    b3World_CastRayClosest(state->worldId, input.origin, input.translation, state->filter);
		state->debugHits[index] = result.hit ? 1 : 0;
		state->debugHitFractions[index] = result.hit ? result.fraction : 1.0f;
	}
	for (int index = 0; index < samples; ++index)
	{
		const Box3DSphereCastInput& input = state->sphereCastInputs[index];
		ClosestCast closest = {0, 1.0f};
		b3World_CastShape(state->worldId, input.origin, &state->sphere, input.translation, state->filter,
		                  ClosestShapeCast, &closest);
		const int debugIndex = samples + index;
		state->debugHits[debugIndex] = closest.hit;
		state->debugHitFractions[debugIndex] = closest.fraction;
	}
	for (int index = 0; index < samples; ++index)
	{
		std::uint8_t hit = 0;
		b3World_OverlapAABB(state->worldId, state->overlapInputs[index], state->filter, AnyOverlap, &hit);
		const int debugIndex = 2 * samples + index;
		state->debugHits[debugIndex] = hit;
		state->debugHitFractions[debugIndex] = 1.0f;
	}
}

void ExecuteSpatialQueryBatch(Box3DSpatialQueryState* state, int measured,
                              std::chrono::steady_clock::duration::rep* batchDuration)
{
	const std::chrono::steady_clock::time_point batchStart = std::chrono::steady_clock::now();
	const std::chrono::steady_clock::time_point rayStart = std::chrono::steady_clock::now();
	state->rayHitCount = DispatchSpatialQuery(state, Box3DSpatialQueryFamily::Ray);
	const std::chrono::steady_clock::time_point rayEnd = std::chrono::steady_clock::now();
	const std::chrono::steady_clock::time_point castStart = std::chrono::steady_clock::now();
	state->sphereCastHitCount = DispatchSpatialQuery(state, Box3DSpatialQueryFamily::SphereCast);
	const std::chrono::steady_clock::time_point castEnd = std::chrono::steady_clock::now();
	const std::chrono::steady_clock::time_point overlapStart = std::chrono::steady_clock::now();
	state->overlapHitCount = DispatchSpatialQuery(state, Box3DSpatialQueryFamily::Overlap);
	const std::chrono::steady_clock::time_point overlapEnd = std::chrono::steady_clock::now();
	const std::chrono::steady_clock::time_point batchEnd = std::chrono::steady_clock::now();
	if (measured != 0)
	{
		state->rayElapsed += (rayEnd - rayStart).count();
		state->sphereCastElapsed += (castEnd - castStart).count();
		state->overlapElapsed += (overlapEnd - overlapStart).count();
		*batchDuration = (batchEnd - batchStart).count();
	}
}

double QueryRate(int queryCount, int batchCount, std::chrono::steady_clock::duration::rep elapsed)
{
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::duration(elapsed)).count();
	return static_cast<double>(queryCount) * static_cast<double>(batchCount) / seconds;
}

std::uint64_t DoubleBits(double value)
{
	std::uint64_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));
	return bits;
}

int CreateSpatialQueryState(const Box3DCaseConfig& config, benchmark_replay::RecordingMode recordingMode,
                            Box3DSpatialQueryState* state)
{
	if (state == nullptr || config.caseExecution == nullptr ||
	    config.caseExecution->fixtureKind != CaseFixtureKind_SpatialQueryTrace || config.threadCount <= 0 ||
	    config.stepCount != static_cast<int>(config.caseExecution->measuredWorkUnitCount) ||
	    config.warmupSteps != static_cast<int>(config.caseExecution->warmupWorkUnitCount))
		return 2;
	const CaseExecutionSpec& execution = *config.caseExecution;
	const CaseExecutionSpatialQuery& fixture = execution.spatialQuery;
	if (fixture.rayCount < static_cast<std::uint32_t>(config.threadCount) ||
	    fixture.sphereCastCount < static_cast<std::uint32_t>(config.threadCount) ||
	    fixture.overlapCount < static_cast<std::uint32_t>(config.threadCount))
		return 2;
	*state = {};
	state->config = config;
	state->rayInputs = new (std::nothrow) Box3DRayInput[fixture.rayCount];
	state->sphereCastInputs = new (std::nothrow) Box3DSphereCastInput[fixture.sphereCastCount];
	state->overlapInputs = new (std::nothrow) b3AABB[fixture.overlapCount];
	if (recordingMode == benchmark_replay::RecordingMode_On)
	{
		state->debugHits = new (std::nothrow) std::uint8_t[execution.visualDebugPrimitiveCount]();
		state->debugHitFractions = new (std::nothrow) float[execution.visualDebugPrimitiveCount]();
	}
	state->rawBatchDurations =
	    new (std::nothrow) std::chrono::steady_clock::duration::rep[execution.measuredWorkUnitCount]();
	if (state->rayInputs == nullptr || state->sphereCastInputs == nullptr || state->overlapInputs == nullptr ||
	    (recordingMode == benchmark_replay::RecordingMode_On &&
	     (state->debugHits == nullptr || state->debugHitFractions == nullptr)) ||
	    state->rawBatchDurations == nullptr)
	{
		DestroySpatialQueryState(state);
		return 2;
	}
	for (std::uint32_t index = 0; index < fixture.rayCount; ++index)
	{
		Box3DSpatialQuery query = {};
		GenerateSpatialQuery(execution, static_cast<int>(index), &query);
		state->rayInputs[index] = {
		    {query.originX, query.originY, query.originZ},
		    {query.directionX * fixture.queryDistance, query.directionY * fixture.queryDistance,
			 query.directionZ * fixture.queryDistance},
		};
	}
	for (std::uint32_t index = 0; index < fixture.sphereCastCount; ++index)
	{
		Box3DSpatialQuery query = {};
		GenerateSpatialQuery(execution, static_cast<int>(fixture.rayCount + index), &query);
		state->sphereCastInputs[index] = {
		    {query.originX, query.originY, query.originZ},
		    {query.directionX * fixture.queryDistance, query.directionY * fixture.queryDistance,
			 query.directionZ * fixture.queryDistance},
		};
	}
	for (std::uint32_t index = 0; index < fixture.overlapCount; ++index)
	{
		Box3DSpatialQuery query = {};
		GenerateSpatialQuery(execution, static_cast<int>(fixture.rayCount + fixture.sphereCastCount + index), &query);
		state->overlapInputs[index] = {
		    {query.originX - fixture.overlapHalfExtents.x, query.originY - fixture.overlapHalfExtents.y,
			 query.originZ - fixture.overlapHalfExtents.z},
		    {query.originX + fixture.overlapHalfExtents.x, query.originY + fixture.overlapHalfExtents.y,
			 query.originZ + fixture.overlapHalfExtents.z},
		};
	}
	state->filter = b3DefaultQueryFilter();
	state->spherePoint = {0.0f, 0.0f, 0.0f};
	state->sphere = {&state->spherePoint, 1, fixture.sphereCastRadius};
	b3WorldDef worldDef = b3DefaultWorldDef();
	worldDef.workerCount = 0;
	worldDef.gravity = {execution.gravity.x, execution.gravity.y, execution.gravity.z};
	worldDef.enableContinuous = execution.continuousCollisionMode == CaseExecutionToggle_Enabled;
	worldDef.capacity.staticBodyCount = static_cast<int>(execution.staticBodyCount);
	worldDef.capacity.staticShapeCount = static_cast<int>(execution.shapeCount);
	state->worldId = b3CreateWorld(&worldDef);
	if (B3_IS_NULL(state->worldId) || CreateSpatialQueryFixture(state) != 0)
	{
		DestroySpatialQueryState(state);
		return 2;
	}
	if (recordingMode == benchmark_replay::RecordingMode_On)
		CaptureSpatialQueryDebugSamples(state);
	if (CreateSpatialQueryExecutor(state) != 0)
	{
		DestroySpatialQueryState(state);
		return 2;
	}
	return 0;
}

void DestroySpatialQueryState(Box3DSpatialQueryState* state)
{
	if (state == nullptr)
		return;
	DestroyBox3DQueryExecutor(state->executor);
	state->executor = nullptr;
	if (B3_IS_NON_NULL(state->worldId))
		b3DestroyWorld(state->worldId);
	delete[] state->rayInputs;
	delete[] state->sphereCastInputs;
	delete[] state->overlapInputs;
	delete[] state->debugHits;
	delete[] state->debugHitFractions;
	delete[] state->rawBatchDurations;
	*state = {};
}

int CheckSpatialQueryBatch(const Box3DSpatialQueryState* state, const char* phase, int batch)
{
	const CaseExecutionSpatialQuery& fixture = state->config.caseExecution->spatialQuery;
	for (int family = 0; family < 3; ++family)
	{
		const std::uint32_t count =
		    family == 0 ? fixture.rayCount : (family == 1 ? fixture.sphereCastCount : fixture.overlapCount);
		const std::uint64_t expected = count / 2 + count % 2;
		const std::uint64_t actual =
		    family == 0 ? state->rayHitCount : (family == 1 ? state->sphereCastHitCount : state->overlapHitCount);
		if (actual != expected)
		{
			std::fprintf(
			    stderr,
			    "run_failed reason=query_batch engine=%s phase=%s batch=%d family=%s expected=%llu actual=%llu\n",
			    "box3d", phase, batch, family == 0 ? "ray" : (family == 1 ? "sphere_cast" : "overlap"),
			    static_cast<unsigned long long>(expected), static_cast<unsigned long long>(actual));
			return 2;
		}
	}
	return 0;
}

int WarmupSpatialQueryState(Box3DSpatialQueryState* state, int batchCount)
{
	if (state == nullptr || state->config.caseExecution == nullptr ||
	    batchCount != static_cast<int>(state->config.caseExecution->warmupWorkUnitCount))
		return 2;
	for (int batch = 0; batch < batchCount; ++batch)
	{
		ExecuteSpatialQueryBatch(state, 0, nullptr);
		if (CheckSpatialQueryBatch(state, "warmup", batch) != 0)
			return 2;
	}
	return 0;
}

int StepSpatialQueryState(Box3DSpatialQueryState* state, int batchCount)
{
	if (state == nullptr || state->config.caseExecution == nullptr || batchCount < 0 ||
	    batchCount > static_cast<int>(state->config.caseExecution->measuredWorkUnitCount) - state->completedBatchCount)
		return 2;
	for (int batch = 0; batch < batchCount; ++batch)
	{
		const int slot = state->completedBatchCount + batch;
		ExecuteSpatialQueryBatch(state, 1, &state->rawBatchDurations[slot]);
		if (CheckSpatialQueryBatch(state, "measured", slot) != 0)
			return 2;
		const double milliseconds = std::chrono::duration<double, std::milli>(
		                                std::chrono::steady_clock::duration(state->rawBatchDurations[slot]))
		                                .count();
		state->workloadElapsedMs += milliseconds;
		state->latestBatchElapsedMs = milliseconds;
	}
	state->completedBatchCount += batchCount;
	return 0;
}

int FormatSpatialQuerySettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                               std::size_t settingsCapacity)
{
	if (settings == nullptr || settingsCapacity == 0 || threadCount <= 0)
		return 2;
	const CaseExecutionSpatialQuery& fixture = execution.spatialQuery;
	const int size = std::snprintf(settings, settingsCapacity,
	                               "query_world=static_only; worker_count=%d; rays=%u; sphere_casts=%u; overlaps=%u",
	                               threadCount - 1, fixture.rayCount, fixture.sphereCastCount, fixture.overlapCount);
	return size > 0 && static_cast<std::size_t>(size) < settingsCapacity ? 0 : 2;
}

void BuildHeadlessObservations(const Box3DSpatialQueryState& state, std::array<Box3DObservationRow, 6>* observations)
{
	const CaseExecutionSpatialQuery& fixture = state.config.caseExecution->spatialQuery;
	const double rayRate = QueryRate(static_cast<int>(fixture.rayCount), state.completedBatchCount, state.rayElapsed);
	const double castRate =
	    QueryRate(static_cast<int>(fixture.sphereCastCount), state.completedBatchCount, state.sphereCastElapsed);
	const double overlapRate =
	    QueryRate(static_cast<int>(fixture.overlapCount), state.completedBatchCount, state.overlapElapsed);
	(*observations)[0] = {"ray_queries_per_second", "final", 0, Box3DObservationValueType_Float64, DoubleBits(rayRate)};
	(*observations)[1] = {"sphere_cast_queries_per_second", "final", 0, Box3DObservationValueType_Float64,
	                      DoubleBits(castRate)};
	(*observations)[2] = {"overlap_queries_per_second", "final", 0, Box3DObservationValueType_Float64,
	                      DoubleBits(overlapRate)};
	(*observations)[3] = {"ray_hit_count", "final", 0, Box3DObservationValueType_Uint64, state.rayHitCount};
	(*observations)[4] = {"sphere_cast_hit_count", "final", 0, Box3DObservationValueType_Uint64,
	                      state.sphereCastHitCount};
	(*observations)[5] = {"overlap_hit_count", "final", 0, Box3DObservationValueType_Uint64, state.overlapHitCount};
}

int RunSpatialQueryHeadless(const Box3DRunRequest& request)
{
	const Box3DCaseConfig config = {&request.caseExecution, request.threadCount, request.repeatIndex, request.stepCount,
	                                request.warmupSteps};
	Box3DSpatialQueryState state = {};
	if (CreateSpatialQueryState(config, request.recordingMode, &state) != 0)
	{
		DestroySpatialQueryState(&state);
		return 2;
	}
	int status = WarmupSpatialQueryState(&state, request.warmupSteps);
	Box3DCaseView recordingState = {&state};
	if (status == 0)
		status = RecordBox3DCase(request, &recordingState);
	if (status == 0)
	{
		const b3Counters counters = b3World_GetCounters(state.worldId);
		const int valid = counters.bodyCount == static_cast<int>(request.caseExecution.staticBodyCount) &&
		                  counters.shapeCount == static_cast<int>(request.caseExecution.shapeCount) &&
		                  state.completedBatchCount == request.stepCount && state.workloadElapsedMs > 0.0 &&
		                  std::isfinite(state.workloadElapsedMs) != 0;
		std::array<char, 256> settings = {};
		std::array<Box3DObservationRow, 6> observations = {};
		FormatSpatialQuerySettings(request.caseExecution, request.threadCount, settings.data(), settings.size());
		BuildHeadlessObservations(state, &observations);
		const Box3DResult result = {
		    request.caseExecution.fixtureSemantic,
		    request.caseExecution.fixtureRevision,
		    settings.data(),
		    static_cast<int>(request.caseExecution.bodyCount),
		    static_cast<int>(request.caseExecution.shapeCount),
		    static_cast<int>(request.caseExecution.queryCount),
		    static_cast<int>(request.caseExecution.constraintCount),
		    0,
		    valid != 0 ? "ok" : "invalid_result",
		    valid != 0 ? "ok" : "invalid_result",
		    request.threadCount,
		    request.threadCount - 1,
		    state.completedBatchCount,
		    state.workloadElapsedMs,
		    state.rawBatchDurations,
		    observations.data(),
		    static_cast<std::uint32_t>(observations.size()),
		};
		status = WriteBox3DResult(request, result);
	}
	DestroySpatialQueryState(&state);
	return status;
}

int BuildSpatialQueryScene(const Box3DCaseView& state, benchmark_visual::VisualGeometry* geometries,
                           benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                           benchmark_visual::VisualInstance* instances, int instanceCapacity, int* geometryCount,
                           int* instanceCount)
{
	if (state.value == nullptr || geometries == nullptr || instances == nullptr || geometryCount == nullptr ||
	    instanceCount == nullptr)
		return 2;
	const Box3DSpatialQueryState& value = *static_cast<const Box3DSpatialQueryState*>(state.value);
	const CaseExecutionSpec& execution = *value.config.caseExecution;
	const CaseExecutionSpatialQuery& fixture = execution.spatialQuery;
	if (geometryCapacity < 1 || instanceCapacity < static_cast<int>(execution.staticBodyCount))
		return 2;
	if (BuildResolvedVisualGeometry(execution, execution.selectedGeometry, meshes, &geometries[0]) != 0)
		return 2;
	const CaseExecutionQuaternion rotation = CaseExecutionAxisRotation(execution.selectedGeometry.axis);
	for (std::uint32_t iy = 0; iy < fixture.staticGrid[1]; ++iy)
		for (std::uint32_t iz = 0; iz < fixture.staticGrid[2]; ++iz)
			for (std::uint32_t ix = 0; ix < fixture.staticGrid[0]; ++ix)
			{
				const std::uint32_t slot = (iy * fixture.staticGrid[2] + iz) * fixture.staticGrid[0] + ix;
				instances[slot] = {};
				instances[slot].geometryIndex = 0;
				instances[slot].stableSlot = static_cast<std::uint32_t>(slot);
				instances[slot].transformSlot = UINT32_MAX;
				instances[slot].initialTransform = {
				    CenteredGridCoordinate(fixture.staticBaseCenter.x, fixture.staticSpacing.x, ix,
					                       fixture.staticGrid[0]),
				    UncenteredGridCoordinate(fixture.staticBaseCenter.y, fixture.staticSpacing.y, iy),
				    CenteredGridCoordinate(fixture.staticBaseCenter.z, fixture.staticSpacing.z, iz,
					                       fixture.staticGrid[2]),
				    rotation.x,
				    rotation.y,
				    rotation.z,
				    rotation.w};
			}
	*geometryCount = 1;
	*instanceCount = static_cast<int>(execution.staticBodyCount);
	return 0;
}

int SampleNoSpatialQueryTransforms(const Box3DCaseView&, benchmark_visual::VisualStableTransform*, int capacity)
{
	return capacity == 0 ? 0 : 2;
}

int BuildSpatialQueryDebugPrimitives(const Box3DCaseView& state, benchmark_visual::VisualDebugPrimitive* primitives,
                                     int capacity)
{
	if (state.value == nullptr || primitives == nullptr)
		return 2;
	const Box3DSpatialQueryState& value = *static_cast<const Box3DSpatialQueryState*>(state.value);
	const CaseExecutionSpec& execution = *value.config.caseExecution;
	const CaseExecutionSpatialQuery& fixture = execution.spatialQuery;
	const int samples = static_cast<int>(fixture.debugSamplesPerFamily);
	if (capacity < static_cast<int>(execution.visualDebugPrimitiveCount))
		return 2;
	for (int local = 0; local < samples; ++local)
	{
		for (int family = 0; family < 3; ++family)
		{
			const int primitiveIndex = family * samples + local;
			benchmark_visual::VisualDebugPrimitive& primitive = primitives[primitiveIndex];
			primitive = {};
			primitive.kind = static_cast<std::uint32_t>(family);
			primitive.materialIndex = value.debugHits[primitiveIndex] != 0 ? 6u : 7u;
			if (family == 0)
			{
				const Box3DRayInput& input = value.rayInputs[local];
				primitive.originOrCenterX = input.origin.x;
				primitive.originOrCenterY = input.origin.y;
				primitive.originOrCenterZ = input.origin.z;
				const float fraction = value.debugHitFractions[primitiveIndex];
				primitive.endOrHalfExtentsX = input.origin.x + input.translation.x * fraction;
				primitive.endOrHalfExtentsY = input.origin.y + input.translation.y * fraction;
				primitive.endOrHalfExtentsZ = input.origin.z + input.translation.z * fraction;
			}
			else if (family == 1)
			{
				const Box3DSphereCastInput& input = value.sphereCastInputs[local];
				primitive.originOrCenterX = input.origin.x;
				primitive.originOrCenterY = input.origin.y;
				primitive.originOrCenterZ = input.origin.z;
				const float fraction = value.debugHitFractions[primitiveIndex];
				primitive.endOrHalfExtentsX = input.origin.x + input.translation.x * fraction;
				primitive.endOrHalfExtentsY = input.origin.y + input.translation.y * fraction;
				primitive.endOrHalfExtentsZ = input.origin.z + input.translation.z * fraction;
				primitive.radius = fixture.sphereCastRadius;
			}
			else
			{
				const b3AABB& bounds = value.overlapInputs[local];
				primitive.originOrCenterX = 0.5f * (bounds.lowerBound.x + bounds.upperBound.x);
				primitive.originOrCenterY = 0.5f * (bounds.lowerBound.y + bounds.upperBound.y);
				primitive.originOrCenterZ = 0.5f * (bounds.lowerBound.z + bounds.upperBound.z);
				primitive.endOrHalfExtentsX = fixture.overlapHalfExtents.x;
				primitive.endOrHalfExtentsY = fixture.overlapHalfExtents.y;
				primitive.endOrHalfExtentsZ = fixture.overlapHalfExtents.z;
			}
		}
	}
	return 0;
}

}

const Box3DCaseDescriptor& Box3DSpatialQueryTraceCaseDescriptor()
{
	static const Box3DCaseDescriptor descriptor = {
	    kSpatialQueryEngineId,
	    RunSpatialQueryHeadless,
	    [](Box3DCaseView* state, int count)
	    {
		    return state == nullptr || state->value == nullptr
		               ? 2
					   : StepSpatialQueryState(static_cast<Box3DSpatialQueryState*>(state->value), count);
	    },
	    BuildSpatialQueryScene,
	    SampleNoSpatialQueryTransforms,
	    BuildSpatialQueryDebugPrimitives,
	};
	return descriptor;
}
}
