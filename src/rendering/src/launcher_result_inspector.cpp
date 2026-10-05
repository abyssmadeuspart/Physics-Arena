#include "launcher_app_internal.h"

#include <imgui.h>
#include <algorithm>
#include <array>
#include <cstdio>

namespace benchmark_visual
{
using namespace physics_arena;

void InspectorFact(const char* label, std::string_view value)
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	const float baseline = ImGui::GetCursorPosY() + ImGui::GetFontBaked()->Ascent;
	ImGui::PushTextWrapPos(0);
	ImGui::TextDisabled("%s", label);
	ImGui::PopTextWrapPos();
	ImGui::TableSetColumnIndex(1);
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 11);
	ImGui::SetCursorPosY(baseline - ImGui::GetFontBaked()->Ascent);
	ImGui::TextWrapped("%.*s", static_cast<int>(value.size()), value.data());
	ImGui::PopFont();
}

NativeUiCommand DrawResultInspector(PhysicsArenaApp* app)
{
	const ResultViewModel& model = app->workspace.finalization.model;
	if (model.verificationMode == VerificationMode_Off)
		ImGui::TextWrapped("Verification Off: physical quality not checked");
	NativeResultsState& state = app->results;
	const int rayFrame = ResultViewTextView(&model, model.workUnitId) == "ray_frame";
	if (state.selectedResultPresence != PresenceStatus_Present)
	{
		ImGui::TextWrapped("No selected measurement. Choose an engine in the comparison or Repeats view.");
		return NativeUiCommand_None;
	}
	const ResultEngineView& engine = model.engines[state.selectedEngineIndex];
	const std::string_view label = ResultViewTextView(&model, engine.provenanceLabel);
	const ImVec2 header = ImGui::GetCursorPos();
	DrawPaneHeading("Selected result");
	ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 50 * app->platform.dpiScale, header.y + 5 * app->platform.dpiScale));
	if (ImGui::SmallButton("Copy##selected_result"))
	{
		std::array<char, kEngineProvenanceLabelCapacity + 3 * kCatalogTextValueCapacity + 512> text = {};
		const std::string_view run = ResultViewTextView(&model, model.runId);
		if (state.selectedRepeatIndex < model.repeatCount && state.repeatStatus == ArenaStatus_Ok)
		{
			const ResultRepeatRow& row = state.repeats.rows[state.selectedRepeatIndex];
			const std::string_view unit = ResultViewTextView(&model, model.primaryMetricUnit);
			if (row.measurement == PresenceStatus_Absent)
			{
				const ExecutionFailure* failure = FindExecutionFailure(model.executionFailures, engine.catalogEngineIndex,
				    model.threadCounts[state.selectedThreadIndex], row.repeatIndex);
				std::snprintf(text.data(), text.size(), "%.*s\n%.*s\nThreads: %u\nRepeat: %u\nMeasurement unavailable: %s",
				    static_cast<int>(run.size()), run.data(), static_cast<int>(label.size()), label.data(),
				    model.threadCounts[state.selectedThreadIndex], row.repeatIndex + 1,
				    failure != nullptr ? failure->detail.data() : "No saved measurement");
			}
			else if (model.measurementMode == ResultMeasurementMode_Timed)
				std::snprintf(text.data(), text.size(), "%.*s\n%.*s\nThreads: %u\nRepeat: %u\n%s: %s %.*s\n%s: %s ms/work unit\nWork units/s: %s\n%s: %s ms\n%s: %u\nInvalid transforms: %llu",
				    static_cast<int>(run.size()), run.data(), static_cast<int>(label.size()), label.data(), model.threadCounts[state.selectedThreadIndex], state.selectedRepeatIndex + 1,
				    rayFrame != 0 ? "Coherent primary rays/s" : "Primary", FormatNativeRaw(row.primaryValue).data(), static_cast<int>(unit.size()), unit.data(),
				    rayFrame != 0 ? "Coherent primary frame time" : "Mean", FormatNativeRaw(row.meanWorkUnitMilliseconds).data(),
				    FormatNativeRaw(row.workUnitsPerSecond).data(), rayFrame != 0 ? "Primary query time" : "Native workload", FormatNativeRaw(row.workloadElapsedMilliseconds).data(),
				    rayFrame != 0 ? "Primary ray frames" : "Completed work units", row.completedWorkUnitCount, static_cast<unsigned long long>(row.invalidTransformCount));
			else
				std::snprintf(text.data(), text.size(), "%.*s\n%.*s\nThreads: %u\nRepeat: %u\nQuality observations only; timing not recorded\nCompleted work units: %u\nInvalid transforms: %llu",
				    static_cast<int>(run.size()), run.data(), static_cast<int>(label.size()), label.data(), model.threadCounts[state.selectedThreadIndex], state.selectedRepeatIndex + 1,
				    row.completedWorkUnitCount, static_cast<unsigned long long>(row.invalidTransformCount));
		}
		else
			for (std::uint32_t index = 0; index < model.summaryRowCount; ++index)
			{
				const ResultSummaryViewRow& row = model.summaryRows[index];
				if (row.engineOrdinal != state.selectedEngineIndex || row.threadCount != model.threadCounts[state.selectedThreadIndex])
					continue;
				const std::string_view outcome = row.outcome == ObservationOutcome_Unknown ? "Not recorded" : ObservationOutcomeText(row.outcome);
				if (row.repeatCount == 0)
					std::snprintf(text.data(), text.size(), "%.*s\n%.*s\nThreads: %u\nAll %u repeats\nMeasurement unavailable: no measured repeats",
					    static_cast<int>(run.size()), run.data(), static_cast<int>(label.size()), label.data(), row.threadCount, model.repeatCount);
				else if (model.measurementMode == ResultMeasurementMode_Timed)
				{
					const ResultMetricDescriptor& metric = model.metrics[state.metricIndex];
					std::string statistic(ResultViewTextView(&model, metric.label));
					if (rayFrame != 0)
						statistic = std::string(metric.id < ResultMetric_MedianWorkUnitMilliseconds ? "Coherent primary rays/s | " : "Coherent primary frame time | ") + statistic;
					const std::string_view unit = ResultViewTextView(&model, metric.unitLabel);
					std::snprintf(text.data(), text.size(), "%.*s\n%.*s\nThreads: %u\nAll %u repeat%s\n%.*s: %s %.*s\nObservation outcome: %.*s",
					    static_cast<int>(run.size()), run.data(), static_cast<int>(label.size()), label.data(), row.threadCount, model.repeatCount, model.repeatCount == 1 ? "" : "s",
					    static_cast<int>(statistic.size()), statistic.data(), FormatNativeRaw(ResultMetricValue(&row, metric.id)).data(), static_cast<int>(unit.size()), unit.data(), static_cast<int>(outcome.size()), outcome.data());
				}
				else
					std::snprintf(text.data(), text.size(), "%.*s\n%.*s\nThreads: %u\nAll %u repeat%s\nQuality observations only; timing not recorded\nObservation outcome: %.*s",
					    static_cast<int>(run.size()), run.data(), static_cast<int>(label.size()), label.data(), row.threadCount, model.repeatCount, model.repeatCount == 1 ? "" : "s", static_cast<int>(outcome.size()), outcome.data());
				break;
			}
		if (model.verificationMode == VerificationMode_Off)
		{
			const std::size_t used = std::strlen(text.data());
			std::snprintf(text.data() + used, text.size() - used, "\nVerification Off: physical quality not checked");
		}
		ImGui::SetClipboardText(text.data());
	}
	ImGui::SetCursorPos(ImVec2(7 * app->platform.dpiScale, header.y + 38 * app->platform.dpiScale));
	ImGui::BeginChild("selected_result_content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
	const ImVec2 swatch = ImGui::GetCursorScreenPos();
	ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(swatch.x, swatch.y + 4 * app->platform.dpiScale),
	    ImVec2(swatch.x + 7 * app->platform.dpiScale, swatch.y + 11 * app->platform.dpiScale), ImGui::GetColorU32(ResultEngineColor(engine.colorRgb)));
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12 * app->platform.dpiScale);
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Heading), 13 * kNativeUiBodyLineScale);
	ImGui::TextWrapped("%.*s", static_cast<int>(label.size()), label.data());
	ImGui::PopFont();
	const int aggregate = state.selectedRepeatIndex >= model.repeatCount;
	const int query = rayFrame != 0 || ResultViewTextView(&model, model.workUnitId) == "query_batch";
	if (ImGui::BeginTable("selected_identity", 2, ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100 * app->platform.dpiScale);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
		InspectorFact("Threads", FormatNativeCount(model.threadCounts[state.selectedThreadIndex]).data());
		std::array<char, 64> population = {};
		if (aggregate != 0)
			std::snprintf(population.data(), population.size(), "All %u repeat%s", model.repeatCount, model.repeatCount == 1 ? "" : "s");
		else
			std::snprintf(population.data(), population.size(), "%u", state.selectedRepeatIndex + 1);
		InspectorFact(aggregate != 0 ? "Repeats" : "Repeat", population.data());
		ImGui::EndTable();
	}
	if (state.repeatStatus != ArenaStatus_Ok)
		DrawStatusError(state.repeatError);
	else if (aggregate != 0)
	{
		for (std::uint32_t index = 0; index < model.summaryRowCount; ++index)
		{
			const ResultSummaryViewRow& row = model.summaryRows[index];
			if (row.engineOrdinal != state.selectedEngineIndex || row.threadCount != model.threadCounts[state.selectedThreadIndex])
				continue;
			if (row.repeatCount == 0)
				ImGui::TextUnformatted("Measurement unavailable");
			else if (model.measurementMode == ResultMeasurementMode_Timed)
			{
				const ResultMetricDescriptor& metric = model.metrics[state.metricIndex];
				const std::string_view unit = ResultViewTextView(&model, metric.unitLabel);
				const std::string_view statistic = ResultViewTextView(&model, metric.label);
				const NativeValueFormat format = SummaryNumberFormat(model, metric.id);
				ImGui::PushFont(NativeUiFontFace(NativeUiFont_DataStrong), 18);
				ImGui::Text("%s %s", FormatNativeValue(ResultMetricValue(&row, metric.id), format).data(), FormatNativeUnit(format, unit).data());
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
					ImGui::SetTooltip("%s %.*s", FormatNativeRaw(ResultMetricValue(&row, metric.id)).data(), static_cast<int>(unit.size()), unit.data());
				ImGui::PopFont();
				ImGui::TextWrapped("%s | %.*s of repeat %s", rayFrame != 0 ? (metric.id < ResultMetric_MedianWorkUnitMilliseconds ? "Coherent primary rays/s" : "Coherent primary frame time") : query != 0 ? (metric.id < ResultMetric_MedianWorkUnitMilliseconds ? "Query throughput" : "Batch time") : "Step time",
				    static_cast<int>(statistic.size()), statistic.data(), query != 0 && metric.id < ResultMetric_MedianWorkUnitMilliseconds ? "throughputs" : "means");
			}
			else
				ImGui::TextWrapped("Quality only | Timing not recorded");
			break;
		}
	}
	else
	{
		const ResultRepeatRow& row = state.repeats.rows[state.selectedRepeatIndex];
		if (row.measurement == PresenceStatus_Absent)
			ImGui::TextUnformatted("Measurement unavailable");
		else if (model.measurementMode == ResultMeasurementMode_Timed)
		{
			const std::string_view unit = ResultViewTextView(&model, model.primaryMetricUnit);
			const NativeValueFormat primary = SummaryNumberFormat(model, ResultMetric_MedianPrimaryValue);
			ImGui::PushFont(NativeUiFontFace(NativeUiFont_DataStrong), 18);
			ImGui::Text("%s %s", FormatNativeValue(row.primaryValue, primary).data(), FormatNativeUnit(primary, unit).data());
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
				ImGui::SetTooltip("%s %.*s", FormatNativeRaw(row.primaryValue).data(), static_cast<int>(unit.size()), unit.data());
			ImGui::PopFont();
			ImGui::TextWrapped("%s", rayFrame != 0 ? "Coherent primary rays/s" : query != 0 ? "Query throughput" : "Mean step time");
			if (ImGui::BeginTable("repeat_measurements", 2, ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100 * app->platform.dpiScale);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
				const std::array<double, 3> values = {row.meanWorkUnitMilliseconds, row.workUnitsPerSecond, row.workloadElapsedMilliseconds};
				const std::array<const char*, 3> labels = {rayFrame != 0 ? "Coherent primary frame time" : query != 0 ? "Mean batch time" : "Mean step time", rayFrame != 0 ? "Frames/s" : query != 0 ? "Batches/s" : "Steps/s", rayFrame != 0 ? "Primary query time" : "Native workload"};
				for (std::size_t index = 0; index < values.size(); ++index)
				{
					const NativeValueFormat format = SelectNativeValueFormat(index == 1 ? NativeValueDomain_Rate : NativeValueDomain_Milliseconds, values[index]);
					NativeValueText number = {};
					std::snprintf(number.data(), number.size(), "%s %s", FormatNativeValue(values[index], format).data(),
					    FormatNativeUnit(format, rayFrame != 0 ? "frames/s" : query != 0 ? "batches/s" : "steps/s").data());
					InspectorFact(labels[index], number.data());
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
						ImGui::SetTooltip("%s %s", FormatNativeRaw(values[index]).data(), index == 1 ? (rayFrame != 0 ? "frames/s" : query != 0 ? "batches/s" : "steps/s") : "ms");
				}
				ImGui::EndTable();
			}
		}
		else
			ImGui::TextWrapped("Quality observations only. No performance measurement or physical gap tolerance.");
		if (row.measurement == PresenceStatus_Present)
		{
		ImGui::Text("Completed %s: %s", rayFrame != 0 ? "primary ray frames" : query != 0 ? "batches" : "steps", FormatNativeCount(row.completedWorkUnitCount).data());
		ImGui::Text("Invalid transforms: %s", FormatNativeCount(row.invalidTransformCount).data());
		}
	}
	for (std::uint32_t index = 0; index < model.summaryRowCount; ++index)
	{
		const ResultSummaryViewRow& summary = model.summaryRows[index];
		if (summary.engineOrdinal != state.selectedEngineIndex || summary.threadCount != model.threadCounts[state.selectedThreadIndex])
			continue;
		const std::string_view outcome = summary.outcome == ObservationOutcome_Unknown ? "Not recorded" : ObservationOutcomeText(summary.outcome);
		if (ImGui::BeginTable("selected_outcome", 2, ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100 * app->platform.dpiScale);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
			InspectorFact("Outcome", outcome);
			ImGui::EndTable();
		}
		break;
	}
	ImGui::TextDisabled("Outcome across all saved repeats");
	if (ImGui::Button("Inspect repeats", ImVec2(-1, 0)))
		state.mode = NativeResultMode_Repeats;
	if (model.timing.availability == PresenceStatus_Present && ImGui::Button(rayFrame != 0 ? "View primary timing" : query != 0 ? "View batch timing" : "View step timing", ImVec2(-1, 0)))
	{
		state.timingSelectedEngineMask = 1u << state.selectedEngineIndex;
		state.timingHiddenEngineMask = 0;
		state.timingThreadIndex = state.selectedThreadIndex;
		state.timingRepeatIndex = aggregate != 0 ? 0 : state.selectedRepeatIndex + 1;
		state.timingFitRequest = PresenceStatus_Present;
		state.mode = NativeResultMode_StepTiming;
	}
	DrawPaneHeading("Saved run", 28);
	DrawSavedRunFacts(app);
	DrawPaneHeading("Engine settings", 28);
	DrawSavedEngineConfiguration(app, state.selectedEngineIndex);
	if (ImGui::Button("Source configuration", ImVec2(-1, 0)))
	{
		app->ui.resultsDetailsVisible = PresenceStatus_Absent;
		state.mode = NativeResultMode_CaseData;
		state.caseDataPage = NativeCaseDataPage_Configuration;
	}
	if (ImGui::Button("Case observations", ImVec2(-1, 0)))
	{
		app->ui.resultsDetailsVisible = PresenceStatus_Absent;
		state.mode = NativeResultMode_CaseData;
		state.caseDataPage = NativeCaseDataPage_Observations;
		state.caseDataEngineIndex = state.selectedEngineIndex;
		state.caseDataThreadIndex = state.selectedThreadIndex;
		state.caseDataRepeatIndex = aggregate != 0 ? 0 : state.selectedRepeatIndex;
		RefreshCaseDataDetail(app);
	}
	NativeUiCommand command = NativeUiCommand_None;
	DrawPaneHeading("Recording", 28);
	ImGui::BeginDisabled(state.selectedReplayAvailability != ReplayAvailability_Available);
	if (ImGui::Button("Play recording", ImVec2(-1, 0)))
		command = RequestRecordingPlayback(app);
	ImGui::EndDisabled();
	if (state.selectedReplayAvailability != ReplayAvailability_Available)
	{
		constexpr std::array<const char*, 5> reasons = {"Not recorded", "Available", "Recording file missing", "Recording invalid", "Temporary recording only"};
		ImGui::TextWrapped("%s for this selection", reasons[state.selectedReplayAvailability]);
	}
	if (ImGui::Button("Browse recordings", ImVec2(-1, 0)))
	{
		state.mode = NativeResultMode_Recordings;
		app->ui.resultsDetailsVisible = PresenceStatus_Absent;
	}
	ImGui::Spacing();
	if (ImGui::CollapsingHeader("Technical details"))
	{
		DrawSavedRunTechnicalDetails(app);
		ImGui::Separator();
		DrawRecordedEngineDetails(app, state.selectedEngineIndex);
	}
	ImGui::EndChild();
	return command;
}
}
