#include "benchmark_visual/visual_snapshot.h"

#include <array>
#include <cmath>
#include <cstring>

namespace benchmark_visual
{
int IsUntimedVisualCase(const VisualRunIdentity& identity)
{
	return std::strcmp(identity.caseId, "ragdoll_stair_tumble") == 0 ||
	               std::strcmp(identity.caseId, "ragdoll_stair_tumble_sphere") == 0 ||
	               std::strcmp(identity.caseId, "ragdoll_stair_tumble_capsule") == 0 ||
	               std::strcmp(identity.caseId, "ragdoll_stair_tumble_convex_hull") == 0
	           ? 1
			   : 0;
}

int ValidateScene(const VisualScene* scene)
{
	if (scene == nullptr || scene->geometries == nullptr || scene->instances == nullptr || scene->geometryCount == 0 ||
	    scene->geometryCount > kMaxSceneGeometries || scene->instanceCount == 0 ||
	    scene->instanceCount > kMaxSceneInstances || scene->dynamicTransformCount > kMaxTransforms ||
	    scene->dynamicTransformCount > scene->instanceCount || scene->debugPrimitiveCount > kMaxDebugPrimitives ||
	    scene->vertexCount > kMaxSceneVertices || scene->indexCount > kMaxSceneIndices ||
	    scene->edgeCount > kMaxSceneEdges || (scene->vertexCount != 0 && scene->vertices == nullptr) ||
	    (scene->indexCount != 0 && scene->indices == nullptr) || (scene->edgeCount != 0 && scene->edges == nullptr) ||
	    std::memchr(scene->identity.caseId, '\0', sizeof(scene->identity.caseId)) == nullptr ||
	    std::memchr(scene->identity.engineId, '\0', sizeof(scene->identity.engineId)) == nullptr ||
	    std::memchr(scene->identity.fixtureSemantic, '\0', sizeof(scene->identity.fixtureSemantic)) == nullptr ||
	    std::memchr(scene->identity.fixtureVersion, '\0', sizeof(scene->identity.fixtureVersion)) == nullptr ||
	    scene->identity.caseId[0] == '\0' || scene->identity.engineId[0] == '\0' ||
	    scene->identity.fixtureSemantic[0] == '\0' || scene->identity.fixtureVersion[0] == '\0' ||
	    scene->identity.threadCount <= 0 || scene->identity.repeatIndex < 0 || scene->identity.stepCount <= 0 ||
	    scene->identity.bodyCount <= 0 || scene->identity.shapeCount <= 0)
		return VisualBridgeStatus_InvalidArgument;
	if (ValidateVisualCameraPolicy(&scene->camera) != VisualCameraStatus_Ok)
		return VisualBridgeStatus_InvalidArgument;
	for (uint32_t geometryIndex = 0; geometryIndex < scene->geometryCount; ++geometryIndex)
	{
		const VisualGeometry& geometry = scene->geometries[geometryIndex];
		if (geometry.kind == VisualGeometryKind_Sphere)
		{
			if (std::isfinite(geometry.parameterX) == 0 || geometry.parameterX <= 0.0f || geometry.parameterY != 0.0f ||
			    geometry.parameterZ != 0.0f)
				return VisualBridgeStatus_InvalidArgument;
		}
		else if (geometry.kind == VisualGeometryKind_Box)
		{
			if (std::isfinite(geometry.parameterX) == 0 || std::isfinite(geometry.parameterY) == 0 ||
			    std::isfinite(geometry.parameterZ) == 0 || geometry.parameterX <= 0.0f || geometry.parameterY <= 0.0f ||
			    geometry.parameterZ <= 0.0f)
				return VisualBridgeStatus_InvalidArgument;
		}
		else if (geometry.kind == VisualGeometryKind_IndexedTriangles)
		{
			if (geometry.parameterX != 0.0f || geometry.parameterY != 0.0f || geometry.parameterZ != 0.0f ||
			    geometry.vertexCount < 4 || geometry.indexCount < 12 || geometry.indexCount % 3 != 0 ||
			    geometry.edgeCount < 6 || geometry.vertexOffset > scene->vertexCount ||
			    geometry.vertexCount > scene->vertexCount - geometry.vertexOffset ||
			    geometry.indexOffset > scene->indexCount ||
			    geometry.indexCount > scene->indexCount - geometry.indexOffset ||
			    geometry.edgeOffset > scene->edgeCount || geometry.edgeCount > scene->edgeCount - geometry.edgeOffset)
				return VisualBridgeStatus_InvalidArgument;
			for (uint32_t index = 0; index < geometry.vertexCount; ++index)
			{
				const VisualMeshVertex& vertex = scene->vertices[geometry.vertexOffset + index];
				if (std::isfinite(vertex.x) == 0 || std::isfinite(vertex.y) == 0 || std::isfinite(vertex.z) == 0)
					return VisualBridgeStatus_InvalidArgument;
			}
			for (uint32_t index = 0; index < geometry.indexCount; index += 3)
			{
				const uint32_t a = scene->indices[geometry.indexOffset + index];
				const uint32_t b = scene->indices[geometry.indexOffset + index + 1];
				const uint32_t c = scene->indices[geometry.indexOffset + index + 2];
				if (a >= geometry.vertexCount || b >= geometry.vertexCount || c >= geometry.vertexCount || a == b ||
				    b == c || a == c)
					return VisualBridgeStatus_InvalidArgument;
				const VisualMeshVertex& p = scene->vertices[geometry.vertexOffset + a];
				const VisualMeshVertex& q = scene->vertices[geometry.vertexOffset + b];
				const VisualMeshVertex& r = scene->vertices[geometry.vertexOffset + c];
				const double ux = static_cast<double>(q.x) - p.x, uy = static_cast<double>(q.y) - p.y,
				             uz = static_cast<double>(q.z) - p.z;
				const double vx = static_cast<double>(r.x) - p.x, vy = static_cast<double>(r.y) - p.y,
				             vz = static_cast<double>(r.z) - p.z;
				if (uy * vz == uz * vy && uz * vx == ux * vz && ux * vy == uy * vx)
					return VisualBridgeStatus_InvalidArgument;
			}
			for (uint32_t index = 0; index < geometry.edgeCount; ++index)
			{
				const VisualMeshEdge& edge = scene->edges[geometry.edgeOffset + index];
				if (edge.a >= geometry.vertexCount || edge.b >= geometry.vertexCount || edge.a == edge.b)
					return VisualBridgeStatus_InvalidArgument;
			}
			continue;
		}
		else
			return VisualBridgeStatus_InvalidArgument;
		if (geometry.vertexOffset != 0 || geometry.vertexCount != 0 || geometry.indexOffset != 0 ||
		    geometry.indexCount != 0 || geometry.edgeOffset != 0 || geometry.edgeCount != 0)
			return VisualBridgeStatus_InvalidArgument;
	}
	std::array<uint64_t, (kVisualBridgeMaxTransforms + 63) / 64> transformSlots = {};
	uint32_t dynamicCount = 0;
	for (uint32_t instanceIndex = 0; instanceIndex < scene->instanceCount; ++instanceIndex)
	{
		const VisualInstance& instance = scene->instances[instanceIndex];
		if (instance.geometryIndex >= scene->geometryCount ||
		    (instance.transformSlot != UINT32_MAX && instance.transformSlot >= scene->dynamicTransformCount))
			return VisualBridgeStatus_InvalidArgument;
		for (uint32_t reserved : instance.reserved)
			if (reserved != 0)
				return VisualBridgeStatus_InvalidArgument;
		if (instance.transformSlot != UINT32_MAX)
		{
			const uint32_t word = instance.transformSlot / 64;
			const uint64_t mask = uint64_t{1} << (instance.transformSlot % 64);
			if ((transformSlots[word] & mask) != 0)
				return VisualBridgeStatus_InvalidArgument;
			transformSlots[word] |= mask;
			++dynamicCount;
		}
		const VisualTransform& transform = instance.initialTransform;
		if (std::isfinite(transform.positionX) == 0 || std::isfinite(transform.positionY) == 0 ||
		    std::isfinite(transform.positionZ) == 0 || std::isfinite(transform.rotationX) == 0 ||
		    std::isfinite(transform.rotationY) == 0 || std::isfinite(transform.rotationZ) == 0 ||
		    std::isfinite(transform.rotationW) == 0)
			return VisualBridgeStatus_InvalidArgument;
	}
	if (dynamicCount != scene->dynamicTransformCount)
		return VisualBridgeStatus_InvalidArgument;
	return VisualBridgeStatus_Ok;
}

int ValidateSnapshot(VisualSnapshot snapshot)
{
	if (snapshot.transformCount < 0 || snapshot.transformCount > kVisualBridgeMaxTransforms)
	{
		return VisualBridgeStatus_CapacityExceeded;
	}

	if (snapshot.transformCapacity < snapshot.transformCount)
	{
		return VisualBridgeStatus_CapacityExceeded;
	}

	if (snapshot.debugPrimitiveCount < 0 || snapshot.debugPrimitiveCount > static_cast<int>(kMaxDebugPrimitives) ||
	    snapshot.debugPrimitiveCapacity < snapshot.debugPrimitiveCount)
	{
		return VisualBridgeStatus_CapacityExceeded;
	}

	if ((snapshot.transformCount != 0 && snapshot.transforms == nullptr) || snapshot.stepIndex < 0 ||
	    (snapshot.debugPrimitiveCount != 0 && snapshot.debugPrimitives == nullptr) || snapshot.identity.stepCount < 0 ||
	    ValidateScene(snapshot.scene) != VisualBridgeStatus_Ok)
	{
		return VisualBridgeStatus_InvalidArgument;
	}

	if (snapshot.identity.threadCount <= 0 || snapshot.identity.repeatIndex < 0 || snapshot.identity.bodyCount <= 0 ||
	    snapshot.identity.shapeCount <= 0 || snapshot.stepIndex > snapshot.identity.stepCount)
	{
		return VisualBridgeStatus_InvalidArgument;
	}
	if ((snapshot.completedStepCount == 0 && snapshot.transformCount != 0) ||
	    (snapshot.completedStepCount != 0 &&
	     snapshot.transformCount != static_cast<int>(snapshot.scene->dynamicTransformCount)))
		return VisualBridgeStatus_InvalidArgument;
	if ((snapshot.completedStepCount == 0 && snapshot.debugPrimitiveCount != 0) ||
	    (snapshot.completedStepCount != 0 &&
	     snapshot.debugPrimitiveCount != static_cast<int>(snapshot.scene->debugPrimitiveCount)))
		return VisualBridgeStatus_InvalidArgument;

	if (snapshot.completedStepCount > 0 &&
	    (IsUntimedVisualCase(snapshot.identity) != 0
	         ? (!std::isnan(snapshot.physicsElapsedMs) || !std::isnan(snapshot.physicsStepMs) ||
			    snapshot.timingSampleCount != 0)
			 : (!std::isfinite(snapshot.physicsElapsedMs) || snapshot.physicsElapsedMs <= 0.0 ||
			    !std::isfinite(snapshot.physicsStepMs) || snapshot.physicsStepMs <= 0.0)))
	{
		return VisualBridgeStatus_InvalidArgument;
	}

	for (int index = 0; index < snapshot.transformCount; ++index)
	{
		VisualStableTransform stable = snapshot.transforms[index];
		if (stable.stableSlot >= snapshot.scene->dynamicTransformCount)
			return VisualBridgeStatus_InvalidArgument;
		VisualTransform transform = stable.transform;
		if (std::isfinite(transform.positionX) == 0 || std::isfinite(transform.positionY) == 0 ||
		    std::isfinite(transform.positionZ) == 0 || std::isfinite(transform.rotationX) == 0 ||
		    std::isfinite(transform.rotationY) == 0 || std::isfinite(transform.rotationZ) == 0 ||
		    std::isfinite(transform.rotationW) == 0)
		{
			return VisualBridgeStatus_InvalidArgument;
		}
	}
	if (snapshot.transformCount != 0)
		for (uint32_t instanceIndex = 0; instanceIndex < snapshot.scene->instanceCount; ++instanceIndex)
		{
			const VisualInstance& instance = snapshot.scene->instances[instanceIndex];
			if (instance.transformSlot != UINT32_MAX &&
			    snapshot.transforms[instance.transformSlot].stableSlot != instance.stableSlot)
				return VisualBridgeStatus_InvalidArgument;
		}

	for (int index = 0; index < snapshot.debugPrimitiveCount; ++index)
	{
		const VisualDebugPrimitive& primitive = snapshot.debugPrimitives[index];
		if ((primitive.kind != VisualDebugPrimitiveKind_Ray && primitive.kind != VisualDebugPrimitiveKind_SphereCast &&
		     primitive.kind != VisualDebugPrimitiveKind_AabbOverlap) ||
		    (primitive.materialIndex != 6u && primitive.materialIndex != 7u) ||
		    std::isfinite(primitive.originOrCenterX) == 0 || std::isfinite(primitive.originOrCenterY) == 0 ||
		    std::isfinite(primitive.originOrCenterZ) == 0 || std::isfinite(primitive.endOrHalfExtentsX) == 0 ||
		    std::isfinite(primitive.endOrHalfExtentsY) == 0 || std::isfinite(primitive.endOrHalfExtentsZ) == 0 ||
		    std::isfinite(primitive.radius) == 0 || primitive.reserved != 0u)
			return VisualBridgeStatus_InvalidArgument;
		if ((primitive.kind == VisualDebugPrimitiveKind_SphereCast && primitive.radius <= 0.0f) ||
		    (primitive.kind != VisualDebugPrimitiveKind_SphereCast && primitive.radius != 0.0f))
			return VisualBridgeStatus_InvalidArgument;
	}

	return VisualBridgeStatus_Ok;
}
}
