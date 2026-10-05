#include "launcher_app_internal.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>

namespace benchmark_visual
{
using namespace physics_arena;

void DrawResultTableValue(const char* text)
{
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 11);
	const float remaining = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text).x;
	if (remaining > 0)
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + remaining);
	ImGui::TextUnformatted(text);
	ImGui::PopFont();
}

ImVec4 ResultEngineColor(std::uint32_t colorRgb)
{
	return ImVec4(static_cast<float>((colorRgb >> 16) & 0xff) / 255.0f,
	              static_cast<float>((colorRgb >> 8) & 0xff) / 255.0f, static_cast<float>(colorRgb & 0xff) / 255.0f,
	              1.0f);
}

std::uint32_t CountSelectedEngines(std::uint32_t mask)
{
	std::uint32_t count = 0;
	while (mask != 0)
	{
		count += mask & 1u;
		mask >>= 1u;
	}
	return count;
}

std::uint32_t SingleSelectedEngine(std::uint32_t mask)
{
	for (std::uint32_t index = 0; index < kEngineCapacity; ++index)
		if ((mask & (1u << index)) != 0)
			return index;
	return static_cast<std::uint32_t>(kEngineCapacity);
}

void DrawRunPickerLabel(const ResultIndexRecord& record, ImVec2 origin, float width)
{
	NativeRunLabelProjection identity = {};
	ProjectNativeRunLabel(IndexText(record.runSlug), record.threadCount, record.repeatCount, &identity);
	const NativeValueText threads = FormatNativeThreadSet(record.threadCounts.data(), record.threadCount);
	NativeValueText metadata = {};
	std::snprintf(metadata.data(), metadata.size(), "%s | %u engine%s, threads %s, %u repeat%s",
	    record.storage == ResultStorage_Local ? "Test" : "Release", record.engineCount,
	    record.engineCount == 1 ? "" : "s", threads.data(), record.repeatCount, record.repeatCount == 1 ? "" : "s");
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(origin, ImVec2(origin.x + width, origin.y + ImGui::GetFrameHeight()), true);
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Body), 10 * kNativeUiBodyLineScale);
	const float baseline = origin.y + ImGui::GetFontBaked()->Ascent;
	ImGui::PopFont();
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 11);
	draw->AddText(ImVec2(origin.x, baseline - ImGui::GetFontBaked()->Ascent), ImGui::GetColorU32(ImGuiCol_Text), identity.text.data());
	const float offset = ImGui::CalcTextSize(identity.text.data()).x + 14 * ImGui::GetStyle().FontScaleDpi;
	ImGui::PopFont();
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Body), 10 * kNativeUiBodyLineScale);
	if (offset + ImGui::CalcTextSize(metadata.data()).x <= width)
		draw->AddText(ImVec2(origin.x + offset, origin.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), metadata.data());
	ImGui::PopFont();
	draw->PopClipRect();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		ImGui::SetTooltip("%s\n%u engines\nThreads: %s\n%u repeats\nProcessor: %s\nRun ID: %s", identity.text.data(), record.engineCount,
		    threads.data(), record.repeatCount, record.cpuName.data.data(), record.runSlug.data.data());
}

int BeginRunPickerTable(PresenceStatus includeCase)
{
	const float scale = ImGui::GetStyle().FontScaleDpi;
	const int secondary = ImGui::GetContentRegionAvail().x >= (includeCase == PresenceStatus_Present ? 600 : 400) * scale;
	const int columns = (includeCase == PresenceStatus_Present ? 2 : 1) + (secondary != 0 ? 3 : 0);
	if (!ImGui::BeginTable("saved_run_choices", columns,
	    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoSavedSettings))
		return 0;
	if (includeCase == PresenceStatus_Present)
		ImGui::TableSetupColumn("Case", ImGuiTableColumnFlags_WidthStretch, 1.8f);
	ImGui::TableSetupColumn("Run", ImGuiTableColumnFlags_WidthFixed, 200 * scale);
	if (secondary != 0)
	{
		ImGui::TableSetupColumn("Engines", ImGuiTableColumnFlags_WidthFixed, 52 * scale);
		ImGui::TableSetupColumn("Threads", ImGuiTableColumnFlags_WidthStretch, 1);
		ImGui::TableSetupColumn("Repeats", ImGuiTableColumnFlags_WidthFixed, 52 * scale);
	}
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
	ImGui::TableHeadersRow();
	ImGui::PopItemFlag();
	ImGui::PopStyleColor();
	return 1;
}

