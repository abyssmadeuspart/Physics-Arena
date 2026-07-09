#pragma once

#include "nvidia_physx5_case_registry.h"
#include "nvidia_physx5_runner_args.h"

namespace nvidia_physx5_benchmark
{
int WritePhysXResultRow(const PhysXRunRequest& request, const PhysXCaseDescriptor& descriptor, const PhysXCaseState& state);
}
