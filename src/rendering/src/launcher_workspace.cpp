#include "launcher_app_internal.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace benchmark_visual
{
using namespace physics_arena;

int DrawWorkspaceTab(const char* label, PresenceStatus selected, float width, float height)
{
	const float scale = ImGui::GetStyle().FontScaleDpi;
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
	ImGui::PushStyleColor(ImGuiCol_Button, selected == PresenceStatus_Present ?
	    ImGui::GetStyleColorVec4(ImGuiCol_TableHeaderBg) : ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_Text, selected == PresenceStatus_Present ?
	    ImVec4(0.941f, 0.949f, 0.961f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	const int pressed = ImGui::Button(label, ImVec2(width * scale, height * scale));
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(2);
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 end = ImGui::GetItemRectMax();
	draw->AddLine(ImVec2(end.x, origin.y), end, IM_COL32(38, 43, 50, 255), scale);
	if (selected == PresenceStatus_Present)
		draw->AddRectFilled(ImVec2(origin.x, end.y - 2 * scale), end, IM_COL32(76, 136, 213, 255));
	return pressed;
}

void DrawPaneHeading(const char* label, float height)
{
	const float scale = ImGui::GetStyle().FontScaleDpi;
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 end(origin.x + ImGui::GetContentRegionAvail().x, origin.y + height * scale);
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
	draw->AddLine(ImVec2(origin.x, end.y), end, ImGui::GetColorU32(ImGuiCol_Border), scale);
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Heading), 12 * kNativeUiBodyLineScale);
	draw->AddText(ImVec2(origin.x + 8 * scale, origin.y + (height * scale - ImGui::GetFontSize()) * 0.5f),
	    IM_COL32(240, 242, 245, 255), label);
	ImGui::PopFont();
	ImGui::Dummy(ImVec2(end.x - origin.x, height * scale));
}

NativeUiCommand DrawPhysicsArenaUi(PhysicsArenaApp* app)
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float scale = app->platform.dpiScale;
	if (app->ui.layoutInitialized != PresenceStatus_Present)
	{
		if (ActiveAction(app->action) != PresenceStatus_Present && app->events->rowCount == 0)
			app->ui.dockCollapsed = PresenceStatus_Present;
		app->ui.layoutInitialized = PresenceStatus_Present;
		app->ui.expandedDockHeight = kNativeDockHeight;
	}
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	ImGui::Begin("Physics Arena", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
	             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
	             (app->replay != nullptr && app->ui.replayPage == NativeReplayPage_Player ? ImGuiWindowFlags_NoBackground : 0));
	ImGui::BeginChild("product_bar", ImVec2(0, kNativeMenuHeight * scale), ImGuiChildFlags_None,
	                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Heading), 12 * kNativeUiBodyLineScale);
	const float productWidth = ImGui::CalcTextSize("Physics Arena").x + 20 * scale;
	ImGui::PopFont();
	DrawPaneHeading("Physics Arena", kNativeMenuHeight);
	ImGui::SetCursorPos(ImVec2(productWidth, 0));
	const std::array<const char*, 3> views = {"Run", "Results", "Replay"};
	for (std::uint32_t index = 0; index < views.size(); ++index)
	{
		if (index != 0)
			ImGui::SameLine(0, 0);
		const NativeArenaView view = static_cast<NativeArenaView>(index);
		ImGui::BeginDisabled(view != NativeArenaView_Run && RunWorkspaceBusy(app) == PresenceStatus_Present);
		if (DrawWorkspaceTab(views[index], view == app->model.view ? PresenceStatus_Present : PresenceStatus_Absent,
		    72, kNativeMenuHeight) != 0 && view != app->model.view)
		{
			if (app->replay != nullptr || RayTracingReplayPresence(app) == PresenceStatus_Present)
				CloseReplayView(app);
			app->model.view = view;
		}
		ImGui::EndDisabled();
	}
	ImGui::GetWindowDrawList()->AddLine(ImVec2(viewport->WorkPos.x + productWidth, viewport->WorkPos.y),
	    ImVec2(viewport->WorkPos.x + productWidth, viewport->WorkPos.y + kNativeMenuHeight * scale), IM_COL32(38, 43, 50, 255), scale);
	ImGui::EndChild();
	app->resultViewSelectionPending = PresenceStatus_Absent;
	NativeUiCommand command = NativeUiCommand_None;
	const float statusHeight = kNativeStatusHeight * scale;
	const float availableHeight = (std::max)(1.0f, viewport->WorkSize.y / scale - kNativeMenuHeight - kNativeStatusHeight);
	const float splitterHeight = app->model.view != NativeArenaView_Replay && app->ui.dockCollapsed != PresenceStatus_Present ? 5.0f : 0;
	const float minimumDock = kNativeDockHeaderHeight + 40.0f;
	const int compactRun = viewport->WorkSize.x / scale < 1100 || viewport->WorkSize.y / scale <= 600;
	const float minimumMain = app->model.view == NativeArenaView_Run ? (compactRun != 0 ? 420.0f : 350.0f) : 240.0f;
	const float maximumDock = (std::max)(minimumDock, availableHeight - minimumMain - splitterHeight);
	const float dockHeight = app->model.view == NativeArenaView_Replay ? 0 :
	    (app->ui.dockCollapsed == PresenceStatus_Present ? kNativeDockHeaderHeight :
	     std::clamp(app->ui.expandedDockHeight, minimumDock, maximumDock)) * scale;
	const float contentHeight = (std::max)(1.0f, availableHeight * scale - dockHeight - splitterHeight * scale);
	ImGui::BeginChild("workspace_content", ImVec2(0, contentHeight), ImGuiChildFlags_None,
	                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
	                  (app->replay != nullptr && app->ui.replayPage == NativeReplayPage_Player ? ImGuiWindowFlags_NoBackground : 0));
	ImGui::PopStyleVar(2);
	if (app->model.startupStatus != ArenaStatus_Ok)
	{
		DrawStatusError(app->model.startupError);
		if (ImGui::Button("Retry"))
			command = NativeUiCommand_Retry;
	}
	else if (app->model.view == NativeArenaView_Run)
		command = DrawRunView(app);
	else if (app->model.view == NativeArenaView_Results)
		command = DrawResultsView(app);
	else
		command = DrawReplayWorkspace(app);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	ImGui::EndChild();
	if (dockHeight > 0)
	{
		if (splitterHeight > 0)
		{
			const ImVec2 start = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton("output_splitter", ImVec2(-1, splitterHeight * scale));
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
			if (ImGui::IsItemActivated())
				app->ui.dockDragStartHeight = dockHeight / scale;
			if (ImGui::IsItemActive())
				app->ui.expandedDockHeight = std::clamp(app->ui.dockDragStartHeight - ImGui::GetMouseDragDelta().y / scale,
				    minimumDock, maximumDock);
			ImGui::GetWindowDrawList()->AddRectFilled(start, ImGui::GetItemRectMax(),
			    ImGui::GetColorU32(ImGui::IsItemActive() || ImGui::IsItemHovered() ? ImGuiCol_SeparatorActive : ImGuiCol_Separator));
		}
		ImGui::BeginChild("activity_dock", ImVec2(0, dockHeight), ImGuiChildFlags_None,
		                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		const NativeUiCommand dockCommand = DrawActivityDock(app);
		if (dockCommand != NativeUiCommand_None)
			command = dockCommand;
		ImGui::EndChild();
	}
	ImGui::BeginChild("status_bar", ImVec2(0, statusHeight));
	ImGui::SetCursorPos(ImVec2(7 * scale, 5 * scale));
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 10);
	if (app->uiError.code != ArenaStatus_Ok)
	{
		ImGui::TextColored(ImVec4(0.87f, 0.40f, 0.40f, 1), "%.*s", static_cast<int>(app->uiError.detailSize), app->uiError.detail.data());
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			ImGui::SetTooltip("%.*s", static_cast<int>(app->uiError.detailSize), app->uiError.detail.data());
	}
	else if (ActiveAction(app->action) == PresenceStatus_Present && app->action.kind == NativeActionKind_Storage)
		ImGui::TextUnformatted(FormatNativeStorageProgress(app->action).data());
	else if (ActiveAction(app->action) == PresenceStatus_Present)
		ImGui::Text("Action in progress | %u / %u work units", app->action.control.completedUnitCount.load(std::memory_order_relaxed) + app->action.control.failedUnitCount.load(std::memory_order_relaxed),
		            app->action.request.totalUnitCount);
	else if (app->model.view != NativeArenaView_Run && app->results.modelPresence == PresenceStatus_Present)
	{
		const ResultViewModel& result = app->workspace.finalization.model;
		const std::string_view name = ResultViewTextView(&result, result.caseDisplayName);
		ImGui::TextDisabled("%.*s | %u engine%s | %u thread count%s | %u repeat%s", static_cast<int>(name.size()), name.data(),
		    result.engineCount, result.engineCount == 1 ? "" : "s", result.threadCount, result.threadCount == 1 ? "" : "s",
		    result.repeatCount, result.repeatCount == 1 ? "" : "s");
	}
	else if (app->model.startupStatus == ArenaStatus_Ok)
	{
		const std::string_view name = CatalogTextView(&app->model.catalog, app->model.catalog.cases[app->model.selection.caseIndex].displayName);
		std::uint32_t engines = 0;
		std::uint32_t threads = 0;
		for (PresenceStatus selected : app->model.selection.engines)
			engines += selected == PresenceStatus_Present ? 1u : 0u;
		for (PresenceStatus selected : app->model.selection.threads)
			threads += selected == PresenceStatus_Present ? 1u : 0u;
		ImGui::TextDisabled("%.*s | %u engine%s | %u thread count%s | %u repeat%s", static_cast<int>(name.size()), name.data(),
		    engines, engines == 1 ? "" : "s", threads, threads == 1 ? "" : "s", app->model.selection.repeatCount,
		    app->model.selection.repeatCount == 1 ? "" : "s");
	}
	ImGui::PopFont();
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	const NativeUiCommand picker = DrawRecordingPicker(app);
	if (picker != NativeUiCommand_None)
		command = picker;
	if (app->results.deletionPending == PresenceStatus_Present)
	{
		PauseReplayView(app);
		ImGui::OpenPopup("Delete recordings permanently?");
		app->results.deletionPending = PresenceStatus_Absent;
	}
	const NativeUiCommand deletion = DrawRecordingDeletionConfirmation(app);
	if (deletion != NativeUiCommand_None)
		command = deletion;
	ImGui::End();
	return command;
}
}
