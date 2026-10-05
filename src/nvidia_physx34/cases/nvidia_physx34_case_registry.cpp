#include "nvidia_physx34_case_registry.h"

#include "nvidia_physx34_box_contact_islands_10k_case.h"
#include "nvidia_physx34_box_container_pile_10k_case.h"
#include "nvidia_physx34_large_pyramid_case.h"
#include "nvidia_physx34_pyramid_wall_case.h"
#include "nvidia_physx34_ragdoll_stair_tumble_case.h"
#include "nvidia_physx34_spatial_query_trace_case.h"
#include "nvidia_physx34_runner_args.h"

#define PHYSICS_ARENA_PHYSX_NAMESPACE nvidia_physx34_benchmark
#include "../../common/physx_case_registry.inl"
#undef PHYSICS_ARENA_PHYSX_NAMESPACE
