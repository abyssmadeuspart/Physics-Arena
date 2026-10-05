#pragma once

#include "box3d_runner_args.h"
#include "ray_tracing.h"

namespace box3d_benchmark
{
struct Box3DRayRecording
{
	std::ofstream output;
	std::filesystem::path path, partial;
	std::array<std::uint32_t, 16> header;
};
benchmark_ray::Status BeginBox3DRayRecording(Box3DRayRecording* recording, const Box3DRunRequest& request);
benchmark_ray::Status AppendBox3DRayRecording(Box3DRayRecording* recording, const benchmark_ray::CorpusPhase& corpus,
                                              const std::vector<benchmark_ray::Output>& outputs,
                                              const std::vector<benchmark_ray::Hit>& hits, std::uint32_t suite);
benchmark_ray::Status CompleteBox3DRayRecording(Box3DRayRecording* recording);
}
