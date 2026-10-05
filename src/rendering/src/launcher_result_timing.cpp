#include "launcher_app_internal.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace benchmark_visual
{
using namespace physics_arena;
struct TimingPlotView
{
	const std::uint32_t* steps;
	const double* values;
};

ImPlotPoint TimingPoint(int index, void* opaque)
{
	const TimingPlotView* view = static_cast<const TimingPlotView*>(opaque);
	return ImPlotPoint(static_cast<double>(view->steps[index]), view->values[index]);
}

void DrawTimingView(PhysicsArenaApp* app)
{
	ResultViewModel& model = app->workspace.finalization.model;
	NativeResultsState& state = app->results;
	std::array<char, kEngineProvenanceLabelCapacity> enginePreview = {};
	const std::uint32_t availableMask = AvailableEngineMask(model);
	std::uint32_t selectedMask = state.timingSelectedEngineMask & availableMask;
	const std::uint32_t selectedCount = CountSelectedEngines(selectedMask);
	if (selectedMask == availableMask)
		std::snprintf(enginePreview.data(), enginePreview.size(), "All engines");
	else if (selectedCount == 0)
		std::snprintf(enginePreview.data(), enginePreview.size(), "No engines");
	else if (selectedCount == 1)
	{
		const std::uint32_t selectedEngine = SingleSelectedEngine(selectedMask);
		const std::string_view name = ResultViewTextView(&model, model.engines[selectedEngine].provenanceLabel);
		std::snprintf(enginePreview.data(), enginePreview.size(), "%.*s", static_cast<int>(name.size()), name.data());
	}
	else
		std::snprintf(enginePreview.data(), enginePreview.size(), "%u engines", selectedCount);
	std::array<char, 48> threadPreview = {};
	std::snprintf(threadPreview.data(), threadPreview.size(), "%u", model.threadCounts[state.timingThreadIndex]);
	std::array<char, 48> repeatPreview = {};
	if (state.timingRepeatIndex == 0)
		std::snprintf(repeatPreview.data(), repeatPreview.size(), "Median");
	else
		std::snprintf(repeatPreview.data(), repeatPreview.size(), "Repeat %u", state.timingRepeatIndex);
	if (ImGui::BeginTable("timing_controls", 7, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
	{
		ImGui::TableSetupColumn("##timing_engine_label", ImGuiTableColumnFlags_WidthFixed, 48.0f);
		ImGui::TableSetupColumn("##timing_engine_control", ImGuiTableColumnFlags_WidthStretch, 2.1f);
		ImGui::TableSetupColumn("##timing_threads_label", ImGuiTableColumnFlags_WidthFixed, 58.0f);
		ImGui::TableSetupColumn("##timing_threads_control", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("##timing_series_label", ImGuiTableColumnFlags_WidthFixed, 48.0f);
		ImGui::TableSetupColumn("##timing_series_control", ImGuiTableColumnFlags_WidthStretch, 1.4f);
		ImGui::TableSetupColumn("##timing_fit_control", ImGuiTableColumnFlags_WidthFixed, 40.0f);
		ImGui::TableNextRow(0, ImGui::GetFrameHeight());
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Engine");
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##timing_engine", enginePreview.data(), ImGuiComboFlags_HeightLargest))
		{
			if (ImGui::Selectable("Select all", selectedMask == availableMask, ImGuiSelectableFlags_DontClosePopups) &&
			    selectedMask != availableMask)
			{
				state.timingSelectedEngineMask = availableMask;
				state.timingFitRequest = PresenceStatus_Present;
			}
			if (ImGui::Selectable("Deselect all", selectedMask == 0, ImGuiSelectableFlags_DontClosePopups) &&
			    selectedMask != 0)
			{
				state.timingSelectedEngineMask = 0;
				state.timingFitRequest = PresenceStatus_Present;
			}
			for (std::uint32_t index = 0; index < model.engineCount; ++index)
			{
				const std::string_view name = ResultViewTextView(&model, model.engines[index].provenanceLabel);
				std::array<char, kEngineProvenanceLabelCapacity> label = {};
				std::snprintf(label.data(), label.size(), "%.*s", static_cast<int>(name.size()), name.data());
				const std::uint32_t engineMask = 1u << index;
				ImGui::PushID(static_cast<int>(index));
				if (ImGui::Selectable(label.data(), (state.timingSelectedEngineMask & engineMask) != 0,
				                      ImGuiSelectableFlags_DontClosePopups))
				{
					state.timingSelectedEngineMask ^= engineMask;
					state.timingFitRequest = PresenceStatus_Present;
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		selectedMask = state.timingSelectedEngineMask & availableMask;
		state.timingHiddenEngineMask &= selectedMask;
		ImGui::TableSetColumnIndex(2);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Threads");
		ImGui::TableSetColumnIndex(3);
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##timing_threads", threadPreview.data(), ImGuiComboFlags_HeightLargest))
		{
			for (std::uint32_t index = 0; index < model.threadCount; ++index)
			{
				std::array<char, 48> label = {};
				std::snprintf(label.data(), label.size(), "%u", model.threadCounts[index]);
				if (ImGui::Selectable(label.data(), state.timingThreadIndex == index))
				{
					state.timingThreadIndex = index;
					state.timingFitRequest = PresenceStatus_Present;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::TableSetColumnIndex(4);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Series");
		ImGui::TableSetColumnIndex(5);
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##timing_series", repeatPreview.data(), ImGuiComboFlags_HeightLargest))
		{
			if (ImGui::Selectable("Median", state.timingRepeatIndex == 0))
			{
				state.timingRepeatIndex = 0;
				state.timingFitRequest = PresenceStatus_Present;
			}
			for (std::uint32_t repeat = 0; repeat < model.repeatCount; ++repeat)
			{
				std::array<char, 48> label = {};
				std::snprintf(label.data(), label.size(), "Repeat %u", repeat + 1);
				if (ImGui::Selectable(label.data(), state.timingRepeatIndex == repeat + 1))
				{
					state.timingRepeatIndex = repeat + 1;
					state.timingFitRequest = PresenceStatus_Present;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::TableSetColumnIndex(6);
		if (ImGui::Button("Fit##timing_plot", ImVec2(-1.0f, 0.0f)))
			state.timingFitRequest = PresenceStatus_Present;
		ImGui::EndTable();
	}

	const std::uint32_t threadCount = model.threadCounts[state.timingThreadIndex];
	const std::uint32_t repeatIndex = state.timingRepeatIndex == 0 ? 0 : state.timingRepeatIndex - 1;
	const TimingProjectionMode mode =
	    state.timingRepeatIndex == 0 ? TimingProjectionMode_MedianAcrossRepeats : TimingProjectionMode_ExactRepeat;
	TimingComparisonCache& cache = model.timingComparison;
	const int selectionChanged = cache.availability == AvailabilityStatus_Unknown ||
	                             cache.selectedEngineMask != selectedMask || cache.threadCount != threadCount ||
	                             cache.repeatIndex != repeatIndex || cache.mode != mode;
	if (selectionChanged != 0)
	{
		cache.selectedEngineMask = selectedMask;
		cache.threadCount = threadCount;
		cache.repeatIndex = repeatIndex;
		cache.mode = mode;
		cache.traceCount = 0;
		cache.projection.sampleCount = 0;
		cache.projection.repeatCount = 0;
		cache.availability = AvailabilityStatus_Unavailable;
		for (std::uint32_t engineOrdinal = 0; engineOrdinal < model.engineCount; ++engineOrdinal)
		{
			if ((selectedMask & (1u << engineOrdinal)) == 0)
				continue;
			TimingProjectionSelection selection = {};
			selection.engineIndex = model.engines[engineOrdinal].catalogEngineIndex;
			selection.threadCount = threadCount;
			selection.repeatIndex = repeatIndex;
			selection.mode = mode;
			StatusRecord error = {};
			if (ProjectTimingSlice(&model.timing, &selection, &app->workspace.finalization.timingScratch,
			                       &cache.projection, &error) != ArenaStatus_Ok)
			{
				state.status = error.code;
				state.error = error;
				return;
			}
			const std::uint32_t traceIndex = cache.traceCount++;
			TimingComparisonTrace& trace = cache.traces[traceIndex];
			double* traceSamples = app->timingComparisonSamples->data() + traceIndex * kTimingProjectionStepCapacity;
			std::copy_n(cache.projection.physicsStepMilliseconds.begin(), cache.projection.sampleCount, traceSamples);
			trace.medianPhysicsStepMilliseconds = cache.projection.medianPhysicsStepMilliseconds;
			trace.p95PhysicsStepMilliseconds = cache.projection.p95PhysicsStepMilliseconds;
			trace.p99PhysicsStepMilliseconds = cache.projection.p99PhysicsStepMilliseconds;
			trace.maximumPhysicsStepMilliseconds = cache.projection.maximumPhysicsStepMillisecondsValue;
			trace.engineOrdinal = engineOrdinal;
			trace.worstStepIndex = cache.projection.worstStepIndex;
		}
		cache.availability = AvailabilityStatus_Available;
		cache.generation += 1;
		state.status = ArenaStatus_Ok;
		state.error = {};
	}
	if (cache.availability != AvailabilityStatus_Available)
		return;
	double maximum = 0;
	for (std::uint32_t index = 0; index < cache.traceCount; ++index)
		maximum = (std::max)(maximum, cache.traces[index].maximumPhysicsStepMilliseconds);
	NativeValueFormat numberFormat = SelectNativeValueFormat(NativeValueDomain_Milliseconds, maximum);
	const NativeValueText durationUnit = FormatNativeUnit(numberFormat, "ms");
	if (state.timingFitRequest == PresenceStatus_Present && selectionChanged == 0)
	{
		ImPlot::SetNextAxesToFit();
		state.timingFitRequest = PresenceStatus_Absent;
	}
	const float statisticsHeight = (std::max)(80 * app->platform.dpiScale, ImGui::GetContentRegionAvail().y * 0.45f);
	const float plotHeight =
	    std::max(180 * app->platform.dpiScale, ImGui::GetContentRegionAvail().y - statisticsHeight - ImGui::GetStyle().ItemSpacing.y);
	const std::string_view workUnit = ResultViewTextView(&model, model.workUnitLabel);
	std::array<char, kCatalogTextValueCapacity> capitalizedWorkUnit = {};
	std::snprintf(capitalizedWorkUnit.data(), capitalizedWorkUnit.size(), "%.*s", static_cast<int>(workUnit.size()),
	              workUnit.data());
	if (capitalizedWorkUnit[0] >= 'a' && capitalizedWorkUnit[0] <= 'z')
		capitalizedWorkUnit[0] = static_cast<char>(capitalizedWorkUnit[0] - 'a' + 'A');
	std::array<char, kCatalogTextValueCapacity + 40> plotTitle = {};
	std::snprintf(plotTitle.data(), plotTitle.size(), "%s timing comparison##step_timing_plot",
	              capitalizedWorkUnit.data());
	std::array<char, kCatalogTextValueCapacity + 8> worstUnitLabel = {};
	std::snprintf(worstUnitLabel.data(), worstUnitLabel.size(), "Worst %.*s", static_cast<int>(workUnit.size()),
	              workUnit.data());
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 10);
	if (ImPlot::BeginPlot(plotTitle.data(), ImVec2(-1.0f, plotHeight), ImPlotFlags_Crosshairs))
	{
		ImPlot::SetupAxes(capitalizedWorkUnit.data(), durationUnit.data(), ImPlotAxisFlags_NoInitialFit,
		                  ImPlotAxisFlags_NoInitialFit);
		ImPlot::SetupAxisFormat(ImAxis_Y1, NativePlotNumber, &numberFormat);
		ImPlot::SetupLegend(ImPlotLocation_East, ImPlotLegendFlags_Outside);
		if (cache.traceCount == 1 && cache.mode == TimingProjectionMode_MedianAcrossRepeats &&
		    cache.projection.repeatCount > 1 &&
		    (state.timingHiddenEngineMask & (1u << cache.traces[0].engineOrdinal)) == 0)
		{
			const ResultEngineView& engine = model.engines[cache.traces[0].engineOrdinal];
			const ImVec4 color = ResultEngineColor(engine.colorRgb);
			TimingPlotView minimum = {cache.projection.stepIndexes.data(),
			                          cache.projection.minimumPhysicsStepMilliseconds.data()};
			TimingPlotView maximum = {cache.projection.stepIndexes.data(),
			                          cache.projection.maximumPhysicsStepMilliseconds.data()};
			std::array<char, 64> rangeId = {};
			std::snprintf(rangeId.data(), rangeId.size(), "##timing_range_%u", cache.traces[0].engineOrdinal);
			ImPlotSpec rangeStyle = {};
			rangeStyle.FillColor = color;
			rangeStyle.FillAlpha = 0.35f;
			rangeStyle.Flags = ImPlotItemFlags_NoLegend;
			ImPlot::PlotShadedG(rangeId.data(), TimingPoint, &minimum, TimingPoint, &maximum,
			                    static_cast<int>(cache.projection.sampleCount), rangeStyle);
		}
		for (std::uint32_t traceIndex = 0; traceIndex < cache.traceCount; ++traceIndex)
		{
			const TimingComparisonTrace& trace = cache.traces[traceIndex];
			const double* traceSamples =
			    app->timingComparisonSamples->data() + traceIndex * kTimingProjectionStepCapacity;
			const ResultEngineView& engine = model.engines[trace.engineOrdinal];
			const std::string_view name = ResultViewTextView(&model, engine.provenanceLabel);
			std::array<char, kEngineProvenanceLabelCapacity + 32> label = {};
			std::snprintf(label.data(), label.size(), "%.*s##timing_trace_%u", static_cast<int>(name.size()),
			              name.data(), trace.engineOrdinal);
			const ImVec4 color = ResultEngineColor(engine.colorRgb);
			ImPlotSpec lineStyle = {};
			lineStyle.LineColor = color;
			lineStyle.LineWeight = 2.0f;
			TimingPlotView physics = {cache.projection.stepIndexes.data(), traceSamples};
			const std::uint32_t visibilityMask = 1u << trace.engineOrdinal;
			ImPlot::HideNextItem((state.timingHiddenEngineMask & visibilityMask) != 0, ImPlotCond_Always);
			ImPlot::PlotLineG(label.data(), TimingPoint, &physics, static_cast<int>(cache.projection.sampleCount),
			                  lineStyle);
			if (ImPlot::IsLegendEntryHovered(label.data()) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				state.timingHiddenEngineMask ^= visibilityMask;
				state.timingFitRequest = PresenceStatus_Present;
			}
		}
		const TimingComparisonTrace* hoveredTrace = nullptr;
		std::uint32_t hoveredTraceIndex = 0;
		std::uint32_t hoveredIndex = 0;
		if (ImPlot::IsPlotHovered() && cache.projection.sampleCount != 0)
		{
			const ImPlotPoint plotMouse = ImPlot::GetPlotMousePos();
			const ImVec2 screenMouse = ImGui::GetIO().MousePos;
			const int step = std::clamp(static_cast<int>(std::llround(plotMouse.x)), 1,
			                            static_cast<int>(cache.projection.sampleCount));
			const std::uint32_t index = static_cast<std::uint32_t>(step - 1);
			float nearestDistanceSquared = 64.0f;
			for (std::uint32_t traceIndex = 0; traceIndex < cache.traceCount; ++traceIndex)
			{
				const TimingComparisonTrace& trace = cache.traces[traceIndex];
				if ((state.timingHiddenEngineMask & (1u << trace.engineOrdinal)) != 0)
					continue;
				const double* traceSamples =
				    app->timingComparisonSamples->data() + traceIndex * kTimingProjectionStepCapacity;
				const ImVec2 point =
				    ImPlot::PlotToPixels(static_cast<double>(cache.projection.stepIndexes[index]), traceSamples[index]);
				const float deltaX = point.x - screenMouse.x;
				const float deltaY = point.y - screenMouse.y;
				const float distanceSquared = deltaX * deltaX + deltaY * deltaY;
				if (distanceSquared <= nearestDistanceSquared)
				{
					nearestDistanceSquared = distanceSquared;
					hoveredTrace = &trace;
					hoveredTraceIndex = traceIndex;
					hoveredIndex = index;
				}
			}
		}
		if (hoveredTrace != nullptr)
		{
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				SelectAnalysisResult(app, hoveredTrace->engineOrdinal, state.timingThreadIndex, state.timingRepeatIndex == 0 ? kRunRepeatCapacity : state.timingRepeatIndex - 1);
			const ResultEngineView& engine = model.engines[hoveredTrace->engineOrdinal];
			const std::string_view name = ResultViewTextView(&model, engine.provenanceLabel);
			ImGui::BeginTooltip();
			ImGui::Text("%.*s", static_cast<int>(name.size()), name.data());
			ImGui::Text("%s %u", capitalizedWorkUnit.data(), cache.projection.stepIndexes[hoveredIndex]);
			ImGui::Text("%u thread%s", cache.threadCount, cache.threadCount == 1 ? "" : "s");
			if (cache.mode == TimingProjectionMode_MedianAcrossRepeats)
				ImGui::TextUnformatted("Median");
			else
				ImGui::Text("Repeat %u", cache.repeatIndex + 1);
			const double hoveredValue =
			    (*app->timingComparisonSamples)[hoveredTraceIndex * kTimingProjectionStepCapacity + hoveredIndex];
			ImGui::Text("Physics %s ms", FormatNativeRaw(hoveredValue).data());
			if (cache.traceCount == 1 && cache.mode == TimingProjectionMode_MedianAcrossRepeats &&
			    cache.projection.repeatCount > 1)
				ImGui::Text("Range %s - %s ms", FormatNativeRaw(cache.projection.minimumPhysicsStepMilliseconds[hoveredIndex]).data(),
				            FormatNativeRaw(cache.projection.maximumPhysicsStepMilliseconds[hoveredIndex]).data());
			ImGui::EndTooltip();
		}
		ImPlot::EndPlot();
	}
	ImGui::PopFont();
	if (ImGui::BeginTable("timing_statistics", 6,
	                      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
	                          ImGuiTableFlags_ScrollY,
	                      ImVec2(0.0f, statisticsHeight)))
	{
		ImGui::TableSetupColumn("Engine", ImGuiTableColumnFlags_WidthStretch, 1.6f);
		ImGui::TableSetupColumn("Median", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("P95", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("P99", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("Maximum", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn(worstUnitLabel.data(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();
		for (std::uint32_t traceIndex = 0; traceIndex < cache.traceCount; ++traceIndex)
		{
			const TimingComparisonTrace& trace = cache.traces[traceIndex];
			const ResultEngineView& engine = model.engines[trace.engineOrdinal];
			const std::string_view name = ResultViewTextView(&model, engine.provenanceLabel);
			const ImVec4 color = ResultEngineColor(engine.colorRgb);
			ImGui::TableNextRow(0, 27 * app->platform.dpiScale);
			ImGui::TableNextColumn();
			const ImVec2 swatch = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddRectFilled(swatch, ImVec2(swatch.x + 6 * app->platform.dpiScale, swatch.y + 10 * app->platform.dpiScale), ImGui::GetColorU32(color));
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 11 * app->platform.dpiScale);
			ImGui::Text("%.*s", static_cast<int>(name.size()), name.data());
			const std::array<double, 4> values = {trace.medianPhysicsStepMilliseconds, trace.p95PhysicsStepMilliseconds,
			    trace.p99PhysicsStepMilliseconds, trace.maximumPhysicsStepMilliseconds};
			for (double value : values)
			{
				ImGui::TableNextColumn();
				std::array<char, 640> number = {};
				std::snprintf(number.data(), number.size(), "%s %s", FormatNativeValue(value, numberFormat).data(), durationUnit.data());
				DrawResultTableValue(number.data());
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s ms", FormatNativeRaw(value).data());
			}
			ImGui::TableNextColumn();
			DrawResultTableValue(FormatNativeCount(trace.worstStepIndex).data());
		}
		ImGui::EndTable();
	}
}
}
