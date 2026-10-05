#include "physics_arena/result_view_model.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "physics_arena/csv_io.h"
#include "physics_arena/run.h"
#include "result_pipeline_internal.h"

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
constexpr std::array<std::string_view, 22> kSummaryColumns = {"run_id",
                                                              "engine_id",
                                                              "case_id",
                                                              "benchmark_mode",
                                                              "thread_count",
                                                              "repeat_count",
                                                              "work_unit_id",
                                                              "warmup_work_unit_count",
                                                              "measured_work_unit_count",
                                                              "primary_metric_id",
                                                              "primary_metric_unit",
                                                              "primary_metric_direction",
                                                              "minimum_primary_value",
                                                              "median_primary_value",
                                                              "maximum_primary_value",
                                                              "body_count",
                                                              "shape_count",
                                                              "query_count",
                                                              "constraint_count",
                                                              "invalid_transform_count",
                                                              "physics_settings",
                                                              "build_settings"};

constexpr std::array<std::string_view, 4> kNormalizedLatencyColumns = {"engine_id", "thread_count", "repeat_index",
                                                                       "mean_ms_per_work_unit"};

struct ResultViewContext
{
	const Catalog* catalog;
	const ResultManifestRecord* manifest;
	ResultViewModel* model;
	std::array<std::int32_t, kSummaryColumns.size()> headerIndexes;
	std::array<std::array<PresenceStatus, kThreadCountCapacity>, kEngineCapacity> seen;
	PresenceStatus headerMapped;
};

struct NormalizedLatencyAccumulator
{
	std::array<double, kRunRepeatCapacity> values;
	std::array<PresenceStatus, kRunRepeatCapacity> seen;
};

struct NormalizedLatencyContext
{
	const Catalog* catalog;
	const ResultManifestRecord* manifest;
	std::array<NormalizedLatencyAccumulator, kResultSummaryCapacity> accumulators;
	std::array<std::int32_t, kNormalizedLatencyColumns.size()> headerIndexes;
	std::uint32_t rowCount;
	PresenceStatus headerMapped;
};

struct NormalizedOutcomeContext
{
	NormalizedValidationContext validation;
	ResultViewModel* model;
	std::array<std::uint32_t, kResultSummaryCapacity> rowIndexes;
};

ArenaStatus ConsumeNormalizedOutcomeRow(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error)
{
	NormalizedOutcomeContext* context = static_cast<NormalizedOutcomeContext*>(opaque);
	if (ValidateNormalizedRow(header, row, &context->validation, error) != ArenaStatus_Ok)
		return error->code;
	std::uint32_t engine = 0, thread = 0;
	const ResultManifestRecord& manifest = *context->validation.manifest;
	while (ResultViewTextView(context->model, context->model->engines[engine].id) != CsvRowTextView(row, row->fields[1]))
		++engine;
	ParseUnsigned(CsvRowTextView(row, row->fields[7]), &thread);
	std::uint32_t threadOrdinal = 0;
	while (manifest.threadCounts[threadOrdinal] != thread)
		++threadOrdinal;
	std::uint64_t invalidTransforms = 0;
	ParseUnsigned64(CsvRowTextView(row, row->fields[22]), &invalidTransforms);
	if (CsvRowTextView(row, row->fields[23]) == "failed" || invalidTransforms != 0)
		context->model->summaryRows[context->rowIndexes[engine * manifest.threadCount + threadOrdinal]].outcome = ObservationOutcome_Failed;
	return ArenaStatus_Ok;
}

ArenaStatus ViewError(StatusRecord* error, ArenaStatus status, std::string_view detail)
{
	*error = {};
	const std::string_view component = "result_view_model";
	const std::string_view statusText = ArenaStatusText(status);
	std::copy(component.begin(), component.end(), error->component.begin());
	error->componentSize = static_cast<std::uint32_t>(component.size());
	std::copy(statusText.begin(), statusText.end(), error->status.begin());
	error->statusSize = static_cast<std::uint32_t>(statusText.size());
	if (detail.size() > error->detail.size())
		detail = "result_view_detail_capacity";
	std::copy(detail.begin(), detail.end(), error->detail.begin());
	error->detailSize = static_cast<std::uint32_t>(detail.size());
	error->code = status;
	return status;
}

ArenaStatus StoreViewText(ResultViewModel* model, ResultViewText* output, std::string_view text, StatusRecord* error)
{
	if (text.empty() || text.size() > model->textArena.size() - model->textArenaUsed)
		return ViewError(error, ArenaStatus_InvalidResult, "result_view_text_capacity");
	output->offset = model->textArenaUsed;
	output->size = static_cast<std::uint32_t>(text.size());
	std::copy(text.begin(), text.end(), model->textArena.begin() + model->textArenaUsed);
	model->textArenaUsed += output->size;
	return ArenaStatus_Ok;
}

ArenaStatus ParseUnsigned(std::string_view text, std::uint32_t* value)
{
	if (text.empty())
		return ArenaStatus_InvalidResult;
	const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), *value);
	return result.ec == std::errc() && result.ptr == text.data() + text.size() ? ArenaStatus_Ok
	                                                                           : ArenaStatus_InvalidResult;
}

struct PhysicsSettingParts
{
	std::string_view prefix;
	std::string_view suffix;
	std::uint32_t workerCount;
};

ArenaStatus ParsePhysicsSettingParts(std::string_view text, PhysicsSettingParts* parts)
{
	constexpr std::string_view marker = "worker_count=";
	const std::size_t markerIndex = text.find(marker);
	if (markerIndex == std::string_view::npos ||
	    (markerIndex != 0 && (markerIndex < 2 || text.substr(markerIndex - 2, 2) != "; ")))
		return ArenaStatus_InvalidResult;
	const std::size_t valueStart = markerIndex + marker.size();
	std::size_t valueEnd = text.find(';', valueStart);
	if (valueEnd == std::string_view::npos)
		valueEnd = text.size();
	if (valueEnd == valueStart ||
	    (valueEnd != text.size() && (valueEnd + 1 >= text.size() || text[valueEnd + 1] != ' ')) ||
	    text.find(marker, valueEnd) != std::string_view::npos ||
	    ParseUnsigned(text.substr(valueStart, valueEnd - valueStart), &parts->workerCount) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	parts->prefix = text.substr(0, markerIndex);
	parts->suffix = text.substr(valueEnd);
	return ArenaStatus_Ok;
}

ArenaStatus ValidatePhysicsSettingVariation(std::string_view first, std::uint32_t firstThreadCount,
                                            std::string_view current, std::uint32_t currentThreadCount,
                                            const EngineRecord& engine)
{
	PhysicsSettingParts firstParts = {};
	PhysicsSettingParts currentParts = {};
	if (ParsePhysicsSettingParts(first, &firstParts) != ArenaStatus_Ok ||
	    ParsePhysicsSettingParts(current, &currentParts) != ArenaStatus_Ok ||
	    firstParts.prefix != currentParts.prefix || firstParts.suffix != currentParts.suffix ||
	    firstParts.workerCount != EffectiveWorkerCount(engine.effectiveWorkerPolicy, firstThreadCount) ||
	    currentParts.workerCount != EffectiveWorkerCount(engine.effectiveWorkerPolicy, currentThreadCount))
		return ArenaStatus_InvalidResult;
	return ArenaStatus_Ok;
}

ArenaStatus ParseSignedNonnegative(std::string_view text, std::int64_t* value)
{
	if (text.empty())
		return ArenaStatus_InvalidResult;
	const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), *value);
	return result.ec == std::errc() && result.ptr == text.data() + text.size() && *value >= 0
	           ? ArenaStatus_Ok
			   : ArenaStatus_InvalidResult;
}

