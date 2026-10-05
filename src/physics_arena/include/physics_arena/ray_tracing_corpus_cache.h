#pragma once

#include "physics_arena/ray_tracing_corpus.h"

namespace physics_arena
{
enum RayCorpusAcquisition
{
	RayCorpusAcquisition_Generated,
	RayCorpusAcquisition_Reused
};
void RayCorpusFileNames(std::vector<std::filesystem::path>* names, std::uint32_t views);
std::filesystem::path RayCorpusCachePath(const std::filesystem::path& repositoryRoot, const CaseExecutionRayTracing& ray);
benchmark_ray::Status FindReusableRayCorpus(const std::filesystem::path& repositoryRoot, const CaseExecutionRayTracing& ray,
                                            std::filesystem::path* source);
benchmark_ray::Status CopyAdmittedRayCorpus(const std::filesystem::path& source,
                                            const std::filesystem::path& destination, const CaseExecutionRayTracing& ray,
                                            const std::atomic<std::uint32_t>* cancellation);
benchmark_ray::Status AcquireHeavyRayCorpus(const std::filesystem::path& repositoryRoot,
                                            const std::filesystem::path& destination, const CaseExecutionRayTracing& ray,
                                            const std::atomic<std::uint32_t>* cancellation, RayCorpusProgress progress,
                                            void* context, RayCorpusAcquisition* acquisition,
                                            std::filesystem::path* source);
}
