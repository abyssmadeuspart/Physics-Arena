#include "nvidia_physx34_pyramid_wall_case.h"
#include "physx_engine_settings.h"

#include "nvidia_physx34_result_writer.h"
#include "nvidia_physx34_runner_args.h"

namespace nvidia_physx34_benchmark
{
int InitializeContext(const PhysXCaseConfig& config, PhysXContext* context);
void ReleaseContext(PhysXContext* context);
}

#define PHYSICS_ARENA_PHYSX_NAMESPACE nvidia_physx34_benchmark
#include "../../common/physx_pyramid_wall_case.inl"
#undef PHYSICS_ARENA_PHYSX_NAMESPACE
