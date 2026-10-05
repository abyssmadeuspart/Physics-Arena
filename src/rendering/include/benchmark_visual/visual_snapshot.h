#pragma once

#include "benchmark_visual/visual_camera.h"

#include <stdint.h>
#include <cmath>
#include <vector>

namespace benchmark_visual
{
constexpr uint32_t kMaxTransforms = 16290u;
constexpr int kVisualBridgeMaxTransforms = static_cast<int>(kMaxTransforms);
constexpr int kVisualBridgeTextCapacity = 64;
constexpr uint32_t kMaxSceneGeometries = 64u;
constexpr uint32_t kMaxSceneInstances = 16291u;
constexpr uint32_t kMaxDebugPrimitives = 768u;
constexpr uint32_t kMaxSceneVertices = 8192u;
constexpr uint32_t kMaxSceneIndices = 49152u;
constexpr uint32_t kMaxSceneEdges = 24576u;

enum VisualBridgeStatus
{
	VisualBridgeStatus_Ok = 0,
	VisualBridgeStatus_CapacityExceeded = 2,
	VisualBridgeStatus_InvalidArgument = 3,
	VisualBridgeStatus_UnsupportedCase = 4,
	VisualBridgeStatus_ProducerFailed = 5,
};

enum VisualRendererStatus
{
	VisualRendererStatus_NotStarted = 0,
	VisualRendererStatus_FrameSubmitted = 1,
	VisualRendererStatus_WindowHidden = 2,
	VisualRendererStatus_RenderSkipped = 3,
};

struct VisualRunConfig
{
	char caseId[kVisualBridgeTextCapacity];
	char engineId[kVisualBridgeTextCapacity];
	uint32_t threadCount;
	uint32_t repeatIndex;
	uint32_t stepCount;
	uint32_t warmupSteps;
};

struct VisualRunIdentity
{
	char caseId[kVisualBridgeTextCapacity];
	char engineId[kVisualBridgeTextCapacity];
	char fixtureSemantic[kVisualBridgeTextCapacity];
	char fixtureVersion[kVisualBridgeTextCapacity];
	int threadCount;
	int repeatIndex;
	int stepCount;
	int warmupSteps;
	int bodyCount;
	int shapeCount;
};

int IsUntimedVisualCase(const VisualRunIdentity& identity);

struct VisualTransform
{
	float positionX;
	float positionY;
	float positionZ;
	float rotationX;
	float rotationY;
	float rotationZ;
	float rotationW;
};

enum VisualGeometryKind : uint32_t
{
	VisualGeometryKind_Sphere = 1u,
	VisualGeometryKind_Box = 2u,
	VisualGeometryKind_IndexedTriangles = 3u,
};

struct VisualMeshVertex
{
	float x;
	float y;
	float z;
};

struct VisualMeshEdge
{
	uint32_t a;
	uint32_t b;
};

struct VisualMeshStorage
{
	std::vector<VisualMeshVertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<VisualMeshEdge> edges;
};

struct VisualGeometry
{
	uint32_t kind;
	float parameterX;
	float parameterY;
	float parameterZ;
	uint32_t vertexOffset;
	uint32_t vertexCount;
	uint32_t indexOffset;
	uint32_t indexCount;
	uint32_t edgeOffset;
	uint32_t edgeCount;
};

inline int ReserveVisualMesh(VisualMeshStorage* storage, uint32_t vertices, uint32_t indices, uint32_t edges,
                             VisualGeometry* geometry)
{
	if (storage == nullptr || geometry == nullptr || storage->vertices.size() + vertices > kMaxSceneVertices ||
	    storage->indices.size() + indices > kMaxSceneIndices || storage->edges.size() + edges > kMaxSceneEdges)
		return VisualBridgeStatus_CapacityExceeded;
	*geometry = {VisualGeometryKind_IndexedTriangles,
	             0.0f,
	             0.0f,
	             0.0f,
	             static_cast<uint32_t>(storage->vertices.size()),
	             vertices,
	             static_cast<uint32_t>(storage->indices.size()),
	             indices,
	             static_cast<uint32_t>(storage->edges.size()),
	             edges};
	storage->vertices.resize(storage->vertices.size() + vertices);
	storage->indices.resize(storage->indices.size() + indices);
	storage->edges.resize(storage->edges.size() + edges);
	return VisualBridgeStatus_Ok;
}

inline int AppendVisualCapsule(float radius, float halfSegment, VisualMeshStorage* storage, VisualGeometry* geometry)
{
	if (std::isfinite(radius) == 0 || std::isfinite(halfSegment) == 0 || radius <= 0.0f || halfSegment <= 0.0f)
		return VisualBridgeStatus_InvalidArgument;
	constexpr uint32_t longitudeCount = 16;
	constexpr uint32_t ringCount = 8;
	constexpr uint32_t vertexCount = 2 + longitudeCount * ringCount;
	constexpr uint32_t indexCount = longitudeCount * ringCount * 6;
	constexpr uint32_t edgeCount = longitudeCount * (ringCount * 2 + 1);
	if (ReserveVisualMesh(storage, vertexCount, indexCount, edgeCount, geometry) != 0)
		return VisualBridgeStatus_CapacityExceeded;
	VisualMeshVertex* vertices = storage->vertices.data() + geometry->vertexOffset;
	uint32_t* indices = storage->indices.data() + geometry->indexOffset;
	VisualMeshEdge* edges = storage->edges.data() + geometry->edgeOffset;
	vertices[0] = {0.0f, halfSegment + radius, 0.0f};
	vertices[vertexCount - 1] = {0.0f, -halfSegment - radius, 0.0f};
	for (uint32_t ring = 0; ring < ringCount; ++ring)
	{
		const float latitude = static_cast<float>(ring < 4 ? ring + 1 : ring) * 3.14159265358979323846f / 8.0f;
		const float y = (ring < 4 ? halfSegment : -halfSegment) + radius * std::cos(latitude);
		const float radial = radius * std::sin(latitude);
		for (uint32_t longitude = 0; longitude < longitudeCount; ++longitude)
		{
			const float angle = static_cast<float>(longitude) * 6.28318530717958647692f / longitudeCount;
			vertices[1 + ring * longitudeCount + longitude] = {radial * std::cos(angle), y, radial * std::sin(angle)};
		}
	}
	uint32_t indexCursor = 0;
	uint32_t edgeCursor = 0;
	for (uint32_t longitude = 0; longitude < longitudeCount; ++longitude)
	{
		const uint32_t next = (longitude + 1) % longitudeCount;
		indices[indexCursor++] = 0;
		indices[indexCursor++] = 1 + next;
		indices[indexCursor++] = 1 + longitude;
		edges[edgeCursor++] = {0, 1 + longitude};
		for (uint32_t ring = 0; ring < ringCount; ++ring)
		{
			const uint32_t current = 1 + ring * longitudeCount;
			edges[edgeCursor++] = {current + longitude, current + next};
			if (ring + 1 < ringCount)
			{
				const uint32_t lower = current + longitudeCount;
				indices[indexCursor++] = current + longitude;
				indices[indexCursor++] = current + next;
				indices[indexCursor++] = lower + longitude;
				indices[indexCursor++] = current + next;
				indices[indexCursor++] = lower + next;
				indices[indexCursor++] = lower + longitude;
				edges[edgeCursor++] = {current + longitude, lower + longitude};
			}
		}
		const uint32_t last = 1 + (ringCount - 1) * longitudeCount;
		indices[indexCursor++] = last + longitude;
		indices[indexCursor++] = last + next;
		indices[indexCursor++] = vertexCount - 1;
		edges[edgeCursor++] = {last + longitude, vertexCount - 1};
	}
	return indexCursor == indexCount && edgeCursor == edgeCount ? VisualBridgeStatus_Ok
	                                                            : VisualBridgeStatus_ProducerFailed;
}

inline int AppendVisualBeveledBox(const VisualMeshVertex* canonicalPoints, float halfX, float halfY, float halfZ,
                                  VisualMeshStorage* storage, VisualGeometry* geometry)
{
	if (canonicalPoints == nullptr || ReserveVisualMesh(storage, 24, 132, 36, geometry) != 0)
		return VisualBridgeStatus_InvalidArgument;
	VisualMeshVertex* vertices = storage->vertices.data() + geometry->vertexOffset;
	uint32_t* indices = storage->indices.data() + geometry->indexOffset;
	VisualMeshEdge* edges = storage->edges.data() + geometry->edgeOffset;
	for (uint32_t index = 0; index < 24; ++index)
		vertices[index] = {canonicalPoints[index].x * halfX, canonicalPoints[index].y * halfY,
		                   canonicalPoints[index].z * halfZ};
	uint32_t indexCursor = 0;
	uint32_t edgeCursor = 0;
	for (uint32_t face = 0; face < 14; ++face)
	{
		uint32_t corners[8] = {};
		uint32_t count = 0;
		if (face < 6)
		{
			const uint32_t axis = face / 2;
			const float sign = (face & 1) != 0 ? 1.0f : -1.0f;
			float angles[8] = {};
			for (uint32_t index = 0; index < 24; ++index)
			{
				const float point[3] = {canonicalPoints[index].x, canonicalPoints[index].y, canonicalPoints[index].z};
				if (point[axis] != sign)
					continue;
				if (count == 8)
					return VisualBridgeStatus_InvalidArgument;
				const float angle = sign * std::atan2(point[(axis + 2) % 3], point[(axis + 1) % 3]);
				uint32_t insertion = count++;
				while (insertion != 0 && angles[insertion - 1] > angle)
				{
					corners[insertion] = corners[insertion - 1];
					angles[insertion] = angles[insertion - 1];
					--insertion;
				}
				corners[insertion] = index;
				angles[insertion] = angle;
			}
			if (count != 8)
				return VisualBridgeStatus_InvalidArgument;
		}
		else
		{
			const uint32_t signs = face - 6;
			const uint32_t positiveCount = (signs & 1) + ((signs >> 1) & 1) + ((signs >> 2) & 1);
			corners[0] = signs;
			corners[1] = (positiveCount & 1) != 0 ? 8 + signs : 16 + signs;
			corners[2] = (positiveCount & 1) != 0 ? 16 + signs : 8 + signs;
			count = 3;
		}
		for (uint32_t corner = 1; corner + 1 < count; ++corner)
		{
			indices[indexCursor++] = corners[0];
			indices[indexCursor++] = corners[corner];
			indices[indexCursor++] = corners[corner + 1];
		}
		for (uint32_t corner = 0; corner < count; ++corner)
		{
			uint32_t a = corners[corner];
			uint32_t b = corners[(corner + 1) % count];
			if (a > b)
			{
				const uint32_t swap = a;
				a = b;
				b = swap;
			}
			uint32_t edge = 0;
			while (edge < edgeCursor && (edges[edge].a != a || edges[edge].b != b))
				++edge;
			if (edge != edgeCursor)
				continue;
			if (edgeCursor == 36)
				return VisualBridgeStatus_InvalidArgument;
			edges[edgeCursor++] = {a, b};
		}
	}
	return indexCursor == 132 && edgeCursor == 36 ? VisualBridgeStatus_Ok : VisualBridgeStatus_ProducerFailed;
}

struct VisualInstance
{
	uint32_t geometryIndex;
	uint32_t stableSlot;
	uint32_t materialIndex;
	uint32_t transformSlot;
	VisualTransform initialTransform;
	uint32_t reserved[8];
};

struct VisualStableTransform
{
	uint32_t stableSlot;
	VisualTransform transform;
};

enum VisualDebugPrimitiveKind : uint32_t
{
	VisualDebugPrimitiveKind_Ray = 0u,
	VisualDebugPrimitiveKind_SphereCast = 1u,
	VisualDebugPrimitiveKind_AabbOverlap = 2u,
};

struct VisualDebugPrimitive
{
	uint32_t kind;
	uint32_t materialIndex;
	float originOrCenterX;
	float originOrCenterY;
	float originOrCenterZ;
	float endOrHalfExtentsX;
	float endOrHalfExtentsY;
	float endOrHalfExtentsZ;
	float radius;
	uint32_t reserved;
};

struct VisualScene
{
	VisualRunIdentity identity;
	const VisualGeometry* geometries;
	const VisualInstance* instances;
	uint32_t geometryCount;
	uint32_t instanceCount;
	uint32_t dynamicTransformCount;
	uint32_t debugPrimitiveCount;
	VisualCameraPolicy camera;
	const VisualMeshVertex* vertices;
	const uint32_t* indices;
	const VisualMeshEdge* edges;
	uint32_t vertexCount;
	uint32_t indexCount;
	uint32_t edgeCount;
};

struct VisualRendererDiagnostics
{
	int rendererStatus;
	double frameElapsedMs;
};

struct VisualTiming
{
	double physicsElapsedMs;
	double physicsStepMs;
};

struct VisualSnapshot
{
	VisualRunIdentity identity;
	const VisualScene* scene;
	VisualStableTransform* transforms;
	int transformCapacity;
	int transformCount;
	VisualDebugPrimitive* debugPrimitives;
	int debugPrimitiveCapacity;
	int debugPrimitiveCount;
	int stepIndex;
	int completedStepCount;
	double physicsElapsedMs;
	double physicsStepMs;
	const uint32_t* timingStepIndexes;
	const double* timingPhysicsStepMs;
	const double* timingRenderFrameMs;
	int timingSampleCount;
	VisualRendererDiagnostics renderer;
	int status;
};

int ValidateSnapshot(VisualSnapshot snapshot);
int ValidateScene(const VisualScene* scene);

static_assert(sizeof(VisualGeometry) == 40);
static_assert(sizeof(VisualInstance) == 76);
static_assert(sizeof(VisualStableTransform) == 32);
static_assert(sizeof(VisualDebugPrimitive) == 40);
static_assert(sizeof(VisualMeshVertex) == 12);
static_assert(sizeof(VisualMeshEdge) == 8);
}
