#include "physics_arena/ray_tracing_corpus.h"
#include "physics_arena/csv_io.h"
#include <charconv>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace physics_arena
{
namespace
{
using namespace benchmark_ray;
constexpr double kPi = 3.14159265358979323846;
constexpr std::uint64_t kSeed = 0x42504f4c59524159ull;
std::uint64_t Next(std::uint64_t* state)
{
	std::uint64_t z = (*state += 0x9e3779b97f4a7c15ull);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
	return z ^ (z >> 31);
}
float Random(std::uint64_t* state)
{
	return static_cast<float>(Next(state) >> 40) / 16777216.0f;
}
Vector Add(Vector a, Vector b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vector Sub(Vector a, Vector b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vector Mul(Vector a, float s)
{
	return {a.x * s, a.y * s, a.z * s};
}
float Dot(Vector a, Vector b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vector Cross(Vector a, Vector b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vector Unit(Vector v)
{
	return Mul(v, 1.0f / std::sqrt(Dot(v, v)));
}
Vector Rotate(Quaternion q, Vector v)
{
	const Vector axis = {q.x, q.y, q.z};
	const Vector t = Mul(Cross(axis, v), 2);
	return Add(v, Add(Mul(t, q.w), Cross(axis, t)));
}
Pose Camera(Vector eye, float yaw, float pitch)
{
	const float sy = std::sin(yaw * 0.5f), cy = std::cos(yaw * 0.5f);
	const float sx = std::sin(pitch * 0.5f), cx = std::cos(pitch * 0.5f);
	return {eye, {cy * sx, sy * cx, -sy * sx, cy * cx}};
}
Ray Segment(Vector origin, Vector direction, float length, std::uint32_t pixel, std::uint32_t mask = 15)
{
	const Vector translation = Mul(Unit(direction), length);
	return {origin, translation, std::sqrt(Dot(translation, translation)), mask, pixel};
}
void AddQuad(Scene* scene, Vector a, Vector b, Vector c, Vector d)
{
	scene->triangles.push_back({a, b, c});
	scene->triangles.push_back({a, c, d});
}
void BuildHulls(Scene* scene)
{
	scene->hulls.push_back({0, 8});
	for (std::uint32_t index = 0; index < 8; ++index)
		scene->vertices.push_back(
		    {(index & 1) != 0 ? 1.f : -1.f, (index & 2) != 0 ? 1.f : -1.f, (index & 4) != 0 ? 1.f : -1.f});
	scene->hulls.push_back({8, 24});
	for (std::uint32_t axis = 0; axis < 3; ++axis)
		for (std::uint32_t index = 0; index < 8; ++index)
		{
			Vector p = scene->vertices[index];
			if (axis == 0)
				p.x *= .5f;
			if (axis == 1)
				p.y *= .5f;
			if (axis == 2)
				p.z *= .5f;
			scene->vertices.push_back(p);
		}
	scene->hulls.push_back({32, 64});
	for (std::uint32_t layer = 0; layer < 2; ++layer)
		for (std::uint32_t index = 0; index < 32; ++index)
		{
			const double angle = 2 * kPi * static_cast<double>(index) / 32;
			scene->vertices.push_back(
			    {static_cast<float>(std::cos(angle)), layer == 0 ? -1.f : 1.f, static_cast<float>(std::sin(angle))});
		}
}
void BuildTile(Scene* scene, std::uint32_t tile, std::uint32_t triangles)
{
	Collider collider = {};
	collider.shape = Shape_Mesh;
	collider.category = 1u << (tile % 4);
	collider.pose = {{-80.f + 5.f * static_cast<float>(tile % 32), 0, -80.f + 5.f * static_cast<float>(tile / 32)},
	                 {0, 0, 0, 1}};
	collider.firstTriangle = static_cast<std::uint32_t>(scene->triangles.size());
	for (std::uint32_t z = 0; z < 16; ++z)
		for (std::uint32_t x = 0; x < 16; ++x)
		{
			const float x0 = static_cast<float>(x) * .3125f, z0 = static_cast<float>(z) * .3125f;
			const float x1 = x0 + .3125f, z1 = z0 + .3125f;
			const float a = .12f * std::sin(static_cast<float>(x + tile) * .7f) * std::cos(static_cast<float>(z) * .5f);
			const float b =
			    .12f * std::sin(static_cast<float>(x + 1 + tile) * .7f) * std::cos(static_cast<float>(z) * .5f);
			const float c =
			    .12f * std::sin(static_cast<float>(x + 1 + tile) * .7f) * std::cos(static_cast<float>(z + 1) * .5f);
			const float d =
			    .12f * std::sin(static_cast<float>(x + tile) * .7f) * std::cos(static_cast<float>(z + 1) * .5f);
			AddQuad(scene, {x0, a, z0}, {x0, d, z1}, {x1, c, z1}, {x1, b, z0});
		}
	for (std::uint32_t row = 0; row < 8; ++row)
		for (std::uint32_t column = 0; column < 32; ++column)
		{
			const float x0 = .5f + .125f * static_cast<float>(column), x1 = x0 + .125f;
			const float y0 = .25f + .125f * static_cast<float>(row), y1 = y0 + .125f;
			const float z0 = 1.25f + .15f * std::sin(static_cast<float>(column) * .6f);
			const float z1 = 1.25f + .15f * std::sin(static_cast<float>(column + 1) * .6f);
			if ((tile & 1) == 0)
				AddQuad(scene, {x0, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z0});
			else
				AddQuad(scene, {z0, y0, x0}, {z0, y1, x0}, {z1, y1, x1}, {z1, y0, x1});
		}
	scene->triangles.resize(collider.firstTriangle + triangles);
	collider.triangleCount = triangles;
	scene->colliders.push_back(collider);
}
Status ReferencePhase(const Scene& scene, const RayReference& reference, CorpusPhase* output,
                      RayCorpusDistribution* distribution)
{
	std::vector<Hit> hits;
	std::vector<Hit> brute;
	output->expected.reserve(output->rays.size());
	output->nativeEventCapacity = 1;
	for (std::size_t index = 0; index < output->rays.size(); ++index)
	{
		if ((index & 4095) == 0 && reference.cancellation != nullptr &&
		    reference.cancellation->load(std::memory_order_acquire) != 0)
			return Status_Interrupted;
		std::uint32_t nativeCount = 0;
		Status status = TraceRayReference(reference, output->rays[index], output->phase, &hits, &nativeCount);
		if (status != Status_Ok)
			return status;
		// the fixed subset uses primitive tests without consulting any BVH bounds
		const std::size_t auditSamples = reference.audit == RayReferenceAudit_Preflight ? 1 : 16;
		if (index % std::max<std::size_t>(1, output->rays.size() / auditSamples) == 0)
		{
			std::uint32_t bruteCount = 0;
			status = TraceRayReference(reference, output->rays[index], output->phase, &brute, &bruteCount, 1);
			if (status != Status_Ok || nativeCount != bruteCount || hits.size() != brute.size())
				return Status_Failed;
			for (std::size_t event = 0; event < hits.size(); ++event)
				if (hits[event].collider != brute[event].collider || hits[event].source != brute[event].source ||
				    hits[event].distance != brute[event].distance)
					return Status_Failed;
		}
		output->nativeEventCapacity = std::max(output->nativeEventCapacity, nativeCount + 8);
		if (output->hits.size() + hits.size() > UINT32_MAX ||
		    (output->hits.size() + hits.size()) * sizeof(Hit) * 2 +
		            output->rays.size() * (sizeof(Ray) + sizeof(Range)) >
		        kActiveByteLimit / 2)
			return Status_Capacity;
		output->expected.push_back(
		    {static_cast<std::uint32_t>(output->hits.size()), static_cast<std::uint32_t>(hits.size())});
		output->hits.insert(output->hits.end(), hits.begin(), hits.end());
		distribution->rays += 1;
		distribution->referenceEvents += hits.size();
		if (hits.empty())
			distribution->misses += 1;
		else
		{
			distribution->hits += 1;
			const Collider& collider = scene.colliders[hits[0].collider - 1];
			distribution->shapeHits[collider.shape] += 1;
			if (collider.shape == Shape_Hull)
				distribution->hullHits[collider.hull] += 1;
			const double endpointDistance = output->rays[index].length;
			for (const Hit& hit : hits)
				distribution->endpointBands += hit.distance < RayDistanceTolerance(hit.distance) ||
				                               endpointDistance - hit.distance < RayDistanceTolerance(endpointDistance);
			distribution->shortHits += hits[0].distance < 5;
			distribution->longHits += hits[0].distance > 50;
		}
		if (IsAny(output->phase) != 0)
		{
			distribution->clearSecondary += hits.empty();
			distribution->blockedSecondary += !hits.empty();
		}
	}
	return Status_Ok;
}

Ray PrimaryRay(const Pose& camera, std::uint32_t width, std::uint32_t height, std::uint32_t x, std::uint32_t y)
{
	const float tangent = static_cast<float>(std::tan(kPi / 6));
	const Vector direction = {
	    (2 * (static_cast<float>(x) + .5f) / static_cast<float>(width) - 1) * tangent *
	        static_cast<float>(width) / static_cast<float>(height),
	    (1 - 2 * (static_cast<float>(y) + .5f) / static_cast<float>(height)) * tangent, -1};
	return Segment(camera.position, Rotate(camera.rotation, direction), 512, y * width + x);
}

template <typename T> void WriteArray(std::ofstream& stream, const std::vector<T>& values)
{
	stream.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
}
}

int IsAuthoredRayCorpus(const CaseExecutionRayTracing& ray)
{
	return ray.recipeRevision == 1 && ray.width == benchmark_ray::kWidth && ray.height == benchmark_ray::kHeight &&
	    ray.viewCount == benchmark_ray::kViewCount && ray.primitiveCount == 65536 && ray.meshCount == 1024 &&
	    ray.trianglesPerMesh == 1024 && ray.movingCount == 8192 && ray.seedLow == 0x59524159u && ray.seedHigh == 0x42504f4cu;
}

benchmark_ray::Status BuildHeavyRayScene(benchmark_ray::Scene* scene, const CaseExecutionRayTracing& ray)
{
	using namespace benchmark_ray;
	*scene = {};
	scene->colliders.reserve(ray.primitiveCount + ray.meshCount);
	scene->triangles.reserve(ray.meshCount * ray.trianglesPerMesh + 1024);
	BuildHulls(scene);
	std::uint64_t random = (static_cast<std::uint64_t>(ray.seedHigh) << 32) | ray.seedLow;
	for (std::uint32_t index = 0; index < ray.primitiveCount; ++index)
	{
		Collider collider = {};
		collider.shape = static_cast<Shape>(index % 4);
		collider.category = 1u << ((index / 4) % 4);
		std::uint32_t movingRank = (index / 32) * 4 + index % 4;
		for (std::uint32_t group = 0; group < (index / 4) % 8; ++group)
			movingRank += ray.primitiveCount / 32 * 4 +
			    std::min(4u, ray.primitiveCount % 32 > group * 4 ? ray.primitiveCount % 32 - group * 4 : 0u);
		collider.moving = movingRank < ray.movingCount ? 1u : 0u;
		collider.hull = (index / 4) % 3;
		collider.pose.position = {-78.75f + 2.5f * static_cast<float>(index % 64) + .2f * (Random(&random) - .5f),
		                          2.5f + 2.3f * static_cast<float>(index / 4096),
		                          -78.75f + 2.5f * static_cast<float>((index / 64) % 64) +
		                              .2f * (Random(&random) - .5f)};
		const float angle = Random(&random) * static_cast<float>(kPi);
		collider.pose.rotation = {0, std::sin(angle), 0, std::cos(angle)};
		collider.size = {.2f + .4f * Random(&random), .2f + .4f * Random(&random), .2f + .4f * Random(&random)};
		if (collider.shape == Shape_Capsule)
			collider.size = {.2f + .15f * Random(&random), .2f + .25f * Random(&random), 0};
		scene->colliders.push_back(collider);
	}
	for (std::uint32_t tile = 0; tile < ray.meshCount; ++tile)
		BuildTile(scene, tile, ray.trianglesPerMesh);
	scene->cameras = {Camera({0, 28, 118}, 0, -.12f),      Camera({110, 20, 20}, 1.3f, -.04f),
	                  Camera({-35, 18, 35}, -.45f, -.08f), Camera({0, 50, 0}, 0, -1.35f),
	                  Camera({0, 38, 95}, 0, .7f),         Camera({25, 1.6f, 45}, .15f, -.12f)};
	scene->light = {-45, 75, 35};
	return scene->colliders.size() == ray.primitiveCount + ray.meshCount &&
	    scene->triangles.size() == ray.meshCount * ray.trianglesPerMesh ? Status_Ok : Status_Failed;
}

benchmark_ray::Status BuildRayPrimarySelection(const benchmark_ray::Scene& scene, const RayReference& reference,
                                               std::uint32_t view, std::uint32_t width, std::uint32_t height, std::span<const std::uint32_t> pixels,
                                               benchmark_ray::CorpusPhase* output, RayCorpusDistribution* distribution)
{
	using namespace benchmark_ray;
	if (view >= kViewCount || pixels.empty() || pixels.size() > 4096)
		return Status_Invalid;
	*output = {};
	output->view = view;
	output->phase = Phase_Primary;
	output->width = width;
	output->height = height;
	output->rays.reserve(pixels.size());
	for (std::uint32_t pixel : pixels)
	{
		if (pixel >= width * height)
			return Status_Invalid;
		output->rays.push_back(PrimaryRay(scene.cameras[view], width, height, pixel % width, pixel / width));
	}
	return ReferencePhase(scene, reference, output, distribution);
}

benchmark_ray::Status BuildRayPrimary(const benchmark_ray::Scene& scene, const RayReference& reference,
                                      std::uint32_t view, std::uint32_t width, std::uint32_t height,
                                      benchmark_ray::CorpusPhase* output, RayCorpusDistribution* distribution)
{
	using namespace benchmark_ray;
	if (view >= kViewCount || width == 0 || height == 0 || width > kWidth || height > kHeight)
		return Status_Invalid;
	*output = {};
	output->view = view;
	output->phase = Phase_Primary;
	output->width = width;
	output->height = height;
	output->rays.reserve(static_cast<std::size_t>(width) * height);
	const Pose camera = scene.cameras[view];
	for (std::uint32_t tileY = 0; tileY < height; tileY += 16)
		for (std::uint32_t tileX = 0; tileX < width; tileX += 16)
			for (std::uint32_t y = tileY; y < std::min(height, tileY + 16); ++y)
				for (std::uint32_t x = tileX; x < std::min(width, tileX + 16); ++x)
				{
					output->rays.push_back(PrimaryRay(camera, width, height, x, y));
				}
	return ReferencePhase(scene, reference, output, distribution);
}

benchmark_ray::Status BuildRaySecondary(const benchmark_ray::Scene& scene, const RayReference& reference,
                                        const benchmark_ray::CorpusPhase& primary, benchmark_ray::Phase phase,
                                        benchmark_ray::CorpusPhase* output, RayCorpusDistribution* distribution)
{
	using namespace benchmark_ray;
	if (phase <= Phase_Primary || phase >= Phase_Count || primary.expected.size() != primary.rays.size())
		return Status_Invalid;
	*output = {};
	output->view = primary.view;
	output->phase = phase;
	output->width = primary.width;
	output->height = primary.height;
	if (phase == Phase_Shuffled)
	{
		std::vector<std::uint32_t> order(primary.rays.size());
		std::iota(order.begin(), order.end(), 0u);
		std::uint64_t random = kSeed + primary.view;
		for (std::size_t count = order.size(); count > 1; --count)
			std::swap(order[count - 1], order[Next(&random) % count]);
		output->hits = primary.hits;
		output->nativeEventCapacity = primary.nativeEventCapacity;
		for (std::uint32_t index : order)
		{
			output->rays.push_back(primary.rays[index]);
			output->expected.push_back(primary.expected[index]);
		}
		return Status_Ok;
	}
	if (phase == Phase_Updated)
		output->rays = primary.rays;
	else if (phase == Phase_Filtered || IsEnumeration(phase) != 0)
	{
		const std::size_t count = std::min<std::size_t>(primary.rays.size(), phase == Phase_Filtered ? 262144 : 65536);
		for (std::size_t index = 0; index < count; ++index)
		{
			const std::size_t source = index * primary.rays.size() / count;
			Ray ray = primary.rays[source];
			const Range range = primary.expected[source];
			if (phase == Phase_Filtered && (index & 1) == 0 && range.count != 0)
				ray.mask &= ~scene.colliders[primary.hits[range.first].collider - 1].category;
			output->rays.push_back(ray);
			if (phase == Phase_Filtered && ray.mask != primary.rays[source].mask)
				distribution->excludedNearest += 1;
		}
	}
	else
		for (std::size_t index = 0; index < primary.rays.size(); ++index)
		{
			const Range range = primary.expected[index];
			if (range.count == 0)
				continue;
			const Hit& hit = primary.hits[range.first];
			const Ray& ray = primary.rays[index];
			const Vector point = Add(ray.origin, Mul(ray.translation, static_cast<float>(hit.distance / ray.length)));
			const Vector origin = Add(point, Mul(hit.normal, .001f));
			if (phase == Phase_Shadow)
			{
				const Vector direction = Sub(scene.light, origin);
				if (Dot(hit.normal, direction) > 0)
					output->rays.push_back(
					    Segment(origin, direction, std::sqrt(Dot(direction, direction)) - .001f, ray.pixel));
			}
			else if (phase == Phase_Reflection)
			{
				const Vector direction = Sub(ray.translation, Mul(hit.normal, 2 * Dot(hit.normal, ray.translation)));
				output->rays.push_back(Segment(origin, direction, 512, ray.pixel));
			}
			else if (phase == Phase_Ambient && (ray.pixel % primary.width) % 2 == 0 &&
			         (ray.pixel / primary.width) % 2 == 0)
			{
				const Vector axis = std::abs(hit.normal.y) < .9f ? Vector{0, 1, 0} : Vector{1, 0, 0};
				const Vector tangent = Unit(Cross(axis, hit.normal)), bitangent = Cross(hit.normal, tangent);
				for (std::uint32_t sample = 0; sample < 4; ++sample)
				{
					const float angle = (static_cast<float>(sample) + .375f) * static_cast<float>(kPi / 2);
					const float z = sample < 2 ? .35f : .8f, radial = std::sqrt(1 - z * z);
					const Vector direction = Add(Mul(hit.normal, z), Add(Mul(tangent, radial * std::cos(angle)),
					                                                     Mul(bitangent, radial * std::sin(angle))));
					output->rays.push_back(Segment(origin, direction, 5, ray.pixel));
				}
			}
		}
	if (phase == Phase_Ambient)
	{
		std::vector<Hit> endpointHits;
		for (std::size_t index = 0; index < output->rays.size(); ++index)
		{
			if ((index & 4095) == 0 && reference.cancellation != nullptr &&
			    reference.cancellation->load(std::memory_order_acquire) != 0)
				return Status_Interrupted;
			Ray& ray = output->rays[index];
			int admitted = 0;
			for (std::uint32_t attempt = 0; attempt < 8; ++attempt)
			{
				const float band = static_cast<float>(RayDistanceTolerance(ray.length) * 2);
				const Ray extended = Segment(ray.origin, ray.translation, ray.length + band, ray.pixel, ray.mask);
				std::uint32_t events = 0;
				if (TraceRayReference(reference, extended, phase, &endpointHits, &events) != Status_Ok)
					return Status_Failed;
				if (endpointHits.empty() || std::abs(endpointHits[0].distance - ray.length) > band)
				{
					admitted = 1;
					break;
				}
				const Vector direction =
				    Add(ray.translation, {.002f * static_cast<float>(attempt + 1), .001f, -.0017f});
				ray = Segment(ray.origin, direction, 5, ray.pixel, ray.mask);
			}
			if (admitted == 0)
				return Status_Failed;
		}
	}
	const Status status = ReferencePhase(scene, reference, output, distribution);
	if (status == Status_Ok && phase == Phase_Filtered)
		for (std::size_t index = 0; index < output->rays.size(); ++index)
			if (output->rays[index].mask != 15 && output->expected[index].count != 0)
				distribution->fartherAccepted += 1;
	return status;
}

benchmark_ray::Status WriteRayScene(const std::filesystem::path& path, const benchmark_ray::Scene& scene)
{
	using namespace benchmark_ray;
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	const std::array<std::uint32_t, 6> header = {0x43545242,
	                                             kRecipeRevision,
	                                             static_cast<std::uint32_t>(scene.colliders.size()),
	                                             static_cast<std::uint32_t>(scene.triangles.size()),
	                                             static_cast<std::uint32_t>(scene.hulls.size()),
	                                             static_cast<std::uint32_t>(scene.vertices.size())};
	output.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
	WriteArray(output, scene.colliders);
	WriteArray(output, scene.triangles);
	WriteArray(output, scene.hulls);
	WriteArray(output, scene.vertices);
	output.write(reinterpret_cast<const char*>(scene.cameras.data()), sizeof(scene.cameras));
	output.write(reinterpret_cast<const char*>(&scene.light), sizeof(Vector));
	return output ? Status_Ok : Status_Io;
}

benchmark_ray::Status WriteRayPhase(const std::filesystem::path& path, const benchmark_ray::CorpusPhase& phase)
{
	using namespace benchmark_ray;
	const std::uint64_t bytes =
	    40 + phase.rays.size() * sizeof(Ray) + phase.expected.size() * sizeof(Range) + phase.hits.size() * sizeof(Hit);
	if (bytes > kActiveByteLimit || phase.rays.size() != phase.expected.size() || phase.hits.size() > UINT32_MAX)
		return Status_Capacity;
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	const std::array<std::uint32_t, 10> header = {0x52545242,
	                                              kRecipeRevision,
	                                              phase.view,
	                                              phase.phase,
	                                              phase.width,
	                                              phase.height,
	                                              static_cast<std::uint32_t>(phase.rays.size()),
	                                              static_cast<std::uint32_t>(phase.hits.size()),
	                                              phase.nativeEventCapacity,
	                                              0};
	output.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
	WriteArray(output, phase.rays);
	WriteArray(output, phase.expected);
	WriteArray(output, phase.hits);
	return output ? Status_Ok : Status_Io;
}

benchmark_ray::Status PrepareRayProbeCorpus(const std::filesystem::path& root)
{
	using namespace benchmark_ray;
	Scene scene = {};
	for (Pose& camera : scene.cameras)
		camera.rotation.w = 1;
	BuildHulls(&scene);
	for (std::uint32_t index = 0; index < 6; ++index)
	{
		Collider collider = {};
		collider.shape = index < 3 ? static_cast<Shape>(index) : Shape_Hull;
		collider.hull = index < 3 ? 0 : index - 3;
		collider.category = 1;
		collider.pose = {{4.f * static_cast<float>(index), 0, 0}, {0, 0, 0, 1}};
		collider.size = {1, 1, 1};
		scene.colliders.push_back(collider);
	}
	for (std::uint32_t index = 0; index < 2; ++index)
	{
		Collider collider = {};
		collider.shape = Shape_Mesh;
		collider.category = 1;
		collider.pose = {{28.f + 4.f * static_cast<float>(index), 0, 0}, {0, 0, 0, 1}};
		collider.firstTriangle = index;
		collider.triangleCount = 1;
		scene.colliders.push_back(collider);
		scene.triangles.push_back(index == 0 ? Triangle{{-1, -1, 0}, {1, -1, 0}, {0, 1, 0}}
		                                     : Triangle{{-1, -1, 0}, {0, 1, 0}, {1, -1, 0}});
	}
	Collider far = scene.colliders[0];
	far.pose.position.x = 1000000.f;
	scene.colliders.push_back(far);
	for (std::uint32_t index = 0; index < 16; ++index)
	{
		Collider layer = scene.colliders[0];
		layer.pose.position = {40, 0, -3.f * static_cast<float>(index)};
		scene.colliders.push_back(layer);
	}
	std::error_code error;
	std::filesystem::create_directories(root, error);
	if (error || WriteRayScene(root / "probe-scene.rtc", scene) != Status_Ok)
		return Status_Io;
	RayReference reference;
	if (BuildRayReference(scene, -1, &reference) != Status_Ok)
		return Status_Invalid;
	for (std::uint32_t view = 0; view < 6; ++view)
		for (std::uint32_t probeIndex = 0; probeIndex < Probe_Count; ++probeIndex)
		{
			const Probe probe = static_cast<Probe>(probeIndex);
			CorpusPhase phase = {};
			phase.view = view;
			phase.width = 512;
			phase.height = 1;
			phase.phase = probe == Probe_Overflow ? Phase_ColliderHits : Phase_Primary;
			for (std::uint32_t index = 0; index < 512; ++index)
			{
				const float jitter = (static_cast<float>((index + view) % 17) - 8) * .001f;
				Ray ray = {};
				const Vector position = scene.colliders[index % 6].pose.position;
				switch (probe)
				{
				case Probe_Inside:
					ray = Segment(position, {1, .1f + jitter, 0}, 4, index);
					break;
				case Probe_Backfaces:
					ray = Segment(
					    {28.f + 4.f * static_cast<float>(index % 2), -.1f + jitter, (index & 2) == 0 ? 3.f : -3.f},
					    {0, 0, (index & 2) == 0 ? -1.f : 1.f}, 6, index);
					break;
				case Probe_Surface:
					ray = Segment({1.f + static_cast<float>(static_cast<int>((index / 2) % 3) - 1) * .001f, jitter, 0},
					              {(index & 1) == 0 ? 1.f : -1.f, 0, 0}, 4, index);
					break;
				case Probe_Range:
					ray = Segment({0, jitter, 3}, {0, 0, -1},
					              2.f + static_cast<float>(static_cast<int>(index % 3) - 1) * .001f, index);
					break;
				case Probe_Grazing:
					ray = Segment({1.f + jitter * .001f, 0, 3}, {0, 0, -1}, 6, index);
					break;
				case Probe_FarOrigin:
					ray = Segment({1000000.f + (static_cast<float>((index + view) % 17) - 8) * .0625f, 0, 3},
					              {0, 0, -1}, 6, index);
					break;
				case Probe_Overflow:
					ray = Segment({40, jitter, 3}, {0, 0, -1}, 60, index);
					break;
				case Probe_Concurrent:
					ray = Segment({position.x, jitter, 3}, {0, 0, -1}, 6, index);
					break;
				case Probe_Count:
					return Status_Invalid;
				}
				phase.rays.push_back(ray);
			}
			RayCorpusDistribution distribution = {};
			const Status status = ReferencePhase(scene, reference, &phase, &distribution);
			if (status != Status_Ok)
				return status;
			if (probe == Probe_Overflow)
				phase.nativeEventCapacity = 2;
			const std::filesystem::path path =
			    root / ("probe-view-" + std::to_string(view) + "-" + ProbeName(probe) + ".rtr");
			if (WriteRayPhase(path, phase) != Status_Ok)
				return Status_Io;
		}
	return Status_Ok;
}

namespace
{
struct CorpusAdmission
{
	std::array<std::array<std::uint32_t, Phase_Count>, kViewCount> queries;
	std::array<std::array<std::uint32_t, Phase_Count>, kViewCount> seen;
	std::array<std::uint64_t, 13> totals;
	std::array<std::uint64_t, 3> hullCoverage;
	std::array<std::uint64_t, 2> clear, blocked;
	std::uint64_t filteredFarther;
	std::uint32_t hitRichViews, missRichViews, summaryRows;
};

ArenaStatus CorpusInvalid(StatusRecord* error)
{
	error->code = ArenaStatus_InvalidResult;
	return error->code;
}

ArenaStatus ReadViewDistribution(const CsvHeader* header, const CsvRow* row, void* context, StatusRecord* error)
{
	constexpr std::array<std::string_view, 20> columns = {"view",
	                                                      "phase",
	                                                      "rays",
	                                                      "hits",
	                                                      "misses",
	                                                      "short_hits",
	                                                      "long_hits",
	                                                      "clear_secondary",
	                                                      "blocked_secondary",
	                                                      "box",
	                                                      "sphere",
	                                                      "capsule",
	                                                      "hull",
	                                                      "mesh",
	                                                      "hull8",
	                                                      "hull24",
	                                                      "hull64",
	                                                      "excluded_nearest",
	                                                      "farther_accepted",
	                                                      "endpoint_bands"};
	if (header->fieldCount != columns.size() || row->fieldCount != columns.size())
		return CorpusInvalid(error);
	std::array<std::uint64_t, 20> values = {};
	std::uint32_t phase = 0;
	for (std::size_t index = 0; index < columns.size(); ++index)
	{
		if (CsvHeaderTextView(header, header->fields[index]) != columns[index])
			return CorpusInvalid(error);
		const std::string_view field = CsvRowTextView(row, row->fields[index]);
		if (index == 1)
		{
			while (phase < Phase_Count && field != PhaseName(static_cast<Phase>(phase)))
				++phase;
			if (phase == Phase_Count)
				return CorpusInvalid(error);
		}
		else
		{
			const std::from_chars_result parsed =
			    std::from_chars(field.data(), field.data() + field.size(), values[index]);
			if (field.empty() || parsed.ec != std::errc() || parsed.ptr != field.data() + field.size() ||
			    values[index] > kWidth * kHeight)
				return CorpusInvalid(error);
		}
	}
	CorpusAdmission& admission = *static_cast<CorpusAdmission*>(context);
	const std::uint64_t view = values[0];
	if (view >= kViewCount || admission.seen[view][phase]++ != 0 || values[2] != admission.queries[view][phase] ||
	    values[3] + values[4] != values[2] || values[5] + values[6] > values[3] || values[19] != 0 ||
	    values[9] + values[10] + values[11] + values[12] + values[13] != values[3] ||
	    values[14] + values[15] + values[16] != values[12])
		return CorpusInvalid(error);
	for (std::size_t index = 2; index <= 13; ++index)
		admission.totals[index - 2] += values[index];
	admission.filteredFarther += values[18];
	if (phase == Phase_Primary)
	{
		admission.hitRichViews += values[3] > values[4];
		admission.missRichViews += values[4] > values[3];
		for (std::size_t index = 0; index < 3; ++index)
			admission.hullCoverage[index] += values[14 + index];
	}
	if (phase == Phase_Shadow || phase == Phase_Ambient)
	{
		if (values[7] != values[4] || values[8] != values[3])
			return CorpusInvalid(error);
		const std::size_t index = phase == Phase_Shadow ? 0 : 1;
		admission.clear[index] += values[7];
		admission.blocked[index] += values[8];
	}
	else if (values[7] != 0 || values[8] != 0)
		return CorpusInvalid(error);
	return ArenaStatus_Ok;
}

ArenaStatus ReadCorpusDistribution(const CsvHeader* header, const CsvRow* row, void* context, StatusRecord* error)
{
	constexpr std::array<std::string_view, 13> columns = {
	    "rays", "hits",   "misses",  "short_hits", "long_hits", "clear_secondary", "blocked_secondary",
	    "box",  "sphere", "capsule", "hull",       "mesh",      "corpus_bytes"};
	CorpusAdmission& admission = *static_cast<CorpusAdmission*>(context);
	if (header->fieldCount != columns.size() || row->fieldCount != columns.size() || admission.summaryRows++ != 0)
		return CorpusInvalid(error);
	for (std::size_t index = 0; index < columns.size(); ++index)
	{
		if (CsvHeaderTextView(header, header->fields[index]) != columns[index])
			return CorpusInvalid(error);
		const std::string_view field = CsvRowTextView(row, row->fields[index]);
		std::uint64_t value = 0;
		const std::from_chars_result parsed = std::from_chars(field.data(), field.data() + field.size(), value);
		if (field.empty() || parsed.ec != std::errc() || parsed.ptr != field.data() + field.size() ||
		    value != admission.totals[index])
			return CorpusInvalid(error);
	}
	return ArenaStatus_Ok;
}
}

benchmark_ray::Status AdmitHeavyRayCorpus(const std::filesystem::path& root, const CaseExecutionRayTracing& ray)
{
	using namespace benchmark_ray;
	CorpusAdmission admission = {};
	for (const char* name : {"scene.rtc", "probe-scene.rtc"})
	{
		std::array<std::uint32_t, 4> counts = {};
		if (ReadSceneMetadata(root / name, &counts) != Status_Ok)
			return Status_Invalid;
		const std::array<std::uint32_t, 4> expected = std::string_view(name) == "scene.rtc"
		                                                  ? std::array<std::uint32_t, 4>{ray.primitiveCount + ray.meshCount, ray.meshCount * ray.trianglesPerMesh, 3, 96}
		                                                  : std::array<std::uint32_t, 4>{25, 2, 3, 96};
		if (counts != expected)
			return Status_Invalid;
		admission.totals[12] += 204 + static_cast<std::uint64_t>(counts[0]) * sizeof(Collider) +
		                        static_cast<std::uint64_t>(counts[1]) * sizeof(Triangle) + counts[2] * sizeof(Hull) +
		                        counts[3] * sizeof(Vector);
	}
	for (std::uint32_t view = 0; view < kViewCount; ++view)
	{
		for (std::uint32_t phase = 0; view < ray.viewCount && phase < Phase_Count; ++phase)
		{
			PhaseMetadata metadata = {};
			if (ReadPhaseMetadata(PhasePath(root, view, static_cast<Phase>(phase)), &metadata) != Status_Ok ||
			    metadata.view != view || metadata.phase != phase || metadata.width != ray.width ||
			    metadata.height != ray.height)
				return Status_Invalid;
			if (((phase == Phase_Primary || phase == Phase_Shuffled || phase == Phase_Updated) &&
			     metadata.rays != ray.width * ray.height) ||
			    (phase == Phase_Filtered && metadata.rays != std::min(262144u, ray.width * ray.height)) ||
			    (IsEnumeration(static_cast<Phase>(phase)) != 0 && metadata.rays != std::min(65536u, ray.width * ray.height)))
				return Status_Invalid;
			admission.queries[view][phase] = metadata.rays;
			admission.totals[12] += 40 + static_cast<std::uint64_t>(metadata.rays) * (sizeof(Ray) + sizeof(Range)) +
			                        static_cast<std::uint64_t>(metadata.hits) * sizeof(Hit);
		}
		for (std::uint32_t probe = 0; probe < Probe_Count; ++probe)
		{
			PhaseMetadata metadata = {};
			if (ReadPhaseMetadata(
			        root / ("probe-view-" + std::to_string(view) + "-" + ProbeName(static_cast<Probe>(probe)) + ".rtr"),
			        &metadata) != Status_Ok ||
			    metadata.view != view ||
			    metadata.phase != (probe == Probe_Overflow ? Phase_ColliderHits : Phase_Primary) ||
			    metadata.width != 512 || metadata.height != 1 || metadata.rays != 512 ||
			    (probe == Probe_Overflow && metadata.nativeEvents != 2))
				return Status_Invalid;
			admission.totals[12] += 40 + static_cast<std::uint64_t>(metadata.rays) * (sizeof(Ray) + sizeof(Range)) +
			                        static_cast<std::uint64_t>(metadata.hits) * sizeof(Hit);
		}
	}
	if (admission.totals[12] > kCorpusByteLimit)
		return Status_Capacity;
	CsvHeader header = {};
	CsvReadRecord record = {};
	StatusRecord error = {};
	if (ReadCsvFile((root / "view-distribution.csv").c_str(), &header, ReadViewDistribution, &admission, &record,
	                &error) != ArenaStatus_Ok ||
	    record.rowCount != ray.viewCount * Phase_Count)
		return Status_Invalid;
	if (IsAuthoredRayCorpus(ray) != 0)
	{
		if (admission.hitRichViews == 0 || admission.missRichViews == 0 || admission.filteredFarther == 0)
			return Status_Invalid;
		for (std::uint64_t count : admission.totals)
			if (count == 0) return Status_Invalid;
		for (std::uint64_t count : admission.hullCoverage)
			if (count == 0) return Status_Invalid;
		for (std::size_t index = 0; index < 2; ++index)
			if (admission.clear[index] == 0 || admission.blocked[index] == 0) return Status_Invalid;
	}
	if (ReadCsvFile((root / "distribution.csv").c_str(), &header, ReadCorpusDistribution, &admission, &record,
	                &error) != ArenaStatus_Ok ||
	    admission.summaryRows != 1)
		return Status_Invalid;
	return Status_Ok;
}

benchmark_ray::Status PrepareHeavyRayCorpus(const std::filesystem::path& root, const CaseExecutionRayTracing& ray, RayCorpusDistribution* distribution,
                                            const std::atomic<std::uint32_t>* cancellation, RayCorpusProgress progress,
                                            void* context)
{
	using namespace benchmark_ray;
	std::error_code error;
	std::filesystem::create_directories(root, error);
	if (error)
		return Status_Io;
	Status status = PrepareRayProbeCorpus(root);
	if (status != Status_Ok)
		return status;
	Scene scene = {};
	status = BuildHeavyRayScene(&scene, ray);
	if (status != Status_Ok)
		return status;
	status = WriteRayScene(root / "scene.rtc", scene);
	if (status != Status_Ok)
		return status;
	RayReference reference;
	status = BuildRayReference(scene, -1, &reference);
	if (status != Status_Ok)
		return status;
	reference.cancellation = cancellation;
	std::uint64_t retained = 0;
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(root, error))
		if (entry.path().extension() == ".rtc" || entry.path().filename().string().starts_with("probe-view-"))
			retained += entry.file_size(error);
	std::ofstream perView(root / "view-distribution.csv", std::ios::trunc);
	perView
	    << "view,phase,rays,hits,misses,short_hits,long_hits,clear_secondary,blocked_secondary,box,sphere,capsule,hull,mesh,hull8,hull24,hull64,excluded_nearest,farther_accepted,endpoint_bands\n";
	std::uint64_t primaryHits = 0, primaryMisses = 0, filteredFarther = 0;
	std::uint32_t hitRichViews = 0, missRichViews = 0;
	std::array<std::uint64_t, 2> clear = {}, blocked = {};
	std::array<std::uint64_t, 3> hullCoverage = {};
	if (error)
		return Status_Io;
	for (std::uint32_t view = 0; view < ray.viewCount; ++view)
	{
		if (cancellation != nullptr && cancellation->load(std::memory_order_acquire) != 0)
			return Status_Interrupted;
		if (progress != nullptr && progress(context, view, Phase_Primary) != 0)
			return Status_Interrupted;
		CorpusPhase primary;
		RayCorpusDistribution primaryDistribution = {};
		status = BuildRayPrimary(scene, reference, view, ray.width, ray.height, &primary, &primaryDistribution);
		primaryHits += primaryDistribution.hits;
		primaryMisses += primaryDistribution.misses;
		hitRichViews += primaryDistribution.hits > primaryDistribution.misses;
		missRichViews += primaryDistribution.misses > primaryDistribution.hits;
		for (std::size_t hull = 0; hull < hullCoverage.size(); ++hull)
			hullCoverage[hull] += primaryDistribution.hullHits[hull];
		if (status != Status_Ok)
			return status;
		for (std::uint32_t phaseIndex = 0; phaseIndex < Phase_Count; ++phaseIndex)
		{
			const Phase phase = static_cast<Phase>(phaseIndex);
			RayCorpusDistribution phaseDistribution = primaryDistribution;
			CorpusPhase secondary;
			const CorpusPhase* data = &primary;
			if (phase != Phase_Primary)
			{
				if (progress != nullptr && progress(context, view, phase) != 0)
					return Status_Interrupted;
				phaseDistribution = {};
				RayReference updated;
				const RayReference* oracle = &reference;
				if (phase == Phase_Updated)
				{
					status = BuildRayReference(scene, static_cast<int>(view), &updated);
					if (status != Status_Ok)
						return status;
					updated.cancellation = cancellation;
					oracle = &updated;
				}
				status = BuildRaySecondary(scene, *oracle, primary, phase, &secondary, &phaseDistribution);
				if (phase == Phase_Shuffled)
					phaseDistribution = primaryDistribution;
				if (status != Status_Ok)
					return status;
				data = &secondary;
			}
			const std::uint64_t bytes = 40 + data->rays.size() * sizeof(Ray) + data->expected.size() * sizeof(Range) +
			                            data->hits.size() * sizeof(Hit);
			if (bytes > kActiveByteLimit || retained > kCorpusByteLimit - bytes)
				return Status_Capacity;
			status = WriteRayPhase(PhasePath(root, view, phase), *data);
			if (status != Status_Ok)
				return status;
			retained += bytes;
			filteredFarther += phaseDistribution.fartherAccepted;
			if (phase == Phase_Shadow || phase == Phase_Ambient)
			{
				const std::size_t kind = phase == Phase_Shadow ? 0 : 1;
				clear[kind] += phaseDistribution.clearSecondary;
				blocked[kind] += phaseDistribution.blockedSecondary;
			}
			perView << view << ',' << PhaseName(phase) << ',' << phaseDistribution.rays << ',' << phaseDistribution.hits
			        << ',' << phaseDistribution.misses << ',' << phaseDistribution.shortHits << ','
			        << phaseDistribution.longHits << ',' << phaseDistribution.clearSecondary << ','
			        << phaseDistribution.blockedSecondary;
			for (std::uint64_t hits : phaseDistribution.shapeHits)
				perView << ',' << hits;
			for (std::uint64_t hits : phaseDistribution.hullHits)
				perView << ',' << hits;
			perView << ',' << phaseDistribution.excludedNearest << ',' << phaseDistribution.fartherAccepted << ','
			        << phaseDistribution.endpointBands << '\n';
			if (phaseDistribution.endpointBands != 0)
				return Status_Failed;
			distribution->rays += phaseDistribution.rays;
			distribution->hits += phaseDistribution.hits;
			distribution->misses += phaseDistribution.misses;
			distribution->shortHits += phaseDistribution.shortHits;
			distribution->longHits += phaseDistribution.longHits;
			distribution->clearSecondary += phaseDistribution.clearSecondary;
			distribution->blockedSecondary += phaseDistribution.blockedSecondary;
			for (std::size_t shape = 0; shape < 5; ++shape)
				distribution->shapeHits[shape] += phaseDistribution.shapeHits[shape];
		}
	}
	if (!perView)
		return Status_Io;
	if (IsAuthoredRayCorpus(ray) != 0)
	{
		if (hitRichViews == 0 || missRichViews == 0 || clear[0] == 0 || clear[1] == 0 || blocked[0] == 0 ||
		    blocked[1] == 0 || primaryHits == 0 || primaryMisses == 0 || filteredFarther == 0 || !perView ||
		    distribution->hits == 0 || distribution->misses == 0 || distribution->shortHits == 0 ||
		    distribution->longHits == 0 || distribution->clearSecondary == 0 || distribution->blockedSecondary == 0)
			return Status_Failed;
		for (std::uint64_t hits : distribution->shapeHits)
			if (hits == 0)
				return Status_Failed;
		for (std::uint64_t hits : hullCoverage)
			if (hits == 0)
				return Status_Failed;
	}
	std::ofstream counts(root / "distribution.csv", std::ios::trunc);
	counts
	    << "rays,hits,misses,short_hits,long_hits,clear_secondary,blocked_secondary,box,sphere,capsule,hull,mesh,corpus_bytes\n"
	    << distribution->rays << ',' << distribution->hits << ',' << distribution->misses << ','
	    << distribution->shortHits << ',' << distribution->longHits << ',' << distribution->clearSecondary << ','
	    << distribution->blockedSecondary;
	for (std::uint64_t hits : distribution->shapeHits)
		counts << ',' << hits;
	counts << ',' << retained << '\n';
	return counts ? Status_Ok : Status_Io;
}
}
