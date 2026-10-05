#include "nvidia_physx5_ragdoll_stair_tumble_case.h"
#include "physx_engine_settings.h"

#include "nvidia_physx5_box_container_pile_10k_case.h"
#include "nvidia_physx5_result_writer.h"
#include "nvidia_physx5_runner_args.h"

#define PHYSICS_ARENA_PHYSX_NAMESPACE nvidia_physx5_benchmark
#define PHYSICS_ARENA_PHYSX5_API 1
#include "../../common/physx_ragdoll_stair_tumble_case.inl"
#undef PHYSICS_ARENA_PHYSX5_API
#undef PHYSICS_ARENA_PHYSX_NAMESPACE
