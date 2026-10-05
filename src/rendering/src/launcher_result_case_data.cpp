#include "launcher_app_internal.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cinttypes>
#include <cstdio>
#include <string_view>

namespace benchmark_visual
{
using namespace physics_arena;
std::uint32_t ResultThreadOrdinal(const ResultViewModel& model, std::uint32_t threadCount)
{
	std::uint32_t ordinal = 0;
	while (ordinal < model.threadCount && model.threadCounts[ordinal] != threadCount)
		++ordinal;
	return ordinal;
}

NativeValueFormat ObservationNumberFormat(const ResultViewModel& model, std::uint32_t observationIndex)
{
	const ResultObservationView& observation = model.observationViews[observationIndex];
	if (observation.valueType != ObservationValueType_Float64 || observation.role != ObservationRole_Performance ||
	    ResultViewTextView(&model, observation.unit) != ResultViewTextView(&model, model.primaryMetricUnit))
		return SelectNativeValueFormat(NativeValueDomain_Quantity, 0);
	const NativeValueDomain domain = ResultViewTextView(&model, model.workUnitId) == "query_batch" ?
	    NativeValueDomain_Rate : NativeValueDomain_Milliseconds;
	double maximum = 0;
	for (std::uint32_t rowIndex = 0; rowIndex < model.displayRowCount; ++rowIndex)
	{
		const ResultSummaryViewRow& row = model.summaryRows[model.displayRowIndexes[rowIndex]];
		const ObservationAggregate* aggregate = ObservationAggregateAt(&model.observations, row.engineOrdinal,
		    ResultThreadOrdinal(model, row.threadCount), observationIndex);
		if (aggregate != nullptr && aggregate->sampleCount != 0)
			maximum = (std::max)(maximum, (std::max)(std::abs(aggregate->minimumActual.float64Value),
			    std::abs(aggregate->maximumActual.float64Value)));
	}
	return SelectNativeValueFormat(domain, maximum);
}

NativeValueText FormatObservationValue(const ResultObservationView& observation, const ObservationValue& value,
                                      NativeValueFormat format, PresenceStatus raw = PresenceStatus_Absent)
{
	if (observation.valueType == ObservationValueType_Uint64)
		return raw == PresenceStatus_Present ? FormatNativeRawCount(value.unsignedValue) : FormatNativeCount(value.unsignedValue);
	return raw == PresenceStatus_Present ? FormatNativeRaw(value.float64Value) :
	    FormatNativeValue(value.float64Value, format);
}

void DrawObservationAggregateCell(const ResultViewModel& model, const ResultObservationView& observation,
                                  const ObservationAggregate& aggregate, NativeValueFormat format)
{
	if (aggregate.sampleCount == 0)
	{
		ImGui::TextDisabled("Unavailable");
		return;
	}
	const int validity = observation.role == ObservationRole_ValidityZero || observation.role == ObservationRole_ValidityExact;
	const ObservationValue& first = validity != 0 ? aggregate.actualTotal : aggregate.minimumActual;
	const ObservationValue& second = validity != 0 ? aggregate.expectedTotal : aggregate.maximumActual;
	std::array<char, 1050> value = {};
	if (validity != 0 || aggregate.sampleCount != 1)
		std::snprintf(value.data(), value.size(), "%s %s %s", FormatObservationValue(observation, first, format).data(),
		    validity != 0 ? "/" : "-", FormatObservationValue(observation, second, format).data());
	else
		std::snprintf(value.data(), value.size(), "%s", FormatObservationValue(observation, first, format).data());
	ImGui::TextUnformatted(value.data());
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
	{
		const std::string_view unit = ResultViewTextView(&model, observation.unit);
		ImGui::BeginTooltip();
		ImGui::Text("%s: %s\n%s: %s", validity != 0 ? "Actual total" : "Minimum",
		    FormatObservationValue(observation, first, format, PresenceStatus_Present).data(), validity != 0 ? "Expected total" : "Maximum",
		    FormatObservationValue(observation, second, format, PresenceStatus_Present).data());
		if (observation.role == ObservationRole_ValidityZero || observation.role == ObservationRole_ValidityExact)
			ImGui::Text("Failed samples: %u\nSamples: %u\nUnit: %.*s\nStatus: %.*s", aggregate.failedSampleCount,
			                  aggregate.sampleCount, static_cast<int>(unit.size()), unit.data(),
			                  static_cast<int>(ObservationOutcomeText(aggregate.outcome).size()),
			                  ObservationOutcomeText(aggregate.outcome).data());
		else
			ImGui::Text("Samples: %u\nUnit: %.*s\nStatus: %.*s", aggregate.sampleCount,
			                  static_cast<int>(unit.size()), unit.data(),
			                  static_cast<int>(ObservationOutcomeText(aggregate.outcome).size()),
		                  ObservationOutcomeText(aggregate.outcome).data());
		ImGui::EndTooltip();
	}
}

void DrawCaseDataControls(PhysicsArenaApp* app)
{
	ResultViewModel& model = app->workspace.finalization.model;
	NativeResultsState& state = app->results;
	const std::string_view engine =
	    ResultViewTextView(&model, model.engines[state.caseDataEngineIndex].provenanceLabel);
	std::array<char, kEngineProvenanceLabelCapacity> enginePreview = {};
	std::snprintf(enginePreview.data(), enginePreview.size(), "%.*s", static_cast<int>(engine.size()), engine.data());
	std::array<char, 32> threadPreview = {};
	std::array<char, 32> repeatPreview = {};
	std::snprintf(threadPreview.data(), threadPreview.size(), "%u", model.threadCounts[state.caseDataThreadIndex]);
	std::snprintf(repeatPreview.data(), repeatPreview.size(), "Repeat %u", state.caseDataRepeatIndex + 1);
	if (!ImGui::BeginTable("case_data_controls", 6,
	                       ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
		return;
	ImGui::TableSetupColumn("##case_data_engine_label", ImGuiTableColumnFlags_WidthFixed, 48.0f);
	ImGui::TableSetupColumn("##case_data_engine", ImGuiTableColumnFlags_WidthStretch, 2.0f);
	ImGui::TableSetupColumn("##case_data_thread_label", ImGuiTableColumnFlags_WidthFixed, 58.0f);
	ImGui::TableSetupColumn("##case_data_thread", ImGuiTableColumnFlags_WidthStretch, 1.0f);
	ImGui::TableSetupColumn("##case_data_repeat_label", ImGuiTableColumnFlags_WidthFixed, 48.0f);
	ImGui::TableSetupColumn("##case_data_repeat", ImGuiTableColumnFlags_WidthStretch, 1.0f);
	ImGui::TableNextRow(0, ImGui::GetFrameHeight());
	ImGui::TableSetColumnIndex(0);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Engine");
	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::BeginCombo("##case_data_engine_selector", enginePreview.data()))
	{
		for (std::uint32_t index = 0; index < model.engineCount; ++index)
		{
			const std::string_view name = ResultViewTextView(&model, model.engines[index].provenanceLabel);
			std::array<char, kEngineProvenanceLabelCapacity> label = {};
			std::snprintf(label.data(), label.size(), "%.*s", static_cast<int>(name.size()), name.data());
			if (ImGui::Selectable(label.data(), state.caseDataEngineIndex == index))
			{
				state.caseDataEngineIndex = index;
				RefreshCaseDataDetail(app);
				SelectAnalysisResult(app, state.caseDataEngineIndex, state.caseDataThreadIndex, state.caseDataRepeatIndex);
			}
		}
		ImGui::EndCombo();
	}
	ImGui::TableSetColumnIndex(2);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Threads");
	ImGui::TableSetColumnIndex(3);
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::BeginCombo("##case_data_thread_selector", threadPreview.data()))
	{
		for (std::uint32_t index = 0; index < model.threadCount; ++index)
		{
			std::array<char, 32> label = {};
			std::snprintf(label.data(), label.size(), "%u", model.threadCounts[index]);
			if (ImGui::Selectable(label.data(), state.caseDataThreadIndex == index))
			{
				state.caseDataThreadIndex = index;
				RefreshCaseDataDetail(app);
				SelectAnalysisResult(app, state.caseDataEngineIndex, state.caseDataThreadIndex, state.caseDataRepeatIndex);
			}
		}
		ImGui::EndCombo();
	}
	ImGui::TableSetColumnIndex(4);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Repeat");
	ImGui::TableSetColumnIndex(5);
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::BeginCombo("##case_data_repeat_selector", repeatPreview.data()))
	{
		for (std::uint32_t index = 0; index < model.repeatCount; ++index)
		{
			std::array<char, 32> label = {};
			std::snprintf(label.data(), label.size(), "Repeat %u", index + 1);
			if (ImGui::Selectable(label.data(), state.caseDataRepeatIndex == index))
			{
				state.caseDataRepeatIndex = index;
				RefreshCaseDataDetail(app);
				SelectAnalysisResult(app, state.caseDataEngineIndex, state.caseDataThreadIndex, state.caseDataRepeatIndex);
			}
		}
		ImGui::EndCombo();
	}
	ImGui::EndTable();
}

