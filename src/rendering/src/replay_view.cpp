#include "ray_tracing.h"
#include "benchmark_visual/native_replay_comparison.h"
#include "launcher_app_internal.h"

#include <imgui.h>
#include <implot.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <new>
#include <string_view>

namespace benchmark_visual
{
using namespace physics_arena;
ArenaStatus GraphicalWindowError(StatusRecord* error, std::string_view detail)
{
	*error = {};
	constexpr std::string_view component = "graphics_window";
	const std::string_view status = ArenaStatusText(ArenaStatus_RunFailed);
	std::copy(component.begin(), component.end(), error->component.begin());
	error->componentSize = static_cast<std::uint32_t>(component.size());
	std::copy(status.begin(), status.end(), error->status.begin());
	error->statusSize = static_cast<std::uint32_t>(status.size());
	if (detail.size() > error->detail.size())
		detail = "graphics_window_detail_capacity";
	std::copy(detail.begin(), detail.end(), error->detail.begin());
	error->detailSize = static_cast<std::uint32_t>(detail.size());
	error->code = ArenaStatus_RunFailed;
	return ArenaStatus_RunFailed;
}

double ReplayNow()
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void PauseReplayView(PhysicsArenaApp* app)
{
	if (app->replay != nullptr)
	{
		if (app->replay->comparison != nullptr && app->replay->comparison->peer != nullptr)
			SeekReplayClock(&app->replay->clock, app->replay->comparison->committedOrdinal, ReplayNow());
		else
			PlayReplayClock(&app->replay->clock, ReplayPlayState_Paused, ReplayNow());
	}
}

const char* ReplayOrdinalLabel(const NativeReplayView& replay)
{
	return replay.recording.workKind == benchmark_replay::WorkKind_QueryBatch ? "Batch" : "Step";
}

const char* ReplayStateLabel(const NativeReplayView& replay)
{
	if (replay.recording.frame.presence != PresenceStatus_Present)
		return "Recording read failed";
	if (replay.clock.state == ReplayPlayState_Playing)
		return "Playing";
	return replay.clock.ordinal == replay.clock.finalOrdinal && replay.clock.loop == ReplayLoopMode_Off ? "End of recording" : "Paused";
}

void SeekReplayView(PhysicsArenaApp* app, std::uint64_t ordinal)
{
	PauseReplayView(app);
	if (ordinal > app->replay->clock.finalOrdinal)
	{
		GraphicalWindowError(&app->results.replayError, "replay_step_out_of_range");
		return;
	}
	const ArenaStatus status = app->replay->comparison != nullptr && app->replay->comparison->peer != nullptr
	    ? ReadReplayComparisonOrdinal(app, ordinal) : SeekReplay(&app->replay->recording, ordinal, &app->results.replayError);
	if (status == ArenaStatus_Ok)
	{
		SeekReplayClock(&app->replay->clock, ordinal, ReplayNow());
		app->results.replayError = {};
		app->replay->ordinalError = PresenceStatus_Absent;
	}
	app->renderRequested = PresenceStatus_Present;
}

ArenaStatus ResizeReplayView(PhysicsArenaApp* app, std::uint32_t preset)
{
	if (preset >= kNativeRenderResolutionPresets.size())
		return GraphicalWindowError(&app->results.replayError, "replay_resolution_preset");
	if (app->graphicalWindowRestore.status == RenderWindowRestoreStatus_Ready &&
	    PlatformRaylibEndFixedDrawable(&app->platform, &app->graphicalWindowRestore) != RenderViewerStatus_Ok)
		return GraphicalWindowError(&app->results.replayError, "replay_window_restore_failed");
	const NativeRenderResolution& resolution = kNativeRenderResolutionPresets[preset];
	if (PlatformRaylibBeginFixedDrawable(&app->platform, static_cast<int>(resolution.widthPixels),
	                                     static_cast<int>(resolution.heightPixels),
	                                     &app->graphicalWindowRestore) != RenderViewerStatus_Ok ||
	    RendererRaylibResize(app->platform) != RenderViewerStatus_Ok)
		return GraphicalWindowError(&app->results.replayError, "exact_replay_resolution_unavailable");
	app->model.selection.resolutionPresetIndex = preset;
	app->graphicalWindowStatus = NativeGraphicalWindowStatus_Fixed;
	app->backbufferWidth = app->platform.width;
	app->backbufferHeight = app->platform.height;
	app->interactionRenderFrames = 3;
	app->renderRequested = PresenceStatus_Present;
	return ArenaStatus_Ok;
}

void ApplyReplayCamera(PhysicsArenaApp* app)
{
	NativeReplayView& replay = *app->replay;
	ResolvedVisualCamera camera = {};
	const ReplayCameraContext& cameraContext = replay.comparison != nullptr && replay.comparison->peer != nullptr
	    ? replay.comparison->cameraContext : replay.cameraContext;
	if (ComposeReplayCamera(cameraContext, static_cast<float>(app->ui.scene.width) / app->ui.scene.height,
	                        &app->model.selection.replayCamera, &camera) != VisualCameraStatus_Ok)
	{
		GraphicalWindowError(&app->results.replayError, "replay_camera_resolution_failed");
		return;
	}
	if (PhysicsSceneViewSetCameraControl(replay.sceneResources, &camera) != RenderViewerStatus_Ok)
		GraphicalWindowError(&app->results.replayError, "replay_camera_admission_failed");
}

void ToggleReplayPlayback(PhysicsArenaApp* app)
{
	if (app->replay->recording.frame.presence != PresenceStatus_Present ||
	    (app->replay->comparison != nullptr && app->replay->comparison->peer != nullptr && app->replay->comparison->pairPresence != PresenceStatus_Present))
		return;
	ReplayClock& clock = app->replay->clock;
	if (clock.state == ReplayPlayState_Paused && clock.ordinal == clock.finalOrdinal)
		SeekReplayView(app, 0);
	if (app->replay->recording.frame.presence != PresenceStatus_Present ||
	    (app->replay->comparison != nullptr && app->replay->comparison->peer != nullptr && app->replay->comparison->pairPresence != PresenceStatus_Present))
		return;
	if (app->replay->comparison != nullptr && app->replay->comparison->peer != nullptr && clock.state == ReplayPlayState_Playing)
		PauseReplayView(app);
	else
		PlayReplayClock(&clock, clock.state == ReplayPlayState_Playing ? ReplayPlayState_Paused : ReplayPlayState_Playing,
		                ReplayNow());
}

void UpdateReplayCameraInput(PhysicsArenaApp* app)
{
	const ImGuiIO& io = ImGui::GetIO();
	ResolvedVisualCamera resolved = {};
	const NativeReplayView& replay = *app->replay;
	const ReplayCameraContext& cameraContext = replay.comparison != nullptr && replay.comparison->peer != nullptr
	    ? replay.comparison->cameraContext : replay.cameraContext;
	if (ComposeReplayCamera(cameraContext, static_cast<float>(app->ui.scene.width) / app->ui.scene.height,
	                        &app->model.selection.replayCamera, &resolved) != VisualCameraStatus_Ok)
	{
		GraphicalWindowError(&app->results.replayError, "replay_camera_input_failed");
		return;
	}
	NativeReplayCameraPreferences& camera = app->model.selection.replayCamera;
	const float x = resolved.eye.x - resolved.target.x;
	const float y = resolved.eye.y - resolved.target.y;
	const float z = resolved.eye.z - resolved.target.z;
	const float distance = std::sqrt(x * x + y * y + z * z);
	const float yaw = std::atan2(x, z);
	const float pitch = std::asin(std::clamp(y / distance, -1.0f, 1.0f));
	if (!io.AppFocusLost && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
	{
		if (app->ui.sceneActive == PresenceStatus_Present && ImGui::IsMouseDragging(ImGuiMouseButton_Right) && !io.KeyShift)
		{
			camera.yawRadians = std::remainder(camera.yawRadians + io.MouseDelta.x * 0.006f, 6.2831853f);
			camera.pitchRadians += std::clamp(pitch + io.MouseDelta.y * 0.006f, -1.55f, 1.55f) - pitch;
		}
		if (app->ui.sceneActive == PresenceStatus_Present && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
		    (io.KeyShift && ImGui::IsMouseDragging(ImGuiMouseButton_Right))))
		{
			const float scale =
			    2 * distance * std::tan(resolved.verticalFovDegrees * 0.008726646f) / app->ui.scene.height;
			camera.pan[0] +=
			    (io.MouseDelta.x * std::cos(yaw) - io.MouseDelta.y * std::sin(yaw) * std::sin(pitch)) * scale;
			camera.pan[1] += io.MouseDelta.y * std::cos(pitch) * scale;
			camera.pan[2] -=
			    (io.MouseDelta.x * std::sin(yaw) + io.MouseDelta.y * std::cos(yaw) * std::sin(pitch)) * scale;
		}
		if (app->ui.sceneHovered == PresenceStatus_Present)
			camera.distanceScale = camera.distanceScale * std::exp(-io.MouseWheel * 0.12f);
	}
	if (!io.WantTextInput && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
	    app->ui.sceneFocused == PresenceStatus_Present && !io.AppFocusLost)
	{
		const int elevation =
		    static_cast<int>(ImGui::IsKeyDown(ImGuiKey_E)) - static_cast<int>(ImGui::IsKeyDown(ImGuiKey_Q));
		if (elevation != 0)
		{
			// limit the first movement after an idle frame or a window stall
			camera.pan[1] += elevation * distance * 0.25f * (std::min)(io.DeltaTime, 0.05f);
			app->interactionRenderFrames = 3;
		}
	}
}

void DrawReplayTransportButtons(PhysicsArenaApp* app)
{
	NativeReplayView& replay = *app->replay;
	ReplayClock& clock = replay.clock;
	ImGui::BeginDisabled(clock.ordinal == 0);
	if (ImGui::Button("First")) SeekReplayView(app, 0);
	ImGui::SameLine();
	if (ImGui::Button("Previous")) SeekReplayView(app, clock.ordinal - 1);
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button(clock.state == ReplayPlayState_Playing ? "Pause" : "Play")) ToggleReplayPlayback(app);
	ImGui::SameLine();
	ImGui::BeginDisabled(clock.ordinal == clock.finalOrdinal);
	if (ImGui::Button("Next")) SeekReplayView(app, clock.ordinal + 1);
	ImGui::SameLine();
	if (ImGui::Button("Last")) SeekReplayView(app, clock.finalOrdinal);
	ImGui::EndDisabled();
	ImGui::SameLine();
	std::uint64_t ordinal = clock.ordinal;
	const std::uint64_t zero = 0;
	ImGui::SetNextItemWidth(-1);
	if (ImGui::SliderScalar("##replay_timeline", ImGuiDataType_U64, &ordinal, &zero, &clock.finalOrdinal))
		SeekReplayView(app, ordinal);
	if (ImGui::IsItemActivated())
		PauseReplayView(app);
}