ArenaStatus ParsePositiveDouble(std::string_view text, double* value)
{
	if (text.empty())
		return ArenaStatus_InvalidResult;
	const std::from_chars_result result =
	    std::from_chars(text.data(), text.data() + text.size(), *value, std::chars_format::general);
	return result.ec == std::errc() && result.ptr == text.data() + text.size() && std::isfinite(*value) && *value > 0.0
	           ? ArenaStatus_Ok
			   : ArenaStatus_InvalidResult;
}

std::string_view RowValue(const CsvRow* row, const ResultViewContext* context, std::uint32_t field)
{
	return CsvRowTextView(row, row->fields[static_cast<std::uint32_t>(context->headerIndexes[field])]);
}

ArenaStatus MapHeader(const CsvHeader* header, ResultViewContext* context, StatusRecord* error)
{
	context->headerIndexes.fill(-1);
	const std::size_t expectedCount = kSummaryColumns.size();
	if (header->fieldCount != expectedCount)
		return ViewError(error, ArenaStatus_InvalidResult, "result_view_header_count");
	for (std::uint32_t column = 0; column < header->fieldCount; ++column)
	{
		const std::string_view name = CsvHeaderTextView(header, header->fields[column]);
		std::uint32_t expected = 0;
		while (expected < expectedCount && kSummaryColumns[expected] != name)
			++expected;
		if (expected == expectedCount || context->headerIndexes[expected] >= 0)
			return ViewError(error, ArenaStatus_InvalidResult, "result_view_header_column");
		context->headerIndexes[expected] = static_cast<std::int32_t>(column);
	}
	context->headerMapped = PresenceStatus_Present;
	return ArenaStatus_Ok;
}

std::uint32_t ManifestEngineOrdinal(const ResultManifestRecord* manifest, std::uint32_t engineIndex)
{
	std::uint32_t ordinal = 0;
	while (ordinal < manifest->engineCount && manifest->engines[ordinal].engineIndex != engineIndex)
		++ordinal;
	return ordinal;
}

std::uint32_t ThreadOrdinal(const ResultManifestRecord* manifest, std::uint32_t threadCount)
{
	std::uint32_t ordinal = 0;
	while (ordinal < manifest->threadCount && manifest->threadCounts[ordinal] != threadCount)
		++ordinal;
	return ordinal;
}