void DrawCaseDataSummary(const ResultViewModel& model)
{
	std::array<NativeValueFormat, kObservationPerCaseCapacity> formats = {};
	for (std::uint32_t observation = 0; observation < model.observationCount; ++observation)
		formats[observation] = ObservationNumberFormat(model, observation);
	for (std::uint32_t group = 0; group < model.resultGroupCount; ++group)
	{
		const std::string_view groupLabel = ResultViewTextView(&model, model.resultGroups[group].label);
		std::array<char, kCatalogTextValueCapacity> groupHeading = {};
		std::snprintf(groupHeading.data(), groupHeading.size(), "%.*s", static_cast<int>(groupLabel.size()),
		              groupLabel.data());
		ImGui::SeparatorText(groupHeading.data());
		std::uint32_t groupObservationCount = 0;
		for (std::uint32_t observation = 0; observation < model.observationCount; ++observation)
			if (model.observationViews[observation].resultGroupOrdinal == group)
				groupObservationCount += 1;
		std::array<char, 64> tableId = {};
		std::snprintf(tableId.data(), tableId.size(), "case_data_summary_%u", group);
		if (!ImGui::BeginTable(tableId.data(), static_cast<int>(groupObservationCount + 3),
		                       ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX |
		                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
		                       ImVec2(0.0f, (std::min)(240 * ImGui::GetStyle().FontScaleDpi, (model.displayRowCount + 3) * ImGui::GetTextLineHeightWithSpacing() + 18 * ImGui::GetStyle().FontScaleDpi))))
			continue;
		ImGui::TableSetupScrollFreeze(2, 1);
		ImGui::TableSetupColumn("Engine", ImGuiTableColumnFlags_WidthFixed, (std::min)(240 * ImGui::GetStyle().FontScaleDpi, ImGui::GetContentRegionAvail().x * 0.38f));
		ImGui::TableSetupColumn("Threads", ImGuiTableColumnFlags_WidthFixed, 80 * ImGui::GetStyle().FontScaleDpi);
		std::array<std::array<char, kCatalogTextValueCapacity * 2 + 4>, kObservationPerCaseCapacity> observationLabels = {};
		for (std::uint32_t observation = 0; observation < model.observationCount; ++observation)
			if (model.observationViews[observation].resultGroupOrdinal == group)
			{
				std::string_view label = ResultViewTextView(&model, model.observationViews[observation].label);
				const std::string_view unit = ResultViewTextView(&model, model.observationViews[observation].unit);
				if (!unit.empty() && label.size() >= unit.size() && label.substr(label.size() - unit.size()) == unit)
				{
					label.remove_suffix(unit.size());
					while (!label.empty() && label.back() == ' ')
						label.remove_suffix(1);
				}
				std::snprintf(observationLabels[observation].data(), observationLabels[observation].size(), "%.*s\n(%s)",
				              static_cast<int>(label.size()), label.data(), FormatNativeUnit(formats[observation], unit).data());
				ImGui::TableSetupColumn(observationLabels[observation].data(), ImGuiTableColumnFlags_WidthFixed);
			}
		ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 90 * ImGui::GetStyle().FontScaleDpi);
		ImGui::TableHeadersRow();
		for (std::uint32_t rowIndex = 0; rowIndex < model.displayRowCount; ++rowIndex)
		{
			const ResultSummaryViewRow& row = model.summaryRows[model.displayRowIndexes[rowIndex]];
			const std::uint32_t threadOrdinal = ResultThreadOrdinal(model, row.threadCount);
			ObservationOutcome groupOutcome = ObservationOutcome_Ok;
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			const ResultEngineView& engine = model.engines[row.engineOrdinal];
			const std::string_view name = ResultViewTextView(&model, engine.provenanceLabel);
			const ImVec2 swatch = ImGui::GetCursorScreenPos();
			const float scale = ImGui::GetStyle().FontScaleDpi;
			ImGui::GetWindowDrawList()->AddRectFilled(swatch, ImVec2(swatch.x + 6 * scale, swatch.y + 10 * scale), ImGui::GetColorU32(ResultEngineColor(engine.colorRgb)));
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 11 * scale);
			ImGui::Text("%.*s", static_cast<int>(name.size()), name.data());
			ImGui::TableNextColumn();
			ImGui::Text("%u", row.threadCount);
			for (std::uint32_t observation = 0; observation < model.observationCount; ++observation)
			{
				const ResultObservationView& view = model.observationViews[observation];
				if (view.resultGroupOrdinal != group)
					continue;
				const ObservationAggregate* aggregate =
				    ObservationAggregateAt(&model.observations, row.engineOrdinal, threadOrdinal, observation);
				ImGui::TableNextColumn();
				if (aggregate != nullptr)
				{
					DrawObservationAggregateCell(model, view, *aggregate, formats[observation]);
					if (aggregate->outcome == ObservationOutcome_Failed)
						groupOutcome = ObservationOutcome_Failed;
					else if ((aggregate->sampleCount == 0 || aggregate->outcome == ObservationOutcome_Unknown) && groupOutcome != ObservationOutcome_Failed)
						groupOutcome = ObservationOutcome_Unknown;
				}
				else
				{
					ImGui::TextDisabled("Unavailable");
					if (groupOutcome != ObservationOutcome_Failed)
						groupOutcome = ObservationOutcome_Unknown;
				}
			}
			ImGui::TableNextColumn();
			const std::string_view status = ObservationOutcomeText(groupOutcome);
			ImGui::TextUnformatted(status.data(), status.data() + status.size());
		}
		ImGui::EndTable();
	}
}

