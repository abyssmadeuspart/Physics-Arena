#include "physics_arena/timing_series.h"

#include "physics_arena/csv_io.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cwchar>
#include <string_view>

namespace physics_arena
{
namespace
{
constexpr std::array<std::string_view, 8> kTimingColumns = {"run_id",          "case_id",        "engine_id",
                                                            "thread_count",    "repeat_index",   "step_index",
                                                            "physics_step_ms", "render_frame_ms"};

struct TimingCsvContext
{
	const Catalog* catalog;
	const ResultManifestRecord* manifest;
	TimingSeriesModel* model;
	std::array<std::int32_t, 8> headerIndexes;
	std::uint32_t rowCount;
	PresenceStatus headerMapped;
};

struct TimingIndexContext
{
	const Catalog* catalog;
	const ResultManifestRecord* manifest;
	TimingArtifactIndex* index;
	TimingArtifactTotals* totals;
	std::array<std::int32_t, 8> headerIndexes;
	std::uint64_t rowCount;
	PresenceStatus headerMapped;
};

struct TimingSliceContext
{
	const TimingArtifactSlice* slice;
	TimingProjectionScratch* scratch;
	std::array<std::int32_t, 8> headerIndexes;
	std::uint32_t rowCount;
	PresenceStatus headerMapped;
	TimingRenderSeries renderSeries;
};

ArenaStatus TimingError(StatusRecord* error, ArenaStatus status, std::string_view detail)
{
	*error = {};
	const std::string_view component = "timing_series";
	const std::string_view statusText = ArenaStatusText(status);
	std::copy(component.begin(), component.end(), error->component.begin());
	error->componentSize = static_cast<std::uint32_t>(component.size());
	std::copy(statusText.begin(), statusText.end(), error->status.begin());
	error->statusSize = static_cast<std::uint32_t>(statusText.size());
	if (detail.size() > error->detail.size())
		detail = "timing_detail_capacity";
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

ArenaStatus ParseTimingDouble(std::string_view text, int positive, double* value)
{
	if (text.empty())
		return ArenaStatus_InvalidResult;
	const std::from_chars_result result =
	    std::from_chars(text.data(), text.data() + text.size(), *value, std::chars_format::general);
	if (result.ec != std::errc() || result.ptr != text.data() + text.size() || !std::isfinite(*value))
		return ArenaStatus_InvalidResult;
	return positive != 0 ? (*value > 0.0 ? ArenaStatus_Ok : ArenaStatus_InvalidResult)
	                     : (*value >= 0.0 ? ArenaStatus_Ok : ArenaStatus_InvalidResult);
}

ArenaStatus ParseRenderSample(std::string_view text, TimingRenderSeries series, double* value,
                              TimingRenderSamplePresence* presence)
{
	*value = 0.0;
	*presence = TimingRenderSamplePresence_Absent;
	if (series == TimingRenderSeries_Absent)
		return text.empty() ? ArenaStatus_Ok : ArenaStatus_InvalidResult;
	if (series == TimingRenderSeries_SampledFrameWall && text.empty())
		return ArenaStatus_Ok;
	if (series != TimingRenderSeries_SampledFrameWall || ParseTimingDouble(text, 0, value) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	*presence = TimingRenderSamplePresence_Present;
	return ArenaStatus_Ok;
}

template <typename Context> ArenaStatus MapTimingHeader(const CsvHeader* header, Context* context, StatusRecord* error)
{
	context->headerIndexes.fill(-1);
	if (header->fieldCount != kTimingColumns.size())
		return TimingError(error, ArenaStatus_InvalidResult, "timing_header_count");
	for (std::uint32_t column = 0; column < header->fieldCount; ++column)
	{
		const std::string_view name = CsvHeaderTextView(header, header->fields[column]);
		std::uint32_t expected = 0;
		while (expected < kTimingColumns.size() && kTimingColumns[expected] != name)
			++expected;
		if (expected == kTimingColumns.size())
			return TimingError(error, ArenaStatus_InvalidResult, "timing_header_column");
		context->headerIndexes[expected] = static_cast<std::int32_t>(column);
	}
	for (const std::int32_t index : context->headerIndexes)
		if (index < 0)
			return TimingError(error, ArenaStatus_InvalidResult, "timing_header_missing");
	context->headerMapped = PresenceStatus_Present;
	return ArenaStatus_Ok;
}

template <typename Context> std::string_view TimingValue(const CsvRow* row, const Context* context, std::uint32_t field)
{
	return CsvRowTextView(row, row->fields[static_cast<std::uint32_t>(context->headerIndexes[field])]);
}

ArenaStatus ConsumeTimingRow(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error)
{
	TimingCsvContext* context = static_cast<TimingCsvContext*>(opaque);
	if (context->headerMapped != PresenceStatus_Present && MapTimingHeader(header, context, error) != ArenaStatus_Ok)
		return error->code;
	const std::uint64_t expectedRows = static_cast<std::uint64_t>(context->manifest->engineCount) *
	                                   context->manifest->threadCount * context->manifest->repeatCount *
	                                   context->manifest->measuredWorkUnitCount;
	if (context->rowCount >= expectedRows)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_row_overflow");
	const std::uint32_t unitIndex = context->rowCount / context->manifest->measuredWorkUnitCount;
	const std::uint32_t expectedStep = context->rowCount % context->manifest->measuredWorkUnitCount + 1;
	if (unitIndex == context->model->unitCount)
	{
		const std::uint32_t unitsPerEngine = context->manifest->threadCount * context->manifest->repeatCount;
		const std::uint32_t engineOrdinal = unitIndex / unitsPerEngine;
		const std::uint32_t withinEngine = unitIndex % unitsPerEngine;
		const std::uint32_t threadOrdinal = withinEngine / context->manifest->repeatCount;
		const std::uint32_t repeatOrdinal = withinEngine % context->manifest->repeatCount;
		std::uint32_t appendedUnit = 0;
		if (BeginTimingUnit(context->model, context->manifest->engines[engineOrdinal].engineIndex,
		                    context->manifest->threadCounts[threadOrdinal], repeatOrdinal,
		                    context->manifest->measuredWorkUnitCount, &appendedUnit, error) != ArenaStatus_Ok ||
		    appendedUnit != unitIndex)
			return error->code;
	}
	else if (unitIndex > context->model->unitCount)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_unit_order");
	const TimingUnitRange& unit = context->model->units[unitIndex];
	const std::string_view engineId = CatalogTextView(context->catalog, context->catalog->engines[unit.engineIndex].id);
	std::uint32_t threadCount = 0;
	std::uint32_t repeatIndex = 0;
	std::uint32_t stepIndex = 0;
	double physics = 0.0;
	double render = 0.0;
	TimingRenderSamplePresence renderPresence = TimingRenderSamplePresence_Absent;
	if (TimingValue(row, context, 0) != ResultTextView(context->manifest, context->manifest->runId) ||
	    TimingValue(row, context, 1) != ResultTextView(context->manifest, context->manifest->caseId) ||
	    TimingValue(row, context, 2) != engineId ||
	    ParseUnsigned(TimingValue(row, context, 3), &threadCount) != ArenaStatus_Ok ||
	    ParseUnsigned(TimingValue(row, context, 4), &repeatIndex) != ArenaStatus_Ok ||
	    ParseUnsigned(TimingValue(row, context, 5), &stepIndex) != ArenaStatus_Ok ||
	    ParseTimingDouble(TimingValue(row, context, 6), 1, &physics) != ArenaStatus_Ok ||
	    ParseRenderSample(TimingValue(row, context, 7), context->manifest->timingRenderSeries, &render,
	                      &renderPresence) != ArenaStatus_Ok)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_row_value");
	if (threadCount != unit.threadCount || repeatIndex != unit.repeatIndex || stepIndex != expectedStep)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_row_identity");
	if (AppendTimingSample(context->model, unitIndex, stepIndex, physics, render, renderPresence, error) !=
	    ArenaStatus_Ok)
		return error->code;
	context->rowCount += 1;
	return ArenaStatus_Ok;
}

ArenaStatus WriteTimingText(CsvWriter* writer, std::string_view value, std::uint32_t field, StatusRecord* error)
{
	return WriteCsvField(writer, value, field == 7 ? CsvFieldTerminator_EndRow : CsvFieldTerminator_MoreFields, error);
}

ArenaStatus WriteTimingUnsigned(CsvWriter* writer, std::uint32_t value, std::uint32_t field, StatusRecord* error)
{
	std::array<char, 32> text = {};
	const std::to_chars_result result = std::to_chars(text.data(), text.data() + text.size(), value);
	return result.ec == std::errc() ? WriteTimingText(writer, std::string_view(text.data(), result.ptr), field, error)
	                                : TimingError(error, ArenaStatus_InvalidResult, "timing_unsigned_format");
}

ArenaStatus WriteTimingDouble(CsvWriter* writer, double value, std::uint32_t field, StatusRecord* error)
{
	std::array<char, 64> text = {};
	const std::to_chars_result result =
	    std::to_chars(text.data(), text.data() + text.size(), value, std::chars_format::fixed, 9);
	return result.ec == std::errc() ? WriteTimingText(writer, std::string_view(text.data(), result.ptr), field, error)
	                                : TimingError(error, ArenaStatus_InvalidResult, "timing_double_format");
}

double Median(std::array<double, kTimingRepeatCapacity>* values, std::uint32_t count)
{
	std::sort(values->begin(), values->begin() + count);
	return count % 2 != 0 ? (*values)[count / 2] : ((*values)[count / 2 - 1] + (*values)[count / 2]) / 2.0;
}

void CompleteProjectionStatistics(double* statisticsScratch, TimingProjection* projection)
{
	if (projection->sampleCount == 0)
		return;
	projection->maximumPhysicsStepMillisecondsValue = projection->physicsStepMilliseconds[0];
	projection->worstStepIndex = projection->stepIndexes[0];
	for (std::uint32_t index = 0; index < projection->sampleCount; ++index)
	{
		const double value = projection->physicsStepMilliseconds[index];
		statisticsScratch[index] = value;
		if (value > projection->maximumPhysicsStepMillisecondsValue)
		{
			projection->maximumPhysicsStepMillisecondsValue = value;
			projection->worstStepIndex = projection->stepIndexes[index];
		}
	}
	std::sort(statisticsScratch, statisticsScratch + projection->sampleCount);
	const std::uint32_t middle = projection->sampleCount / 2;
	projection->medianPhysicsStepMilliseconds = projection->sampleCount % 2 != 0
	                                                ? statisticsScratch[middle]
	                                                : (statisticsScratch[middle - 1] + statisticsScratch[middle]) / 2.0;
	const std::uint32_t p95 = static_cast<std::uint32_t>(std::ceil(0.95 * projection->sampleCount)) - 1;
	const std::uint32_t p99 = static_cast<std::uint32_t>(std::ceil(0.99 * projection->sampleCount)) - 1;
	projection->p95PhysicsStepMilliseconds = statisticsScratch[p95];
	projection->p99PhysicsStepMilliseconds = statisticsScratch[p99];
}

ArenaStatus ConsumeTimingIndexRow(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error)
{
	TimingIndexContext* context = static_cast<TimingIndexContext*>(opaque);
	if (context->headerMapped != PresenceStatus_Present && MapTimingHeader(header, context, error) != ArenaStatus_Ok)
		return error->code;
	std::uint32_t engineOrdinal = 0, threadOrdinal = 0;
	while (engineOrdinal < context->manifest->engineCount && TimingValue(row, context, 2) !=
	       CatalogTextView(context->catalog, context->catalog->engines[context->manifest->engines[engineOrdinal].engineIndex].id))
		++engineOrdinal;
	std::uint32_t threadCount = 0;
	if (engineOrdinal == context->manifest->engineCount || ParseUnsigned(TimingValue(row, context, 3), &threadCount) != ArenaStatus_Ok)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_index_identity");
	while (threadOrdinal < context->manifest->threadCount && context->manifest->threadCounts[threadOrdinal] != threadCount)
		++threadOrdinal;
	if (threadOrdinal == context->manifest->threadCount)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_index_thread");
	const std::uint32_t engineIndex = context->manifest->engines[engineOrdinal].engineIndex;
	if (context->index->sliceCount == 0 || context->index->slices[context->index->sliceCount - 1].engineIndex != engineIndex ||
	    context->index->slices[context->index->sliceCount - 1].threadCount != threadCount)
	{
		for (std::uint32_t index = 0; index < context->index->sliceCount; ++index)
			if (context->index->slices[index].engineIndex == engineIndex && context->index->slices[index].threadCount == threadCount)
				return TimingError(error, ArenaStatus_InvalidResult, "timing_index_noncontiguous_slice");
		if (context->index->sliceCount == context->index->slices.size())
			return TimingError(error, ArenaStatus_InvalidResult, "timing_index_slice_capacity");
		TimingArtifactSlice& added = context->index->slices[context->index->sliceCount++];
		added.engineIndex = engineIndex;
		added.threadCount = threadCount;
		added.measuredWorkUnitCount = context->manifest->measuredWorkUnitCount;
		added.firstRowByteOffset = row->sourceByteOffset;
	}
	TimingArtifactSlice& slice = context->index->slices[context->index->sliceCount - 1];
	const std::string_view engineId = CatalogTextView(context->catalog, context->catalog->engines[engineIndex].id);
	std::uint32_t parsedThread = 0;
	std::uint32_t parsedRepeat = 0;
	std::uint32_t parsedStep = 0;
	double physics = 0.0;
	const std::string_view renderText = TimingValue(row, context, 7);
	double render = 0.0;
	TimingRenderSamplePresence renderPresence = TimingRenderSamplePresence_Absent;
	if (TimingValue(row, context, 0) != ResultTextView(context->manifest, context->manifest->runId) ||
	    TimingValue(row, context, 1) != ResultTextView(context->manifest, context->manifest->caseId) ||
	    TimingValue(row, context, 2) != engineId ||
	    ParseUnsigned(TimingValue(row, context, 3), &parsedThread) != ArenaStatus_Ok ||
	    ParseUnsigned(TimingValue(row, context, 4), &parsedRepeat) != ArenaStatus_Ok ||
	    ParseUnsigned(TimingValue(row, context, 5), &parsedStep) != ArenaStatus_Ok ||
	    ParseTimingDouble(TimingValue(row, context, 6), 1, &physics) != ArenaStatus_Ok ||
	    ParseRenderSample(renderText, context->manifest->timingRenderSeries, &render, &renderPresence) !=
	        ArenaStatus_Ok)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_index_row_value");
	if (parsedThread != slice.threadCount || parsedRepeat >= context->manifest->repeatCount ||
	    parsedStep != slice.rowCount % slice.measuredWorkUnitCount + 1)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_index_row_identity");
	if (parsedStep == 1)
	{
		if (slice.repeatCount >= kTimingRepeatCapacity ||
		    (slice.repeatCount != 0 && parsedRepeat <= slice.repeatIndexes[slice.repeatCount - 1]))
			return TimingError(error, ArenaStatus_InvalidResult, "timing_index_repeat_order");
		slice.repeatIndexes.push_back(parsedRepeat);
		++slice.repeatCount;
	}
	if (parsedRepeat != slice.repeatIndexes[slice.repeatCount - 1])
		return TimingError(error, ArenaStatus_InvalidResult, "timing_index_repeat_identity");
	++slice.rowCount;
	if (context->totals != nullptr)
		context->totals->physicsMilliseconds[engineOrdinal * context->manifest->threadCount + threadOrdinal][parsedRepeat] += physics;
	context->rowCount += 1;
	return ArenaStatus_Ok;
}

ArenaStatus ConsumeTimingSliceRow(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error)
{
	TimingSliceContext* context = static_cast<TimingSliceContext*>(opaque);
	if (context->headerMapped != PresenceStatus_Present && MapTimingHeader(header, context, error) != ArenaStatus_Ok)
		return error->code;
	if (context->rowCount >= context->slice->rowCount)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_slice_row_overflow");
	const std::uint32_t repeatIndex = context->slice->repeatIndexes[context->rowCount / context->slice->measuredWorkUnitCount];
	const std::uint32_t stepIndex = context->rowCount % context->slice->measuredWorkUnitCount + 1;
	std::uint32_t parsedThread = 0;
	std::uint32_t parsedRepeat = 0;
	std::uint32_t parsedStep = 0;
	double physics = 0.0;
	const std::string_view renderText = TimingValue(row, context, 7);
	double render = 0.0;
	TimingRenderSamplePresence renderPresence = TimingRenderSamplePresence_Absent;
	if (ParseUnsigned(TimingValue(row, context, 3), &parsedThread) != ArenaStatus_Ok ||
	    ParseUnsigned(TimingValue(row, context, 4), &parsedRepeat) != ArenaStatus_Ok ||
	    ParseUnsigned(TimingValue(row, context, 5), &parsedStep) != ArenaStatus_Ok ||
	    ParseTimingDouble(TimingValue(row, context, 6), 1, &physics) != ArenaStatus_Ok ||
	    ParseRenderSample(renderText, context->renderSeries, &render, &renderPresence) != ArenaStatus_Ok)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_slice_row_value");
	if (parsedThread != context->slice->threadCount || parsedRepeat != repeatIndex || parsedStep != stepIndex)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_slice_row_identity");
	context->scratch->physicsStepMilliseconds[context->rowCount++] = physics;
	return ArenaStatus_Ok;
}
} // namespace

void InitializeTimingSeries(TimingSeriesModel* model, PresenceStatus availability)
{
	if (model == nullptr)
		return;
	*model = {};
	model->availability = availability;
}

ArenaStatus BeginTimingUnit(TimingSeriesModel* model, std::uint32_t engineIndex, std::uint32_t threadCount,
                            std::uint32_t repeatIndex, std::uint32_t expectedWorkUnitCount, std::uint32_t* unitIndex,
                            StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (model == nullptr || unitIndex == nullptr || error == nullptr || model->availability != PresenceStatus_Present ||
	    threadCount == 0 || expectedWorkUnitCount == 0 || expectedWorkUnitCount > kTimingProjectionStepCapacity ||
	    model->unitCount >= model->units.size() ||
	    expectedWorkUnitCount > model->stepIndexes.size() - model->sampleCount)
		return error != nullptr ? TimingError(error, ArenaStatus_InvalidArgument, "timing_unit_capacity_or_argument")
		                        : ArenaStatus_InvalidArgument;
	for (std::uint32_t index = 0; index < model->unitCount; ++index)
		if (model->units[index].engineIndex == engineIndex && model->units[index].threadCount == threadCount &&
		    model->units[index].repeatIndex == repeatIndex)
			return TimingError(error, ArenaStatus_InvalidResult, "timing_unit_duplicate");
	*unitIndex = model->unitCount++;
	TimingUnitRange& unit = model->units[*unitIndex];
	unit.engineIndex = engineIndex;
	unit.threadCount = threadCount;
	unit.repeatIndex = repeatIndex;
	unit.sampleOffset = model->sampleCount;
	unit.expectedWorkUnitCount = expectedWorkUnitCount;
	return ArenaStatus_Ok;
}

ArenaStatus AppendTimingSample(TimingSeriesModel* model, std::uint32_t unitIndex, std::uint32_t stepIndex,
                               double physicsStepMilliseconds, double renderFrameMilliseconds,
                               TimingRenderSamplePresence renderPresence, StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (model == nullptr || error == nullptr || unitIndex >= model->unitCount ||
	    !std::isfinite(physicsStepMilliseconds) || physicsStepMilliseconds <= 0.0 ||
	    !std::isfinite(renderFrameMilliseconds) || renderFrameMilliseconds < 0.0 ||
	    (renderPresence != TimingRenderSamplePresence_Absent && renderPresence != TimingRenderSamplePresence_Present) ||
	    (renderPresence == TimingRenderSamplePresence_Absent && renderFrameMilliseconds != 0.0))
		return error != nullptr ? TimingError(error, ArenaStatus_InvalidArgument, "timing_sample_argument")
		                        : ArenaStatus_InvalidArgument;
	TimingUnitRange& unit = model->units[unitIndex];
	if (unit.completion == PresenceStatus_Present || unit.sampleCount >= unit.expectedWorkUnitCount ||
	    model->sampleCount >= model->stepIndexes.size() || stepIndex != unit.sampleCount + 1 ||
	    model->sampleCount != unit.sampleOffset + unit.sampleCount)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_sample_order_or_capacity");
	model->stepIndexes[model->sampleCount] = stepIndex;
	model->physicsStepMilliseconds[model->sampleCount] = physicsStepMilliseconds;
	model->renderFrameMilliseconds[model->sampleCount] = renderFrameMilliseconds;
	model->renderSamplePresence[model->sampleCount] = renderPresence;
	model->sampleCount += 1;
	unit.sampleCount += 1;
	unit.physicsTotalMs += physicsStepMilliseconds;
	if (renderPresence == TimingRenderSamplePresence_Present)
		unit.renderTotalMs += renderFrameMilliseconds;
	return ArenaStatus_Ok;
}

ArenaStatus AppendCumulativeTiming(TimingSeriesModel* model, std::uint32_t unitIndex, std::uint32_t stepIndex,
                                   double cumulativePhysicsMilliseconds, double renderFrameMilliseconds,
                                   TimingRenderSamplePresence renderPresence, StatusRecord* error)
{
	if (model == nullptr || error == nullptr || unitIndex >= model->unitCount ||
	    !std::isfinite(cumulativePhysicsMilliseconds) || cumulativePhysicsMilliseconds <= 0.0)
		return error != nullptr ? TimingError(error, ArenaStatus_InvalidArgument, "timing_cumulative_argument")
		                        : ArenaStatus_InvalidArgument;
	TimingUnitRange& unit = model->units[unitIndex];
	const double delta = cumulativePhysicsMilliseconds - unit.previousCumulativePhysicsMs;
	if (!std::isfinite(delta) || delta <= 0.0)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_cumulative_nonmonotonic");
	if (AppendTimingSample(model, unitIndex, stepIndex, delta, renderFrameMilliseconds, renderPresence, error) !=
	    ArenaStatus_Ok)
		return error->code;
	unit.previousCumulativePhysicsMs = cumulativePhysicsMilliseconds;
	return ArenaStatus_Ok;
}

ArenaStatus SetTimingRenderSample(TimingSeriesModel* model, std::uint32_t unitIndex, std::uint32_t stepIndex,
                                  double renderFrameMilliseconds, StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (model == nullptr || error == nullptr || unitIndex >= model->unitCount || stepIndex == 0 ||
	    !std::isfinite(renderFrameMilliseconds) || renderFrameMilliseconds < 0.0)
		return error != nullptr ? TimingError(error, ArenaStatus_InvalidArgument, "timing_render_sample_argument")
		                        : ArenaStatus_InvalidArgument;
	TimingUnitRange& unit = model->units[unitIndex];
	if (unit.completion == PresenceStatus_Present || stepIndex > unit.sampleCount)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_render_sample_order");
	const std::uint32_t sample = unit.sampleOffset + stepIndex - 1;
	if (model->stepIndexes[sample] != stepIndex ||
	    model->renderSamplePresence[sample] != TimingRenderSamplePresence_Absent)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_render_sample_duplicate");
	model->renderFrameMilliseconds[sample] = renderFrameMilliseconds;
	model->renderSamplePresence[sample] = TimingRenderSamplePresence_Present;
	unit.renderTotalMs += renderFrameMilliseconds;
	return ArenaStatus_Ok;
}

ArenaStatus CompleteTimingUnit(TimingSeriesModel* model, std::uint32_t unitIndex,
                               double expectedPhysicsTotalMilliseconds, double expectedRenderTotalMilliseconds,
                               double toleranceMilliseconds, StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (model == nullptr || error == nullptr || unitIndex >= model->unitCount ||
	    !std::isfinite(expectedPhysicsTotalMilliseconds) || expectedPhysicsTotalMilliseconds <= 0.0 ||
	    !std::isfinite(expectedRenderTotalMilliseconds) || expectedRenderTotalMilliseconds < 0.0 ||
	    !std::isfinite(toleranceMilliseconds) || toleranceMilliseconds < 0.0)
		return error != nullptr ? TimingError(error, ArenaStatus_InvalidArgument, "timing_complete_argument")
		                        : ArenaStatus_InvalidArgument;
	TimingUnitRange& unit = model->units[unitIndex];
	if (unit.completion == PresenceStatus_Present || unit.sampleCount != unit.expectedWorkUnitCount ||
	    std::abs(unit.physicsTotalMs - expectedPhysicsTotalMilliseconds) > toleranceMilliseconds ||
	    std::abs(unit.renderTotalMs - expectedRenderTotalMilliseconds) > toleranceMilliseconds)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_complete_cardinality_or_total");
	unit.completion = PresenceStatus_Present;
	return ArenaStatus_Ok;
}

ArenaStatus ProjectTimingMedian(const TimingSeriesModel* model, std::uint32_t engineIndex, std::uint32_t threadCount,
                                TimingProjection* projection, StatusRecord* error)
{
	if (projection != nullptr)
		*projection = {};
	if (error != nullptr)
		*error = {};
	if (model == nullptr || projection == nullptr || error == nullptr ||
	    model->availability != PresenceStatus_Present || threadCount == 0)
		return error != nullptr ? TimingError(error, ArenaStatus_InvalidArgument, "timing_projection_argument")
		                        : ArenaStatus_InvalidArgument;
	std::array<std::uint32_t, kTimingRepeatCapacity> unitIndexes = {};
	for (std::uint32_t index = 0; index < model->unitCount; ++index)
	{
		const TimingUnitRange& unit = model->units[index];
		if (unit.engineIndex != engineIndex || unit.threadCount != threadCount)
			continue;
		if (unit.completion != PresenceStatus_Present || projection->repeatCount >= unitIndexes.size())
			return TimingError(error, ArenaStatus_InvalidResult, "timing_projection_unit");
		unitIndexes[projection->repeatCount++] = index;
	}
	if (projection->repeatCount == 0)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_projection_empty");
	projection->sampleCount = model->units[unitIndexes[0]].sampleCount;
	if (projection->sampleCount > projection->stepIndexes.size())
		return TimingError(error, ArenaStatus_InvalidResult, "timing_projection_capacity");
	for (std::uint32_t step = 0; step < projection->sampleCount; ++step)
	{
		std::array<double, kTimingRepeatCapacity> physics = {};
		for (std::uint32_t repeat = 0; repeat < projection->repeatCount; ++repeat)
		{
			const TimingUnitRange& unit = model->units[unitIndexes[repeat]];
			if (unit.sampleCount != projection->sampleCount || model->stepIndexes[unit.sampleOffset + step] != step + 1)
				return TimingError(error, ArenaStatus_InvalidResult, "timing_projection_identity");
			physics[repeat] = model->physicsStepMilliseconds[unit.sampleOffset + step];
		}
		projection->stepIndexes[step] = step + 1;
		projection->physicsStepMilliseconds[step] = Median(&physics, projection->repeatCount);
		projection->minimumPhysicsStepMilliseconds[step] =
		    *std::min_element(physics.begin(), physics.begin() + projection->repeatCount);
		projection->maximumPhysicsStepMilliseconds[step] =
		    *std::max_element(physics.begin(), physics.begin() + projection->repeatCount);
	}
	std::array<double, kTimingProjectionStepCapacity> statisticsScratch = {};
	CompleteProjectionStatistics(statisticsScratch.data(), projection);
	return ArenaStatus_Ok;
}

ArenaStatus LoadTimingArtifactIndex(const wchar_t* path, const Catalog* catalog, const ResultManifestRecord* manifest,
                                    TimingArtifactIndex* index, StatusRecord* error, TimingArtifactTotals* totals)
{
	if (totals != nullptr)
		*totals = {};
	if (index != nullptr)
		*index = {};
	if (error != nullptr)
		*error = {};
	if (path == nullptr || catalog == nullptr || manifest == nullptr || index == nullptr || error == nullptr ||
	    manifest->engineCount == 0 || manifest->threadCount == 0 || manifest->repeatCount == 0 ||
	    manifest->repeatCount > kTimingRepeatCapacity || manifest->measuredWorkUnitCount == 0 ||
	    manifest->measuredWorkUnitCount > kTimingProjectionStepCapacity)
		return error != nullptr ? TimingError(error, ArenaStatus_InvalidArgument, "timing_index_argument")
		                        : ArenaStatus_InvalidArgument;
	const std::size_t pathSize = std::wcslen(path);
	if (pathSize + 1 > index->path.size())
		return TimingError(error, ArenaStatus_InvalidResult, "timing_index_path_capacity");
	const std::uint64_t expectedSlices = static_cast<std::uint64_t>(manifest->engineCount) * manifest->threadCount;
	const std::uint64_t expectedRows = expectedSlices * manifest->repeatCount * manifest->measuredWorkUnitCount;
	if (expectedSlices > index->slices.size())
		return TimingError(error, ArenaStatus_InvalidResult, "timing_index_slice_capacity");
	TimingIndexContext context = {};
	context.catalog = catalog;
	context.manifest = manifest;
	context.index = index;
	context.totals = totals;
	CsvHeader header = {};
	CsvReadRecord record = {};
	if (ReadCsvFile(path, &header, ConsumeTimingIndexRow, &context, &record, error) != ArenaStatus_Ok ||
	    context.rowCount > expectedRows || context.index->sliceCount > expectedSlices)
	{
		*index = {};
		return error->code != ArenaStatus_Ok
		           ? error->code
				   : TimingError(error, ArenaStatus_InvalidResult, "timing_index_cardinality");
	}
	for (std::uint32_t engine = 0; engine < manifest->engineCount; ++engine)
		for (std::uint32_t thread = 0; thread < manifest->threadCount; ++thread)
			for (std::uint32_t repeat = 0; repeat < manifest->repeatCount; ++repeat)
			{
				PresenceStatus present = PresenceStatus_Absent;
				for (std::uint32_t ordinal = 0; ordinal < index->sliceCount; ++ordinal)
				{
					const TimingArtifactSlice& slice = index->slices[ordinal];
					if (slice.rowCount != slice.repeatCount * slice.measuredWorkUnitCount)
						return TimingError(error, ArenaStatus_InvalidResult, "timing_index_incomplete_repeat");
					if (slice.engineIndex == manifest->engines[engine].engineIndex && slice.threadCount == manifest->threadCounts[thread] &&
					    std::find(slice.repeatIndexes.begin(), slice.repeatIndexes.begin() + slice.repeatCount, repeat) != slice.repeatIndexes.begin() + slice.repeatCount)
						present = PresenceStatus_Present;
				}
				const ExecutionFailure* failure = FindExecutionFailure(manifest->executionFailures, manifest->engines[engine].engineIndex,
				                                                        manifest->threadCounts[thread], repeat);
				if ((present == PresenceStatus_Absent && failure == nullptr) ||
				    (present == PresenceStatus_Present && failure != nullptr && failure->outcome == ExecutionOutcome_NotRun))
					return TimingError(error, ArenaStatus_InvalidResult, "timing_index_terminal_coverage");
			}
	std::copy(path, path + pathSize + 1, index->path.begin());
	index->sourceSize = record.sourceSize;
	index->rowCount = context.rowCount;
	index->availability = PresenceStatus_Present;
	index->renderSeries = manifest->timingRenderSeries;
	return ArenaStatus_Ok;
}

ArenaStatus ProjectTimingSlice(const TimingArtifactIndex* index, const TimingProjectionSelection* selection,
                               TimingProjectionScratch* scratch, TimingProjection* projection, StatusRecord* error)
{
	if (projection != nullptr)
		*projection = {};
	if (error != nullptr)
		*error = {};
	if (index == nullptr || selection == nullptr || scratch == nullptr || projection == nullptr || error == nullptr ||
	    index->availability != PresenceStatus_Present || selection->threadCount == 0 ||
	    (selection->mode != TimingProjectionMode_MedianAcrossRepeats &&
	     selection->mode != TimingProjectionMode_ExactRepeat))
		return error != nullptr ? TimingError(error, ArenaStatus_InvalidArgument, "timing_projection_argument")
		                        : ArenaStatus_InvalidArgument;
	const TimingArtifactSlice* slice = nullptr;
	for (std::uint32_t sliceIndex = 0; sliceIndex < index->sliceCount; ++sliceIndex)
		if (index->slices[sliceIndex].engineIndex == selection->engineIndex &&
		    index->slices[sliceIndex].threadCount == selection->threadCount)
		{
			slice = &index->slices[sliceIndex];
			break;
		}
	if (slice == nullptr)
		return ArenaStatus_Ok;
	if (slice->measuredWorkUnitCount > kTimingProjectionStepCapacity ||
	    slice->repeatCount > kTimingRepeatCapacity)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_projection_selection");
	std::uint32_t selectedRepeat = 0;
	if (selection->mode == TimingProjectionMode_ExactRepeat)
	{
		while (selectedRepeat < slice->repeatCount && slice->repeatIndexes[selectedRepeat] != selection->repeatIndex)
			++selectedRepeat;
		if (selectedRepeat == slice->repeatCount)
			return ArenaStatus_Ok;
	}
	TimingSliceContext context = {};
	context.slice = slice;
	context.scratch = scratch;
	context.renderSeries = index->renderSeries;
	CsvHeader header = {};
	CsvReadRecord record = {};
	if (ReadCsvFileRange(index->path.data(), slice->firstRowByteOffset, slice->rowCount, &header, ConsumeTimingSliceRow,
	                     &context, &record, error) != ArenaStatus_Ok ||
	    context.rowCount != slice->rowCount || record.sourceSize != index->sourceSize)
		return error->code != ArenaStatus_Ok
		           ? error->code
				   : TimingError(error, ArenaStatus_InvalidResult, "timing_projection_source_changed");
	projection->sampleCount = slice->measuredWorkUnitCount;
	projection->repeatCount = slice->repeatCount;
	for (std::uint32_t step = 0; step < slice->measuredWorkUnitCount; ++step)
	{
		projection->stepIndexes[step] = step + 1;
		if (selection->mode == TimingProjectionMode_ExactRepeat)
		{
			const double value =
			    scratch->physicsStepMilliseconds[selectedRepeat * slice->measuredWorkUnitCount + step];
			projection->physicsStepMilliseconds[step] = value;
			projection->minimumPhysicsStepMilliseconds[step] = value;
			projection->maximumPhysicsStepMilliseconds[step] = value;
			continue;
		}
		std::array<double, kTimingRepeatCapacity> values = {};
		for (std::uint32_t repeat = 0; repeat < slice->repeatCount; ++repeat)
			values[repeat] = scratch->physicsStepMilliseconds[repeat * slice->measuredWorkUnitCount + step];
		projection->minimumPhysicsStepMilliseconds[step] =
		    *std::min_element(values.begin(), values.begin() + slice->repeatCount);
		projection->maximumPhysicsStepMilliseconds[step] =
		    *std::max_element(values.begin(), values.begin() + slice->repeatCount);
		projection->physicsStepMilliseconds[step] = Median(&values, slice->repeatCount);
	}
	CompleteProjectionStatistics(scratch->physicsStepMilliseconds.data(), projection);
	return ArenaStatus_Ok;
}

ArenaStatus LoadTimingCsv(const wchar_t* path, const Catalog* catalog, const ResultManifestRecord* manifest,
                          TimingSeriesModel* model, StatusRecord* error)
{
	if (model != nullptr)
		InitializeTimingSeries(model, PresenceStatus_Present);
	if (error != nullptr)
		*error = {};
	if (path == nullptr || catalog == nullptr || manifest == nullptr || model == nullptr || error == nullptr ||
	    manifest->engineCount == 0 || manifest->threadCount == 0 || manifest->repeatCount == 0 ||
	    manifest->repeatCount > kTimingRepeatCapacity || manifest->measuredWorkUnitCount == 0)
		return error != nullptr ? TimingError(error, ArenaStatus_InvalidArgument, "timing_csv_argument")
		                        : ArenaStatus_InvalidArgument;
	const std::uint64_t unitCount =
	    static_cast<std::uint64_t>(manifest->engineCount) * manifest->threadCount * manifest->repeatCount;
	const std::uint64_t sampleCount = unitCount * manifest->measuredWorkUnitCount;
	if (unitCount > kTimingUnitCapacity || sampleCount > kTimingSampleCapacity ||
	    manifest->measuredWorkUnitCount > kTimingProjectionStepCapacity)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_csv_capacity");
	TimingCsvContext context = {};
	context.catalog = catalog;
	context.manifest = manifest;
	context.model = model;
	CsvHeader header = {};
	CsvReadRecord record = {};
	if (ReadCsvFile(path, &header, ConsumeTimingRow, &context, &record, error) != ArenaStatus_Ok ||
	    context.rowCount != sampleCount)
	{
		InitializeTimingSeries(model, PresenceStatus_Present);
		return error->code != ArenaStatus_Ok ? error->code
		                                     : TimingError(error, ArenaStatus_InvalidResult, "timing_csv_cardinality");
	}
	if (model->unitCount != unitCount)
	{
		InitializeTimingSeries(model, PresenceStatus_Present);
		return TimingError(error, ArenaStatus_InvalidResult, "timing_csv_unit_count");
	}
	for (std::uint32_t unit = 0; unit < model->unitCount; ++unit)
	{
		TimingUnitRange& range = model->units[unit];
		if (range.sampleCount != range.expectedWorkUnitCount)
		{
			InitializeTimingSeries(model, PresenceStatus_Present);
			return TimingError(error, ArenaStatus_InvalidResult, "timing_csv_unit_cardinality");
		}
		range.completion = PresenceStatus_Present;
	}
	return ArenaStatus_Ok;
}

ArenaStatus WriteTimingCsv(const wchar_t* path, const Catalog* catalog, const ResultManifestRecord* manifest,
                           const TimingSeriesModel* model, StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (path == nullptr || catalog == nullptr || manifest == nullptr || model == nullptr || error == nullptr ||
	    model->availability != PresenceStatus_Present || manifest->engineCount == 0 || manifest->threadCount == 0 ||
	    manifest->repeatCount == 0 || manifest->repeatCount > kTimingRepeatCapacity ||
	    manifest->measuredWorkUnitCount == 0)
		return error != nullptr ? TimingError(error, ArenaStatus_InvalidArgument, "timing_write_argument")
		                        : ArenaStatus_InvalidArgument;
	const std::uint64_t expectedUnits =
	    static_cast<std::uint64_t>(manifest->engineCount) * manifest->threadCount * manifest->repeatCount;
	const std::uint64_t expectedSamples = expectedUnits * manifest->measuredWorkUnitCount;
	if (expectedUnits != model->unitCount || expectedSamples != model->sampleCount ||
	    expectedUnits > kTimingUnitCapacity || expectedSamples > kTimingSampleCapacity)
		return TimingError(error, ArenaStatus_InvalidResult, "timing_write_cardinality");
	CsvWriter writer = {};
	if (OpenCsvWriter(path, &writer, error) != ArenaStatus_Ok)
		return error->code;
	for (std::uint32_t field = 0; field < kTimingColumns.size(); ++field)
		if (WriteTimingText(&writer, kTimingColumns[field], field, error) != ArenaStatus_Ok)
		{
			DestroyCsvWriter(&writer);
			return error->code;
		}
	for (std::uint32_t unitIndex = 0; unitIndex < model->unitCount; ++unitIndex)
	{
		const TimingUnitRange& unit = model->units[unitIndex];
		const std::uint32_t unitsPerEngine = manifest->threadCount * manifest->repeatCount;
		const std::uint32_t engineOrdinal = unitIndex / unitsPerEngine;
		const std::uint32_t withinEngine = unitIndex % unitsPerEngine;
		const std::uint32_t threadOrdinal = withinEngine / manifest->repeatCount;
		const std::uint32_t repeatOrdinal = withinEngine % manifest->repeatCount;
		if (unit.completion != PresenceStatus_Present || unit.engineIndex >= catalog->engineCount ||
		    unit.engineIndex != manifest->engines[engineOrdinal].engineIndex ||
		    unit.threadCount != manifest->threadCounts[threadOrdinal] || unit.repeatIndex != repeatOrdinal ||
		    unit.expectedWorkUnitCount != manifest->measuredWorkUnitCount ||
		    unit.sampleCount != manifest->measuredWorkUnitCount)
		{
			DestroyCsvWriter(&writer);
			return TimingError(error, ArenaStatus_InvalidResult, "timing_write_identity_or_completion");
		}
		for (std::uint32_t sample = 0; sample < unit.sampleCount; ++sample)
		{
			const std::uint32_t index = unit.sampleOffset + sample;
			const TimingRenderSamplePresence renderPresence = model->renderSamplePresence[index];
			if ((manifest->timingRenderSeries == TimingRenderSeries_Absent &&
			     renderPresence != TimingRenderSamplePresence_Absent))
			{
				DestroyCsvWriter(&writer);
				return TimingError(error, ArenaStatus_InvalidResult, "timing_write_render_presence");
			}
			if (WriteTimingText(&writer, ResultTextView(manifest, manifest->runId), 0, error) != ArenaStatus_Ok ||
			    WriteTimingText(&writer, ResultTextView(manifest, manifest->caseId), 1, error) != ArenaStatus_Ok ||
			    WriteTimingText(&writer, CatalogTextView(catalog, catalog->engines[unit.engineIndex].id), 2, error) !=
			        ArenaStatus_Ok ||
			    WriteTimingUnsigned(&writer, unit.threadCount, 3, error) != ArenaStatus_Ok ||
			    WriteTimingUnsigned(&writer, unit.repeatIndex, 4, error) != ArenaStatus_Ok ||
			    WriteTimingUnsigned(&writer, model->stepIndexes[index], 5, error) != ArenaStatus_Ok ||
			    WriteTimingDouble(&writer, model->physicsStepMilliseconds[index], 6, error) != ArenaStatus_Ok ||
			    (model->renderSamplePresence[index] == TimingRenderSamplePresence_Present
			         ? WriteTimingDouble(&writer, model->renderFrameMilliseconds[index], 7, error)
					 : WriteTimingText(&writer, {}, 7, error)) != ArenaStatus_Ok)
			{
				DestroyCsvWriter(&writer);
				return error->code;
			}
		}
	}
	return FinishCsvWriter(&writer, error);
}
} // namespace physics_arena
