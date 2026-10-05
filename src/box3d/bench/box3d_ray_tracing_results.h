#pragma once

#include "box3d_runner_args.h"
#include "ray_tracing.h"

namespace box3d_benchmark
{
struct Box3DRayPhaseResult
{
	benchmark_ray::Phase phase;
	benchmark_ray::Api api;
	const char* status;
	std::uint64_t queries;
	benchmark_ray::Validation validation;
	double queryMs, updateMs, conditioningMs, validationMs;
	std::uint64_t bufferBytes;
};
benchmark_ray::Status FlushBox3DRaySuite(std::ostream& output, const Box3DRunRequest& request, std::uint32_t view,
                                         std::uint32_t suite, const Box3DRayPhaseResult* rows, std::size_t count,
                                         double setupMs, double suiteMs);
benchmark_ray::Status WriteBox3DRayFailures(std::ofstream& output, const benchmark_ray::CorpusPhase& corpus,
                           const std::vector<benchmark_ray::Output>& outputs,
                           const std::vector<benchmark_ray::Hit>& hits, const benchmark_ray::Validation& validation);
}