ArenaStatus ConsumeSummaryRow(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error)
{
	ResultViewContext* context = static_cast<ResultViewContext*>(opaque);
	if (context->headerMapped != PresenceStatus_Present && MapHeader(header, context, error) != ArenaStatus_Ok)
		return error->code;
	ResultViewModel* model = context->model;
	if (model->summaryRowCount >= model->summaryRows.size())
	{
		return ViewError(error, ArenaStatus_InvalidResult, "result_view_row_capacity");
	}
	const std::string_view engineId = RowValue(row, context, 1);
	std::uint32_t catalogEngineIndex = 0;
	while (catalogEngineIndex < context->catalog->engineCount &&
	       CatalogTextView(context->catalog, context->catalog->engines[catalogEngineIndex].id) != engineId)
		++catalogEngineIndex;
	const std::uint32_t engineOrdinal = ManifestEngineOrdinal(context->manifest, catalogEngineIndex);
	std::uint32_t threadCount = 0;
	std::uint32_t repeatCount = 0;
	std::uint32_t warmupCount = 0;
	std::uint32_t measuredCount = 0;
	std::uint32_t bodyCount = 0;
	std::uint32_t shapeCount = 0;
	std::uint32_t queryCount = 0;
	std::uint32_t constraintCount = 0;
	const EffectiveRunConfiguration* configuration = ResultViewConfiguration(model);
	const ResultCaseMetadata metadata =
	    ProjectResultCaseMetadata(context->catalog, ResultViewCaseDefinition(context->catalog, model),
		                          context->manifest->measurementMode, configuration != nullptr || model->descriptiveCasePresence == PresenceStatus_Present ? &model->savedConfiguration : nullptr);
	const CaseRecord& benchmarkCase = metadata.record;
	const std::string_view direction =
	    metadata.direction == PrimaryMetricDirection_NotRanked
	        ? "not_ranked"
	        : (metadata.direction == PrimaryMetricDirection_LowerIsBetter ? "lower_is_better" : "higher_is_better");
	if (catalogEngineIndex == context->catalog->engineCount || engineOrdinal == context->manifest->engineCount ||
	    RowValue(row, context, 0) != ResultTextView(context->manifest, context->manifest->runId) ||
	    RowValue(row, context, 2) != ResultTextView(context->manifest, context->manifest->caseId) ||
	    ParseUnsigned(RowValue(row, context, 4), &threadCount) != ArenaStatus_Ok ||
	    RowValue(row, context, 3) != ResultThreadBenchmarkMode(context->manifest, threadCount) ||
	    ParseUnsigned(RowValue(row, context, 5), &repeatCount) != ArenaStatus_Ok ||
	    RowValue(row, context, 6) !=
	        CaseConfigurationTextView(context->catalog, configuration != nullptr || model->descriptiveCasePresence == PresenceStatus_Present ? &model->savedConfiguration : nullptr, benchmarkCase.workUnitId) ||
	    ParseUnsigned(RowValue(row, context, 7), &warmupCount) != ArenaStatus_Ok ||
	    ParseUnsigned(RowValue(row, context, 8), &measuredCount) != ArenaStatus_Ok ||
	    RowValue(row, context, 9) != metadata.primaryMetricId ||
	    RowValue(row, context, 10) != metadata.primaryMetricUnit || RowValue(row, context, 11) != direction ||
	    ParseUnsigned(RowValue(row, context, 15), &bodyCount) != ArenaStatus_Ok ||
	    ParseUnsigned(RowValue(row, context, 16), &shapeCount) != ArenaStatus_Ok ||
	    ParseUnsigned(RowValue(row, context, 17), &queryCount) != ArenaStatus_Ok ||
	    ParseUnsigned(RowValue(row, context, 18), &constraintCount) != ArenaStatus_Ok)
		return ViewError(error, ArenaStatus_InvalidResult, "result_view_identity");
	const std::uint32_t threadOrdinal = ThreadOrdinal(context->manifest, threadCount);
	if (threadOrdinal == context->manifest->threadCount ||
	    context->seen[engineOrdinal][threadOrdinal] == PresenceStatus_Present ||
	    (repeatCount > context->manifest->repeatCount || (repeatCount != context->manifest->repeatCount && context->manifest->executionFailures.empty())) || warmupCount != context->manifest->warmupWorkUnitCount ||
	    measuredCount != context->manifest->measuredWorkUnitCount || bodyCount != benchmarkCase.bodyCount ||
	    shapeCount != benchmarkCase.shapeCount || queryCount != benchmarkCase.queryCount ||
	    constraintCount != benchmarkCase.constraintCount)
		return ViewError(error, ArenaStatus_InvalidResult, "result_view_count");
	ResultEngineView& engine = model->engines[engineOrdinal];
	if (engine.id.size == 0)
	{
		const EngineRecord& catalogEngine = context->catalog->engines[catalogEngineIndex];
		if (repeatCount != 0 && ValidatePhysicsSettingVariation(RowValue(row, context, 20), threadCount, RowValue(row, context, 20),
		                                    threadCount, catalogEngine) != ArenaStatus_Ok)
			return ViewError(error, ArenaStatus_InvalidResult, "result_view_setting_drift");
		const ResultEngineSnapshot& snapshot = context->manifest->engines[engineOrdinal];
		const std::string_view displayName = CatalogTextView(context->catalog, catalogEngine.displayName);
		const std::string_view reportVersion = snapshot.reportVersion.size != 0
		                                           ? ResultTextView(context->manifest, snapshot.reportVersion)
		                                           : std::string_view();
		EngineProvenanceLabelProjection provenance = {};
		if (ProjectEngineProvenanceLabel(engineId, displayName, reportVersion, &provenance, error) != ArenaStatus_Ok)
			return error->code;
		engine.catalogEngineIndex = catalogEngineIndex;
		engine.presentationOrder = catalogEngineIndex;
		engine.colorRgb = catalogEngine.colorRgb;
		engine.physicsSettingsThreadCount = threadCount;
		if (StoreViewText(model, &engine.id, engineId, error) != ArenaStatus_Ok ||
		    StoreViewText(model, &engine.displayName, displayName, error) != ArenaStatus_Ok ||
		    StoreViewText(model, &engine.provenanceLabel, std::string_view(provenance.text.data(), provenance.size),
		                  error) != ArenaStatus_Ok ||
		    StoreViewText(model, &engine.physicsSettings, RowValue(row, context, 20), error) != ArenaStatus_Ok ||
		    StoreViewText(model, &engine.buildSettings, RowValue(row, context, 21), error) != ArenaStatus_Ok)
			return error->code;
		if (!reportVersion.empty() &&
		    StoreViewText(model, &engine.reportVersion, reportVersion, error) != ArenaStatus_Ok)
			return error->code;
	}
	else if (repeatCount != 0 && ResultViewTextView(model, engine.physicsSettings) != "unavailable" &&
	         (ResultViewTextView(model, engine.buildSettings) != RowValue(row, context, 21) ||
	         ValidatePhysicsSettingVariation(ResultViewTextView(model, engine.physicsSettings),
	                                         engine.physicsSettingsThreadCount, RowValue(row, context, 20), threadCount,
	                                         context->catalog->engines[catalogEngineIndex]) != ArenaStatus_Ok))
		return ViewError(error, ArenaStatus_InvalidResult, "result_view_setting_drift");
	ResultSummaryViewRow& output = model->summaryRows[model->summaryRowCount++];
	output.engineOrdinal = engineOrdinal;
	output.threadCount = threadCount;
	output.repeatCount = repeatCount;
	if (repeatCount < context->manifest->repeatCount)
		output.outcome = ObservationOutcome_Failed;
	output.warmupWorkUnitCount = warmupCount;
	output.bodyCount = bodyCount;
	output.shapeCount = shapeCount;
	output.queryCount = queryCount;
	output.constraintCount = constraintCount;
	if ((model->measurementMode == ResultMeasurementMode_PhysicalQuality || repeatCount == 0
	         ? (!RowValue(row, context, 12).empty() || !RowValue(row, context, 13).empty() ||
			    !RowValue(row, context, 14).empty())
			 : (ParsePositiveDouble(RowValue(row, context, 12), &output.minimumPrimaryValue) != ArenaStatus_Ok ||
			    ParsePositiveDouble(RowValue(row, context, 13), &output.medianPrimaryValue) != ArenaStatus_Ok ||
			    ParsePositiveDouble(RowValue(row, context, 14), &output.maximumPrimaryValue) != ArenaStatus_Ok)) ||
	    ParseSignedNonnegative(RowValue(row, context, 19), &output.invalidTransformCount) != ArenaStatus_Ok)
		return ViewError(error, ArenaStatus_InvalidResult, "result_view_metric");
	if (output.invalidTransformCount != 0)
		output.outcome = ObservationOutcome_Failed;
	for (const ExecutionFailure& failure : context->manifest->executionFailures)
		if (failure.engineIndex == catalogEngineIndex && failure.threadCount == threadCount)
			output.outcome = ObservationOutcome_Failed;
	if (output.minimumPrimaryValue > output.medianPrimaryValue ||
	    output.medianPrimaryValue > output.maximumPrimaryValue)
		return ViewError(error, ArenaStatus_InvalidResult, "result_view_unordered_statistics");
	context->seen[engineOrdinal][threadOrdinal] = PresenceStatus_Present;
	return ArenaStatus_Ok;
}

ArenaStatus AddMetric(ResultViewModel* model, ResultMetricId id, std::string_view label, std::string_view axis,
                      std::string_view unit, PresenceStatus lower, StatusRecord* error)
{
	if (model->metricCount >= model->metrics.size())
		return ViewError(error, ArenaStatus_InvalidResult, "result_metric_capacity");
	ResultMetricDescriptor& metric = model->metrics[model->metricCount++];
	metric.id = id;
	metric.lowerIsBetter = lower;
	if (StoreViewText(model, &metric.label, label, error) != ArenaStatus_Ok ||
	    StoreViewText(model, &metric.axisLabel, axis, error) != ArenaStatus_Ok ||
	    StoreViewText(model, &metric.unitLabel, unit, error) != ArenaStatus_Ok)
		return error->code;
	return ArenaStatus_Ok;
}

