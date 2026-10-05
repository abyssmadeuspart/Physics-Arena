#pragma once

#include "physics_arena/ray_tracing_reference.h"
#include "case_execution_wire.h"

#include <span>

namespace physics_arena
{
int IsAuthoredRayCorpus(const CaseExecutionRayTracing& ray);
struct RayCorpusDistribution
{
	std::array<std::uint64_t, 5> shapeHits;
	std::array<std::uint64_t, 3> hullHits;
	std::uint64_t excludedNearest, fartherAccepted, endpointBands;
	std::uint64_t hits, misses, shortHits, longHits, rays, referenceEvents;
	std::uint64_t clearSecondary, blockedSecondary;
};
benchmark_ray::Status BuildHeavyRayScene(benchmark_ray::Scene* scene, const CaseExecutionRayTracing& ray);
benchmark_ray::Status BuildRayPrimarySelection(const benchmark_ray::Scene& scene, const RayReference& reference,
                                               std::uint32_t view, std::uint32_t width, std::uint32_t height, std::span<const std::uint32_t> pixels,
                                               benchmark_ray::CorpusPhase* output, RayCorpusDistribution* distribution);
benchmark_ray::Status BuildRayPrimary(const benchmark_ray::Scene& scene, const RayReference& reference,
                                      std::uint32_t view, std::uint32_t width, std::uint32_t height,
                                      benchmark_ray::CorpusPhase* output, RayCorpusDistribution* distribution);
benchmark_ray::Status BuildRaySecondary(const benchmark_ray::Scene& scene, const RayReference& reference,
                                        const benchmark_ray::CorpusPhase& primary, benchmark_ray::Phase phase,
                                        benchmark_ray::CorpusPhase* output, RayCorpusDistribution* distribution);
benchmark_ray::Status WriteRayScene(const std::filesystem::path& path, const benchmark_ray::Scene& scene);
benchmark_ray::Status WriteRayPhase(const std::filesystem::path& path, const benchmark_ray::CorpusPhase& phase);
benchmark_ray::Status AdmitHeavyRayCorpus(const std::filesystem::path& root, const CaseExecutionRayTracing& ray);
benchmark_ray::Status PrepareRayProbeCorpus(const std::filesystem::path& root);
using RayCorpusProgress = int (*)(void*, std::uint32_t, benchmark_ray::Phase);
benchmark_ray::Status PrepareHeavyRayCorpus(const std::filesystem::path& root, const CaseExecutionRayTracing& ray, RayCorpusDistribution* distribution,
                                            const std::atomic<std::uint32_t>* cancellation = nullptr,
                                            RayCorpusProgress progress = nullptr, void* context = nullptr);
}
