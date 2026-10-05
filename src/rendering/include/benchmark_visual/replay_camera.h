#pragma once

#include "benchmark_visual/visual_camera.h"

#include <array>

struct CaseExecutionSpec;

namespace benchmark_visual
{
struct NativeReplayCameraPreferences
{
	float yawRadians;
	float pitchRadians;
	float distanceScale;
	std::array<float, 3> pan;
};

struct ReplayCameraContext
{
	VisualCameraContext framing;
	VisualCameraVector sceneMinimum;
	VisualCameraVector sceneMaximum;
	float objectScale;
};

int PrepareReplayCamera(const VisualScene& scene, const CaseExecutionSpec* saved, ReplayCameraContext* context);
int ComposeReplayCamera(const ReplayCameraContext& context, float aspect,
                        NativeReplayCameraPreferences* controls, ResolvedVisualCamera* camera);
}