void DrawReplayOrdinalInput(PhysicsArenaApp* app)
{
	NativeReplayView& replay = *app->replay;
	ReplayClock& clock = replay.clock;
	const float scale = app->platform.dpiScale;
	std::uint64_t ordinal = clock.ordinal;
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(ReplayOrdinalLabel(replay));
	ImGui::SameLine();
	ImGui::SetNextItemWidth(80 * scale);
	if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_L, ImGuiInputFlags_RouteGlobal))
		ImGui::SetKeyboardFocusHere();
	if (replay.ordinalEditing != PresenceStatus_Present)
		std::snprintf(replay.ordinalText.data(), replay.ordinalText.size(), "%llu", static_cast<unsigned long long>(clock.ordinal));
	const int committed = ImGui::InputText("##replay_step", replay.ordinalText.data(), replay.ordinalText.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
	if (ImGui::IsItemActivated())
		PauseReplayView(app);
	replay.ordinalEditing = ImGui::IsItemActive() ? PresenceStatus_Present : PresenceStatus_Absent;
	if (committed != 0)
	{
		const char* end = replay.ordinalText.data() + std::strlen(replay.ordinalText.data());
		const std::from_chars_result parsed = std::from_chars(replay.ordinalText.data(), end, ordinal);
		if (parsed.ec != std::errc{} || parsed.ptr != end || ordinal > clock.finalOrdinal)
			replay.ordinalError = PresenceStatus_Present;
		else
			SeekReplayView(app, ordinal);
	}
	if (ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Escape))
		replay.ordinalError = PresenceStatus_Absent;
	ImGui::SameLine();
	ImGui::Text("/ %llu", static_cast<unsigned long long>(clock.finalOrdinal));
}

