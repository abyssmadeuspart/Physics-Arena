#pragma once

#include "nvidia_physx34_box_container_pile_10k_case.h"

#include <cstddef>
#include <cstdint>

namespace nvidia_physx34_benchmark
{
struct PhysXRunRequest;

struct PhysXContactIslandsState
{
	PhysXCaseConfig config;
	PhysXContext context;
	physx::PxRigidDynamic** bodies;
	std::chrono::steady_clock::duration::rep* rawWorkUnitDurations;
	int completedWorkUnitCount;
	double workloadElapsedMs;
	double latestWorkUnitElapsedMs;
};

int CreatePhysXContactIslandsState(const PhysXCaseConfig& config, PhysXContactIslandsState* state);
int WarmupPhysXContactIslands(PhysXContactIslandsState* state, int workUnitCount);
int StepPhysXContactIslands(PhysXContactIslandsState* state, int workUnitCount);
void DestroyPhysXContactIslandsState(PhysXContactIslandsState* state);
int SamplePhysXContactIslandsTransforms(const PhysXContactIslandsState& state, PhysXTransform* transforms,
                                        int transformCapacity);
int ValidatePhysXContactIslands(const PhysXContactIslandsState& state, std::uint64_t* invalidTransformCount);
int RunPhysXContactIslandsHeadless(const PhysXRunRequest& request);
int FormatPhysXContactIslandsPhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                             std::size_t settingsCapacity);
const PhysXCaseDescriptor& PhysXContactIslandsCaseDescriptor();
}
