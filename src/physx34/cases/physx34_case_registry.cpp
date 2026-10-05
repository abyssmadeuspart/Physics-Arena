#include "physx34_case_registry.h"

#include "physx34_box_contact_islands_10k_case.h"
#include "physx34_box_container_pile_10k_case.h"
#include "physx34_large_pyramid_case.h"
#include "physx34_pyramid_wall_case.h"
#include "physx34_ragdoll_stair_tumble_case.h"
#include "physx34_spatial_query_trace_case.h"
#include "physx34_runner_args.h"

#define PHYSICS_ARENA_PHYSX_NAMESPACE physx34_benchmark
#include "../../common/physx_case_registry.inl"
#undef PHYSICS_ARENA_PHYSX_NAMESPACE