void DrawReplaySpeedAndLoop(PhysicsArenaApp* app)
{
	NativeReplayView& replay = *app->replay;
	ReplayClock& clock = replay.clock;
	const float scale = app->platform.dpiScale;
	ImGui::SetNextItemWidth(62 * scale);
	std::array<char, 16> speedLabel = {};
	std::snprintf(speedLabel.data(), speedLabel.size(), "%.2gx", clock.speed);
	if (ImGui::BeginCombo("##replay_speed", speedLabel.data()))
	{
		for (double speed : {0.25, 0.5, 1.0, 2.0, 4.0})
		{
			std::snprintf(speedLabel.data(), speedLabel.size(), "%.2gx", speed);
			if (ImGui::Selectable(speedLabel.data(), clock.speed == speed))
			{
				const ReplayPlayState state = clock.state;
				if (replay.comparison != nullptr && replay.comparison->peer != nullptr) PauseReplayView(app);
				SetReplaySpeed(&clock, speed, ReplayNow());
				PlayReplayClock(&clock, state, ReplayNow());
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	unsigned int loop = clock.loop == ReplayLoopMode_On ? 1 : 0;
	if (ImGui::CheckboxFlags("Loop", &loop, 1))
		clock.loop = loop != 0 ? ReplayLoopMode_On : ReplayLoopMode_Off;
}

void DrawReplayElapsedTime(const NativeReplayView& replay)
{
	const ReplayClock& clock = replay.clock;
	if (replay.recording.workKind == benchmark_replay::WorkKind_QueryBatch)
		ImGui::TextDisabled("Display: 30 frames/s");
	else
		ImGui::TextDisabled("%.3f / %.3f s", clock.ordinal * replay.recording.timestep, clock.finalOrdinal * replay.recording.timestep);
}

void DrawReplayOrdinalError(const NativeReplayView& replay)
{
	if (replay.ordinalError == PresenceStatus_Present)
		ImGui::TextColored(ImVec4(0.95f, 0.48f, 0.38f, 1), "Enter a %s from 0 to %llu", ReplayOrdinalLabel(replay), static_cast<unsigned long long>(replay.clock.finalOrdinal));
}

void DrawReplayTransport(PhysicsArenaApp* app)
{
	NativeReplayView& replay = *app->replay;
	ImGui::BeginDisabled(replay.recording.frame.presence != PresenceStatus_Present &&
	    (replay.comparison == nullptr || replay.comparison->peer == nullptr));
	DrawReplayTransportButtons(app);
	DrawReplayOrdinalInput(app);
	ImGui::SameLine();
	DrawReplaySpeedAndLoop(app);
	ImGui::SameLine();
	ImGui::TextUnformatted(ReplayStateLabel(replay));
	ImGui::SameLine();
	DrawReplayElapsedTime(replay);
	ImGui::EndDisabled();
	DrawReplayOrdinalError(replay);
}

void DrawReplayInspector(PhysicsArenaApp* app, PresenceStatus playbackSettings)
{
	NativeReplayView& replay = *app->replay;
	const float scale = app->platform.dpiScale;
	DrawPaneHeading("Recording", 28);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7 * scale, 7 * scale));
	ImGui::BeginChild("replay_details_content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::PopStyleVar();
	ImGui::Text("Frames: %s", FormatNativeCount(replay.clock.finalOrdinal + 1).data());
	ImGui::TextUnformatted(ReplayStateLabel(replay));
	if (playbackSettings == PresenceStatus_Present)
	{
		DrawPaneHeading("Playback", 28);
		ImGui::BeginDisabled(replay.recording.frame.presence != PresenceStatus_Present);
		DrawReplayOrdinalInput(app);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Speed");
		ImGui::SameLine();
		DrawReplaySpeedAndLoop(app);
		DrawReplayElapsedTime(replay);
		ImGui::EndDisabled();
		DrawReplayOrdinalError(replay);
	}
	DrawPaneHeading("Display", 28);
	ImGui::TextWrapped("Drawable window size");
	std::array<char, 64> resolutionLabel = {};
	std::snprintf(resolutionLabel.data(), resolutionLabel.size(), "%s (%d x %d)", app->graphicalWindowStatus == NativeGraphicalWindowStatus_Fixed ? "Fixed" : "Use window size", app->platform.width, app->platform.height);
	ImGui::SetNextItemWidth(-1);
	if (ImGui::BeginCombo("##replay_resolution", resolutionLabel.data()))
	{
		if (ImGui::Selectable("Use window size", app->graphicalWindowStatus != NativeGraphicalWindowStatus_Fixed) && app->graphicalWindowRestore.status == RenderWindowRestoreStatus_Ready)
		{
			if (PlatformRaylibEndFixedDrawable(&app->platform, &app->graphicalWindowRestore) != RenderViewerStatus_Ok || RendererRaylibResize(app->platform) != RenderViewerStatus_Ok)
				GraphicalWindowError(&app->results.replayError, "replay_window_restore_failed");
			else
				app->graphicalWindowStatus = NativeGraphicalWindowStatus_Inactive;
			app->backbufferWidth = app->platform.width;
			app->backbufferHeight = app->platform.height;
		}
		for (std::uint32_t index = 0; index < kNativeRenderResolutionPresets.size(); ++index)
		{
			const NativeRenderResolution& item = kNativeRenderResolutionPresets[index];
			if (NativeRenderResolutionAvailability(item, app->platform.desktopWidthPixels, app->platform.desktopHeightPixels) != PresenceStatus_Present)
				continue;
			std::snprintf(resolutionLabel.data(), resolutionLabel.size(), "%u x %u", item.widthPixels, item.heightPixels);
			if (ImGui::Selectable(resolutionLabel.data(), app->graphicalWindowStatus == NativeGraphicalWindowStatus_Fixed && index == app->model.selection.resolutionPresetIndex))
				ResizeReplayView(app, index);
		}
		ImGui::EndCombo();
	}
	ImGui::TextUnformatted("Appearance");
	int surface = static_cast<int>(replay.appearance.surface);
	ImGui::SetNextItemWidth(-1);
	if (ImGui::Combo("##replay_surface", &surface, "Solid\0Wireframe\0"))
		replay.appearance.surface = static_cast<ReplaySurfaceMode>(surface);
	PhysicsSceneViewSetAppearance(replay.sceneResources, replay.appearance);
	DrawPaneHeading("Camera", 28);
	if (ImGui::Button("Reset camera"))
		app->model.selection.replayCamera = {0, 0, 1, {}};
	if (ImGui::TreeNode("Controls and shortcuts"))
	{
		ImGui::TextWrapped("Space: play/pause\nRight drag: orbit\nMiddle drag or Shift+right drag: pan\nWheel over scene: zoom\nQ/E with scene focus: down/up\nR: reset camera\nCtrl+L: edit %s\nEnter: seek, Escape: cancel edit", ReplayOrdinalLabel(replay));
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Display details"))
	{
		const RenderAntialiasing aa = RendererRaylibAntialiasing();
		ImGui::Text("Scene pane: %d x %d px", app->ui.scene.width, app->ui.scene.height);
		if (aa.sampleBuffers > 0 && aa.samples > 1)
			ImGui::Text("AA: %dx MSAA", aa.samples);
		else
			ImGui::TextUnformatted("AA: unavailable (4x requested)");
		ImGui::TreePop();
	}
	if (ImGui::CollapsingHeader("About benchmark"))
		DrawSavedCaseExplanation(app);
	ImGui::EndChild();
}

void DrawReplayFailure(PhysicsArenaApp* app)
{
	ImGui::TextColored(ImVec4(0.95f, 0.48f, 0.38f, 1), "Recording could not be read. Playback is paused");
	const std::string_view detail(app->results.replayError.detail.data(), app->results.replayError.detailSize);
	if (detail == "recording_path_missing_or_reparse" || detail == "missing_or_truncated_recording" || detail == "recording_open_failed")
		ImGui::TextWrapped("The saved file is missing, incomplete or cannot be opened");
	else if (app->replay != nullptr && app->replay->recording.frame.presence != PresenceStatus_Present)
		ImGui::TextWrapped("A recorded frame is incomplete or invalid. Reopen the recording to try again");
	else
		ImGui::TextWrapped("The recording could not be admitted or displayed. See the exact reason below");
	if (ImGui::TreeNode("Error details"))
	{
		DrawStatusError(app->results.replayError);
		ImGui::TreePop();
	}
}

void DrawReplaySelectionIdentity(PhysicsArenaApp* app)
{
	const ResultViewModel& model = app->workspace.finalization.model;
	const std::string_view name = ResultViewTextView(&model, model.caseDisplayName);
	const std::string_view engine = ResultViewTextView(&model, model.engines[app->results.replayEngineIndex].provenanceLabel);
	ImGui::TextWrapped("%.*s | %.*s | Threads %u | Repeat %u", static_cast<int>(name.size()), name.data(),
	    static_cast<int>(engine.size()), engine.data(), model.threadCounts[app->results.replayThreadIndex], app->results.replayRepeatIndex + 1);
}

NativeUiCommand DrawReplayWorkspace(PhysicsArenaApp* app)
{
	NativeUiCommand command = NativeUiCommand_None;
	if (RunWorkspaceBusy(app) == PresenceStatus_Present)
	{
		ImGui::TextWrapped("Replay is unavailable while the current action owns the result workspace");
		return command;
	}
	if (app->results.rayLoadRequest == NativeRayLoadRequest_Image)
	{
		app->ui.scene = {};
		DrawReplaySelectionIdentity(app);
		ImGui::TextUnformatted("Opening recording...");
		app->results.rayLoadingPresented = PresenceStatus_Present;
		return NativeUiCommand_None;
	}
	if (app->results.replayOpenPending == PresenceStatus_Present)
	{
		app->ui.scene = {};
		DrawReplaySelectionIdentity(app);
		ImGui::TextUnformatted("Opening recording...");
		return NativeUiCommand_LoadReplay;
	}
	if (app->ui.replayPage == NativeReplayPage_Browser ||
	    (app->replay == nullptr && RayTracingReplayPresence(app) != PresenceStatus_Present))
	{
		app->ui.scene = {};
		app->ui.sceneHovered = app->ui.sceneFocused = app->ui.sceneActive = PresenceStatus_Absent;
		if (app->replay != nullptr || RayTracingReplayPresence(app) == PresenceStatus_Present)
		{
			if (ImGui::Button("Return to player"))
				app->ui.replayPage = NativeReplayPage_Player;
			ImGui::SameLine();
			if (ImGui::Button("Close recording"))
				CloseReplayView(app);
		}
		else if (app->results.replayError.code != ArenaStatus_Ok && app->results.modelPresence == PresenceStatus_Present)
		{
			DrawReplaySelectionIdentity(app);
			DrawReplayFailure(app);
			if (ImGui::Button("Retry opening"))
				command = NativeUiCommand_OpenReplay;
		}
		const NativeUiCommand browser = DrawReplayBrowser(app);
		return browser != NativeUiCommand_None ? browser : command;
	}
	if (app->replay != nullptr && app->replay->comparison != nullptr && app->replay->comparison->peer != nullptr)
	{
		DrawReplayComparisonWorkspace(app);
		DrawReplayComparisonPicker(app);
		return command;
	}
	const std::array<std::uint32_t, 3>& selection = app->replay != nullptr
	    ? app->replay->selection : RayTracingReplaySelection(app);
	const float scale = app->platform.dpiScale;
	const int compact = ImGui::GetContentRegionAvail().x / scale < 1000;
	const ResultViewModel& model = app->workspace.finalization.model;
	const std::string_view caseName = ResultViewTextView(&model, model.caseDisplayName);
	const std::string_view engineName = ResultViewTextView(&model, model.engines[selection[0]].provenanceLabel);
	std::array<char, 2 * kCatalogTextValueCapacity + kEngineProvenanceLabelCapacity> identity = {};
	std::snprintf(identity.data(), identity.size(), "%.*s | %.*s | Threads %u | Repeat %u", static_cast<int>(caseName.size()), caseName.data(),
	    static_cast<int>(engineName.size()), engineName.data(), model.threadCounts[selection[1]], selection[2] + 1);
	const float identityHeight = ImGui::CalcTextSize(identity.data(), nullptr, false, ImGui::GetContentRegionAvail().x - 14 * scale).y;
	const int transportRows = app->replay != nullptr && compact != 0 ? 2 : 0;
	const float toolbarHeight = identityHeight + (1 + transportRows) * ImGui::GetFrameHeight() + (16 + 4 * transportRows) * scale +
	    (compact != 0 && app->replay != nullptr && app->replay->ordinalError == PresenceStatus_Present ? ImGui::GetTextLineHeightWithSpacing() : 0);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7 * scale, 5 * scale));
	ImGui::BeginChild("replay_toolbar", ImVec2(0, toolbarHeight), ImGuiChildFlags_AlwaysUseWindowPadding,
	    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7 * scale, 4 * scale));
	ImGui::TextWrapped("%s", identity.data());
	if (ImGui::Button("Browse recordings"))
	{
		PauseReplayView(app);
		app->results.replayEngineIndex = selection[0];
		app->results.replayThreadIndex = selection[1];
		app->results.replayRepeatIndex = selection[2];
		app->results.recordingRefreshPending = PresenceStatus_Present;
		app->ui.replayPage = NativeReplayPage_Browser;
	}
	ImGui::SameLine();
	if (ImGui::Button("Open folder"))
		command = NativeUiCommand_OpenResultFolder;
	if (compact != 0)
	{
		ImGui::SameLine();
		if (ImGui::Button(app->ui.replayDetailsVisible == PresenceStatus_Present ? "Back to replay" : "Details"))
		{
			PauseReplayView(app);
			app->ui.replayDetailsVisible = app->ui.replayDetailsVisible == PresenceStatus_Present ? PresenceStatus_Absent : PresenceStatus_Present;
		}
	}
	if (app->replay != nullptr)
	{
		ImGui::SameLine();
		if (ImGui::Button("Compare against..."))
			BeginReplayComparisonPicker(app);
		if (compact != 0)
		{
			ImGui::BeginDisabled(app->ui.replayDetailsVisible == PresenceStatus_Present);
			DrawReplayTransport(app);
			ImGui::EndDisabled();
		}
	}
	ImGui::PopStyleVar();
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	if (RayTracingReplayPresence(app) == PresenceStatus_Present)
	{
		DrawRayTracingReplay(app);
		return command;
	}
	NativeReplayView& replay = *app->replay;
	if (app->results.replayError.code != ArenaStatus_Ok)
	{
		DrawReplayFailure(app);
		if (ImGui::Button("Retry opening"))
		{
			app->results.replayEngineIndex = replay.selection[0];
			app->results.replayThreadIndex = replay.selection[1];
			app->results.replayRepeatIndex = replay.selection[2];
			command = NativeUiCommand_OpenReplay;
		}
	}
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	const float inspectorWidth = compact == 0 ? 288 * scale : 0;
	const float sceneWidth = ImGui::GetContentRegionAvail().x - inspectorWidth - (compact == 0 ? ImGui::GetStyle().ItemSpacing.x : 0);
	if (compact == 0 || app->ui.replayDetailsVisible != PresenceStatus_Present)
	{
		ImGui::BeginChild("replay_scene", ImVec2(sceneWidth, 0), ImGuiChildFlags_None,
		                  ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		if (compact == 0)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7 * scale, 5 * scale));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7 * scale, 4 * scale));
			ImGui::BeginChild("replay_playback_toolbar", ImVec2(0, ImGui::GetFrameHeight() + 10 * scale),
			    ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			ImGui::BeginDisabled(replay.recording.frame.presence != PresenceStatus_Present);
			DrawReplayTransportButtons(app);
			ImGui::EndDisabled();
			ImGui::EndChild();
			ImGui::PopStyleVar(2);
		}
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 size = ImGui::GetContentRegionAvail();
		app->ui.scene = {static_cast<int>(origin.x), static_cast<int>(origin.y), static_cast<int>(size.x), static_cast<int>(size.y)};
		ImGui::InvisibleButton("saved_scene", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
		    ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_EnableNav);
		app->ui.sceneHovered = ImGui::IsItemHovered() ? PresenceStatus_Present : PresenceStatus_Absent;
		app->ui.sceneFocused = ImGui::IsItemFocused() ? PresenceStatus_Present : PresenceStatus_Absent;
		app->ui.sceneActive = ImGui::IsItemActive() ? PresenceStatus_Present : PresenceStatus_Absent;
		ImGui::EndChild();
	}
	else
	{
		PauseReplayView(app);
		app->ui.scene = {};
		app->ui.sceneHovered = PresenceStatus_Absent;
		app->ui.sceneFocused = PresenceStatus_Absent;
		app->ui.sceneActive = PresenceStatus_Absent;
	}
	if (compact == 0 || app->ui.replayDetailsVisible == PresenceStatus_Present)
	{
		if (compact == 0)
			ImGui::SameLine();
		ImGui::BeginChild("replay_inspector", ImVec2(0, 0), ImGuiChildFlags_Borders);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7 * scale, 4 * scale));
		DrawReplayInspector(app, compact == 0 ? PresenceStatus_Present : PresenceStatus_Absent);
		ImGui::PopStyleVar();
		ImGui::EndChild();
	}
	ImGui::PopStyleVar(2);
	const ImGuiIO& io = ImGui::GetIO();
	if (!io.AppFocusLost && app->ui.scene.width > 0 && replay.recording.frame.presence == PresenceStatus_Present &&
	    !io.WantTextInput && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
	    (!ImGui::IsAnyItemFocused() || app->ui.sceneFocused == PresenceStatus_Present))
	{
		if (ImGui::Shortcut(ImGuiKey_Space, ImGuiInputFlags_RouteGlobal)) ToggleReplayPlayback(app);
		if (ImGui::Shortcut(ImGuiKey_R, ImGuiInputFlags_RouteGlobal))
			app->model.selection.replayCamera = {0, 0, 1, {}};
		if (ImGui::Shortcut(ImGuiKey_LeftArrow, ImGuiInputFlags_RouteGlobal)) SeekReplayView(app, replay.clock.ordinal == 0 ? 0 : replay.clock.ordinal - 1);
		if (ImGui::Shortcut(ImGuiKey_RightArrow, ImGuiInputFlags_RouteGlobal)) SeekReplayView(app, (std::min)(replay.clock.ordinal + 1, replay.clock.finalOrdinal));
		if (ImGui::Shortcut(ImGuiKey_Home, ImGuiInputFlags_RouteGlobal)) SeekReplayView(app, 0);
		if (ImGui::Shortcut(ImGuiKey_End, ImGuiInputFlags_RouteGlobal)) SeekReplayView(app, replay.clock.finalOrdinal);
	}
	DrawReplayComparisonPicker(app);
	return command;
}

