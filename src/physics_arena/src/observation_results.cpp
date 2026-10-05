#include "physics_arena/observation_results.h"

#include "physics_arena/csv_io.h"

#include <algorithm>
#include <memory>
#include <array>
#include <charconv>
#include <cmath>
#include <cwchar>
#include <string_view>

namespace physics_arena
{
namespace
{
constexpr std::array<std::string_view, 14> kObservationColumns = {
    "run_id",   "engine_id",    "case_id", "benchmark_mode", "thread_count", "repeat_index",   "metric_id",
    "phase_id", "sample_index", "value",   "unit",           "role",         "expected_value", "validation_status"};

struct ObservationIndexContext
{
	const Catalog* catalog;
	const CaseRecord* benchmarkCase;
	const EffectiveRunConfiguration* configuration;
	const ResultManifestRecord* manifest;
	ObservationResultModel* model;
	std::array<ObservationValue, 10> qualityValues;
	std::array<std::int32_t, kObservationColumns.size()> headerIndexes;
	std::array<std::array<PresenceStatus, kThreadCountCapacity>, kEngineCapacity> seen;
	std::uint32_t rowsPerRepeat;
	PresenceStatus headerMapped;
};

struct ObservationDetailContext
{
	const Catalog* catalog;
	const CaseRecord* benchmarkCase;
	const EffectiveRunConfiguration* configuration;
	const ObservationArtifactSlice* slice;
	const ObservationDetailSelection* selection;
	ObservationDetailProjection* projection;
	std::array<std::int32_t, kObservationColumns.size()> headerIndexes;
	std::uint32_t rowCount;
	PresenceStatus headerMapped;
};

ArenaStatus ObservationError(StatusRecord* error, ArenaStatus status, std::string_view detail)
{
	*error = {};
	const std::string_view component = "observation_results";
	const std::string_view statusText = ArenaStatusText(status);
	std::copy(component.begin(), component.end(), error->component.begin());
	error->componentSize = static_cast<std::uint32_t>(component.size());
	std::copy(statusText.begin(), statusText.end(), error->status.begin());
	error->statusSize = static_cast<std::uint32_t>(statusText.size());
	if (detail.size() > error->detail.size())
		detail = "observation_detail_capacity";
	std::copy(detail.begin(), detail.end(), error->detail.begin());
	error->detailSize = static_cast<std::uint32_t>(detail.size());
	error->code = status;
	return status;
}

ArenaStatus ParseUnsigned(std::string_view text, std::uint32_t* value)
{
	if (text.empty())
		return ArenaStatus_InvalidResult;
	const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), *value);
	return result.ec == std::errc() && result.ptr == text.data() + text.size() ? ArenaStatus_Ok
	                                                                           : ArenaStatus_InvalidResult;
}

ArenaStatus ParseUnsigned64(std::string_view text, std::uint64_t* value)
{
	if (text.empty())
		return ArenaStatus_InvalidResult;
	const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), *value);
	return result.ec == std::errc() && result.ptr == text.data() + text.size() ? ArenaStatus_Ok
	                                                                           : ArenaStatus_InvalidResult;
}

ArenaStatus ParseFiniteDouble(std::string_view text, double* value)
{
	if (text.empty())
		return ArenaStatus_InvalidResult;
	const std::from_chars_result result =
	    std::from_chars(text.data(), text.data() + text.size(), *value, std::chars_format::general);
	return result.ec == std::errc() && result.ptr == text.data() + text.size() && std::isfinite(*value)
	           ? ArenaStatus_Ok
			   : ArenaStatus_InvalidResult;
}