const ObservationDetailRecord& FindObservationDetail(const ObservationDetailProjection& projection,
                                                     std::uint32_t declarationOrdinal, std::uint32_t sampleIndex)
{
	std::uint32_t first = 0;
	std::uint32_t end = projection.rowCount;
	while (first < end)
	{
		const std::uint32_t middle = first + (end - first) / 2;
		const ObservationDetailRecord& row = projection.rows[middle];
		if (row.declarationOrdinal < declarationOrdinal ||
		    (row.declarationOrdinal == declarationOrdinal && row.sampleIndex < sampleIndex))
			first = middle + 1;
		else
			end = middle;
	}
	return projection.rows[first];
}

void DrawObservationDetailCell(const ResultViewModel& model, const ResultObservationView& observation,
                               const ObservationDetailRecord& detail, NativeValueFormat format)
{
	const NativeValueText value = FormatObservationValue(observation, detail.actual, format);
	if (detail.outcome == ObservationOutcome_Failed)
		ImGui::TextColored(ImVec4(0.95f, 0.48f, 0.38f, 1), "%s / %s  Failed", value.data(), FormatObservationValue(observation, detail.expected, format).data());
	else
		ImGui::TextUnformatted(value.data());
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
	{
		const std::string_view unit = ResultViewTextView(&model, observation.unit);
		if (observation.expectedValuePresence == PresenceStatus_Present)
		{
			const NativeValueText expected = FormatObservationValue(observation, detail.expected, format, PresenceStatus_Present);
			ImGui::SetTooltip("Actual: %s\nExpected: %s\nUnit: %.*s\nStatus: %.*s", FormatObservationValue(observation, detail.actual, format, PresenceStatus_Present).data(), expected.data(),
			                  static_cast<int>(unit.size()), unit.data(),
			                  static_cast<int>(ObservationOutcomeText(detail.outcome).size()),
			                  ObservationOutcomeText(detail.outcome).data());
		}
		else
			ImGui::SetTooltip("Actual: %s\nUnit: %.*s\nStatus: %.*s", FormatObservationValue(observation, detail.actual, format, PresenceStatus_Present).data(), static_cast<int>(unit.size()),
			                  unit.data(), static_cast<int>(ObservationOutcomeText(detail.outcome).size()),
			                  ObservationOutcomeText(detail.outcome).data());
	}
}

