#pragma once

#include "nvidia_physx5_runner_args.h"

#include "nvidia_physx5_box_container_pile_10k_case.h"

#include "pyramid_wall.h"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace nvidia_physx5_benchmark
{
struct PhysXWallBodyInput
{
	physx::PxTransform pose;
	physx::PxQuat inertiaRotation;
	physx::PxVec3 linear;
	physx::PxVec3 angular;
	physx::PxVec3 inertia;
	float mass;
	float sleepThreshold;
	std::uint32_t sleepFlags;
};
struct PhysXPyramidWallCaseState
{
	VerificationMode verificationMode = VerificationMode_On;
	PhysXCaseConfig config;
	PhysXContext context;
	std::unique_ptr<physx::PxRigidDynamic*[]> dynamicBodies;
	std::unique_ptr<std::chrono::steady_clock::duration::rep[]> rawStepDurations;
	physx::PxRigidStatic* floorBody;
	int createdDynamicBodyCount;
	int completedStepCount;
	std::array<PyramidWallObservation, 4> observations;
	std::array<std::vector<PhysXWallBodyInput>, 4> observationInputs;
	double initialPotentialEnergy;
	double physicsElapsedMs;
	double latestPhysicsStepMs;
};

const PhysXCaseDescriptor& PhysXPyramidWallCaseDescriptor();
}
