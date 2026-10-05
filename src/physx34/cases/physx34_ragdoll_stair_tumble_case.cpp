#include "physx34_ragdoll_stair_tumble_case.h"
#include "physx_engine_settings.h"

#include "physx34_box_container_pile_10k_case.h"
#include "physx34_result_writer.h"
#include "physx34_runner_args.h"

#define PHYSICS_ARENA_PHYSX_NAMESPACE physx34_benchmark
#include "../../common/physx_ragdoll_stair_tumble_case.inl"
#undef PHYSICS_ARENA_PHYSX_NAMESPACE