ArenaStatus MapNormalizedLatencyHeader(const CsvHeader* header, NormalizedLatencyContext* context, StatusRecord* error)
{
	context->headerIndexes.fill(-1);
	for (std::uint32_t column = 0; column < header->fieldCount; ++column)
	{
		const std::string_view name = CsvHeaderTextView(header, header->fields[column]);
		std::uint32_t expected = 0;
		while (expected < kNormalizedLatencyColumns.size() && kNormalizedLatencyColumns[expected] != name)
			++expected;
		if (expected == kNormalizedLatencyColumns.size())
			continue;
		if (context->headerIndexes[expected] >= 0)
			return ViewError(error, ArenaStatus_InvalidResult, "result_view_latency_header");
		context->headerIndexes[expected] = static_cast<std::int32_t>(column);
	}
	for (const std::int32_t index : context->headerIndexes)
		if (index < 0)
			return ViewError(error, ArenaStatus_InvalidResult, "result_view_latency_header");
	context->headerMapped = PresenceStatus_Present;
	return ArenaStatus_Ok;
}

std::string_view NormalizedLatencyValue(const CsvRow* row, const NormalizedLatencyContext* context, std::uint32_t field)
{
	return CsvRowTextView(row, row->fields[static_cast<std::uint32_t>(context->headerIndexes[field])]);
}

ArenaStatus ConsumeNormalizedLatencyRow(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error)
{
	NormalizedLatencyContext* context = static_cast<NormalizedLatencyContext*>(opaque);
	if (context->headerMapped != PresenceStatus_Present &&
	    MapNormalizedLatencyHeader(header, context, error) != ArenaStatus_Ok)
		return error->code;
	const std::string_view engineId = NormalizedLatencyValue(row, context, 0);
	std::uint32_t engineOrdinal = 0;
	while (engineOrdinal < context->manifest->engineCount &&
	       CatalogTextView(context->catalog,
	                       context->catalog->engines[context->manifest->engines[engineOrdinal].engineIndex].id) !=
	           engineId)
		++engineOrdinal;
	std::uint32_t threadCount = 0;
	std::uint32_t repeatIndex = 0;
	double meanMilliseconds = 0.0;
	if (engineOrdinal == context->manifest->engineCount ||
	    ParseUnsigned(NormalizedLatencyValue(row, context, 1), &threadCount) != ArenaStatus_Ok ||
	    ParseUnsigned(NormalizedLatencyValue(row, context, 2), &repeatIndex) != ArenaStatus_Ok ||
	    repeatIndex >= context->manifest->repeatCount ||
	    ParsePositiveDouble(NormalizedLatencyValue(row, context, 3), &meanMilliseconds) != ArenaStatus_Ok)
		return ViewError(error, ArenaStatus_InvalidResult, "result_view_latency_identity");
	const std::uint32_t threadOrdinal = ThreadOrdinal(context->manifest, threadCount);
	if (threadOrdinal == context->manifest->threadCount)
		return ViewError(error, ArenaStatus_InvalidResult, "result_view_latency_identity");
	NormalizedLatencyAccumulator& accumulator =
	    context->accumulators[engineOrdinal * context->manifest->threadCount + threadOrdinal];
	if (accumulator.seen[repeatIndex] == PresenceStatus_Present)
		return ViewError(error, ArenaStatus_InvalidResult, "result_view_latency_duplicate");
	accumulator.values[repeatIndex] = meanMilliseconds;
	accumulator.seen[repeatIndex] = PresenceStatus_Present;
	context->rowCount += 1;
	return ArenaStatus_Ok;
}

ArenaStatus ProjectNormalizedQueryBatchLatency(const wchar_t* normalizedPath, const Catalog* catalog,
                                               const ResultManifestRecord* manifest, ResultViewModel* model,
                                               StatusRecord* error)
{
	if (normalizedPath == nullptr)
		return ViewError(error, ArenaStatus_InvalidResult, "result_view_latency_missing");
	NormalizedLatencyContext context = {};
	context.catalog = catalog;
	context.manifest = manifest;
	CsvHeader header = {};
	CsvReadRecord record = {};
	if (ReadCsvFile(normalizedPath, &header, ConsumeNormalizedLatencyRow, &context, &record, error) != ArenaStatus_Ok)
		return error->code;
	for (std::uint32_t engine = 0; engine < manifest->engineCount; ++engine)
		for (std::uint32_t thread = 0; thread < manifest->threadCount; ++thread)
			for (std::uint32_t repeat = 0; repeat < manifest->repeatCount; ++repeat)
				if (context.accumulators[engine * manifest->threadCount + thread].seen[repeat] != PresenceStatus_Present &&
				    FindExecutionFailure(manifest->executionFailures, manifest->engines[engine].engineIndex,
				                         manifest->threadCounts[thread], repeat) == nullptr)
					return ViewError(error, ArenaStatus_InvalidResult, "result_view_latency_cardinality");
	for (std::uint32_t rowIndex = 0; rowIndex < model->summaryRowCount; ++rowIndex)
	{
		ResultSummaryViewRow& row = model->summaryRows[rowIndex];
		const std::uint32_t threadOrdinal = ThreadOrdinal(manifest, row.threadCount);
		std::array<double, kRunRepeatCapacity> ordered = {};
		std::uint32_t count = 0;
		const NormalizedLatencyAccumulator& accumulator = context.accumulators[row.engineOrdinal * manifest->threadCount + threadOrdinal];
		for (std::uint32_t repeat = 0; repeat < manifest->repeatCount; ++repeat)
			if (accumulator.seen[repeat] == PresenceStatus_Present)
				ordered[count++] = accumulator.values[repeat];
		if (count != row.repeatCount)
			return ViewError(error, ArenaStatus_InvalidResult, "result_view_latency_summary_coverage");
		if (count == 0)
			continue;
		std::sort(ordered.begin(), ordered.begin() + count);
		row.minimumWorkUnitMilliseconds = ordered[0];
		row.medianWorkUnitMilliseconds = count % 2 != 0 ? ordered[count / 2] : (ordered[count / 2 - 1] + ordered[count / 2]) / 2.0;
		row.maximumWorkUnitMilliseconds = ordered[count - 1];
	}
	return ArenaStatus_Ok;
}

ArenaStatus BuildObservationPath(const wchar_t* timingPath,
                                 std::array<wchar_t, kTimingArtifactPathCapacity>* observationPath, StatusRecord* error)
{
	const std::size_t size = std::wcslen(timingPath);
	if (size + 1 > observationPath->size())
		return ViewError(error, ArenaStatus_InvalidResult, "result_view_observation_path");
	std::copy(timingPath, timingPath + size + 1, observationPath->begin());
	wchar_t* separator = std::wcsrchr(observationPath->data(), L'\\');
	wchar_t* forward = std::wcsrchr(observationPath->data(), L'/');
	if (forward != nullptr && (separator == nullptr || forward > separator))
		separator = forward;
	constexpr std::wstring_view name = L"observations.csv";
	if (separator == nullptr ||
	    static_cast<std::size_t>(separator - observationPath->data() + 1) + name.size() + 1 > observationPath->size())
		return ViewError(error, ArenaStatus_InvalidResult, "result_view_observation_path");
	std::copy(name.begin(), name.end(), separator + 1);
	separator[1 + name.size()] = L'\0';
	return ArenaStatus_Ok;
}
} // namespace

