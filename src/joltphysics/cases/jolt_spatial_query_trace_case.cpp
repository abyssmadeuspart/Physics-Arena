#include "jolt_spatial_query_trace_case.h"

#include "jolt_result_writer.h"
#include "jolt_runner_args.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace jolt_benchmark
{
namespace
{
void DestroySpatialQueryState(JoltSpatialQueryState* state);

float CenteredGridCoordinate(float base, float spacing, std::uint32_t coordinate, std::uint32_t count)
{
	return base + spacing * (static_cast<float>(coordinate) - 0.5f * static_cast<float>(count - 1));
}

float UncenteredGridCoordinate(float base, float spacing, std::uint32_t coordinate)
{
	return base + spacing * static_cast<float>(coordinate);
}

void GenerateSpatialQuery(const CaseExecutionSpec& execution, int index, JoltSpatialQuery* query)
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

int CreateSpatialQueryFixture(JoltSpatialQueryState* state)
{
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	const CaseExecutionSpatialQuery& fixture = execution.spatialQuery;
	JPH::BodyInterface& bodies = state->physicsSystem.GetBodyInterface();
	JPH::RefConst<JPH::Shape> boxShape;
	if (CreateJoltResolvedShape(execution.selectedGeometry, execution, &boxShape) != 0)
		return 2;
	const JPH::Quat shapeRotation = JoltShapeRotation(execution.selectedGeometry.axis);
	for (std::uint32_t iy = 0; iy < fixture.staticGrid[1]; ++iy)
		for (std::uint32_t iz = 0; iz < fixture.staticGrid[2]; ++iz)
			for (std::uint32_t ix = 0; ix < fixture.staticGrid[0]; ++ix)
			{
				const std::uint32_t slot = (iy * fixture.staticGrid[2] + iz) * fixture.staticGrid[0] + ix;
				const JPH::BodyCreationSettings settings(
				    boxShape,
				    JPH::RVec3(CenteredGridCoordinate(fixture.staticBaseCenter.x, fixture.staticSpacing.x, ix,
					                                  fixture.staticGrid[0]),
					           UncenteredGridCoordinate(fixture.staticBaseCenter.y, fixture.staticSpacing.y, iy),
					           CenteredGridCoordinate(fixture.staticBaseCenter.z, fixture.staticSpacing.z, iz,
					                                  fixture.staticGrid[2])),
				    shapeRotation, JPH::EMotionType::Static, Layers::NON_MOVING);
				state->bodies[slot] = bodies.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
				if (state->bodies[slot].IsInvalid())
					return 2;
				state->createdBodyCount += 1;
			}
	state->physicsSystem.OptimizeBroadPhase();
	return 0;
}

void CaptureSpatialQueryDebugSamples(JoltSpatialQueryState* state)
{
	// the optimized world stays immutable and has no concurrent query access
	const JPH::NarrowPhaseQuery& narrow = state->physicsSystem.GetNarrowPhaseQueryNoLock();
	const JPH::BroadPhaseQuery& broad = state->physicsSystem.GetBroadPhaseQuery();
	const int samples = static_cast<int>(state->config.caseExecution->spatialQuery.debugSamplesPerFamily);
	for (int index = 0; index < samples; ++index)
	{
		JPH::RayCastResult result;
		const bool hit = narrow.CastRay(state->rayInputs[index], result);
		state->debugHits[index] = hit ? 1 : 0;
		state->debugHitFractions[index] = hit ? result.mFraction : 1.0f;
	}
	for (int index = 0; index < samples; ++index)
	{
		const JPH::RShapeCast& input = state->sphereCastInputs[index];
		JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
		narrow.CastShape(input, state->castSettings, input.mCenterOfMassStart.GetTranslation(), collector);
		const bool hit = collector.HadHit();
		const int debugIndex = samples + index;
		state->debugHits[debugIndex] = hit ? 1 : 0;
		state->debugHitFractions[debugIndex] = hit ? collector.mHit.mFraction : 1.0f;
	}
	for (int index = 0; index < samples; ++index)
	{
		JPH::AnyHitCollisionCollector<JPH::CollideShapeBodyCollector> collector;
		broad.CollideAABox(state->overlapInputs[index], collector);
		const int debugIndex = 2 * samples + index;
		state->debugHits[debugIndex] = collector.HadHit() ? 1 : 0;
		state->debugHitFractions[debugIndex] = 1.0f;
	}
}

enum class JoltSpatialQueryFamily : std::uint8_t
{
	Ray,
	SphereCast,
	Overlap,
};

std::uint64_t ExecuteSpatialQueryLane(JoltSpatialQueryState* state, JoltSpatialQueryFamily family, int laneIndex)
{
	const CaseExecutionSpatialQuery& fixture = state->config.caseExecution->spatialQuery;
	const std::uint32_t queryCount = family == JoltSpatialQueryFamily::Ray          ? fixture.rayCount
	                                 : family == JoltSpatialQueryFamily::SphereCast ? fixture.sphereCastCount
	                                                                                : fixture.overlapCount;
	const std::uint32_t start =
	    static_cast<std::uint32_t>((static_cast<std::uint64_t>(queryCount) * static_cast<std::uint32_t>(laneIndex)) /
		                           static_cast<std::uint32_t>(state->config.threadCount));
	const std::uint32_t end = static_cast<std::uint32_t>(
	    (static_cast<std::uint64_t>(queryCount) * static_cast<std::uint32_t>(laneIndex + 1)) /
	    static_cast<std::uint32_t>(state->config.threadCount));
	// the optimized static scene and shapes stay immutable until all query jobs finish
	const JPH::NarrowPhaseQuery& narrow = state->physicsSystem.GetNarrowPhaseQueryNoLock();
	const JPH::BroadPhaseQuery& broad = state->physicsSystem.GetBroadPhaseQuery();
	std::uint64_t hitCount = 0;
	if (family == JoltSpatialQueryFamily::Ray)
	{
		for (std::uint32_t index = start; index < end; ++index)
		{
			JPH::RayCastResult result;
			hitCount += narrow.CastRay(state->rayInputs[index], result) ? 1U : 0U;
		}
	}
	else if (family == JoltSpatialQueryFamily::SphereCast)
	{
		for (std::uint32_t index = start; index < end; ++index)
		{
			const JPH::RShapeCast& input = state->sphereCastInputs[index];
			JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
			narrow.CastShape(input, state->castSettings, input.mCenterOfMassStart.GetTranslation(), collector);
			hitCount += collector.HadHit() ? 1U : 0U;
		}
	}
	else
	{
		for (std::uint32_t index = start; index < end; ++index)
		{
			JPH::AnyHitCollisionCollector<JPH::CollideShapeBodyCollector> collector;
			broad.CollideAABox(state->overlapInputs[index], collector);
			hitCount += collector.HadHit() ? 1U : 0U;
		}
	}
	return hitCount;
}

std::uint64_t DispatchSpatialQuery(JoltSpatialQueryState* state, JoltSpatialQueryFamily family)
{
	if (state->config.threadCount == 1)
		return ExecuteSpatialQueryLane(state, family, 0);
	for (int laneIndex = 0; laneIndex < state->config.threadCount; ++laneIndex)
	{
		state->jobHandles[laneIndex] = state->jobSystem->CreateJob(
		    "Spatial Query", JPH::Color::sGreen,
		    [state, family, laneIndex]
		    {
			    state->laneHitCounts[laneIndex] = ExecuteSpatialQueryLane(state, family, laneIndex);
		    },
		    1);
	}
	state->queryBarrier->AddJobs(state->jobHandles, static_cast<JPH::uint>(state->config.threadCount));
	JPH::JobSystem::JobHandle::sRemoveDependencies(state->jobHandles,
	                                               static_cast<JPH::uint>(state->config.threadCount));
	state->jobSystem->WaitForJobs(state->queryBarrier);
	std::uint64_t hitCount = 0;
	for (int laneIndex = 0; laneIndex < state->config.threadCount; ++laneIndex)
	{
		hitCount += state->laneHitCounts[laneIndex];
		state->jobHandles[laneIndex] = {};
	}
	return hitCount;
}

void ExecuteSpatialQueryBatch(JoltSpatialQueryState* state, int measured,
                              std::chrono::steady_clock::duration::rep* batchDuration)
{
	const std::chrono::steady_clock::time_point batchStart = std::chrono::steady_clock::now();
	const std::chrono::steady_clock::time_point rayStart = std::chrono::steady_clock::now();
	state->rayHitCount = DispatchSpatialQuery(state, JoltSpatialQueryFamily::Ray);
	const std::chrono::steady_clock::time_point rayEnd = std::chrono::steady_clock::now();
	const std::chrono::steady_clock::time_point castStart = std::chrono::steady_clock::now();
	state->sphereCastHitCount = DispatchSpatialQuery(state, JoltSpatialQueryFamily::SphereCast);
	const std::chrono::steady_clock::time_point castEnd = std::chrono::steady_clock::now();
	const std::chrono::steady_clock::time_point overlapStart = std::chrono::steady_clock::now();
	state->overlapHitCount = DispatchSpatialQuery(state, JoltSpatialQueryFamily::Overlap);
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

int CreateSpatialQueryState(const JoltCaseConfig& config, benchmark_replay::RecordingMode recordingMode,
                            JoltSpatialQueryState* state)
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
	state->config = config;
	state->bodies = new (std::nothrow) JPH::BodyID[execution.staticBodyCount]();
	state->rayInputs = new (std::nothrow) JPH::RRayCast[fixture.rayCount];
	state->sphereCastInputs =
	    static_cast<JPH::RShapeCast*>(::operator new(sizeof(JPH::RShapeCast) * fixture.sphereCastCount, std::nothrow));
	state->overlapInputs = new (std::nothrow) JPH::AABox[fixture.overlapCount];
	if (recordingMode == benchmark_replay::RecordingMode_On)
	{
		state->debugHits = new (std::nothrow) std::uint8_t[execution.visualDebugPrimitiveCount]();
		state->debugHitFractions = new (std::nothrow) float[execution.visualDebugPrimitiveCount]();
	}
	state->rawBatchDurations =
	    new (std::nothrow) std::chrono::steady_clock::duration::rep[execution.measuredWorkUnitCount]();
	state->jobHandles = new (std::nothrow) JPH::JobSystem::JobHandle[config.threadCount];
	state->laneHitCounts = new (std::nothrow) std::uint64_t[config.threadCount]();
	if (state->bodies == nullptr || state->rayInputs == nullptr || state->sphereCastInputs == nullptr ||
	    state->overlapInputs == nullptr ||
	    (recordingMode == benchmark_replay::RecordingMode_On &&
	     (state->debugHits == nullptr || state->debugHitFractions == nullptr)) ||
	    state->rawBatchDurations == nullptr || state->jobHandles == nullptr || state->laneHitCounts == nullptr)
	{
		DestroySpatialQueryState(state);
		return 2;
	}
	state->singleThreaded = nullptr;
	state->threadPool = nullptr;
	if (config.threadCount <= 1)
	{
		state->singleThreaded = new JPH::JobSystemSingleThreaded(JPH::cMaxPhysicsJobs);
		state->jobSystem = state->singleThreaded;
	}
	else
	{
		state->threadPool =
		    new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, config.threadCount - 1);
		state->jobSystem = state->threadPool;
	}
	if (state->jobSystem == nullptr || state->jobSystem->GetMaxConcurrency() != config.threadCount)
	{
		DestroySpatialQueryState(state);
		return 2;
	}
	state->queryBarrier = state->jobSystem->CreateBarrier();
	if (state->queryBarrier == nullptr)
	{
		DestroySpatialQueryState(state);
		return 2;
	}
	state->sphereShape = new JPH::SphereShape(fixture.sphereCastRadius);
	if (state->sphereShape == nullptr)
	{
		DestroySpatialQueryState(state);
		return 2;
	}
	for (std::uint32_t index = 0; index < fixture.rayCount; ++index)
	{
		JoltSpatialQuery query = {};
		GenerateSpatialQuery(execution, static_cast<int>(index), &query);
		state->rayInputs[index] =
		    JPH::RRayCast(JPH::RVec3(query.originX, query.originY, query.originZ),
			              JPH::Vec3(query.directionX, query.directionY, query.directionZ) * fixture.queryDistance);
	}
	for (std::uint32_t index = 0; index < fixture.sphereCastCount; ++index)
	{
		JoltSpatialQuery query = {};
		GenerateSpatialQuery(execution, static_cast<int>(fixture.rayCount + index), &query);
		const JPH::RVec3 origin(query.originX, query.originY, query.originZ);
		new (&state->sphereCastInputs[index])
		    JPH::RShapeCast(state->sphereShape, JPH::Vec3::sReplicate(1.0f), JPH::RMat44::sTranslation(origin),
			                JPH::Vec3(query.directionX, query.directionY, query.directionZ) * fixture.queryDistance);
		state->createdSphereCastInputCount += 1;
	}
	for (std::uint32_t index = 0; index < fixture.overlapCount; ++index)
	{
		JoltSpatialQuery query = {};
		GenerateSpatialQuery(execution, static_cast<int>(fixture.rayCount + fixture.sphereCastCount + index), &query);
		const JPH::Vec3 center(query.originX, query.originY, query.originZ);
		state->overlapInputs[index] =
		    JPH::AABox(center - JPH::Vec3(fixture.overlapHalfExtents.x, fixture.overlapHalfExtents.y,
			                              fixture.overlapHalfExtents.z),
			           center + JPH::Vec3(fixture.overlapHalfExtents.x, fixture.overlapHalfExtents.y,
			                              fixture.overlapHalfExtents.z));
	}
	state->physicsSystem.Init(execution.staticBodyCount, 0, execution.staticBodyCount, execution.shapeCount,
	                          state->broadPhaseLayerInterface, state->objectVsBroadPhaseLayerFilter,
	                          state->objectVsObjectLayerFilter);
	if (CreateSpatialQueryFixture(state) != 0)
	{
		DestroySpatialQueryState(state);
		return 2;
	}
	if (recordingMode == benchmark_replay::RecordingMode_On)
		CaptureSpatialQueryDebugSamples(state);
	return 0;
}

void DestroySpatialQueryState(JoltSpatialQueryState* state)
{
	if (state == nullptr)
		return;
	delete[] state->jobHandles;
	if (state->jobSystem != nullptr && state->queryBarrier != nullptr)
		state->jobSystem->DestroyBarrier(state->queryBarrier);
	delete state->threadPool;
	delete state->singleThreaded;
	delete[] state->laneHitCounts;
	state->jobHandles = nullptr;
	state->queryBarrier = nullptr;
	state->jobSystem = nullptr;
	state->threadPool = nullptr;
	state->singleThreaded = nullptr;
	state->laneHitCounts = nullptr;
	if (state->bodies != nullptr && state->createdBodyCount > 0)
	{
		JPH::BodyInterface& bodies = state->physicsSystem.GetBodyInterface();
		bodies.RemoveBodies(state->bodies, state->createdBodyCount);
		bodies.DestroyBodies(state->bodies, state->createdBodyCount);
	}
	state->sphereShape = nullptr;
	delete[] state->bodies;
	delete[] state->rayInputs;
	for (int index = 0; index < state->createdSphereCastInputCount; ++index)
		state->sphereCastInputs[index].~RShapeCast();
	::operator delete(state->sphereCastInputs);
	delete[] state->overlapInputs;
	delete[] state->debugHits;
	delete[] state->debugHitFractions;
	delete[] state->rawBatchDurations;
	state->bodies = nullptr;
	state->rayInputs = nullptr;
	state->sphereCastInputs = nullptr;
	state->overlapInputs = nullptr;
	state->debugHits = nullptr;
	state->debugHitFractions = nullptr;
	state->rawBatchDurations = nullptr;
	state->createdBodyCount = 0;
	state->createdSphereCastInputCount = 0;
}

int CheckSpatialQueryBatch(const JoltSpatialQueryState* state, const char* phase, int batch)
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
			    "joltphysics", phase, batch, family == 0 ? "ray" : (family == 1 ? "sphere_cast" : "overlap"),
			    static_cast<unsigned long long>(expected), static_cast<unsigned long long>(actual));
			return 2;
		}
	}
	return 0;
}

