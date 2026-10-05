#include "box3d_ray_tracing_case.h"
#include "box3d_query_executor.h"
#include "box3d_result_writer.h"
#include "box3d_runner_args.h"
#include "ray_tracing.h"
#include "box3d_ray_tracing_results.h"
#include "box3d_ray_tracing_recording.h"

#include <chrono>
#include <iomanip>

namespace box3d_benchmark
{
namespace
{
using namespace benchmark_ray;
using Clock = std::chrono::steady_clock;
enum RaySceneMode
{
	RaySceneMode_Heavy,
	RaySceneMode_Probe
};
struct RayState
{
	Scene scene;
	b3WorldId world;
	std::vector<b3BodyId> bodies;
	std::vector<b3HullData*> hulls;
	std::vector<b3MeshData*> meshes;
	std::vector<std::uint32_t> triangleMap;
	Box3DQueryExecutor* executor;
	CorpusPhase phase;
	PhaseCapacity capacity;
	std::uint64_t bufferBytes;
	std::vector<Output> outputs;
	std::vector<Hit> hits;
	int threads;
	std::uint32_t callbackCapacity;
	double setupMs;
};
struct RayCallback
{
	RayState* state;
	const Ray* input;
	Output* output;
	std::uint32_t capacity;
};

Status CreateMesh(RayState* state, const Collider& collider, b3BodyId body, const b3ShapeDef& shape)
{
	std::vector<b3Vec3> vertices(collider.triangleCount * 3);
	std::vector<std::int32_t> indices(vertices.size());
	for (std::uint32_t index = 0; index < collider.triangleCount; ++index)
	{
		const Triangle& t = state->scene.triangles[collider.firstTriangle + index];
		vertices[index * 3] = {t.a.x, t.a.y, t.a.z};
		vertices[index * 3 + 1] = {t.b.x, t.b.y, t.b.z};
		vertices[index * 3 + 2] = {t.c.x, t.c.y, t.c.z};
		for (std::uint32_t corner = 0; corner < 3; ++corner)
			indices[index * 3 + corner] = static_cast<std::int32_t>(index * 3 + corner);
	}
	b3MeshDef definition = {};
	definition.vertices = vertices.data();
	definition.indices = indices.data();
	definition.vertexCount = static_cast<int>(vertices.size());
	definition.triangleCount = static_cast<int>(collider.triangleCount);
	b3MeshData* mesh = b3CreateMesh(&definition, nullptr, 0);
	if (mesh == nullptr || mesh->triangleCount != static_cast<int>(collider.triangleCount))
		return Status_Failed;
	state->meshes.push_back(mesh);
	const b3Vec3* cookedVertices = b3GetMeshVertices(mesh);
	const b3MeshTriangle* triangles = b3GetMeshTriangles(mesh);
	for (std::uint32_t cooked = 0; cooked < collider.triangleCount; ++cooked)
	{
		std::uint32_t source = kNoSource;
		for (std::uint32_t candidate = 0; candidate < collider.triangleCount; ++candidate)
		{
			int matches = 1;
			for (std::uint32_t corner = 0; corner < 3; ++corner)
			{
				const b3MeshTriangle t = triangles[cooked];
				const std::int32_t nativeIndex = corner == 0 ? t.index1 : corner == 1 ? t.index2 : t.index3;
				const b3Vec3 a = cookedVertices[nativeIndex];
				const b3Vec3 b = vertices[candidate * 3 + corner];
				matches &= a.x == b.x && a.y == b.y && a.z == b.z;
			}
			if (matches != 0)
			{
				source = candidate;
				break;
			}
		}
		if (source == kNoSource)
			return Status_Failed;
		state->triangleMap[collider.firstTriangle + cooked] = source;
	}
	return b3Shape_IsValid(b3CreateMeshShape(body, &shape, mesh, {1, 1, 1})) ? Status_Ok : Status_Failed;
}

Status CreateCollider(RayState* state, std::uint32_t index)
{
	const Collider& collider = state->scene.colliders[index];
	b3BodyDef bodyDefinition = b3DefaultBodyDef();
	bodyDefinition.type = collider.moving != 0 ? b3_kinematicBody : b3_staticBody;
	bodyDefinition.position = {collider.pose.position.x, collider.pose.position.y, collider.pose.position.z};
	bodyDefinition.rotation = {{collider.pose.rotation.x, collider.pose.rotation.y, collider.pose.rotation.z},
	                           collider.pose.rotation.w};
	bodyDefinition.enableSleep = false;
	const b3BodyId body = b3CreateBody(state->world, &bodyDefinition);
	if (!b3Body_IsValid(body))
		return Status_Failed;
	state->bodies.push_back(body);
	b3ShapeDef shape = b3DefaultShapeDef();
	shape.filter.categoryBits = collider.category;
	shape.filter.maskBits = UINT64_MAX;
	shape.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(index + 1));
	b3ShapeId id = {};
	switch (collider.shape)
	{
	case Shape_Box:
	{
		const b3BoxHull box = b3MakeBoxHull(collider.size.x, collider.size.y, collider.size.z);
		id = b3CreateHullShape(body, &shape, &box.base);
		break;
	}
	case Shape_Sphere:
	{
		const b3Sphere sphere = {{0, 0, 0}, collider.size.x};
		id = b3CreateSphereShape(body, &shape, &sphere);
		break;
	}
	case Shape_Capsule:
	{
		const b3Capsule capsule = {{0, -collider.size.y, 0}, {0, collider.size.y, 0}, collider.size.x};
		id = b3CreateCapsuleShape(body, &shape, &capsule);
		break;
	}
	case Shape_Hull:
	{
		const Hull& hull = state->scene.hulls[collider.hull];
		std::array<b3Vec3, 64> points = {};
		for (std::uint32_t vertex = 0; vertex < hull.vertexCount; ++vertex)
		{
			const Vector p = state->scene.vertices[hull.firstVertex + vertex];
			points[vertex] = {p.x * collider.size.x, p.y * collider.size.y, p.z * collider.size.z};
		}
		b3HullData* native =
		    b3CreateHull(points.data(), static_cast<int>(hull.vertexCount), static_cast<int>(hull.vertexCount));
		if (native == nullptr)
			return Status_Failed;
		state->hulls.push_back(native);
		id = b3CreateHullShape(body, &shape, native);
		break;
	}
	case Shape_Mesh:
		return CreateMesh(state, collider, body, shape);
	}
	return b3Shape_IsValid(id) ? Status_Ok : Status_Failed;
}

float CollectRay(b3ShapeId shape, b3Pos, b3Vec3 normal, float fraction, std::uint64_t, int triangle, int, void* context)
{
	RayCallback* callback = static_cast<RayCallback*>(context);
	Output& output = *callback->output;
	const Phase phase = callback->state->phase.phase;
	if (IsAny(phase) != 0)
	{
		output.count = 1;
		return 0;
	}
	const std::uint32_t id = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(b3Shape_GetUserData(shape)));
	if (id == 0 || id > callback->state->scene.colliders.size())
	{
		output.status = Status_Failed;
		return 0;
	}
	std::uint32_t source = kNoSource;
	const Collider& collider = callback->state->scene.colliders[id - 1];
	if (collider.shape == Shape_Mesh)
	{
		if (triangle < 0 || static_cast<std::uint32_t>(triangle) >= collider.triangleCount)
		{
			output.status = Status_Failed;
			return 0;
		}
		source = callback->state->triangleMap[collider.firstTriangle + static_cast<std::uint32_t>(triangle)];
	}
	const Hit hit = {
	    static_cast<double>(fraction) * callback->input->length, {normal.x, normal.y, normal.z}, id, source, 0};
	if (phase == Phase_ColliderHits)
	{
		if (output.count >= callback->capacity)
		{
			output.status = Status_Capacity;
			return 0;
		}
		callback->state->hits[output.first + output.count++] = hit;
		return 1;
	}
	callback->state->hits[output.first] = hit;
	output.count = 1;
	return fraction;
}