void DrawCaseDataDetail(const ResultViewModel& model)
{
	if (model.observationDetail.availability != AvailabilityStatus_Available)
		return;
	std::array<NativeValueFormat, kObservationPerCaseCapacity> formats = {};
	for (std::uint32_t observation = 0; observation < model.observationCount; ++observation)
		formats[observation] = ObservationNumberFormat(model, observation);
	const std::string_view workUnit = ResultViewTextView(&model, model.workUnitLabel);
	std::array<char, kCatalogTextValueCapacity> workUnitLabel = {};
	std::snprintf(workUnitLabel.data(), workUnitLabel.size(), "%.*s", static_cast<int>(workUnit.size()),
	              workUnit.data());
	for (std::uint32_t group = 0; group < model.resultGroupCount; ++group)
	{
		std::uint32_t firstObservation = model.observationCount;
		std::uint32_t groupObservationCount = 0;
		for (std::uint32_t observation = 0; observation < model.observationCount; ++observation)
			if (model.observationViews[observation].resultGroupOrdinal == group)
			{
				if (firstObservation == model.observationCount)
					firstObservation = observation;
				groupObservationCount += 1;
			}
		const std::string_view groupLabel = ResultViewTextView(&model, model.resultGroups[group].label);
		std::array<char, 256> heading = {};
		std::snprintf(heading.data(), heading.size(), "%.*s detail", static_cast<int>(groupLabel.size()),
		              groupLabel.data());
		ImGui::SeparatorText(heading.data());
		std::uint32_t firstRow = 0;
		while (firstRow < model.observationDetail.rowCount &&
		       model.observationDetail.rows[firstRow].declarationOrdinal < firstObservation)
			firstRow += 1;
		std::uint32_t endRow = firstRow;
		while (endRow < model.observationDetail.rowCount &&
		       model.observationDetail.rows[endRow].declarationOrdinal == firstObservation)
			endRow += 1;
		std::array<char, 64> tableId = {};
		std::snprintf(tableId.data(), tableId.size(), "case_data_detail_%u", group);
		if (!ImGui::BeginTable(tableId.data(), static_cast<int>(groupObservationCount + 1),
		                       ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX |
		                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
		                       ImVec2(0.0f, (std::min)(240 * ImGui::GetStyle().FontScaleDpi, (endRow - firstRow + 3) * ImGui::GetTextLineHeightWithSpacing() + 18 * ImGui::GetStyle().FontScaleDpi))))
			continue;
		ImGui::TableSetupScrollFreeze(1, 1);
		ImGui::TableSetupColumn(workUnitLabel.data(), ImGuiTableColumnFlags_WidthFixed, 120 * ImGui::GetStyle().FontScaleDpi);
		std::array<std::array<char, kCatalogTextValueCapacity * 2 + 4>, kObservationPerCaseCapacity> observationLabels = {};
		for (std::uint32_t observation = 0; observation < model.observationCount; ++observation)
			if (model.observationViews[observation].resultGroupOrdinal == group)
			{
				std::string_view label = ResultViewTextView(&model, model.observationViews[observation].label);
				const std::string_view unit = ResultViewTextView(&model, model.observationViews[observation].unit);
				if (!unit.empty() && label.size() >= unit.size() && label.substr(label.size() - unit.size()) == unit)
				{
					label.remove_suffix(unit.size());
					while (!label.empty() && label.back() == ' ')
						label.remove_suffix(1);
				}
				std::snprintf(observationLabels[observation].data(), observationLabels[observation].size(), "%.*s\n(%s)",
				              static_cast<int>(label.size()), label.data(), FormatNativeUnit(formats[observation], unit).data());
				ImGui::TableSetupColumn(observationLabels[observation].data(), ImGuiTableColumnFlags_WidthFixed);
			}
		ImGui::TableHeadersRow();
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(endRow - firstRow));
		while (clipper.Step())
		{
			for (int visible = clipper.DisplayStart; visible < clipper.DisplayEnd; ++visible)
			{
				const std::uint32_t row = firstRow + static_cast<std::uint32_t>(visible);
				const ObservationDetailRecord& schedule = model.observationDetail.rows[row];
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%u", schedule.sampleIndex);
				for (std::uint32_t observation = 0; observation < model.observationCount; ++observation)
				{
					const ResultObservationView& view = model.observationViews[observation];
					if (view.resultGroupOrdinal != group)
						continue;
					ImGui::TableNextColumn();
					const ObservationDetailRecord& detail =
					    FindObservationDetail(model.observationDetail, observation, schedule.sampleIndex);
					DrawObservationDetailCell(model, view, detail, formats[observation]);
				}
			}
		}
		ImGui::EndTable();
	}
}

