#include "launcher_app_internal.h"

#include <imgui.h>
#include <array>
#include <cstdio>
#include <string>
#include <algorithm>

namespace benchmark_visual
{
using namespace physics_arena;

NativeUiCommand DrawRecordingDeletionConfirmation(PhysicsArenaApp* app)
{
	NativeUiCommand command = NativeUiCommand_None;
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float scale = app->platform.dpiScale;
	const float width = (std::min)(620 * scale, viewport->WorkSize.x - 32 * scale);
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, viewport->WorkSize.y - 32 * scale));
	if (ImGui::BeginPopupModal("Delete recordings permanently?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		const ReplayStorageSelection& selection = app->results.deletionSelection;
		const ReplayStorageInventory& preview = app->results.deletionPreview;
		constexpr std::array<const char*, 4> scopes = {"Selected recording", "Selected run", "Completed local library",
		                                               "Unfinished run leftovers"};
		ImGui::TextUnformatted(scopes[selection.scope]);
		if (selection.scope == ReplayStorageScope_Tuple)
		{
			const ResultManifestRecord& manifest = app->workspace.finalization.manifest;
			const std::string_view engine =
			    ResultViewTextView(&app->workspace.finalization.model,
				                app->workspace.finalization.model.engines[selection.engineOrdinal].provenanceLabel);
			ImGui::Text("%.*s | Threads %u | Repeat %u", static_cast<int>(engine.size()), engine.data(),
			            manifest.threadCounts[selection.threadOrdinal], selection.repeatIndex + 1);
		}
		if (selection.scope == ReplayStorageScope_Library)
			ImGui::Text("%s completed runs in the local Results library", FormatNativeCount(selection.resultDirectories.size()).data());
		if (selection.scope != ReplayStorageScope_Library)
			ImGui::TextWrapped("%s", app->results.deletionPath.c_str());
		ImGui::Text("%s recordings and %s temporary files | %s", FormatNativeCount(preview.recordingCount).data(), FormatNativeCount(preview.temporaryCount).data(),
		    FormatNativeBytes(preview.recordingBytes + preview.temporaryBytes).data());
		ImGui::Text("Exact size: %s bytes", FormatNativeRawCount(preview.recordingBytes + preview.temporaryBytes).data());
		ImGui::TextUnformatted("Deletion is permanent. Benchmark measurements and reports remain.");
		if (selection.scope == ReplayStorageScope_Leftovers)
			ImGui::TextUnformatted("Deleting the only raw spool prevents replay recovery for that tuple.");
		if (ImGui::Button("Delete permanently"))
		{
			command = NativeUiCommand_DeleteRecordings;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();
		ImGui::SetItemDefaultFocus();
		ImGui::EndPopup();
	}
	return command;
}

void RecordingStorageFact(const char* label, const char* value, std::uint64_t bytes)
{
	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	const float baseline = ImGui::GetCursorPosY() + ImGui::GetFontBaked()->Ascent;
	ImGui::TextDisabled("%s", label);
	ImGui::TableNextColumn();
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 11);
	ImGui::SetCursorPosY(baseline - ImGui::GetFontBaked()->Ascent);
	ImGui::TextUnformatted(value);
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		ImGui::SetTooltip("%s bytes", FormatNativeRawCount(bytes).data());
	ImGui::PopFont();
}

