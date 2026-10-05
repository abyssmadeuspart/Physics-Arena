#include "nvidia_physx5_large_pyramid_case.h"
#include "physx_engine_settings.h"

#include "nvidia_physx5_result_writer.h"
#include "nvidia_physx5_runner_args.h"

namespace nvidia_physx5_benchmark
{
int InitializeContext(const PhysXCaseConfig& config, PhysXContext* context);
void ReleaseContext(PhysXContext* context);
}

#define PHYSICS_ARENA_PHYSX_NAMESPACE nvidia_physx5_benchmark
#define PHYSICS_ARENA_PHYSX5_API 1
#include "../../common/physx_large_pyramid_case.inl"
#undef PHYSICS_ARENA_PHYSX5_API
#undef PHYSICS_ARENA_PHYSX_NAMESPACE
