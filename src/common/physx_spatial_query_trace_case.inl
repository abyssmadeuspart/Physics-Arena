#include "task/PxTask.h"

#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>

namespace PHYSICS_ARENA_PHYSX_NAMESPACE
{
enum class PhysXSpatialQueryFamily : std::uint8_t
{
	Ray,
	SphereCast,
	Overlap,
};

std::uint64_t ExecuteSpatialQueryLane(PhysXSpatialQueryState* state, PhysXSpatialQueryFamily family, int laneIndex)
{
	const CaseExecutionSpatialQuery& fixture = state->config.caseExecution->spatialQuery;
	const std::uint32_t queryCount = family == PhysXSpatialQueryFamily::Ray          ? fixture.rayCount
	                                 : family == PhysXSpatialQueryFamily::SphereCast ? fixture.sphereCastCount
	                                                                                 : fixture.overlapCount;
	const std::uint32_t start =
	    static_cast<std::uint32_t>((static_cast<std::uint64_t>(queryCount) * static_cast<std::uint32_t>(laneIndex)) /
		                           static_cast<std::uint32_t>(state->config.threadCount));
	const std::uint32_t end = static_cast<std::uint32_t>(
	    (static_cast<std::uint64_t>(queryCount) * static_cast<std::uint32_t>(laneIndex + 1)) /
	    static_cast<std::uint32_t>(state->config.threadCount));
	std::uint64_t hitCount = 0;
	if (family == PhysXSpatialQueryFamily::Ray)
	{
		for (std::uint32_t index = start; index < end; ++index)
		{
			const PhysXRayInput& input = state->rayInputs[index];
			physx::PxRaycastBuffer result;
			const bool hit = state->context.scene->raycast(input.origin, input.direction, fixture.queryDistance, result,
			                                               physx::PxHitFlags(), state->closestFilter);
			hitCount += hit ? 1U : 0U;
		}
	}
	else if (family == PhysXSpatialQueryFamily::SphereCast)
	{
		for (std::uint32_t index = start; index < end; ++index)
		{
			const PhysXSphereCastInput& input = state->sphereCastInputs[index];
			physx::PxSweepBuffer result;
			const bool hit = state->context.scene->sweep(
			    state->sphere, input.pose, input.direction, fixture.queryDistance, result,
			    physx::PxHitFlags(physx::PxHitFlag::eASSUME_NO_INITIAL_OVERLAP), state->closestFilter);
			hitCount += hit ? 1U : 0U;
		}
	}
	else
	{
		for (std::uint32_t index = start; index < end; ++index)
		{
			physx::PxOverlapBuffer result;
			const bool hit = state->context.scene->overlap(state->overlapBox, state->overlapInputs[index], result,
			                                               state->overlapFilter);
			hitCount += hit ? 1U : 0U;
		}
	}
	return hitCount;
}

struct PhysXSpatialQueryCompletion
{
	std::mutex mutex;
	std::condition_variable condition;
	int remainingTaskCount;
};

struct PhysXSpatialQueryLaneTask : physx::PxLightCpuTask
{
	PhysXSpatialQueryState* state;
	PhysXSpatialQueryCompletion* completion;
	PhysXSpatialQueryFamily family;
	int laneIndex;

	void run() override
	{
		state->laneHitCounts[laneIndex] = ExecuteSpatialQueryLane(state, family, laneIndex);
	}

	void release() override
	{
		physx::PxLightCpuTask::release();
		std::lock_guard<std::mutex> lock(completion->mutex);
		--completion->remainingTaskCount;
		if (completion->remainingTaskCount == 0)
			completion->condition.notify_one();
	}