std::uint32_t EffectiveWorkerCount(WorkerCountPolicy policy, std::uint32_t threadCount)
{
	return policy == WorkerCountPolicy_ThreadCount
	           ? threadCount
			   : (policy == WorkerCountPolicy_ThreadCountMinusOne && threadCount > 1 ? threadCount - 1 : 0);
}

std::string_view ResultViewTextView(const ResultViewModel* model, ResultViewText text)
{
	if (model == nullptr || text.offset > model->textArenaUsed || text.size > model->textArenaUsed - text.offset)
		return {};
	return std::string_view(model->textArena.data() + text.offset, text.size);
}

ArenaStatus ProjectEngineProvenanceLabel(std::string_view engineId, std::string_view displayName,
                                         std::string_view reportVersion, EngineProvenanceLabelProjection* projection,
                                         StatusRecord* error)
{
	if (projection != nullptr)
		*projection = {};
	if (error != nullptr)
		*error = {};
	if (projection == nullptr || error == nullptr || displayName.empty())
		return error != nullptr ? ViewError(error, ArenaStatus_InvalidArgument, "engine_provenance_label_argument")
		                        : ArenaStatus_InvalidArgument;
	const std::string_view prefix = displayName;
	std::string_view suffix;
	PresenceStatus separator = PresenceStatus_Absent;
	if (!reportVersion.empty() && !displayName.ends_with(reportVersion))
	{
		suffix = reportVersion;
		separator = PresenceStatus_Present;
	}
	(void)engineId;
	const std::size_t outputSize = prefix.size() + suffix.size() + (separator == PresenceStatus_Present ? 1 : 0);
	if (outputSize + 1 > projection->text.size())
		return ViewError(error, ArenaStatus_InvalidResult, "engine_provenance_label_capacity");
	std::copy(prefix.begin(), prefix.end(), projection->text.begin());
	std::size_t offset = prefix.size();
	if (separator == PresenceStatus_Present)
		projection->text[offset++] = ' ';
	std::copy(suffix.begin(), suffix.end(), projection->text.begin() + offset);
	projection->size = static_cast<std::uint32_t>(outputSize);
	projection->text[outputSize] = '\0';
	return ArenaStatus_Ok;
}

double ResultMetricValue(const ResultSummaryViewRow* row, ResultMetricId metric)
{
	using MetricField = double ResultSummaryViewRow::*;
	constexpr std::array<MetricField, kResultMetricCapacity> fields = {
	    &ResultSummaryViewRow::medianPrimaryValue,          &ResultSummaryViewRow::minimumPrimaryValue,
	    &ResultSummaryViewRow::maximumPrimaryValue,         &ResultSummaryViewRow::medianWorkUnitMilliseconds,
	    &ResultSummaryViewRow::minimumWorkUnitMilliseconds, &ResultSummaryViewRow::maximumWorkUnitMilliseconds,
	};
	return row->*fields[static_cast<std::size_t>(metric)];
}

const EffectiveRunConfiguration* ResultViewConfiguration(const ResultViewModel* model)
{
	return model->resultSchemaVersion >= 7 ? &model->savedConfiguration : nullptr;
}

const CaseRecord& ResultViewCaseDefinition(const Catalog* catalog, const ResultViewModel* model)
{
	const EffectiveRunConfiguration* configuration = ResultViewConfiguration(model);
	return configuration != nullptr || model->descriptiveCasePresence == PresenceStatus_Present ? model->savedConfiguration.benchmarkCase : catalog->cases[model->caseIndex];
}