template <typename Context>
ArenaStatus MapObservationHeader(const CsvHeader* header, Context* context, StatusRecord* error)
{
	context->headerIndexes.fill(-1);
	if (header->fieldCount != kObservationColumns.size())
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_header_count");
	for (std::uint32_t column = 0; column < header->fieldCount; ++column)
	{
		const std::string_view name = CsvHeaderTextView(header, header->fields[column]);
		std::uint32_t expected = 0;
		while (expected < kObservationColumns.size() && kObservationColumns[expected] != name)
			++expected;
		if (expected == kObservationColumns.size() || context->headerIndexes[expected] >= 0)
			return ObservationError(error, ArenaStatus_InvalidResult, "observation_header_column");
		context->headerIndexes[expected] = static_cast<std::int32_t>(column);
	}
	for (const std::int32_t index : context->headerIndexes)
		if (index < 0)
			return ObservationError(error, ArenaStatus_InvalidResult, "observation_header_missing");
	context->headerMapped = PresenceStatus_Present;
	return ArenaStatus_Ok;
}

template <typename Context>
std::string_view ObservationRowValue(const CsvRow* row, const Context* context, std::uint32_t field)
{
	return CsvRowTextView(row, row->fields[static_cast<std::uint32_t>(context->headerIndexes[field])]);
}

std::uint32_t RowsPerRepeat(const Catalog* catalog, const CaseRecord* benchmarkCase,
                            const EffectiveRunConfiguration* configuration)
{
	std::uint32_t count = 0;
	for (std::uint32_t declaration = 0; declaration < benchmarkCase->observationCount; ++declaration)
		count += CaseConfigurationObservation(catalog, *benchmarkCase, configuration, declaration).sampleIndexCount;
	return count;
}

const ObservationDeclaration* ExpectedDeclaration(const Catalog* catalog, const CaseRecord* benchmarkCase,
                                                  const EffectiveRunConfiguration* configuration,
                                                  std::uint32_t rowWithinRepeat, std::uint32_t* declarationOrdinal,
                                                  std::uint32_t* sampleOrdinal)
{
	*declarationOrdinal = 0;
	while (*declarationOrdinal < benchmarkCase->observationCount)
	{
		const ObservationDeclaration& declaration =
		    CaseConfigurationObservation(catalog, *benchmarkCase, configuration, *declarationOrdinal);
		if (rowWithinRepeat < declaration.sampleIndexCount)
		{
			*sampleOrdinal = rowWithinRepeat;
			return &declaration;
		}
		rowWithinRepeat -= declaration.sampleIndexCount;
		*declarationOrdinal += 1;
	}
	return nullptr;
}

ArenaStatus ParseObservationValues(const CsvRow* row, const ObservationDeclaration& declaration,
                                   const std::array<std::int32_t, kObservationColumns.size()>& headerIndexes,
                                   ObservationValue* actual, ObservationValue* expected, ObservationOutcome* outcome,
                                   StatusRecord* error)
{
	const std::string_view actualText = CsvRowTextView(row, row->fields[static_cast<std::uint32_t>(headerIndexes[9])]);
	const std::string_view expectedText =
	    CsvRowTextView(row, row->fields[static_cast<std::uint32_t>(headerIndexes[12])]);
	if (declaration.valueType == ObservationValueType_Uint64)
	{
		actual->unsignedValue = 0;
		expected->unsignedValue = 0;
		if (ParseUnsigned64(actualText, &actual->unsignedValue) != ArenaStatus_Ok)
			return ObservationError(error, ArenaStatus_InvalidResult, "observation_value");
		if (declaration.expectedValuePresence == PresenceStatus_Present)
		{
			if (ParseUnsigned64(expectedText, &expected->unsignedValue) != ArenaStatus_Ok ||
			    expected->unsignedValue != declaration.expectedUnsigned)
				return ObservationError(error, ArenaStatus_InvalidResult, "observation_expected_value");
			*outcome =
			    actual->unsignedValue == expected->unsignedValue ? ObservationOutcome_Ok : ObservationOutcome_Failed;
		}
		else if (!expectedText.empty())
			return ObservationError(error, ArenaStatus_InvalidResult, "observation_unexpected_value");
	}
	else if (declaration.valueType == ObservationValueType_Float64)
	{
		actual->float64Value = 0.0;
		expected->float64Value = 0.0;
		if (ParseFiniteDouble(actualText, &actual->float64Value) != ArenaStatus_Ok)
			return ObservationError(error, ArenaStatus_InvalidResult, "observation_value");
		if (declaration.expectedValuePresence == PresenceStatus_Present)
		{
			if (ParseFiniteDouble(expectedText, &expected->float64Value) != ArenaStatus_Ok ||
			    expected->float64Value != declaration.expectedFloat64)
				return ObservationError(error, ArenaStatus_InvalidResult, "observation_expected_value");
			*outcome =
			    actual->float64Value == expected->float64Value ? ObservationOutcome_Ok : ObservationOutcome_Failed;
		}
		else if (!expectedText.empty())
			return ObservationError(error, ArenaStatus_InvalidResult, "observation_unexpected_value");
	}
	else
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_value_type");
	const std::string_view statusText = CsvRowTextView(row, row->fields[static_cast<std::uint32_t>(headerIndexes[13])]);
	if (statusText != ObservationOutcomeText(*outcome))
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_outcome");
	return ArenaStatus_Ok;
}