	const char* getName() const override
	{
		return "Spatial Query Lane";
	}
};

namespace
{
void DestroySpatialQueryState(PhysXSpatialQueryState* state);

float CenteredGridCoordinate(float base, float spacing, std::uint32_t coordinate, std::uint32_t count)
{
	return base + spacing * (static_cast<float>(coordinate) - 0.5f * static_cast<float>(count - 1));
}

float UncenteredGridCoordinate(float base, float spacing, std::uint32_t coordinate)
{
	return base + spacing * static_cast<float>(coordinate);
}

void GenerateSpatialQuery(const CaseExecutionSpec& execution, int index, PhysXSpatialQuery* query)
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

int InitializeSpatialQueryContext(PhysXSpatialQueryState* state)
{
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	PhysXContext* context = &state->context;
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
	const int workerCount = RequestedWorkerCount(state->config.threadCount);
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
	sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;
	sceneDesc.staticStructure = physx::PxPruningStructureType::eSTATIC_AABB_TREE;
	context->scene = context->physics->createScene(sceneDesc);
	if (context->scene == nullptr)
		return 2;
	context->material = context->physics->createMaterial(execution.friction, execution.friction, execution.restitution);
	return context->material != nullptr ? 0 : 2;
}

int CreateSpatialQueryFixture(PhysXSpatialQueryState* state)
{
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	const CaseExecutionSpatialQuery& fixture = state->config.caseExecution->spatialQuery;
	for (std::uint32_t iy = 0; iy < fixture.staticGrid[1]; ++iy)
		for (std::uint32_t iz = 0; iz < fixture.staticGrid[2]; ++iz)
			for (std::uint32_t ix = 0; ix < fixture.staticGrid[0]; ++ix)
			{
				const std::uint32_t slot = (iy * fixture.staticGrid[2] + iz) * fixture.staticGrid[0] + ix;
				const physx::PxTransform transform(
				    physx::PxVec3(CenteredGridCoordinate(fixture.staticBaseCenter.x, fixture.staticSpacing.x, ix,
					                                     fixture.staticGrid[0]),
					              UncenteredGridCoordinate(fixture.staticBaseCenter.y, fixture.staticSpacing.y, iy),
					              CenteredGridCoordinate(fixture.staticBaseCenter.z, fixture.staticSpacing.z, iz,
					                                     fixture.staticGrid[2])),
				    PhysXShapeRotation(execution.selectedGeometry.axis));
				physx::PxRigidStatic* actor = state->context.physics->createRigidStatic(transform);
				if (actor == nullptr)
					return 2;
				physx::PxShape* shape = AttachPhysXResolvedShape(&state->context, actor, execution.selectedGeometry);
				if (shape == nullptr)
				{
					actor->release();
					return 2;
				}
				state->context.scene->addActor(*actor);
				state->bodies[slot] = actor;
				state->createdBodyCount += 1;
			}
	return 0;
}

void CaptureSpatialQueryDebugSamples(PhysXSpatialQueryState* state)
{
	const CaseExecutionSpatialQuery& fixture = state->config.caseExecution->spatialQuery;
	const int samples = static_cast<int>(fixture.debugSamplesPerFamily);
	for (int index = 0; index < samples; ++index)
	{
		const PhysXRayInput& input = state->rayInputs[index];
		physx::PxRaycastBuffer result;
		const bool hit = state->context.scene->raycast(input.origin, input.direction, fixture.queryDistance, result,
		                                               physx::PxHitFlags(), state->closestFilter);
		state->debugHits[index] = hit ? 1 : 0;
		state->debugHitDistances[index] = hit ? result.block.distance : fixture.queryDistance;
	}
	for (int index = 0; index < samples; ++index)
	{
		const PhysXSphereCastInput& input = state->sphereCastInputs[index];
		physx::PxSweepBuffer result;
		const bool hit = state->context.scene->sweep(
		    state->sphere, input.pose, input.direction, fixture.queryDistance, result,
		    physx::PxHitFlags(physx::PxHitFlag::eASSUME_NO_INITIAL_OVERLAP), state->closestFilter);
		const int debugIndex = samples + index;
		state->debugHits[debugIndex] = hit ? 1 : 0;
		state->debugHitDistances[debugIndex] = hit ? result.block.distance : fixture.queryDistance;
	}
	for (int index = 0; index < samples; ++index)
	{
		physx::PxOverlapBuffer result;
		const bool hit =
		    state->context.scene->overlap(state->overlapBox, state->overlapInputs[index], result, state->overlapFilter);
		const int debugIndex = 2 * samples + index;
		state->debugHits[debugIndex] = hit ? 1 : 0;
		state->debugHitDistances[debugIndex] = fixture.queryDistance;
	}
}

std::uint64_t DispatchSpatialQuery(PhysXSpatialQueryState* state, PhysXSpatialQueryFamily family)
{
	if (state->config.threadCount == 1)
		return ExecuteSpatialQueryLane(state, family, 0);
	PhysXSpatialQueryCompletion* completion = state->queryCompletion;
	{
		std::lock_guard<std::mutex> lock(completion->mutex);
		completion->remainingTaskCount = state->config.threadCount - 1;
	}
	for (int laneIndex = 1; laneIndex < state->config.threadCount; ++laneIndex)
	{
		PhysXSpatialQueryLaneTask& task = state->laneTasks[laneIndex - 1];
		task.state = state;
		task.completion = completion;
		task.family = family;
		task.laneIndex = laneIndex;
		state->context.dispatcher->submitTask(task);
	}
	state->laneHitCounts[0] = ExecuteSpatialQueryLane(state, family, 0);
	{
		std::unique_lock<std::mutex> lock(completion->mutex);
		completion->condition.wait(lock,
		                           [completion]
		                           {
			                           return completion->remainingTaskCount == 0;
		                           });
	}
	std::uint64_t hitCount = 0;
	for (int laneIndex = 0; laneIndex < state->config.threadCount; ++laneIndex)
		hitCount += state->laneHitCounts[laneIndex];
	return hitCount;
}

void ExecuteSpatialQueryBatch(PhysXSpatialQueryState* state, int measured,
                              std::chrono::steady_clock::duration::rep* batchDuration)
{
	const std::chrono::steady_clock::time_point batchStart = std::chrono::steady_clock::now();
	const std::chrono::steady_clock::time_point rayStart = std::chrono::steady_clock::now();
	state->rayHitCount = DispatchSpatialQuery(state, PhysXSpatialQueryFamily::Ray);
	const std::chrono::steady_clock::time_point rayEnd = std::chrono::steady_clock::now();
	const std::chrono::steady_clock::time_point castStart = std::chrono::steady_clock::now();
	state->sphereCastHitCount = DispatchSpatialQuery(state, PhysXSpatialQueryFamily::SphereCast);
	const std::chrono::steady_clock::time_point castEnd = std::chrono::steady_clock::now();
	const std::chrono::steady_clock::time_point overlapStart = std::chrono::steady_clock::now();
	state->overlapHitCount = DispatchSpatialQuery(state, PhysXSpatialQueryFamily::Overlap);
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

int CreateSpatialQueryState(const PhysXCaseConfig& config, benchmark_replay::RecordingMode recordingMode,
                            PhysXSpatialQueryState* state)
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
	state->bodies = new (std::nothrow) physx::PxRigidStatic*[execution.staticBodyCount]();
	state->rayInputs = new (std::nothrow) PhysXRayInput[fixture.rayCount];
	state->sphereCastInputs = new (std::nothrow) PhysXSphereCastInput[fixture.sphereCastCount];
	state->overlapInputs = new (std::nothrow) physx::PxTransform[fixture.overlapCount];
	if (recordingMode == benchmark_replay::RecordingMode_On)
	{
		state->debugHits = new (std::nothrow) std::uint8_t[execution.visualDebugPrimitiveCount]();
		state->debugHitDistances = new (std::nothrow) float[execution.visualDebugPrimitiveCount]();
	}
	state->rawBatchDurations =
	    new (std::nothrow) std::chrono::steady_clock::duration::rep[execution.measuredWorkUnitCount]();
	state->queryCompletion = new (std::nothrow) PhysXSpatialQueryCompletion{};
	state->laneTasks =
	    config.threadCount > 1 ? new (std::nothrow) PhysXSpatialQueryLaneTask[config.threadCount - 1] : nullptr;
	state->laneHitCounts = new (std::nothrow) std::uint64_t[config.threadCount]();
	if (state->bodies == nullptr || state->rayInputs == nullptr || state->sphereCastInputs == nullptr ||
	    state->overlapInputs == nullptr ||
	    (recordingMode == benchmark_replay::RecordingMode_On &&
	     (state->debugHits == nullptr || state->debugHitDistances == nullptr)) ||
	    state->rawBatchDurations == nullptr || state->queryCompletion == nullptr ||
	    (config.threadCount > 1 && state->laneTasks == nullptr) || state->laneHitCounts == nullptr ||
	    InitializeSpatialQueryContext(state) != 0)
	{
		DestroySpatialQueryState(state);
		return 2;
	}
	for (std::uint32_t index = 0; index < fixture.rayCount; ++index)
	{
		PhysXSpatialQuery query = {};
		GenerateSpatialQuery(execution, static_cast<int>(index), &query);
		state->rayInputs[index] = {
		    physx::PxVec3(query.originX, query.originY, query.originZ),
		    physx::PxVec3(query.directionX, query.directionY, query.directionZ),
		};
	}
	for (std::uint32_t index = 0; index < fixture.sphereCastCount; ++index)
	{
		PhysXSpatialQuery query = {};
		GenerateSpatialQuery(execution, static_cast<int>(fixture.rayCount + index), &query);
		state->sphereCastInputs[index] = {
		    physx::PxTransform(physx::PxVec3(query.originX, query.originY, query.originZ)),
		    physx::PxVec3(query.directionX, query.directionY, query.directionZ),
		};
	}
	for (std::uint32_t index = 0; index < fixture.overlapCount; ++index)
	{
		PhysXSpatialQuery query = {};
		GenerateSpatialQuery(execution, static_cast<int>(fixture.rayCount + fixture.sphereCastCount + index), &query);
		state->overlapInputs[index] = physx::PxTransform(physx::PxVec3(query.originX, query.originY, query.originZ));
	}
	state->closestFilter.flags = physx::PxQueryFlag::eSTATIC;
	state->overlapFilter.flags = physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eANY_HIT;
	state->sphere = physx::PxSphereGeometry(fixture.sphereCastRadius);
	state->overlapBox =
	    physx::PxBoxGeometry(fixture.overlapHalfExtents.x, fixture.overlapHalfExtents.y, fixture.overlapHalfExtents.z);
	if (CreateSpatialQueryFixture(state) != 0)
	{
		DestroySpatialQueryState(state);
		return 2;
	}
	state->context.scene->flushQueryUpdates();
	if (recordingMode == benchmark_replay::RecordingMode_On)
		CaptureSpatialQueryDebugSamples(state);
	return 0;
}

void DestroySpatialQueryState(PhysXSpatialQueryState* state)
{
	if (state == nullptr)
		return;
	delete[] state->laneTasks;
	delete state->queryCompletion;
	delete[] state->laneHitCounts;
	state->laneTasks = nullptr;
	state->queryCompletion = nullptr;
	state->laneHitCounts = nullptr;
	if (state->bodies != nullptr)
		for (int index = 0; index < state->createdBodyCount; ++index)
			if (state->bodies[index] != nullptr)
			{
				state->bodies[index]->release();
				state->bodies[index] = nullptr;
			}
	if (state->context.scene != nullptr)
	{
		state->context.scene->release();
		state->context.scene = nullptr;
	}
	if (state->context.material != nullptr)
	{
		state->context.material->release();
		state->context.material = nullptr;
	}
	if (state->context.dispatcher != nullptr)
	{
		state->context.dispatcher->release();
		state->context.dispatcher = nullptr;
	}
	if (state->context.physics != nullptr)
	{
		if (state->context.convexMesh != nullptr)
		{
			state->context.convexMesh->release();
			state->context.convexMesh = nullptr;
		}
		state->context.physics->release();
		state->context.physics = nullptr;
	}
	if (state->context.foundation != nullptr)
	{
		state->context.foundation->release();
		state->context.foundation = nullptr;
	}
	delete[] state->bodies;
	delete[] state->rayInputs;
	delete[] state->sphereCastInputs;
	delete[] state->overlapInputs;
	delete[] state->debugHits;
	delete[] state->debugHitDistances;
	delete[] state->rawBatchDurations;
	state->bodies = nullptr;
	state->rayInputs = nullptr;
	state->sphereCastInputs = nullptr;
	state->overlapInputs = nullptr;
	state->debugHits = nullptr;
	state->debugHitDistances = nullptr;
	state->rawBatchDurations = nullptr;
	state->createdBodyCount = 0;
}

int CheckSpatialQueryBatch(const PhysXSpatialQueryState* state, const char* phase, int batch)
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
			    kEngineId, phase, batch, family == 0 ? "ray" : (family == 1 ? "sphere_cast" : "overlap"),
			    static_cast<unsigned long long>(expected), static_cast<unsigned long long>(actual));
			return 2;
		}
	}
	return 0;
}

