#include "launcher_app_internal.h"

#include <imgui.h>
#include <implot.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <cstdio>

namespace benchmark_visual
{
using namespace physics_arena;

void DrawResultRepeats(PhysicsArenaApp* app)
{
	NativeResultsState& state = app->results;
	const ResultViewModel& model = app->workspace.finalization.model;
	if (model.verificationMode == VerificationMode_Off)
		ImGui::TextWrapped("Verification Off: physical quality not checked");
	const std::string_view name = ResultViewTextView(&model, model.engines[state.selectedEngineIndex].provenanceLabel);
	std::array<char, 256> preview = {};
	std::snprintf(preview.data(), preview.size(), "%.*s", static_cast<int>(name.size()), name.data());
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.65f);
	if (ImGui::BeginCombo("##repeat_engine", preview.data()))
	{
		for (std::uint32_t index = 0; index < model.engineCount; ++index)
		{
			const std::string_view label = ResultViewTextView(&model, model.engines[index].provenanceLabel);
			std::snprintf(preview.data(), preview.size(), "%.*s", static_cast<int>(label.size()), label.data());
			if (ImGui::Selectable(preview.data(), index == state.selectedEngineIndex))
				SelectAnalysisResult(app, index, state.selectedThreadIndex, state.selectedRepeatIndex);
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1);
	std::snprintf(preview.data(), preview.size(), "%u thread%s", model.threadCounts[state.selectedThreadIndex], model.threadCounts[state.selectedThreadIndex] == 1 ? "" : "s");
	if (ImGui::BeginCombo("##repeat_threads", preview.data()))
	{
		for (std::uint32_t index = 0; index < model.threadCount; ++index)
		{
			std::snprintf(preview.data(), preview.size(), "%u thread%s", model.threadCounts[index], model.threadCounts[index] == 1 ? "" : "s");
			if (ImGui::Selectable(preview.data(), index == state.selectedThreadIndex))
				SelectAnalysisResult(app, state.selectedEngineIndex, index, state.selectedRepeatIndex);
		}
		ImGui::EndCombo();
	}
	if (state.repeatStatus != ArenaStatus_Ok)
	{
		DrawStatusError(state.repeatError);
		return;
	}
	const ResultRepeatProjection& projection = state.repeats;
	const int timed = projection.measurementMode == ResultMeasurementMode_Timed;
	const int rayFrame = ResultViewTextView(&model, model.workUnitId) == "ray_frame";
	const int query = rayFrame != 0 || ResultViewTextView(&model, model.workUnitId) == "query_batch";
	const std::string_view primaryUnit = ResultViewTextView(&model, model.primaryMetricUnit);
	const char* rateUnit = rayFrame != 0 ? "frames/s" : query != 0 ? "batches/s" : "steps/s";
	const std::array<const char*, 4> headings = {rayFrame != 0 ? "Coherent primary rays/s" : query != 0 ? "Query throughput" : "Step time",
	    rayFrame != 0 ? "Coherent primary frame time" : query != 0 ? "Mean batch time" : "Mean step time", rayFrame != 0 ? "Primary frame rate" : query != 0 ? "Batch rate" : "Step rate", rayFrame != 0 ? "Primary query time" : "Workload time"};
	std::array<double, 4> maxima = {};
	for (std::uint32_t index = 0; index < projection.rowCount; ++index)
	{
		const ResultRepeatRow& row = projection.rows[index];
		const std::array<double, 4> values = {row.primaryValue, row.meanWorkUnitMilliseconds,
		    row.workUnitsPerSecond, row.workloadElapsedMilliseconds};
		for (std::size_t column = 0; column < values.size(); ++column)
			maxima[column] = (std::max)(maxima[column], values[column]);
	}
	const std::array<NativeValueFormat, 4> formats = {
	    SelectNativeValueFormat(query != 0 ? NativeValueDomain_Rate : NativeValueDomain_Milliseconds, maxima[0]),
	    SelectNativeValueFormat(NativeValueDomain_Milliseconds, maxima[1]),
	    SelectNativeValueFormat(NativeValueDomain_Rate, maxima[2]),
	    SelectNativeValueFormat(NativeValueDomain_Milliseconds, maxima[3])};
	const std::array<std::string_view, 4> rawUnits = {primaryUnit, "ms", rateUnit, "ms"};
	if (timed != 0)
	{
		int metric = static_cast<int>(state.repeatMetricIndex);
		ImGui::SetNextItemWidth(240 * app->platform.dpiScale);
		const char* choices = rayFrame != 0 ? "Coherent primary frame time\0Coherent primary rays/s\0Primary frames per second\0Primary query time\0" : query != 0 ? "Mean batch time\0Query throughput\0Batches per second\0Workload time\0" :
		    "Mean step time\0Step time\0Steps per second\0Workload time\0";
		if (ImGui::Combo("Metric##repeat_metric", &metric, choices))
		{
			state.repeatMetricIndex = static_cast<std::uint32_t>(metric);
			state.repeatFitRequest = PresenceStatus_Present;
		}
		std::array<double, kRunRepeatCapacity> repeats = {};
		std::array<double, kRunRepeatCapacity> values = {};
		for (std::uint32_t index = 0; index < projection.rowCount; ++index)
		{
			const ResultRepeatRow& row = projection.rows[index];
			repeats[index] = row.repeatIndex + 1;
			values[index] = row.measurement == PresenceStatus_Absent ? std::numeric_limits<double>::quiet_NaN() : state.repeatMetricIndex == 0 ? row.meanWorkUnitMilliseconds :
			    state.repeatMetricIndex == 1 ? row.primaryValue : state.repeatMetricIndex == 2 ? row.workUnitsPerSecond : row.workloadElapsedMilliseconds;
		}
		const std::uint32_t column = state.repeatMetricIndex < 2 ? 1 - state.repeatMetricIndex : state.repeatMetricIndex;
		NativeValueFormat plotFormat = formats[column];
		const NativeValueText axis = FormatNativeUnit(plotFormat, rawUnits[column]);
		if (state.repeatFitRequest == PresenceStatus_Present)
			ImPlot::SetNextAxisToFit(ImAxis_Y1);
		ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 10);
		ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0.1f));
		if (ImPlot::BeginPlot("Repeat measurements", ImVec2(-1, (std::max)(180 * app->platform.dpiScale, ImGui::GetContentRegionAvail().y * 0.47f)), ImPlotFlags_NoLegend))
		{
			state.repeatFitRequest = PresenceStatus_Absent;
			ImPlot::SetupAxes("Repeat", axis.data());
			ImPlot::SetupAxisFormat(ImAxis_Y1, NativePlotNumber, &plotFormat);
			ImPlot::SetupAxisLimits(ImAxis_X1, 0.75, projection.rowCount + 0.25, ImGuiCond_Always);
			ImPlot::SetupAxisTicks(ImAxis_X1, repeats.data(), static_cast<int>(projection.rowCount));
			ImPlotSpec style = {};
			style.LineColor = ResultEngineColor(model.engines[state.selectedEngineIndex].colorRgb);
			style.Marker = ImPlotMarker_Circle;
			style.MarkerSize = 4;
			ImPlot::PlotLine("Saved measurement", repeats.data(), values.data(), static_cast<int>(projection.rowCount), style);
			if (state.selectedRepeatIndex < projection.rowCount)
			{
				ImPlotSpec selected = style;
				selected.MarkerSize = 6;
				selected.MarkerFillColor = ImVec4(1, 1, 1, 1);
				ImPlot::PlotScatter("Selected repeat", &repeats[state.selectedRepeatIndex], &values[state.selectedRepeatIndex], 1, selected);
			}
			if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				const ImVec2 mouse = ImGui::GetIO().MousePos;
				for (std::uint32_t index = 0; index < projection.rowCount; ++index)
				{
					const ImVec2 point = ImPlot::PlotToPixels(repeats[index], values[index]);
					if (std::abs(mouse.x - point.x) < 10 && std::abs(mouse.y - point.y) < 10)
						SelectAnalysisResult(app, state.selectedEngineIndex, state.selectedThreadIndex, index);
				}
			}
			ImPlot::EndPlot();
		}
		ImPlot::PopStyleVar();
		ImGui::PopFont();
	}
	else
		ImGui::TextWrapped("Quality-only repeats contain outcomes and observations. Timing was not recorded.");
	if (ImGui::BeginTable("repeat_table", timed != 0 ? 7 : 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
	                      ImVec2(0, (std::max)(80.0f, ImGui::GetContentRegionAvail().y))))
	{
		ImGui::TableSetupColumn("Repeat");
		ImGui::TableSetupColumn("State");
		if (timed != 0)
		{
			for (std::size_t column = 0; column < headings.size(); ++column)
			{
				std::array<char, 640> heading = {};
				if (rayFrame != 0)
				{
					constexpr std::array<const char*, 4> rayHeadings = {"Coherent primary", "Coherent primary\nframe time", "Primary frame rate", "Primary query time"};
					std::snprintf(heading.data(), heading.size(), column == 1 ? "%s (%s)" : "%s\n(%s)", rayHeadings[column], FormatNativeUnit(formats[column], rawUnits[column]).data());
				}
				else
					std::snprintf(heading.data(), heading.size(), "%s (%s)", headings[column], FormatNativeUnit(formats[column], rawUnits[column]).data());
				ImGui::TableSetupColumn(heading.data());
			}
		}
		else
			ImGui::TableSetupColumn("Invalid transforms");
		ImGui::TableSetupColumn(rayFrame != 0 ? "Primary ray frames" : query != 0 ? "Completed batches" : "Completed steps");
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();
		for (std::uint32_t index = 0; index < projection.rowCount; ++index)
		{
			const ResultRepeatRow& row = projection.rows[index];
			ImGui::TableNextRow(0, 27 * app->platform.dpiScale);
			ImGui::TableNextColumn();
			std::array<char, 32> label = {};
			std::snprintf(label.data(), label.size(), "%u", row.repeatIndex + 1);
			if (ImGui::Selectable(label.data(), state.selectedRepeatIndex == index, ImGuiSelectableFlags_SpanAllColumns))
				SelectAnalysisResult(app, state.selectedEngineIndex, state.selectedThreadIndex, index);
			ImGui::TableNextColumn();
			DrawResultTableValue(row.disposition == ResultRepeatDisposition_Skipped ? "Skipped after failed repeat" :
			    row.disposition == ResultRepeatDisposition_ExecutionFailed ? "Execution failed" :
			    row.outcome == ObservationOutcome_Failed ? "Measured failure" : "Measured");
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			{
				const ExecutionFailure* failure = FindExecutionFailure(model.executionFailures,
				    model.engines[state.selectedEngineIndex].catalogEngineIndex, model.threadCounts[state.selectedThreadIndex], row.repeatIndex);
				if (failure != nullptr)
					ImGui::SetTooltip("%s", failure->detail.data());
			}
			if (timed != 0)
			{
				const std::array<double, 4> values = {row.primaryValue, row.meanWorkUnitMilliseconds,
				    row.workUnitsPerSecond, row.workloadElapsedMilliseconds};
				for (std::size_t column = 0; column < values.size(); ++column)
				{
					ImGui::TableNextColumn();
					DrawResultTableValue(row.measurement == PresenceStatus_Present ? FormatNativeValue(values[column], formats[column]).data() : "unavailable");
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
					{
						if (row.measurement == PresenceStatus_Present)
							ImGui::SetTooltip("%s %.*s", FormatNativeRaw(values[column]).data(), static_cast<int>(rawUnits[column].size()), rawUnits[column].data());
						else
						{
							const ExecutionFailure* failure = FindExecutionFailure(model.executionFailures,
							    model.engines[state.selectedEngineIndex].catalogEngineIndex, model.threadCounts[state.selectedThreadIndex], row.repeatIndex);
							ImGui::SetTooltip("Repeat %u: measurement unavailable. %s", row.repeatIndex + 1,
							    failure != nullptr ? failure->detail.data() : "No saved measurement");
						}
					}
				}
			}
			else
			{
				ImGui::TableNextColumn();
				DrawResultTableValue(row.measurement == PresenceStatus_Present ? FormatNativeCount(row.invalidTransformCount).data() : "unavailable");
			}
			ImGui::TableNextColumn();
			DrawResultTableValue(row.measurement == PresenceStatus_Present ? FormatNativeCount(row.completedWorkUnitCount).data() : "unavailable");
		}
		ImGui::EndTable();
	}
}
}
