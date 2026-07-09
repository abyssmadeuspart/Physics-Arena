#pragma once

#include "nvidia_physx34_case_registry.h"
#include "nvidia_physx34_runner_args.h"

namespace nvidia_physx34_benchmark
{
int WritePhysXResultRow(const PhysXRunRequest& request, const PhysXCaseDescriptor& descriptor, const PhysXCaseState& state);
}
