#include "launcher_app_internal.h"
#include "physics_arena/ray_tracing_images.h"
#include "physics_arena/ray_tracing_results.h"

#include <imgui.h>
#include <rlgl.h>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <new>

namespace benchmark_visual
{
using namespace physics_arena;
using namespace benchmark_ray;

struct NativeRayTracingView
{
	std::filesystem::path directory;
	std::array<std::uint32_t, 3> tuple;
	std::vector<RayResultRow> rows;
	std::vector<RayProbeRow> probes;
	std::vector<RayProcessMemory> memory;
	RayImage image;
	std::array<RayPhaseStatistics, 2> statistics;
	std::array<std::uint32_t, 3> statisticsSelection;
	PresenceStatus statisticsPresence;
	StatusRecord dataError;
	std::uint32_t texture;
	int view, phase, api, channel, statisticPhase, probeView;
	PresenceStatus replayPresence;
};

void CloseRayTracingReplay(PhysicsArenaApp* app)
{
	app->results.rayLoadRequest = NativeRayLoadRequest_None;
	app->results.rayLoadingPresented = PresenceStatus_Absent;
	if (app->rayView == nullptr)
		return;
	NativeRayTracingView& state = *app->rayView;
	if (state.replayPresence == PresenceStatus_Present &&
	    state.tuple != std::array<std::uint32_t, 3>{app->results.replayEngineIndex,
	        app->results.replayThreadIndex, app->results.replayRepeatIndex})
		app->results.replayError = {};
	if (state.texture != 0)
		rlUnloadTexture(state.texture);
	state.texture = 0;
	state.image = {};
	state.replayPresence = PresenceStatus_Absent;
}

void CloseRayTracingView(PhysicsArenaApp* app)
{
	if (app->rayView != nullptr || app->results.rayLoadRequest != NativeRayLoadRequest_None)
		app->results.replayError = {};
	CloseRayTracingReplay(app);
	delete app->rayView;
	app->rayView = nullptr;
}

PresenceStatus RayTracingReplayPresence(const PhysicsArenaApp* app)
{
	return app->rayView == nullptr ? PresenceStatus_Absent : app->rayView->replayPresence;
}

const std::array<std::uint32_t, 3>& RayTracingReplaySelection(const PhysicsArenaApp* app)
{
	return app->rayView->tuple;
}

ArenaStatus LoadRayCaseData(PhysicsArenaApp* app)
{
	if (app->rayView != nullptr)
		return app->rayView->dataError.code;
	app->rayView = new (std::nothrow) NativeRayTracingView{};
	if (app->rayView == nullptr)
		return GraphicalWindowError(&app->results.error, "ray_view_allocation");
	NativeRayTracingView& state = *app->rayView;
	state.directory = app->results.selectedDirectory.data();
	ArenaStatus status = ReadRayTracingResults(state.directory / "ray-tracing.csv", &state.rows, &state.dataError);
	if (status == ArenaStatus_Ok)
		status = ReadRayCapabilities(state.directory / "ray-capabilities.csv", &state.probes, &state.dataError);
	if (status == ArenaStatus_Ok)
		status = ReadRayProcessMemory(state.directory / "ray-process.csv", &state.memory, &state.dataError);
	return status;
}

ArenaStatus SelectRayTracingImage(PhysicsArenaApp* app, std::uint32_t view, std::uint32_t phase,
                                  std::uint32_t api, std::uint32_t channel)
{
	CloseRayTracingReplay(app);
	app->results.rayImageRequest = {view, phase, api, channel};
	const ArenaStatus loaded = LoadRayCaseData(app);
	if (app->rayView == nullptr)
	{
		app->results.replayError = app->results.error;
		return loaded;
	}
	NativeRayTracingView& state = *app->rayView;
	StatusRecord& error = app->results.replayError;
	error = {};
	state.tuple = {app->results.replayEngineIndex, app->results.replayThreadIndex, app->results.replayRepeatIndex};
	state.view = static_cast<int>(view);
	state.phase = phase == Phase_Updated ? 1 : 0;
	state.api = static_cast<int>(api);
	state.channel = static_cast<int>(channel);
	const ResultViewModel& model = app->workspace.finalization.model;
	if (state.tuple[0] >= model.engineCount || state.tuple[1] >= model.threadCount || state.tuple[2] >= model.repeatCount)
		return GraphicalWindowError(&error, "ray_tuple_not_in_result");
	state.replayPresence = PresenceStatus_Present;
	if (loaded != ArenaStatus_Ok)
	{
		error = state.dataError;
		return loaded;
	}
	const std::string_view engine = ResultViewTextView(&model, model.engines[state.tuple[0]].id);
	const std::uint32_t threads = model.threadCounts[state.tuple[1]];
	if (RecordingForThread(app->workspace.finalization.manifest.recordingThreads, threads) == RecordingMode_Off)
		return GraphicalWindowError(&error, "selected_thread_not_recorded");
	RayImageArchive archive = {};
	ArenaStatus status = OpenRayImages(RayImageTuplePath(state.directory, engine, threads, state.tuple[2]),
	    engine, threads, state.tuple[2], &archive, &error);
	if (status == ArenaStatus_Ok)
		status = ComposeRayImage(state.directory / "ray-corpus", &archive, view, static_cast<Phase>(phase),
		    static_cast<Api>(api), static_cast<RayImageChannel>(channel), &state.image, &error);
	CloseRayImages(&archive);
	if (status == ArenaStatus_Ok)
	{
		state.texture = rlLoadTexture(state.image.rgba.data(), static_cast<int>(state.image.width),
		    static_cast<int>(state.image.height), RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
		if (state.texture == 0)
			return GraphicalWindowError(&error, "ray_image_texture_upload");
	}
	return status;
}

void RequestRayTracingImage(PhysicsArenaApp* app, std::uint32_t view, std::uint32_t phase,
                             std::uint32_t api, std::uint32_t channel)
{
	app->results.rayImageRequest = {view, phase, api, channel};
	app->results.rayLoadRequest = NativeRayLoadRequest_Image;
	app->results.rayLoadingPresented = PresenceStatus_Absent;
	app->renderRequested = PresenceStatus_Present;
}

void CompleteRayTracingRequest(PhysicsArenaApp* app)
{
	const NativeRayLoadRequest request = app->results.rayLoadRequest;
	app->results.rayLoadRequest = NativeRayLoadRequest_None;
	app->results.rayLoadingPresented = PresenceStatus_Absent;
	if (request == NativeRayLoadRequest_Image)
	{
		const std::array<std::uint32_t, 4> image = app->results.rayImageRequest;
		SelectRayTracingImage(app, image[0], image[1], image[2], image[3]);
	}
	else if (request == NativeRayLoadRequest_CaseData && LoadRayCaseData(app) == ArenaStatus_Ok)
	{
		NativeRayTracingView& state = *app->rayView;
		const ResultViewModel& model = app->workspace.finalization.model;
		const NativeResultsState& selected = app->results;
		const ResultEngineView& engine = model.engines[selected.caseDataEngineIndex];
		for (std::uint32_t api = 0; api < state.statistics.size(); ++api)
			state.statistics[api] = SummarizeRayPhase(state.rows, ResultViewTextView(&model, engine.id),
			    model.threadCounts[selected.caseDataThreadIndex], static_cast<Phase>(state.statisticPhase),
			    static_cast<Api>(api), model.repeatCount, model.measuredWorkUnitCount, model.executionFailures, engine.catalogEngineIndex);
		state.statisticsSelection = {selected.caseDataEngineIndex, selected.caseDataThreadIndex,
		    static_cast<std::uint32_t>(state.statisticPhase)};
		state.statisticsPresence = PresenceStatus_Present;
	}
	app->renderRequested = PresenceStatus_Present;
}

namespace
{
constexpr const char* kRayViews = "Exterior\0Side\0Interior\0Overhead\0Long rays\0Corridor\0";
constexpr const char* kRayPhases = "Primary coherent\0Primary shuffled\0Shadows\0Reflections\0Ambient occlusion\0Filtered closest hits\0All collider hits\0All mesh surface hits\0Updated primary\0";
constexpr std::array<const char*, Probe_Count> kRayProbes = {"Inside origin", "Mesh backfaces", "Surface start",
    "Range endpoints", "Grazing", "Far origin", "Enumeration overflow", "Concurrent reads"};

const char* RayOutcomeLabel(RayCapability capability)
{
	switch (capability)
	{
	case RayCapability_Supported: return "Passed";
	case RayCapability_Failed: return "Failed";
	case RayCapability_Unsupported: return "Unsupported";
	case RayCapability_NativeSemanticsDiffer: return "Different semantics";
	case RayCapability_ExecutionFailed: return "Execution failed";
	case RayCapability_NotRun: return "Not run";
	}
	return "Unavailable";
}

void DrawRayPhaseTable(PhysicsArenaApp* app)
{
	NativeRayTracingView& state = *app->rayView;
	DrawPaneHeading("Query phases", 28);
	ImGui::SetNextItemWidth(-1);
	if (ImGui::Combo("##ray_measured_phase", &state.statisticPhase, kRayPhases))
	{
		state.statisticsPresence = PresenceStatus_Absent;
		app->renderRequested = PresenceStatus_Present;
		return;
	}
	ImGui::TextDisabled("All repeats for the selected engine and thread count");
	const std::array<RayPhaseStatistics, 2>& statistics = state.statistics;
	double maximumDuration = 0;
	for (const RayPhaseStatistics& value : statistics)
		if (value.samples != 0)
			maximumDuration = (std::max)({maximumDuration, value.medianMs, value.p95Ms, value.repeatMedianMaxMs,
			    state.statisticPhase == Phase_Updated ? value.combinedMedianMs : 0.0});
	const NativeValueFormat rate = SelectNativeValueFormat(NativeValueDomain_Rate,
	    (std::max)(statistics[0].raysPerSecond, statistics[1].raysPerSecond));
	const NativeValueFormat duration = SelectNativeValueFormat(NativeValueDomain_Milliseconds, maximumDuration);
	if (!ImGui::BeginTable("ray_phase_measurements", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
	    ImGuiTableFlags_SizingStretchProp))
		return;
	ImGui::TableSetupColumn("Measurement", ImGuiTableColumnFlags_WidthStretch, 1.2f);
	ImGui::TableSetupColumn("Ordinary", ImGuiTableColumnFlags_WidthStretch, 1);
	ImGui::TableSetupColumn("Native batch", ImGuiTableColumnFlags_WidthStretch, 1);
	ImGui::TableHeadersRow();
	const std::array<const char*, 14> labels = {"Outcome", "Measured samples", "Whole repeats", "Queries", "Median", "p95",
	    "Throughput", "Failed comparisons", "Hit / miss, %", "Repeat medians", "Execution failed / not run", "Partial / missing repeats", "Update", "Update + query"};
	const std::uint32_t count = state.statisticPhase == Phase_Updated ? 14 : 12;
	for (std::uint32_t metric = 0; metric < count; ++metric)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		const int time = metric == 4 || metric == 5 || metric == 9 || metric >= 12;
		const NativeValueText unit = FormatNativeUnit(time != 0 ? duration : rate, "rays/s");
		if (time != 0 || metric == 6)
			ImGui::TextDisabled("%s, %s", labels[metric], unit.data());
		else
			ImGui::TextDisabled("%s", labels[metric]);
		for (const RayPhaseStatistics& value : statistics)
		{
			ImGui::TableNextColumn();
			std::array<char, 256> text = {}, exact = {};
			if (metric == 0)
			{
				DrawResultTableValue(RayPhaseOutcomeText(value));
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
					ImGui::SetTooltip("Measured quality: %s\nExecution failed: %u; not run: %u\nPartial repeats: %u; missing repeats: %u",
					    value.capabilityPresence == PresenceStatus_Present ? RayOutcomeLabel(value.capability) : "Unavailable",
					    value.executionFailedRepeats, value.notRunRepeats, value.partialRepeats, value.missingRepeats);
				continue;
			}
			if ((value.samples == 0 && metric >= 3 && metric <= 9) || (metric == 9 && value.repeats == 0) ||
			    (metric == 8 && value.proportions == PresenceStatus_Absent) || (metric >= 12 && value.samples == 0))
			{
				DrawResultTableValue("Unavailable");
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
					ImGui::SetTooltip("%s unavailable: %s", labels[metric], metric == 9 ? "no whole measured repeats" : "no complete measurement");
				continue;
			}
			if (time != 0 && metric != 9)
			{
				const double milliseconds = metric == 4 ? value.medianMs : metric == 5 ? value.p95Ms :
				    metric == 12 ? value.updateMedianMs : value.combinedMedianMs;
				std::snprintf(text.data(), text.size(), "%s", FormatNativeValue(milliseconds, duration).data());
				std::snprintf(exact.data(), exact.size(), "%s: %s ms", labels[metric], FormatNativeRaw(milliseconds).data());
			}
			else switch (metric)
			{
			case 1:
				std::snprintf(text.data(), text.size(), "%u / %u", value.samples, value.requestedSamples);
				std::snprintf(exact.data(), exact.size(), "%u complete measured samples / %u requested samples", value.samples, value.requestedSamples);
				break;
			case 2:
				std::snprintf(text.data(), text.size(), "%u / %u", value.repeats, value.requestedRepeats);
				std::snprintf(exact.data(), exact.size(), "%u repeats with all %u measured suites / %u requested repeats", value.repeats, app->workspace.finalization.model.measuredWorkUnitCount, value.requestedRepeats);
				break;
			case 3: case 7:
			{
				const std::uint64_t number = metric == 3 ? value.queries : value.errors;
				std::snprintf(text.data(), text.size(), "%s", FormatNativeCount(number).data());
				std::snprintf(exact.data(), exact.size(), "%s: %s", labels[metric], FormatNativeRawCount(number).data());
				break;
			}
			case 6:
				std::snprintf(text.data(), text.size(), "%s", FormatNativeValue(value.raysPerSecond, rate).data());
				std::snprintf(exact.data(), exact.size(), "%s rays/s", FormatNativeRaw(value.raysPerSecond).data());
				break;
			case 8:
				std::snprintf(text.data(), text.size(), "%.1f / %.1f", value.hitPercent, value.missPercent);
				std::snprintf(exact.data(), exact.size(), "Hits: %s (%s %%)\nMisses: %s (%s %%)", FormatNativeRawCount(value.hitRays).data(),
				    FormatNativeRaw(value.hitPercent).data(), FormatNativeRawCount(value.missRays).data(), FormatNativeRaw(value.missPercent).data());
				break;
			case 9:
				std::snprintf(text.data(), text.size(), "%s - %s", FormatNativeValue(value.repeatMedianMinMs, duration).data(), FormatNativeValue(value.repeatMedianMaxMs, duration).data());
				std::snprintf(exact.data(), exact.size(), "%s - %s ms\nWhole-repeat throughput: %s - %s rays/s", FormatNativeRaw(value.repeatMedianMinMs).data(),
				    FormatNativeRaw(value.repeatMedianMaxMs).data(), FormatNativeRaw(value.repeatRaysPerSecondMin).data(), FormatNativeRaw(value.repeatRaysPerSecondMax).data());
				break;
			case 10: case 11:
				std::snprintf(text.data(), text.size(), "%u / %u", metric == 10 ? value.executionFailedRepeats : value.partialRepeats,
				    metric == 10 ? value.notRunRepeats : value.missingRepeats);
				std::snprintf(exact.data(), exact.size(), "%s: %s", labels[metric], text.data());
				break;
			}
			DrawResultTableValue(text.data());
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
				ImGui::SetTooltip("%s", exact.data());
		}
	}
	ImGui::EndTable();
}

void DrawRayProbes(PhysicsArenaApp* app)
{
	NativeRayTracingView& state = *app->rayView;
	const NativeResultsState& selected = app->results;
	const ResultViewModel& model = app->workspace.finalization.model;
	const ResultEngineView& engine = model.engines[selected.caseDataEngineIndex];
	const std::string_view id = ResultViewTextView(&model, engine.id);
	const std::uint32_t threads = model.threadCounts[selected.caseDataThreadIndex];
	DrawPaneHeading("Selected repeat", 28);
	const ExecutionFailure* failure = FindExecutionFailure(model.executionFailures, engine.catalogEngineIndex,
	    threads, selected.caseDataRepeatIndex);
	if (failure != nullptr)
		ImGui::TextWrapped("%s: %s", failure->outcome == ExecutionOutcome_NotRun ? "Not run" : "Execution failed",
		    ExecutionFailureReasonName(failure->reason));
	const RayProcessMemory* memory = nullptr;
	for (const RayProcessMemory& row : state.memory)
		if (std::string_view(row.engine.data()) == id && row.threads == threads && row.repeat == selected.caseDataRepeatIndex)
			memory = &row;
	if (ImGui::BeginTable("ray_process_memory", 2, ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 1.2f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 2);
		InspectorFact("Peak committed memory", memory != nullptr && memory->available == PresenceStatus_Present
		    ? FormatNativeBytes(memory->peakCommittedBytes).data() : "Unavailable");
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		{
			if (memory != nullptr && memory->available == PresenceStatus_Present)
				ImGui::SetTooltip("%s bytes\nWhole process, including the native world and query buffers", FormatNativeRawCount(memory->peakCommittedBytes).data());
			else
				ImGui::SetTooltip("Peak committed memory unavailable for this repeat");
		}
		ImGui::EndTable();
	}
	if (!ImGui::CollapsingHeader("Capability probes"))
		return;
	ImGui::SetNextItemWidth(-1);
	ImGui::Combo("##ray_probe_view", &state.probeView, kRayViews);
	ImGui::TextDisabled("Outside measured query time");
	if (!ImGui::BeginTable("ray_probes", 5, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
	    ImGuiTableFlags_SizingStretchProp))
		return;
	ImGui::TableSetupColumn("Probe", ImGuiTableColumnFlags_WidthStretch, 1.4f);
	ImGui::TableSetupColumn("Outcome", ImGuiTableColumnFlags_WidthStretch, 1.4f);
	for (const char* label : {"Mismatches", "Native errors", "Overflows"})
		ImGui::TableSetupColumn(label, ImGuiTableColumnFlags_WidthStretch, 1);
	ImGui::TableHeadersRow();
	for (const RayProbeRow& row : state.probes)
	{
		if (std::string_view(row.engine.data()) != id || row.threads != threads || row.repeat != selected.caseDataRepeatIndex ||
		    row.view != static_cast<std::uint32_t>(state.probeView))
			continue;
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextWrapped("%s", kRayProbes[row.probe]);
		ImGui::TableNextColumn();
		ImGui::TextWrapped("%s", RayOutcomeLabel(row.capability));
		for (std::uint32_t value : {row.mismatches, row.nativeErrors, row.overflows})
		{
			ImGui::TableNextColumn();
			DrawResultTableValue(FormatNativeCount(value).data());
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
				ImGui::SetTooltip("%s", FormatNativeRawCount(value).data());
		}
	}
	ImGui::EndTable();
}

void DrawRayImageControls(PhysicsArenaApp* app)
{
	NativeRayTracingView& state = *app->rayView;
	int changed = 0;
	if (ImGui::BeginTable("ray_image_controls", 2, ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, 62 * app->platform.dpiScale);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
		constexpr std::array<const char*, 4> labels = {"View", "Scene", "API", "Channel"};
		const std::array<int*, 4> values = {&state.view, &state.phase, &state.api, &state.channel};
		const std::array<const char*, 4> options = {kRayViews, "Primary coherent\0Updated primary\0",
		    "Ordinary\0Native batch\0", "Shaded\0Depth\0Normals\0Errors\0"};
		for (std::uint32_t index = 0; index < labels.size(); ++index)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", labels[index]);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);
			ImGui::PushID(static_cast<int>(index));
			if (index == 0)
			{
				const CaseExecutionSpec& execution = app->workspace.finalization.manifest.configuration.execution;
				const int viewCount = static_cast<int>(std::min(execution.rayTracing.viewCount, execution.measuredWorkUnitCount));
				const std::array<const char*, 6> views = {"Exterior", "Side", "Interior", "Overhead", "Long rays", "Corridor"};
				changed |= ImGui::Combo("##ray_image", &state.view, views.data(), viewCount);
			}
			else
				changed |= ImGui::Combo("##ray_image", values[index], options[index]);
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	changed |= ImGui::Button("Reload image");
	if (changed != 0)
		RequestRayTracingImage(app, static_cast<std::uint32_t>(state.view), state.phase == 0 ? Phase_Primary : Phase_Updated,
		    static_cast<std::uint32_t>(state.api), static_cast<std::uint32_t>(state.channel));
}
}

void DrawRayCaseData(PhysicsArenaApp* app)
{
	if (app->rayView != nullptr && app->rayView->dataError.code != ArenaStatus_Ok)
	{
		DrawStatusError(app->rayView->dataError);
		return;
	}
	const NativeResultsState& selected = app->results;
	if (app->rayView == nullptr || app->rayView->statisticsPresence == PresenceStatus_Absent ||
	    app->rayView->statisticsSelection != std::array<std::uint32_t, 3>{selected.caseDataEngineIndex,
	        selected.caseDataThreadIndex, static_cast<std::uint32_t>(app->rayView->statisticPhase)})
	{
		ImGui::TextUnformatted("Loading ray measurements...");
		app->results.rayLoadRequest = NativeRayLoadRequest_CaseData;
		app->results.rayLoadingPresented = PresenceStatus_Present;
		app->renderRequested = PresenceStatus_Present;
		return;
	}
	DrawRayPhaseTable(app);
	DrawRayProbes(app);
}

void DrawRayTracingReplay(PhysicsArenaApp* app)
{
	NativeRayTracingView& state = *app->rayView;
	const float scale = app->platform.dpiScale;
	const int compact = ImGui::GetContentRegionAvail().x / scale < 1000;
	const float inspectorWidth = compact == 0 ? 288 * scale : 0;
	app->ui.scene = {};
	app->ui.sceneHovered = app->ui.sceneFocused = app->ui.sceneActive = PresenceStatus_Absent;
	const ImVec2 origin = ImGui::GetCursorPos();
	const float imageWidth = ImGui::GetContentRegionAvail().x - inspectorWidth;
	if (compact == 0 || app->ui.replayDetailsVisible == PresenceStatus_Present)
	{
		if (compact == 0)
			ImGui::SetCursorPos(ImVec2(origin.x + imageWidth, origin.y));
		ImGui::BeginChild("ray_replay_inspector", ImVec2(compact == 0 ? inspectorWidth : 0, 0), ImGuiChildFlags_Borders);
		DrawPaneHeading("Recording", 28);
		DrawReplayTupleControls(app);
		const std::array<std::uint32_t, 3> selected = {app->results.replayEngineIndex,
		    app->results.replayThreadIndex, app->results.replayRepeatIndex};
		if (selected != state.tuple)
			RequestRayTracingImage(app, 0, Phase_Primary, Api_Ordinary, RayImageChannel_Shaded);
		DrawPaneHeading("Image", 28);
		DrawRayImageControls(app);
		if (state.texture != 0)
		{
			if (ImGui::BeginTable("ray_image_facts", 2, ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
				std::array<char, 80> dimensions = {};
				std::snprintf(dimensions.data(), dimensions.size(), "%u x %u", state.image.width, state.image.height);
				InspectorFact("Pixels", dimensions.data());
				InspectorFact("Queries", FormatNativeCount(state.image.queries).data());
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
					ImGui::SetTooltip("%s queries", FormatNativeRawCount(state.image.queries).data());
				InspectorFact("Failed comparisons", FormatNativeCount(state.image.errors).data());
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
					ImGui::SetTooltip("%s failed comparisons", FormatNativeRawCount(state.image.errors).data());
				ImGui::EndTable();
			}
			if (ImGui::TreeNode("Image details"))
			{
				if (ImGui::BeginTable("ray_image_details", 2, ImGuiTableFlags_SizingStretchProp))
				{
					ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
					const std::array<double, 3> position = {state.image.camera.position.x, state.image.camera.position.y, state.image.camera.position.z};
					const std::array<const char*, 3> axes = {"Camera X, m", "Camera Y, m", "Camera Z, m"};
					for (std::size_t axis = 0; axis < axes.size(); ++axis)
					{
						InspectorFact(axes[axis], FormatNativeRaw(position[axis]).data());
						if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
							ImGui::SetTooltip("%s m", FormatNativeRaw(position[axis]).data());
					}
					InspectorFact("Shadows", state.image.shadows == PresenceStatus_Present ? "Saved" : "Unavailable");
					InspectorFact("Reflections", state.image.reflections == PresenceStatus_Present ? "Saved" : "Unavailable");
					InspectorFact("AO", state.image.ambient == PresenceStatus_Present ? "Saved" : "Unavailable");
					ImGui::EndTable();
				}
				ImGui::TreePop();
			}
		}
		ImGui::EndChild();
	}
	ImGui::SetCursorPos(origin);
	if (compact == 0 || app->ui.replayDetailsVisible != PresenceStatus_Present)
	{
		ImGui::BeginChild("ray_saved_image", ImVec2(compact == 0 ? imageWidth : 0, 0),
		    ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		if (app->results.rayLoadRequest == NativeRayLoadRequest_Image)
			ImGui::TextUnformatted("Opening recording...");
		else if (state.texture != 0)
		{
			const ImVec2 available = ImGui::GetContentRegionAvail();
			const float imageScale = (std::max)(0.01f, (std::min)(available.x / state.image.width, available.y / state.image.height));
			const ImVec2 size(state.image.width * imageScale, state.image.height * imageScale);
			ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + (available.x - size.x) * 0.5f,
			    ImGui::GetCursorPosY() + (available.y - size.y) * 0.5f));
			ImGui::Image(static_cast<ImTextureID>(state.texture), size);
		}
		else
		{
			ImGui::TextWrapped("No image is available for this saved selection");
			if (app->results.replayError.code != ArenaStatus_Ok)
				DrawStatusError(app->results.replayError);
		}
		ImGui::EndChild();
	}
	std::array<wchar_t, 200> title = {};
	std::swprintf(title.data(), title.size(), L"Physics Arena | Ray view=%d phase=%d api=%d channel=%d status=%d tuple=%u/%u/%u",
	    state.view, state.phase, state.api, state.channel, static_cast<int>(app->results.replayError.code),
	    state.tuple[0], state.tuple[1], state.tuple[2]);
	std::array<wchar_t, 200> currentTitle = {};
	GetWindowTextW(static_cast<HWND>(app->platform.nativeHandle), currentTitle.data(), static_cast<int>(currentTitle.size()));
	if (std::wcscmp(currentTitle.data(), title.data()) != 0)
		SetWindowTextW(static_cast<HWND>(app->platform.nativeHandle), title.data());
}
}
