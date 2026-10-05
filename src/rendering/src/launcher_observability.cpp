#include "launcher_app_internal.h"

#include <imgui.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace benchmark_visual
{
using namespace physics_arena;
namespace
{
std::string_view EventText(const NativeEventStore* store, std::uint32_t offset, std::uint32_t size)
{
	return offset <= store->textUsed && size <= store->textUsed - offset
	           ? std::string_view(store->textArena.data() + offset, size)
			   : std::string_view{};
}

std::string_view EventTimestamp(const NativeEventStore* store, const NativeEventRow& row)
{
	return EventText(store, row.timestampOffset, row.timestampSize);
}

std::string_view EventComponentText(const NativeEventStore* store, const NativeEventRow& row)
{
	return EventText(store, row.componentOffset, row.componentSize);
}

std::string_view EventStatusText(const NativeEventStore* store, const NativeEventRow& row)
{
	return EventText(store, row.statusOffset, row.statusSize);
}

std::string_view EventDetailText(const NativeEventStore* store, const NativeEventRow& row)
{
	return EventText(store, row.detailOffset, row.detailSize);
}

ImVec4 EventColor(std::string_view status)
{
	if (status == "ok")
		return ImVec4(0.32f, 0.72f, 0.43f, 1.0f);
	if (status == "running")
		return ImVec4(0.28f, 0.58f, 0.92f, 1.0f);
	if (status == "interrupted" || status == "required")
		return ImVec4(0.93f, 0.66f, 0.18f, 1.0f);
	return ImVec4(0.90f, 0.28f, 0.25f, 1.0f);
}

void CopySelectedEvent(const PhysicsArenaApp* app)
{
	std::array<char, 65536> output = {};
	FormatSelectedEvent(app, &output);
	ImGui::SetClipboardText(output.data());
}

void CopyVisibleEvents(PhysicsArenaApp* app)
{
	char* output = nullptr;
	if (CreateVisibleEventCopy(app, &output, &app->uiError) != physics_arena::ArenaStatus_Ok)
		return;
	ImGui::SetClipboardText(output);
	delete[] output;
	if (std::string_view(app->uiError.component.data(), app->uiError.componentSize) == "activity_copy")
		app->uiError = {};
}

}

void DrawNativeObservability(PhysicsArenaApp* app)
{
	const float scale = app->platform.dpiScale;
	const ImVec2 origin = ImGui::GetCursorPos();
	const float actionWidth = 116 * scale;
	const float logWidth = ImGui::GetContentRegionAvail().x - actionWidth;
	ImGui::SetCursorPos(ImVec2(origin.x + logWidth, origin.y));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7 * scale, 0));
	ImGui::BeginChild("output_actions", ImVec2(actionWidth, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::PopStyleVar();
	if (app->ui.dockPage != NativeDockPage_Warnings)
	{
		int view = app->logView == NativeLogView_Activity ? 0 : 1;
		ImGui::SetNextItemWidth(-1);
		if (ImGui::Combo("##output_detail", &view, "Activity\0Full details\0"))
			app->logView = view == 0 ? NativeLogView_Activity : NativeLogView_FullDetails;
	}
	const int followChanged = ImGui::Button(app->followStatus == NativeFollowStatus_Following ? "Pause follow" : "Follow", ImVec2(-1, 23 * scale));
	if (followChanged != 0)
		app->followStatus = app->followStatus == NativeFollowStatus_Following ? NativeFollowStatus_Paused : NativeFollowStatus_Following;
	if (ImGui::Button("Copy selected", ImVec2(-1, 23 * scale)))
		CopySelectedEvent(app);
	if (ImGui::Button("Copy visible", ImVec2(-1, 23 * scale)))
		CopyVisibleEvents(app);
	ImGui::EndChild();
	ImGui::SetCursorPos(origin);
	ImGui::BeginChild("activity_rows", ImVec2(logWidth, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
	                  ImGuiWindowFlags_HorizontalScrollbar);
	ImGuiStorage* scroll = ImGui::GetStateStorage();
	const ImGuiID heightId = ImGui::GetID("previous_viewport_height");
	const float previousHeight = scroll->GetFloat(heightId);
	const float height = ImGui::GetWindowHeight();
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 10);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 3 * scale));
	if (std::string_view(app->uiError.component.data(), app->uiError.componentSize) == "activity_copy")
		DrawStatusError(app->uiError);
	std::uint32_t visibleCount = 0;
	for (std::uint32_t index = 0; index < app->events->rowCount; ++index)
	{
		const NativeEventRow& row = app->events->rows[index];
		if (VisibleEvent(app->events, row, app->logView) != physics_arena::PresenceStatus_Present)
			continue;
		++visibleCount;
		const std::string_view timestamp = EventTimestamp(app->events, row);
		const std::string_view component = EventComponentText(app->events, row);
		const std::string_view status = EventStatusText(app->events, row);
		const std::string_view detail = EventDetailText(app->events, row);
		ImGui::PushID(static_cast<int>(index));
		const ImVec4 color = EventColor(status);
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		const char* preview = detail.empty() ? component.data() : detail.data();
		const int previewSize = static_cast<int>(detail.empty() ? component.size() : detail.size());
		const ImVec2 rowOrigin = ImGui::GetCursorPos();
		if (ImGui::Selectable("##event", app->selectedEventSequence == row.sequence,
		                      ImGuiSelectableFlags_SpanAllColumns, ImVec2(0.0f, ImGui::GetTextLineHeight())))
			app->selectedEventSequence = row.sequence;
		ImGui::SetCursorPos(rowOrigin);
		if (app->logView == NativeLogView_Activity)
			ImGui::TextUnformatted(preview, preview + previewSize);
		else
			ImGui::Text("%.*s  %.*s  %.*s  %.*s", static_cast<int>(timestamp.size()), timestamp.data(),
			            static_cast<int>(status.size()), status.data(), static_cast<int>(component.size()),
			            component.data(), static_cast<int>(detail.size()), detail.data());
		ImGui::PopStyleColor();
		ImGui::PopID();
	}
	if (visibleCount == 0)
		ImGui::TextDisabled(app->logView == NativeLogView_WarningsErrors ? "No warnings" : "No activity");
	if (app->followStatus == NativeFollowStatus_Following)
	{
		if (previousHeight == height && followChanged == 0 && app->lastFollowGeneration == app->events->generation &&
		    ImGui::GetScrollY() < ImGui::GetScrollMaxY() - 1.0f)
			app->followStatus = NativeFollowStatus_Paused;
		else
			ImGui::SetScrollHereY(1.0f);
		app->lastFollowGeneration = app->events->generation;
	}
	scroll->SetFloat(heightId, height);
	ImGui::PopStyleVar();
	ImGui::PopFont();
	ImGui::EndChild();
}
NativeUiCommand DrawActivityDock(PhysicsArenaApp* app)
{
	NativeUiCommand command = NativeUiCommand_None;
	const std::array<const char*, 2> pages = {"Output", "Warnings"};
	std::uint32_t warnings = 0;
	for (std::uint32_t index = 0; index < app->events->rowCount; ++index)
		if (VisibleEvent(app->events, app->events->rows[index], NativeLogView_WarningsErrors) == PresenceStatus_Present)
			++warnings;
	const float scale = app->platform.dpiScale;
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImGui::GetWindowDrawList()->AddLine(origin, ImVec2(origin.x + ImGui::GetContentRegionAvail().x, origin.y), ImGui::GetColorU32(ImGuiCol_Border), scale);
	ImGui::BeginChild("dock_header", ImVec2(0, kNativeDockHeaderHeight * scale), ImGuiChildFlags_None,
	    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	for (std::uint32_t index = 0; index < pages.size(); ++index)
	{
		if (index != 0)
			ImGui::SameLine(0, 0);
		std::array<char, 64> label = {};
		if (index == NativeDockPage_Warnings)
			std::snprintf(label.data(), label.size(), "%s %u", pages[index], warnings);

		else
			std::snprintf(label.data(), label.size(), "%s", pages[index]);
		if (DrawWorkspaceTab(label.data(), app->ui.dockPage == static_cast<NativeDockPage>(index) ?
		    PresenceStatus_Present : PresenceStatus_Absent, index == 0 ? 72 : 110, kNativeDockHeaderHeight))
		{
			app->ui.dockPage = static_cast<NativeDockPage>(index);
			app->ui.dockCollapsed = PresenceStatus_Absent;
		}
	}
	ImGui::SameLine();
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 76 * scale);
	if (DrawWorkspaceTab(app->ui.dockCollapsed == PresenceStatus_Present ? "Expand" : "Collapse",
	    PresenceStatus_Absent, 76, kNativeDockHeaderHeight))
		app->ui.dockCollapsed = app->ui.dockCollapsed == PresenceStatus_Present ? PresenceStatus_Absent : PresenceStatus_Present;
	ImGui::EndChild();
	if (app->ui.dockCollapsed == PresenceStatus_Present)
		return command;
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7 * scale, 3 * scale));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7 * scale, 4 * scale));
	ImGui::BeginChild("dock_content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
	if (app->queue.activeId != 0)
	{
		std::size_t ordinal = 0;
		while (app->queue.entries[ordinal].id != app->queue.activeId)
			++ordinal;
		const std::uint32_t engineIndex = app->action.control.currentEngineIndex.load(std::memory_order_relaxed);
		const std::string_view engine = CatalogTextView(&app->model.catalog, app->model.catalog.engines[engineIndex].displayName);
		ImGui::TextWrapped("Item %zu/%zu | %.*s | thread %u | repeat %u/%u", ordinal + 1, app->queue.entries.size(),
		    static_cast<int>(engine.size()), engine.data(), app->action.control.currentThreadCount.load(std::memory_order_relaxed),
		    app->action.control.currentRepeatIndex.load(std::memory_order_relaxed) + 1, app->action.request.repeatCount);
		ImGui::TextWrapped("Passed %u | completed unverified %u | quality failed %u | execution failed %u | skipped %u",
		    app->action.control.passedRepeatCount.load(std::memory_order_relaxed),
		    app->action.control.unverifiedRepeatCount.load(std::memory_order_relaxed),
		    app->action.control.qualityFailedRepeatCount.load(std::memory_order_relaxed),
		    app->action.control.executionFailedRepeatCount.load(std::memory_order_relaxed),
		    app->action.control.skippedRepeatCount.load(std::memory_order_relaxed));
	}

	{
		if (app->ui.dockPage == NativeDockPage_Warnings)
			app->logView = NativeLogView_WarningsErrors;
		else if (app->logView == NativeLogView_WarningsErrors)
			app->logView = NativeLogView_Activity;
		DrawNativeObservability(app);
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	return command;
}


}
