#include "physx34_spatial_query_trace_case.h"

#include "physx34_result_writer.h"
#include "physx34_runner_args.h"

#define PHYSICS_ARENA_PHYSX_NAMESPACE physx34_benchmark
#include "../../common/physx_spatial_query_trace_case.inl"
#undef PHYSICS_ARENA_PHYSX_NAMESPACE
