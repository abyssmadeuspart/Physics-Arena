#include "launcher_app_internal.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace benchmark_visual
{
using namespace physics_arena;

NativeValueFormat SummaryNumberFormat(const ResultViewModel& model, ResultMetricId metric)
{
	const ResultMetricId maximum = metric >= ResultMetric_MedianWorkUnitMilliseconds ? ResultMetric_MaximumWorkUnitMilliseconds : ResultMetric_MaximumPrimaryValue;
	double magnitude = 0;
	for (std::uint32_t index = 0; index < model.summaryRowCount; ++index)
		magnitude = (std::max)(magnitude, std::abs(ResultMetricValue(&model.summaryRows[index], maximum)));
	const NativeValueDomain domain = maximum == ResultMetric_MaximumPrimaryValue && (ResultViewTextView(&model, model.workUnitId) == "query_batch" || ResultViewTextView(&model, model.workUnitId) == "ray_frame") ?
	    NativeValueDomain_Rate : NativeValueDomain_Milliseconds;
	return SelectNativeValueFormat(domain, magnitude);
}
struct SummaryPlotView
{
	const double* threads;
	const double* values;
};

struct OutcomePlotView
{
	double x;
	double y;
};

ImPlotPoint SummaryPoint(int index, void* opaque)
{
	const SummaryPlotView* view = static_cast<const SummaryPlotView*>(opaque);
	return ImPlotPoint(view->threads[index], view->values[index]);
}

ImPlotPoint OutcomePoint(int, void* opaque)
{
	const OutcomePlotView* view = static_cast<const OutcomePlotView*>(opaque);
	return ImPlotPoint(view->x, view->y);
}

const char* SummaryOrderLabel(NativeSummaryOrder order)
{
	switch (order)
	{
	case NativeSummaryOrder_FastestFirst:
		return "Fastest first";
	case NativeSummaryOrder_SlowestFirst:
		return "Slowest first";
	case NativeSummaryOrder_EngineName:
		return "Engine name";
	}
	return "Fastest first";
}

void FormatMetricLabel(const ResultViewModel& model, const ResultMetricDescriptor& metric, char* output,
                       std::size_t capacity)
{
	const std::string_view domain = ResultViewTextView(&model, model.workUnitId) == "ray_frame" ?
	    (metric.id >= ResultMetric_MedianWorkUnitMilliseconds ? "Coherent primary frame time" : "Coherent primary rays/s") :
	    metric.id >= ResultMetric_MedianWorkUnitMilliseconds ? "Query batch time" :
	    ResultViewTextView(&model, model.workUnitId) == "query_batch" ? "Query throughput" : "Step time";
	const std::string_view statistic = ResultViewTextView(&model, metric.label);
	std::snprintf(output, capacity, "%.*s - %.*s", static_cast<int>(domain.size()), domain.data(),
	              static_cast<int>(statistic.size()), statistic.data());
}

void DrawResultFilters(PhysicsArenaApp* app)
{
	ResultViewModel& model = app->workspace.finalization.model;
	NativeResultsState& state = app->results;
	std::array<char, kEngineProvenanceLabelCapacity> engineValue = {};
	const std::uint32_t availableMask = AvailableEngineMask(model);
	const std::uint32_t selectedMask = state.summarySelectedEngineMask & availableMask;
	const std::uint32_t selectedCount = CountSelectedEngines(selectedMask);
	if (selectedMask == availableMask)
		std::snprintf(engineValue.data(), engineValue.size(), "All engines");
	else if (selectedCount == 0)
		std::snprintf(engineValue.data(), engineValue.size(), "No engines");
	else if (selectedCount == 1)
	{
		const std::uint32_t selectedEngine = SingleSelectedEngine(selectedMask);
		const std::string_view name = ResultViewTextView(&model, model.engines[selectedEngine].provenanceLabel);
		std::snprintf(engineValue.data(), engineValue.size(), "%.*s", static_cast<int>(name.size()), name.data());
	}
	else
		std::snprintf(engineValue.data(), engineValue.size(), "%u engines", selectedCount);
	std::array<char, kEngineProvenanceLabelCapacity + 16> enginePreview = {};
	std::snprintf(enginePreview.data(), enginePreview.size(), "Engine: %s", engineValue.data());
	const int compareMode = state.summaryChartMode == NativeSummaryChartMode_Compare;
	const int compact = app->platform.width / app->platform.dpiScale < 1000;
	const int filterCount = compareMode != 0 ? 4 : 2;
	const int columnCount = filterCount + (compact != 0 ? 0 : 2);
	const char* tableId = compareMode != 0 ? "summary_compare_controls" : "summary_scaling_controls";
	if (ImGui::BeginTable(tableId, columnCount, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
	{
		ImGui::TableSetupColumn("##summary_engine_control", ImGuiTableColumnFlags_WidthStretch, 1.45f);
		if (compareMode != 0)
		{
			ImGui::TableSetupColumn("##summary_thread_control", ImGuiTableColumnFlags_WidthStretch, 0.75f);
			ImGui::TableSetupColumn("##summary_order_control", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		}
		ImGui::TableSetupColumn("##summary_metric_control", ImGuiTableColumnFlags_WidthStretch, 1.8f);
		if (compact == 0)
		{
			ImGui::TableSetupColumn("View", ImGuiTableColumnFlags_WidthFixed, 110 * app->platform.dpiScale);
			ImGui::TableSetupColumn("Fit", ImGuiTableColumnFlags_WidthFixed, 32 * app->platform.dpiScale);
		}
		ImGui::TableNextRow(0, ImGui::GetFrameHeight());
		ImGui::TableSetColumnIndex(0);
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##summary_engine_filter", enginePreview.data(), ImGuiComboFlags_HeightLargest))
		{
			if (ImGui::Selectable("All engines", selectedMask == availableMask, ImGuiSelectableFlags_DontClosePopups) &&
			    selectedMask != availableMask)
				SetSummaryEngineSelection(app, availableMask);
			if (ImGui::Selectable("Clear selection", selectedMask == 0, ImGuiSelectableFlags_DontClosePopups) &&
			    selectedMask != 0)
				SetSummaryEngineSelection(app, 0);
			for (std::uint32_t index = 0; index < model.engineCount; ++index)
			{
				const std::string_view name = ResultViewTextView(&model, model.engines[index].provenanceLabel);
				std::array<char, kEngineProvenanceLabelCapacity> label = {};
				std::snprintf(label.data(), label.size(), "%.*s", static_cast<int>(name.size()), name.data());
				const std::uint32_t engineMask = 1u << index;
				ImGui::PushID(static_cast<int>(index));
				if (ImGui::Selectable(label.data(), (state.summarySelectedEngineMask & engineMask) != 0,
				                      ImGuiSelectableFlags_DontClosePopups))
					SetSummaryEngineSelection(app, state.summarySelectedEngineMask ^ engineMask);
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		if (compareMode != 0)
		{
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
			std::array<char, 48> threadPreview = {};
			std::snprintf(threadPreview.data(), threadPreview.size(), "Threads: %u",
			              model.threadCounts[state.summaryThreadIndex]);
			if (ImGui::BeginCombo("##summary_thread_count", threadPreview.data()))
			{
				for (std::uint32_t index = 0; index < model.threadCount; ++index)
				{
					std::array<char, 48> label = {};
					std::snprintf(label.data(), label.size(), "%u", model.threadCounts[index]);
					if (ImGui::Selectable(label.data(), state.summaryThreadIndex == index))
					{
						state.summaryThreadIndex = index;
						state.summaryFitRequest = PresenceStatus_Present;
						RebuildSummaryRows(app);
					}
				}
				ImGui::EndCombo();
			}
			ImGui::TableSetColumnIndex(2);
			ImGui::SetNextItemWidth(-1.0f);
			std::array<char, 64> orderPreview = {};
			std::snprintf(orderPreview.data(), orderPreview.size(), "Order: %s", SummaryOrderLabel(state.summaryOrder));
			if (ImGui::BeginCombo("##summary_order", orderPreview.data()))
			{
				for (const NativeSummaryOrder order :
				     {NativeSummaryOrder_FastestFirst, NativeSummaryOrder_SlowestFirst, NativeSummaryOrder_EngineName})
				{
					if (ImGui::Selectable(SummaryOrderLabel(order), state.summaryOrder == order))
					{
						state.summaryOrder = order;
						state.summaryFitRequest = PresenceStatus_Present;
						RebuildSummaryRows(app);
					}
				}
				ImGui::EndCombo();
			}
		}
		const int metricColumn = compareMode != 0 ? 3 : 1;
		ImGui::TableSetColumnIndex(metricColumn);
		ImGui::SetNextItemWidth(-1.0f);
		const ResultMetricDescriptor& metric = model.metrics[state.metricIndex];
		std::array<char, 192> metricValue = {};
		FormatMetricLabel(model, metric, metricValue.data(), metricValue.size());
		std::array<char, 208> metricPreview = {};
		std::snprintf(metricPreview.data(), metricPreview.size(), "Metric: %s", metricValue.data());
		const int metricOpen = ImGui::BeginCombo("##summary_metric", metricPreview.data());
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			ImGui::SetTooltip("%s", metricPreview.data());
		if (metricOpen != 0)
		{
			for (std::uint32_t index = 0; index < model.metricCount; ++index)
			{
				std::array<char, 192> label = {};
				FormatMetricLabel(model, model.metrics[index], label.data(), label.size());
				ImGui::PushID(static_cast<int>(model.metrics[index].id));
				if (ImGui::Selectable(label.data(), state.metricIndex == index))
				{
					state.metricIndex = index;
					state.summaryFitRequest = PresenceStatus_Present;
					RebuildSummaryRows(app);
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		if (compact == 0)
		{
			ImGui::TableSetColumnIndex(filterCount);
			int layout = static_cast<int>(state.summaryLayout);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::Combo("##summary_layout", &layout, "Chart + table\0Chart only\0Table only\0"))
				state.summaryLayout = static_cast<NativeSummaryLayout>(layout);
			ImGui::TableSetColumnIndex(filterCount + 1);
			if (ImGui::Button("Fit##summary_plot", ImVec2(-1, 0)))
				state.summaryFitRequest = PresenceStatus_Present;
		}
		ImGui::EndTable();
	}
	if (compact != 0)
	{
		int layout = static_cast<int>(state.summaryLayout);
		ImGui::SetNextItemWidth(140 * app->platform.dpiScale);
		if (ImGui::Combo("##summary_layout", &layout, "Chart + table\0Chart only\0Table only\0"))
			state.summaryLayout = static_cast<NativeSummaryLayout>(layout);
		ImGui::SameLine();
		if (ImGui::Button("Fit##summary_plot"))
			state.summaryFitRequest = PresenceStatus_Present;
	}
}

void DrawSummaryPlot(PhysicsArenaApp* app, float height)
{
	const ResultViewModel& model = app->workspace.finalization.model;
	NativeResultsState& state = app->results;
	const ResultMetricDescriptor& metric = model.metrics[state.metricIndex];
	NativeValueFormat format = SummaryNumberFormat(model, metric.id);
	const NativeValueText unit = FormatNativeUnit(format, ResultViewTextView(&model, metric.unitLabel));
	const char* domain = ResultViewTextView(&model, model.workUnitId) == "ray_frame" ?
	    (metric.id >= ResultMetric_MedianWorkUnitMilliseconds ? "Coherent primary frame time" : "Coherent primary rays/s") :
	    metric.id >= ResultMetric_MedianWorkUnitMilliseconds ? "Query batch time" :
	    ResultViewTextView(&model, model.workUnitId) == "query_batch" ? "Query throughput" : "Step time";
	std::array<char, 128> axisLabel = {};
	std::snprintf(axisLabel.data(), axisLabel.size(), "%s (%s)", domain, unit.data());
	std::array<double, kEngineCapacity> comparePositions = {};
	std::array<double, kEngineCapacity> compareValues = {};
	std::array<std::uint32_t, kEngineCapacity> compareRowIndexes = {};
	std::array<std::array<char, kEngineProvenanceLabelCapacity>, kEngineCapacity> compareLabelStorage = {};
	std::array<const char*, kEngineCapacity> compareLabels = {};
	std::array<double, kEngineCapacity> compareTickPositions = {};
	std::array<const char*, kEngineCapacity> compareTickLabels = {};
	std::uint32_t compareCount = 0;
	std::uint32_t compareTickCount = 0;
	if (state.summaryChartMode == NativeSummaryChartMode_Compare)
	{
		for (std::uint32_t rowOrdinal = 0; rowOrdinal < state.summaryRowCount; ++rowOrdinal)
		{
			const std::uint32_t rowIndex = state.summaryPlotRowIndexes[rowOrdinal];
			const ResultSummaryViewRow& row = model.summaryRows[rowIndex];
			const std::string_view name = ResultViewTextView(&model, model.engines[row.engineOrdinal].provenanceLabel);
			std::snprintf(compareLabelStorage[compareCount].data(), compareLabelStorage[compareCount].size(), "   %.*s",
			              static_cast<int>(name.size()), name.data());
			compareLabels[compareCount] = compareLabelStorage[compareCount].data();
			compareValues[compareCount] = row.repeatCount == 0 ? std::numeric_limits<double>::quiet_NaN() : ResultMetricValue(&row, metric.id);
			compareRowIndexes[compareCount] = rowIndex;
			{
				comparePositions[compareCount] = static_cast<double>(compareTickCount);
				compareTickPositions[compareTickCount] = comparePositions[compareCount];
				compareTickLabels[compareTickCount] = compareLabels[compareCount];
				compareTickCount += 1;
			}
			compareCount += 1;
		}
	}
	std::array<char, 128> title = {};
	if (state.summaryChartMode == NativeSummaryChartMode_Compare)
		std::snprintf(title.data(), title.size(), "Engine comparison at %u thread%s##summary_plot",
		              model.threadCounts[state.summaryThreadIndex], model.threadCounts[state.summaryThreadIndex] == 1 ? "" : "s");
	else
		std::snprintf(title.data(), title.size(), "Scaling by thread count##summary_plot");
	ImGuiStorage* layout = ImGui::GetStateStorage();
	const ImGuiID widthId = ImGui::GetID("summary_plot_width");
	const ImGuiID scaleId = ImGui::GetID("summary_plot_scale");
	const float width = ImGui::GetContentRegionAvail().x;
	if (layout->GetFloat(widthId) != width || layout->GetFloat(scaleId) != app->platform.dpiScale)
	{
		layout->SetFloat(widthId, width);
		layout->SetFloat(scaleId, app->platform.dpiScale);
		state.summaryFitRequest = PresenceStatus_Present;
	}
	if (state.summaryFitRequest == PresenceStatus_Present)
		ImPlot::SetNextAxesToFit();
	const ImVec2 frame = ImGui::GetCursorScreenPos();
	if (!ImPlot::BeginPlot(title.data(), ImVec2(-1.0f, height), ImPlotFlags_Crosshairs |
	    (state.summaryChartMode == NativeSummaryChartMode_Compare ? ImPlotFlags_NoLegend : 0)))
		return;
	state.summaryFitRequest = PresenceStatus_Absent;
	if (state.summaryChartMode != NativeSummaryChartMode_Compare)
		ImPlot::SetupLegend(ImPlotLocation_East, ImPlotLegendFlags_Outside);
	if (state.summaryChartMode == NativeSummaryChartMode_Compare)
	{
		ImPlot::SetupAxes(axisLabel.data(), nullptr, ImPlotAxisFlags_NoInitialFit,
		                  ImPlotAxisFlags_NoInitialFit | ImPlotAxisFlags_Invert | ImPlotAxisFlags_NoHighlight);
		ImPlot::SetupAxisTicks(ImAxis_Y1, compareTickPositions.data(), static_cast<int>(compareTickCount),
		                       compareTickLabels.data());
		ImPlot::SetupAxisFormat(ImAxis_X1, NativePlotNumber, &format);
		ImPlot::SetupAxisLimits(ImAxis_Y1, -0.5, (std::max)(0.5, static_cast<double>(compareTickCount) - 0.5), ImPlotCond_Always);
		ImPlot::SetupFinish();
		float clearance = 0;
		for (std::uint32_t index = 0; index < compareCount; ++index)
		{
			if ((state.summaryHiddenEngineMask & (1u << model.summaryRows[compareRowIndexes[index]].engineOrdinal)) == 0)
				clearance = (std::max)(clearance, ImGui::CalcTextSize(FormatNativeValue(compareValues[index], format).data()).x + 18 * app->platform.dpiScale);
		}
		const float plotWidth = ImPlot::GetPlotSize().x;
		ImPlot::GetStyle().FitPadding.x = 2 * clearance / (std::max)(1.0f, plotWidth - 2 * clearance);
		for (std::uint32_t index = 0; index < compareCount; ++index)
		{
			const ResultSummaryViewRow& row = model.summaryRows[compareRowIndexes[index]];
			const ResultEngineView& engine = model.engines[row.engineOrdinal];
			const std::uint32_t visibilityMask = 1u << row.engineOrdinal;
			const ImVec4 color = ResultEngineColor(engine.colorRgb);
			const int selected = state.selectedResultPresence == PresenceStatus_Present && state.selectedEngineIndex == row.engineOrdinal &&
			    model.threadCounts[state.selectedThreadIndex] == row.threadCount;
			ImPlotSpec style = {};
			style.LineColor = color;
			style.LineWeight = selected != 0 ? 3 : 2;
			const double xs[] = {0, compareValues[index]};
			const double ys[] = {comparePositions[index], comparePositions[index]};
			ImPlot::HideNextItem((state.summaryHiddenEngineMask & visibilityMask) != 0, ImPlotCond_Always);
			ImPlot::PlotLine(compareLabels[index], xs, ys, 2, style);
			const float scale = app->platform.dpiScale;
			const ImVec2 point = ImPlot::PlotToPixels(compareValues[index], comparePositions[index]);
			const ImVec2 swatch(frame.x + 10 * scale, point.y);
			ImDrawList* draw = ImGui::GetWindowDrawList();
			draw->AddRectFilled(ImVec2(swatch.x - 3 * scale, swatch.y - 3 * scale), ImVec2(swatch.x + 3 * scale, swatch.y + 3 * scale),
			    ImGui::GetColorU32(ImGui::ColorConvertFloat4ToU32(color), (state.summaryHiddenEngineMask & visibilityMask) == 0 ? 1.0f : 0.3f));
			if (ImGui::IsMouseHoveringRect(ImVec2(swatch.x - 7 * scale, swatch.y - 7 * scale), ImVec2(swatch.x + 7 * scale, swatch.y + 7 * scale)))
			{
				ImGui::SetTooltip("%s: toggle trace", compareLabels[index]);
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					state.summaryHiddenEngineMask ^= visibilityMask;
					state.summaryFitRequest = PresenceStatus_Present;
				}
			}
			if ((state.summaryHiddenEngineMask & visibilityMask) == 0 && row.repeatCount != 0)
			{
				ImPlotSpec marker = style;
				marker.Marker = ImPlotMarker_Circle;
				marker.MarkerSize = selected != 0 ? 5 : 3;
				marker.MarkerFillColor = color;
				marker.MarkerLineColor = selected != 0 ? ImVec4(0.94f, 0.95f, 0.96f, 1) : color;
				ImPlot::PlotScatter(compareLabels[index], &compareValues[index], &comparePositions[index], 1, marker);
				const NativeValueText value = FormatNativeValue(compareValues[index], format);
				const ImVec2 textSize = ImGui::CalcTextSize(value.data());
				const float x = point.x + 9 * scale;
				ImPlot::PushPlotClipRect();
				draw->AddText(ImVec2(x, point.y - textSize.y * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), value.data());
				ImPlot::PopPlotClipRect();
			}
			if ((state.summaryHiddenEngineMask & visibilityMask) == 0 && row.repeatCount != 0 && row.outcome == ObservationOutcome_Failed)
			{
				OutcomePlotView failed = {compareValues[index], comparePositions[index]};
				std::array<char, 48> failedLabel = {};
				std::snprintf(failedLabel.data(), failedLabel.size(), "##summary_failed_%u_%u", row.engineOrdinal,
				              row.threadCount);
				ImPlotSpec failedStyle = {};
				failedStyle.LineColor = ImVec4(0.95f, 0.30f, 0.24f, 1.0f);
				failedStyle.LineWeight = 2.0f;
				failedStyle.Marker = ImPlotMarker_Cross;
				failedStyle.MarkerSize = 11.0f;
				failedStyle.MarkerLineColor = failedStyle.LineColor;
				failedStyle.Flags = ImPlotItemFlags_NoLegend;
				ImPlot::PlotLineG(failedLabel.data(), OutcomePoint, &failed, 1, failedStyle);
			}
		}
	}
	else
	{
		std::array<double, kThreadCountCapacity> threadTicks = {};
		for (std::uint32_t index = 0; index < model.threadCount; ++index)
			threadTicks[index] = static_cast<double>(model.threadCounts[index]);
		ImPlot::SetupAxes("Thread count", axisLabel.data(), ImPlotAxisFlags_NoInitialFit, ImPlotAxisFlags_NoInitialFit);
		ImPlot::SetupAxisFormat(ImAxis_Y1, NativePlotNumber, &format);
		ImPlot::SetupAxisTicks(ImAxis_X1, threadTicks.data(), static_cast<int>(model.threadCount));
		for (std::uint32_t engineOrdinal = 0; engineOrdinal < model.engineCount; ++engineOrdinal)
		{
			if ((state.summarySelectedEngineMask & (1u << engineOrdinal)) == 0)
				continue;
			std::array<double, kThreadCountCapacity> threads = {};
			std::array<double, kThreadCountCapacity> values = {};
			std::array<OutcomePlotView, kThreadCountCapacity> failures = {};
			std::uint32_t count = 0;
			std::uint32_t failureCount = 0;
			for (std::uint32_t rowIndex = 0; rowIndex < state.summaryRowCount; ++rowIndex)
			{
				const ResultSummaryViewRow& row = model.summaryRows[state.summaryPlotRowIndexes[rowIndex]];
				if (row.engineOrdinal != engineOrdinal || count == threads.size())
					continue;
				threads[count] = row.threadCount;
				values[count] = row.repeatCount == 0 || row.outcome == ObservationOutcome_Failed
				    ? std::numeric_limits<double>::quiet_NaN() : ResultMetricValue(&row, metric.id);
				if (row.outcome == ObservationOutcome_Failed && row.repeatCount != 0)
					failures[failureCount++] = {threads[count], ResultMetricValue(&row, metric.id)};
				count += 1;
			}
			if (count == 0)
				continue;
			const ResultEngineView& engine = model.engines[engineOrdinal];
			const std::uint32_t visibilityMask = 1u << engineOrdinal;
			const std::string_view name = ResultViewTextView(&model, engine.provenanceLabel);
			std::array<char, kEngineProvenanceLabelCapacity> label = {};
			std::snprintf(label.data(), label.size(), "%.*s", static_cast<int>(name.size()), name.data());
			ImPlotSpec style = {};
			style.LineColor = ResultEngineColor(engine.colorRgb);
			style.LineWeight = 2.0f;
			style.Marker = ImPlotMarker_Auto;
			SummaryPlotView points = {threads.data(), values.data()};
			ImPlot::HideNextItem((state.summaryHiddenEngineMask & visibilityMask) != 0, ImPlotCond_Always);
			ImPlot::PlotLineG(label.data(), SummaryPoint, &points, static_cast<int>(count), style);
			if ((state.summaryHiddenEngineMask & visibilityMask) == 0)
			{
				ImPlotSpec failedStyle = {};
				failedStyle.LineColor = ImVec4(0.95f, 0.30f, 0.24f, 1.0f);
				failedStyle.LineWeight = 2.0f;
				failedStyle.Marker = ImPlotMarker_Cross;
				failedStyle.MarkerSize = 11.0f;
				failedStyle.MarkerLineColor = failedStyle.LineColor;
				failedStyle.Flags = ImPlotItemFlags_NoLegend;
				for (std::uint32_t failed = 0; failed < failureCount; ++failed)
				{
					std::array<char, 48> failedLabel = {};
					std::snprintf(failedLabel.data(), failedLabel.size(), "##scaling_failed_%u_%u", engineOrdinal,
					              failed);
					ImPlot::PlotLineG(failedLabel.data(), OutcomePoint, &failures[failed], 1, failedStyle);
				}
			}
			if (ImPlot::IsLegendEntryHovered(label.data()) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				state.summaryHiddenEngineMask ^= visibilityMask;
				state.summaryFitRequest = PresenceStatus_Present;
			}
		}
	}
	const ResultSummaryViewRow* hoveredRow = nullptr;
	if (ImPlot::IsPlotHovered())
	{
		if (state.summaryChartMode == NativeSummaryChartMode_Compare)
		{
			const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
			for (std::uint32_t index = 0; index < compareCount; ++index)
				if ((state.summaryHiddenEngineMask &
				     (1u << model.summaryRows[compareRowIndexes[index]].engineOrdinal)) == 0 &&
				    mouse.x >= 0.0 && mouse.x <= compareValues[index] && mouse.y >= comparePositions[index] - 0.5 &&
				    mouse.y <= comparePositions[index] + 0.5)
					hoveredRow = &model.summaryRows[compareRowIndexes[index]];
		}
		else
		{
			const ImVec2 mouse = ImGui::GetIO().MousePos;
			float nearestDistanceSquared = 64.0f;
			for (std::uint32_t rowOrdinal = 0; rowOrdinal < state.summaryRowCount; ++rowOrdinal)
			{
				const ResultSummaryViewRow& row = model.summaryRows[state.summaryRowIndexes[rowOrdinal]];
				if ((state.summaryHiddenEngineMask & (1u << row.engineOrdinal)) != 0)
					continue;
				const ImVec2 point =
				    ImPlot::PlotToPixels(static_cast<double>(row.threadCount), ResultMetricValue(&row, metric.id));
				const float dx = point.x - mouse.x;
				const float dy = point.y - mouse.y;
				const float distanceSquared = dx * dx + dy * dy;
				if (distanceSquared <= nearestDistanceSquared)
				{
					nearestDistanceSquared = distanceSquared;
					hoveredRow = &row;
				}
			}
		}
	}
	if (hoveredRow != nullptr)
	{
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			std::uint32_t thread = 0;
			while (model.threadCounts[thread] != hoveredRow->threadCount)
				++thread;
			SelectAnalysisResult(app, hoveredRow->engineOrdinal, thread, kRunRepeatCapacity);
		}
		const std::string_view engine =
		    ResultViewTextView(&model, model.engines[hoveredRow->engineOrdinal].provenanceLabel);
		const std::string_view unit = ResultViewTextView(&model, metric.unitLabel);
		std::array<char, 192> metricLabel = {};
		FormatMetricLabel(model, metric, metricLabel.data(), metricLabel.size());
		const std::string_view outcome = hoveredRow->outcome == ObservationOutcome_Unknown
		                                     ? std::string_view("not recorded")
		                                     : ObservationOutcomeText(hoveredRow->outcome);
		ImGui::SetTooltip("%.*s\n%u thread%s\n%.*s: %s %.*s\nOutcome: %.*s", static_cast<int>(engine.size()),
		                  engine.data(), hoveredRow->threadCount, hoveredRow->threadCount == 1 ? "" : "s", static_cast<int>(std::strlen(metricLabel.data())),
		                  metricLabel.data(), FormatNativeRaw(ResultMetricValue(hoveredRow, metric.id)).data(), static_cast<int>(unit.size()),
		                  unit.data(), static_cast<int>(outcome.size()), outcome.data());
	}
	ImPlot::EndPlot();
}

void DrawSummaryTable(PhysicsArenaApp* app, float height)
{
	const ResultViewModel& model = app->workspace.finalization.model;
	const ResultMetricDescriptor& selectedMetric = model.metrics[app->results.metricIndex];
	const ResultMetricId family = selectedMetric.id >= ResultMetric_MedianWorkUnitMilliseconds
	                                  ? ResultMetric_MedianWorkUnitMilliseconds
	                                  : ResultMetric_MedianPrimaryValue;
	const std::string_view unit = ResultViewTextView(&model, selectedMetric.unitLabel);
	const NativeValueFormat format = SummaryNumberFormat(model, selectedMetric.id);
	const NativeValueText displayedUnit = FormatNativeUnit(format, unit);
	std::array<std::array<char, 192>, 3> headers = {};
	const std::array<const char*, 3> statistics = {"Median", "Minimum", "Maximum"};
	for (std::uint32_t index = 0; index < headers.size(); ++index)
		std::snprintf(headers[index].data(), headers[index].size(), "%s (%s)", statistics[index], displayedUnit.data());
	if (!ImGui::BeginTable("summary_table", 6,
	                       ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
	                           ImGuiTableFlags_Sortable,
	                       ImVec2(0.0f, height)))
		return;
	ImGui::TableSetupColumn("Engine", ImGuiTableColumnFlags_DefaultSort, 0.0f, 0);
	ImGui::TableSetupColumn("Threads", ImGuiTableColumnFlags_PreferSortAscending, 0.0f, 1);
	ImGui::TableSetupColumn(headers[0].data(), ImGuiTableColumnFlags_PreferSortAscending, 0.0f, 2);
	ImGui::TableSetupColumn(headers[1].data(), ImGuiTableColumnFlags_PreferSortAscending, 0.0f, 3);
	ImGui::TableSetupColumn(headers[2].data(), ImGuiTableColumnFlags_PreferSortAscending, 0.0f, 4);
	ImGui::TableSetupColumn("Outcome", ImGuiTableColumnFlags_PreferSortAscending, 0.0f, 5);
	ImGui::TableSetupScrollFreeze(0, 1);
	ImGui::TableHeadersRow();
	ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
	if (specs != nullptr && specs->SpecsDirty && specs->SpecsCount != 0)
	{
		app->results.sortColumn = static_cast<int>(specs->Specs[0].ColumnUserID);
		app->results.sortDirection = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending ? -1 : 1;
		specs->SpecsDirty = false;
		RebuildSummaryRows(app);
	}
	for (std::uint32_t ordinal = 0; ordinal < app->results.summaryRowCount; ++ordinal)
	{
		const ResultSummaryViewRow& row = model.summaryRows[app->results.summaryRowIndexes[ordinal]];
		const ResultEngineView& resultEngine = model.engines[row.engineOrdinal];
		const std::string_view engine = ResultViewTextView(&model, resultEngine.provenanceLabel);
		const ImVec4 color = ResultEngineColor(resultEngine.colorRgb);
		ImGui::TableNextRow(0, 27 * app->platform.dpiScale);
		ImGui::TableNextColumn();
		ImGui::PushID(static_cast<int>(app->results.summaryRowIndexes[ordinal]));
		const ImVec2 swatch = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(swatch.x, swatch.y + 4 * app->platform.dpiScale),
		    ImVec2(swatch.x + 6 * app->platform.dpiScale, swatch.y + 10 * app->platform.dpiScale), ImGui::GetColorU32(color));
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 11 * app->platform.dpiScale);
		std::array<char, kEngineProvenanceLabelCapacity> rowLabel = {};
		std::snprintf(rowLabel.data(), rowLabel.size(), "%.*s", static_cast<int>(engine.size()), engine.data());
		if (ImGui::Selectable(rowLabel.data(), app->results.selectedEngineIndex == row.engineOrdinal &&
		                      model.threadCounts[app->results.selectedThreadIndex] == row.threadCount,
		                      ImGuiSelectableFlags_SpanAllColumns))
		{
			std::uint32_t thread = 0;
			while (model.threadCounts[thread] != row.threadCount)
				++thread;
			SelectAnalysisResult(app, row.engineOrdinal, thread, kRunRepeatCapacity);
		}
		ImGui::PopID();
		for (std::uint32_t column = 0; column < 4; ++column)
		{
			ImGui::TableNextColumn();
			NativeValueText number = {};
			if (column == 0)
				number = FormatNativeCount(row.threadCount);
			else
				number = FormatNativeValue(ResultMetricValue(&row, static_cast<ResultMetricId>(family + column - 1)), format);
			DrawResultTableValue(column != 0 && row.repeatCount == 0 ? "unavailable" : number.data());
			if (column != 0 && row.repeatCount != 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
				ImGui::SetTooltip("%s %.*s", FormatNativeRaw(ResultMetricValue(&row, static_cast<ResultMetricId>(family + column - 1))).data(),
				    static_cast<int>(unit.size()), unit.data());
		}
		ImGui::TableNextColumn();
		const std::string_view outcome = row.outcome == ObservationOutcome_Unknown
		                                     ? std::string_view("not recorded")
		                                     : ObservationOutcomeText(row.outcome);
		ImGui::TextUnformatted(outcome.data(), outcome.data() + outcome.size());
	}
	ImGui::EndTable();
}

void DrawSummaryAnalysis(PhysicsArenaApp* app, NativeSummaryChartMode chartMode)
{
	if (app->results.summaryChartMode != chartMode)
	{
		app->results.summaryChartMode = chartMode;
		app->results.summaryFitRequest = PresenceStatus_Present;
		RebuildSummaryRows(app);
	}
	DrawResultFilters(app);
	const ResultViewModel& model = app->workspace.finalization.model;
	NativeResultsState& state = app->results;
	if ((state.summarySelectedEngineMask & AvailableEngineMask(model)) == 0)
	{
		ImGui::TextDisabled("No engines selected");
		return;
	}
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 10);
	const float availableHeight = ImGui::GetContentRegionAvail().y;
	const float minimumPlotHeight = app->platform.height / app->platform.dpiScale <= 600 ? 150 : 280;
	const float contentHeight = chartMode == NativeSummaryChartMode_Compare ?
	    state.summaryRowCount * ImGui::GetTextLineHeightWithSpacing() + 64 * app->platform.dpiScale : 0;
	const float minimumContentHeight = (std::max)(150 * app->platform.dpiScale, contentHeight);
	const float plotHeight = state.summaryLayout == NativeSummaryLayout_ChartOnly ? (std::max)(minimumContentHeight, availableHeight) :
	    (std::max)(minimumContentHeight, (std::min)((std::max)(minimumPlotHeight * app->platform.dpiScale, availableHeight * 0.55f),
	        availableHeight - 140 * app->platform.dpiScale));
	if (state.summaryLayout != NativeSummaryLayout_TableOnly)
	{
		ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding,
		    chartMode == NativeSummaryChartMode_Compare ? ImVec2(0, 0) : ImVec2(0.1f, 0.1f));
		DrawSummaryPlot(app, plotHeight);
		ImPlot::PopStyleVar();
	}
	ImGui::PopFont();
	if (state.summaryLayout != NativeSummaryLayout_ChartOnly)
		DrawSummaryTable(app, (std::max)(70 * app->platform.dpiScale, ImGui::GetContentRegionAvail().y));
}
}