int WarmupSpatialQueryState(JoltSpatialQueryState* state, int batchCount)
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

int StepSpatialQueryState(JoltSpatialQueryState* state, int batchCount)
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

void BuildHeadlessObservations(const JoltSpatialQueryState& state, std::array<JoltObservationRow, 6>* observations)
{
	const CaseExecutionSpatialQuery& fixture = state.config.caseExecution->spatialQuery;
	(*observations)[0] = {
	    "ray_queries_per_second", "final", 0, JoltObservationValueType_Float64,
	    DoubleBits(QueryRate(static_cast<int>(fixture.rayCount), state.completedBatchCount, state.rayElapsed))};
	(*observations)[1] = {"sphere_cast_queries_per_second", "final", 0, JoltObservationValueType_Float64,
	                      DoubleBits(QueryRate(static_cast<int>(fixture.sphereCastCount), state.completedBatchCount,
	                                           state.sphereCastElapsed))};
	(*observations)[2] = {
	    "overlap_queries_per_second", "final", 0, JoltObservationValueType_Float64,
	    DoubleBits(QueryRate(static_cast<int>(fixture.overlapCount), state.completedBatchCount, state.overlapElapsed))};
	(*observations)[3] = {"ray_hit_count", "final", 0, JoltObservationValueType_Uint64, state.rayHitCount};
	(*observations)[4] = {"sphere_cast_hit_count", "final", 0, JoltObservationValueType_Uint64,
	                      state.sphereCastHitCount};
	(*observations)[5] = {"overlap_hit_count", "final", 0, JoltObservationValueType_Uint64, state.overlapHitCount};
}

