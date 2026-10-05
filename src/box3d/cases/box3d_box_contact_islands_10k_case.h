#pragma once

#include "box3d_box_container_pile_10k_case.h"

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace box3d_benchmark
{
struct Box3DRunRequest;

struct Box3DContactIslandsState
{
	Box3DCaseConfig config;
	b3WorldId worldId;
	b3BodyId* dynamicBodies;
	std::chrono::steady_clock::duration::rep* rawWorkUnitDurations;
	double workloadElapsedMs;
	double latestWorkUnitElapsedMs;
	int completedWorkUnitCount;
};

int CreateBox3DContactIslandsState(const Box3DCaseConfig& config, Box3DContactIslandsState* state);
int WarmupBox3DContactIslands(Box3DContactIslandsState* state, int workUnitCount);
int StepBox3DContactIslands(Box3DContactIslandsState* state, int workUnitCount);
void DestroyBox3DContactIslandsState(Box3DContactIslandsState* state);
int SampleBox3DContactIslandsTransforms(const Box3DContactIslandsState& state,
                                        benchmark_visual::VisualStableTransform* transforms, int transformCapacity);
int ValidateBox3DContactIslands(const Box3DContactIslandsState& state, std::uint64_t* invalidTransformCount);
int RunBox3DContactIslandsHeadless(const Box3DRunRequest& request);
int FormatBox3DContactIslandsPhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                             std::size_t settingsCapacity);
const Box3DCaseDescriptor& Box3DContactIslandsCaseDescriptor();
}
