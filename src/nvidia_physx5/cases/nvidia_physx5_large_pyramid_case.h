#pragma once

#include "nvidia_physx5_box_container_pile_10k_case.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace nvidia_physx5_benchmark
{
struct PhysXLargePyramidCaseState
{
	PhysXCaseConfig config;
	PhysXContext context;
	std::unique_ptr<physx::PxRigidDynamic*[]> dynamicBodies;
	std::unique_ptr<std::chrono::steady_clock::duration::rep[]> rawStepDurations;
	physx::PxRigidStatic* floorBody;
	int createdDynamicBodyCount;
	int completedStepCount;
	double physicsElapsedMs;
	double latestPhysicsStepMs;
};

const PhysXCaseDescriptor& PhysXLargePyramidCaseDescriptor();
}
