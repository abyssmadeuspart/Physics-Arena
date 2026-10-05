#pragma once

#include "box3d_case_registry.h"
#include "box3d_query_executor.h"
#include "box3d/box3d.h"

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace box3d_benchmark
{
constexpr const char* kSpatialQueryEngineId = "box3d";

struct Box3DSpatialQuery
{
	float originX;
	float originY;
	float originZ;
	float directionX;
	float directionY;
	float directionZ;
};

struct Box3DRayInput
{
	b3Vec3 origin;
	b3Vec3 translation;
};

struct Box3DSphereCastInput
{
	b3Vec3 origin;
	b3Vec3 translation;
};


struct Box3DSpatialQueryState
{
	Box3DCaseConfig config;
	b3WorldId worldId;
	b3QueryFilter filter;
	b3Vec3 spherePoint;
	b3ShapeProxy sphere;
	Box3DRayInput* rayInputs;
	Box3DSphereCastInput* sphereCastInputs;
	b3AABB* overlapInputs;
	std::uint8_t* debugHits;
	float* debugHitFractions;
	std::chrono::steady_clock::duration::rep* rawBatchDurations;
	Box3DQueryExecutor* executor;
	std::chrono::steady_clock::duration::rep rayElapsed;
	std::chrono::steady_clock::duration::rep sphereCastElapsed;
	std::chrono::steady_clock::duration::rep overlapElapsed;
	double workloadElapsedMs;
	double latestBatchElapsedMs;
	std::uint64_t rayHitCount;
	std::uint64_t sphereCastHitCount;
	std::uint64_t overlapHitCount;
	int completedBatchCount;
};

const Box3DCaseDescriptor& Box3DSpatialQueryTraceCaseDescriptor();
}
