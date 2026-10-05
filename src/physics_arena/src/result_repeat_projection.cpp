#include "physics_arena/result_view_model.h"
#include "result_pipeline_internal.h"

#include <memory>
#include <new>

namespace physics_arena
{
struct RepeatProjectionContext
{
	NormalizedValidationContext validation;
	ResultRepeatProjection projection;
	std::string_view engineId;
	PresenceStatus headerValidated;
};

ArenaStatus ReadRepeatProjectionRow(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error)
{
	RepeatProjectionContext* context = static_cast<RepeatProjectionContext*>(opaque);
	if (context->headerValidated != PresenceStatus_Present)
	{
		if (header->fieldCount != kNormalizedColumns.size())
			return PipelineError(error, ArenaStatus_InvalidResult, "repeat_projection_header");
		for (std::uint32_t index = 0; index < header->fieldCount; ++index)
			if (CsvHeaderTextView(header, header->fields[index]) != kNormalizedColumns[index])
				return PipelineError(error, ArenaStatus_InvalidResult, "repeat_projection_header");
		context->headerValidated = PresenceStatus_Present;
	}
	if (CsvRowTextView(row, row->fields[1]) != context->engineId)
		return ArenaStatus_Ok;
	std::uint32_t thread = 0;
	if (ParseUnsigned(CsvRowTextView(row, row->fields[7]), &thread) != ArenaStatus_Ok)
		return PipelineError(error, ArenaStatus_InvalidResult, "repeat_projection_thread");
	if (thread != context->projection.threadCount)
		return ArenaStatus_Ok;
	if (ValidateNormalizedRow(header, row, &context->validation, error) != ArenaStatus_Ok)
		return error->code;
	std::uint32_t repeat = 0;
	ParseUnsigned(CsvRowTextView(row, row->fields[11]), &repeat);
	ResultRepeatRow& output = context->projection.rows[repeat];
	output.repeatIndex = repeat;
	output.measurement = PresenceStatus_Present;
	output.outcome = CsvRowTextView(row, row->fields[23]) == "ok" ? ObservationOutcome_Ok : ObservationOutcome_Failed;
	ParseUnsigned(CsvRowTextView(row, row->fields[48]), &output.completedWorkUnitCount);
	ParseUnsigned64(CsvRowTextView(row, row->fields[22]), &output.invalidTransformCount);
	if (output.invalidTransformCount != 0)
		output.outcome = ObservationOutcome_Failed;
	if (context->projection.measurementMode == ResultMeasurementMode_Timed)
	{
		ParsePositiveDouble(CsvRowTextView(row, row->fields[17]), &output.primaryValue);
		ParsePositiveDouble(CsvRowTextView(row, row->fields[20]), &output.meanWorkUnitMilliseconds);
		ParsePositiveDouble(CsvRowTextView(row, row->fields[21]), &output.workUnitsPerSecond);
		ParsePositiveDouble(CsvRowTextView(row, row->fields[49]), &output.workloadElapsedMilliseconds);
	}
	++context->projection.rowCount;
	return ArenaStatus_Ok;
}

ArenaStatus ProjectResultRepeats(const wchar_t* normalizedPath, const Catalog* catalog,
                                 const ResultManifestRecord* manifest, std::uint32_t engineOrdinal,
                                 std::uint32_t threadCount, ResultRepeatProjection* projection, StatusRecord* error, const ResultViewModel* outcomeModel)
{
	*projection = {};
	*error = {};
	if (engineOrdinal >= manifest->engineCount || manifest->repeatCount == 0 ||
	    manifest->repeatCount > kRunRepeatCapacity)
		return PipelineError(error, ArenaStatus_InvalidArgument, "repeat_projection_identity");
	std::uint32_t threadOrdinal = 0;
	while (threadOrdinal < manifest->threadCount && manifest->threadCounts[threadOrdinal] != threadCount)
		++threadOrdinal;
	if (threadOrdinal == manifest->threadCount)
		return PipelineError(error, ArenaStatus_InvalidArgument, "repeat_projection_identity");
	std::unique_ptr<RepeatProjectionContext> context(new (std::nothrow) RepeatProjectionContext{});
	if (context == nullptr)
		return PipelineError(error, ArenaStatus_RunFailed, "repeat_projection_allocation");
	context->validation.catalog = catalog;
	context->validation.manifest = manifest;
	context->projection.engineOrdinal = engineOrdinal;
	context->projection.threadCount = threadCount;
	context->projection.measurementMode = manifest->measurementMode;
	context->engineId = CatalogTextView(catalog, catalog->engines[manifest->engines[engineOrdinal].engineIndex].id);
	CsvHeader header = {};
	CsvReadRecord record = {};
	if (ReadCsvFile(normalizedPath, &header, ReadRepeatProjectionRow, context.get(), &record, error) != ArenaStatus_Ok)
		return error->code;
	for (std::uint32_t repeat = 0; repeat < manifest->repeatCount; ++repeat)
	{
		ResultRepeatRow& row = context->projection.rows[repeat];
		row.repeatIndex = repeat;
		const ExecutionFailure* failure = FindExecutionFailure(manifest->executionFailures,
		    manifest->engines[engineOrdinal].engineIndex, threadCount, repeat);
		if (row.measurement == PresenceStatus_Absent && failure == nullptr)
			return PipelineError(error, ArenaStatus_InvalidResult, "repeat_projection_missing_repeat");
		if (failure != nullptr)
		{
			row.outcome = ObservationOutcome_Failed;
			row.disposition = failure->outcome == ExecutionOutcome_NotRun ? ResultRepeatDisposition_Skipped : ResultRepeatDisposition_ExecutionFailed;
			if (row.disposition == ResultRepeatDisposition_Skipped && row.measurement == PresenceStatus_Present)
				return PipelineError(error, ArenaStatus_InvalidResult, "repeat_projection_skipped_measurement");
		}
	}
	if (outcomeModel != nullptr)
	{
		if (outcomeModel->engineCount != manifest->engineCount || outcomeModel->repeatCount != manifest->repeatCount ||
		    ResultViewTextView(outcomeModel, outcomeModel->runId) != ResultTextView(manifest, manifest->runId))
			return PipelineError(error, ArenaStatus_InvalidArgument, "repeat_projection_outcome_identity");
		const CaseRecord& benchmarkCase = ResultViewCaseDefinition(catalog, outcomeModel);
		for (std::uint32_t repeat = 0; repeat < manifest->repeatCount; ++repeat)
		{
			ResultRepeatRow& row = context->projection.rows[repeat];
			if (row.measurement == PresenceStatus_Absent) continue;
			StackAssessment fall = StackAssessment_Unassessed;
			for (const StackStabilityResult& stability : outcomeModel->stabilityResults)
				if (stability.engineId == context->engineId && stability.threadCount == threadCount && stability.repeatIndex == repeat)
					fall = StackQualificationAssessment(stability, outcomeModel->requiredStabilityCriterion);
			if (fall == StackAssessment_Fail) row.outcome = ObservationOutcome_Failed;
			else if (outcomeModel->stabilityRequired == PresenceStatus_Present && fall == StackAssessment_Unassessed &&
			         row.outcome != ObservationOutcome_Failed) row.outcome = ObservationOutcome_Unknown;
			if (outcomeModel->observationCount == 0) continue;
			const ObservationDetailSelection selection = {manifest->engines[engineOrdinal].engineIndex, threadCount, repeat};
			ObservationDetailProjection detail = {};
			if (ProjectObservationDetail(&outcomeModel->observations, catalog, &benchmarkCase, &selection, &detail,
			    error, ResultViewConfiguration(outcomeModel)) != ArenaStatus_Ok) return error->code;
			for (const ObservationDetailRecord& observation : std::span(detail.rows).first(detail.rowCount))
				if (observation.outcome == ObservationOutcome_Failed) row.outcome = ObservationOutcome_Failed;
		}
	}
	if (manifest->verificationMode == VerificationMode_Off)
		for (std::uint32_t repeat = 0; repeat < manifest->repeatCount; ++repeat)
			if (context->projection.rows[repeat].outcome != ObservationOutcome_Failed)
				context->projection.rows[repeat].outcome = ObservationOutcome_Unknown;
	context->projection.rowCount = manifest->repeatCount;
	*projection = context->projection;
	return ArenaStatus_Ok;
}
}