int RunSpatialQueryHeadless(const JoltRunRequest& request)
{
	const JoltCaseConfig config = {&request.caseExecution, request.threadCount, request.repeatIndex, request.stepCount,
	                               request.warmupSteps};
	JoltSpatialQueryState* state = new (std::nothrow) JoltSpatialQueryState{};
	if (state == nullptr)
		return 2;
	int status = CreateSpatialQueryState(config, request.recordingMode, state);
	if (status == 0)
		status = WarmupSpatialQueryState(state, request.warmupSteps);
	JoltCaseView recordingState = {state};
	if (status == 0)
		status = RecordJoltCase(request, &recordingState);
	if (status == 0)
	{
		const int valid =
		    state->physicsSystem.GetNumBodies() == static_cast<JPH::uint>(request.caseExecution.staticBodyCount) &&
		    state->completedBatchCount == request.stepCount && state->workloadElapsedMs > 0.0 &&
		    std::isfinite(state->workloadElapsedMs) != 0;
		std::array<char, 256> settings = {};
		std::array<JoltObservationRow, 6> observations = {};
		FormatSpatialQuerySettings(request.caseExecution, request.threadCount, settings.data(), settings.size());
		BuildHeadlessObservations(*state, &observations);
		const JoltResult result = {
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
		    state->completedBatchCount,
		    state->workloadElapsedMs,
		    state->rawBatchDurations,
		    observations.data(),
		    static_cast<std::uint32_t>(observations.size()),
		};
		status = WriteJoltResult(request, result);
	}
	DestroySpatialQueryState(state);
	delete state;
	return status;
}

