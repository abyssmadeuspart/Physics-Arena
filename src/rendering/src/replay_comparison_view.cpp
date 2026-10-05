#include "benchmark_visual/native_replay_comparison.h"
#include "launcher_app_internal.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <cmath>

namespace benchmark_visual
{
using namespace physics_arena;

void DrawReplayComparisonRunPicker(PhysicsArenaApp* app)
{
	NativeReplayComparison& comparison = *app->replay->comparison;
	const float scale = app->platform.dpiScale;
	const float width = ImGui::GetContentRegionAvail().x;
	ImGui::TextUnformatted("Saved run");
	const ImVec2 preview = ImGui::GetCursorScreenPos();
	ImGui::SetNextItemWidth(-1);
	ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, 300 * scale));
	if (ImGui::BeginCombo("##comparison_run", ""))
	{
		if (BeginRunPickerTable(PresenceStatus_Absent) != 0)
		{
			for (std::uint32_t run = 0; run < comparison.candidates.size(); ++run)
			{
				const NativeReplayComparisonCandidate& candidate = comparison.candidates[run];
				ImGui::PushID(static_cast<int>(run));
				ImGui::BeginDisabled(candidate.error.code != ArenaStatus_Ok);
				if (DrawRunPickerRow(app->workspace.finalization.indexes.records[candidate.runIndex],
				    comparison.runChoice == run ? PresenceStatus_Present : PresenceStatus_Absent,
				    PresenceStatus_Absent) != 0)
				{
					SelectReplayComparisonRun(app, run);
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndDisabled();
				if (candidate.error.code != ArenaStatus_Ok)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextWrapped("%.*s", static_cast<int>(candidate.error.detailSize), candidate.error.detail.data());
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::EndCombo();
	}
	if (comparison.runChoice < comparison.candidates.size())
		DrawRunPickerLabel(app->workspace.finalization.indexes.records[comparison.candidates[comparison.runChoice].runIndex],
		    ImVec2(preview.x + 7 * scale, preview.y + 3 * scale), width - 42 * scale);
	else
		ImGui::GetWindowDrawList()->AddText(ImVec2(preview.x + 7 * scale, preview.y + 3 * scale),
		    ImGui::GetColorU32(ImGuiCol_TextDisabled), "Choose a compatible saved run");
}

void DrawReplayComparisonRecordingChoices(PhysicsArenaApp* app)
{
	NativeReplayComparison& comparison = *app->replay->comparison;
	if (comparison.runChoice >= comparison.candidates.size())
	{
		ImGui::TextWrapped("No other recording is available in the primary run. Choose another compatible saved run");
		for (const NativeReplayComparisonCandidate& candidate : comparison.candidates)
			if (candidate.error.code != ArenaStatus_Ok)
				ImGui::TextDisabled("%.*s", static_cast<int>(candidate.error.detailSize), candidate.error.detail.data());
		return;
	}
	const NativeReplayComparisonCandidate& candidate = comparison.candidates[comparison.runChoice];
	if (candidate.choices.empty())
	{
		ImGui::TextWrapped("No other saved recording is available in this run");
		ImGui::TextWrapped("Choose another compatible saved run to compare against the primary recording");
		return;
	}
	const float scale = app->platform.dpiScale;
	ImGui::TextDisabled("Choose one recording, then confirm Compare");
	if (ImGui::BeginTable("comparison_recordings", 5,
	    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoSavedSettings,
	    ImVec2(0, ImGui::GetContentRegionAvail().y)))
	{
		ImGui::TableSetupColumn("Engine", ImGuiTableColumnFlags_WidthStretch, 1.2f);
		ImGui::TableSetupColumn("Threads", ImGuiTableColumnFlags_WidthFixed, 58 * scale);
		ImGui::TableSetupColumn("Repeat", ImGuiTableColumnFlags_WidthFixed, 52 * scale);
		ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.2f);
		ImGui::TableSetupColumn("Saved size", ImGuiTableColumnFlags_WidthFixed, 88 * scale);
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();
		for (std::uint32_t row = 0; row < candidate.choices.size(); ++row)
		{
			const NativeReplayComparisonChoice& choice = candidate.choices[row];
			const NativeValueText bytes = FormatNativeBytes(choice.bytes);
			ImGui::PushID(static_cast<int>(row));
			if (DrawRecordingRow(choice.engineLabel, choice.threads, choice.tuple[2],
			    choice.state == ReplayComparisonChoiceState_Ready ? "Available" : choice.reason.c_str(), bytes.data(), choice.bytes,
			    PresenceStatus_Present, comparison.tupleChoice == choice.tuple ? PresenceStatus_Present : PresenceStatus_Absent,
			    choice.state == ReplayComparisonChoiceState_Ready ? PresenceStatus_Present : PresenceStatus_Absent) != 0)
			{
				comparison.tupleChoice = choice.tuple;
				comparison.selectionError = {};
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
}

void DrawReplayComparisonPicker(PhysicsArenaApp* app)
{
	if (app->replay == nullptr || app->replay->comparison == nullptr ||
	    app->replay->comparison->picker == ReplayComparisonPickerState_Closed)
		return;
	NativeReplayComparison& comparison = *app->replay->comparison;
	if (!ImGui::IsPopupOpen("Compare saved recordings"))
		ImGui::OpenPopup("Compare saved recordings");
	const float scale = app->platform.dpiScale;
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const std::size_t rows = comparison.runChoice < comparison.candidates.size()
	    ? comparison.candidates[comparison.runChoice].choices.size() : 0;
	const float height = (206 + 44 * (std::min)(std::size_t{8}, (std::max)(std::size_t{2}, rows))) * scale;
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2((std::min)(840 * scale, viewport->WorkSize.x - 32 * scale),
	    (std::min)(height, viewport->WorkSize.y - 32 * scale)), ImGuiCond_Always);
	if (!ImGui::BeginPopupModal("Compare saved recordings", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar))
		return;
	const ResultViewModel& model = app->workspace.finalization.model;
	const std::string_view name = ResultViewTextView(&model, model.caseDisplayName);
	const std::string_view engine = ResultViewTextView(&model, model.engines[app->replay->selection[0]].provenanceLabel);
	ImGui::TextWrapped("Primary case: %.*s", static_cast<int>(name.size()), name.data());
	ImGui::TextWrapped("%.*s | Threads %u | Repeat %u", static_cast<int>(engine.size()), engine.data(),
	    model.threadCounts[app->replay->selection[1]], app->replay->selection[2] + 1);
	if (comparison.selectionError.code != ArenaStatus_Ok)
		ImGui::TextWrapped("%.*s", static_cast<int>(comparison.selectionError.detailSize), comparison.selectionError.detail.data());
	ImGui::BeginChild("comparison_choices", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_None);
	if (comparison.picker == ReplayComparisonPickerState_Scanning)
		ImGui::Text("Loading saved configurations... %u / %u", comparison.scanIndex, app->results.recordingRunCount);
	else if (comparison.candidates.empty())
		ImGui::TextWrapped("No other saved recording is available. Save a recording of this exact case to compare its behavior");
	else
	{
		DrawReplayComparisonRunPicker(app);
		DrawReplayComparisonRecordingChoices(app);
	}
	ImGui::EndChild();
	ImGui::BeginDisabled(comparison.picker != ReplayComparisonPickerState_Ready || comparison.tupleChoice[2] == UINT32_MAX);
	if (ImGui::Button("Compare") && OpenReplayComparisonPeer(app) == ArenaStatus_Ok)
		ImGui::CloseCurrentPopup();
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
	{
		CancelReplayComparisonPicker(app);
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void DrawComparisonPane(PhysicsArenaApp* app, ReplayComparisonPane pane, float width, float height, float headingHeight)
{
	NativeReplayComparison& comparison = *app->replay->comparison;
	ImGui::PushID(static_cast<int>(pane));
	ImGui::BeginChild("comparison_pane", ImVec2(width, height), ImGuiChildFlags_None,
	    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	const NativeReplayView& primary = *app->replay;
	const ResultViewModel& model = app->workspace.finalization.model;
	const std::string_view engine = pane == ReplayComparisonPane_Primary
	    ? ResultViewTextView(&model, model.engines[primary.selection[0]].provenanceLabel) : comparison.peer->engineLabel;
	const std::uint32_t threads = pane == ReplayComparisonPane_Primary ? model.threadCounts[primary.selection[1]]
	    : comparison.peer->manifest.threadCounts[comparison.peer->tuple[1]];
	const std::uint32_t repeat = pane == ReplayComparisonPane_Primary ? primary.selection[2] : comparison.peer->tuple[2];
	const float headingTop = ImGui::GetCursorPosY();
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Heading), 12 * kNativeUiBodyLineScale);
	ImGui::TextWrapped("%s: %.*s", pane == ReplayComparisonPane_Primary ? "Primary" : "Comparison",
	    static_cast<int>(engine.size()), engine.data());
	ImGui::PopFont();
	ImGui::TextDisabled("Threads %u | Repeat %u", threads, repeat + 1);
	ImGui::SetCursorPosY(headingTop + headingHeight);
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 size = ImGui::GetContentRegionAvail();
	RenderViewport& viewport = pane == ReplayComparisonPane_Primary ? comparison.primaryViewport : comparison.peerViewport;
	viewport = {static_cast<int>(origin.x), static_cast<int>(origin.y), static_cast<int>(size.x), static_cast<int>(size.y)};
	const ImVec2 clipMinimum = ImGui::GetWindowDrawList()->GetClipRectMin();
	const ImVec2 clipMaximum = ImGui::GetWindowDrawList()->GetClipRectMax();
	RenderViewport& clip = pane == ReplayComparisonPane_Primary ? comparison.primaryClip : comparison.peerClip;
	clip.x = (std::max)(viewport.x, static_cast<int>(std::ceil(clipMinimum.x)));
	clip.y = (std::max)(viewport.y, static_cast<int>(std::ceil(clipMinimum.y)));
	clip.width = (std::min)(viewport.x + viewport.width, static_cast<int>(std::floor(clipMaximum.x))) - clip.x;
	clip.height = (std::min)(viewport.y + viewport.height, static_cast<int>(std::floor(clipMaximum.y))) - clip.y;
	ImGui::InvisibleButton("recorded_scene", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
	    ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_EnableNav);
	if (ImGui::IsItemHovered() || ImGui::IsItemActive() || (ImGui::IsItemFocused() &&
	    app->ui.sceneHovered != PresenceStatus_Present && app->ui.sceneActive != PresenceStatus_Present))
	{
		comparison.activePane = pane;
		app->ui.sceneHovered = ImGui::IsItemHovered() ? PresenceStatus_Present : PresenceStatus_Absent;
		app->ui.sceneActive = ImGui::IsItemActive() ? PresenceStatus_Present : PresenceStatus_Absent;
		app->ui.sceneFocused = ImGui::IsItemFocused() ? PresenceStatus_Present : PresenceStatus_Absent;
	}
	ImGui::GetWindowDrawList()->AddRect(origin, ImVec2(origin.x + size.x - 1, origin.y + size.y - 1),
	    ImGui::GetColorU32(comparison.activePane == pane ? ImGuiCol_ButtonActive : ImGuiCol_Border), 0, 0, 2 * app->platform.dpiScale);
	ImGui::EndChild();
	ImGui::PopID();
}

void DrawReplayComparisonWorkspace(PhysicsArenaApp* app)
{
	NativeReplayView& primary = *app->replay;
	NativeReplayComparison& comparison = *primary.comparison;
	const float scale = app->platform.dpiScale;
	const ResultViewModel& model = app->workspace.finalization.model;
	const std::string_view caseName = ResultViewTextView(&model, model.caseDisplayName);
	const std::string_view run = ResultViewTextView(&model, model.runId);
	const std::string_view engine = ResultViewTextView(&model, model.engines[primary.selection[0]].provenanceLabel);
	ImGui::BeginChild("comparison_workspace", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
	ImGui::TextWrapped("%.*s", static_cast<int>(caseName.size()), caseName.data());
	NativeRunLabelProjection primaryRun = {};
	ProjectNativeRunLabel(run, model.threadCount, model.repeatCount, &primaryRun);
	ImGui::TextWrapped("Primary: %.*s | Threads %u | Repeat %u | %s", static_cast<int>(engine.size()), engine.data(),
	    model.threadCounts[primary.selection[1]], primary.selection[2] + 1, primaryRun.text.data());
	ImGui::TextWrapped("Comparison: %s | Threads %u | Repeat %u | %s", comparison.peer->engineLabel.c_str(),
	    comparison.peer->manifest.threadCounts[comparison.peer->tuple[1]], comparison.peer->tuple[2] + 1, comparison.peer->runLabel.c_str());
	if (ImGui::Button("Compare against...")) BeginReplayComparisonPicker(app);
	ImGui::SameLine();
	if (ImGui::Button("Stop comparison"))
	{
		StopReplayComparison(app);
		ImGui::EndChild();
		return;
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse recordings"))
	{
		PauseReplayView(app);
		app->ui.replayPage = NativeReplayPage_Browser;
		app->ui.scene = {};
		ImGui::EndChild();
		return;
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset camera")) app->model.selection.replayCamera = {0, 0, 1, {}};
	ImGui::SameLine();
	int appearance = static_cast<int>(primary.appearance.surface);
	ImGui::SetNextItemWidth(95 * scale);
	if (ImGui::Combo("##comparison_surface", &appearance, "Solid\0Wireframe\0"))
		primary.appearance.surface = static_cast<ReplaySurfaceMode>(appearance);
	ImGui::Text("Common %s: 0 - %llu | Full lengths: %llu / %llu", ReplayOrdinalLabel(primary),
	    static_cast<unsigned long long>(primary.clock.finalOrdinal), static_cast<unsigned long long>(primary.recording.layout.frameCount - 1),
	    static_cast<unsigned long long>(comparison.peer->recording.layout.frameCount - 1));
	if (ImGui::TreeNode("Recording details"))
	{
		const ResultManifestRecord* manifests[] = {&app->workspace.finalization.manifest, &comparison.peer->manifest};
		const std::uint32_t engines[] = {primary.selection[0], comparison.peer->tuple[0]};
		const std::string_view labels[] = {engine, comparison.peer->engineLabel};
		const std::string_view runs[] = {std::string_view(primaryRun.text.data(), primaryRun.size), comparison.peer->runLabel};
		const ObservationOutcome outcomes[] = {comparison.primaryOutcome, comparison.peer->outcome};
		const StatusRecord* outcomeErrors[] = {&comparison.primaryOutcomeError, &comparison.peer->outcomeError};
		if (ImGui::BeginTable("comparison_recording_details", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableNextRow();
			for (int pane = 0; pane < 2; ++pane)
			{
				ImGui::TableSetColumnIndex(pane);
				ImGui::PushID(pane);
				ImGui::PushFont(NativeUiFontFace(NativeUiFont_Heading), 12 * kNativeUiBodyLineScale);
				ImGui::TextWrapped("%s: %.*s", pane == 0 ? "Primary" : "Comparison", static_cast<int>(labels[pane].size()), labels[pane].data());
				ImGui::PopFont();
				ImGui::TextWrapped("Run: %.*s", static_cast<int>(runs[pane].size()), runs[pane].data());
				const std::string_view outcome = outcomes[pane] == ObservationOutcome_Unknown ? "Not recorded" : ObservationOutcomeText(outcomes[pane]);
				ImGui::Text("Recorded outcome: %.*s", static_cast<int>(outcome.size()), outcome.data());
				if (manifests[pane]->verificationMode == VerificationMode_Off)
					ImGui::TextWrapped("Verification Off: physical quality not checked");
				if (outcomeErrors[pane]->code != ArenaStatus_Ok)
					ImGui::TextWrapped("%.*s", static_cast<int>(outcomeErrors[pane]->detailSize), outcomeErrors[pane]->detail.data());
				DrawSavedEngineSettings(manifests[pane]->configuration.selectedEngineSettings[engines[pane]],
				    manifests[pane]->configuration.execution.fixtureKind, CatalogTextView(&app->model.catalog,
				    app->model.catalog.engines[manifests[pane]->engines[engines[pane]].engineIndex].id), scale);
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::TreePop();
	}
	if (comparison.error.code != ArenaStatus_Ok)
	{
		ImGui::TextWrapped("%s read failed. Playback paused at %llu. Seek to a readable frame or stop comparison",
		    comparison.failedPane == ReplayComparisonPane_Primary ? "Primary" : comparison.peer->identity.c_str(),
		    static_cast<unsigned long long>(comparison.committedOrdinal));
		ImGui::TextWrapped("%.*s", static_cast<int>(comparison.error.detailSize), comparison.error.detail.data());
	}
	if (comparison.selectionError.code != ArenaStatus_Ok)
	{
		ImGui::TextWrapped("Comparison selection failed. Current pair retained, paused at %llu",
		    static_cast<unsigned long long>(comparison.committedOrdinal));
		ImGui::TextWrapped("%.*s", static_cast<int>(comparison.selectionError.detailSize), comparison.selectionError.detail.data());
	}
	DrawReplayTransport(app);
	app->ui.sceneHovered = app->ui.sceneActive = app->ui.sceneFocused = PresenceStatus_Absent;
	const ImVec2 available = ImGui::GetContentRegionAvail();
	const float gap = ImGui::GetStyle().ItemSpacing.x;
	const int compact = available.x / scale < 1000;
	const float paneWidth = compact != 0 ? std::floor(available.x) : std::floor((available.x - gap) / 2);
	const float paneHeight = compact != 0 ? (std::max)(140 * scale, std::floor((available.y - gap) / 2)) : (std::max)(140 * scale, std::floor(available.y));
	std::array<char, kEngineProvenanceLabelCapacity + 16> primaryHeading = {}, peerHeading = {};
	std::snprintf(primaryHeading.data(), primaryHeading.size(), "Primary: %.*s", static_cast<int>(engine.size()), engine.data());
	std::snprintf(peerHeading.data(), peerHeading.size(), "Comparison: %s", comparison.peer->engineLabel.c_str());
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Heading), 12 * kNativeUiBodyLineScale);
	const float engineHeight = (std::max)(ImGui::CalcTextSize(primaryHeading.data(), nullptr, false, paneWidth).y,
	    ImGui::CalcTextSize(peerHeading.data(), nullptr, false, paneWidth).y);
	ImGui::PopFont();
	const float headingHeight = engineHeight + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
	DrawComparisonPane(app, ReplayComparisonPane_Primary, paneWidth, paneHeight, headingHeight);
	if (compact == 0) ImGui::SameLine();
	DrawComparisonPane(app, ReplayComparisonPane_Peer, paneWidth, paneHeight, headingHeight);
	app->ui.scene = comparison.activePane == ReplayComparisonPane_Primary ? comparison.primaryViewport : comparison.peerViewport;
	const ImGuiIO& io = ImGui::GetIO();
	if (!io.AppFocusLost && !io.WantTextInput && !ImGui::IsAnyItemActive() &&
	    !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
	    (!ImGui::IsAnyItemFocused() || app->ui.sceneFocused == PresenceStatus_Present))
	{
		if (ImGui::Shortcut(ImGuiKey_Space, ImGuiInputFlags_RouteGlobal)) ToggleReplayPlayback(app);
		if (ImGui::Shortcut(ImGuiKey_R, ImGuiInputFlags_RouteGlobal)) app->model.selection.replayCamera = {0, 0, 1, {}};
		if (ImGui::Shortcut(ImGuiKey_LeftArrow, ImGuiInputFlags_RouteGlobal)) SeekReplayView(app, primary.clock.ordinal == 0 ? 0 : primary.clock.ordinal - 1);
		if (ImGui::Shortcut(ImGuiKey_RightArrow, ImGuiInputFlags_RouteGlobal)) SeekReplayView(app, (std::min)(primary.clock.ordinal + 1, primary.clock.finalOrdinal));
		if (ImGui::Shortcut(ImGuiKey_Home, ImGuiInputFlags_RouteGlobal)) SeekReplayView(app, 0);
		if (ImGui::Shortcut(ImGuiKey_End, ImGuiInputFlags_RouteGlobal)) SeekReplayView(app, primary.clock.finalOrdinal);
	}
	ImGui::EndChild();
}

void DrawComparisonRecording(PhysicsArenaApp* app, PhysicsSceneResources* resources, ReplayRecording& recording,
                             RenderViewport viewport, RenderViewport clip)
{
	VisualSnapshot snapshot = {};
	snapshot.identity = recording.scene.identity;
	snapshot.scene = &recording.scene;
	snapshot.transforms = recording.frame.transforms.data();
	snapshot.transformCount = snapshot.transformCapacity = static_cast<int>(recording.frame.transforms.size());
	snapshot.debugPrimitives = recording.frame.debug.data();
	snapshot.debugPrimitiveCount = snapshot.debugPrimitiveCapacity = static_cast<int>(recording.frame.debug.size());
	snapshot.completedStepCount = snapshot.stepIndex = static_cast<int>(recording.frame.ordinal);
	PhysicsSceneViewSetAppearance(resources, app->replay->appearance);
	if (PhysicsSceneViewDrawSnapshot(resources, app->platform, viewport, snapshot, clip) != RenderViewerStatus_Ok)
		ReplayError(&app->replay->comparison->error, "comparison_draw_failed");
}

void DrawReplayComparisonScenes(PhysicsArenaApp* app)
{
	NativeReplayView& primary = *app->replay;
	NativeReplayComparison& comparison = *primary.comparison;
	if (comparison.pairPresence != PresenceStatus_Present || comparison.primaryViewport.width <= 0 || comparison.primaryViewport.height <= 0 ||
	    comparison.peerViewport.width <= 0 || comparison.peerViewport.height <= 0)
		return;
	UpdateReplayCameraInput(app);
	ResolvedVisualCamera camera = {};
	if (ComposeReplayCamera(comparison.cameraContext, static_cast<float>(comparison.primaryViewport.width) / comparison.primaryViewport.height,
	                        &app->model.selection.replayCamera, &camera) != VisualCameraStatus_Ok ||
	    PhysicsSceneViewSetCameraControl(primary.sceneResources, &camera) != RenderViewerStatus_Ok ||
	    PhysicsSceneViewSetCameraControl(comparison.peer->sceneResources, &camera) != RenderViewerStatus_Ok)
	{
		ReplayError(&comparison.error, "comparison_camera_failed");
		return;
	}
	DrawComparisonRecording(app, primary.sceneResources, primary.recording, comparison.primaryViewport, comparison.primaryClip);
	DrawComparisonRecording(app, comparison.peer->sceneResources, comparison.peer->recording, comparison.peerViewport, comparison.peerClip);
}
}