ArenaStatus LoadResultViewModel(const wchar_t* summaryPath, const wchar_t* normalizedPath, const wchar_t* timingPath,
                                const Catalog* catalog, const ResultManifestRecord* manifest, ResultViewModel* model,
                                StatusRecord* error, TimingArtifactTotals* totals)
{
	if (model != nullptr)
		{
			std::destroy_at(model);
			std::construct_at(model);
		}
	if (error != nullptr)
		*error = {};
	if (summaryPath == nullptr || catalog == nullptr || manifest == nullptr || model == nullptr || error == nullptr ||
	    manifest->engineCount == 0 || manifest->threadCount == 0 || manifest->repeatCount == 0 ||
	    manifest->repeatCount > kRunRepeatCapacity)
		return error != nullptr ? ViewError(error, ArenaStatus_InvalidArgument, "result_view_argument")
		                        : ArenaStatus_InvalidArgument;
	std::uint32_t caseIndex = 0;
	while (caseIndex < catalog->caseCount &&
	       CatalogTextView(catalog, catalog->cases[caseIndex].id) != ResultTextView(manifest, manifest->caseId))
		++caseIndex;
	if (caseIndex == catalog->caseCount && ResultConfiguration(manifest) == nullptr && manifest->descriptiveCasePresence != PresenceStatus_Present)
		return ViewError(error, ArenaStatus_InvalidResult, "result_view_case");
	std::uint32_t reportIndex = 0;
	while (reportIndex < catalog->reportCount &&
	       CatalogTextView(catalog, catalog->reports[reportIndex].caseId) != ResultTextView(manifest, manifest->caseId))
		++reportIndex;
	model->reportIndex = reportIndex;
	model->publicationReportPresence =
	    reportIndex < catalog->reportCount ? PresenceStatus_Present : PresenceStatus_Absent;
	model->caseIndex = caseIndex;
	model->resultSchemaVersion = manifest->schemaVersion;
	model->verificationMode = manifest->verificationMode;
	model->executionFailures = manifest->executionFailures;
	model->descriptiveCasePresence = manifest->descriptiveCasePresence;
	if (ResultConfiguration(manifest) != nullptr || manifest->descriptiveCasePresence == PresenceStatus_Present)
		model->savedConfiguration = manifest->configuration;
	const EffectiveRunConfiguration* configuration = ResultViewConfiguration(model);
	const CaseRecord& savedCase = ResultViewCaseDefinition(catalog, model);
	model->measurementMode = manifest->measurementMode;
	model->engineCount = manifest->engineCount;
	model->threadCount = manifest->threadCount;
	model->measuredWorkUnitCount = manifest->measuredWorkUnitCount;
	model->warmupWorkUnitCount = manifest->warmupWorkUnitCount;
	model->repeatCount = manifest->repeatCount;
	model->timestepHz = savedCase.timestepHz;
	model->bodyCount = savedCase.bodyCount;
	model->shapeCount = savedCase.shapeCount;
	model->queryCount = savedCase.queryCount;
	model->constraintCount = savedCase.constraintCount;
	model->host = manifest->host;
	model->renderResolutionPresence = manifest->renderResolutionPresence;
	model->renderWidthPixels = manifest->renderWidthPixels;
	model->renderHeightPixels = manifest->renderHeightPixels;
	std::copy(manifest->threadCounts.begin(), manifest->threadCounts.begin() + manifest->threadCount,
	          model->threadCounts.begin());
	const std::string_view benchmarkMode = ResultTextView(manifest, manifest->benchmarkMode);
	const std::string_view runProvenance = manifest->timingRenderSeries == TimingRenderSeries_SampledFrameWall
	                                           ? std::string_view("Asynchronous live visualization; adds overhead")
	                                           : benchmarkMode;
	const ResultCaseMetadata metadata =
	    ProjectResultCaseMetadata(catalog, savedCase, manifest->measurementMode, configuration != nullptr || model->descriptiveCasePresence == PresenceStatus_Present ? &model->savedConfiguration : nullptr);
	const CaseRecord benchmarkCase = RunObservationCase(metadata.record, manifest->verificationMode);
	std::string_view reportTitle = CaseConfigurationTextView(catalog, configuration != nullptr || model->descriptiveCasePresence == PresenceStatus_Present ? &model->savedConfiguration : nullptr, benchmarkCase.displayName);
	std::string_view chartLabel = metadata.primaryMetricLabel;
	std::string_view chartNote = metadata.primaryMetricNote;
	if (model->publicationReportPresence == PresenceStatus_Present &&
	    model->measurementMode == ResultMeasurementMode_Timed &&
	    (configuration == nullptr || configuration->mode == RunConfigurationMode_Authored))
	{
		const ReportRecord& report = catalog->reports[reportIndex];
		reportTitle = CatalogTextView(catalog, report.title);
		if (report.chartOverridePresence == PresenceStatus_Present &&
		    benchmarkCase.fixtureKind != CaseFixtureKind_RagdollStairTumble)
		{
			if (CatalogTextView(catalog, report.chartMetric) != metadata.primaryMetricId)
				return ViewError(error, ArenaStatus_InvalidResult, "result_view_report_metric");
			chartLabel = CatalogTextView(catalog, report.chartLabel);
			chartNote = CatalogTextView(catalog, report.chartNote);
		}
	}
	if (StoreViewText(model, &model->runId, ResultTextView(manifest, manifest->runId), error) != ArenaStatus_Ok ||
	    StoreViewText(model, &model->caseId, ResultTextView(manifest, manifest->caseId), error) != ArenaStatus_Ok ||
	    StoreViewText(model, &model->caseDisplayName,
	                  CaseConfigurationTextView(catalog, configuration != nullptr || model->descriptiveCasePresence == PresenceStatus_Present ? &model->savedConfiguration : nullptr, savedCase.displayName),
	                  error) != ArenaStatus_Ok ||
	    StoreViewText(model, &model->caseDescription,
	                  configuration != nullptr && configuration->mode == RunConfigurationMode_Custom
	                      ? std::string_view("Custom fixture and simulation settings.")
						  : CaseConfigurationTextView(catalog, configuration != nullptr || model->descriptiveCasePresence == PresenceStatus_Present ? &model->savedConfiguration : nullptr, savedCase.description),
	                  error) != ArenaStatus_Ok ||
	    StoreViewText(model, &model->benchmarkMode, benchmarkMode, error) != ArenaStatus_Ok ||
	    StoreViewText(model, &model->runProvenance, runProvenance, error) != ArenaStatus_Ok ||
	    StoreViewText(model, &model->hostRoute, ResultTextView(manifest, manifest->hostRoute), error) !=
	        ArenaStatus_Ok ||
	    StoreViewText(model, &model->reportTitle, reportTitle, error) != ArenaStatus_Ok ||
	    StoreViewText(model, &model->chartLabel, chartLabel, error) != ArenaStatus_Ok ||
	    StoreViewText(model, &model->chartNote, chartNote, error) != ArenaStatus_Ok)
		return error->code;
	model->primaryMetricDirection = metadata.direction;
	if (StoreViewText(model, &model->workUnitId,
	                  CaseConfigurationTextView(catalog, configuration != nullptr || model->descriptiveCasePresence == PresenceStatus_Present ? &model->savedConfiguration : nullptr, benchmarkCase.workUnitId),
	                  error) != ArenaStatus_Ok ||
	    StoreViewText(model, &model->workUnitLabel,
	                  CaseConfigurationTextView(catalog, configuration != nullptr || model->descriptiveCasePresence == PresenceStatus_Present ? &model->savedConfiguration : nullptr, benchmarkCase.workUnitLabel),
	                  error) != ArenaStatus_Ok ||
	    StoreViewText(model, &model->primaryMetricId, metadata.primaryMetricId, error) != ArenaStatus_Ok ||
	    StoreViewText(model, &model->primaryMetricUnit, metadata.primaryMetricUnit, error) != ArenaStatus_Ok)
		return error->code != ArenaStatus_Ok ? error->code
		                                     : ViewError(error, ArenaStatus_InvalidResult, "result_view_report_metric");
	model->resultGroupCount = benchmarkCase.resultGroupCount;
	model->observationCount = benchmarkCase.observationCount;
	for (std::uint32_t group = 0; group < benchmarkCase.resultGroupCount; ++group)
	{
		const ResultGroupRecord& source = configuration != nullptr
		                                      ? configuration->resultGroups[group]
		                                      : catalog->resultGroups[benchmarkCase.resultGroupOffset + group];
		ResultGroupView& destination = model->resultGroups[group];
		if (StoreViewText(model, &destination.label, CaseConfigurationTextView(catalog, configuration != nullptr || model->descriptiveCasePresence == PresenceStatus_Present ? &model->savedConfiguration : nullptr, source.label),
		                  error) != ArenaStatus_Ok)
			return error->code;
	}
	for (std::uint32_t observation = 0; observation < benchmarkCase.observationCount; ++observation)
	{
		const ObservationDeclaration& source =
		    CaseConfigurationObservation(catalog, benchmarkCase, configuration, observation);
		ResultObservationView& destination = model->observationViews[observation];
		destination.resultGroupOrdinal = source.resultGroupOrdinal;
		destination.valueType = source.valueType;
		destination.role = source.role;
		destination.expectedValuePresence = source.expectedValuePresence;
		if (StoreViewText(model, &destination.label, CaseConfigurationTextView(catalog, configuration != nullptr || model->descriptiveCasePresence == PresenceStatus_Present ? &model->savedConfiguration : nullptr, source.label),
		                  error) != ArenaStatus_Ok ||
		    StoreViewText(model, &destination.unit, CaseConfigurationTextView(catalog, configuration != nullptr || model->descriptiveCasePresence == PresenceStatus_Present ? &model->savedConfiguration : nullptr, source.unit),
		                  error) != ArenaStatus_Ok)
			return error->code;
	}
	PresenceStatus windowsX64 = PresenceStatus_Present;
	for (std::uint32_t engine = 0; engine < manifest->engineCount; ++engine)
		if (!ResultTextView(manifest, manifest->engines[engine].artifactManifestPath)
		         .starts_with("release/windows-x64/"))
			windowsX64 = PresenceStatus_Absent;
	const std::string_view releaseLabel = windowsX64 == PresenceStatus_Present
	                                          ? std::string_view("Windows x64 release files")
	                                          : std::string_view("Windows route");
	if (StoreViewText(model, &model->releaseSourceLabel, releaseLabel, error) != ArenaStatus_Ok)
		return error->code;
	if (model->measurementMode == ResultMeasurementMode_Timed)
	{
		if (AddMetric(model, ResultMetric_MedianPrimaryValue, "Median", ResultViewTextView(model, model->chartLabel),
		              ResultViewTextView(model, model->primaryMetricUnit),
		              model->primaryMetricDirection == PrimaryMetricDirection_LowerIsBetter ? PresenceStatus_Present
		                                                                                    : PresenceStatus_Absent,
		              error) != ArenaStatus_Ok)
			return error->code;
		if (AddMetric(model, ResultMetric_MinimumPrimaryValue, "Minimum", ResultViewTextView(model, model->chartLabel),
		              ResultViewTextView(model, model->primaryMetricUnit),
		              model->primaryMetricDirection == PrimaryMetricDirection_LowerIsBetter ? PresenceStatus_Present
		                                                                                    : PresenceStatus_Absent,
		              error) != ArenaStatus_Ok)
			return error->code;
		if (AddMetric(model, ResultMetric_MaximumPrimaryValue, "Maximum", ResultViewTextView(model, model->chartLabel),
		              ResultViewTextView(model, model->primaryMetricUnit),
		              model->primaryMetricDirection == PrimaryMetricDirection_LowerIsBetter ? PresenceStatus_Present
		                                                                                    : PresenceStatus_Absent,
		              error) != ArenaStatus_Ok)
			return error->code;
	}
	ResultViewContext context = {};
	context.catalog = catalog;
	context.manifest = manifest;
	context.model = model;
	CsvHeader header = {};
	CsvReadRecord record = {};
	const std::uint64_t expectedRows = static_cast<std::uint64_t>(manifest->engineCount) * manifest->threadCount;
	if (expectedRows > model->summaryRows.size() ||
	    ReadCsvFile(summaryPath, &header, ConsumeSummaryRow, &context, &record, error) != ArenaStatus_Ok ||
	    model->summaryRowCount != expectedRows)
	{
		{
			std::destroy_at(model);
			std::construct_at(model);
		}
		return error->code != ArenaStatus_Ok ? error->code
		                                     : ViewError(error, ArenaStatus_InvalidResult, "result_view_cardinality");
	}
	for (std::uint32_t engine = 0; engine < manifest->engineCount; ++engine)
		for (std::uint32_t thread = 0; thread < manifest->threadCount; ++thread)
			if (context.seen[engine][thread] != PresenceStatus_Present)
			{
				{
					std::destroy_at(model);
					std::construct_at(model);
				}
				return ViewError(error, ArenaStatus_InvalidResult, "result_view_missing_unit");
			}
	const int rayFrame = CaseConfigurationTextView(catalog, configuration != nullptr || model->descriptiveCasePresence == PresenceStatus_Present ? &model->savedConfiguration : nullptr, benchmarkCase.workUnitId) == "ray_frame";
	if (normalizedPath != nullptr)
	{
		std::unique_ptr<NormalizedOutcomeContext> outcomes(new (std::nothrow) NormalizedOutcomeContext{});
		if (outcomes == nullptr)
			return ViewError(error, ArenaStatus_RunFailed, "result_view_outcome_allocation");
		outcomes->validation.catalog = catalog;
		outcomes->validation.manifest = manifest;
		outcomes->model = model;
		for (std::uint32_t index = 0; index < model->summaryRowCount; ++index)
		{
			const ResultSummaryViewRow& row = model->summaryRows[index];
			outcomes->rowIndexes[row.engineOrdinal * manifest->threadCount + ThreadOrdinal(manifest, row.threadCount)] = index;
		}
		if (ReadCsvFile(normalizedPath, &header, ConsumeNormalizedOutcomeRow, outcomes.get(), &record, error) != ArenaStatus_Ok ||
		    ValidateNormalizedCoverage(outcomes->validation, error) != ArenaStatus_Ok)
			return error->code;
	}
	if (rayFrame != 0 || CaseConfigurationTextView(catalog, configuration != nullptr || model->descriptiveCasePresence == PresenceStatus_Present ? &model->savedConfiguration : nullptr, benchmarkCase.workUnitId) == "query_batch")
	{
		if (ProjectNormalizedQueryBatchLatency(normalizedPath, catalog, manifest, model, error) != ArenaStatus_Ok ||
		    AddMetric(model, ResultMetric_MedianWorkUnitMilliseconds, "Median", rayFrame != 0 ? "Ordinary coherent primary frame" : "Query batch time", "ms",
		              PresenceStatus_Present, error) != ArenaStatus_Ok ||
		    AddMetric(model, ResultMetric_MinimumWorkUnitMilliseconds, "Minimum", rayFrame != 0 ? "Ordinary coherent primary frame" : "Query batch time", "ms",
		              PresenceStatus_Present, error) != ArenaStatus_Ok ||
		    AddMetric(model, ResultMetric_MaximumWorkUnitMilliseconds, "Maximum", rayFrame != 0 ? "Ordinary coherent primary frame" : "Query batch time", "ms",
		              PresenceStatus_Present, error) != ArenaStatus_Ok)
		{
			{
				std::destroy_at(model);
				std::construct_at(model);
			}
			return error->code;
		}
	}
	for (std::uint32_t rowIndex = 0; rowIndex < model->summaryRowCount; ++rowIndex)
		model->displayRowIndexes[rowIndex] = rowIndex;
	std::sort(model->displayRowIndexes.begin(), model->displayRowIndexes.begin() + model->summaryRowCount,
	          [model](std::uint32_t leftIndex, std::uint32_t rightIndex)
	          {
		          const ResultSummaryViewRow& left = model->summaryRows[leftIndex];
		          const ResultSummaryViewRow& right = model->summaryRows[rightIndex];
		          if (left.threadCount != right.threadCount)
			          return left.threadCount < right.threadCount;
		          if (left.medianPrimaryValue != right.medianPrimaryValue)
			          return model->primaryMetricDirection == PrimaryMetricDirection_HigherIsBetter
			                     ? left.medianPrimaryValue > right.medianPrimaryValue
								 : left.medianPrimaryValue < right.medianPrimaryValue;
		          const ResultEngineView& leftEngine = model->engines[left.engineOrdinal];
		          const ResultEngineView& rightEngine = model->engines[right.engineOrdinal];
		          if (leftEngine.presentationOrder != rightEngine.presentationOrder)
			          return leftEngine.presentationOrder < rightEngine.presentationOrder;
		          return ResultViewTextView(model, leftEngine.id) < ResultViewTextView(model, rightEngine.id);
	          });
	model->displayRowCount = model->summaryRowCount;
	if (model->measurementMode == ResultMeasurementMode_Timed &&
	    (timingPath == nullptr ||
	     LoadTimingArtifactIndex(timingPath, catalog, manifest, &model->timing, error, totals) != ArenaStatus_Ok))
	{
		{
			std::destroy_at(model);
			std::construct_at(model);
		}
		return error->code != ArenaStatus_Ok
		           ? error->code
				   : ViewError(error, ArenaStatus_InvalidResult, "result_view_timing_missing");
	}
	if (model->measurementMode == ResultMeasurementMode_PhysicalQuality && timingPath != nullptr &&
	    GetFileAttributesW(timingPath) != INVALID_FILE_ATTRIBUTES)
		return ViewError(error, ArenaStatus_InvalidResult, "quality_run_contains_timing");
	std::array<wchar_t, kTimingArtifactPathCapacity> observationPath = {};
	if (BuildObservationPath(timingPath, &observationPath, error) != ArenaStatus_Ok ||
	    LoadObservationResultModel(observationPath.data(), catalog, &benchmarkCase, manifest, &model->observations,
	                               error) != ArenaStatus_Ok)
	{
		{
			std::destroy_at(model);
			std::construct_at(model);
		}
		return error->code != ArenaStatus_Ok
		           ? error->code
				   : ViewError(error, ArenaStatus_InvalidResult, "result_view_observation_missing");
	}
	model->requiredStabilityCriterion = CurrentStackCriterion(benchmarkCase.fixtureKind);
	model->stabilityRequired = manifest->verificationMode == VerificationMode_On && benchmark_stack::TargetFixture(benchmarkCase.fixtureKind) != 0 ? PresenceStatus_Present : PresenceStatus_Absent;
	if (manifest->stabilityPresence == PresenceStatus_Present)
	{
		const std::filesystem::path stabilityPath = std::filesystem::path(observationPath.data()).parent_path() / "stability.csv";
		if (LoadStackStability(stabilityPath, &model->stabilityResults, error) != ArenaStatus_Ok)
			return error->code;
		for (const StackStabilityResult& tuple : model->stabilityResults)
		{
			std::uint32_t engine = 0;
			while (engine < model->engineCount && ResultViewTextView(model, model->engines[engine].id) != tuple.engineId)
				++engine;
			if (tuple.criterion != manifest->stabilityCriterion || tuple.runId != ResultViewTextView(model, model->runId) || engine == model->engineCount ||
			    tuple.repeatIndex >= manifest->repeatCount || ThreadOrdinal(manifest, tuple.threadCount) >= manifest->threadCount)
				return ViewError(error, ArenaStatus_InvalidResult, "stability_result_identity");
			if (ValidateStackShapeStabilityEvidence(ResolveEngineCaseExecution(&manifest->configuration, engine), tuple, error) != ArenaStatus_Ok)
				return error->code;
			const ExecutionFailure* terminal = FindExecutionFailure(manifest->executionFailures,
			    manifest->engines[engine].engineIndex, tuple.threadCount, tuple.repeatIndex);
			if (terminal != nullptr && terminal->outcome == ExecutionOutcome_NotRun)
				return ViewError(error, ArenaStatus_InvalidResult, "stability_skipped_tuple_conflict");
		}
		for (std::uint32_t engine = 0; engine < model->engineCount; ++engine)
			for (std::uint32_t thread = 0; thread < manifest->threadCount; ++thread)
				for (std::uint32_t repeat = 0; repeat < manifest->repeatCount; ++repeat)
				{
					const std::uint32_t count = manifest->threadCounts[thread];
					if (FindExecutionFailure(manifest->executionFailures, manifest->engines[engine].engineIndex, count, repeat) != nullptr)
						continue;
					const std::string_view id = ResultViewTextView(model, model->engines[engine].id);
					const StackStabilityResult* found = nullptr;
					for (const StackStabilityResult& tuple : model->stabilityResults)
						if (tuple.engineId == id && tuple.threadCount == count && tuple.repeatIndex == repeat)
							found = &tuple;
					if (found == nullptr || found->coverage != StackCoverage_Complete)
						return ViewError(error, ArenaStatus_InvalidResult, "stability_declared_tuple_incomplete");
				}
	}
	for (std::uint32_t index = 0; index < model->summaryRowCount; ++index)
	{
		ResultSummaryViewRow& row = model->summaryRows[index];
		row.stability = AggregateStackStability(model->stabilityResults,
		    ResultViewTextView(model, model->engines[row.engineOrdinal].id), row.threadCount, manifest->repeatCount, model->requiredStabilityCriterion);
		if (model->stabilityRequired == PresenceStatus_Present && row.outcome != ObservationOutcome_Failed)
			row.outcome = row.stability == StackAssessment_Fail ? ObservationOutcome_Failed :
			    row.stability == StackAssessment_Pass ? row.outcome : ObservationOutcome_Unknown;
	}
	if (benchmarkCase.observationCount != 0)
	{
		for (std::uint32_t rowIndex = 0; rowIndex < model->summaryRowCount; ++rowIndex)
		{
			ResultSummaryViewRow& row = model->summaryRows[rowIndex];
			const std::uint32_t threadOrdinal = ThreadOrdinal(manifest, row.threadCount);
			if (row.outcome != ObservationOutcome_Failed &&
			    (model->stabilityRequired == PresenceStatus_Absent || row.stability == StackAssessment_Pass))
				row.outcome = ObservationOutcome_Ok;
			for (std::uint32_t declaration = 0; declaration < benchmarkCase.observationCount; ++declaration)
			{
				const ObservationAggregate* aggregate =
				    ObservationAggregateAt(&model->observations, row.engineOrdinal, threadOrdinal, declaration);
				if (aggregate == nullptr)
				{
					{
						std::destroy_at(model);
						std::construct_at(model);
					}
					return ViewError(error, ArenaStatus_InvalidResult, "result_view_observation_aggregate");
				}
				if (aggregate->outcome == ObservationOutcome_Failed)
					row.outcome = ObservationOutcome_Failed;
			}
		}
		ObservationDetailSelection selection = {manifest->engines[0].engineIndex, manifest->threadCounts[0], 0};
		if (FindExecutionFailure(manifest->executionFailures, selection.engineIndex, selection.threadCount, selection.repeatIndex) == nullptr &&
		    ProjectObservationDetail(&model->observations, catalog, &benchmarkCase, &selection,
		                             &model->observationDetail, error, configuration) != ArenaStatus_Ok)
		{
			{
				std::destroy_at(model);
				std::construct_at(model);
			}
			return error->code;
		}
	}
	if (model->verificationMode == VerificationMode_Off)
		for (std::uint32_t index = 0; index < model->summaryRowCount; ++index)
			if (model->summaryRows[index].outcome != ObservationOutcome_Failed)
				model->summaryRows[index].outcome = ObservationOutcome_Unknown;
	return ArenaStatus_Ok;
}
} // namespace physics_arena