int WarmupSpatialQueryState(PhysXSpatialQueryState* state, int batchCount)
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

int StepSpatialQueryState(PhysXSpatialQueryState* state, int batchCount)
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
	const int size = std::snprintf(
	    settings, settingsCapacity, "query_world=static_only; worker_count=%d; rays=%u; sphere_casts=%u; overlaps=%u",
	    RequestedWorkerCount(threadCount), fixture.rayCount, fixture.sphereCastCount, fixture.overlapCount);
	return size > 0 && static_cast<std::size_t>(size) < settingsCapacity ? 0 : 2;
}

void BuildHeadlessObservations(const PhysXSpatialQueryState& state, std::array<PhysXObservationRow, 6>* observations)
{
	const CaseExecutionSpatialQuery& fixture = state.config.caseExecution->spatialQuery;
	(*observations)[0] = {
	    "ray_queries_per_second", "final", 0, PhysXObservationValueType_Float64,
	    DoubleBits(QueryRate(static_cast<int>(fixture.rayCount), state.completedBatchCount, state.rayElapsed))};
	(*observations)[1] = {"sphere_cast_queries_per_second", "final", 0, PhysXObservationValueType_Float64,
	                      DoubleBits(QueryRate(static_cast<int>(fixture.sphereCastCount), state.completedBatchCount,
	                                           state.sphereCastElapsed))};
	(*observations)[2] = {
	    "overlap_queries_per_second", "final", 0, PhysXObservationValueType_Float64,
	    DoubleBits(QueryRate(static_cast<int>(fixture.overlapCount), state.completedBatchCount, state.overlapElapsed))};
	(*observations)[3] = {"ray_hit_count", "final", 0, PhysXObservationValueType_Uint64, state.rayHitCount};
	(*observations)[4] = {"sphere_cast_hit_count", "final", 0, PhysXObservationValueType_Uint64,
	                      state.sphereCastHitCount};
	(*observations)[5] = {"overlap_hit_count", "final", 0, PhysXObservationValueType_Uint64, state.overlapHitCount};
}