ArenaStatus OpenReplayView(PhysicsArenaApp* app, NativeReplayStartup startup)
{
	app->results.replayError = {};
	if (ActiveAction(app->action) == PresenceStatus_Present || app->results.modelPresence != PresenceStatus_Present)
		return GraphicalWindowError(&app->results.replayError, "replay_requires_idle_completed_result");
	if (app->replay != nullptr)
	{
		ReleaseReplayComparison(app);
		PhysicsSceneViewReleaseScene(app->replay->sceneResources);
		CloseReplay(&app->replay->recording);
		delete app->replay;
		app->replay = nullptr;
	}
	CloseRayTracingReplay(app);
	app->ui.replayDetailsVisible = PresenceStatus_Absent;
	app->ui.replayPage = NativeReplayPage_Player;
	app->model.view = NativeArenaView_Replay;
	if (app->workspace.finalization.manifest.recordingKind == RecordingKind_NativeRayHits)
		return SelectRayTracingImage(app, 0, benchmark_ray::Phase_Primary, benchmark_ray::Api_Ordinary, 0);
	NativeReplayView* replay = new (std::nothrow) NativeReplayView{};
	if (replay == nullptr)
		return GraphicalWindowError(&app->results.replayError, "replay_storage_allocation_failed");
	ArenaStatus status = OpenResultReplay(&app->model.catalog, &app->workspace.finalization.manifest,
	                                      app->results.selectedDirectory.data(), app->results.replayEngineIndex,
	                                      app->results.replayThreadIndex, app->results.replayRepeatIndex,
	                                      &replay->recording, &app->results.replayError);
	const EffectiveRunConfiguration* savedConfiguration = ResultConfiguration(&app->workspace.finalization.manifest);
	app->model.selection.replayCamera = {0, 0, 1, {}};
	if (status == ArenaStatus_Ok &&
	    (PhysicsSceneViewInstallScene(&replay->recording.scene, &replay->sceneResources) != RenderViewerStatus_Ok ||
	     PrepareReplayCamera(replay->recording.scene, savedConfiguration == nullptr ? nullptr : &savedConfiguration->execution, &replay->cameraContext) != VisualCameraStatus_Ok))
		status = GraphicalWindowError(&app->results.replayError, "replay_scene_install_failed");
	if (status == ArenaStatus_Ok && startup == NativeReplayStartup_Paused)
		status = ResizeReplayView(app, app->model.selection.resolutionPresetIndex);
	if (status != ArenaStatus_Ok)
	{
		PhysicsSceneViewReleaseScene(replay->sceneResources);
		CloseReplay(&replay->recording);
		delete replay;
		CloseReplayView(app);
		app->model.view = NativeArenaView_Replay;
		return status;
	}
	replay->selection = {app->results.replayEngineIndex, app->results.replayThreadIndex, app->results.replayRepeatIndex};
	InitializeReplayClock(replay->recording, &replay->clock);
	app->replay = replay;
	app->model.view = NativeArenaView_Replay;
	if (startup == NativeReplayStartup_Playing)
		PlayReplayClock(&replay->clock, ReplayPlayState_Playing, ReplayNow());
	app->results.replayError = {};
	return ArenaStatus_Ok;
}