template <typename Context>
ArenaStatus ValidateObservationIdentity(const CsvRow* row, const Context* context,
                                        const ObservationDeclaration& declaration, std::uint32_t expectedRepeat,
                                        std::uint32_t expectedSample, StatusRecord* error)
{
	std::uint32_t threadCount = 0;
	std::uint32_t repeatIndex = 0;
	std::uint32_t sampleIndex = 0;
	if (ObservationRowValue(row, context, 1) !=
	        CatalogTextView(context->catalog, context->catalog->engines[context->slice->engineIndex].id) ||
	    ObservationRowValue(row, context, 2) !=
	        CaseConfigurationTextView(context->catalog, context->configuration, context->benchmarkCase->id) ||
	    ParseUnsigned(ObservationRowValue(row, context, 4), &threadCount) != ArenaStatus_Ok ||
	    ParseUnsigned(ObservationRowValue(row, context, 5), &repeatIndex) != ArenaStatus_Ok ||
	    ObservationRowValue(row, context, 6) !=
	        CaseConfigurationTextView(context->catalog, context->configuration, declaration.id) ||
	    ObservationRowValue(row, context, 7) !=
	        CaseConfigurationTextView(context->catalog, context->configuration, declaration.phaseId) ||
	    ParseUnsigned(ObservationRowValue(row, context, 8), &sampleIndex) != ArenaStatus_Ok ||
	    ObservationRowValue(row, context, 10) !=
	        CaseConfigurationTextView(context->catalog, context->configuration, declaration.unit) ||
	    ObservationRowValue(row, context, 11) != ObservationRoleWireText(declaration.role) ||
	    threadCount != context->slice->threadCount || repeatIndex != expectedRepeat || sampleIndex != expectedSample)
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_identity");
	return ArenaStatus_Ok;
}

ArenaStatus AccumulateObservation(ObservationAggregate* aggregate, const ObservationDeclaration& declaration,
                                  const ObservationValue& actual, const ObservationValue& expected,
                                  ObservationOutcome outcome, StatusRecord* error)
{
	if (aggregate->sampleCount == 0)
	{
		aggregate->minimumActual = actual;
		aggregate->maximumActual = actual;
		if (declaration.valueType == ObservationValueType_Uint64)
		{
			aggregate->actualTotal.unsignedValue = 0;
			aggregate->expectedTotal.unsignedValue = 0;
		}
		else
		{
			aggregate->actualTotal.float64Value = 0.0;
			aggregate->expectedTotal.float64Value = 0.0;
		}
		aggregate->outcome = ObservationOutcome_Ok;
	}
	if (declaration.valueType == ObservationValueType_Uint64)
	{
		aggregate->minimumActual.unsignedValue =
		    (std::min)(aggregate->minimumActual.unsignedValue, actual.unsignedValue);
		aggregate->maximumActual.unsignedValue =
		    (std::max)(aggregate->maximumActual.unsignedValue, actual.unsignedValue);
		if (UINT64_MAX - aggregate->actualTotal.unsignedValue < actual.unsignedValue ||
		    UINT64_MAX - aggregate->expectedTotal.unsignedValue < expected.unsignedValue)
			return ObservationError(error, ArenaStatus_InvalidResult, "observation_unsigned_overflow");
		aggregate->actualTotal.unsignedValue += actual.unsignedValue;
		aggregate->expectedTotal.unsignedValue += expected.unsignedValue;
	}
	else
	{
		aggregate->minimumActual.float64Value = (std::min)(aggregate->minimumActual.float64Value, actual.float64Value);
		aggregate->maximumActual.float64Value = (std::max)(aggregate->maximumActual.float64Value, actual.float64Value);
		const double actualTotal = aggregate->actualTotal.float64Value + actual.float64Value;
		const double expectedTotal = aggregate->expectedTotal.float64Value + expected.float64Value;
		if (!std::isfinite(actualTotal) || !std::isfinite(expectedTotal))
			return ObservationError(error, ArenaStatus_InvalidResult, "observation_float_overflow");
		aggregate->actualTotal.float64Value = actualTotal;
		aggregate->expectedTotal.float64Value = expectedTotal;
	}
	aggregate->sampleCount += 1;
	if (outcome == ObservationOutcome_Failed)
	{
		aggregate->failedSampleCount += 1;
		aggregate->outcome = ObservationOutcome_Failed;
	}
	return ArenaStatus_Ok;
}

