#include "benchmark_visual/visual_renderer.h"

#include <imgui.h>
#include <implot.h>

namespace benchmark_visual
{
constexpr int kMetricsSampleCapacity = 512;

static float s_stepSamples[kMetricsSampleCapacity];
static float s_physicsSamples[kMetricsSampleCapacity];
static int s_metricsSampleCount = 0;
static int s_lastRecordedStep = -1;

double PhysicsMsPerStep(VisualSnapshot snapshot)
{
	if (snapshot.stepIndex <= 0)
	{
		return 0.0;
	}

	return snapshot.physicsElapsedMs / static_cast<double>(snapshot.stepIndex);
}

void RecordMetricsSample(VisualSnapshot snapshot)
{
	if (snapshot.stepIndex == s_lastRecordedStep || snapshot.stepIndex <= 0)
	{
		return;
	}

	if (s_metricsSampleCount >= kMetricsSampleCapacity)
	{
		for (int index = 1; index < kMetricsSampleCapacity; ++index)
		{
			s_stepSamples[index - 1] = s_stepSamples[index];
			s_physicsSamples[index - 1] = s_physicsSamples[index];
		}
		s_metricsSampleCount = kMetricsSampleCapacity - 1;
	}

	int writeIndex = s_metricsSampleCount;
	s_stepSamples[writeIndex] = static_cast<float>(snapshot.stepIndex);
	s_physicsSamples[writeIndex] = static_cast<float>(PhysicsMsPerStep(snapshot));
	s_metricsSampleCount += 1;
	s_lastRecordedStep = snapshot.stepIndex;
}

void DrawLiveMetricsUi(VisualSnapshot snapshot)
{
	RecordMetricsSample(snapshot);

	ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Always);
	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_AlwaysAutoResize;

	ImGui::Begin("Live Metrics", nullptr, flags);
	ImGui::Text("Engine: %s", snapshot.identity.engineId);
	ImGui::Text("Case: %s", snapshot.identity.caseId);
	ImGui::Separator();
	ImGui::Text("Thread count: %d", snapshot.identity.threadCount);
	ImGui::Text("Repeat: %d", snapshot.identity.repeatIndex + 1);
	ImGui::Text("Warmup: %d steps", snapshot.identity.warmupSteps);
	ImGui::Text("Step: %d / %d", snapshot.stepIndex, snapshot.identity.stepCount);
	ImGui::Text("Bodies / shapes: %d / %d", snapshot.identity.bodyCount, snapshot.identity.shapeCount);
	ImGui::Text("Static boxes: %d", snapshot.identity.staticBoxCount);
	ImGui::Text("Physics ms: %.3f", snapshot.physicsElapsedMs);
	ImGui::Text("Physics ms/step: %.3f", PhysicsMsPerStep(snapshot));
	ImGui::Text("Present wait ms: %.3f", snapshot.renderer.presentWaitMs);
	ImGui::Text("Skipped frames: %d", snapshot.renderer.skippedFrameCount);
	if (s_metricsSampleCount > 1 && ImPlot::BeginPlot("Timing", ImVec2(350.0f, 150.0f), ImPlotFlags_NoMenus))
	{
		ImPlot::SetupAxes("Step", "ms", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
		ImPlot::PlotLine("physics", s_stepSamples, s_physicsSamples, s_metricsSampleCount);
		ImPlot::EndPlot();
	}
	ImGui::End();
}
}