int BuildSpatialQueryScene(const JoltCaseView& state, benchmark_visual::VisualGeometry* geometries,
                           benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                           benchmark_visual::VisualInstance* instances, int instanceCapacity, int* geometryCount,
                           int* instanceCount)
{
	if (state.value == nullptr || geometries == nullptr || instances == nullptr || geometryCount == nullptr ||
	    instanceCount == nullptr)
		return 2;
	const JoltSpatialQueryState& value = *static_cast<const JoltSpatialQueryState*>(state.value);
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

int SampleNoSpatialQueryTransforms(const JoltCaseView&, benchmark_visual::VisualStableTransform*, int capacity)
{
	return capacity == 0 ? 0 : 2;
}

int BuildSpatialQueryDebugPrimitives(const JoltCaseView& state, benchmark_visual::VisualDebugPrimitive* primitives,
                                     int capacity)
{
	if (state.value == nullptr || primitives == nullptr)
		return 2;
	const JoltSpatialQueryState& value = *static_cast<const JoltSpatialQueryState*>(state.value);
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
				const JPH::RRayCast& input = value.rayInputs[local];
				const JPH::RVec3 origin = input.mOrigin;
				const JPH::RVec3 end = origin + input.mDirection * value.debugHitFractions[primitiveIndex];
				primitive.originOrCenterX = static_cast<float>(origin.GetX());
				primitive.originOrCenterY = static_cast<float>(origin.GetY());
				primitive.originOrCenterZ = static_cast<float>(origin.GetZ());
				primitive.endOrHalfExtentsX = static_cast<float>(end.GetX());
				primitive.endOrHalfExtentsY = static_cast<float>(end.GetY());
				primitive.endOrHalfExtentsZ = static_cast<float>(end.GetZ());
			}
			else if (family == 1)
			{
				const JPH::RShapeCast& input = value.sphereCastInputs[local];
				const JPH::RVec3 origin = input.mCenterOfMassStart.GetTranslation();
				const JPH::RVec3 end = origin + input.mDirection * value.debugHitFractions[primitiveIndex];
				primitive.originOrCenterX = static_cast<float>(origin.GetX());
				primitive.originOrCenterY = static_cast<float>(origin.GetY());
				primitive.originOrCenterZ = static_cast<float>(origin.GetZ());
				primitive.endOrHalfExtentsX = static_cast<float>(end.GetX());
				primitive.endOrHalfExtentsY = static_cast<float>(end.GetY());
				primitive.endOrHalfExtentsZ = static_cast<float>(end.GetZ());
				primitive.radius = fixture.sphereCastRadius;
			}
			else
			{
				const JPH::AABox& bounds = value.overlapInputs[local];
				const JPH::Vec3 center = bounds.GetCenter();
				primitive.originOrCenterX = center.GetX();
				primitive.originOrCenterY = center.GetY();
				primitive.originOrCenterZ = center.GetZ();
				primitive.endOrHalfExtentsX = fixture.overlapHalfExtents.x;
				primitive.endOrHalfExtentsY = fixture.overlapHalfExtents.y;
				primitive.endOrHalfExtentsZ = fixture.overlapHalfExtents.z;
			}
		}
	}
	return 0;
}

}

const JoltCaseDescriptor& JoltSpatialQueryTraceCaseDescriptor()
{
	static const JoltCaseDescriptor descriptor = {
	    kEngineId,
	    RunSpatialQueryHeadless,
	    [](JoltCaseView* state, int count)
	    {
		    return state == nullptr || state->value == nullptr
		               ? 2
					   : StepSpatialQueryState(static_cast<JoltSpatialQueryState*>(state->value), count);
	    },
	    BuildSpatialQueryScene,
	    SampleNoSpatialQueryTransforms,
	    BuildSpatialQueryDebugPrimitives,
	};
	return descriptor;
}
}
