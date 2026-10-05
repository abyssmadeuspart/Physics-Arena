#pragma once

#include "stack_state_capture.h"

#include "box3d_case_registry.h"
#include "box3d/box3d.h"

#include <cstddef>
#include <chrono>
#include <vector>

namespace box3d_benchmark
{
struct Box3DRunRequest;

constexpr const char* kEngineId = "box3d";

struct Box3DCaseState
{
	Box3DCaseConfig config;
	b3WorldId worldId;
	std::vector<b3BodyId> dynamicBodies;
	std::vector<std::chrono::steady_clock::duration::rep> rawStepDurations;
	double physicsElapsedMs;
	double latestPhysicsStepMs;
	int completedStepCount;
};

int CreateBox3DCaseState(const Box3DCaseConfig& config, Box3DCaseState* state);
int RunBox3DCaseWarmup(const Box3DCaseConfig& config, benchmark_stack::Capture* capture);
int StepBox3DCase(Box3DCaseState* state, int stepCount);
void DestroyBox3DCaseState(Box3DCaseState* state);
int SampleBox3DTransforms(const Box3DCaseState& state, benchmark_visual::VisualStableTransform* transforms,
                          int transformCapacity);
std::uint64_t CountBox3DContainerPileInvalidTransforms(const Box3DCaseState& state);
int RunBox3DContainerPileHeadless(const Box3DRunRequest& request);
int FormatBox3DContainerPilePhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                            std::size_t settingsCapacity);
const Box3DCaseDescriptor& Box3DContainerPileCaseDescriptor();
}