void SavedRunFact(const char* label, std::string_view value)
{
	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	const float baseline = ImGui::GetCursorPosY() + ImGui::GetFontBaked()->Ascent;
	ImGui::TextDisabled("%s", label);
	ImGui::TableNextColumn();
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 11);
	ImGui::SetCursorPosY(baseline - ImGui::GetFontBaked()->Ascent);
	ImGui::TextWrapped("%.*s", static_cast<int>(value.size()), value.data());
	ImGui::PopFont();
}

void DrawSavedRunFacts(PhysicsArenaApp* app, PresenceStatus includeCase)
{
	const ResultViewModel& model = app->workspace.finalization.model;
	NativeRunLabelProjection runLabel = {};
	ProjectNativeRunLabel(ResultViewTextView(&model, model.runId), model.threadCount, model.repeatCount, &runLabel);
	if (ImGui::BeginTable("result_metadata", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
	{
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100 * app->platform.dpiScale);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
		SavedRunFact("Run", runLabel.text.data());
		if (includeCase == PresenceStatus_Present)
			SavedRunFact("Case", ResultViewTextView(&model, model.caseDisplayName));
		SavedRunFact("Processor", HostTextView(&model.host, model.host.cpuModel));
		if (includeCase == PresenceStatus_Present)
			SavedRunFact("Mode", ResultViewTextView(&model, model.runProvenance));
		SavedRunFact("Verification", model.verificationMode == VerificationMode_Off ? "Off: physical quality not checked" : "On");
		SavedRunFact("Repeats", FormatNativeCount(model.repeatCount).data());
		SavedRunFact("Engines", FormatNativeCount(model.engineCount).data());
		SavedRunFact("Threads", FormatNativeThreadSet(model.threadCounts.data(), model.threadCount).data());
		if (includeCase == PresenceStatus_Present && model.renderResolutionPresence == PresenceStatus_Present)
		{
			NativeValueText resolution = {};
			std::snprintf(resolution.data(), resolution.size(), "%u x %u px", model.renderWidthPixels, model.renderHeightPixels);
			SavedRunFact("Resolution", resolution.data());
		}
		ImGui::EndTable();
	}
}

void DrawSavedRunTechnicalDetails(PhysicsArenaApp* app)
{
	const ResultViewModel& model = app->workspace.finalization.model;
	const std::string_view runId = ResultViewTextView(&model, model.runId);
	ImGui::TextDisabled("Run ID");
	ImGui::BeginChild("run_id_text", ImVec2(0, 48 * app->platform.dpiScale), ImGuiChildFlags_Borders,
	    ImGuiWindowFlags_HorizontalScrollbar);
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 11);
	ImGui::TextUnformatted(runId.data(), runId.data() + runId.size());
	ImGui::PopFont();
	ImGui::EndChild();
	if (ImGui::Button("Copy run ID"))
	{
		std::array<char, kCatalogTextValueCapacity + 1> text = {};
		std::snprintf(text.data(), text.size(), "%.*s", static_cast<int>(runId.size()), runId.data());
		ImGui::SetClipboardText(text.data());
	}
	ImGui::TextDisabled("Saved schema: %u", model.resultSchemaVersion);
	ImGui::TextWrapped("Configuration: %s", ResultViewConfiguration(&model) != nullptr ? "Saved with this run" : "Not saved. Descriptions are current catalog context");
}

