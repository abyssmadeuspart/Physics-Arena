#pragma once

#include "jolt_box_container_pile_10k_case.h"

#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace jolt_benchmark
{
struct JoltSpatialQuery
{
	float originX;
	float originY;
	float originZ;
	float directionX;
	float directionY;
	float directionZ;
};

struct JoltSpatialQueryState
{
	JoltCaseConfig config;
	BPLayerInterfaceImpl broadPhaseLayerInterface;
	ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
	ObjectLayerPairFilterImpl objectVsObjectLayerFilter;
	JPH::PhysicsSystem physicsSystem;
	JPH::RefConst<JPH::Shape> sphereShape;
	JPH::ShapeCastSettings castSettings;
	JPH::BodyID* bodies;
	JPH::RRayCast* rayInputs;
	JPH::RShapeCast* sphereCastInputs;
	JPH::AABox* overlapInputs;
	std::uint8_t* debugHits;
	float* debugHitFractions;
	std::chrono::steady_clock::duration::rep* rawBatchDurations;
	JPH::JobSystemSingleThreaded* singleThreaded;
	JPH::JobSystemThreadPool* threadPool;
	JPH::JobSystem* jobSystem;
	JPH::JobSystem::Barrier* queryBarrier;
	JPH::JobSystem::JobHandle* jobHandles;
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
	int createdSphereCastInputCount;
	int completedBatchCount;
};

const JoltCaseDescriptor& JoltSpatialQueryTraceCaseDescriptor();
}