NativeUiCommand DrawRecordings(PhysicsArenaApp* app)
{
	NativeUiCommand command = NativeUiCommand_None;
	const PresenceStatus active = ActiveAction(app->action);
	const float scale = app->platform.dpiScale;
	constexpr std::array<const char*, 3> scopes = {"Selected run", "Library", "Unfinished"};
	for (std::uint32_t page = 0; page < scopes.size(); ++page)
	{
		if (page != 0)
			ImGui::SameLine(0, 0);
		if (DrawWorkspaceTab(scopes[page], app->ui.recordingPage == static_cast<NativeRecordingPage>(page) ?
		    PresenceStatus_Present : PresenceStatus_Absent, page == 0 ? 112 : 100, 28))
			app->ui.recordingPage = static_cast<NativeRecordingPage>(page);
	}
	ImGui::Spacing();
	if (app->ui.recordingPage == NativeRecordingPage_Run && app->results.modelPresence == PresenceStatus_Present &&
	    app->results.runStorage.recordingCount + app->results.runStorage.temporaryCount != 0)
	{
		const ReplayStorageInventory& tuple = app->results.tupleStorage;
		const ReplayStorageInventory& run = app->results.runStorage;
		if (ImGui::BeginTable("selected_storage", 2, ImGuiTableFlags_SizingFixedFit))
		{
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 145 * scale);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 150 * scale);
			RecordingStorageFact("Selected recording", tuple.recordingCount == 0 ? "Unavailable" : FormatNativeBytes(tuple.recordingBytes).data(), tuple.recordingBytes);
			RecordingStorageFact("Run recordings", FormatNativeBytes(run.recordingBytes).data(), run.recordingBytes);
			RecordingStorageFact("Temporary files", FormatNativeBytes(run.temporaryBytes).data(), run.temporaryBytes);
			ImGui::EndTable();
		}
		ImGui::BeginDisabled(tuple.recordingCount + tuple.temporaryCount == 0 || app->results.recordingRefreshPending == PresenceStatus_Present);
		if (ImGui::Button("Delete replay") && PrepareStorageDeletion(app, ReplayStorageScope_Tuple) == ArenaStatus_Ok)
			app->results.deletionPending = PresenceStatus_Present;
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(run.recordingCount + run.temporaryCount == 0);
		if (ImGui::Button("Delete run recordings") &&
		    PrepareStorageDeletion(app, ReplayStorageScope_Run) == ArenaStatus_Ok)
			app->results.deletionPending = PresenceStatus_Present;
		ImGui::EndDisabled();
	}
	if (app->ui.recordingPage == NativeRecordingPage_Run && app->results.modelPresence != PresenceStatus_Present)
		ImGui::TextDisabled("Choose a completed result to inspect its recordings");
	if (app->ui.recordingPage == NativeRecordingPage_Library)
	{
		const ReplayStorageInventory& library = app->results.libraryStorage;
		if (ImGui::BeginTable("library_storage", 2, ImGuiTableFlags_SizingFixedFit))
		{
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 145 * scale);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 150 * scale);
			RecordingStorageFact("Library recordings", FormatNativeBytes(library.recordingBytes).data(), library.recordingBytes);
			RecordingStorageFact("Temporary files", FormatNativeBytes(library.temporaryBytes).data(), library.temporaryBytes);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			const float baseline = ImGui::GetCursorPosY() + ImGui::GetFontBaked()->Ascent;
			ImGui::TextDisabled("Saved recordings");
			ImGui::TableNextColumn();
			ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 11);
			ImGui::SetCursorPosY(baseline - ImGui::GetFontBaked()->Ascent);
			ImGui::Text("%s in %s runs", FormatNativeCount(library.recordingCount).data(), FormatNativeCount(app->results.recordingRunCount).data());
			ImGui::PopFont();
			ImGui::EndTable();
		}
	}
	if (app->ui.recordingPage == NativeRecordingPage_Unfinished && !app->results.leftovers.empty())
	{
		PresenceStatus confirmation = PresenceStatus_Absent;
		for (const NativeRecordingLeftover& leftover : app->results.leftovers)
		{
			const std::string& path = leftover.displayPath;
			ImGui::PushID(path.c_str());
			ImGui::TextWrapped("%s", path.c_str());
			ImGui::Text("Saved: %s | Temporary: %s", FormatNativeBytes(leftover.storage.recordingBytes).data(), FormatNativeBytes(leftover.storage.temporaryBytes).data());
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
				ImGui::SetTooltip("Saved: %s bytes\nTemporary: %s bytes", FormatNativeRawCount(leftover.storage.recordingBytes).data(), FormatNativeRawCount(leftover.storage.temporaryBytes).data());
			ImGui::TextWrapped("%.*s", static_cast<int>(leftover.reason.detailSize), leftover.reason.detail.data());
			ImGui::BeginDisabled(leftover.storage.recordingBytes == 0 && leftover.storage.temporaryBytes == 0);
			const int cleanup = ImGui::Button("Clean recording leftovers");
			ImGui::EndDisabled();
			ImGui::PopID();
			if (cleanup &&
			    PrepareStorageDeletion(app, ReplayStorageScope_Leftovers, leftover.directory) == ArenaStatus_Ok)
				confirmation = PresenceStatus_Present;
		}
		if (confirmation == PresenceStatus_Present)
			app->results.deletionPending = PresenceStatus_Present;
	}
	if (app->action.kind == NativeActionKind_Storage && active != PresenceStatus_Present)
	{
		const ReplayStorageResult& outcome = app->action.storageResult;
		ImGui::Text("%s: removed %s | Remaining %s | Failed %s",
		    app->action.error.code == ArenaStatus_Interrupted ? "Interrupted" : "Finished",
		    FormatNativeCount(outcome.removedCount).data(),
		    outcome.inventoryComplete == PresenceStatus_Present ? FormatNativeCount(outcome.remaining.files.size()).data() : "unchanged",
		    FormatNativeCount(outcome.failures.size()).data());
		ImGui::Text("Freed %s | Remaining %s", FormatNativeBytes(outcome.freedBytes).data(), FormatNativeBytes(outcome.remainingBytes).data());
		for (std::size_t index = 0; index < outcome.failures.size(); ++index)
			ImGui::TextWrapped("Retained: %s (system error %u)", app->results.storageFailurePaths[index].c_str(), outcome.failures[index].systemError);
	}
	if (app->results.status != ArenaStatus_Ok)
		DrawStatusError(app->results.error);
	if (app->ui.recordingPage == NativeRecordingPage_Unfinished)
	{
		if (app->results.unfinishedResultCount == 0 && app->results.skippedResultCount == 0)
			ImGui::TextDisabled("No unfinished or invalid entries");
		if (app->results.unfinishedResultCount != 0)
		{
			ImGui::TextWrapped("%u unfinished run(s) omitted from completed results. Files retained.",
			                   app->results.unfinishedResultCount);
		}
		if (app->results.skippedResultCount != 0)
		{
			ImGui::TextWrapped("Skipped %u unsupported or invalid local results. Repair the indicated entry and Refresh.",
			                   app->results.skippedResultCount);
			for (const NativeResultIssue& issue : app->results.issues)
			{
				ImGui::TextWrapped("%s", issue.path.c_str());
				DrawStatusError(issue.reason);
			}
			if (app->results.issues.empty())
				DrawStatusError(app->results.discoveryError);
		}
	}
	return command;
}

