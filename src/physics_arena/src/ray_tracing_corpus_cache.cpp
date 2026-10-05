#include "physics_arena/ray_tracing_corpus_cache.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace physics_arena
{
void RayCorpusFileNames(std::vector<std::filesystem::path>* names, std::uint32_t views)
{
	using namespace benchmark_ray;
	names->clear();
	names->reserve(106);
	for (const char* name : {"scene.rtc", "probe-scene.rtc", "view-distribution.csv", "distribution.csv"})
		names->emplace_back(name);
	for (std::uint32_t view = 0; view < kViewCount; ++view)
	{
		for (std::uint32_t phase = 0; view < views && phase < Phase_Count; ++phase)
			names->push_back(PhasePath({}, view, static_cast<Phase>(phase)).filename());
		for (std::uint32_t probe = 0; probe < Probe_Count; ++probe)
			names->emplace_back("probe-view-" + std::to_string(view) + "-" + ProbeName(static_cast<Probe>(probe)) +
			                    ".rtr");
	}
}

benchmark_ray::Status CopyAdmittedRayCorpus(const std::filesystem::path& source,
                                            const std::filesystem::path& destination, const CaseExecutionRayTracing& ray,
                                            const std::atomic<std::uint32_t>* cancellation)
{
	using namespace benchmark_ray;
	std::error_code error;
	if (std::filesystem::exists(destination, error) || error)
		return Status_Invalid;
	const std::filesystem::path staging = destination.wstring() + L".partial";
	if (std::filesystem::exists(staging, error) || error)
		return Status_Invalid;
	std::filesystem::create_directories(staging, error);
	if (error)
		return Status_Io;
	std::vector<std::filesystem::path> names;
	RayCorpusFileNames(&names, ray.viewCount);
	for (const std::filesystem::path& name : names)
	{
		if (cancellation != nullptr && cancellation->load(std::memory_order_acquire) != 0)
			return Status_Interrupted;
		const std::filesystem::path input = source / name;
		const DWORD attributes = GetFileAttributesW(input.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES ||
		    (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
			return Status_Invalid;
		std::filesystem::copy_file(input, staging / name, std::filesystem::copy_options::none, error);
		if (error)
			return Status_Io;
	}
	std::filesystem::rename(staging, destination, error);
	return error ? Status_Io : Status_Ok;
}

std::filesystem::path RayCorpusCachePath(const std::filesystem::path& repositoryRoot, const CaseExecutionRayTracing& ray)
{
	constexpr std::uint32_t referenceGeneration = 1;
	std::string key = "heavy-" + std::to_string(ray.recipeRevision) + "-reference-" + std::to_string(referenceGeneration);
	if (IsAuthoredRayCorpus(ray) == 0)
		for (const std::uint32_t value : {ray.width, ray.height, ray.viewCount, ray.primitiveCount, ray.meshCount,
		    ray.trianglesPerMesh, ray.movingCount, ray.seedLow, ray.seedHigh})
			key += "-" + std::to_string(value);
	return repositoryRoot / ".build/ray-corpus" / key;
}

benchmark_ray::Status FindReusableRayCorpus(const std::filesystem::path& repositoryRoot, const CaseExecutionRayTracing& ray,
                                            std::filesystem::path* source)
{
	using namespace benchmark_ray;
	source->clear();
	std::error_code error;
	const std::filesystem::path cache = RayCorpusCachePath(repositoryRoot, ray);
	if (std::filesystem::exists(cache, error))
	{
		if ((GetFileAttributesW(cache.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
		    AdmitHeavyRayCorpus(cache, ray) != Status_Ok)
			return Status_Invalid;
		*source = cache;
	}
	else if (error)
		return Status_Io;
	return Status_Ok;
}

benchmark_ray::Status AcquireHeavyRayCorpus(const std::filesystem::path& repositoryRoot,
                                            const std::filesystem::path& destination, const CaseExecutionRayTracing& ray,
                                            const std::atomic<std::uint32_t>* cancellation, RayCorpusProgress progress,
                                            void* context, RayCorpusAcquisition* acquisition,
                                            std::filesystem::path* source)
{
	using namespace benchmark_ray;
	*acquisition = RayCorpusAcquisition_Generated;
	const Status selection = FindReusableRayCorpus(repositoryRoot, ray, source);
	if (selection != Status_Ok)
		return selection;
	std::error_code error;
	const std::filesystem::path cache = RayCorpusCachePath(repositoryRoot, ray);
	Status status = Status_Ok;
	if (!source->empty())
	{
		*acquisition = RayCorpusAcquisition_Reused;
		status = CopyAdmittedRayCorpus(*source, destination, ray, cancellation);
	}
	else
	{
		RayCorpusDistribution distribution = {};
		status = PrepareHeavyRayCorpus(destination, ray, &distribution, cancellation, progress, context);
		*source = destination;
	}
	if (status != Status_Ok)
		return status;
	if (AdmitHeavyRayCorpus(destination, ray) != Status_Ok)
		return Status_Invalid;
	if (!std::filesystem::exists(cache, error))
		return error ? Status_Io : CopyAdmittedRayCorpus(destination, cache, ray, cancellation);
	return error ? Status_Io : Status_Ok;
}
}
