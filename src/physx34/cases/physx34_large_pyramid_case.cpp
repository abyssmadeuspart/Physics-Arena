#include "physx34_large_pyramid_case.h"
#include "physx_engine_settings.h"

#include "physx34_result_writer.h"
#include "physx34_runner_args.h"

namespace physx34_benchmark
{
int InitializeContext(const PhysXCaseConfig& config, PhysXContext* context);
void ReleaseContext(PhysXContext* context);
}

#define PHYSICS_ARENA_PHYSX_NAMESPACE physx34_benchmark
#include "../../common/physx_large_pyramid_case.inl"
#undef PHYSICS_ARENA_PHYSX_NAMESPACE
