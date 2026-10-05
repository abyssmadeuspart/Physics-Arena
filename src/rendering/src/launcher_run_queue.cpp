#include "launcher_app_internal.h"

#include "physics_arena/run_recommendations.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace benchmark_visual
{
using namespace physics_arena;

PresenceStatus RunWorkspaceBusy(const PhysicsArenaApp* app)
{
	return ActiveAction(app->action) == PresenceStatus_Present || app->queue.state == NativeRunQueueState_Armed
	    ? PresenceStatus_Present : PresenceStatus_Absent;
}

NativeUiCommand DrawRunQueuePreparation(PhysicsArenaApp* app, ArenaStatus ready)
{
	NativeUiCommand command = NativeUiCommand_None;
	const float scale = app->platform.dpiScale;
	const int armed = app->queue.state == NativeRunQueueState_Armed;
	const int testOutput = app->model.selection.storage == ResultStorage_Local;
	const char* draftLabel = app->queue.editingId != 0
	    ? (testOutput != 0 ? "Update queued Test run" : "Update queued Release run")
	    : "Add to queue";
	const char* recommendedLabel = "Add recommended Release runs";
	const float draftWidth = (std::max)(170 * scale, ImGui::CalcTextSize(draftLabel).x + ImGui::GetStyle().FramePadding.x * 2);
	const float recommendedWidth = ImGui::CalcTextSize(recommendedLabel).x + ImGui::GetStyle().FramePadding.x * 2;
	const float availableWidth = ImGui::GetContentRegionAvail().x;
	ImGui::BeginDisabled(ready != ArenaStatus_Ok);
	if (app->queue.editingId != 0)
	{
		if (ImGui::Button(draftLabel, ImVec2(draftWidth, 28 * scale)))
			command = NativeUiCommand_UpdateQueue;
	}
	else if (ImGui::Button(draftLabel, ImVec2(draftWidth, 28 * scale)))
		command = NativeUiCommand_AddQueue;
	ImGui::EndDisabled();
	if (app->queue.editingId != 0)
	{
		if (availableWidth >= draftWidth + ImGui::GetStyle().ItemSpacing.x + 100 * scale)
			ImGui::SameLine();
		if (ImGui::Button("Cancel edit", ImVec2(100 * scale, 28 * scale)))
			command = NativeUiCommand_CancelQueueEdit;
	}
	else
	{
		if (availableWidth >= draftWidth + ImGui::GetStyle().ItemSpacing.x + recommendedWidth)
			ImGui::SameLine();
		if (ImGui::Button(recommendedLabel, ImVec2(recommendedWidth, 28 * scale)))
			command = NativeUiCommand_AddRecommendedQueue;
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			ImGui::SetTooltip("Prepare three authored cases with five repeats and default settings. Start executes the prepared queue");
	}
	ImGui::TextWrapped("Manual output is saved when added. Recommended runs use Release. Changing the draft keeps queued outputs unchanged");
	if (app->queue.editingId != 0)
		ImGui::TextWrapped(armed != 0 ? "Finish editing to continue queue" : "Update or cancel the pending edit before Start");
	return command;
}

NativeValueText QueueThreadText(const std::array<PresenceStatus, kThreadCountCapacity>& selection)
{
	std::array<std::uint32_t, kThreadCountCapacity> values = {};
	std::uint32_t count = 0;
	for (std::uint32_t index = 0; index < selection.size(); ++index)
		if (selection[index] == PresenceStatus_Present)
			values[count++] = index + 1;
	return FormatNativeThreadSet(values.data(), count);
}

NativeUiCommand DrawRunQueue(PhysicsArenaApp* app)
{
	NativeUiCommand command = NativeUiCommand_None;
	const float scale = app->platform.dpiScale;
	NativeRunQueue& queue = app->queue;
	DrawPaneHeading("Run queue", 28);
	ImGui::BeginChild("run_queue_content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::Text("%u pending | %zu total", PendingNativeRunQueueCount(queue), queue.entries.size());
	ImGui::BeginDisabled(queue.editingId != 0);
	if (ImGui::SmallButton("Clear pending"))
		command = NativeUiCommand_ClearPendingQueue;
	ImGui::EndDisabled();
	ImGui::BeginChild("queue_rows", ImVec2(0, (std::max)(100 * scale, ImGui::GetContentRegionAvail().y * 0.44f)), ImGuiChildFlags_Borders);
	for (std::size_t index = 0; index < queue.entries.size(); ++index)
	{
		const NativeRunQueueEntry& entry = queue.entries[index];
		const CaseRecord& benchmarkCase = app->model.catalog.cases[entry.selection.caseIndex];
		const std::string_view name = CatalogTextView(&app->model.catalog, benchmarkCase.displayName);
		ImGui::PushID(static_cast<int>(entry.id));
		std::array<char, 512> label = {};
		std::snprintf(label.data(), label.size(), "%zu. %.*s\n%s%s", index + 1, static_cast<int>(name.size()), name.data(),
		    NativeRunQueueEntryStateText(entry.state), queue.editingId == entry.id ? " (editing)" : "");
		const ImVec2 row = ImGui::GetCursorScreenPos();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->ChannelsSplit(2);
		draw->ChannelsSetCurrent(1);
		ImGui::TextWrapped("%s", label.data());
		ImGui::SetCursorScreenPos(ImVec2(row.x, (std::max)(ImGui::GetCursorScreenPos().y, row.y + 38 * scale + ImGui::GetStyle().ItemSpacing.y)));
		std::uint32_t engines = 0;
		for (const PresenceStatus selected : entry.selection.engines)
			engines += selected == PresenceStatus_Present;
		ImGui::TextWrapped("%u engines | threads %s | %u repeats | %s", engines, QueueThreadText(entry.selection.threads).data(),
		    entry.selection.repeatCount, entry.selection.storage == ResultStorage_Local ? "Test" : "Release");
		ImGui::TextWrapped("Verify physics %s", entry.selection.verificationMode == VerificationMode_On ? "On" : "Off");
		ImGui::TextWrapped("Replay %s%s%s", entry.selection.recordingMode == RecordingMode_On ? "On" : "Off",
		    entry.selection.recordingMode == RecordingMode_On ? " | capture threads " : "",
		    entry.selection.recordingMode == RecordingMode_On ? QueueThreadText(entry.selection.recordingThreads).data() : "");
		if (entry.error.code != ArenaStatus_Ok)
			DrawStatusError(entry.error);
		const ImVec2 end = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(row);
		draw->ChannelsSetCurrent(0);
		const float height = end.y - row.y - ImGui::GetStyle().ItemSpacing.y;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		if (ImGui::Selectable("##queue_row", queue.selectedId == entry.id, 0, ImVec2(0, height)))
			queue.selectedId = entry.id;
		ImGui::PopStyleVar();
		draw->ChannelsMerge();
		ImGui::SetCursorScreenPos(end);
		ImGui::Separator();
		ImGui::PopID();
	}
	if (queue.entries.empty())
		ImGui::TextWrapped("Add the draft or three recommended runs. Review the queue before Start. Saved results are in Recent runs");
	ImGui::EndChild();
	NativeRunQueueEntry* selected = FindNativeRunQueueEntry(&queue, queue.selectedId);
	if (selected != nullptr)
	{
		ImGui::TextWrapped("Selected: %s", NativeRunQueueEntryStateText(selected->state));
		ImGui::BeginDisabled(selected->state != NativeRunQueueEntryState_Pending);
		ImGui::BeginDisabled(queue.editingId != 0);
		if (ImGui::SmallButton("Edit"))
			command = NativeUiCommand_EditQueue;
		ImGui::SameLine();
		if (ImGui::SmallButton("Remove"))
			command = NativeUiCommand_RemoveQueue;
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::SmallButton("Move up"))
			command = NativeUiCommand_MoveQueueUp;
		ImGui::SameLine();
		if (ImGui::SmallButton("Move down"))
			command = NativeUiCommand_MoveQueueDown;
		ImGui::EndDisabled();
		if (ImGui::CollapsingHeader("Queued configuration", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (std::uint32_t engine = 0; engine < app->model.catalog.engineCount; ++engine)
				if (selected->selection.engines[engine] == PresenceStatus_Present)
				{
					const std::string_view name = CatalogTextView(&app->model.catalog, app->model.catalog.engines[engine].displayName);
					ImGui::TextWrapped("%.*s", static_cast<int>(name.size()), name.data());
				}
			EffectiveRunConfiguration configuration = {};
			StatusRecord error = {};
			if (ComposeRunSettings(&app->model.catalog, selected->selection.caseIndex, &selected->selection.settings,
			    {}, &configuration, &error) == ArenaStatus_Ok)
				DrawRunCaseSummary(configuration);
			else
				DrawStatusError(error);
		}
	}
	if (ImGui::CollapsingHeader("Recent runs", ImGuiTreeNodeFlags_DefaultOpen))
		DrawRecentRuns(app);
	ImGui::EndChild();
	return command;
}
}