void DrawResultMetadata(PhysicsArenaApp* app)
{
	DrawSavedRunFacts(app, PresenceStatus_Present);
	if (ImGui::CollapsingHeader("Technical details"))
		DrawSavedRunTechnicalDetails(app);
}

void DrawCaseData(PhysicsArenaApp* app)
{
	const ResultViewModel& model = app->workspace.finalization.model;
	NativeResultsState& state = app->results;
	if (model.verificationMode == VerificationMode_Off)
		ImGui::TextWrapped("Verification Off: physical quality not checked");
	int page = static_cast<int>(state.caseDataPage);
	ImGui::SetNextItemWidth(-1);
	const int pageChanged = ImGui::Combo("##case_data_page", &page, "About benchmark\0Configuration\0Observations\0Provenance\0");
	if (pageChanged != 0)
		state.caseDataPage = static_cast<NativeCaseDataPage>(page);
	ImGui::BeginChild("case_data_content", ImVec2(0, 0));
	if (pageChanged != 0)
		ImGui::SetScrollY(0);
	if (state.caseDataPage == NativeCaseDataPage_About)
		DrawSavedCaseExplanation(app);
	else if (state.caseDataPage == NativeCaseDataPage_Configuration)
		DrawSavedCaseConfiguration(app);
	else if (state.caseDataPage == NativeCaseDataPage_Provenance)
	{
		DrawResultMetadata(app);

	}
	else if (ResultViewTextView(&model, model.workUnitId) == "ray_frame")
	{
		DrawCaseDataControls(app);
		DrawRayCaseData(app);
	}
	else if (model.resultGroupCount == 0)
		ImGui::TextDisabled("No case observations recorded by this run");
	else
	{
		if (ImGui::CollapsingHeader("Summary across all repeats", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextWrapped("All saved engines, thread counts and repeats. The controls below select only repeat samples. Validation cells show actual / expected totals. Descriptive cells show minimum - maximum");
			DrawCaseDataSummary(model);
		}
		ImGui::SeparatorText("Selected repeat samples");
		DrawCaseDataControls(app);
		DrawCaseDataDetail(model);
	}
	ImGui::EndChild();
}
}
