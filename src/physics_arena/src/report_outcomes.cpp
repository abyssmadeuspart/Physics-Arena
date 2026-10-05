#include "report_pipeline_internal.h"
#include "json_contracts_internal.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>

namespace physics_arena
{
const char* StackPhaseName(benchmark_stack::Phase phase)
{
	return phase == benchmark_stack::Phase_Construction ? "construction" : phase == benchmark_stack::Phase_Warmup ? "warmup" : "measured";
}

ArenaStatus ProjectStackDetails(const ResultViewModel* model, ReportOutcomeInput* output, StatusRecord* error)
{
	if (model->stabilityRequired != PresenceStatus_Present) return ArenaStatus_Ok;
	output->tupleLines.reserve(model->summaryRowCount);
	for (std::uint32_t index = 0; index < model->summaryRowCount; ++index)
	{
		const ResultSummaryViewRow& row = model->summaryRows[index];
		if (row.stability == StackAssessment_Pass && model->requiredStabilityCriterion != StackCriterion_ContactIslandsShapePreservation && model->requiredStabilityCriterion != StackCriterion_PyramidUnforcedShape) continue;
		const ResultEngineView& engine = model->engines[row.engineOrdinal];
		const std::string_view engineId = ResultViewTextView(model, engine.id);
		const StackStabilityResult* determining = nullptr;
		std::uint64_t passingRepeats = 0;
		for (const StackStabilityResult& candidate : model->stabilityResults)
		{
			if (candidate.engineId != engineId || candidate.threadCount != row.threadCount || candidate.repeatIndex >= model->repeatCount)
				continue;
			if (row.stability == StackAssessment_Unassessed)
			{
				if (StackQualificationAssessment(candidate, model->requiredStabilityCriterion) == StackAssessment_Pass && candidate.coverage == StackCoverage_Complete)
					passingRepeats |= UINT64_C(1) << candidate.repeatIndex;
				else if (determining == nullptr || candidate.repeatIndex < determining->repeatIndex)
					determining = &candidate;
			}
			else if (StackQualificationAssessment(candidate, model->requiredStabilityCriterion) == row.stability &&
			         (determining == nullptr || candidate.repeatIndex < determining->repeatIndex))
				determining = &candidate;
		}
		ReportOutcomeLine line = {};
		line.rowIndex = index;
		const std::string_view name = ResultViewTextView(model, engine.displayName);
		ArenaStatus status = ArenaStatus_Ok;
		if (row.stability == StackAssessment_Unassessed)
		{
			std::uint32_t repeat = 0;
			while (repeat < model->repeatCount && (passingRepeats & (UINT64_C(1) << repeat)) != 0)
				++repeat;
			const char* reason = "saved trajectory evidence unavailable";
			if (determining != nullptr && determining->repeatIndex == repeat)
			{
				reason = determining->reason.c_str();
				if (determining->criterion == StackCriterion_LegacyMovement)
					reason = "legacy movement assessment, fall status unassessed";
				else if (model->requiredStabilityCriterion == StackCriterion_PyramidUnforcedShape && determining->criterion != StackCriterion_PyramidUnforcedShape)
					reason = "historical fall-check outcome, current unforced 10 cm shape unassessed";
				else if (model->requiredStabilityCriterion == StackCriterion_ContactIslandsShapePreservation)
				{
					if (determining->criterion == StackCriterion_SupportPlane)
						reason = "historical fall-check pass, current safety and shape unassessed";
					else if (determining->criterion == StackCriterion_ContactIslandsStabilization)
						reason = determining->assessment == StackAssessment_Pass
						    ? "historical 2 cm policy passed, current safety and 10 cm shape unassessed"
						    : determining->assessment == StackAssessment_Fail
						    ? "historical 2 cm policy failed, current safety and 10 cm shape unassessed"
						    : "historical 2 cm policy unassessed, current safety and 10 cm shape unassessed";
					else if (determining->criterion == StackCriterion_ContactIslandsStabilization10cm)
						reason = determining->assessment == StackAssessment_Pass
						    ? "historical 10 cm settling policy passed, current safety and 10 cm shape unassessed"
						    : determining->assessment == StackAssessment_Fail
						    ? "historical 10 cm settling policy failed, current safety and 10 cm shape unassessed"
						    : "historical 10 cm settling policy unassessed, current safety and 10 cm shape unassessed";
				}
			}
			status = FormatCell(&line.text, "%.*s t%u r%u: unassessed (%s)", static_cast<int>(name.size()), name.data(), row.threadCount,
			    repeat, reason);
		}
		else
		{
			const StackStabilityResult& result = *determining;
			const char* rule = result.firstRule == StackRule_SupportPlane ? "box fell below current support plane" :
			    result.firstRule == StackRule_ContainerEscape ? "box escaped upward above container rim" :
			    result.firstRule == StackRule_TerminalSlotEnvelope ? "terminal slot-envelope excess" :
			    result.firstRule == StackRule_TerminalAdjacentCompression ? "terminal assigned-neighbour compression" :
			    result.firstRule == StackRule_TerminalMotion ? "terminal sampled motion" :
			    result.firstRule == StackRule_UnforcedSlotEnvelope ? "unforced slot-envelope excess" :
			    result.firstRule == StackRule_UnforcedAdjacentCompression ? "unforced assigned-neighbour compression" :
			    result.firstRule == StackRule_None ? ((result.criterion == StackCriterion_ContactIslandsShapePreservation || result.criterion == StackCriterion_PyramidUnforcedShape) ? "safety and shape passed" : "settling and shape passed") : "invalid body state";
			if (row.stability == StackAssessment_Pass)
				status = FormatCell(&line.text, "%.*s t%u r%u: %s", static_cast<int>(name.size()), name.data(), row.threadCount, result.repeatIndex, rule);
			else
				status = FormatCell(&line.text,
				    "%.*s t%u r%u: %s, body %u at s%u %s %u, assessed s%u %s %u to s%u %s %u",
				    static_cast<int>(name.size()), name.data(), row.threadCount, result.repeatIndex, rule, result.firstBody,
				    result.firstBreach.segment, StackPhaseName(result.firstBreach.phase), result.firstBreach.step,
				    result.firstSample.segment, StackPhaseName(result.firstSample.phase), result.firstSample.step,
				    result.lastSample.segment, StackPhaseName(result.lastSample.phase), result.lastSample.step);
			if (status == ArenaStatus_Ok && result.impactPresence == PresenceStatus_Present)
			{
				ReportCell cutoff = {};
				status = FormatCell(&cutoff, ", excluded possible impact interval ending s%u %s %u (%.3f ms samples)", result.impactIntervalEnd.segment,
				    StackPhaseName(result.impactIntervalEnd.phase), result.impactIntervalEnd.step, result.sampleSeconds * 1000);
				if (status == ArenaStatus_Ok) status = AppendCellText(&line.text, CellView(cutoff));
			}
		}
		if (status == ArenaStatus_Ok && determining != nullptr && determining->criterion == StackCriterion_PyramidUnforcedShape)
		{
			const StackStabilityResult& result = *determining;
			const char* window = model->savedConfiguration.benchmarkCase.fixtureKind == CaseFixtureKind_LargePyramid
			    ? "before possible impact" : "full unforced run";
			ReportCell shape = {};
			if (result.unforcedShapePresence == PresenceStatus_Present)
				status = FormatCell(&shape, ", shape excess %.4f m (%.2f cm), limit 0.10 m (10 cm), body %u at s%u %s %u, %s",
				    result.unforcedStackShapeExcess.value, result.unforcedStackShapeExcess.value * 100,
				    result.unforcedStackShapeExcess.body, result.unforcedStackShapeExcess.sample.segment,
				    StackPhaseName(result.unforcedStackShapeExcess.sample.phase), result.unforcedStackShapeExcess.sample.step, window);
			else status = FormatCell(&shape, ", unforced shape evidence unavailable, limit 0.10 m (10 cm)");
			if (status == ArenaStatus_Ok) status = AppendCellText(&line.text, CellView(shape));
		}
		if (status == ArenaStatus_Ok && determining != nullptr &&
		    (determining->criterion == StackCriterion_ContactIslandsStabilization || determining->criterion == StackCriterion_ContactIslandsStabilization10cm ||
	     determining->criterion == StackCriterion_ContactIslandsShapePreservation))
		{
			const StackStabilityResult& result = *determining;
			ReportCell terminal = {};
			if (result.criterion == StackCriterion_ContactIslandsShapePreservation)
			{
				if (result.terminalMetricsPresence == PresenceStatus_Present)
				{
					const std::uint32_t terminalStart = model->measuredWorkUnitCount - model->timestepHz;
					if (result.coverage == StackCoverage_Incomplete)
						status = FormatCell(&terminal, ". partial final 1s measured window %u-%u, observed %u-%u: shape %.9g m, limit %.9g m%s",
						    terminalStart, model->measuredWorkUnitCount, terminalStart, result.lastSample.step,
						    result.terminalStackShapeExcess.value, result.margin,
						    result.firstRule >= StackRule_TerminalSlotEnvelope ? ", breach first observed in terminal window" : "");
					else
						status = FormatCell(&terminal, ". final 1s measured window %u-%u: shape %.9g m, limit %.9g m%s",
						    terminalStart, model->measuredWorkUnitCount, result.terminalStackShapeExcess.value, result.margin,
						    result.firstRule >= StackRule_TerminalSlotEnvelope ? ", breach first observed in terminal window" : "");
				}
				else status = FormatCell(&terminal, ". final 1s shape evidence unavailable");
			}
			else if (result.terminalMetricsPresence == PresenceStatus_Present)
				status = FormatCell(&terminal, ". historical final 1s measured window %u-%u: settling %.9g m/step, limit %.9g m/step, shape %.9g m, limit %.9g m%s",
				    result.lastSample.step - static_cast<std::uint32_t>(std::llround(1 / result.sampleSeconds)), result.lastSample.step,
				    result.settlingStepDisplacement.value, ContactIslandsSettlingStepLimit(result),
				    result.terminalStackShapeExcess.value, result.margin,
				    result.firstRule >= StackRule_TerminalSlotEnvelope ? ", breach first observed in terminal window" : "");
			else status = FormatCell(&terminal, ". historical final 1s settling and shape evidence unavailable");
			if (status == ArenaStatus_Ok) status = AppendCellText(&line.text, CellView(terminal));
		}
		if (status != ArenaStatus_Ok) return ReportError(error, ArenaStatus_InvalidResult, "stability_report_detail_capacity");
		output->tupleLines.push_back(line);
	}
	return ArenaStatus_Ok;
}

ArenaStatus ProjectTerminalDetails(const ResultViewModel* model, ReportOutcomeInput* output, StatusRecord* error)
{
	for (std::uint32_t index = 0; index < model->summaryRowCount; ++index)
	{
		const ResultSummaryViewRow& row = model->summaryRows[index];
		const ResultEngineView& engine = model->engines[row.engineOrdinal];
		const ExecutionFailure* determining = nullptr;
		std::uint32_t skipped = 0;
		for (const ExecutionFailure& failure : model->executionFailures)
		{
			if (failure.engineIndex != engine.catalogEngineIndex || failure.threadCount != row.threadCount) continue;
			skipped += failure.outcome == ExecutionOutcome_NotRun;
			if (determining == nullptr || failure.repeatIndex < determining->repeatIndex) determining = &failure;
		}
		if (determining == nullptr) continue;
		ReportOutcomeLine line = {};
		line.rowIndex = index;
		const std::string_view name = ResultViewTextView(model, engine.displayName);
		if (FormatCell(&line.text, "%.*s t%u: %u/%u measured repeats, %u skipped. %s%s",
		    static_cast<int>(name.size()), name.data(), row.threadCount, row.repeatCount, model->repeatCount, skipped,
		    row.repeatCount == 0 && skipped != 0 ? "Skipped after failed repeat. Measurements unavailable. " : "",
		    determining->detail.data()) != ArenaStatus_Ok)
			return ReportError(error, ArenaStatus_InvalidResult, "terminal_report_detail_capacity");
		output->tupleLines.push_back(line);
	}
	return ArenaStatus_Ok;
}

ArenaStatus LoadReportOutcomes(const wchar_t* resultDirectory, const ResultViewModel* model, ReportOutcomeInput* output,
                               StatusRecord* error)
{
	*output = {};
	if (ProjectStackDetails(model, output, error) != ArenaStatus_Ok ||
	    ProjectTerminalDetails(model, output, error) != ArenaStatus_Ok) return error->code;
	std::array<wchar_t, kReportPathCapacity> path = {};
	if (ChildPath(resultDirectory, L"report-outcomes.json", &path, error) != ArenaStatus_Ok)
		return error->code;
	HANDLE file =
	    CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return GetLastError() == ERROR_FILE_NOT_FOUND
		           ? ArenaStatus_Ok
		           : ReportError(error, ArenaStatus_InvalidResult, "report_outcomes_open");
	constexpr std::size_t kOutcomeFileCapacity = 65536;
	LARGE_INTEGER size = {};
	if (GetFileSizeEx(file, &size) == 0 || size.QuadPart <= 0 ||
	    size.QuadPart > static_cast<LONGLONG>(kOutcomeFileCapacity))
	{
		CloseHandle(file);
		return ReportError(error, ArenaStatus_InvalidResult, "report_outcomes_file_capacity");
	}
	std::array<char, kOutcomeFileCapacity> bytes;
	DWORD count = 0;
	const DWORD expected = static_cast<DWORD>(size.QuadPart);
	const int read = ReadFile(file, bytes.data(), expected, &count, nullptr);
	CloseHandle(file);
	if (read == 0 || count != expected)
		return ReportError(error, ArenaStatus_InvalidResult, "report_outcomes_read");
	const OrderedJson document = OrderedJson::parse(bytes.begin(), bytes.begin() + count, nullptr, false);
	if (document.is_discarded() || !document.is_object() || document.size() != 2 || !document.contains("run_id") ||
	    !document["run_id"].is_string() || !document.contains("failures") || !document["failures"].is_array())
		return ReportError(error, ArenaStatus_InvalidResult, "report_outcomes_shape");
	if (document["run_id"].get_ref<const std::string&>() != ResultViewTextView(model, model->runId))
		return ReportError(error, ArenaStatus_InvalidResult, "report_outcomes_run_id");
	if (document["failures"].size() > kEngineCapacity)
		return ReportError(error, ArenaStatus_InvalidResult, "report_outcomes_engine_capacity");
	if (model->measurementMode == ResultMeasurementMode_PhysicalQuality)
		return ReportError(error, ArenaStatus_InvalidResult, "report_outcomes_quality_unsupported");
	for (const OrderedJson& entry : document["failures"])
	{
		if (!entry.is_object() || entry.size() != 2 || !entry.contains("engine_id") ||
		    !entry["engine_id"].is_string() || !entry.contains("reason") || !entry["reason"].is_string())
			return ReportError(error, ArenaStatus_InvalidResult, "report_outcomes_entry_shape");
		const std::string& id = entry["engine_id"].get_ref<const std::string&>();
		std::uint32_t ordinal = 0;
		while (ordinal < model->engineCount && ResultViewTextView(model, model->engines[ordinal].id) != id)
			++ordinal;
		if (ordinal == model->engineCount)
			return ReportError(error, ArenaStatus_InvalidResult, "report_outcomes_unknown_engine");
		ReportCell& line = output->failureLines[ordinal];
		if (line.size != 0)
			return ReportError(error, ArenaStatus_InvalidResult, "report_outcomes_duplicate_engine");
		const std::string& reason = entry["reason"].get_ref<const std::string&>();
		if (reason.empty() || reason.size() >= kReportCellCapacity ||
		    reason.find_first_not_of(' ') == std::string::npos)
			return ReportError(error, ArenaStatus_InvalidResult, "report_outcomes_reason_capacity");
		for (const unsigned char character : reason)
			if (character < 32 || character == 127)
				return ReportError(error, ArenaStatus_InvalidResult, "report_outcomes_reason_text");
		if (AppendCellText(&line, ResultViewTextView(model, model->engines[ordinal].displayName)) != ArenaStatus_Ok ||
		    AppendCellText(&line, ": ") != ArenaStatus_Ok || AppendCellText(&line, reason) != ArenaStatus_Ok)
			return ReportError(error, ArenaStatus_InvalidResult, "report_outcomes_line_capacity");
	}
	output->presence = PresenceStatus_Present;
	return ArenaStatus_Ok;
}

ObservationOutcome SummaryReportOutcome(const ResultViewModel* model, const ReportOutcomeInput& input,
                                        const ResultSummaryViewRow& row)
{
	if (input.failureLines[row.engineOrdinal].size != 0 || row.outcome == ObservationOutcome_Failed)
		return ObservationOutcome_Failed;
	if (model->verificationMode == VerificationMode_Off)
		return ObservationOutcome_Unknown;
	if (model->stabilityRequired == PresenceStatus_Present)
		return row.stability == StackAssessment_Fail ? ObservationOutcome_Failed :
		    row.stability == StackAssessment_Pass ? ObservationOutcome_Ok : ObservationOutcome_Unknown;
	return row.outcome == ObservationOutcome_Failed || model->resultGroupCount != 0 ? row.outcome : ObservationOutcome_Unknown;
}
}