void CloseReplayView(PhysicsArenaApp* app)
{
	app->results.replayOpenPending = PresenceStatus_Absent;
	CloseRayTracingReplay(app);
	if (app->replay != nullptr)
	{
		if (app->replay->selection != std::array<std::uint32_t, 3>{app->results.replayEngineIndex,
		    app->results.replayThreadIndex, app->results.replayRepeatIndex})
			app->results.replayError = {};
		ReleaseReplayComparison(app);
		PhysicsSceneViewReleaseScene(app->replay->sceneResources);
		CloseReplay(&app->replay->recording);
		delete app->replay;
		app->replay = nullptr;
	}
	if (app->graphicalWindowRestore.status == RenderWindowRestoreStatus_Ready)
	{
		if (PlatformRaylibEndFixedDrawable(&app->platform, &app->graphicalWindowRestore) != RenderViewerStatus_Ok ||
		    RendererRaylibResize(app->platform) != RenderViewerStatus_Ok)
			GraphicalWindowError(&app->uiError, "replay_window_restore_failed");
		app->backbufferWidth = app->platform.width;
		app->backbufferHeight = app->platform.height;
	}
	app->graphicalWindowStatus = NativeGraphicalWindowStatus_Inactive;
	SetWindowTextW(static_cast<HWND>(app->platform.nativeHandle), L"Physics Arena");
	app->renderRequested = PresenceStatus_Present;
	app->interactionRenderFrames = 3;
}

