#pragma once

#include "ray_tracing.h"
#include <atomic>

namespace physics_arena
{
enum RayReferenceAudit
{
	RayReferenceAudit_Stratified = 0,
	RayReferenceAudit_Preflight = 1,
};
struct RayReferencePlane
{
	double x, y, z, offset;
};
struct RayReferenceBounds
{
	std::array<double, 3> minimum, maximum;
};
struct RayReferenceLeaf
{
	RayReferenceBounds bounds;
	std::uint32_t collider, source;
};
struct RayReferenceNode
{
	RayReferenceBounds bounds;
	std::uint32_t first, count, right;
};
struct RayReference
{
	RayReferenceAudit audit;
	const benchmark_ray::Scene* scene;
	const std::atomic<std::uint32_t>* cancellation;
	std::vector<benchmark_ray::Pose> poses;
	std::vector<std::vector<RayReferencePlane>> hullPlanes;
	std::vector<RayReferenceLeaf> leaves;
	std::vector<RayReferenceNode> nodes;
};
benchmark_ray::Status BuildRayReference(const benchmark_ray::Scene& scene, int updatedView, RayReference* reference);
benchmark_ray::Status TraceRayReference(const RayReference& reference, const benchmark_ray::Ray& ray,
                                        benchmark_ray::Phase phase, std::vector<benchmark_ray::Hit>* hits,
                                        std::uint32_t* nativeEventCount, int bruteForce = 0);
double RayDistanceTolerance(double distance);
}