ArenaStatus ConsumeObservationIndexRow(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error)
{
	ObservationIndexContext* context = static_cast<ObservationIndexContext*>(opaque);
	if (context->headerMapped != PresenceStatus_Present &&
	    MapObservationHeader(header, context, error) != ArenaStatus_Ok)
		return error->code;
	std::uint32_t engineOrdinal = 0, threadOrdinal = 0, threadCount = 0, repeatIndex = 0;
	while (engineOrdinal < context->manifest->engineCount && ObservationRowValue(row, context, 1) !=
	       CatalogTextView(context->catalog, context->catalog->engines[context->manifest->engines[engineOrdinal].engineIndex].id))
		++engineOrdinal;
	if (engineOrdinal == context->manifest->engineCount ||
	    ParseUnsigned(ObservationRowValue(row, context, 4), &threadCount) != ArenaStatus_Ok ||
	    ParseUnsigned(ObservationRowValue(row, context, 5), &repeatIndex) != ArenaStatus_Ok || repeatIndex >= context->manifest->repeatCount)
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_unit_identity");
	while (threadOrdinal < context->manifest->threadCount && context->manifest->threadCounts[threadOrdinal] != threadCount)
		++threadOrdinal;
	if (threadOrdinal == context->manifest->threadCount)
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_thread_identity");
	const std::uint32_t engineIndex = context->manifest->engines[engineOrdinal].engineIndex;
	if (context->model->sliceCount == 0 || context->model->slices[context->model->sliceCount - 1].engineIndex != engineIndex ||
	    context->model->slices[context->model->sliceCount - 1].threadCount != threadCount)
	{
		if (context->seen[engineOrdinal][threadOrdinal] == PresenceStatus_Present || context->model->sliceCount >= context->model->slices.size())
			return ObservationError(error, ArenaStatus_InvalidResult, "observation_unit_duplicate");
		ObservationArtifactSlice& added = context->model->slices[context->model->sliceCount++];
		added.firstRowByteOffset = row->sourceByteOffset;
		added.engineIndex = engineIndex;
		added.threadCount = threadCount;
		added.repeatCount = context->manifest->repeatCount;
		added.repeatOffsets.resize(added.repeatCount);
		added.repeatRows.resize(added.repeatCount);
		added.rowsPerRepeat = context->rowsPerRepeat;
		context->seen[engineOrdinal][threadOrdinal] = PresenceStatus_Present;
	}
	ObservationArtifactSlice& slice = context->model->slices[context->model->sliceCount - 1];
	const std::uint32_t rowWithinRepeat = slice.repeatRows[repeatIndex];
	if (rowWithinRepeat >= context->rowsPerRepeat)
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_repeat_overflow");
	for (std::uint32_t later = repeatIndex + 1; later < slice.repeatCount; ++later)
		if (slice.repeatRows[later] != 0)
			return ObservationError(error, ArenaStatus_InvalidResult, "observation_repeat_order");
	if (rowWithinRepeat == 0)
		slice.repeatOffsets[repeatIndex] = row->sourceByteOffset;
	std::uint32_t declarationOrdinal = 0;
	std::uint32_t sampleOrdinal = 0;
	const ObservationDeclaration* declaration =
	    ExpectedDeclaration(context->catalog, context->benchmarkCase, context->configuration, rowWithinRepeat,
		                    &declarationOrdinal, &sampleOrdinal);
	if (declaration == nullptr)
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_declaration");
	const std::uint32_t expectedSample =
	    CaseConfigurationSampleIndex(context->catalog, context->configuration, *declaration, sampleOrdinal);
	std::uint32_t parsedThread = 0;
	std::uint32_t parsedRepeat = 0;
	std::uint32_t parsedSample = 0;
	if (ObservationRowValue(row, context, 0) != ResultTextView(context->manifest, context->manifest->runId) ||
	    ObservationRowValue(row, context, 1) !=
	        CatalogTextView(context->catalog, context->catalog->engines[slice.engineIndex].id) ||
	    ObservationRowValue(row, context, 2) != ResultTextView(context->manifest, context->manifest->caseId) ||
	    ObservationRowValue(row, context, 3) != ResultThreadBenchmarkMode(context->manifest, slice.threadCount) ||
	    ParseUnsigned(ObservationRowValue(row, context, 4), &parsedThread) != ArenaStatus_Ok ||
	    ParseUnsigned(ObservationRowValue(row, context, 5), &parsedRepeat) != ArenaStatus_Ok ||
	    ObservationRowValue(row, context, 6) !=
	        CaseConfigurationTextView(context->catalog, context->configuration, declaration->id) ||
	    ObservationRowValue(row, context, 7) !=
	        CaseConfigurationTextView(context->catalog, context->configuration, declaration->phaseId) ||
	    ParseUnsigned(ObservationRowValue(row, context, 8), &parsedSample) != ArenaStatus_Ok ||
	    ObservationRowValue(row, context, 10) !=
	        CaseConfigurationTextView(context->catalog, context->configuration, declaration->unit) ||
	    ObservationRowValue(row, context, 11) != ObservationRoleWireText(declaration->role) ||
	    parsedThread != slice.threadCount || parsedRepeat != repeatIndex || parsedSample != expectedSample)
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_identity");
	ObservationValue actual = {};
	ObservationValue expected = {};
	ObservationOutcome outcome = ObservationOutcome_Ok;
	if (ParseObservationValues(row, *declaration, context->headerIndexes, &actual, &expected, &outcome, error) !=
	    ArenaStatus_Ok)
		return error->code;
	if (context->benchmarkCase->fixtureKind == CaseFixtureKind_RagdollStairTumble)
	{
		if (declarationOrdinal >= context->qualityValues.size())
			return ObservationError(error, ArenaStatus_InvalidResult, "ragdoll_quality_declarations");
		context->qualityValues[declarationOrdinal] = actual;
		if (declarationOrdinal == 9)
		{
			const std::array<ObservationValue, 10>& values = context->qualityValues;
			const CaseRecord& benchmarkCase = *context->benchmarkCase;
			const std::uint64_t expectedBodies =
			    static_cast<std::uint64_t>(benchmarkCase.dynamicBodyCount) * benchmarkCase.measuredWorkUnitCount;
			const std::uint64_t expectedJoints =
			    static_cast<std::uint64_t>(benchmarkCase.constraintCount) * benchmarkCase.measuredWorkUnitCount;
			if (values[0].float64Value < 0.0 || values[1].float64Value < 0.0 ||
			    values[0].float64Value > values[1].float64Value + std::max(1e-9, values[1].float64Value * 1e-9) ||
			    values[2].unsignedValue >= benchmarkCase.constraintCount || values[3].unsignedValue == 0 ||
			    values[3].unsignedValue > benchmarkCase.measuredWorkUnitCount || values[4].unsignedValue == 0 ||
			    values[4].unsignedValue > expectedJoints || values[5].unsignedValue > expectedBodies ||
			    values[7].unsignedValue > expectedBodies ||
			    values[5].unsignedValue != expectedBodies - values[7].unsignedValue ||
			    values[6].unsignedValue > values[5].unsignedValue ||
			    values[8].unsignedValue >= benchmarkCase.dynamicBodyCount ||
			    values[9].unsignedValue > benchmarkCase.measuredWorkUnitCount ||
			    ((values[6].unsignedValue == 0 && values[7].unsignedValue == 0)
			         ? (values[8].unsignedValue != 0 || values[9].unsignedValue != 0 ||
					    values[4].unsignedValue != expectedJoints)
					 : values[9].unsignedValue == 0))
				return ObservationError(error, ArenaStatus_InvalidResult, "ragdoll_quality_value_contract");
		}
	}
	const std::uint32_t aggregateIndex =
	    (engineOrdinal * context->manifest->threadCount + threadOrdinal) * context->benchmarkCase->observationCount +
	    declarationOrdinal;
	if (aggregateIndex >= context->model->aggregates.size())
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_aggregate_capacity");
	if (AccumulateObservation(&context->model->aggregates[aggregateIndex], *declaration, actual, expected, outcome,
	                          error) != ArenaStatus_Ok)
		return error->code;
	++slice.repeatRows[repeatIndex];
	++slice.rowCount;
	context->model->rowCount += 1;
	return ArenaStatus_Ok;
}