void UpdateReplayView(PhysicsArenaApp* app)
{
	if (app->replay == nullptr)
		return;
	NativeReplayView& replay = *app->replay;
	if (replay.comparison != nullptr)
		UpdateReplayComparisonPicker(app);
	if (app->ui.replayPage == NativeReplayPage_Browser)
	{
		PauseReplayView(app);
		return;
	}
	const double now = ReplayNow();
	SetReplayVisibility(&replay.clock,
	                    IsIconic(static_cast<HWND>(app->platform.nativeHandle)) ? ReplayVisibility_Minimized
	                                                                            : ReplayVisibility_Visible,
	                    now);
	const std::uint64_t ordinal = AdvanceReplayClock(&replay.clock, now);
	if (replay.comparison != nullptr && replay.comparison->peer != nullptr)
	{
		if (ordinal != replay.comparison->committedOrdinal)
		{
			if (ReadReplayComparisonOrdinal(app, ordinal) == ArenaStatus_Ok)
				replay.clock.ordinal = ordinal;
			app->renderRequested = PresenceStatus_Present;
		}
		return;
	}
	if (replay.recording.frame.presence != PresenceStatus_Present)
	{
		PauseReplayView(app);
		return;
	}
	if (ordinal == replay.recording.frame.ordinal)
		return;
	if (SeekReplay(&replay.recording, ordinal, &app->results.replayError) != ArenaStatus_Ok)
		SeekReplayClock(&replay.clock, replay.recording.frame.ordinal, now);
	app->renderRequested = PresenceStatus_Present;
}

