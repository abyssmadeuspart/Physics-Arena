#pragma once

#include "physx34_case_registry.h"
#include "physx34_runner_args.h"

namespace physx34_benchmark
{
int WritePhysXResultRow(const PhysXRunRequest& request, const PhysXCaseDescriptor& descriptor, const PhysXCaseState& state);
}