int DrawRunPickerRow(const ResultIndexRecord& record, PresenceStatus selected, PresenceStatus includeCase)
{
	const float scale = ImGui::GetStyle().FontScaleDpi;
	NativeRunLabelProjection identity = {};
	ProjectNativeRunLabel(IndexText(record.runSlug), record.threadCount, record.repeatCount, &identity);
	const NativeValueText threads = FormatNativeThreadSet(record.threadCounts.data(), record.threadCount);
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(7 * scale, 0));
	ImGui::TableNextRow(0, 31 * scale);
	ImGui::TableSetColumnIndex(0);
	const ImVec2 origin = ImGui::GetCursorPos();
	const int pressed = ImGui::Selectable("##saved_run", selected == PresenceStatus_Present,
	    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
	    ImVec2(0, 31 * scale - 2 * ImGui::GetStyle().CellPadding.y));
	const int hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
	ImGui::SetCursorPos(ImVec2(origin.x, origin.y + 7 * scale));
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Heading), 12 * kNativeUiBodyLineScale);
	const float baseline = ImGui::GetCursorPosY() + ImGui::GetFontBaked()->Ascent;
	if (includeCase == PresenceStatus_Present)
		ImGui::TextUnformatted(record.caseName.data.data());
	ImGui::PopFont();
	if (includeCase == PresenceStatus_Present)
		ImGui::TableNextColumn();
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 11);
	const float dataY = baseline - ImGui::GetFontBaked()->Ascent;
	ImGui::SetCursorPosY(dataY);
	ImGui::Text("%s | %s", identity.text.data(), record.storage == ResultStorage_Local ? "Test" : "Release");
	if (ImGui::TableGetColumnCount() > 2)
	{
		ImGui::TableNextColumn();
		ImGui::SetCursorPosY(dataY);
		ImGui::Text("%u", record.engineCount);
		ImGui::TableNextColumn();
		ImGui::SetCursorPosY(dataY);
		ImGui::TextUnformatted(threads.data());
		ImGui::TableNextColumn();
		ImGui::SetCursorPosY(dataY);
		ImGui::Text("%u", record.repeatCount);
	}
	ImGui::PopFont();
	ImGui::PopStyleVar();
	if (hovered != 0)
		ImGui::SetTooltip("%s\n%s\n%u engines\nThreads: %s\n%u repeats\nProcessor: %s\nResult: results/%s%s/%s/%s",
		    record.caseName.data.data(), identity.text.data(), record.engineCount, threads.data(), record.repeatCount,
		    record.cpuName.data.data(), record.storage == ResultStorage_Local ? "local/" : "",
		    record.caseSlug.data.data(), record.cpuSlug.data.data(), record.runSlug.data.data());
	return pressed;
}

