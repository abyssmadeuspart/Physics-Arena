#pragma once

#include "jolt_case_registry.h"
#include "jolt_runner_args.h"

namespace jolt_benchmark
{
int WriteJoltResultRow(const JoltRunRequest& request, const JoltCaseDescriptor& descriptor, const JoltCaseState& state);
}