ArenaStatus ConsumeObservationDetailRow(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error)
{
	ObservationDetailContext* context = static_cast<ObservationDetailContext*>(opaque);
	if (context->headerMapped != PresenceStatus_Present &&
	    MapObservationHeader(header, context, error) != ArenaStatus_Ok)
		return error->code;
	if (context->rowCount >= context->slice->rowCount)
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_detail_row_overflow");
	const std::uint32_t repeatIndex = context->selection->repeatIndex;
	const std::uint32_t rowWithinRepeat = context->rowCount;
	std::uint32_t declarationOrdinal = 0;
	std::uint32_t sampleOrdinal = 0;
	const ObservationDeclaration* declaration =
	    ExpectedDeclaration(context->catalog, context->benchmarkCase, context->configuration, rowWithinRepeat,
		                    &declarationOrdinal, &sampleOrdinal);
	if (declaration == nullptr)
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_detail_declaration");
	const std::uint32_t expectedSample =
	    CaseConfigurationSampleIndex(context->catalog, context->configuration, *declaration, sampleOrdinal);
	if (ValidateObservationIdentity(row, context, *declaration, repeatIndex, expectedSample, error) != ArenaStatus_Ok)
		return error->code;
	ObservationValue actual = {};
	ObservationValue expected = {};
	ObservationOutcome outcome = ObservationOutcome_Ok;
	if (ParseObservationValues(row, *declaration, context->headerIndexes, &actual, &expected, &outcome, error) !=
	    ArenaStatus_Ok)
		return error->code;
	context->rowCount += 1;
	if (repeatIndex != context->selection->repeatIndex)
		return ArenaStatus_Ok;
	if (context->projection->rowCount >= context->projection->rows.size())
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_detail_capacity");
	ObservationDetailRecord& detail = context->projection->rows[context->projection->rowCount++];
	detail.actual = actual;
	detail.expected = expected;
	detail.declarationOrdinal = declarationOrdinal;
	detail.sampleIndex = expectedSample;
	detail.outcome = outcome;
	return ArenaStatus_Ok;
}
} // namespace