int RunSpatialQueryHeadless(const PhysXRunRequest& request)
{
	const PhysXCaseConfig config = {&request.caseExecution, request.threadCount, request.repeatIndex, request.stepCount,
	                                request.warmupSteps};
	PhysXSpatialQueryState* state = new (std::nothrow) PhysXSpatialQueryState{};
	if (state == nullptr)
		return 2;
	int status = CreateSpatialQueryState(config, request.recordingMode, state);
	if (status == 0)
		status = WarmupSpatialQueryState(state, request.warmupSteps);
	PhysXCaseView recordingState = {state};
	if (status == 0)
		status = RecordPhysXCase(request, &recordingState);
	if (status == 0)
	{
		const int valid = state->context.scene->getNbActors(physx::PxActorTypeFlag::eRIGID_STATIC) ==
		                      static_cast<physx::PxU32>(request.caseExecution.staticBodyCount) &&
		                  state->completedBatchCount == request.stepCount && state->workloadElapsedMs > 0.0 &&
		                  std::isfinite(state->workloadElapsedMs) != 0;
		std::array<char, 256> settings = {};
		std::array<PhysXObservationRow, 6> observations = {};
		FormatSpatialQuerySettings(request.caseExecution, request.threadCount, settings.data(), settings.size());
		BuildHeadlessObservations(*state, &observations);
		const PhysXResult result = {
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
		    RequestedWorkerCount(request.threadCount),
		    state->completedBatchCount,
		    state->workloadElapsedMs,
		    state->rawBatchDurations,
		    observations.data(),
		    static_cast<std::uint32_t>(observations.size()),
		};
		status = WritePhysXResult(request, result);
	}
	DestroySpatialQueryState(state);
	delete state;
	return status;
}

