#pragma once

#include "physx34_runner_args.h"

#include "physx34_case_registry.h"
#include "physx34_box_container_pile_10k_case.h"
#include "PxPhysicsAPI.h"
#include "extensions/PxSphericalJoint.h"

#include "ragdoll_quality.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace physx34_benchmark
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
