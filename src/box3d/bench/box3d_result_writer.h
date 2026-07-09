#pragma once

#include "box3d_case_registry.h"
#include "box3d_runner_args.h"

namespace box3d_benchmark
{
int WriteBox3DResultRow(const Box3DRunRequest& request, const Box3DCaseDescriptor& descriptor, const Box3DCaseState& state);
}
