#pragma once

#include "physics_arena/ray_tracing_corpus.h"
#include "physics_arena/run.h"

namespace physics_arena
{
void SelectRayPreflightPixels(std::uint32_t view, std::uint32_t width, std::uint32_t height, std::vector<std::uint32_t>* pixels);
benchmark_ray::Status PrepareRayPreflightCorpus(const std::filesystem::path& root, const CaseExecutionRayTracing& ray,
                                                const std::atomic<std::uint32_t>* cancellation);
ArenaStatus RunRayPreflight(const wchar_t* repositoryRoot, const Catalog* catalog, const ReleaseCatalog* releaseCatalog,
                            const PreparedRunRequest* request, const std::filesystem::path& directory,
                            RunExecutionControl* control, InvocationContext* invocation, EventTransport* transport,
                            StatusRecord* error, std::vector<ExecutionFailure>* failures = nullptr,
                            const RunPathRecord* manifestPaths = nullptr);
}