std::string_view ObservationRoleWireText(ObservationRole role)
{
	switch (role)
	{
	case ObservationRole_ValidityZero:
		return "validity_zero";
	case ObservationRole_ValidityExact:
		return "validity_exact";
	case ObservationRole_Quality:
		return "quality";
	case ObservationRole_Performance:
		return "performance";
	default:
		return {};
	}
}

std::string_view ObservationOutcomeText(ObservationOutcome outcome)
{
	switch (outcome)
	{
	case ObservationOutcome_Ok:
		return "ok";
	case ObservationOutcome_Failed:
		return "failed";
	default:
		return {};
	}
}

const ObservationAggregate* ObservationAggregateAt(const ObservationResultModel* model, std::uint32_t engineOrdinal,
                                                   std::uint32_t threadOrdinal, std::uint32_t declarationOrdinal)
{
	if (model == nullptr || engineOrdinal >= model->engineCount || threadOrdinal >= model->threadCount ||
	    declarationOrdinal >= model->declarationCount)
		return nullptr;
	const std::uint64_t index =
	    (static_cast<std::uint64_t>(engineOrdinal) * model->threadCount + threadOrdinal) * model->declarationCount +
	    declarationOrdinal;
	return index < model->aggregateCount ? &model->aggregates[static_cast<std::size_t>(index)] : nullptr;
}