void DrawRecordingChoiceControls(PhysicsArenaApp* app, std::array<std::uint32_t, 3>* choice, int includeRepeat)
{
	const ResultViewModel& model = app->workspace.finalization.model;
	const std::array<std::uint32_t, 3> previous = *choice;
	const float scale = app->platform.dpiScale;
	if (ImGui::BeginTable("recording_choice", 2, ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 70 * scale);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
		for (std::uint32_t field = 0; field < (includeRepeat != 0 ? 3u : 2u); ++field)
		{
			constexpr std::array<const char*, 3> labels = {"Engine", "Threads", "Repeat"};
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", labels[field]);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);
			std::array<char, kEngineProvenanceLabelCapacity> label = {};
			if (field == 0)
			{
				const std::string_view name = ResultViewTextView(&model, model.engines[(*choice)[0]].provenanceLabel);
				std::snprintf(label.data(), label.size(), "%.*s", static_cast<int>(name.size()), name.data());
			}
			else
				std::snprintf(label.data(), label.size(), "%u", field == 1 ? model.threadCounts[(*choice)[1]] : (*choice)[2] + 1);
			ImGui::PushID(static_cast<int>(field));
			if (ImGui::BeginCombo("##choice", label.data()))
			{
				const std::uint32_t count = field == 0 ? model.engineCount : field == 1 ? model.threadCount : model.repeatCount;
				for (std::uint32_t index = 0; index < count; ++index)
				{
					if (field == 0)
					{
						const std::string_view name = ResultViewTextView(&model, model.engines[index].provenanceLabel);
						std::snprintf(label.data(), label.size(), "%.*s", static_cast<int>(name.size()), name.data());
					}
					else
						std::snprintf(label.data(), label.size(), "%u", field == 1 ? model.threadCounts[index] : index + 1);
					if (ImGui::Selectable(label.data(), (*choice)[field] == index))
						(*choice)[field] = index;
				}
				ImGui::EndCombo();
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	if (previous != *choice)
		app->results.recordingRefreshPending = PresenceStatus_Present;
}

void DrawReplayTupleControls(PhysicsArenaApp* app)
{
	std::array<std::uint32_t, 3> choice = {app->results.replayEngineIndex, app->results.replayThreadIndex, app->results.replayRepeatIndex};
	DrawRecordingChoiceControls(app, &choice, 1);
	if (app->replay == nullptr && RayTracingReplayPresence(app) != PresenceStatus_Present && choice != std::array<std::uint32_t, 3>{app->results.replayEngineIndex, app->results.replayThreadIndex, app->results.replayRepeatIndex})
		app->results.replayError = {};
	app->results.replayEngineIndex = choice[0];
	app->results.replayThreadIndex = choice[1];
	app->results.replayRepeatIndex = choice[2];
}

void BeginRecordingChoice(PhysicsArenaApp* app, const std::array<std::uint32_t, 3>& selection)
{
	PauseReplayView(app);
	app->results.recordingChoice = selection;
	app->results.recordingPickerPending = PresenceStatus_Present;
	app->results.recordingPickerActive = PresenceStatus_Present;
	app->results.recordingAutoPlaySingle = PresenceStatus_Absent;
	app->results.recordingRefreshPending = PresenceStatus_Present;
}

NativeUiCommand RequestRecordingPlayback(PhysicsArenaApp* app)
{
	NativeResultsState& state = app->results;
	const ResultViewModel& model = app->workspace.finalization.model;
	const std::uint32_t repeat = state.selectedRepeatIndex < model.repeatCount ? state.selectedRepeatIndex : 0;
	if (app->replay == nullptr && RayTracingReplayPresence(app) != PresenceStatus_Present && (state.replayEngineIndex != state.selectedEngineIndex ||
	    state.replayThreadIndex != state.selectedThreadIndex || state.replayRepeatIndex != repeat))
		state.replayError = {};
	state.replayEngineIndex = state.selectedEngineIndex;
	state.replayThreadIndex = state.selectedThreadIndex;
	state.replayRepeatIndex = repeat;
	if (state.selectedRepeatIndex < model.repeatCount)
		return NativeUiCommand_OpenReplay;
	BeginRecordingChoice(app, {state.replayEngineIndex, state.replayThreadIndex, state.replayRepeatIndex});
	state.recordingAutoPlaySingle = PresenceStatus_Present;
	return NativeUiCommand_None;
}

const char* RecordingAvailabilityLabel(ReplayAvailability availability)
{
	constexpr std::array<const char*, 5> labels = {"Not recorded", "Available", "File missing", "Invalid recording", "Temporary recording only"};
	return labels[availability];
}

NativeUiCommand DrawRecordingPicker(PhysicsArenaApp* app)
{
	NativeResultsState& state = app->results;
	if (state.recordingPickerPending == PresenceStatus_Present)
	{
		ImGui::OpenPopup("Choose recording");
		state.recordingPickerPending = PresenceStatus_Absent;
	}
	NativeUiCommand command = NativeUiCommand_None;
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	const float width = (std::min)(520 * app->platform.dpiScale, ImGui::GetMainViewport()->WorkSize.x - 32 * app->platform.dpiScale);
	ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, ImGui::GetMainViewport()->WorkSize.y - 32 * app->platform.dpiScale));
	if (ImGui::BeginPopupModal("Choose recording", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (state.recordingAutoPlaySingle == PresenceStatus_Present && state.recordingRefreshPending != PresenceStatus_Present)
		{
			std::uint32_t available = 0;
			std::uint32_t only = 0;
			for (std::uint32_t repeat = 0; repeat < app->workspace.finalization.model.repeatCount; ++repeat)
				if (state.recordingAvailability[repeat] == ReplayAvailability_Available)
				{
					++available;
					only = repeat;
				}
			state.recordingAutoPlaySingle = PresenceStatus_Absent;
			if (available == 1)
			{
				state.replayRepeatIndex = only;
				state.recordingPickerActive = PresenceStatus_Absent;
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return NativeUiCommand_OpenReplay;
			}
		}
		DrawRecordingChoiceControls(app, &state.recordingChoice, 1);
		const ReplayAvailability availability = state.recordingAvailability[state.recordingChoice[2]];
		ImGui::TextUnformatted(state.recordingRefreshPending == PresenceStatus_Present ? "Checking recording" : RecordingAvailabilityLabel(availability));
		ImGui::BeginDisabled(availability != ReplayAvailability_Available || state.recordingRefreshPending == PresenceStatus_Present);
		if (ImGui::Button("Play recording"))
		{
			state.replayEngineIndex = state.recordingChoice[0];
			state.replayThreadIndex = state.recordingChoice[1];
			state.replayRepeatIndex = state.recordingChoice[2];
			state.recordingPickerActive = PresenceStatus_Absent;
			command = NativeUiCommand_OpenReplay;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			state.recordingPickerActive = PresenceStatus_Absent;
			state.recordingRefreshPending = PresenceStatus_Present;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
		ImGui::EndPopup();
	}
	return command;
}

int DrawRecordingRow(std::string_view engine, std::uint32_t threads, std::uint32_t repeat,
                     const char* status, const char* savedSize, std::uint64_t bytes,
                     PresenceStatus sizePresence, PresenceStatus selected, PresenceStatus selectable)
{
	const int identity = ImGui::TableGetColumnCount() == 5;
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	float height = ImGui::GetTextLineHeight();
	if (identity != 0)
	{
		ImGui::PushFont(NativeUiFontFace(NativeUiFont_Heading), 12 * kNativeUiBodyLineScale);
		height = (std::max)(height, ImGui::CalcTextSize(engine.data(), engine.data() + engine.size(), false,
		    ImGui::GetContentRegionAvail().x).y);
		ImGui::PopFont();
		ImGui::TableSetColumnIndex(3);
		height = (std::max)(height, ImGui::CalcTextSize(status, nullptr, false, ImGui::GetContentRegionAvail().x).y);
		ImGui::TableSetColumnIndex(0);
		height = (std::max)(height, 30 * ImGui::GetStyle().FontScaleDpi);
	}
	const ImVec2 origin = ImGui::GetCursorPos();
	ImGui::BeginDisabled(selectable != PresenceStatus_Present);
	const int pressed = ImGui::Selectable("##recording_row", selected == PresenceStatus_Present,
	    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, height));
	ImGui::SetCursorPos(origin);
	if (identity != 0)
	{
		ImGui::PushFont(NativeUiFontFace(NativeUiFont_Heading), 12 * kNativeUiBodyLineScale);
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
		ImGui::TextUnformatted(engine.data(), engine.data() + engine.size());
		ImGui::PopTextWrapPos();
		ImGui::PopFont();
		ImGui::TableNextColumn();
		ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 11);
		ImGui::Text("%u", threads);
		ImGui::PopFont();
		ImGui::TableNextColumn();
	}
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 11);
	ImGui::Text("%u", repeat + 1);
	ImGui::PopFont();
	ImGui::TableNextColumn();
	ImGui::TextWrapped("%s", status);
	ImGui::TableNextColumn();
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 11);
	ImGui::TextUnformatted(savedSize);
	if (sizePresence == PresenceStatus_Present && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		ImGui::SetTooltip("%s bytes", FormatNativeRawCount(bytes).data());
	ImGui::PopFont();
	ImGui::EndDisabled();
	return pressed;
}

