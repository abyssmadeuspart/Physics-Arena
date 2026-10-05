#pragma once

#include "nvidia_physx5_box_container_pile_10k_case.h"

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace nvidia_physx5_benchmark
{
struct PhysXSpatialQuery
{
	float originX;
	float originY;
	float originZ;
	float directionX;
	float directionY;
	float directionZ;
};

struct PhysXRayInput
{
	physx::PxVec3 origin;
	physx::PxVec3 direction;
};

struct PhysXSphereCastInput
{
	physx::PxTransform pose;
	physx::PxVec3 direction;
};

struct PhysXSpatialQueryCompletion;
struct PhysXSpatialQueryLaneTask;

struct PhysXSpatialQueryState
{
	PhysXCaseConfig config;
	PhysXContext context;
	physx::PxRigidStatic** bodies;
	physx::PxQueryFilterData closestFilter = physx::PxQueryFilterData();
	physx::PxQueryFilterData overlapFilter = physx::PxQueryFilterData();
	physx::PxSphereGeometry sphere = physx::PxSphereGeometry();
	physx::PxBoxGeometry overlapBox = physx::PxBoxGeometry();
	PhysXRayInput* rayInputs;
	PhysXSphereCastInput* sphereCastInputs;
	physx::PxTransform* overlapInputs;
	std::uint8_t* debugHits;
	float* debugHitDistances;
	std::chrono::steady_clock::duration::rep* rawBatchDurations;
	PhysXSpatialQueryCompletion* queryCompletion;
	PhysXSpatialQueryLaneTask* laneTasks;
	std::uint64_t* laneHitCounts;
	std::chrono::steady_clock::duration::rep rayElapsed;
	std::chrono::steady_clock::duration::rep sphereCastElapsed;
	std::chrono::steady_clock::duration::rep overlapElapsed;
	double workloadElapsedMs;
	double latestBatchElapsedMs;
	std::uint64_t rayHitCount;
	std::uint64_t sphereCastHitCount;
	std::uint64_t overlapHitCount;
	int createdBodyCount;
	int completedBatchCount;
};

const PhysXCaseDescriptor& PhysXSpatialQueryTraceCaseDescriptor();
}
