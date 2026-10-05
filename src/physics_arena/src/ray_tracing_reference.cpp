#include "physics_arena/ray_tracing_reference.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <span>

namespace physics_arena
{
namespace
{
using namespace benchmark_ray;
using D = std::array<double, 3>;
D Promote(Vector v)
{
	return {v.x, v.y, v.z};
}
D Add(D a, D b)
{
	return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}
D Sub(D a, D b)
{
	return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
D Mul(D a, double s)
{
	return {a[0] * s, a[1] * s, a[2] * s};
}
double Dot(D a, D b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
D Cross(D a, D b)
{
	return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
D Unit(D a)
{
	return Mul(a, 1.0 / std::sqrt(Dot(a, a)));
}
D Rotate(Quaternion q, D p)
{
	const D v = {q.x, q.y, q.z};
	const D t = Mul(Cross(v, p), 2.0);
	return Add(p, Add(Mul(t, q.w), Cross(v, t)));
}
D Inverse(Quaternion q, D p)
{
	return Rotate({-q.x, -q.y, -q.z, q.w}, p);
}
RayReferenceBounds EmptyBounds()
{
	const double infinity = std::numeric_limits<double>::infinity();
	return {{infinity, infinity, infinity}, {-infinity, -infinity, -infinity}};
}
void Extend(RayReferenceBounds* bounds, D point)
{
	for (std::size_t axis = 0; axis < 3; ++axis)
	{
		bounds->minimum[axis] = std::min(bounds->minimum[axis], point[axis]);
		bounds->maximum[axis] = std::max(bounds->maximum[axis], point[axis]);
	}
}
Status HullPlanes(const Scene& scene, const Hull& hull, std::vector<RayReferencePlane>* planes)
{
	if (hull.vertexCount < 4 || hull.vertexCount > 64 || hull.firstVertex > scene.vertices.size() ||
	    hull.vertexCount > scene.vertices.size() - hull.firstVertex)
		return Status_Invalid;
	for (std::uint32_t a = 0; a < hull.vertexCount; ++a)
		for (std::uint32_t b = a + 1; b < hull.vertexCount; ++b)
			for (std::uint32_t c = b + 1; c < hull.vertexCount; ++c)
			{
				const D p = Promote(scene.vertices[hull.firstVertex + a]);
				D n = Cross(Sub(Promote(scene.vertices[hull.firstVertex + b]), p),
				            Sub(Promote(scene.vertices[hull.firstVertex + c]), p));
				if (Dot(n, n) < 1e-18)
					continue;
				n = Unit(n);
				double offset = Dot(n, p);
				int positive = 0, negative = 0;
				for (std::uint32_t index = 0; index < hull.vertexCount; ++index)
				{
					const double side = Dot(n, Promote(scene.vertices[hull.firstVertex + index])) - offset;
					positive |= side > 1e-8;
					negative |= side < -1e-8;
				}
				if (positive != 0 && negative != 0)
					continue;
				if (positive != 0)
				{
					n = Mul(n, -1);
					offset = -offset;
				}
				int duplicate = 0;
				for (const RayReferencePlane& plane : *planes)
					duplicate |=
					    Dot(n, {plane.x, plane.y, plane.z}) > 1 - 1e-10 && std::abs(offset - plane.offset) < 1e-8;
				if (duplicate == 0)
					planes->push_back({n[0], n[1], n[2], offset});
			}
	return planes->size() >= 4 ? Status_Ok : Status_Invalid;
}
std::uint32_t BuildNode(RayReference* reference, std::uint32_t first, std::uint32_t count)
{
	const std::uint32_t node = static_cast<std::uint32_t>(reference->nodes.size());
	RayReferenceBounds bounds = EmptyBounds();
	RayReferenceBounds centers = EmptyBounds();
	for (std::uint32_t index = first; index < first + count; ++index)
	{
		const RayReferenceBounds& leaf = reference->leaves[index].bounds;
		Extend(&bounds, leaf.minimum);
		Extend(&bounds, leaf.maximum);
		Extend(&centers, Add(leaf.minimum, leaf.maximum));
	}
	reference->nodes.push_back({bounds, first, count, 0});
	if (count <= 4)
		return node;
	std::size_t axis = 0;
	for (std::size_t candidate = 1; candidate < 3; ++candidate)
		if (centers.maximum[candidate] - centers.minimum[candidate] > centers.maximum[axis] - centers.minimum[axis])
			axis = candidate;
	const std::uint32_t leftCount = count / 2;
	std::nth_element(reference->leaves.begin() + first, reference->leaves.begin() + first + leftCount,
	                 reference->leaves.begin() + first + count,
	                 [axis](const RayReferenceLeaf& a, const RayReferenceLeaf& b)
	                 {
		                 return a.bounds.minimum[axis] + a.bounds.maximum[axis] <
		                        b.bounds.minimum[axis] + b.bounds.maximum[axis];
	                 });
	BuildNode(reference, first, leftCount);
	const std::uint32_t right = BuildNode(reference, first + leftCount, count - leftCount);
	reference->nodes[node].count = 0;
	reference->nodes[node].first = node + 1;
	reference->nodes[node].right = right;
	return node;
}
int Intersects(const RayReferenceBounds& bounds, D origin, D direction)
{
	double low = 0, high = 1;
	for (std::size_t axis = 0; axis < 3; ++axis)
	{
		if (direction[axis] == 0)
		{
			if (origin[axis] < bounds.minimum[axis] || origin[axis] > bounds.maximum[axis])
				return 0;
			continue;
		}
		double a = (bounds.minimum[axis] - origin[axis]) / direction[axis];
		double b = (bounds.maximum[axis] - origin[axis]) / direction[axis];
		if (a > b)
			std::swap(a, b);
		low = std::max(low, a);
		high = std::min(high, b);
		if (low > high)
			return 0;
	}
	return 1;
}
int Clip(std::span<const RayReferencePlane> planes, D origin, D direction, double* fraction, D* normal,
         std::uint32_t* flags)
{
	double enter = -1, exit = 1;
	for (const RayReferencePlane& plane : planes)
	{
		const D n = {plane.x, plane.y, plane.z};
		const double distance = plane.offset - Dot(n, origin);
		const double denominator = Dot(n, direction);
		if (std::abs(denominator) < 1e-14)
		{
			if (distance < 0)
				return 0;
			continue;
		}
		const double t = distance / denominator;
		if (denominator < 0)
		{
			if (std::abs(t - enter) < 1e-8)
				*flags |= HitFlags_Edge;
			if (t > enter)
			{
				enter = t;
				*normal = n;
			}
		}
		else
			exit = std::min(exit, t);
		if (enter > exit)
			return 0;
	}
	*fraction = enter;
	return enter >= 0 && enter <= 1;
}
int Sphere(D origin, D direction, double radius, double* fraction, D* normal)
{
	const double a = Dot(direction, direction), b = Dot(origin, direction), c = Dot(origin, origin) - radius * radius;
	const double discriminant = b * b - a * c;
	if (c < 0 || a == 0 || discriminant < 0)
		return 0;
	const double t = (-b - std::sqrt(discriminant)) / a;
	if (t < 0 || t > 1)
		return 0;
	*fraction = t;
	*normal = Unit(Add(origin, Mul(direction, t)));
	return 1;
}
int Capsule(D origin, D direction, double radius, double half, double* fraction, D* normal)
{
	const double y = std::clamp(origin[1], -half, half);
	if (Dot(Sub(origin, {0, y, 0}), Sub(origin, {0, y, 0})) < radius * radius)
		return 0;
	double nearest = 2;
	const double a = direction[0] * direction[0] + direction[2] * direction[2];
	const double b = origin[0] * direction[0] + origin[2] * direction[2];
	const double c = origin[0] * origin[0] + origin[2] * origin[2] - radius * radius;
	if (a > 0 && b * b - a * c >= 0)
	{
		const double t = (-b - std::sqrt(b * b - a * c)) / a;
		const D p = Add(origin, Mul(direction, t));
		if (t >= 0 && t <= 1 && std::abs(p[1]) <= half)
		{
			nearest = t;
			*normal = Unit({p[0], 0, p[2]});
		}
	}
	for (const double sign : {-1.0, 1.0})
	{
		double t = 0;
		D n = {};
		if (Sphere(Sub(origin, {0, sign * half, 0}), direction, radius, &t, &n) != 0 &&
		    sign * (origin[1] + t * direction[1]) >= half && t < nearest)
		{
			nearest = t;
			*normal = n;
		}
	}
	*fraction = nearest;
	return nearest <= 1;
}
int TriangleHit(const Triangle& triangle, D origin, D direction, double* fraction, D* normal, std::uint32_t* flags)
{
	const D a = Promote(triangle.a), e1 = Sub(Promote(triangle.b), a), e2 = Sub(Promote(triangle.c), a);
	const D cross = Cross(direction, e2);
	const double determinant = Dot(e1, cross);
	if (determinant <= 1e-14)
		return 0;
	const D offset = Sub(origin, a);
	const double u = Dot(offset, cross) / determinant;
	const D q = Cross(offset, e1);
	const double v = Dot(direction, q) / determinant;
	const double t = Dot(e2, q) / determinant;
	if (u < -1e-12 || v < -1e-12 || u + v > 1 + 1e-12 || t < 0 || t > 1)
		return 0;
	*fraction = t;
	*normal = Unit(Cross(e1, e2));
	if (std::min({u, v, 1 - u - v}) < 1e-7)
		*flags |= HitFlags_Edge;
	return 1;
}
int LeafHit(const RayReference& reference, const RayReferenceLeaf& leaf, const Ray& ray, Hit* hit)
{
	const Collider& collider = reference.scene->colliders[leaf.collider];
	if ((collider.category & ray.mask) == 0)
		return 0;
	const Pose& pose = reference.poses[leaf.collider];
	D origin = Inverse(pose.rotation, Sub(Promote(ray.origin), Promote(pose.position)));
	D direction = Inverse(pose.rotation, Promote(ray.translation));
	D normal = {};
	double fraction = 0;
	int found = 0;
	std::uint32_t flags = 0;
	switch (collider.shape)
	{
	case Shape_Box:
	{
		const std::array<RayReferencePlane, 6> planes = {{{1, 0, 0, collider.size.x},
		                                                  {-1, 0, 0, collider.size.x},
		                                                  {0, 1, 0, collider.size.y},
		                                                  {0, -1, 0, collider.size.y},
		                                                  {0, 0, 1, collider.size.z},
		                                                  {0, 0, -1, collider.size.z}}};
		found = Clip(planes, origin, direction, &fraction, &normal, &flags);
		break;
	}
	case Shape_Sphere:
		found = Sphere(origin, direction, collider.size.x, &fraction, &normal);
		break;
	case Shape_Capsule:
		found = Capsule(origin, direction, collider.size.x, collider.size.y, &fraction, &normal);
		break;
	case Shape_Hull:
	{
		const D scale = Promote(collider.size);
		for (std::size_t axis = 0; axis < 3; ++axis)
		{
			origin[axis] /= scale[axis];
			direction[axis] /= scale[axis];
		}
		found = Clip(reference.hullPlanes[collider.hull], origin, direction, &fraction, &normal, &flags);
		for (std::size_t axis = 0; axis < 3; ++axis)
			normal[axis] /= scale[axis];
		break;
	}
	case Shape_Mesh:
		found = TriangleHit(reference.scene->triangles[collider.firstTriangle + leaf.source], origin, direction,
		                    &fraction, &normal, &flags);
		break;
	}
	if (found == 0)
		return 0;
	normal = Unit(Rotate(pose.rotation, normal));
	*hit = {fraction * ray.length,
	        {static_cast<float>(normal[0]), static_cast<float>(normal[1]), static_cast<float>(normal[2])},
	        leaf.collider + 1,
	        leaf.source,
	        flags};
	return 1;
}
void AppendFeatureNormals(const RayReference& reference, const Ray& ray, const Hit& hit, std::vector<Hit>* hits)
{
	const Collider& collider = reference.scene->colliders[hit.collider - 1];
	if (collider.shape != Shape_Box && collider.shape != Shape_Hull)
		return;
	const Pose& pose = reference.poses[hit.collider - 1];
	const D point =
	    Inverse(pose.rotation, Sub(Add(Promote(ray.origin), Mul(Promote(ray.translation), hit.distance / ray.length)),
	                               Promote(pose.position)));
	const std::array<RayReferencePlane, 6> box = {{{1, 0, 0, collider.size.x},
	                                               {-1, 0, 0, collider.size.x},
	                                               {0, 1, 0, collider.size.y},
	                                               {0, -1, 0, collider.size.y},
	                                               {0, 0, 1, collider.size.z},
	                                               {0, 0, -1, collider.size.z}}};
	const std::span<const RayReferencePlane> planes =
	    collider.shape == Shape_Box ? std::span<const RayReferencePlane>(box)
	                                : std::span<const RayReferencePlane>(reference.hullPlanes[collider.hull]);
	for (const RayReferencePlane& plane : planes)
	{
		D n = {plane.x, plane.y, plane.z};
		if (collider.shape == Shape_Hull)
		{
			n[0] /= collider.size.x;
			n[1] /= collider.size.y;
			n[2] /= collider.size.z;
		}
		const double magnitude = std::sqrt(Dot(n, n));
		if (std::abs(Dot(n, point) - plane.offset) / magnitude > 1e-5)
			continue;
		const D world = Unit(Rotate(pose.rotation, n));
		if (Dot(world, Promote(ray.translation)) >= 0 || Dot(world, Promote(hit.normal)) > 1 - 1e-8)
			continue;
		Hit alternative = hit;
		alternative.normal = {static_cast<float>(world[0]), static_cast<float>(world[1]), static_cast<float>(world[2])};
		alternative.flags |= HitFlags_Edge | HitFlags_Tie;
		hits->push_back(alternative);
	}
}

}

double RayDistanceTolerance(double distance)
{
	return 1e-4 + 1e-5 * std::max(1.0, distance);
}

benchmark_ray::Status BuildRayReference(const benchmark_ray::Scene& scene, int updatedView, RayReference* reference)
{
	using namespace benchmark_ray;
	*reference = {};
	reference->scene = &scene;
	reference->hullPlanes.resize(scene.hulls.size());
	for (std::size_t index = 0; index < scene.hulls.size(); ++index)
		if (HullPlanes(scene, scene.hulls[index], &reference->hullPlanes[index]) != Status_Ok)
			return Status_Invalid;
	for (std::uint32_t index = 0; index < scene.colliders.size(); ++index)
	{
		const Collider& collider = scene.colliders[index];
		if (collider.shape > Shape_Mesh || collider.category == 0 ||
		    (collider.shape == Shape_Hull && collider.hull >= scene.hulls.size()) ||
		    collider.firstTriangle > scene.triangles.size() ||
		    collider.triangleCount > scene.triangles.size() - collider.firstTriangle)
			return Status_Invalid;
		const Pose pose =
		    updatedView < 0 ? collider.pose : UpdatedPose(collider, static_cast<std::uint32_t>(updatedView));
		reference->poses.push_back(pose);
		const std::uint32_t count = collider.shape == Shape_Mesh ? collider.triangleCount : 1;
		for (std::uint32_t child = 0; child < count; ++child)
		{
			RayReferenceBounds bounds = EmptyBounds();
			if (collider.shape == Shape_Mesh)
			{
				const Triangle& triangle = scene.triangles[collider.firstTriangle + child];
				for (Vector vertex : {triangle.a, triangle.b, triangle.c})
					Extend(&bounds, Add(Promote(pose.position), Rotate(pose.rotation, Promote(vertex))));
			}
			else
			{
				D half = Promote(collider.size);
				if (collider.shape == Shape_Sphere)
					half = {collider.size.x, collider.size.x, collider.size.x};
				if (collider.shape == Shape_Capsule)
					half = {collider.size.x, collider.size.x + collider.size.y, collider.size.x};
				for (std::uint32_t corner = 0; corner < 8; ++corner)
				{
					D p = {};
					for (std::size_t axis = 0; axis < 3; ++axis)
						p[axis] = (corner & (1u << axis)) != 0 ? half[axis] : -half[axis];
					Extend(&bounds, Add(Promote(pose.position), Rotate(pose.rotation, p)));
				}
			}
			for (std::size_t axis = 0; axis < 3; ++axis)
			{
				bounds.minimum[axis] -= 1e-7;
				bounds.maximum[axis] += 1e-7;
			}
			reference->leaves.push_back({bounds, index, collider.shape == Shape_Mesh ? child : kNoSource});
		}
	}
	if (!reference->leaves.empty())
		BuildNode(reference, 0, static_cast<std::uint32_t>(reference->leaves.size()));
	return Status_Ok;
}

benchmark_ray::Status TraceRayReference(const RayReference& reference, const benchmark_ray::Ray& ray,
                                        benchmark_ray::Phase phase, std::vector<benchmark_ray::Hit>* hits,
                                        std::uint32_t* nativeEventCount, int bruteForce)
{
	using namespace benchmark_ray;
	hits->clear();
	*nativeEventCount = 0;
	if (!(ray.length > 0) || !std::isfinite(ray.length))
		return Status_Invalid;
	std::array<std::uint32_t, 128> stack = {};
	std::size_t top = 0;
	if (!reference.nodes.empty())
		stack[top++] = 0;
	while (top != 0 || bruteForce != 0)
	{
		std::uint32_t first = 0, count = static_cast<std::uint32_t>(reference.leaves.size());
		if (bruteForce == 0)
		{
			const RayReferenceNode& node = reference.nodes[stack[--top]];
			if (Intersects(node.bounds, Promote(ray.origin), Promote(ray.translation)) == 0)
				continue;
			if (node.count == 0)
			{
				if (top + 2 > stack.size())
					return Status_Capacity;
				stack[top++] = node.first;
				stack[top++] = node.right;
				continue;
			}
			first = node.first;
			count = node.count;
		}
		for (std::uint32_t index = first; index < first + count; ++index)
		{
			Hit hit = {};
			if (LeafHit(reference, reference.leaves[index], ray, &hit) != 0)
			{
				++*nativeEventCount;
				hits->push_back(hit);
				AppendFeatureNormals(reference, ray, hit, hits);
			}
		}
		if (bruteForce != 0)
			break;
	}
	std::sort(hits->begin(), hits->end(),
	          [](const Hit& a, const Hit& b)
	          {
		          if (a.distance != b.distance)
			          return a.distance < b.distance;
		          if (a.collider != b.collider)
			          return a.collider < b.collider;
		          return a.source < b.source;
	          });
	if (IsEnumeration(phase) == 0 && !hits->empty())
	{
		const double distance = hits->front().distance;
		std::size_t count = 1;
		while (count < hits->size() && (*hits)[count].distance - distance <= RayDistanceTolerance(distance))
			++count;
		hits->resize(count);
		if (count > 1)
			for (Hit& hit : *hits)
				hit.flags |= HitFlags_Tie;
	}
	else if (phase == Phase_ColliderHits)
	{
		std::sort(hits->begin(), hits->end(),
		          [](const Hit& a, const Hit& b)
		          {
			          return a.collider != b.collider ? a.collider < b.collider : a.distance < b.distance;
		          });
		std::size_t retained = 0;
		std::uint32_t collider = 0;
		double nearest = 0;
		for (std::size_t index = 0; index < hits->size(); ++index)
		{
			Hit hit = (*hits)[index];
			if (hit.collider != collider)
			{
				collider = hit.collider;
				nearest = hit.distance;
			}
			if (hit.distance - nearest > RayDistanceTolerance(nearest))
				continue;
			if (retained != 0 && (*hits)[retained - 1].collider == hit.collider)
				hit.flags |= HitFlags_Tie;
			(*hits)[retained++] = hit;
		}
		hits->resize(retained);
	}
	return Status_Ok;
}
}
