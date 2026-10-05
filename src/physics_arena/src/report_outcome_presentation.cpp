#include "report_pipeline_internal.h"

#include "physics_arena/result_pipeline.h"

namespace physics_arena
{
const char* StackPhaseName(benchmark_stack::Phase phase);

std::uint32_t OutcomeTextRows(std::string_view text, double width)
{
	std::uint32_t rows = 0;
	do
	{
		text.remove_prefix(MetaWrapSize(text, width, MetaTextStyle_Value));
		++rows;
	} while (!text.empty());
	return rows;
}

ArenaStatus WriteOutcomeText(ReportWriter* writer, double x, double y, double width,
                             std::string_view text, StatusRecord* error)
{
	do
	{
		const std::size_t size = MetaWrapSize(text, width, MetaTextStyle_Value);
		if (WritePlainMeta(writer, x, y, text.substr(0, size), error) != ArenaStatus_Ok)
			return error->code;
		text.remove_prefix(size);
		y += 18;
	} while (!text.empty());
	return ArenaStatus_Ok;
}

const char* PhysicalFailureText(StackRule rule)
{
	switch (rule)
	{
	case StackRule_SupportPlane: return "Box below support plane";
	case StackRule_ContainerEscape: return "Box escaped above container rim";
	case StackRule_TerminalSlotEnvelope:
	case StackRule_UnforcedSlotEnvelope: return "Box moved beyond its allowed shape";
	case StackRule_TerminalAdjacentCompression:
	case StackRule_UnforcedAdjacentCompression: return "Boxes compressed beyond their allowed spacing";
	case StackRule_TerminalMotion: return "Movement exceeded the saved settling limit";
	default: return "Invalid body state";
	}
}

ArenaStatus BuildPhysicalFailureDetail(const StackStabilityResult& result, ReportCell* detail)
{
	ArenaStatus status = FormatCell(detail, "%u thread%s, repeat %u: %s, body %u at %s step %u",
	    result.threadCount, result.threadCount == 1 ? "" : "s", result.repeatIndex + 1,
	    PhysicalFailureText(result.firstRule), result.firstBody, StackPhaseName(result.firstBreach.phase),
	    result.firstBreach.step);
	if (status != ArenaStatus_Ok) return status;
	if (result.criterion == StackCriterion_PyramidUnforcedShape && result.unforcedShapePresence == PresenceStatus_Present)
		status = AppendCellFormat(detail, ". Shape error %.2f cm, limit %.2f cm, %s",
		    result.unforcedStackShapeExcess.value * 100, result.margin * 100,
		    result.impactPresence == PresenceStatus_Present ? "before possible impact" : "full unforced run");
	else if (result.criterion == StackCriterion_ContactIslandsShapePreservation && result.terminalMetricsPresence == PresenceStatus_Present)
		status = AppendCellFormat(detail, ". Final-second shape error %.2f m, limit %.2f m",
		    result.terminalStackShapeExcess.value, result.margin);
	return status;
}

void CountOutcome(ObservationOutcome outcome, ReportOutcomeCounts* counts)
{
	if (outcome == ObservationOutcome_Ok) ++counts->passed;
	else if (outcome == ObservationOutcome_Failed) ++counts->failed;
	else ++counts->unassessed;
}

ArenaStatus BuildEngineOutcome(const ResultViewModel* model, const ReportOutcomeInput& input,
                               std::uint32_t ordinal, std::uint32_t filterThreadCount,
                               ReportOutcomeEngine* output)
{
	output->engineOrdinal = ordinal;
	const ResultEngineView& engine = model->engines[ordinal];
	const std::string_view id = ResultViewTextView(model, engine.id);
	const StackStabilityResult* firstFailure = nullptr;
	const StackStabilityResult* firstUnassessed = nullptr;
	const ExecutionFailure* firstExecution = nullptr;
	PresenceStatus measurementFailure = PresenceStatus_Absent;
	for (const ResultSummaryViewRow& row : std::span(model->summaryRows).first(model->summaryRowCount))
	{
		if (row.engineOrdinal != ordinal || (filterThreadCount != 0 && row.threadCount != filterThreadCount)) continue;
		if (row.outcome == ObservationOutcome_Failed && row.repeatCount != 0)
			measurementFailure = PresenceStatus_Present;
		if (model->verificationMode == VerificationMode_Off) output->counts.unverified += row.repeatCount;
		if (model->stabilityRequired != PresenceStatus_Present && model->verificationMode == VerificationMode_On)
		{
			std::uint32_t skipped = 0;
			for (const ExecutionFailure& failure : model->executionFailures)
				if (failure.engineIndex == engine.catalogEngineIndex && failure.threadCount == row.threadCount &&
				    failure.outcome == ExecutionOutcome_NotRun) ++skipped;
			if (row.repeatCount == 0 && skipped == model->repeatCount) ++output->counts.skipped;
			else CountOutcome(SummaryReportOutcome(model, input, row), &output->counts);
		}
		for (std::uint32_t repeat = 0; repeat < model->repeatCount; ++repeat)
		{
			const ExecutionFailure* execution = FindExecutionFailure(model->executionFailures,
			    engine.catalogEngineIndex, row.threadCount, repeat);
			if (execution != nullptr)
			{
				if (model->stabilityRequired == PresenceStatus_Present || model->verificationMode == VerificationMode_Off)
				{
					if (execution->outcome == ExecutionOutcome_NotRun) ++output->counts.skipped;
					else ++output->counts.failed;
				}
				if (firstExecution == nullptr ||
				    (execution->outcome == ExecutionOutcome_Failed && firstExecution->outcome != ExecutionOutcome_Failed) ||
				    (execution->outcome == firstExecution->outcome &&
				     (execution->threadCount < firstExecution->threadCount ||
				      (execution->threadCount == firstExecution->threadCount && execution->repeatIndex < firstExecution->repeatIndex))))
					firstExecution = execution;
				continue;
			}
			if (model->verificationMode == VerificationMode_Off || model->stabilityRequired != PresenceStatus_Present) continue;
			const StackStabilityResult* saved = nullptr;
			for (const StackStabilityResult& candidate : model->stabilityResults)
				if (candidate.engineId == id && candidate.threadCount == row.threadCount && candidate.repeatIndex == repeat)
					saved = &candidate;
			const StackAssessment assessment = saved == nullptr ? StackAssessment_Unassessed :
			    StackQualificationAssessment(*saved, model->requiredStabilityCriterion);
			if (assessment == StackAssessment_Fail)
			{
				++output->counts.failed;
				if (firstFailure == nullptr || saved->threadCount < firstFailure->threadCount ||
				    (saved->threadCount == firstFailure->threadCount && saved->repeatIndex < firstFailure->repeatIndex))
					firstFailure = saved;
			}
			else if (assessment == StackAssessment_Pass && saved->coverage == StackCoverage_Complete)
				++output->counts.passed;
			else
			{
				++output->counts.unassessed;
				if (firstUnassessed == nullptr) firstUnassessed = saved;
			}
		}
	}
	output->outcome = output->counts.failed != 0 || input.failureLines[ordinal].size != 0 ||
	    measurementFailure == PresenceStatus_Present ? ObservationOutcome_Failed :
	    output->counts.unassessed != 0 ? ObservationOutcome_Unknown : ObservationOutcome_Ok;
	ArenaStatus status = ArenaStatus_Ok;
	if (input.failureLines[ordinal].size != 0)
		status = SetCell(&output->detail, CellView(input.failureLines[ordinal]));
	else if (firstFailure != nullptr) status = BuildPhysicalFailureDetail(*firstFailure, &output->detail);
	else if (measurementFailure == PresenceStatus_Present &&
	         (firstExecution == nullptr || firstExecution->outcome == ExecutionOutcome_NotRun))
		status = SetCell(&output->detail, "Recorded measurements failed, see saved raw results");
	else if (firstExecution != nullptr)
	{
		status = firstExecution->outcome == ExecutionOutcome_NotRun
		    ? SetCell(&output->detail, "Skipped after failed repeat, measurements unavailable for skipped runs")
		    : FormatCell(&output->detail, "%u thread%s, repeat %u: execution failed (%s)", firstExecution->threadCount,
		        firstExecution->threadCount == 1 ? "" : "s", firstExecution->repeatIndex + 1,
		        ExecutionFailureReasonName(firstExecution->reason));
	}
	else if (output->counts.unassessed != 0)
	{
		const char* reason = firstUnassessed == nullptr ? "Saved trajectory evidence unavailable" :
		    firstUnassessed->criterion != model->requiredStabilityCriterion ? "Saved assessment uses an older criterion" :
		    firstUnassessed->coverage != StackCoverage_Complete ? "Saved trajectory is incomplete" :
		    "Current physical assessment unavailable";
		status = SetCell(&output->detail, reason);
	}
	if (status != ArenaStatus_Ok) return status;
	for (const std::pair<std::uint32_t, const char*> count :
	    {std::pair<std::uint32_t, const char*>{output->counts.passed, "passed"}, {output->counts.failed, "failed"},
	     {output->counts.skipped, "skipped"}, {output->counts.unassessed, "unassessed"}})
	{
		if (count.first == 0) continue;
		if (AppendCellFormat(&output->countText, "%s%u %s", output->countText.size == 0 ? "" : ", ", count.first, count.second) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
	}
	if (output->countText.size == 0 && output->counts.unverified != 0)
		if (FormatCell(&output->countText, "%u unverified", output->counts.unverified) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
	output->height = 16 + 18 * std::max(OutcomeTextRows(CellView(output->detail), 541),
	    OutcomeTextRows(CellView(output->countText), 190));
	return ArenaStatus_Ok;
}

ArenaStatus BuildReportOutcomePresentation(const ResultViewModel* model, const ReportOutcomeInput& input,
                                           std::uint32_t filterEngineOrdinal, std::uint32_t filterThreadCount,
                                           ReportOutcomePresentation* output, StatusRecord* error)
{
	*output = {};
	if (model->verificationMode == VerificationMode_On && model->stabilityRequired != PresenceStatus_Present &&
	    model->resultGroupCount == 0 && input.presence != PresenceStatus_Present && model->executionFailures.empty())
		return ArenaStatus_Ok;
	ReportOutcomeCounts total = {};
	for (std::uint32_t ordinal = 0; ordinal < model->engineCount; ++ordinal)
	{
		if (filterEngineOrdinal < model->engineCount && ordinal != filterEngineOrdinal) continue;
		ReportOutcomeEngine engine = {};
		if (BuildEngineOutcome(model, input, ordinal, filterThreadCount, &engine) != ArenaStatus_Ok)
			return ReportError(error, ArenaStatus_InvalidResult, "report_outcome_presentation_capacity");
		total.passed += engine.counts.passed;
		total.failed += engine.counts.failed;
		total.skipped += engine.counts.skipped;
		total.unassessed += engine.counts.unassessed;
		total.unverified += engine.counts.unverified;
		if (engine.counts.failed + engine.counts.skipped + engine.counts.unassessed != 0 || engine.outcome == ObservationOutcome_Failed)
			output->engines[output->engineCount++] = engine;
	}
	ArenaStatus status = model->verificationMode == VerificationMode_Off
	    ? FormatCell(&output->headline, "%u completed repeats, %u execution failures, %u skipped", total.unverified, total.failed, total.skipped)
	    : FormatCell(&output->headline, "%s: %u passed, %u failed, %u skipped, %u unassessed",
	        model->stabilityRequired == PresenceStatus_Present ? "Repeats" : "Engine/thread configurations",
	        total.passed, total.failed, total.skipped, total.unassessed);
	const char* criterion = model->verificationMode == VerificationMode_Off ? "Verification Off: physical quality not checked" :
	    model->requiredStabilityCriterion == StackCriterion_ContainerEscape ? "Container: whole-box escape above the rim after entry" :
	    model->requiredStabilityCriterion == StackCriterion_ContactIslandsShapePreservation ? "Contact Islands: safety and final-second shape, limit 10% of box edge" :
	    model->requiredStabilityCriterion == StackCriterion_PyramidUnforcedShape ? "Pyramid: safety and unforced shape, limit 10 cm" :
	    "Saved observations, timing retained for completed measurements";
	if (status != ArenaStatus_Ok || SetCell(&output->criterion, criterion) != ArenaStatus_Ok)
		return ReportError(error, ArenaStatus_InvalidResult, "report_outcome_presentation_capacity");
	output->height = 66;
	if (output->engineCount != 0)
	{
		output->height += 28;
		for (const ReportOutcomeEngine& engine : std::span(output->engines).first(output->engineCount)) output->height += engine.height;
	}
	return ArenaStatus_Ok;
}

ArenaStatus WriteReportOutcomeSvg(ReportWriter* writer, double y, const ResultViewModel* model,
                                  const ReportOutcomeInput& input, const ReportOutcomePresentation& presentation,
                                  std::uint32_t filterThreadCount, StatusRecord* error)
{
	if (presentation.height == 0) return ArenaStatus_Ok;
	ArenaStatus status = WriteFormat(writer, error, "<g class=\"outcome-summary\"><text x=\"48\" y=\"%.1f\" class=\"meta-label\">Outcome</text>\n", y + 18);
	if (status == ArenaStatus_Ok) status = WritePlainMeta(writer, 150, y + 18, CellView(presentation.headline), error);
	if (status == ArenaStatus_Ok) status = WritePlainMeta(writer, 150, y + 42, CellView(presentation.criterion), error);
	if (presentation.engineCount != 0)
	{
		if (status == ArenaStatus_Ok) status = WriteFormat(writer, error,
		    "<text x=\"150\" y=\"%.1f\" class=\"meta-label\">Engine</text><text x=\"375\" y=\"%.1f\" class=\"meta-label\">Outcome</text>"
		    "<text x=\"475\" y=\"%.1f\" class=\"meta-label\">Counts</text><text x=\"675\" y=\"%.1f\" class=\"meta-label\">First exception</text>\n",
		    y + 72, y + 72, y + 72, y + 72);
		y += 94;
		for (const ReportOutcomeEngine& engine : std::span(presentation.engines).first(presentation.engineCount))
		{
			if (status != ArenaStatus_Ok) break;
			status = WriteFormat(writer, error, "<g class=\"outcome-engine\"><title>");
			if (status == ArenaStatus_Ok && input.failureLines[engine.engineOrdinal].size != 0)
				status = WriteXmlText(writer, CellView(input.failureLines[engine.engineOrdinal]), error);
			for (const ReportOutcomeLine& line : input.tupleLines)
			{
				const ResultSummaryViewRow& row = model->summaryRows[line.rowIndex];
				if (row.engineOrdinal != engine.engineOrdinal || (filterThreadCount != 0 && row.threadCount != filterThreadCount)) continue;
				if (status == ArenaStatus_Ok) status = WriteXmlText(writer, CellView(line.text), error);
				if (status == ArenaStatus_Ok) status = WriteReport(writer, "\n", error);
			}
			if (status == ArenaStatus_Ok) status = WriteFormat(writer, error,
			    "</title><rect x=\"142\" y=\"%.1f\" width=\"1082\" height=\"%u\" rx=\"4\" fill=\"%s\"/>\n",
			    y - 10, engine.height - 4, engine.outcome == ObservationOutcome_Failed ? "#fff1f2" : "#fff8eb");
			if (status == ArenaStatus_Ok) status = WritePlainMeta(writer, 150, y + 4, ResultViewTextView(model, model->engines[engine.engineOrdinal].displayName), error);
			const char* label = engine.outcome == ObservationOutcome_Failed ? "Failed" :
			    engine.counts.unassessed != 0 ? "Unassessed" : "Skipped";
			if (status == ArenaStatus_Ok) status = WriteFormat(writer, error,
			    "<text x=\"375\" y=\"%.1f\" class=\"meta-label\" style=\"fill:%s\">%s</text>\n",
			    y + 4, engine.outcome == ObservationOutcome_Failed ? "#b91c1c" : "#92400e", label);
			if (status == ArenaStatus_Ok) status = WriteOutcomeText(writer, 475, y + 4, 190, CellView(engine.countText), error);
			if (status == ArenaStatus_Ok) status = WriteOutcomeText(writer, 675, y + 4, 541, CellView(engine.detail), error);
			if (status == ArenaStatus_Ok) status = WriteReport(writer, "</g>\n", error);
			y += engine.height;
		}
	}
	if (status == ArenaStatus_Ok) status = WriteReport(writer, "</g>\n", error);
	return status;
}
}
