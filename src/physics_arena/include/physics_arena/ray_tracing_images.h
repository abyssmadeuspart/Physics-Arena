#pragma once

#include "physics_arena/bench_types.h"
#include "ray_tracing.h"
#include <atomic>

namespace physics_arena
{
enum RayImageChannel
{
	RayImageChannel_Shaded,
	RayImageChannel_Depth,
	RayImageChannel_Normals,
	RayImageChannel_Errors
};
struct RayImageChunk
{
	std::uint32_t view;
	benchmark_ray::Phase phase;
	benchmark_ray::Api api;
	std::uint32_t rayCount, hitCount, errors;
	std::uint64_t offset;
};
struct RayImageArchive
{
	void* file;
	void* decoder;
	std::uint64_t rawBytes;
	std::array<char, 32> engine;
	std::uint32_t threads, repeat, width, height;
	std::vector<RayImageChunk> chunks;
};
struct RayImage
{
	std::vector<std::uint32_t> rgba;
	std::uint32_t width, height;
	std::uint64_t queries, errors;
	benchmark_ray::Pose camera;
	PresenceStatus shadows, reflections, ambient;
};
std::filesystem::path RayImageTuplePath(const std::filesystem::path& result, std::string_view engine,
                                        std::uint32_t threads, std::uint32_t repeat);
ArenaStatus OpenRayImages(const std::filesystem::path& path, std::string_view engine, std::uint32_t threads,
                          std::uint32_t repeat, RayImageArchive* archive, StatusRecord* error);
void CloseRayImages(RayImageArchive* archive);
ArenaStatus ValidateRayImages(RayImageArchive* archive, StatusRecord* error);
ArenaStatus ReadRayImageChunk(RayImageArchive* archive, std::uint32_t view, benchmark_ray::Phase phase,
                              benchmark_ray::Api api, std::vector<benchmark_ray::Output>* outputs,
                              std::vector<benchmark_ray::Hit>* hits, StatusRecord* error);
ArenaStatus CompressRayImages(const std::filesystem::path& source, const std::filesystem::path& target,
                              std::string_view engine, std::uint32_t threads, std::uint32_t repeat,
                              const std::atomic<std::uint32_t>* cancellation, StatusRecord* error);
ArenaStatus ComposeRayImage(const std::filesystem::path& corpus, RayImageArchive* archive, std::uint32_t view,
                            benchmark_ray::Phase phase, benchmark_ray::Api api, RayImageChannel channel,
                            RayImage* image, StatusRecord* error);
}