NativeUiCommand DrawResultsView(PhysicsArenaApp* app)
{
	NativeUiCommand command = NativeUiCommand_None;
	ResultIndexWorkspace& indexes = app->workspace.finalization.indexes;
	const PresenceStatus active = ActiveAction(app->action);
	if (active == PresenceStatus_Present)
	{
		ImGui::TextWrapped("Results are unavailable while the current action owns the result workspace");
		if (ImGui::Button("Cancel action"))
			return NativeUiCommand_Cancel;
		return command;
	}
	const float scale = app->platform.dpiScale;
	const int compact = ImGui::GetContentRegionAvail().x / scale < 1000;
	const int inlineActions = ImGui::GetContentRegionAvail().x / scale >= 1400;
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7 * scale, 5 * scale));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7 * scale, 5.5f * scale));
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(3 * scale, 0));
	ImGui::BeginChild("results_toolbar", ImVec2(0, kNativeToolbarHeight * scale), ImGuiChildFlags_AlwaysUseWindowPadding,
	    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7 * scale, 4 * scale));
	const float actionWidth = (inlineActions != 0 ? 400 : 145) * scale;
	ImGui::BeginChild("result_selectors", ImVec2(ImGui::GetContentRegionAvail().x - actionWidth, 0), ImGuiChildFlags_None,
	    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if (app->results.libraryPresence == PresenceStatus_Present &&
	    app->results.selectedRecordIndex < indexes.recordCount)
	{
		std::uint32_t selectedIndex = app->results.selectedRecordIndex;
		std::uint32_t pendingIndex = indexes.recordCount;
		if (ImGui::BeginTable("result_selection_controls", 3,
		                      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
		{
			ImGui::TableSetupColumn("##result_case_control", ImGuiTableColumnFlags_WidthStretch, 0.25f);
			ImGui::TableSetupColumn("##result_processor_control", ImGuiTableColumnFlags_WidthStretch, 0.20f);
			ImGui::TableSetupColumn("##result_run_control", ImGuiTableColumnFlags_WidthStretch, 0.55f);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("Case");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.0f);
			const ResultIndexRecord& caseSelection = indexes.records[selectedIndex];
			const int caseOpen = ImGui::BeginCombo("##result_case", caseSelection.caseName.data.data());
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
				ImGui::SetTooltip("%s", caseSelection.caseName.data.data());
			if (caseOpen != 0)
			{
				for (std::uint32_t index = 0; index < indexes.recordCount; ++index)
				{
					const ResultIndexRecord& record = indexes.records[index];
					const std::string_view caseSlug = IndexText(record.caseSlug);
					if (index != 0 && IndexText(indexes.records[index - 1].caseSlug) == caseSlug)
						continue;
					ImGui::PushID(record.caseSlug.data.data(), record.caseSlug.data.data() + record.caseSlug.size);
					if (ImGui::Selectable(record.caseName.data.data(), IndexText(caseSelection.caseSlug) == caseSlug) &&
					    IndexText(caseSelection.caseSlug) != caseSlug)
					{
						std::uint32_t fallback = indexes.recordCount;
						std::uint32_t preserved = indexes.recordCount;
						for (std::uint32_t candidate = 0; candidate < indexes.recordCount; ++candidate)
						{
							const ResultIndexRecord& item = indexes.records[candidate];
							if (IndexText(item.caseSlug) != caseSlug)
								continue;
							fallback = candidate;
							if (IndexText(item.cpuSlug) == IndexText(caseSelection.cpuSlug))
								preserved = candidate;
						}
						pendingIndex = preserved != indexes.recordCount ? preserved : fallback;
						selectedIndex = pendingIndex;
					}
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}

			ImGui::TableSetColumnIndex(1);
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("Processor");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.0f);
			const ResultIndexRecord& processorSelection = indexes.records[selectedIndex];
			const int processorOpen = ImGui::BeginCombo("##result_processor", processorSelection.cpuName.data.data());
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
				ImGui::SetTooltip("%s", processorSelection.cpuName.data.data());
			if (processorOpen != 0)
			{
				for (std::uint32_t index = 0; index < indexes.recordCount; ++index)
				{
					const ResultIndexRecord& record = indexes.records[index];
					if (IndexText(record.caseSlug) != IndexText(processorSelection.caseSlug))
						continue;
					const std::string_view cpuSlug = IndexText(record.cpuSlug);
					if (index != 0 && IndexText(indexes.records[index - 1].caseSlug) == IndexText(record.caseSlug) &&
					    IndexText(indexes.records[index - 1].cpuSlug) == cpuSlug)
						continue;
					ImGui::PushID(record.cpuSlug.data.data(), record.cpuSlug.data.data() + record.cpuSlug.size);
					if (ImGui::Selectable(record.cpuName.data.data(),
					                      IndexText(processorSelection.cpuSlug) == cpuSlug) &&
					    IndexText(processorSelection.cpuSlug) != cpuSlug)
					{
						for (std::uint32_t candidate = 0; candidate < indexes.recordCount; ++candidate)
						{
							const ResultIndexRecord& item = indexes.records[candidate];
							if (IndexText(item.caseSlug) == IndexText(processorSelection.caseSlug) &&
							    IndexText(item.cpuSlug) == cpuSlug)
								pendingIndex = candidate;
						}
						selectedIndex = pendingIndex;
					}
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}

			ImGui::TableSetColumnIndex(2);
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("Result");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.0f);
			const ResultIndexRecord& resultSelection = indexes.records[selectedIndex];
			const ImVec2 previewOrigin = ImGui::GetCursorScreenPos();
			const float previewWidth = ImGui::GetContentRegionAvail().x;
			ImGui::SetNextWindowSizeConstraints(ImVec2((std::min)(460 * scale, app->platform.width - 24 * scale), 0),
			    ImVec2((std::min)(720 * scale, app->platform.width - 24 * scale), (std::min)(420 * scale, app->platform.height - 90 * scale)));
			const int resultOpen = ImGui::BeginCombo("##result_run", "");
			if (resultOpen == 0)
				DrawRunPickerLabel(resultSelection, ImVec2(previewOrigin.x + 7 * scale, previewOrigin.y + 7 * scale), previewWidth - 42 * scale);
			if (resultOpen != 0)
			{
				if (BeginRunPickerTable(PresenceStatus_Absent) != 0)
				{
					for (std::uint32_t index = 0; index < indexes.recordCount; ++index)
					{
						const ResultIndexRecord& record = indexes.records[index];
						if (IndexText(record.caseSlug) != IndexText(resultSelection.caseSlug) ||
						    IndexText(record.cpuSlug) != IndexText(resultSelection.cpuSlug))
							continue;
						ImGui::PushID(static_cast<int>(index));
						if (DrawRunPickerRow(record, selectedIndex == index ? PresenceStatus_Present : PresenceStatus_Absent,
						    PresenceStatus_Absent) != 0)
						{
							pendingIndex = index;
							selectedIndex = index;
						}
						ImGui::PopID();
					}
					ImGui::EndTable();
				}
				ImGui::EndCombo();
				DrawRunPickerLabel(resultSelection, ImVec2(previewOrigin.x + 7 * scale, previewOrigin.y + 7 * scale), previewWidth - 42 * scale);
			}
			ImGui::EndTable();
		}
		if (pendingIndex != indexes.recordCount)
		{
			std::array<wchar_t, kRunPathCapacity> directory = {};
			StatusRecord error = {};
			const ArenaStatus pathStatus = BuildRecordDirectory(app->model, indexes.records[pendingIndex], &directory);
			const ArenaStatus loadStatus =
			    pathStatus == ArenaStatus_Ok ? LoadResultDirectory(app, directory.data(), &error) : pathStatus;
			if (loadStatus == ArenaStatus_Ok)
				app->results.selectedRecordIndex = pendingIndex;
			else
			{
				app->results.status = error.code == ArenaStatus_Ok ? loadStatus : error.code;
				app->results.error = error;
			}
		}
	}
	else if (app->results.modelPresence == PresenceStatus_Present)
	{
		const ResultViewModel& selected = app->workspace.finalization.model;
		const std::string_view caseName = ResultViewTextView(&selected, selected.caseDisplayName);
		const std::string_view run = ResultViewTextView(&selected, selected.runId);
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%.*s | %.*s", static_cast<int>(caseName.size()), caseName.data(),
		            static_cast<int>(run.size()), run.data());
	}
	else
		ImGui::TextDisabled("No completed results found");
	ImGui::EndChild();
	ImGui::SameLine();
	if (ImGui::Button("Refresh"))
		command = NativeUiCommand_RefreshResults;
	if (app->results.modelPresence == PresenceStatus_Present)
	{
		ImGui::SameLine();
		if (inlineActions != 0)
		{
			if (ImGui::Button("Open folder"))
				command = NativeUiCommand_OpenResultFolder;
			ImGui::SameLine();
			if (ImGui::Button("Open report"))
				command = NativeUiCommand_OpenReport;
			ImGui::SameLine();
			if (ImGui::Button("Regenerate report"))
				command = NativeUiCommand_RegenerateReport;
		}
		else
		{
			if (ImGui::Button("Actions"))
				ImGui::OpenPopup("result_actions");
			if (ImGui::BeginPopup("result_actions"))
			{
				if (ImGui::MenuItem("Open folder"))
					command = NativeUiCommand_OpenResultFolder;
				if (ImGui::MenuItem("Open report"))
					command = NativeUiCommand_OpenReport;
				if (ImGui::MenuItem("Regenerate report"))
					command = NativeUiCommand_RegenerateReport;
				ImGui::EndPopup();
			}
		}
	}
	ImGui::PopStyleVar();
	ImGui::EndChild();
	ImGui::PopStyleVar(4);
	if (app->results.modelPresence == PresenceStatus_Present)
	{
		const ResultStorage storage = ClassifyResultStorage(app->model.repositoryRoot.data(), app->results.selectedDirectory.data());
		ImGui::TextDisabled("%s", storage == ResultStorage_Local ? "Test" : storage == ResultStorage_Repository ? "Release" : "External");
	}
	if (app->results.status != ArenaStatus_Ok)
		DrawStatusError(app->results.error);
	if (app->results.modelPresence != PresenceStatus_Present)
	{
		ImGui::TextWrapped("Select a saved result or configure a new benchmark");
		if (ImGui::Button("Configure benchmark"))
			app->model.view = NativeArenaView_Run;
		return command;
	}
	const ResultViewModel& model = app->workspace.finalization.model;
	if (model.verificationMode == VerificationMode_Off)
		ImGui::TextWrapped("Verification Off: physical quality not checked");
	NativeResultsState& state = app->results;
	const float inspectorWidth = compact == 0 ? (ImGui::GetContentRegionAvail().x / scale > 1120 ? 318 : 286) * scale : 0;
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	if (compact == 0 || app->ui.resultsDetailsVisible != PresenceStatus_Present)
	{
		ImGui::BeginChild("result_analysis", ImVec2(compact == 0 ? ImGui::GetContentRegionAvail().x - inspectorWidth : 0, 0), ImGuiChildFlags_Borders);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7 * scale, 4 * scale));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7 * scale, 7 * scale));
		const std::array<const char*, 6> tabs = {"Benchmark", "Thread scaling",
		    ResultViewTextView(&model, model.workUnitId) == "ray_frame" ? "Ray timing" : ResultViewTextView(&model, model.workUnitId) == "query_batch" ? "Batch timing" : "Step timing", "Repeats", "Case data", "Recordings"};
		for (std::uint32_t index = 0; index < tabs.size(); ++index)
		{
			if (index != 0)
				ImGui::SameLine(0, 0);
			const NativeResultMode mode = static_cast<NativeResultMode>(index);
			ImGui::BeginDisabled((model.measurementMode == ResultMeasurementMode_PhysicalQuality && index < 3) ||
			                     (mode == NativeResultMode_ThreadScaling && model.threadCount < 2));
			if (DrawWorkspaceTab(tabs[index], state.mode == mode ? PresenceStatus_Present : PresenceStatus_Absent,
			    index == 1 ? 108 : 90, 32))
			{
				state.mode = mode;
			}
			ImGui::EndDisabled();
		}
		if (compact != 0)
		{
			ImGui::SameLine(0, 0);
			if (DrawWorkspaceTab("Details", PresenceStatus_Absent, 80, 32))
				app->ui.resultsDetailsVisible = PresenceStatus_Present;
		}
		ImGui::BeginChild("analysis_body", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
		if (state.mode == NativeResultMode_EnginePerformance)
			DrawSummaryAnalysis(app, NativeSummaryChartMode_Compare);
		else if (state.mode == NativeResultMode_ThreadScaling)
			DrawSummaryAnalysis(app, NativeSummaryChartMode_Scaling);
		else if (state.mode == NativeResultMode_StepTiming)
		{
			if (model.timing.availability == PresenceStatus_Present)
				DrawTimingView(app);
			else
				ImGui::TextDisabled("Not recorded by this run");
		}
		else if (state.mode == NativeResultMode_Repeats)
			DrawResultRepeats(app);
		else if (state.mode == NativeResultMode_CaseData)
			DrawCaseData(app);
		else
			command = DrawResultRecordings(app);
		ImGui::EndChild();
		ImGui::PopStyleVar(2);
		ImGui::EndChild();
	}
	if (compact == 0 || app->ui.resultsDetailsVisible == PresenceStatus_Present)
	{
		if (compact == 0)
			ImGui::SameLine();
		ImGui::BeginChild("result_inspector", ImVec2(0, 0), ImGuiChildFlags_Borders);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7 * scale, 4 * scale));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7 * scale, 7 * scale));
		if (compact != 0 && ImGui::Button("Back to results"))
			app->ui.resultsDetailsVisible = PresenceStatus_Absent;
		const NativeUiCommand inspector = DrawResultInspector(app);
		ImGui::PopStyleVar(2);
		if (inspector != NativeUiCommand_None)
			command = inspector;
		ImGui::EndChild();
	}
	ImGui::PopStyleVar(2);
	return command;
}
}
