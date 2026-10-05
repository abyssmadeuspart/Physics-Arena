#include "box3d_case_registry.h"

#include "box3d_box_contact_islands_10k_case.h"
#include "box3d_box_container_pile_10k_case.h"
#include "box3d_large_pyramid_case.h"
#include "box3d_pyramid_wall_case.h"
#include "box3d_ray_tracing_case.h"
#include "box3d_ragdoll_stair_tumble_case.h"
#include "box3d_spatial_query_trace_case.h"

namespace box3d_benchmark
{
int BuildResolvedVisualGeometry(const CaseExecutionSpec& execution, const CaseExecutionGeometry& geometry,
                                benchmark_visual::VisualMeshStorage* meshes, benchmark_visual::VisualGeometry* visual)
{
	switch (geometry.shape)
	{
	case CaseExecutionShape_Box:
		*visual = {benchmark_visual::VisualGeometryKind_Box,
		           geometry.halfExtents.x,
		           geometry.halfExtents.y,
		           geometry.halfExtents.z,
		           0,
		           0,
		           0,
		           0,
		           0,
		           0};
		return 0;
	case CaseExecutionShape_Sphere:
		*visual = {benchmark_visual::VisualGeometryKind_Sphere, geometry.radius, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0};
		return 0;
	case CaseExecutionShape_Capsule:
		return benchmark_visual::AppendVisualCapsule(geometry.radius, geometry.halfSegment, meshes, visual);
	case CaseExecutionShape_ConvexHull:
	{
		benchmark_visual::VisualMeshVertex points[kCaseExecutionHullPointCount];
		for (uint32_t index = 0; index < kCaseExecutionHullPointCount; ++index)
			points[index] = {execution.hullPoints[index].x, execution.hullPoints[index].y,
			                 execution.hullPoints[index].z};
		return benchmark_visual::AppendVisualBeveledBox(points, geometry.halfExtents.x, geometry.halfExtents.y,
		                                                geometry.halfExtents.z, meshes, visual);
	}
	default:
		return 2;
	}
}

int CreateBox3DResolvedShape(const CaseExecutionGeometry& geometry, const CaseExecutionSpec& execution,
                             Box3DResolvedShape* shape)
{
	*shape = {};
	shape->kind = geometry.shape;
	const CaseExecutionQuaternion rotation = CaseExecutionAxisRotation(geometry.axis);
	shape->rotation = {{rotation.x, rotation.y, rotation.z}, rotation.w};
	if (geometry.shape == CaseExecutionShape_Box)
	{
		shape->box = b3MakeBoxHull(geometry.halfExtents.x, geometry.halfExtents.y, geometry.halfExtents.z);
		shape->unitMass = b3ComputeHullMass(&shape->box.base, 1.0f).mass;
	}
	else if (geometry.shape == CaseExecutionShape_Sphere)
	{
		shape->sphere = {{0.0f, 0.0f, 0.0f}, geometry.radius};
		shape->unitMass = b3ComputeSphereMass(&shape->sphere, 1.0f).mass;
	}
	else if (geometry.shape == CaseExecutionShape_Capsule)
	{
		shape->capsule = {{0.0f, -geometry.halfSegment, 0.0f}, {0.0f, geometry.halfSegment, 0.0f}, geometry.radius};
		shape->unitMass = b3ComputeCapsuleMass(&shape->capsule, 1.0f).mass;
	}
	else
	{
		b3Vec3 points[kCaseExecutionHullPointCount] = {};
		for (std::uint32_t index = 0; index < kCaseExecutionHullPointCount; ++index)
		{
			const CaseExecutionVector3 point = execution.hullPoints[index];
			points[index] = {point.x * geometry.halfExtents.x, point.y * geometry.halfExtents.y,
			                 point.z * geometry.halfExtents.z};
		}
		shape->hull = b3CreateHull(points, kCaseExecutionHullPointCount, kCaseExecutionHullPointCount);
		if (shape->hull == nullptr)
			return 2;
		shape->unitMass = b3ComputeHullMass(shape->hull, 1.0f).mass;
	}
	if (!std::isfinite(shape->unitMass) || shape->unitMass <= 0.0f)
	{
		DestroyBox3DResolvedShape(shape);
		return 2;
	}
	return 0;
}

void DestroyBox3DResolvedShape(Box3DResolvedShape* shape)
{
	if (shape->hull != nullptr)
		b3DestroyHull(shape->hull);
	shape->hull = nullptr;
}

int AttachBox3DResolvedShape(b3BodyId body, const b3ShapeDef& definition, const Box3DResolvedShape& shape)
{
	if (!b3Body_IsValid(body))
		return 2;
	b3ShapeId id = {};
	if (shape.kind == CaseExecutionShape_Sphere)
		id = b3CreateSphereShape(body, &definition, &shape.sphere);
	else if (shape.kind == CaseExecutionShape_Capsule)
		id = b3CreateCapsuleShape(body, &definition, &shape.capsule);
	else
		id = b3CreateHullShape(body, &definition, shape.kind == CaseExecutionShape_Box ? &shape.box.base : shape.hull);
	return b3Shape_IsValid(id) ? 0 : 2;
}

const Box3DCaseDescriptor* const CaseRegistrations[] = {
    &Box3DContainerPileCaseDescriptor(),     &Box3DContactIslandsCaseDescriptor(),
    &Box3DSpatialQueryTraceCaseDescriptor(), &Box3DRagdollStairTumbleCaseDescriptor(),
    &Box3DLargePyramidCaseDescriptor(),
    &Box3DPyramidWallCaseDescriptor(),
    &Box3DRayTracingCaseDescriptor(),
};

int ResolveBox3DCase(const CaseExecutionSpec& execution, const Box3DCaseDescriptor** descriptor)
{
	const CaseFixtureKind fixtureKind = execution.fixtureKind;
	if (CaseExecutionIsQuery(fixtureKind) == 0 &&
	    (execution.nativeSolver.supportedFields != 16u ||
	     execution.nativeSolver.values[CaseSolverField_Substeps] < 1u ||
	     execution.nativeSolver.values[CaseSolverField_Substeps] > 2147483647u))
		return 2;
	if (descriptor == nullptr || fixtureKind < CaseFixtureKind_OpenContainerFallingPile ||
	    fixtureKind > CaseFixtureKind_RayTracing)
	{
		return 2;
	}
	*descriptor = CaseRegistrations[static_cast<std::size_t>(fixtureKind) - 1];
	return 0;
}

const Box3DCaseDescriptor& DefaultBox3DCase()
{
	return *CaseRegistrations[0];
}

int RunBox3DCase(const Box3DRunRequest& request, const Box3DCaseDescriptor& descriptor)
{
	return descriptor.runCase(request);
}

}