std::uint64_t ExecuteRayLane(void* context, int, int lane)
{
	RayState* state = static_cast<RayState*>(context);
	const std::size_t first =
	    state->phase.rays.size() * static_cast<std::size_t>(lane) / static_cast<std::size_t>(state->threads);
	const std::size_t end =
	    state->phase.rays.size() * static_cast<std::size_t>(lane + 1) / static_cast<std::size_t>(state->threads);
	std::uint64_t hitRays = 0;
	for (std::size_t index = first; index < end; ++index)
	{
		const Ray& ray = state->phase.rays[index];
		Output& output = state->outputs[index];
		b3QueryFilter filter = b3DefaultQueryFilter();
		filter.maskBits = ray.mask;
		RayCallback callback = {state, &ray, &output,
		                        state->callbackCapacity != 0               ? state->callbackCapacity
		                        : state->phase.phase == Phase_ColliderHits ? state->phase.expected[index].count + 4
		                                                                   : 1};
		b3World_CastRay(state->world, {ray.origin.x, ray.origin.y, ray.origin.z},
		                {ray.translation.x, ray.translation.y, ray.translation.z}, filter, CollectRay, &callback);
		output.written = 1;
		hitRays += output.count != 0;
	}
	return hitRays;
}

Status BuildRayState(RayState* state, const Box3DRunRequest& request, RaySceneMode mode)
{
	if (request.rayCorpusPath == nullptr ||
	    ReadScene(std::filesystem::path(request.rayCorpusPath) /
	                  (mode == RaySceneMode_Heavy ? "scene.rtc" : "probe-scene.rtc"),
	              &state->scene) != Status_Ok)
		return Status_Invalid;
	if (mode == RaySceneMode_Heavy &&
	    (state->scene.colliders.size() != request.caseExecution.bodyCount ||
	     state->scene.triangles.size() != request.caseExecution.meshTriangleCount))
		return Status_Invalid;
	if (mode == RaySceneMode_Probe && (state->scene.colliders.empty() || state->scene.colliders.size() > 128))
		return Status_Invalid;
	const Clock::time_point start = Clock::now();
	int moving = 0;
	for (const Collider& collider : state->scene.colliders)
		moving += collider.moving != 0;
	const int stationary = static_cast<int>(state->scene.colliders.size()) - moving;
	state->threads = request.threadCount;
	state->bodies.reserve(state->scene.colliders.size());
	state->triangleMap.resize(state->scene.triangles.size());
	b3WorldDef world = b3DefaultWorldDef();
	world.gravity = {};
	world.enableSleep = false;
	world.enableContinuous = false;
	world.workerCount = 0;
	world.capacity.staticBodyCount = stationary;
	world.capacity.staticShapeCount = stationary;
	world.capacity.dynamicBodyCount = moving;
	world.capacity.dynamicShapeCount = moving;
	state->world = b3CreateWorld(&world);
	if (!b3World_IsValid(state->world))
		return Status_Failed;
	for (std::uint32_t index = 0; index < state->scene.colliders.size(); ++index)
	{
		const Status status = CreateCollider(state, index);
		if (status != Status_Ok)
			return status;
	}
	b3World_RebuildStaticTree(state->world);
	const b3Counters counters = b3World_GetCounters(state->world);
	if (counters.bodyCount != static_cast<int>(state->scene.colliders.size()) ||
	    counters.shapeCount != counters.bodyCount ||
	    CreateBox3DQueryExecutor(state->threads, ExecuteRayLane, state, &state->executor) != 0)
		return Status_Failed;
	state->setupMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	return Status_Ok;
}

