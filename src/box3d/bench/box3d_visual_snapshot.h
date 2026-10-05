#pragma once

#include "stack_state_capture.h"

#include "box3d_case_registry.h"
#include "box3d_runner_args.h"

namespace box3d_benchmark
{
int RecordBox3DCase(const Box3DRunRequest& request, Box3DCaseView* state, benchmark_stack::Capture* capture = nullptr);
}