void DrawReplayScene(PhysicsArenaApp* app)
{
	NativeReplayView& replay = *app->replay;
	const ResultViewModel& model = app->workspace.finalization.model;
	const std::string_view engine = ResultViewTextView(&model, model.engines[replay.selection[0]].provenanceLabel);
	std::array<wchar_t, 192> title = {};
	std::swprintf(title.data(), title.size(), L"Physics Arena - Replay %.*hs | Threads %u | Repeat %u - AA %dx - %hs %llu/%llu",
	    static_cast<int>((std::min)(engine.size(), std::size_t{70})), engine.data(), model.threadCounts[replay.selection[1]], replay.selection[2] + 1,
	    RendererRaylibAntialiasing().sampleBuffers > 0 ? RendererRaylibAntialiasing().samples : 0, ReplayOrdinalLabel(replay),
	    static_cast<unsigned long long>(replay.clock.ordinal), static_cast<unsigned long long>(replay.clock.finalOrdinal));
	if (replay.comparison != nullptr && replay.comparison->peer != nullptr)
		std::swprintf(title.data(), title.size(), L"Physics Arena - Replay %.*hs | Threads %u | Repeat %u versus %.*hs - %hs %llu/%llu",
		    static_cast<int>((std::min)(engine.size(), std::size_t{40})), engine.data(), model.threadCounts[replay.selection[1]], replay.selection[2] + 1,
		    static_cast<int>((std::min)(replay.comparison->peer->identity.size(), std::size_t{60})),
		    replay.comparison->peer->identity.c_str(), ReplayOrdinalLabel(replay),
		    static_cast<unsigned long long>(replay.clock.ordinal), static_cast<unsigned long long>(replay.clock.finalOrdinal));
	if (replay.comparison != nullptr && replay.comparison->peer != nullptr && replay.comparison->pairPresence != PresenceStatus_Present)
		std::swprintf(title.data(), title.size(), L"Physics Arena - Replay - Comparison read failed - %hs %llu/%llu",
		    ReplayOrdinalLabel(replay), static_cast<unsigned long long>(replay.clock.ordinal), static_cast<unsigned long long>(replay.clock.finalOrdinal));
	else if (replay.recording.frame.presence != PresenceStatus_Present)
		std::swprintf(title.data(), title.size(), L"Physics Arena - Replay %.*hs | Threads %u | Repeat %u - Frame unavailable",
		    static_cast<int>((std::min)(engine.size(), std::size_t{70})), engine.data(), model.threadCounts[replay.selection[1]], replay.selection[2] + 1);
	std::array<wchar_t, 192> currentTitle = {};
	GetWindowTextW(static_cast<HWND>(app->platform.nativeHandle), currentTitle.data(),
	               static_cast<int>(currentTitle.size()));
	if (std::wcscmp(currentTitle.data(), title.data()) != 0)
		SetWindowTextW(static_cast<HWND>(app->platform.nativeHandle), title.data());
	if (app->ui.replayPage != NativeReplayPage_Player || app->ui.scene.width <= 0 || app->ui.scene.height <= 0)
		return;
	if (replay.comparison != nullptr && replay.comparison->peer != nullptr)
	{
		DrawReplayComparisonScenes(app);
		return;
	}
	VisualSnapshot snapshot = {};
	snapshot.identity = replay.recording.scene.identity;
	snapshot.scene = &replay.recording.scene;
	snapshot.transforms = replay.recording.frame.transforms.data();
	snapshot.transformCount = static_cast<int>(replay.recording.frame.transforms.size());
	snapshot.transformCapacity = snapshot.transformCount;
	snapshot.debugPrimitives = replay.recording.frame.debug.data();
	snapshot.debugPrimitiveCount = static_cast<int>(replay.recording.frame.debug.size());
	snapshot.debugPrimitiveCapacity = snapshot.debugPrimitiveCount;
	snapshot.completedStepCount = static_cast<int>(replay.recording.frame.ordinal);
	snapshot.stepIndex = snapshot.completedStepCount;
	if (replay.recording.frame.presence == PresenceStatus_Present)
	{
		UpdateReplayCameraInput(app);
		ApplyReplayCamera(app);
		if (PhysicsSceneViewDrawSnapshot(replay.sceneResources, app->platform, app->ui.scene, snapshot, app->ui.scene) != RenderViewerStatus_Ok)
			GraphicalWindowError(&app->results.replayError, "replay_draw_failed");
	}
}

} // namespace benchmark_visual