ArenaStatus LoadObservationResultModel(const wchar_t* path, const Catalog* catalog, const CaseRecord* benchmarkCase,
                                       const ResultManifestRecord* manifest, ObservationResultModel* model,
                                       StatusRecord* error)
{
	if (model != nullptr)
		{
			std::destroy_at(model);
			std::construct_at(model);
		}
	if (error != nullptr)
		*error = {};
	if (catalog == nullptr || benchmarkCase == nullptr || manifest == nullptr || model == nullptr || error == nullptr ||
	    manifest->engineCount == 0 || manifest->threadCount == 0 || manifest->repeatCount == 0 ||
	    manifest->repeatCount > kTimingRepeatCapacity)
		return error != nullptr ? ObservationError(error, ArenaStatus_InvalidArgument, "observation_model_argument")
		                        : ArenaStatus_InvalidArgument;
	const CaseRecord observationCase = RunObservationCase(*benchmarkCase, manifest->verificationMode);
	benchmarkCase = &observationCase;
	if (manifest->observationsPresence != PresenceStatus_Present)
	{
		if (benchmarkCase->observationCount != 0)
			return ObservationError(error, ArenaStatus_InvalidResult, "observation_manifest_missing");
		model->availability = PresenceStatus_Absent;
		return ArenaStatus_Ok;
	}
	if (path == nullptr)
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_artifact_missing");
	const std::size_t pathSize = std::wcslen(path);
	if (pathSize + 1 > model->path.size())
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_path_capacity");
	const EffectiveRunConfiguration* configuration = ResultConfiguration(manifest);
	const std::uint32_t rowsPerRepeat = RowsPerRepeat(catalog, benchmarkCase, configuration);
	if (rowsPerRepeat > kObservationRepeatRowCapacity)
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_repeat_capacity");
	const std::uint64_t expectedSlices =
	    benchmarkCase->observationCount == 0
	        ? 0
	        : static_cast<std::uint64_t>(manifest->engineCount) * manifest->threadCount;
	const std::uint64_t expectedAggregates = expectedSlices * benchmarkCase->observationCount;
	const std::uint64_t expectedRows = expectedSlices * manifest->repeatCount * rowsPerRepeat;
	if (expectedSlices > model->slices.size() || expectedAggregates > model->aggregates.size())
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_model_capacity");
	model->engineCount = manifest->engineCount;
	model->threadCount = manifest->threadCount;
	model->declarationCount = benchmarkCase->observationCount;
	model->aggregateCount = static_cast<std::uint32_t>(expectedAggregates);
	ObservationIndexContext context = {};
	context.catalog = catalog;
	context.benchmarkCase = benchmarkCase;
	context.configuration = configuration;
	context.manifest = manifest;
	context.model = model;
	context.rowsPerRepeat = rowsPerRepeat;
	CsvHeader header = {};
	CsvReadRecord record = {};
	if (ReadCsvFile(path, &header, ConsumeObservationIndexRow, &context, &record, error) != ArenaStatus_Ok ||
	    (context.headerMapped != PresenceStatus_Present &&
	     MapObservationHeader(&header, &context, error) != ArenaStatus_Ok) ||
	    model->rowCount > expectedRows || model->sliceCount > expectedSlices)
	{
		{
			std::destroy_at(model);
			std::construct_at(model);
		}
		return error->code != ArenaStatus_Ok
		           ? error->code
				   : ObservationError(error, ArenaStatus_InvalidResult, "observation_cardinality");
	}
	if (benchmarkCase->observationCount != 0)
		for (std::uint32_t engine = 0; engine < manifest->engineCount; ++engine)
			for (std::uint32_t thread = 0; thread < manifest->threadCount; ++thread)
				for (std::uint32_t repeat = 0; repeat < manifest->repeatCount; ++repeat)
				{
					std::uint32_t count = 0;
					for (std::uint32_t ordinal = 0; ordinal < model->sliceCount; ++ordinal)
					{
						const ObservationArtifactSlice& slice = model->slices[ordinal];
						if (slice.engineIndex == manifest->engines[engine].engineIndex && slice.threadCount == manifest->threadCounts[thread])
							count = slice.repeatRows[repeat];
					}
					const ExecutionFailure* failure = FindExecutionFailure(manifest->executionFailures, manifest->engines[engine].engineIndex,
					                                                        manifest->threadCounts[thread], repeat);
					if ((count != rowsPerRepeat && failure == nullptr) ||
					    (count != 0 && failure != nullptr && failure->outcome == ExecutionOutcome_NotRun))
						return ObservationError(error, ArenaStatus_InvalidResult, "observation_terminal_coverage");
				}
	std::copy(path, path + pathSize + 1, model->path.begin());
	model->sourceSize = record.sourceSize;
	model->availability = PresenceStatus_Present;
	return ArenaStatus_Ok;
}