int BuildSpatialQueryScene(const PhysXCaseView& state, benchmark_visual::VisualGeometry* geometries,
                           benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                           benchmark_visual::VisualInstance* instances, int instanceCapacity, int* geometryCount,
                           int* instanceCount)
{
	if (state.value == nullptr || geometries == nullptr || instances == nullptr || geometryCount == nullptr ||
	    instanceCount == nullptr)
		return 2;
	const PhysXSpatialQueryState& value = *static_cast<const PhysXSpatialQueryState*>(state.value);
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

int SampleNoSpatialQueryTransforms(const PhysXCaseView&, benchmark_visual::VisualStableTransform*, int capacity)
{
	return capacity == 0 ? 0 : 2;
}

int BuildSpatialQueryDebugPrimitives(const PhysXCaseView& state, benchmark_visual::VisualDebugPrimitive* primitives,
                                     int capacity)
{
	if (state.value == nullptr || primitives == nullptr)
		return 2;
	const PhysXSpatialQueryState& value = *static_cast<const PhysXSpatialQueryState*>(state.value);
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
				const PhysXRayInput& input = value.rayInputs[local];
				const physx::PxVec3 end = input.origin + input.direction * value.debugHitDistances[primitiveIndex];
				primitive.originOrCenterX = input.origin.x;
				primitive.originOrCenterY = input.origin.y;
				primitive.originOrCenterZ = input.origin.z;
				primitive.endOrHalfExtentsX = end.x;
				primitive.endOrHalfExtentsY = end.y;
				primitive.endOrHalfExtentsZ = end.z;
			}
			else if (family == 1)
			{
				const PhysXSphereCastInput& input = value.sphereCastInputs[local];
				const physx::PxVec3 end = input.pose.p + input.direction * value.debugHitDistances[primitiveIndex];
				primitive.originOrCenterX = input.pose.p.x;
				primitive.originOrCenterY = input.pose.p.y;
				primitive.originOrCenterZ = input.pose.p.z;
				primitive.endOrHalfExtentsX = end.x;
				primitive.endOrHalfExtentsY = end.y;
				primitive.endOrHalfExtentsZ = end.z;
				primitive.radius = fixture.sphereCastRadius;
			}
			else
			{
				const physx::PxVec3 center = value.overlapInputs[local].p;
				primitive.originOrCenterX = center.x;
				primitive.originOrCenterY = center.y;
				primitive.originOrCenterZ = center.z;
				primitive.endOrHalfExtentsX = fixture.overlapHalfExtents.x;
				primitive.endOrHalfExtentsY = fixture.overlapHalfExtents.y;
				primitive.endOrHalfExtentsZ = fixture.overlapHalfExtents.z;
			}
		}
	}
	return 0;
}

} // namespace

const PhysXCaseDescriptor& PhysXSpatialQueryTraceCaseDescriptor()
{
	static const PhysXCaseDescriptor descriptor = {
	    kEngineId,
	    RunSpatialQueryHeadless,
	    [](PhysXCaseView* state, int count)
	    {
		    return state == nullptr || state->value == nullptr
		               ? 2
					   : StepSpatialQueryState(static_cast<PhysXSpatialQueryState*>(state->value), count);
	    },
	    BuildSpatialQueryScene,
	    SampleNoSpatialQueryTransforms,
	    BuildSpatialQueryDebugPrimitives,
	};
	return descriptor;
}
} // namespace PHYSICS_ARENA_PHYSX_NAMESPACE
