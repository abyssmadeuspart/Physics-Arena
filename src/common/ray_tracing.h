#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace benchmark_ray
{
constexpr std::uint32_t kRecipeRevision = 1;
constexpr std::uint32_t kViewCount = 6;
constexpr std::uint32_t kWidth = 1920;
constexpr std::uint32_t kHeight = 1080;
constexpr std::uint64_t kActiveByteLimit = 2ull * 1024 * 1024 * 1024;
constexpr std::uint64_t kCorpusByteLimit = 12ull * 1024 * 1024 * 1024;
constexpr std::uint32_t kNoSource = UINT32_MAX;

enum Shape : std::uint32_t
{
	Shape_Box,
	Shape_Sphere,
	Shape_Capsule,
	Shape_Hull,
	Shape_Mesh
};
enum Phase : std::uint32_t
{
	Phase_Primary,
	Phase_Shuffled,
	Phase_Shadow,
	Phase_Reflection,
	Phase_Ambient,
	Phase_Filtered,
	Phase_ColliderHits,
	Phase_MeshHits,
	Phase_Updated,
	Phase_Count
};
enum Probe : std::uint32_t
{
	Probe_Inside,
	Probe_Backfaces,
	Probe_Surface,
	Probe_Range,
	Probe_Grazing,
	Probe_FarOrigin,
	Probe_Overflow,
	Probe_Concurrent,
	Probe_Count
};
inline const char* ProbeName(Probe probe)
{
	constexpr const char* names[] = {"inside-origin", "mesh-backfaces", "surface-start",       "range-endpoints",
	                                 "grazing",       "far-origin",     "enumerator-overflow", "concurrent-read"};
	return probe < Probe_Count ? names[probe] : "invalid";
}
enum Api : std::uint32_t
{
	Api_Ordinary,
	Api_NativeBatch,
	Api_Count
};
enum Status : std::uint32_t
{
	Status_Ok,
	Status_Invalid,
	Status_Capacity,
	Status_Io,
	Status_Unsupported,
	Status_Failed,
	Status_Interrupted
};
enum HitFlags : std::uint32_t
{
	HitFlags_None = 0,
	HitFlags_Edge = 1,
	HitFlags_Tie = 2
};

struct Vector
{
	float x, y, z;
};
struct Quaternion
{
	float x, y, z, w;
};
struct Pose
{
	Vector position;
	Quaternion rotation;
};
struct Collider
{
	Shape shape;
	std::uint32_t category;
	std::uint32_t moving;
	std::uint32_t hull;
	Pose pose;
	Vector size;
	std::uint32_t firstTriangle;
	std::uint32_t triangleCount;
};
struct Triangle
{
	Vector a, b, c;
};
struct Hull
{
	std::uint32_t firstVertex, vertexCount;
};
struct Ray
{
	Vector origin;
	Vector translation;
	float length;
	std::uint32_t mask;
	std::uint32_t pixel;
};
struct Hit
{
	double distance;
	Vector normal;
	std::uint32_t collider;
	std::uint32_t source;
	std::uint32_t flags;
};
struct Range
{
	std::uint32_t first, count;
};
struct Output
{
	std::uint32_t first, count, written;
	Status status;
};
struct Scene
{
	std::vector<Collider> colliders;
	std::vector<Triangle> triangles;
	std::vector<Hull> hulls;
	std::vector<Vector> vertices;
	std::array<Pose, kViewCount> cameras;
	Vector light;
};
struct CorpusPhase
{
	std::uint32_t view;
	Phase phase;
	std::uint32_t width;
	std::uint32_t height;
	std::uint32_t nativeEventCapacity;
	std::vector<Ray> rays;
	std::vector<Range> expected;
	std::vector<Hit> hits;
};

static_assert(sizeof(Vector) == 12 && sizeof(Pose) == 28 && sizeof(Collider) == 64);
static_assert(sizeof(Triangle) == 36 && sizeof(Ray) == 36 && sizeof(Hit) == 32);
static_assert(sizeof(Range) == 8 && sizeof(Output) == 16);

inline int IsAny(Phase phase)
{
	return phase == Phase_Shadow || phase == Phase_Ambient;
}
inline int IsEnumeration(Phase phase)
{
	return phase == Phase_ColliderHits || phase == Phase_MeshHits;
}
inline const char* PhaseName(Phase phase)
{
	constexpr const char* names[] = {"primary-coherent", "primary-shuffled",  "shadows",
	                                 "reflections",      "ambient-occlusion", "filtered-closest",
	                                 "collider_hits",    "mesh_surface_hits", "updated-primary"};
	return phase < Phase_Count ? names[phase] : "invalid";
}
inline const char* ApiName(Api api)
{
	return api == Api_Ordinary ? "ordinary" : "native-batch";
}
inline std::filesystem::path PhasePath(const std::filesystem::path& root, std::uint32_t view, Phase phase)
{
	return root / ("view-" + std::to_string(view) + "-" + PhaseName(phase) + ".rtr");
}

template <typename T>
inline Status ReadArray(std::ifstream& input, std::uint64_t count, std::uint64_t* remaining, std::vector<T>* output)
{
	if (count > *remaining / sizeof(T) || count > kActiveByteLimit / sizeof(T))
		return Status_Capacity;
	output->resize(static_cast<std::size_t>(count));
	input.read(reinterpret_cast<char*>(output->data()), static_cast<std::streamsize>(count * sizeof(T)));
	*remaining -= count * sizeof(T);
	return input ? Status_Ok : Status_Io;
}

inline int FiniteVector(Vector value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
inline int ValidPose(const Pose& pose)
{
	const Quaternion q = pose.rotation;
	const double norm = static_cast<double>(q.x) * q.x + static_cast<double>(q.y) * q.y +
	                    static_cast<double>(q.z) * q.z + static_cast<double>(q.w) * q.w;
	return FiniteVector(pose.position) && std::isfinite(norm) && std::abs(norm - 1) < 1e-5;
}
inline int FiniteHit(const Hit& hit)
{
	return hit.collider > 0 && hit.collider <= 66560 && std::isfinite(hit.distance) && hit.distance >= 0 &&
	       FiniteVector(hit.normal);
}

inline Status ReadSceneHeader(std::ifstream& input, std::array<std::uint32_t, 4>* counts)
{
	input.seekg(0, std::ios::end);
	const std::streamoff size = input.tellg();
	if (size < 204 || static_cast<std::uint64_t>(size) > kActiveByteLimit)
		return Status_Invalid;
	input.seekg(0);
	std::array<std::uint32_t, 6> header = {};
	input.read(reinterpret_cast<char*>(header.data()), sizeof(header));
	const std::uint64_t expected = 204 + static_cast<std::uint64_t>(header[2]) * sizeof(Collider) +
	                               static_cast<std::uint64_t>(header[3]) * sizeof(Triangle) +
	                               static_cast<std::uint64_t>(header[4]) * sizeof(Hull) +
	                               static_cast<std::uint64_t>(header[5]) * sizeof(Vector);
	if (!input || header[0] != 0x43545242 || header[1] != kRecipeRevision || header[2] > 66560 || header[3] > 1048576 ||
	    header[4] > 3 || header[5] > 96 || expected != static_cast<std::uint64_t>(size))
		return Status_Invalid;
	*counts = {header[2], header[3], header[4], header[5]};
	return Status_Ok;
}

inline Status ReadSceneMetadata(const std::filesystem::path& path, std::array<std::uint32_t, 4>* counts)
{
	std::ifstream input(path, std::ios::binary);
	return input ? ReadSceneHeader(input, counts) : Status_Io;
}

inline Status ReadScene(const std::filesystem::path& path, Scene* scene)
{
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input)
		return Status_Io;
	std::array<std::uint32_t, 4> counts = {};
	const Status status = ReadSceneHeader(input, &counts);
	if (status != Status_Ok)
		return status;
	std::uint64_t remaining = static_cast<std::uint64_t>(counts[0]) * sizeof(Collider) +
	                          static_cast<std::uint64_t>(counts[1]) * sizeof(Triangle) +
	                          static_cast<std::uint64_t>(counts[2]) * sizeof(Hull) +
	                          static_cast<std::uint64_t>(counts[3]) * sizeof(Vector);
	if (ReadArray(input, counts[0], &remaining, &scene->colliders) != Status_Ok ||
	    ReadArray(input, counts[1], &remaining, &scene->triangles) != Status_Ok ||
	    ReadArray(input, counts[2], &remaining, &scene->hulls) != Status_Ok ||
	    ReadArray(input, counts[3], &remaining, &scene->vertices) != Status_Ok)
		return Status_Invalid;
	input.read(reinterpret_cast<char*>(scene->cameras.data()), sizeof(scene->cameras));
	input.read(reinterpret_cast<char*>(&scene->light), sizeof(Vector));
	if (!input)
		return Status_Io;
	if (!FiniteVector(scene->light))
		return Status_Invalid;
	for (const Pose& camera : scene->cameras)
		if (!ValidPose(camera))
			return Status_Invalid;
	for (const Hull& hull : scene->hulls)
		if (hull.vertexCount < 4 || hull.vertexCount > 64 || hull.firstVertex > scene->vertices.size() ||
		    hull.vertexCount > scene->vertices.size() - hull.firstVertex)
			return Status_Invalid;
	for (const Vector vertex : scene->vertices)
		if (!FiniteVector(vertex))
			return Status_Invalid;
	for (const Triangle& triangle : scene->triangles)
		if (!FiniteVector(triangle.a) || !FiniteVector(triangle.b) || !FiniteVector(triangle.c))
			return Status_Invalid;
	for (const Collider& collider : scene->colliders)
		if (collider.shape > Shape_Mesh || collider.category == 0 || collider.moving > 1 || !ValidPose(collider.pose) ||
		    !FiniteVector(collider.size) || (collider.shape != Shape_Mesh && collider.size.x <= 0) ||
		    ((collider.shape == Shape_Box || collider.shape == Shape_Hull) &&
		     (collider.size.y <= 0 || collider.size.z <= 0)) ||
		    (collider.shape == Shape_Capsule && collider.size.y < 0) ||
		    (collider.shape == Shape_Hull && collider.hull >= scene->hulls.size()) ||
		    collider.firstTriangle > scene->triangles.size() ||
		    collider.triangleCount > scene->triangles.size() - collider.firstTriangle ||
		    (collider.shape == Shape_Mesh && (collider.triangleCount == 0 || collider.moving != 0)))
			return Status_Invalid;
	return Status_Ok;
}

struct PhaseMetadata
{
	std::uint32_t view;
	Phase phase;
	std::uint32_t width, height, rays, hits, nativeEvents;
};
struct PhaseCapacity
{
	std::uint64_t rays, referenceHits, outputHits, batchRays, nativeEvents;
};

inline Status ReadPhaseHeader(std::ifstream& input, PhaseMetadata* metadata)
{
	input.seekg(0, std::ios::end);
	const std::streamoff size = input.tellg();
	if (size < 40 || static_cast<std::uint64_t>(size) > kActiveByteLimit)
		return Status_Invalid;
	input.seekg(0);
	std::array<std::uint32_t, 10> header = {};
	input.read(reinterpret_cast<char*>(header.data()), sizeof(header));
	const std::uint64_t expected = sizeof(header) +
	                               static_cast<std::uint64_t>(header[6]) * (sizeof(Ray) + sizeof(Range)) +
	                               static_cast<std::uint64_t>(header[7]) * sizeof(Hit);
	if (!input || header[0] != 0x52545242 || header[1] != kRecipeRevision || header[2] >= kViewCount ||
	    header[3] >= Phase_Count || header[4] == 0 || header[4] > kWidth || header[5] == 0 || header[5] > kHeight ||
	    header[6] > kWidth * kHeight || header[8] == 0 || header[9] != 0 ||
	    expected != static_cast<std::uint64_t>(size))
		return Status_Invalid;
	*metadata = {header[2], static_cast<Phase>(header[3]), header[4], header[5], header[6], header[7], header[8]};
	return Status_Ok;
}

inline Status ReadPhaseMetadata(const std::filesystem::path& path, PhaseMetadata* metadata)
{
	std::ifstream input(path, std::ios::binary);
	return input ? ReadPhaseHeader(input, metadata) : Status_Io;
}

inline void IncludePhaseCapacity(const PhaseMetadata& metadata, PhaseCapacity* capacity)
{
	capacity->rays = std::max(capacity->rays, static_cast<std::uint64_t>(metadata.rays));
	capacity->referenceHits = std::max(capacity->referenceHits, static_cast<std::uint64_t>(metadata.hits));
	const std::uint64_t outputHits = IsEnumeration(metadata.phase) != 0
	                                     ? static_cast<std::uint64_t>(metadata.hits) + 4ull * metadata.rays
	                                     : metadata.rays;
	capacity->outputHits = std::max(capacity->outputHits, outputHits);
	if (metadata.phase == Phase_Primary || metadata.phase == Phase_Shuffled || metadata.phase == Phase_Reflection ||
	    metadata.phase == Phase_Updated)
		capacity->batchRays = std::max(capacity->batchRays, static_cast<std::uint64_t>(metadata.rays));
	capacity->nativeEvents = std::max(capacity->nativeEvents, static_cast<std::uint64_t>(metadata.nativeEvents));
}

inline std::uint64_t PhaseStorageBytes(const CorpusPhase& phase, const std::vector<Output>& outputs,
                                       const std::vector<Hit>& hits)
{
	return phase.rays.capacity() * sizeof(Ray) + phase.expected.capacity() * sizeof(Range) +
	       phase.hits.capacity() * sizeof(Hit) + outputs.capacity() * sizeof(Output) + hits.capacity() * sizeof(Hit);
}

inline Status ReservePhaseStorage(const PhaseCapacity& capacity, CorpusPhase* phase, std::vector<Output>* outputs,
                                  std::vector<Hit>* hits)
{
	const std::uint64_t bytes = capacity.rays * (sizeof(Ray) + sizeof(Range) + sizeof(Output)) +
	                            (capacity.referenceHits + capacity.outputHits) * sizeof(Hit);
	if (capacity.outputHits > UINT32_MAX || bytes > kActiveByteLimit)
		return Status_Capacity;
	phase->rays.reserve(static_cast<std::size_t>(capacity.rays));
	phase->expected.reserve(static_cast<std::size_t>(capacity.rays));
	phase->hits.reserve(static_cast<std::size_t>(capacity.referenceHits));
	outputs->reserve(static_cast<std::size_t>(capacity.rays));
	hits->reserve(static_cast<std::size_t>(capacity.outputHits));
	return PhaseStorageBytes(*phase, *outputs, *hits) <= kActiveByteLimit ? Status_Ok : Status_Capacity;
}

inline Status PreparePhaseOutputs(const CorpusPhase& phase, std::vector<Output>* outputs, std::vector<Hit>* hits)
{
	std::uint64_t count = 0;
	for (const Range range : phase.expected)
		count += IsEnumeration(phase.phase) != 0 ? static_cast<std::uint64_t>(range.count) + 4 : 1;
	if (phase.rays.size() > outputs->capacity() || count > hits->capacity() || count > UINT32_MAX)
		return Status_Capacity;
	outputs->resize(phase.rays.size());
	hits->resize(static_cast<std::size_t>(count));
	std::uint32_t offset = 0;
	for (std::size_t index = 0; index < outputs->size(); ++index)
	{
		(*outputs)[index] = {offset, 0, 0, Status_Ok};
		offset += IsEnumeration(phase.phase) != 0 ? phase.expected[index].count + 4 : 1;
	}
	return Status_Ok;
}

inline Status ReadPhase(const std::filesystem::path& path, CorpusPhase* phase, const PhaseCapacity* admitted = nullptr)
{
	std::ifstream input(path, std::ios::binary);
	if (!input)
		return Status_Io;
	PhaseMetadata metadata = {};
	const Status status = ReadPhaseHeader(input, &metadata);
	if (status != Status_Ok)
		return status;
	if (admitted != nullptr && (metadata.rays > admitted->rays || metadata.hits > admitted->referenceHits ||
	                            metadata.nativeEvents > admitted->nativeEvents))
		return Status_Capacity;
	phase->view = metadata.view;
	phase->phase = metadata.phase;
	phase->width = metadata.width;
	phase->height = metadata.height;
	phase->nativeEventCapacity = metadata.nativeEvents;
	std::uint64_t remaining = static_cast<std::uint64_t>(metadata.rays) * (sizeof(Ray) + sizeof(Range)) +
	                          static_cast<std::uint64_t>(metadata.hits) * sizeof(Hit);
	if (ReadArray(input, metadata.rays, &remaining, &phase->rays) != Status_Ok ||
	    ReadArray(input, metadata.rays, &remaining, &phase->expected) != Status_Ok ||
	    ReadArray(input, metadata.hits, &remaining, &phase->hits) != Status_Ok)
		return Status_Invalid;
	for (const Ray& ray : phase->rays)
	{
		const double length = std::sqrt(static_cast<double>(ray.translation.x) * ray.translation.x +
		                                static_cast<double>(ray.translation.y) * ray.translation.y +
		                                static_cast<double>(ray.translation.z) * ray.translation.z);
		if (!FiniteVector(ray.origin) || !FiniteVector(ray.translation) || !std::isfinite(ray.length) ||
		    ray.length <= 0 || std::abs(length - ray.length) > 1e-5 * std::max(1.0, length) || ray.mask == 0 ||
		    ray.pixel >= phase->width * phase->height)
			return Status_Invalid;
	}
	for (const Hit& hit : phase->hits)
		if (!FiniteHit(hit))
			return Status_Invalid;
	for (const Range range : phase->expected)
		if (range.first > phase->hits.size() || range.count > phase->hits.size() - range.first)
			return Status_Invalid;
	return Status_Ok;
}

inline Pose UpdatedPose(const Collider& collider, std::uint32_t view)
{
	Pose pose = collider.pose;
	if (collider.moving != 0)
	{
		pose.position.x += 0.17f * static_cast<float>(1 + view);
		pose.position.y += 0.11f * static_cast<float>((view % 3) + 1);
		pose.position.z -= 0.13f * static_cast<float>((view % 2) + 1);
	}
	return pose;
}

struct Validation
{
	std::uint64_t errors, hitRays, written;
	std::array<std::uint32_t, 16> failingRays;
	std::uint32_t failingCount;
};

inline int MatchingHit(const Hit& actual, const Hit& expected)
{
	if (actual.collider != expected.collider || actual.source != expected.source || !std::isfinite(actual.distance) ||
	    std::abs(actual.distance - expected.distance) > 1e-4 + 1e-5 * std::max(1.0, expected.distance))
		return 0;
	const double length = static_cast<double>(actual.normal.x) * actual.normal.x +
	                      static_cast<double>(actual.normal.y) * actual.normal.y +
	                      static_cast<double>(actual.normal.z) * actual.normal.z;
	const double dot = static_cast<double>(actual.normal.x) * expected.normal.x +
	                   static_cast<double>(actual.normal.y) * expected.normal.y +
	                   static_cast<double>(actual.normal.z) * expected.normal.z;
	const double expectedLength = static_cast<double>(expected.normal.x) * expected.normal.x +
	                              static_cast<double>(expected.normal.y) * expected.normal.y +
	                              static_cast<double>(expected.normal.z) * expected.normal.z;
	return std::isfinite(length) && std::abs(length - 1.0) < 1e-4 &&
	       dot >= 0.9999984769132877 * std::sqrt(length * expectedLength);
}

inline Validation ValidateOutputs(const CorpusPhase& phase, const std::vector<Output>& outputs,
                                  const std::vector<Hit>& hits)
{
	Validation validation = {};
	if (outputs.size() != phase.rays.size())
	{
		validation.errors = 1;
		return validation;
	}
	for (std::size_t index = 0; index < outputs.size(); ++index)
	{
		const Output& output = outputs[index];
		const Range range = phase.expected[index];
		int valid = output.written == 1 && output.status == Status_Ok && output.first <= hits.size() &&
		            output.count <= hits.size() - output.first;
		validation.written += output.written == 1;
		validation.hitRays += output.count != 0;
		if (valid != 0 && IsAny(phase.phase) != 0)
			valid = output.count == (range.count != 0 ? 1u : 0u);
		else if (valid != 0 && IsEnumeration(phase.phase) == 0)
		{
			valid = output.count == (range.count != 0 ? 1u : 0u);
			if (valid != 0 && range.count != 0)
			{
				valid = 0;
				for (std::uint32_t candidate = 0; candidate < range.count; ++candidate)
					valid |= MatchingHit(hits[output.first], phase.hits[range.first + candidate]);
			}
		}
		else if (valid != 0)
		{
			std::uint32_t expectedCount = 0;
			for (std::uint32_t candidate = 0; candidate < range.count; ++candidate)
			{
				const Hit& expected = phase.hits[range.first + candidate];
				int unique = 1;
				for (std::uint32_t prior = 0; prior < candidate; ++prior)
				{
					const Hit& previous = phase.hits[range.first + prior];
					if (previous.collider == expected.collider &&
					    (phase.phase == Phase_ColliderHits || previous.source == expected.source))
						unique = 0;
				}
				expectedCount += unique;
			}
			valid = output.count == expectedCount;
			for (std::uint32_t event = 0; event < output.count && valid != 0; ++event)
			{
				int matched = 0;
				for (std::uint32_t candidate = 0; candidate < range.count; ++candidate)
					matched |= MatchingHit(hits[output.first + event], phase.hits[range.first + candidate]);
				for (std::uint32_t prior = 0; prior < event; ++prior)
					if (hits[output.first + prior].collider == hits[output.first + event].collider &&
					    (phase.phase == Phase_ColliderHits ||
					     hits[output.first + prior].source == hits[output.first + event].source))
						matched = 0;
				valid &= matched;
			}
		}
		if (valid == 0)
		{
			validation.errors += 1;
			if (validation.failingCount < validation.failingRays.size())
				validation.failingRays[validation.failingCount++] = static_cast<std::uint32_t>(index);
		}
	}
	return validation;
}

}
