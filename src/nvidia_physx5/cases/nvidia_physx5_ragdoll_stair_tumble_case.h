#pragma once

#include "nvidia_physx5_runner_args.h"

#include "nvidia_physx5_case_registry.h"
#include "nvidia_physx5_box_container_pile_10k_case.h"
#include "PxPhysicsAPI.h"
#include "extensions/PxSphericalJoint.h"

#include "ragdoll_quality.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace nvidia_physx5_benchmark
{
struct PhysXRunRequest;

struct PhysXRagdollCaseState
{
	VerificationMode verificationMode = VerificationMode_On;
	PhysXCaseConfig config;
	PhysXContext context;
	std::unique_ptr<physx::PxRigidDynamic*[]> dynamicBodies;
	std::unique_ptr<physx::PxRigidStatic*[]> staticBodies;
	std::unique_ptr<physx::PxSphericalJoint*[]> joints;
	std::unique_ptr<benchmark_visual::VisualStableTransform[]> qualityTransforms;
	RagdollQualityAccumulator quality;
	int createdDynamicBodyCount;
	int createdStaticBodyCount;
	int createdJointCount;
	int completedStepCount;
};

const PhysXCaseDescriptor& PhysXRagdollStairTumbleCaseDescriptor();
}