NativeUiCommand DrawResultRecordings(PhysicsArenaApp* app)
{
	NativeResultsState& state = app->results;
	const ResultViewModel& model = app->workspace.finalization.model;
	NativeUiCommand command = NativeUiCommand_None;
	ImGui::Text("%s saved recording%s | %s", FormatNativeCount(state.runStorage.recordingCount).data(), state.runStorage.recordingCount == 1 ? "" : "s", FormatNativeBytes(state.runStorage.recordingBytes).data());
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		ImGui::SetTooltip("%s bytes", FormatNativeRawCount(state.runStorage.recordingBytes).data());
	std::array<std::uint32_t, 3> choice = {state.replayEngineIndex, state.replayThreadIndex, state.replayRepeatIndex};
	DrawRecordingChoiceControls(app, &choice, 0);
	if (app->replay == nullptr && RayTracingReplayPresence(app) != PresenceStatus_Present && (choice[0] != state.replayEngineIndex || choice[1] != state.replayThreadIndex))
		state.replayError = {};
	state.replayEngineIndex = choice[0];
	state.replayThreadIndex = choice[1];
	const float scale = app->platform.dpiScale;
	if (ImGui::BeginTable("recording_repeats", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
	    ImVec2(0, (std::min)(240 * scale, (model.repeatCount + 1) * ImGui::GetFrameHeightWithSpacing()))))
	{
		ImGui::TableSetupColumn("Repeat", ImGuiTableColumnFlags_WidthFixed, 70 * scale);
		ImGui::TableSetupColumn("Status");
		ImGui::TableSetupColumn("Saved size");
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();
		for (std::uint32_t repeat = 0; repeat < model.repeatCount; ++repeat)
		{
			ImGui::PushID(static_cast<int>(repeat));
			const PresenceStatus sizePresence = state.recordingRefreshPending != PresenceStatus_Present &&
			    state.recordingAvailability[repeat] != ReplayAvailability_NotRecorded && state.recordingAvailability[repeat] != ReplayAvailability_Unavailable
			    ? PresenceStatus_Present : PresenceStatus_Absent;
			const NativeValueText savedSize = FormatNativeBytes(state.recordingBytes[repeat]);
			if (DrawRecordingRow({}, 0, repeat,
			    state.recordingRefreshPending == PresenceStatus_Present ? "Checking" : RecordingAvailabilityLabel(state.recordingAvailability[repeat]),
			    state.recordingRefreshPending == PresenceStatus_Present ? "Checking" : sizePresence == PresenceStatus_Present ? savedSize.data() : "Unavailable",
			    state.recordingBytes[repeat], sizePresence, state.replayRepeatIndex == repeat ? PresenceStatus_Present : PresenceStatus_Absent,
			    PresenceStatus_Present) != 0)
			{
				if (app->replay == nullptr && RayTracingReplayPresence(app) != PresenceStatus_Present && state.replayRepeatIndex != repeat)
					state.replayError = {};
				state.replayRepeatIndex = repeat;
				state.recordingRefreshPending = PresenceStatus_Present;
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	ImGui::BeginDisabled(state.recordingRefreshPending == PresenceStatus_Present || state.recordingAvailability[state.replayRepeatIndex] != ReplayAvailability_Available);
	if (ImGui::Button("Play selected recording"))
		command = NativeUiCommand_OpenReplay;
	ImGui::EndDisabled();
	if (state.runStorage.recordingCount == 0)
		ImGui::TextWrapped("No playable recording is saved for this run. Save replay can be enabled when configuring a new run");
	return command;
}

NativeUiCommand DrawReplayBrowser(PhysicsArenaApp* app)
{
	NativeUiCommand command = NativeUiCommand_None;
	const float scale = app->platform.dpiScale;
	ImGui::BeginChild("replay_browser", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
	DrawPaneHeading("Saved recordings");
	const ResultIndexWorkspace& indexes = app->workspace.finalization.indexes;
	const NativeResultsState& state = app->results;
	const float popupWidth = (std::min)(720 * scale, ImGui::GetMainViewport()->WorkSize.x - 28 * scale);
	const float selectorWidth = (std::min)(popupWidth, ImGui::GetContentRegionAvail().x - 267 * scale);
	const ImVec2 previewOrigin = ImGui::GetCursorScreenPos();
	ImGui::SetNextItemWidth(selectorWidth);
	std::uint32_t selected = indexes.recordCount;
	if (state.recordingRunCount != 0)
	{
		ImGui::SetNextWindowSizeConstraints(ImVec2(popupWidth, 0), ImVec2(popupWidth, 420 * scale));
		const int open = ImGui::BeginCombo("##replay_run", "");
		if (open != 0)
		{
			if (BeginRunPickerTable(PresenceStatus_Present) != 0)
			{
				for (std::uint32_t ordinal = 0; ordinal < state.recordingRunCount; ++ordinal)
				{
					const std::uint32_t index = state.recordingRunIndexes[ordinal];
					ImGui::PushID(static_cast<int>(index));
					if (DrawRunPickerRow(indexes.records[index], state.selectedRecordIndex == index ?
					    PresenceStatus_Present : PresenceStatus_Absent, PresenceStatus_Present) != 0)
						selected = index;
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			ImGui::EndCombo();
		}
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 textOrigin(previewOrigin.x + 7 * scale, previewOrigin.y + 3 * scale);
		draw->PushClipRect(textOrigin, ImVec2(previewOrigin.x + selectorWidth - 28 * scale, previewOrigin.y + ImGui::GetFrameHeight()), true);
		if (state.modelPresence == PresenceStatus_Present && state.runStorage.recordingCount != 0)
		{
			const ResultViewModel& model = app->workspace.finalization.model;
			const std::string_view name = ResultViewTextView(&model, model.caseDisplayName);
			NativeRunLabelProjection identity = {};
			ProjectNativeRunLabel(ResultViewTextView(&model, model.runId), model.threadCount, model.repeatCount, &identity);
			ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 11);
			const float dateWidth = ImGui::CalcTextSize(identity.text.data()).x;
			ImGui::PopFont();
			ImGui::PushFont(NativeUiFontFace(NativeUiFont_Heading), 12 * kNativeUiBodyLineScale);
			const float baseline = textOrigin.y + ImGui::GetFontBaked()->Ascent;
			const float nameWidth = (std::min)(ImGui::CalcTextSize(name.data(), name.data() + name.size()).x,
			    selectorWidth - dateWidth - 56 * scale);
			draw->PushClipRect(textOrigin, ImVec2(textOrigin.x + nameWidth, previewOrigin.y + ImGui::GetFrameHeight()), true);
			draw->AddText(textOrigin, ImGui::GetColorU32(ImGuiCol_Text), name.data(), name.data() + name.size());
			draw->PopClipRect();
			ImGui::PopFont();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			{
				const std::string_view runId = ResultViewTextView(&model, model.runId);
				ImGui::SetTooltip("%.*s\nRun ID: %.*s", static_cast<int>(name.size()), name.data(),
				    static_cast<int>(runId.size()), runId.data());
			}
			ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 11);
			draw->AddText(ImVec2(textOrigin.x + nameWidth + 14 * scale, baseline - ImGui::GetFontBaked()->Ascent),
			    ImGui::GetColorU32(ImGuiCol_TextDisabled), identity.text.data());
			ImGui::PopFont();
		}
		else
			draw->AddText(textOrigin, ImGui::GetColorU32(ImGuiCol_TextDisabled), "Choose a recorded run");
		draw->PopClipRect();
		ImGui::SameLine();
	}
	else
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + selectorWidth + 7 * scale);
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - 260 * scale);
	const ReplayStorageInventory& library = state.libraryStorage;
	ImGui::BeginDisabled(ActiveAction(app->action) == PresenceStatus_Present || library.recordingCount + library.temporaryCount == 0);
	if (ImGui::Button("Delete all recordings", ImVec2(170 * scale, 0)) &&
	    PrepareStorageDeletion(app, ReplayStorageScope_Library) == ArenaStatus_Ok)
		app->results.deletionPending = PresenceStatus_Present;
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Refresh", ImVec2(83 * scale, 0)))
		command = NativeUiCommand_RefreshResults;
	if (selected < indexes.recordCount)
	{
		std::array<wchar_t, kRunPathCapacity> directory = {};
		if (BuildRecordDirectory(app->model, indexes.records[selected], &directory) == ArenaStatus_Ok)
			LoadResultDirectory(app, directory.data(), &app->results.error);
	}
	ImGui::Spacing();
	if (state.modelPresence == PresenceStatus_Present && state.runStorage.recordingCount != 0)
	{
		const NativeUiCommand play = DrawResultRecordings(app);
		if (play != NativeUiCommand_None)
			command = play;
	}
	else if (state.recordingRunCount == 0)
	{
		ImGui::TextUnformatted(state.discoveryError.code == ArenaStatus_Ok ? "No saved recordings" :
		    "Saved recordings could not be fully inspected");
		if (state.discoveryError.code == ArenaStatus_Ok)
			ImGui::TextWrapped("Enable Save replay when configuring a new run to keep its recording");
	}
	ImGui::Spacing();
	DrawPaneHeading("Recording storage", 28);
	DrawRecordings(app);
	ImGui::EndChild();
	return command;
}
}