ArenaStatus ProjectObservationDetail(const ObservationResultModel* model, const Catalog* catalog,
                                     const CaseRecord* benchmarkCase, const ObservationDetailSelection* selection,
                                     ObservationDetailProjection* projection, StatusRecord* error,
                                     const EffectiveRunConfiguration* configuration)
{
	if (projection != nullptr)
		*projection = {};
	if (error != nullptr)
		*error = {};
	if (model == nullptr || catalog == nullptr || benchmarkCase == nullptr || selection == nullptr ||
	    projection == nullptr || error == nullptr || model->availability != PresenceStatus_Present)
		return error != nullptr
		           ? ObservationError(error, ArenaStatus_InvalidArgument, "observation_projection_argument")
				   : ArenaStatus_InvalidArgument;
	const ObservationArtifactSlice* slice = nullptr;
	for (std::uint32_t index = 0; index < model->sliceCount; ++index)
		if (model->slices[index].engineIndex == selection->engineIndex &&
		    model->slices[index].threadCount == selection->threadCount)
		{
			slice = &model->slices[index];
			break;
		}
	if (slice == nullptr || selection->repeatIndex >= slice->repeatCount ||
	    slice->rowsPerRepeat > projection->rows.size())
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_projection_selection");
	const std::uint32_t rangeRowCount = slice->repeatRows[selection->repeatIndex];
	if (rangeRowCount == 0 || rangeRowCount > slice->rowCount)
		return ObservationError(error, ArenaStatus_InvalidResult, "observation_projection_range");
	ObservationDetailContext context = {};
	context.catalog = catalog;
	context.benchmarkCase = benchmarkCase;
	context.configuration = configuration;
	context.slice = slice;
	context.selection = selection;
	context.projection = projection;
	CsvHeader header = {};
	CsvReadRecord record = {};
	if (ReadCsvFileRange(model->path.data(), slice->repeatOffsets[selection->repeatIndex], rangeRowCount, &header,
	                     ConsumeObservationDetailRow, &context, &record, error) != ArenaStatus_Ok ||
	    context.rowCount != rangeRowCount || projection->rowCount != rangeRowCount ||
	    record.sourceSize != model->sourceSize)
	{
		*projection = {};
		return error->code != ArenaStatus_Ok
		           ? error->code
				   : ObservationError(error, ArenaStatus_InvalidResult, "observation_projection_source_changed");
	}
	projection->availability = AvailabilityStatus_Available;
	return ArenaStatus_Ok;
}
} // namespace physics_arena
