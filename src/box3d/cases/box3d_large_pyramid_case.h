#pragma once

#include "box3d_case_registry.h"
#include "box3d/box3d.h"

#include <chrono>
#include <cstddef>
#include <vector>

namespace box3d_benchmark
{
struct Box3DRunRequest;

struct Box3DLargePyramidState
{
	Box3DCaseConfig config;
	b3WorldId worldId;
	std::vector<b3BodyId> dynamicBodies;
	std::vector<std::chrono::steady_clock::duration::rep> rawStepDurations;
	double physicsElapsedMs;
	double latestPhysicsStepMs;
	int completedStepCount;
};

int CreateBox3DLargePyramidState(const Box3DCaseConfig& config, Box3DLargePyramidState* state);
int WarmupBox3DLargePyramidState(Box3DLargePyramidState* state, int workUnitCount);
int StepBox3DLargePyramidState(Box3DLargePyramidState* state, int workUnitCount);
void DestroyBox3DLargePyramidState(Box3DLargePyramidState* state);
int SampleBox3DLargePyramidTransforms(const Box3DLargePyramidState& state,
                                      benchmark_visual::VisualStableTransform* transforms, int transformCapacity);
std::uint64_t CountBox3DLargePyramidInvalidTransforms(const Box3DLargePyramidState& state);
int RunBox3DLargePyramidHeadless(const Box3DRunRequest& request);
int FormatBox3DLargePyramidPhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                           std::size_t settingsCapacity);
const Box3DCaseDescriptor& Box3DLargePyramidCaseDescriptor();
}
