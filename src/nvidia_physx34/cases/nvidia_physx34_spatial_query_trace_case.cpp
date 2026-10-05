#include "nvidia_physx34_spatial_query_trace_case.h"

#include "nvidia_physx34_result_writer.h"
#include "nvidia_physx34_runner_args.h"

#define PHYSICS_ARENA_PHYSX_NAMESPACE nvidia_physx34_benchmark
#include "../../common/physx_spatial_query_trace_case.inl"
#undef PHYSICS_ARENA_PHYSX_NAMESPACE
