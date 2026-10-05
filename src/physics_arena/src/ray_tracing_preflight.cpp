#include "physics_arena/ray_tracing_preflight.h"
#include "run_internal.h"

#include <algorithm>
#include <cstdio>

namespace physics_arena
{
void SelectRayPreflightPixels(std::uint32_t view, std::uint32_t width, std::uint32_t height, std::vector<std::uint32_t>* pixels)
{
	using namespace benchmark_ray;
	const std::uint32_t count = std::min(4096u, width * height);
	pixels->resize(count);
	for (std::uint32_t index = 0; index < count; ++index)
		(*pixels)[index] = static_cast<std::uint32_t>(static_cast<std::uint64_t>(index) * width * height / count);
	if (view == 0 && width == kWidth && height == kHeight)
	{
		// retained native failures exercise silhouettes, mesh edges and normal comparisons
		constexpr std::uint32_t failures[] = {1043678, 1032899, 1410454, 1520024, 1564166, 1571446, 1595192, 1609697,
		                                      1726493, 1729622, 1810052, 2013121, 1999988, 2005914, 569221,  623325,
		                                      700959,  732424,  780942,  830281,  900802,  904227,  964112,  963647,
		                                      1006083, 1058022, 1055044, 1101674, 1125869};
		std::copy(std::begin(failures), std::end(failures), pixels->begin());
	}
	std::sort(pixels->begin(), pixels->end());
	pixels->erase(std::unique(pixels->begin(), pixels->end()), pixels->end());
}

benchmark_ray::Status PrepareRayPreflightCorpus(const std::filesystem::path& root, const CaseExecutionRayTracing& ray,
                                                const std::atomic<std::uint32_t>* cancellation)
{
	using namespace benchmark_ray;
	Status status = PrepareRayProbeCorpus(root);
	if (status != Status_Ok)
		return status;
	Scene scene = {};
	status = BuildHeavyRayScene(&scene, ray);
	if (status != Status_Ok || WriteRayScene(root / "scene.rtc", scene) != Status_Ok)
		return status != Status_Ok ? status : Status_Io;
	RayReference reference = {};
	status = BuildRayReference(scene, -1, &reference);
	if (status != Status_Ok)
		return status;
	reference.cancellation = cancellation;
	reference.audit = RayReferenceAudit_Preflight;
	for (std::uint32_t view = 0; view < ray.viewCount; ++view)
	{
		std::vector<std::uint32_t> pixels;
		SelectRayPreflightPixels(view, ray.width, ray.height, &pixels);
		CorpusPhase primary = {};
		RayCorpusDistribution distribution = {};
		status = BuildRayPrimarySelection(scene, reference, view, ray.width, ray.height, pixels, &primary, &distribution);
		if (status != Status_Ok)
			return status;
		for (std::uint32_t phaseIndex = 0; phaseIndex < Phase_Count; ++phaseIndex)
		{
			const Phase phase = static_cast<Phase>(phaseIndex);
			CorpusPhase secondary = {};
			const CorpusPhase* data = &primary;
			if (phase != Phase_Primary)
			{
				RayReference updated = {};
				const RayReference* selected = &reference;
				if (phase == Phase_Updated)
				{
					status = BuildRayReference(scene, static_cast<int>(view), &updated);
					if (status != Status_Ok)
						return status;
					updated.cancellation = cancellation;
					updated.audit = RayReferenceAudit_Preflight;
					selected = &updated;
				}
				distribution = {};
				status = BuildRaySecondary(scene, *selected, primary, phase, &secondary, &distribution);
				if (status != Status_Ok)
					return status;
				data = &secondary;
			}
			status = WriteRayPhase(PhasePath(root, view, phase), *data);
			if (status != Status_Ok)
				return status;
		}
	}
	return Status_Ok;
}

ArenaStatus RunRayPreflight(const wchar_t* repositoryRoot, const Catalog* catalog, const ReleaseCatalog* releaseCatalog,
                            const PreparedRunRequest* request, const std::filesystem::path& directory,
                            RunExecutionControl* control, InvocationContext* invocation, EventTransport* transport,
                            StatusRecord* error, std::vector<ExecutionFailure>* failures, const RunPathRecord* manifestPaths)
{
	std::vector<ExecutionFailure> localFailures;
	if (failures == nullptr)
		failures = &localFailures;
	if (EmitRunEvent(invocation, transport, "ray_preflight", "running", "bounded_native_queries_no_benchmark_score",
	                 error) != ArenaStatus_Ok)
		return error->code;
	const benchmark_ray::Status corpus =
	    PrepareRayPreflightCorpus(directory / "ray-corpus", request->configuration.execution.rayTracing, &control->cancellationRequested);
	if (corpus != benchmark_ray::Status_Ok)
		return RunError(error,
		                corpus == benchmark_ray::Status_Interrupted ? ArenaStatus_Interrupted : ArenaStatus_RunFailed,
		                "ray_preflight_corpus_failed");
	PreparedRunRequest diagnostic = *request;
	diagnostic.recordingMode = RecordingMode_Off;
	diagnostic.recordingThreads = {};
	diagnostic.repeatCount = 1;
	RunPathRecord paths = {};
	if (BuildRunChildPath(directory.c_str(), L"", &paths.resultDirectory) != ArenaStatus_Ok ||
	    BuildRunChildPath(directory.c_str(), L"raw", &paths.rawDirectory) != ArenaStatus_Ok ||
	    EnsureDirectory(paths.rawDirectory.data()) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_RunFailed, "ray_preflight_directory");
	for (std::uint32_t engine = 0; engine < request->engineCount; ++engine)
	{
		for (std::uint32_t thread = 0; thread < request->threadCount; ++thread)
		{
			std::array<wchar_t, kRunPathCapacity> threadDirectory = {};
			if (EnsureUnitDirectories(catalog, &diagnostic, &paths, engine, thread, &threadDirectory, error) !=
			    ArenaStatus_Ok)
				return error->code;
			RunExecutionResult result = {};
			RunProcessOutcome process = {};
			const ArenaStatus status =
			    RunOneProcess(repositoryRoot, catalog, releaseCatalog, &diagnostic, &paths, engine, thread, 0, control,
			                  invocation, &result, error, RayRunStage_Preflight, &process);
			const std::string_view id = CatalogTextView(catalog, catalog->engines[request->engineIndexes[engine]].id);
			char detail[512] = {};
			std::snprintf(detail, std::size(detail), "engine=%.*s threads=%u status=%d heavy_generation=not_started",
			              static_cast<int>(id.size()), id.data(), request->threadCounts[thread],
			              static_cast<int>(status));
			if (process.disposition == RunProcessDisposition_HarnessFailure || process.disposition == RunProcessDisposition_Interrupted)
				return status;
			if (process.disposition == RunProcessDisposition_EngineFailure)
			{
				for (std::uint32_t repeat = 0; repeat < request->repeatCount; ++repeat)
				{
					ExecutionFailure failure = {request->engineIndexes[engine], request->threadCounts[thread], repeat,
					                            ExecutionStage_Preflight, ExecutionOutcome_NotRun, process.reason,
					                            process.exitCodePresence, process.exitCode, error->detail};
					failures->push_back(failure);
				}
				if (manifestPaths != nullptr && PersistExecutionFailures(catalog, manifestPaths, *failures, error) != ArenaStatus_Ok)
					return error->code;
			}
			if (EmitRunEvent(invocation, transport, "ray_preflight", status == ArenaStatus_Ok ? "ok" : "failed", detail,
			                 error) != ArenaStatus_Ok)
				return error->code;
		}
	}
	*error = {};
	return ArenaStatus_Ok;
}
}