void DestroyRayState(RayState* state)
{
	DestroyBox3DQueryExecutor(state->executor);
	if (b3World_IsValid(state->world))
		b3DestroyWorld(state->world);
	for (b3MeshData* mesh : state->meshes)
		b3DestroyMesh(mesh);
	for (b3HullData* hull : state->hulls)
		b3DestroyHull(hull);
}

double PublishPoses(RayState* state, int view)
{
	const Clock::time_point start = Clock::now();
	for (std::size_t index = 0; index < state->scene.colliders.size(); ++index)
	{
		const Collider& collider = state->scene.colliders[index];
		if (collider.moving == 0)
			continue;
		const Pose pose = view < 0 ? collider.pose : UpdatedPose(collider, static_cast<std::uint32_t>(view));
		b3Body_SetTransform(state->bodies[index], {pose.position.x, pose.position.y, pose.position.z},
		                    {{pose.rotation.x, pose.rotation.y, pose.rotation.z}, pose.rotation.w});
	}
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

Status AdmitRayStorage(RayState* state, const Box3DRunRequest& request, RaySceneMode mode)
{
	const std::uint32_t views = mode == RaySceneMode_Heavy ? request.caseExecution.rayTracing.viewCount : kViewCount;
	for (std::uint32_t view = 0; view < views; ++view)
	{
		const std::uint32_t count = mode == RaySceneMode_Heavy ? static_cast<std::uint32_t>(Phase_Count)
		                                                       : static_cast<std::uint32_t>(Probe_Count);
		for (std::uint32_t index = 0; index < count; ++index)
		{
			const std::filesystem::path path =
			    mode == RaySceneMode_Heavy
			        ? PhasePath(request.rayCorpusPath, view, static_cast<Phase>(index))
			        : std::filesystem::path(request.rayCorpusPath) /
			              ("probe-view-" + std::to_string(view) + "-" + ProbeName(static_cast<Probe>(index)) + ".rtr");
			PhaseMetadata metadata = {};
			if (ReadPhaseMetadata(path, &metadata) != Status_Ok || metadata.view != view ||
			    (mode == RaySceneMode_Heavy &&
			     (metadata.phase != index || metadata.width != request.caseExecution.rayTracing.width ||
	      metadata.height != request.caseExecution.rayTracing.height)) ||
			    (mode == RaySceneMode_Probe && metadata.rays != 512))
				return Status_Invalid;
			IncludePhaseCapacity(metadata, &state->capacity);
		}
	}
	const Status status = ReservePhaseStorage(state->capacity, &state->phase, &state->outputs, &state->hits);
	state->bufferBytes = PhaseStorageBytes(state->phase, state->outputs, state->hits);
	return status;
}

void ResetOutputs(RayState* state)
{
	for (Output& output : state->outputs)
	{
		output.count = 0;
		output.written = 0;
		output.status = Status_Ok;
	}
}

double ExecutePhase(RayState* state)
{
	const Clock::time_point start = Clock::now();
	DispatchBox3DQuery(state->executor, 0);
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

Status RunSuite(RayState* state, const Box3DRunRequest& request, std::uint32_t suite, std::ofstream& results,
                std::ofstream& failures, Box3DRayRecording* recording, double* primaryMs,
                std::uint64_t* correctnessErrors)
{
	const std::uint32_t warmup = request.rayStage == Box3DRayRunStage_Preflight ? 0 : request.caseExecution.warmupWorkUnitCount;
	const std::uint32_t measuredSuite = suite >= warmup ? suite - warmup : suite;
	const std::uint32_t view = measuredSuite % request.caseExecution.rayTracing.viewCount;
	PublishPoses(state, -1);
	const Clock::time_point suiteStart = Clock::now();
	std::array<Box3DRayPhaseResult, 18> rows = {};
	std::size_t rowCount = 0;
	for (std::uint32_t slot = 0; slot < Phase_Count; ++slot)
	{
		const Phase phase = slot == 8 ? Phase_Updated : static_cast<Phase>((slot + suite) % 8);
		Status status = ReadPhase(PhasePath(request.rayCorpusPath, view, phase), &state->phase, &state->capacity);
		if (status != Status_Ok || state->phase.view != view || state->phase.phase != phase)
			return Status_Invalid;
		if (suite >= warmup)
			rows[rowCount++] = {phase, Api_NativeBatch, "unsupported"};
		if (phase == Phase_MeshHits)
		{
			if (suite >= warmup)
				rows[rowCount++] = {phase, Api_Ordinary, "unsupported"};
			continue;
		}
		status = PreparePhaseOutputs(state->phase, &state->outputs, &state->hits);
		if (status != Status_Ok)
			return status;
		double conditioningMs = 0, updateMs = 0;
		if (suite >= warmup)
		{
			const Clock::time_point conditioning = Clock::now();
			if (phase == Phase_Updated)
				PublishPoses(state, static_cast<int>(view));
			ExecutePhase(state);
			if (phase == Phase_Updated)
				PublishPoses(state, -1);
			ResetOutputs(state);
			conditioningMs = std::chrono::duration<double, std::milli>(Clock::now() - conditioning).count();
		}
		if (phase == Phase_Updated)
			updateMs = PublishPoses(state, static_cast<int>(view));
		const double queryMs = ExecutePhase(state);
		const Clock::time_point validationStart = Clock::now();
		const Validation validation = ValidateOutputs(state->phase, state->outputs, state->hits);
		const double validationMs = std::chrono::duration<double, std::milli>(Clock::now() - validationStart).count();
		int completed = 1;
		for (const Output& output : state->outputs)
			if (output.status != Status_Ok || output.written != 1 ||
			    static_cast<std::uint64_t>(output.first) + output.count > state->hits.size())
				completed = 0;
		if (suite >= warmup)
			rows[rowCount++] = {phase,
			                    Api_Ordinary,
			                    completed == 0 ? "execution_failed" : validation.errors == 0 ? "supported" : "failed",
			                    state->phase.rays.size(),
			                    validation,
			                    queryMs,
			                    updateMs,
			                    conditioningMs,
			                    validationMs,
			                    state->bufferBytes};
		if (validation.errors != 0)
		{
			if (WriteBox3DRayFailures(failures, state->phase, state->outputs, state->hits, validation) != Status_Ok)
				return Status_Io;
		}
		if (suite >= warmup)
			*correctnessErrors += validation.errors;
		if (completed == 0)
		{
			const Status flush = FlushBox3DRaySuite(results, request, view, measuredSuite, rows.data(), rowCount,
			                                        state->setupMs,
			                                        std::chrono::duration<double, std::milli>(Clock::now() - suiteStart).count());
			return flush == Status_Ok ? Status_Failed : flush;
		}
		if (phase == Phase_Primary)
			*primaryMs = queryMs;
		if (suite >= warmup &&
		    AppendBox3DRayRecording(recording, state->phase, state->outputs, state->hits, measuredSuite) != Status_Ok)
		{
			FlushBox3DRaySuite(results, request, view, measuredSuite, rows.data(), rowCount, state->setupMs,
			    std::chrono::duration<double, std::milli>(Clock::now() - suiteStart).count());
			return Status_Io;
		}
		if (!results)
			return Status_Io;
	}
	const double fullSuiteMs = std::chrono::duration<double, std::milli>(Clock::now() - suiteStart).count();
	return FlushBox3DRaySuite(results, request, view, measuredSuite, rows.data(), rowCount, state->setupMs,
	                          fullSuiteMs);
}

Status RunProbes(const Box3DRunRequest& request)
{
	RayState state = {};
	Status status = BuildRayState(&state, request, RaySceneMode_Probe);
	if (status == Status_Ok)
		status = AdmitRayStorage(&state, request, RaySceneMode_Probe);
	if (status != Status_Ok)
	{
		DestroyRayState(&state);
		return status;
	}
	const std::string prefix =
	    "box3d_t" + std::to_string(request.threadCount) + "_r" + std::to_string(request.repeatIndex);
	std::ofstream results(prefix + "_ray-capabilities.csv", std::ios::trunc);
	results
	    << "engine_id,thread_count,repeat_index,view,capability,api,status,queries,hit_rays,reference_mismatches,native_errors,overflow_reports,world_colliders\n";
	for (std::uint32_t view = 0; view < 6 && status == Status_Ok; ++view)
		for (std::uint32_t probeIndex = 0; probeIndex < Probe_Count && status == Status_Ok; ++probeIndex)
		{
			const Probe probe = static_cast<Probe>(probeIndex);
			const std::filesystem::path path = std::filesystem::path(request.rayCorpusPath) /
			                                   ("probe-view-" + std::to_string(view) + "-" + ProbeName(probe) + ".rtr");
			status = ReadPhase(path, &state.phase, &state.capacity);
			if (status != Status_Ok || state.phase.rays.size() != 512 || state.phase.view != view)
			{
				status = Status_Invalid;
				break;
			}
			status = PreparePhaseOutputs(state.phase, &state.outputs, &state.hits);
			if (status != Status_Ok)
				break;
			state.callbackCapacity = probe == Probe_Overflow ? 2u : 0u;
			ExecutePhase(&state);
			std::uint64_t nativeErrors = 0, overflowReports = 0;
			for (const Output& output : state.outputs)
			{
				if (probe == Probe_Overflow && output.status == Status_Capacity)
					++overflowReports;
				else if (output.status != Status_Ok || output.written != 1)
					++nativeErrors;
			}
			const Validation validation = ValidateOutputs(state.phase, state.outputs, state.hits);
			const int failed = nativeErrors != 0 || (probe == Probe_Overflow && overflowReports != 512) ||
			                   (probe == Probe_Concurrent && validation.errors != 0);
			const char* outcome = failed != 0                                         ? "failed"
			                      : probe != Probe_Overflow && validation.errors != 0 ? "native_semantics_differ"
			                                                                          : "supported";
			results << "box3d," << request.threadCount << ',' << request.repeatIndex << ',' << view << ','
			        << ProbeName(probe) << ",ordinary," << outcome << ",512," << validation.hitRays << ','
			        << (probe == Probe_Overflow ? 0 : validation.errors) << ',' << nativeErrors << ','
			        << overflowReports << ',' << state.scene.colliders.size() << '\n';
			if (!results)
				status = Status_Io;
		}
	DestroyRayState(&state);
	return status;
}

int RunRayTracing(const Box3DRunRequest& request)
{
	if (RunProbes(request) != Status_Ok)
		return 2;
	RayState state = {};
	Status status = BuildRayState(&state, request, RaySceneMode_Heavy);
	if (status == Status_Ok)
		status = AdmitRayStorage(&state, request, RaySceneMode_Heavy);
	if (status != Status_Ok)
	{
		DestroyRayState(&state);
		return 2;
	}
	const std::string prefix =
	    "box3d_t" + std::to_string(request.threadCount) + "_r" + std::to_string(request.repeatIndex);
	std::ofstream results(prefix + "_ray-tracing.csv", std::ios::trunc);
	std::ofstream failures(prefix + "_ray-failures.txt", std::ios::trunc);
	results << std::setprecision(17);
	failures << std::setprecision(17);
	results
	    << "engine_id,thread_count,repeat_index,view,suite,phase,api,status,queries,hit_rays,query_ms,update_ms,conditioning_ms,validation_ms,errors,written,setup_ms,suite_ms,buffer_bytes\n";
	Box3DRayRecording recording = {};
	if (!results || !failures)
		status = Status_Io;
	if (status == Status_Ok)
		status = BeginBox3DRayRecording(&recording, request);
	std::vector<Clock::duration::rep> durations(request.caseExecution.measuredWorkUnitCount);
	double elapsedMs = 0;
	std::uint64_t correctnessErrors = 0;
	const std::uint32_t warmup = request.caseExecution.warmupWorkUnitCount;
	const std::uint32_t suiteCount = request.rayStage == Box3DRayRunStage_Preflight
	    ? request.caseExecution.rayTracing.viewCount : warmup + request.caseExecution.measuredWorkUnitCount;
	for (std::uint32_t suite = 0; suite < suiteCount && status == Status_Ok; ++suite)
	{
		double primaryMs = 0;
		status = RunSuite(&state, request, suite, results, failures, &recording, &primaryMs, &correctnessErrors);
		if (request.rayStage == Box3DRayRunStage_Heavy && suite >= warmup)
		{
			durations[suite - warmup] =
			    std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(primaryMs))
			        .count();
			elapsedMs += primaryMs;
		}
	}
	int resultCode = status == Status_Ok ? 0 : 2;
	if (status == Status_Ok && request.rayStage == Box3DRayRunStage_Heavy)
	{
		const Box3DObservationRow observation = {"ray_correctness_errors", "final", 0, Box3DObservationValueType_Uint64,
		                                         correctnessErrors};
		char settings[512] = {};
		std::snprintf(settings, sizeof(settings), "ordinary coherent closest only in headline; native callback queries; no physics step; batch and mesh_surface_hits unsupported; worker_count=%d", request.threadCount - 1);
		const Box3DResult result = {
		    "ray_tracing",
		    1,
		    settings,
		    static_cast<int>(request.caseExecution.bodyCount),
		    static_cast<int>(request.caseExecution.shapeCount),
		    static_cast<int>(request.caseExecution.queryCount),
		    0,
		    0,
		    correctnessErrors == 0 ? "ok" : "failed",
		    "ok",
		    request.threadCount,
		    request.threadCount - 1,
		    static_cast<int>(request.caseExecution.measuredWorkUnitCount),
		    elapsedMs,
		    durations.data(),
		    &observation,
		    1};
		resultCode = WriteBox3DResult(request, result);
	}
	if (status == Status_Ok && CompleteBox3DRayRecording(&recording) != Status_Ok)
		resultCode = 2;
	DestroyRayState(&state);
	return resultCode;
}
}

const Box3DCaseDescriptor& Box3DRayTracingCaseDescriptor()
{
	static const Box3DCaseDescriptor descriptor = {"box3d", RunRayTracing, nullptr, nullptr, nullptr, nullptr};
	return descriptor;
}
}
