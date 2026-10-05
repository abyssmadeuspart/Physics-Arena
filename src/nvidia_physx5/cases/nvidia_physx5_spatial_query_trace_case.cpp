#include "nvidia_physx5_spatial_query_trace_case.h"

#include "nvidia_physx5_result_writer.h"
#include "nvidia_physx5_runner_args.h"

#define PHYSICS_ARENA_PHYSX_NAMESPACE nvidia_physx5_benchmark
#define PHYSICS_ARENA_PHYSX5_API 1
#include "../../common/physx_spatial_query_trace_case.inl"
#undef PHYSICS_ARENA_PHYSX5_API
#undef PHYSICS_ARENA_PHYSX_NAMESPACE
