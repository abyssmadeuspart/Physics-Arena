#include "result_pipeline_internal.h"
#include "physics_arena/observation_results.h"
#include "physics_arena/replay.h"
#include "physics_arena/ray_tracing_images.h"
#include "physics_arena/stack_stability.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <memory>
#include <new>
#include <string_view>

namespace physics_arena
{

ArenaStatus PipelineError(StatusRecord* error, ArenaStatus status, std::string_view detail)
{
	*error = {};
	const std::string_view component = "result_pipeline";
	const std::string_view statusText = ArenaStatusText(status);
	if (component.size() <= error->component.size())
	{
		std::copy(component.begin(), component.end(), error->component.begin());
		error->componentSize = static_cast<std::uint32_t>(component.size());
	}
	if (statusText.size() <= error->status.size())
	{
		std::copy(statusText.begin(), statusText.end(), error->status.begin());
		error->statusSize = static_cast<std::uint32_t>(statusText.size());
	}
	if (detail.size() > error->detail.size())
		detail = "result_pipeline_detail_capacity";
	std::copy(detail.begin(), detail.end(), error->detail.begin());
	error->detailSize = static_cast<std::uint32_t>(detail.size());
	error->code = status;
	return status;
}

ArenaStatus AppendWide(std::array<wchar_t, kRunPathCapacity>* output, std::uint32_t* size, std::wstring_view value)
{
	if (value.size() >= output->size() - *size)
		return ArenaStatus_InvalidResult;
	std::copy(value.begin(), value.end(), output->begin() + *size);
	*size += static_cast<std::uint32_t>(value.size());
	(*output)[*size] = L'\0';
	return ArenaStatus_Ok;
}

ArenaStatus AppendPath(std::array<wchar_t, kRunPathCapacity>* output, std::uint32_t* size, std::wstring_view value)
{
	if (*size != 0 && (*output)[*size - 1] != L'\\' && (*output)[*size - 1] != L'/')
		if (AppendWide(output, size, L"\\") != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
	return AppendWide(output, size, value);
}

ArenaStatus BuildChildPath(const wchar_t* parent, std::wstring_view child,
                           std::array<wchar_t, kRunPathCapacity>* output)
{
	*output = {};
	std::uint32_t size = 0;
	return AppendWide(output, &size, parent) == ArenaStatus_Ok && AppendPath(output, &size, child) == ArenaStatus_Ok
	           ? ArenaStatus_Ok
			   : ArenaStatus_InvalidResult;
}

ArenaStatus BuildUnitPath(const RunPathRecord* paths, const Catalog* catalog, const PreparedRunRequest* request,
                          const UnitOrderRecord& unit, const wchar_t* suffix,
                          std::array<wchar_t, kRunPathCapacity>* output)
{
	const std::uint32_t engineIndex = request->engineIndexes[unit.selectedEngineIndex];
	const std::string_view engineId = CatalogTextView(catalog, catalog->engines[engineIndex].id);
	std::array<wchar_t, kRunPathCapacity> engineDirectory = {};
	std::array<wchar_t, kRunPathCapacity> threadDirectory = {};
	std::uint32_t engineSize = 0;
	std::uint32_t threadSize = 0;
	std::array<wchar_t, 32> threadName = {};
	std::array<wchar_t, 512> fileName = {};
	std::swprintf(threadName.data(), threadName.size(), L"t%u", request->threadCounts[unit.selectedThreadIndex]);
	std::array<wchar_t, 256> engineWide = {};
	const int engineWideSize =
	    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, engineId.data(), static_cast<int>(engineId.size()),
		                    engineWide.data(), static_cast<int>(engineWide.size()));
	if (engineWideSize <= 0 ||
	    AppendWide(&engineDirectory, &engineSize, paths->rawDirectory.data()) != ArenaStatus_Ok ||
	    AppendPath(&engineDirectory, &engineSize, std::wstring_view(engineWide.data(), engineWideSize)) !=
	        ArenaStatus_Ok ||
	    AppendWide(&threadDirectory, &threadSize, engineDirectory.data()) != ArenaStatus_Ok ||
	    AppendPath(&threadDirectory, &threadSize, threadName.data()) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	const int fileSize = std::swprintf(fileName.data(), fileName.size(), L"%ls_t%u_%ls", engineWide.data(),
	                                   request->threadCounts[unit.selectedThreadIndex], suffix);
	return fileSize > 0 ? BuildChildPath(threadDirectory.data(), fileName.data(), output) : ArenaStatus_InvalidResult;
}

ArenaStatus BuildRootOutputPath(const RunPathRecord* paths, const wchar_t* name,
                                std::array<wchar_t, kRunPathCapacity>* output)
{
	return BuildChildPath(paths->resultDirectory.data(), name, output);
}

ArenaStatus MakeRepositoryRelative(const wchar_t* repositoryRoot, const wchar_t* absolutePath,
                                   std::array<char, kRunPathCapacity>* output, std::uint32_t* outputSize)
{
	const std::size_t rootSize = std::wcslen(repositoryRoot);
	if (_wcsnicmp(repositoryRoot, absolutePath, rootSize) != 0)
		return ArenaStatus_InvalidResult;
	const wchar_t* relative = absolutePath + rootSize;
	while (*relative == L'\\' || *relative == L'/')
		++relative;
	const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, relative, -1, output->data(),
	                                      static_cast<int>(output->size()), nullptr, nullptr);
	if (count <= 1)
		return ArenaStatus_InvalidResult;
	*outputSize = static_cast<std::uint32_t>(count - 1);
	for (std::uint32_t index = 0; index < *outputSize; ++index)
		if ((*output)[index] == '\\')
			(*output)[index] = '/';
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

ArenaStatus ParseMeasurement(std::string_view text, ResultMeasurementMode mode, double* value)
{
	if (mode == ResultMeasurementMode_PhysicalQuality)
	{
		*value = 0.0;
		return text.empty() ? ArenaStatus_Ok : ArenaStatus_InvalidResult;
	}
	return ParsePositiveDouble(text, value);
}

std::string_view FormatDoubleNine(double value, std::array<char, 64>* output)
{
	const std::to_chars_result result =
	    std::to_chars(output->data(), output->data() + output->size(), value, std::chars_format::fixed, 9);
	return result.ec == std::errc() ? std::string_view(output->data(), result.ptr) : std::string_view();
}

double RoundDoubleNine(double value)
{
	std::array<char, 64> text = {};
	const std::string_view formatted = FormatDoubleNine(value, &text);
	double rounded = 0.0;
	const std::from_chars_result result =
	    std::from_chars(formatted.data(), formatted.data() + formatted.size(), rounded, std::chars_format::fixed);
	return result.ec == std::errc() ? rounded : value;
}

ArenaStatus CompareSerializedMetric(double actual, double expected)
{
	// nine decimal output can lose one decimal unit across reduction, and large
	// rates also need the two adjacent binary rounding steps preserved
	const double ulp = std::nextafter(expected, INFINITY) - expected;
	return std::isfinite(expected) && std::abs(actual - expected) <= std::max(1e-9, 2.0 * ulp)
	           ? ArenaStatus_Ok
			   : ArenaStatus_InvalidResult;
}

std::string_view PrimaryDirectionText(PrimaryMetricDirection direction)
{
	return direction == PrimaryMetricDirection_LowerIsBetter
	           ? "lower_is_better"
			   : (direction == PrimaryMetricDirection_HigherIsBetter
	                  ? "higher_is_better"
					  : (direction == PrimaryMetricDirection_NotRanked ? "not_ranked" : std::string_view()));
}

ArenaStatus WriteRowText(RowWriter* row, std::string_view value)
{
	if (row->fieldIndex >= row->fieldCount)
		return PipelineError(row->error, ArenaStatus_InvalidResult, "csv_row_field_overflow");
	const CsvFieldTerminator terminator =
	    row->fieldIndex + 1 == row->fieldCount ? CsvFieldTerminator_EndRow : CsvFieldTerminator_MoreFields;
	row->fieldIndex += 1;
	return WriteCsvField(row->writer, value, terminator, row->error);
}

ArenaStatus WriteRowUnsigned(RowWriter* row, std::uint32_t value)
{
	std::array<char, 32> text = {};
	const std::to_chars_result result = std::to_chars(text.data(), text.data() + text.size(), value);
	return result.ec == std::errc() ? WriteRowText(row, std::string_view(text.data(), result.ptr))
	                                : PipelineError(row->error, ArenaStatus_InvalidResult, "unsigned_format");
}

ArenaStatus WriteRowUnsigned64(RowWriter* row, std::uint64_t value)
{
	std::array<char, 32> text = {};
	const std::to_chars_result result = std::to_chars(text.data(), text.data() + text.size(), value);
	return result.ec == std::errc() ? WriteRowText(row, std::string_view(text.data(), result.ptr))
	                                : PipelineError(row->error, ArenaStatus_InvalidResult, "unsigned64_format");
}

ArenaStatus WriteRowDouble(RowWriter* row, double value)
{
	std::array<char, 64> text = {};
	const std::string_view formatted = FormatDoubleNine(value, &text);
	return !formatted.empty() ? WriteRowText(row, formatted)
	                          : PipelineError(row->error, ArenaStatus_InvalidResult, "double_format");
}

ArenaStatus WriteMeasurement(RowWriter* row, double value, CaseFixtureKind fixture)
{
	return fixture == CaseFixtureKind_RagdollStairTumble ? WriteRowText(row, {}) : WriteRowDouble(row, value);
}

template <std::size_t Count>
ArenaStatus ValidateExactHeader(const CsvHeader* header, const std::array<std::string_view, Count>& columns,
                                std::string_view detail, StatusRecord* error)
{
	if (header->fieldCount != columns.size())
		return PipelineError(error, ArenaStatus_InvalidResult, detail);
	for (std::uint32_t column = 0; column < header->fieldCount; ++column)
	{
		if (CsvHeaderTextView(header, header->fields[column]) != columns[column])
			return PipelineError(error, ArenaStatus_InvalidResult, detail);
	}
	return ArenaStatus_Ok;
}

std::string_view RowValue(const CsvRow* row, std::uint32_t index)
{
	return index < row->fieldCount ? CsvRowTextView(row, row->fields[index]) : std::string_view();
}

std::uint32_t WorkerCount(WorkerCountPolicy policy, std::uint32_t threadCount)
{
	return policy == WorkerCountPolicy_ThreadCount
	           ? threadCount
			   : (policy == WorkerCountPolicy_ThreadCountMinusOne && threadCount > 1 ? threadCount - 1 : 0);
}

ArenaStatus WriteNormalizedRow(RawContext* context, const CsvRow* rawRow, std::uint32_t repeatIndex,
                               double primaryValue, double meanMilliseconds, double workUnitsPerSecond,
                               StatusRecord* error)
{
	RowWriter row = {context->normalizedWriter, error, 0, static_cast<std::uint32_t>(kNormalizedColumns.size())};
	const std::string_view engineId = CatalogTextView(context->catalog, context->engine->id);
	const std::string_view benchmarkMode =
	    RecordingForThread(context->request->recordingThreads, context->threadCount) == RecordingMode_On ? "recorded_api" : "headless_api";
	const std::string_view runtimeRoute = std::string_view("engine_runner_cli");
	const std::string_view timingScope = context->benchmarkCase->fixtureKind == CaseFixtureKind_RayTracing
	                                         ? std::string_view("ordinary_coherent_primary_wall_sum_inline")
	                                     : context->benchmarkCase->fixtureKind == CaseFixtureKind_RagdollStairTumble
	                                         ? std::string_view("absent")
	                                     : CaseConfigurationTextView(context->catalog, context->configuration,
	                                                                 context->benchmarkCase->workUnitId) == "cycle"
	                                         ? std::string_view("timed_phase_wall_sum_inline")
	                                         : std::string_view("work_unit_wall_sum_inline");
	const std::uint32_t requestedWorkers = WorkerCount(context->engine->requestedWorkerPolicy, context->threadCount);
	if (WriteRowText(&row, std::string_view(context->paths->runId.data(), context->paths->runIdSize)) !=
	        ArenaStatus_Ok ||
	    WriteRowText(&row, engineId) != ArenaStatus_Ok ||
	    WriteRowText(&row, ReleaseTextView(context->releaseCatalog, context->artifact->sourceVersion)) !=
	        ArenaStatus_Ok ||
	    WriteRowText(&row, ReleaseTextView(context->releaseCatalog, context->releaseCatalog->hostRoute)) !=
	        ArenaStatus_Ok ||
	    WriteRowText(&row, ReleaseTextView(context->releaseCatalog, context->artifact->toolchainId)) !=
	        ArenaStatus_Ok ||
	    WriteRowText(&row, CaseConfigurationTextView(context->catalog, context->configuration,
	                                                 context->benchmarkCase->id)) != ArenaStatus_Ok ||
	    WriteRowText(&row, benchmarkMode) != ArenaStatus_Ok ||
	    WriteRowUnsigned(&row, context->threadCount) != ArenaStatus_Ok ||
	    WriteRowText(&row, CaseConfigurationTextView(context->catalog, context->configuration,
	                                                 context->benchmarkCase->workUnitId)) != ArenaStatus_Ok ||
	    WriteRowUnsigned(&row, context->benchmarkCase->measuredWorkUnitCount) != ArenaStatus_Ok ||
	    WriteRowUnsigned(&row, context->benchmarkCase->warmupWorkUnitCount) != ArenaStatus_Ok ||
	    WriteRowUnsigned(&row, repeatIndex) != ArenaStatus_Ok ||
	    WriteRowText(&row, RowValue(rawRow, 5)) != ArenaStatus_Ok ||
	    WriteRowText(&row, RowValue(rawRow, 6)) != ArenaStatus_Ok ||
	    WriteRowText(&row, RowValue(rawRow, 7)) != ArenaStatus_Ok ||
	    WriteRowText(&row, RowValue(rawRow, 8)) != ArenaStatus_Ok ||
	    WriteRowText(&row, CaseConfigurationTextView(context->catalog, context->configuration,
	                                                 context->benchmarkCase->primaryMetricId)) != ArenaStatus_Ok ||
	    WriteMeasurement(&row, primaryValue, context->benchmarkCase->fixtureKind) != ArenaStatus_Ok ||
	    WriteRowText(&row, CaseConfigurationTextView(context->catalog, context->configuration,
	                                                 context->benchmarkCase->primaryMetricUnit)) != ArenaStatus_Ok ||
	    WriteRowText(&row, PrimaryDirectionText(context->benchmarkCase->primaryMetricDirection)) != ArenaStatus_Ok ||
	    WriteMeasurement(&row, meanMilliseconds, context->benchmarkCase->fixtureKind) != ArenaStatus_Ok ||
	    WriteMeasurement(&row, workUnitsPerSecond, context->benchmarkCase->fixtureKind) != ArenaStatus_Ok ||
	    WriteRowText(&row, RowValue(rawRow, 9)) != ArenaStatus_Ok ||
	    WriteRowText(&row, RowValue(rawRow, 10)) != ArenaStatus_Ok ||
	    WriteRowText(&row, RowValue(rawRow, 11)) != ArenaStatus_Ok ||
	    WriteRowText(&row, runtimeRoute) != ArenaStatus_Ok || WriteRowText(&row, timingScope) != ArenaStatus_Ok ||
	    WriteRowText(&row, RowValue(rawRow, 12)) != ArenaStatus_Ok ||
	    WriteRowText(&row, RowValue(rawRow, 2)) != ArenaStatus_Ok ||
	    WriteRowText(&row, RowValue(rawRow, 3)) != ArenaStatus_Ok ||
	    WriteRowText(&row, RowValue(rawRow, 4)) != ArenaStatus_Ok ||
	    WriteRowText(&row, ReleaseTextView(context->releaseCatalog, context->artifact->toolchainId)) !=
	        ArenaStatus_Ok ||
	    WriteRowUnsigned(&row, context->threadCount) != ArenaStatus_Ok ||
	    WriteRowUnsigned(&row, requestedWorkers) != ArenaStatus_Ok)
		return error->code;
	if (context->engine->threadSupportMode == ThreadSupportMode_Explicit)
	{
		if (WriteRowUnsigned(&row, context->threadCount > 1 ? context->threadCount - 1 : 1) != ArenaStatus_Ok)
			return error->code;
	}
	else if (WriteRowText(&row, {}) != ArenaStatus_Ok)
		return error->code;
	if (WriteRowText(&row, RowValue(rawRow, 14)) != ArenaStatus_Ok ||
	    WriteRowText(&row, RowValue(rawRow, 13)) != ArenaStatus_Ok ||
	    WriteRowText(&row, context->engine->mainThreadParticipation == MainThreadParticipation_Yes ? "yes" : "no") !=
	        ArenaStatus_Ok ||
	    WriteRowText(&row, {}) != ArenaStatus_Ok || WriteRowText(&row, {}) != ArenaStatus_Ok ||
	    WriteRowText(&row, {}) != ArenaStatus_Ok || WriteRowText(&row, {}) != ArenaStatus_Ok ||
	    WriteRowText(&row, {}) != ArenaStatus_Ok || WriteRowText(&row, {}) != ArenaStatus_Ok ||
	    WriteRowText(&row, {}) != ArenaStatus_Ok || WriteRowText(&row, {}) != ArenaStatus_Ok ||
	    WriteRowText(&row, {}) != ArenaStatus_Ok || WriteRowText(&row, {}) != ArenaStatus_Ok ||
	    WriteRowText(&row, RowValue(rawRow, 15)) != ArenaStatus_Ok ||
	    WriteRowText(&row, RowValue(rawRow, 16)) != ArenaStatus_Ok ||
	    WriteRowText(&row, RowValue(rawRow, 17)) != ArenaStatus_Ok ||
	    WriteRowText(&row, RowValue(rawRow, 18)) != ArenaStatus_Ok ||
	    WriteRowText(&row, RowValue(rawRow, 19)) != ArenaStatus_Ok ||
	    WriteRowText(&row, RowValue(rawRow, 20)) != ArenaStatus_Ok ||
	    WriteRowText(&row, std::string_view(context->rawRelativePath.data(), context->rawRelativePathSize)) !=
	        ArenaStatus_Ok)
		return error->code;
	return row.fieldIndex == row.fieldCount
	           ? ArenaStatus_Ok
			   : PipelineError(error, ArenaStatus_InvalidResult, "result_normalized_row_field_count");
}

ArenaStatus ConsumeCompleteRawRow(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error)
{
	RawContext* context = static_cast<RawContext*>(opaque);
	const ResultMeasurementMode mode = context->benchmarkCase->fixtureKind == CaseFixtureKind_RagdollStairTumble
	                                       ? ResultMeasurementMode_PhysicalQuality
	                                       : ResultMeasurementMode_Timed;
	if (context->headerValidated != PresenceStatus_Present)
	{
		if (ValidateExactHeader(header, kRawColumns, "raw_header", error) != ArenaStatus_Ok)
			return error->code;
		context->headerValidated = PresenceStatus_Present;
	}
	if (context->accumulator.rowCount >= context->request->repeatCount)
		return PipelineError(error, ArenaStatus_InvalidResult, "raw_repeat_overflow");
	std::uint32_t rawSchema = 0;
	std::uint32_t repeatIndex = 0;
	std::uint32_t fixtureRevision = 0;
	std::uint32_t bodyCount = 0;
	std::uint32_t shapeCount = 0;
	std::uint32_t queryCount = 0;
	std::uint32_t constraintCount = 0;
	std::uint32_t effectiveThreadCount = 0;
	std::uint32_t effectiveWorkerCount = 0;
	std::uint32_t completedWorkUnitCount = 0;
	std::uint64_t invalidTransformCount = 0;
	double workloadElapsedMs = 0.0;
	if (ParseUnsigned(RowValue(row, 0), &rawSchema) != ArenaStatus_Ok || rawSchema != 3 ||
	    ParseUnsigned(RowValue(row, 1), &repeatIndex) != ArenaStatus_Ok ||
	    repeatIndex >= context->request->repeatCount || context->accumulator.seen[repeatIndex] == PresenceStatus_Present ||
	    RowValue(row, 2) != CaseConfigurationTextView(context->catalog, context->configuration,
	                                                  context->benchmarkCase->fixtureSemantic) ||
	    ParseUnsigned(RowValue(row, 3), &fixtureRevision) != ArenaStatus_Ok ||
	    fixtureRevision != context->benchmarkCase->fixtureRevision || RowValue(row, 4).empty() ||
	    ParseUnsigned(RowValue(row, 5), &bodyCount) != ArenaStatus_Ok ||
	    bodyCount != context->benchmarkCase->bodyCount ||
	    ParseUnsigned(RowValue(row, 6), &shapeCount) != ArenaStatus_Ok ||
	    shapeCount != context->benchmarkCase->shapeCount ||
	    ParseUnsigned(RowValue(row, 7), &queryCount) != ArenaStatus_Ok ||
	    queryCount != context->benchmarkCase->queryCount ||
	    ParseUnsigned(RowValue(row, 8), &constraintCount) != ArenaStatus_Ok ||
	    constraintCount != context->benchmarkCase->constraintCount ||
	    ParseUnsigned64(RowValue(row, 9), &invalidTransformCount) != ArenaStatus_Ok ||
	    (RowValue(row, 10) != "ok" && RowValue(row, 10) != "failed") ||
	    RowValue(row, 11) != "ok" || ParseUnsigned(RowValue(row, 12), &effectiveThreadCount) != ArenaStatus_Ok ||
	    effectiveThreadCount != context->threadCount ||
	    ParseUnsigned(RowValue(row, 13), &effectiveWorkerCount) != ArenaStatus_Ok ||
	    effectiveWorkerCount != WorkerCount(context->engine->effectiveWorkerPolicy, context->threadCount) ||
	    ParseUnsigned(RowValue(row, 15), &completedWorkUnitCount) != ArenaStatus_Ok ||
	    completedWorkUnitCount != context->benchmarkCase->measuredWorkUnitCount ||
	    ParseMeasurement(RowValue(row, 16), mode, &workloadElapsedMs) != ArenaStatus_Ok)
		return PipelineError(error, ArenaStatus_InvalidResult, "raw_contract");
	std::uint32_t actualTaskgraph = 0;
	if ((context->engine->threadSupportMode == ThreadSupportMode_Explicit &&
	     (ParseUnsigned(RowValue(row, 14), &actualTaskgraph) != ArenaStatus_Ok || actualTaskgraph == 0)) ||
	    (context->engine->threadSupportMode != ThreadSupportMode_Explicit && !RowValue(row, 14).empty()))
		return PipelineError(error, ArenaStatus_InvalidResult, "raw_taskgraph");
	if (!RowValue(row, 17).empty() || !RowValue(row, 18).empty() || !RowValue(row, 19).empty() ||
	    !RowValue(row, 20).empty())
		return PipelineError(error, ArenaStatus_InvalidResult, "raw_recorded_visual_fields");
	if (context->accumulator.rowCount == 0)
	{
		if (RowValue(row, 4).size() >= context->accumulator.physicsSettings.size())
			return PipelineError(error, ArenaStatus_InvalidResult, "raw_physics_settings_capacity");
		std::copy(RowValue(row, 4).begin(), RowValue(row, 4).end(), context->accumulator.physicsSettings.begin());
		context->accumulator.physicsSettingsSize = static_cast<std::uint32_t>(RowValue(row, 4).size());
		const std::string_view buildSettings = ReleaseTextView(context->releaseCatalog, context->artifact->toolchainId);
		if (buildSettings.size() >= context->accumulator.buildSettings.size())
			return PipelineError(error, ArenaStatus_InvalidResult, "build_settings_capacity");
		std::copy(buildSettings.begin(), buildSettings.end(), context->accumulator.buildSettings.begin());
		context->accumulator.buildSettingsSize = static_cast<std::uint32_t>(buildSettings.size());
		context->accumulator.bodyCount = bodyCount;
		context->accumulator.shapeCount = shapeCount;
		context->accumulator.queryCount = queryCount;
		context->accumulator.constraintCount = constraintCount;
		context->accumulator.invalidTransformCount = 0;
	}
	else if (RowValue(row, 4) !=
	         std::string_view(context->accumulator.physicsSettings.data(), context->accumulator.physicsSettingsSize))
	{
		return PipelineError(error, ArenaStatus_InvalidResult, "raw_physics_settings_drift");
	}
	context->accumulator.workloadElapsedMilliseconds[repeatIndex] = workloadElapsedMs;
	context->accumulator.invalidTransformCount += invalidTransformCount;
	context->accumulator.repeatFailures[repeatIndex] = RowValue(row, 10) == "failed" ? RepeatFailureCause_CaseStatus :
	    invalidTransformCount != 0 ? RepeatFailureCause_InvalidTransforms : RepeatFailureCause_None;
	const double meanMilliseconds =
	    mode == ResultMeasurementMode_PhysicalQuality
	        ? 0.0
	        : RoundDoubleNine(workloadElapsedMs / static_cast<double>(context->benchmarkCase->measuredWorkUnitCount));
	const double workUnitsPerSecond =
	    mode == ResultMeasurementMode_PhysicalQuality
	        ? 0.0
	        : RoundDoubleNine(static_cast<double>(context->benchmarkCase->measuredWorkUnitCount) * 1000.0 /
	                          workloadElapsedMs);
	const double primaryValue =
	    (context->benchmarkCase->fixtureKind == CaseFixtureKind_RayTracing || CaseConfigurationTextView(context->catalog, context->configuration, context->benchmarkCase->workUnitId) ==
	            "query_batch")
	        ? RoundDoubleNine(static_cast<double>(context->benchmarkCase->queryCount) *
	                          context->benchmarkCase->measuredWorkUnitCount * 1000.0 / workloadElapsedMs)
	        : meanMilliseconds;
	context->accumulator.seen[repeatIndex] = PresenceStatus_Present;
	context->accumulator.primaryValues[repeatIndex] = primaryValue;
	context->accumulator.meanMilliseconds[repeatIndex] = meanMilliseconds;
	context->accumulator.workUnitsPerSecond[repeatIndex] = workUnitsPerSecond;
	context->accumulator.rowCount += 1;
	return WriteNormalizedRow(context, row, repeatIndex, primaryValue, meanMilliseconds, workUnitsPerSecond, error);
}

ArenaStatus ConsumeRawRow(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error)
{
	RawContext* context = static_cast<RawContext*>(opaque);
	context->rejectedRepeat.reset();
	if (context->headerValidated != PresenceStatus_Present)
	{
		if (ValidateExactHeader(header, kRawColumns, "raw_header", error) != ArenaStatus_Ok)
			return error->code;
		context->headerValidated = PresenceStatus_Present;
	}
	std::uint32_t repeat = 0;
	if (row->fieldCount != kRawColumns.size() || ParseUnsigned(RowValue(row, 1), &repeat) != ArenaStatus_Ok ||
	    repeat >= context->request->repeatCount)
		return PipelineError(error, ArenaStatus_InvalidResult, "raw_repeat_identity");
	const ExecutionFailure* failure = FindExecutionFailure(context->failures, context->engineIndex, context->threadCount, repeat);
	if (failure != nullptr && failure->outcome == ExecutionOutcome_NotRun)
		return PipelineError(error, ArenaStatus_InvalidResult, "not_run_has_raw_work");
	if (context->inputSeen[repeat] == PresenceStatus_Present)
		return PipelineError(error, ArenaStatus_InvalidResult, "raw_repeat_duplicate");
	context->inputSeen[repeat] = PresenceStatus_Present;
	const ArenaStatus status = ConsumeCompleteRawRow(header, row, opaque, error);
	if (status == ArenaStatus_InvalidResult && failure != nullptr)
	{
		*error = {};
		return ArenaStatus_Ok;
	}
	if (status == ArenaStatus_InvalidResult)
		context->rejectedRepeat = repeat;
	return status;
}

ArenaStatus ConsumeObservationRow(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error)
{
	ObservationUnitContext* context = static_cast<ObservationUnitContext*>(opaque);
	context->rejectedRepeat.reset();
	if (context->headerValidated != PresenceStatus_Present)
	{
		if (ValidateExactHeader(header, kObservationSidecarColumns, "observation_sidecar_header", error) !=
		    ArenaStatus_Ok)
			return error->code;
		context->headerValidated = PresenceStatus_Present;
	}
	std::uint32_t repeatIndex = 0;
	if (row->fieldCount != kObservationSidecarColumns.size() ||
	    ParseUnsigned(RowValue(row, 0), &repeatIndex) != ArenaStatus_Ok || repeatIndex >= context->repeatCount)
		return PipelineError(error, ArenaStatus_InvalidResult, "observation_repeat_identity");
	if (context->rowCount >= context->expectedRowCount || context->benchmarkCase->observationCount == 0)
	{
		context->rejectedRepeat = repeatIndex;
		return PipelineError(error, ArenaStatus_InvalidResult, "observation_row_overflow");
	}
	std::uint32_t rowsPerRepeat = 0;
	for (std::uint32_t index = 0; index < context->benchmarkCase->observationCount; ++index)
		rowsPerRepeat +=
		    CaseConfigurationObservation(context->catalog, *context->benchmarkCase, context->configuration, index)
		        .sampleIndexCount;
	if (rowsPerRepeat == 0)
		return PipelineError(error, ArenaStatus_InvalidResult, "observation_row_contract");
	const ExecutionFailure* failure = FindExecutionFailure(context->failures, context->engineIndex, context->threadCount, repeatIndex);
	if (failure != nullptr && failure->outcome == ExecutionOutcome_NotRun)
		return PipelineError(error, ArenaStatus_InvalidResult, "not_run_has_observations");
	std::uint32_t sampleIndex = 0;
	if (ParseUnsigned(RowValue(row, 3), &sampleIndex) != ArenaStatus_Ok)
		return PipelineError(error, ArenaStatus_InvalidResult, "observation_identity");
	std::uint32_t declarationOrdinal = 0;
	std::uint32_t rowOrdinal = 0;
	while (declarationOrdinal < context->benchmarkCase->observationCount)
	{
		const ObservationDeclaration& candidate = CaseConfigurationObservation(
		    context->catalog, *context->benchmarkCase, context->configuration, declarationOrdinal);
		if (RowValue(row, 1) == CaseConfigurationTextView(context->catalog, context->configuration, candidate.id) &&
		    RowValue(row, 2) == CaseConfigurationTextView(context->catalog, context->configuration, candidate.phaseId))
			break;
		rowOrdinal += candidate.sampleIndexCount;
		declarationOrdinal += 1;
	}
	if (declarationOrdinal >= context->benchmarkCase->observationCount)
		return PipelineError(error, ArenaStatus_InvalidResult, "observation_identity");
	const ObservationDeclaration& declaration = CaseConfigurationObservation(
	    context->catalog, *context->benchmarkCase, context->configuration, declarationOrdinal);
	std::uint32_t sampleOrdinal = 0;
	while (sampleOrdinal < declaration.sampleIndexCount &&
	       CaseConfigurationSampleIndex(context->catalog, context->configuration, declaration, sampleOrdinal) != sampleIndex)
		++sampleOrdinal;
	if (sampleOrdinal == declaration.sampleIndexCount)
		return PipelineError(error, ArenaStatus_InvalidResult, "observation_identity");
	rowOrdinal += sampleOrdinal;
	if (context->inputSeen[repeatIndex].test(rowOrdinal))
		return PipelineError(error, ArenaStatus_InvalidResult, "observation_duplicate");
	context->inputSeen[repeatIndex].set(rowOrdinal);
	std::uint64_t unsignedValue = 0;
	double floatValue = 0.0;
	if ((declaration.valueType == ObservationValueType_Uint64 &&
	     ParseUnsigned64(RowValue(row, 4), &unsignedValue) != ArenaStatus_Ok) ||
	    (declaration.valueType == ObservationValueType_Float64 &&
	     ParseFiniteDouble(RowValue(row, 4), &floatValue) != ArenaStatus_Ok))
	{
		if (failure != nullptr)
			return ArenaStatus_Ok;
		context->rejectedRepeat = repeatIndex;
		return PipelineError(error, ArenaStatus_InvalidResult, "observation_value");
	}
	ObservationOutcome outcome = ObservationOutcome_Ok;
	if (declaration.expectedValuePresence == PresenceStatus_Present &&
	    ((declaration.valueType == ObservationValueType_Uint64 && unsignedValue != declaration.expectedUnsigned) ||
	     (declaration.valueType == ObservationValueType_Float64 && floatValue != declaration.expectedFloat64)))
		outcome = ObservationOutcome_Failed;
	if (outcome == ObservationOutcome_Failed)
		context->repeatFailures[repeatIndex] = RepeatFailureCause_ExpectedObservation;
	RowWriter output = {context->rootWriter, error, 0, static_cast<std::uint32_t>(kObservationRootColumns.size())};
	if (WriteRowText(&output, std::string_view(context->paths->runId.data(), context->paths->runIdSize)) !=
	        ArenaStatus_Ok ||
	    WriteRowText(&output, context->engineId) != ArenaStatus_Ok ||
	    WriteRowText(&output, CaseConfigurationTextView(context->catalog, context->configuration,
	                                                    context->benchmarkCase->id)) != ArenaStatus_Ok ||
	    WriteRowText(&output, context->benchmarkMode) != ArenaStatus_Ok ||
	    WriteRowUnsigned(&output, context->threadCount) != ArenaStatus_Ok ||
	    WriteRowUnsigned(&output, repeatIndex) != ArenaStatus_Ok ||
	    WriteRowText(&output, CaseConfigurationTextView(context->catalog, context->configuration, declaration.id)) !=
	        ArenaStatus_Ok ||
	    WriteRowText(&output, CaseConfigurationTextView(context->catalog, context->configuration,
	                                                    declaration.phaseId)) != ArenaStatus_Ok ||
	    WriteRowUnsigned(&output, sampleIndex) != ArenaStatus_Ok ||
	    WriteRowText(&output, RowValue(row, 4)) != ArenaStatus_Ok ||
	    WriteRowText(&output, CaseConfigurationTextView(context->catalog, context->configuration, declaration.unit)) !=
	        ArenaStatus_Ok ||
	    WriteRowText(&output, ObservationRoleWireText(declaration.role)) != ArenaStatus_Ok)
		return error->code;
	if (declaration.expectedValuePresence == PresenceStatus_Present)
	{
		if ((declaration.valueType == ObservationValueType_Uint64
		         ? WriteRowUnsigned64(&output, declaration.expectedUnsigned)
				 : WriteRowDouble(&output, declaration.expectedFloat64)) != ArenaStatus_Ok)
			return error->code;
	}
	else if (WriteRowText(&output, {}) != ArenaStatus_Ok)
		return error->code;
	if (WriteRowText(&output, ObservationOutcomeText(outcome)) != ArenaStatus_Ok)
		return error->code;
	context->repeatRows[repeatIndex] += 1;
	context->rowCount += 1;
	return ArenaStatus_Ok;
}

ResultSummary SummarizeUnit(const ResultAccumulator& accumulator, std::uint32_t engineIndex, std::uint32_t threadCount)
{
	ResultSummary summary = {};
	summary.engineIndex = engineIndex;
	summary.threadCount = threadCount;
	summary.repeatCount = accumulator.rowCount;
	summary.bodyCount = accumulator.bodyCount;
	summary.shapeCount = accumulator.shapeCount;
	summary.queryCount = accumulator.queryCount;
	summary.constraintCount = accumulator.constraintCount;
	summary.invalidTransformCount = accumulator.invalidTransformCount;
	summary.physicsSettings = accumulator.physicsSettings;
	summary.physicsSettingsSize = accumulator.physicsSettingsSize;
	summary.buildSettings = accumulator.buildSettings;
	summary.buildSettingsSize = accumulator.buildSettingsSize;
	if (summary.repeatCount == 0)
		return summary;
	std::array<double, kRunRepeatCapacity> ordered = {};
	std::uint32_t count = 0;
	for (std::uint32_t repeat = 0; repeat < kRunRepeatCapacity; ++repeat)
		if (accumulator.seen[repeat] == PresenceStatus_Present)
			ordered[count++] = accumulator.primaryValues[repeat];
	std::sort(ordered.begin(), ordered.begin() + summary.repeatCount);
	summary.minimumPrimaryValue = ordered[0];
	summary.medianPrimaryValue = summary.repeatCount % 2 != 0
	                                 ? ordered[summary.repeatCount / 2]
	                                 : (ordered[summary.repeatCount / 2 - 1] + ordered[summary.repeatCount / 2]) / 2.0;
	summary.maximumPrimaryValue = ordered[summary.repeatCount - 1];
	return summary;
}

ArenaStatus WriteResultSummaryRow(CsvWriter* writer, const Catalog* catalog, const CaseRecord* benchmarkCase,
                                  std::string_view runId, std::string_view benchmarkMode, const ResultSummary& summary,
                                  StatusRecord* error, ResultMeasurementMode mode,
                                  const EffectiveRunConfiguration* configuration)
{
	const ResultCaseMetadata metadata = ProjectResultCaseMetadata(catalog, *benchmarkCase, mode, configuration);
	const CaseFixtureKind timingFixture =
	    mode == ResultMeasurementMode_Timed ? CaseFixtureKind_Unknown : benchmarkCase->fixtureKind;
	RowWriter row = {writer, error, 0, static_cast<std::uint32_t>(kSummaryColumns.size())};
	const EngineRecord& engine = catalog->engines[summary.engineIndex];
	if (WriteRowText(&row, runId) != ArenaStatus_Ok ||
	    WriteRowText(&row, CatalogTextView(catalog, engine.id)) != ArenaStatus_Ok ||
	    WriteRowText(&row, CaseConfigurationTextView(catalog, configuration, benchmarkCase->id)) != ArenaStatus_Ok ||
	    WriteRowText(&row, benchmarkMode) != ArenaStatus_Ok ||
	    WriteRowUnsigned(&row, summary.threadCount) != ArenaStatus_Ok ||
	    WriteRowUnsigned(&row, summary.repeatCount) != ArenaStatus_Ok ||
	    WriteRowText(&row, CaseConfigurationTextView(catalog, configuration, benchmarkCase->workUnitId)) !=
	        ArenaStatus_Ok ||
	    WriteRowUnsigned(&row, benchmarkCase->warmupWorkUnitCount) != ArenaStatus_Ok ||
	    WriteRowUnsigned(&row, benchmarkCase->measuredWorkUnitCount) != ArenaStatus_Ok ||
	    WriteRowText(&row, metadata.primaryMetricId) != ArenaStatus_Ok ||
	    WriteRowText(&row, metadata.primaryMetricUnit) != ArenaStatus_Ok ||
	    WriteRowText(&row, PrimaryDirectionText(metadata.direction)) != ArenaStatus_Ok ||
	    (summary.repeatCount == 0 ? WriteRowText(&row, {}) : WriteMeasurement(&row, summary.minimumPrimaryValue, timingFixture)) != ArenaStatus_Ok ||
	    (summary.repeatCount == 0 ? WriteRowText(&row, {}) : WriteMeasurement(&row, summary.medianPrimaryValue, timingFixture)) != ArenaStatus_Ok ||
	    (summary.repeatCount == 0 ? WriteRowText(&row, {}) : WriteMeasurement(&row, summary.maximumPrimaryValue, timingFixture)) != ArenaStatus_Ok ||
	    WriteRowUnsigned(&row, summary.bodyCount) != ArenaStatus_Ok ||
	    WriteRowUnsigned(&row, summary.shapeCount) != ArenaStatus_Ok ||
	    WriteRowUnsigned(&row, summary.queryCount) != ArenaStatus_Ok ||
	    WriteRowUnsigned(&row, summary.constraintCount) != ArenaStatus_Ok ||
	    WriteRowUnsigned64(&row, summary.invalidTransformCount) != ArenaStatus_Ok ||
	    WriteRowText(&row, std::string_view(summary.physicsSettings.data(), summary.physicsSettingsSize)) !=
	        ArenaStatus_Ok ||
	    WriteRowText(&row, std::string_view(summary.buildSettings.data(), summary.buildSettingsSize)) != ArenaStatus_Ok)
		return error->code;
	return ArenaStatus_Ok;
}

ArenaStatus CountRawFiles(const wchar_t* directory, std::uint32_t depth, std::uint32_t* count, StatusRecord* error)
{
	if (depth > 8)
		return PipelineError(error, ArenaStatus_InvalidResult, "raw_directory_depth");
	std::array<wchar_t, kRunPathCapacity> pattern = {};
	if (BuildChildPath(directory, L"*", &pattern) != ArenaStatus_Ok)
		return PipelineError(error, ArenaStatus_InvalidResult, "raw_scan_path_capacity");
	WIN32_FIND_DATAW data = {};
	HANDLE search = FindFirstFileW(pattern.data(), &data);
	if (search == INVALID_HANDLE_VALUE)
		return PipelineError(error, ArenaStatus_InvalidResult, "raw_directory_missing");
	ArenaStatus status = ArenaStatus_Ok;
	do
	{
		const std::wstring_view name(data.cFileName);
		if (name == L"." || name == L"..")
			continue;
		if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		{
			if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
			{
				status = PipelineError(error, ArenaStatus_InvalidResult, "raw_reparse_directory");
				break;
			}
			std::array<wchar_t, kRunPathCapacity> child = {};
			if (BuildChildPath(directory, name, &child) != ArenaStatus_Ok ||
			    CountRawFiles(child.data(), depth + 1, count, error) != ArenaStatus_Ok)
			{
				status = error->code;
				break;
			}
		}
		else if (name.size() >= 8 && name.ends_with(L"_raw.csv"))
			*count += 1;
	} while (FindNextFileW(search, &data) != 0);
	FindClose(search);
	return status;
}

void DeleteOutput(const std::array<wchar_t, kRunPathCapacity>& path)
{
	if (path[0] != L'\0')
		DeleteFileW(path.data());
}

ArenaStatus RenameOutput(const std::array<wchar_t, kRunPathCapacity>& temporary,
                         const std::array<wchar_t, kRunPathCapacity>& final, StatusRecord* error)
{
	return MoveFileExW(temporary.data(), final.data(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0
	           ? ArenaStatus_Ok
			   : PipelineError(error, ArenaStatus_RunFailed, "result_output_commit");
}

void CleanupUnitOutputs(const Catalog* catalog, const PreparedRunRequest* request, const RunPathRecord* paths,
                        const std::array<UnitOrderRecord, kResultUnitCapacity>& units, std::uint32_t unitCount,
                        int includeFinal)
{
	for (std::uint32_t index = 0; index < unitCount; ++index)
	{
		std::array<wchar_t, kRunPathCapacity> temporary = {};
		std::array<wchar_t, kRunPathCapacity> final = {};
		BuildUnitPath(paths, catalog, request, units[index], L"summary.csv.tmp", &temporary);
		DeleteOutput(temporary);
		if (includeFinal != 0)
		{
			BuildUnitPath(paths, catalog, request, units[index], L"summary.csv", &final);
			DeleteOutput(final);
		}
	}
}

ArenaStatus ValidateNormalizedRow(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error)
{
	NormalizedValidationContext* context = static_cast<NormalizedValidationContext*>(opaque);
	if (context->headerValidated != PresenceStatus_Present)
	{
		if (ValidateExactHeader(header, kNormalizedColumns, "normalized_validation_header", error) != ArenaStatus_Ok)
			return error->code;
		context->headerValidated = PresenceStatus_Present;
	}
	const auto value = [row](std::uint32_t column)
	{
		return CsvRowTextView(row, row->fields[column]);
	};
	std::uint32_t engineOrdinal = 0;
	while (engineOrdinal < context->manifest->engineCount &&
	       CatalogTextView(context->catalog,
	                       context->catalog->engines[context->manifest->engines[engineOrdinal].engineIndex].id) !=
	           value(1))
		++engineOrdinal;
	std::uint32_t caseIndex = 0;
	while (caseIndex < context->catalog->caseCount &&
	       CatalogTextView(context->catalog, context->catalog->cases[caseIndex].id) !=
	           ResultTextView(context->manifest, context->manifest->caseId))
		++caseIndex;
	if ((caseIndex == context->catalog->caseCount && ResultConfiguration(context->manifest) == nullptr && context->manifest->descriptiveCasePresence != PresenceStatus_Present) ||
	    engineOrdinal == context->manifest->engineCount)
		return PipelineError(error, ArenaStatus_InvalidResult, "normalized_validation_owner");
	const ResultMeasurementMode mode = context->manifest->measurementMode;
	const EffectiveRunConfiguration* configuration = ResultConfiguration(context->manifest);
	const CaseRecord& source =
	    configuration != nullptr || context->manifest->descriptiveCasePresence == PresenceStatus_Present ? context->manifest->configuration.benchmarkCase : context->catalog->cases[caseIndex];
	const ResultCaseMetadata metadata = ProjectResultCaseMetadata(context->catalog, source, mode, configuration != nullptr || context->manifest->descriptiveCasePresence == PresenceStatus_Present ? &context->manifest->configuration : nullptr);
	const CaseRecord& benchmarkCase = metadata.record;
	const EngineRecord& engine = context->catalog->engines[context->manifest->engines[engineOrdinal].engineIndex];
	std::uint32_t threadCount = 0;
	std::uint32_t measuredCount = 0;
	std::uint32_t warmupCount = 0;
	std::uint32_t repeatIndex = 0;
	std::uint32_t bodyCount = 0;
	std::uint32_t shapeCount = 0;
	std::uint32_t queryCount = 0;
	std::uint32_t constraintCount = 0;
	std::uint32_t effectiveThreadCount = 0;
	std::uint32_t fixtureRevision = 0;
	std::uint32_t requestedThreadCount = 0;
	std::uint32_t requestedWorkerCount = 0;
	std::uint32_t effectiveWorkerCount = 0;
	std::uint32_t completedCount = 0;
	std::uint64_t invalidTransformCount = 0;
	double primaryValue = 0.0;
	double meanMilliseconds = 0.0;
	double workUnitsPerSecond = 0.0;
	double workloadElapsed = 0.0;
	const std::string_view direction = PrimaryDirectionText(metadata.direction);
	const std::string_view timingScope =
	    mode == ResultMeasurementMode_PhysicalQuality ? std::string_view("absent")
		: benchmarkCase.fixtureKind == CaseFixtureKind_RayTracing ? std::string_view("ordinary_coherent_primary_wall_sum_inline")
		: ResultCaseTextView(context->catalog, context->manifest, benchmarkCase.workUnitId) == "cycle"
	        ? std::string_view("timed_phase_wall_sum_inline")
	        : std::string_view("work_unit_wall_sum_inline");
	if (value(0) != ResultTextView(context->manifest, context->manifest->runId) || value(2).empty() ||
	    value(3) != ResultTextView(context->manifest, context->manifest->hostRoute) || value(4).empty() ||
	    value(5) != ResultTextView(context->manifest, context->manifest->caseId) ||
	    ParseUnsigned(value(7), &threadCount) != ArenaStatus_Ok ||
	    value(6) != ResultThreadBenchmarkMode(context->manifest, threadCount) ||
	    value(8) != ResultTextView(context->manifest, context->manifest->workUnitId) ||
	    ParseUnsigned(value(9), &measuredCount) != ArenaStatus_Ok ||
	    measuredCount != context->manifest->measuredWorkUnitCount ||
	    ParseUnsigned(value(10), &warmupCount) != ArenaStatus_Ok ||
	    warmupCount != context->manifest->warmupWorkUnitCount ||
	    ParseUnsigned(value(11), &repeatIndex) != ArenaStatus_Ok || repeatIndex >= context->manifest->repeatCount ||
	    ParseUnsigned(value(12), &bodyCount) != ArenaStatus_Ok || bodyCount != benchmarkCase.bodyCount ||
	    ParseUnsigned(value(13), &shapeCount) != ArenaStatus_Ok || shapeCount != benchmarkCase.shapeCount ||
	    ParseUnsigned(value(14), &queryCount) != ArenaStatus_Ok || queryCount != benchmarkCase.queryCount ||
	    ParseUnsigned(value(15), &constraintCount) != ArenaStatus_Ok ||
	    constraintCount != benchmarkCase.constraintCount || value(16) != metadata.primaryMetricId ||
	    ParseMeasurement(value(17), mode, &primaryValue) != ArenaStatus_Ok || value(18) != metadata.primaryMetricUnit ||
	    value(19) != direction || ParseMeasurement(value(20), mode, &meanMilliseconds) != ArenaStatus_Ok ||
	    ParseMeasurement(value(21), mode, &workUnitsPerSecond) != ArenaStatus_Ok ||
	    ParseUnsigned64(value(22), &invalidTransformCount) != ArenaStatus_Ok ||
	    (value(23) != "ok" && value(23) != "failed") || value(24) != "ok" ||
	    value(25).empty() || value(26) != timingScope ||
	    ParseUnsigned(value(27), &effectiveThreadCount) != ArenaStatus_Ok || effectiveThreadCount != threadCount ||
	    value(28) != ResultCaseTextView(context->catalog, context->manifest, benchmarkCase.fixtureSemantic) ||
	    ParseUnsigned(value(29), &fixtureRevision) != ArenaStatus_Ok ||
	    (context->manifest->descriptiveCasePresence == PresenceStatus_Present ? fixtureRevision == 0 : fixtureRevision != benchmarkCase.fixtureRevision) || value(30).empty() || value(31).empty() ||
	    ParseUnsigned(value(32), &requestedThreadCount) != ArenaStatus_Ok || requestedThreadCount != threadCount ||
	    ParseUnsigned(value(33), &requestedWorkerCount) != ArenaStatus_Ok ||
	    requestedWorkerCount != WorkerCount(engine.requestedWorkerPolicy, threadCount) ||
	    ParseUnsigned(value(36), &effectiveWorkerCount) != ArenaStatus_Ok ||
	    effectiveWorkerCount != WorkerCount(engine.effectiveWorkerPolicy, threadCount) ||
	    value(37) != (engine.mainThreadParticipation == MainThreadParticipation_Yes ? "yes" : "no") ||
	    ParseUnsigned(value(48), &completedCount) != ArenaStatus_Ok || completedCount != measuredCount ||
	    ParseMeasurement(value(49), mode, &workloadElapsed) != ArenaStatus_Ok || value(54).empty())
		return PipelineError(error, ArenaStatus_InvalidResult, "normalized_validation_contract");
	const double expectedMean = mode == ResultMeasurementMode_PhysicalQuality
	                                ? 0.0
	                                : RoundDoubleNine(workloadElapsed / static_cast<double>(completedCount));
	const double expectedRate = mode == ResultMeasurementMode_PhysicalQuality
	                                ? 0.0
	                                : RoundDoubleNine(static_cast<double>(completedCount) * 1000.0 / workloadElapsed);
	const double expectedPrimary =
	    (benchmarkCase.fixtureKind == CaseFixtureKind_RayTracing || ResultCaseTextView(context->catalog, context->manifest, benchmarkCase.workUnitId) == "query_batch")
	        ? RoundDoubleNine(static_cast<double>(queryCount) * completedCount * 1000.0 / workloadElapsed)
	        : expectedMean;
	if (CompareSerializedMetric(meanMilliseconds, expectedMean) != ArenaStatus_Ok ||
	    CompareSerializedMetric(workUnitsPerSecond, expectedRate) != ArenaStatus_Ok ||
	    CompareSerializedMetric(primaryValue, expectedPrimary) != ArenaStatus_Ok)
		return PipelineError(error, ArenaStatus_InvalidResult, "normalized_derived_metric_disagreement");
	if ((engine.threadSupportMode == ThreadSupportMode_Explicit && (value(34).empty() || value(35).empty())) ||
	    (engine.threadSupportMode != ThreadSupportMode_Explicit && (!value(34).empty() || !value(35).empty())))
		return PipelineError(error, ArenaStatus_InvalidResult, "normalized_validation_taskgraph");
	const int visual = value(6) == "visualized_release" ? 1 : 0;
	if ((visual == 0 && (!value(50).empty() || !value(51).empty() || !value(52).empty() || !value(53).empty())) ||
	    (visual != 0 && ((mode == ResultMeasurementMode_Timed ? (value(50).empty() || value(51).empty())
	                                                          : (!value(50).empty() || !value(51).empty())) ||
	                     value(52) != "ok" || value(53).empty())))
		return PipelineError(error, ArenaStatus_InvalidResult, "normalized_validation_visual");
	std::uint32_t threadOrdinal = 0;
	while (threadOrdinal < context->manifest->threadCount &&
	       context->manifest->threadCounts[threadOrdinal] != threadCount)
		++threadOrdinal;
	if (threadOrdinal == context->manifest->threadCount ||
	    context->seen[engineOrdinal][threadOrdinal][repeatIndex] == PresenceStatus_Present)
		return PipelineError(error, ArenaStatus_InvalidResult, "normalized_validation_duplicate");
	context->seen[engineOrdinal][threadOrdinal][repeatIndex] = PresenceStatus_Present;
	context->rowCount += 1;
	return ArenaStatus_Ok;
}
ArenaStatus AccumulateNormalizedSummaryRow(const CsvHeader* header, const CsvRow* row, void* opaque,
                                           StatusRecord* error)
{
	NormalizedSummaryContext* context = static_cast<NormalizedSummaryContext*>(opaque);
	const ResultMeasurementMode mode = context->validation.manifest->measurementMode;
	if (ValidateNormalizedRow(header, row, &context->validation, error) != ArenaStatus_Ok)
		return error->code;
	const auto value = [row](std::uint32_t column)
	{
		return CsvRowTextView(row, row->fields[column]);
	};
	std::uint32_t engineOrdinal = 0;
	while (engineOrdinal < context->validation.manifest->engineCount &&
	       CatalogTextView(
	           context->validation.catalog,
	           context->validation.catalog->engines[context->validation.manifest->engines[engineOrdinal].engineIndex]
	               .id) != value(1))
		++engineOrdinal;
	if (engineOrdinal != context->targetEngineOrdinal)
		return ArenaStatus_Ok;
	std::uint32_t threadCount = 0;
	std::uint32_t repeatIndex = 0;
	if (ParseUnsigned(value(7), &threadCount) != ArenaStatus_Ok ||
	    ParseUnsigned(value(11), &repeatIndex) != ArenaStatus_Ok)
		return PipelineError(error, ArenaStatus_InvalidResult, "normalized_summary_identity");
	std::uint32_t threadOrdinal = 0;
	while (threadOrdinal < context->validation.manifest->threadCount &&
	       context->validation.manifest->threadCounts[threadOrdinal] != threadCount)
		++threadOrdinal;
	if (threadOrdinal == context->validation.manifest->threadCount || repeatIndex >= kRunRepeatCapacity)
		return PipelineError(error, ArenaStatus_InvalidResult, "normalized_summary_thread");
	ResultAccumulator& accumulator = context->accumulators[threadOrdinal];
	std::uint32_t bodyCount = 0;
	std::uint32_t shapeCount = 0;
	std::uint32_t queryCount = 0;
	std::uint32_t constraintCount = 0;
	std::uint64_t invalidTransformCount = 0;
	double primaryValue = 0.0;
	double meanMilliseconds = 0.0;
	double workUnitsPerSecond = 0.0;
	if (ParseUnsigned(value(12), &bodyCount) != ArenaStatus_Ok ||
	    ParseUnsigned(value(13), &shapeCount) != ArenaStatus_Ok ||
	    ParseUnsigned(value(14), &queryCount) != ArenaStatus_Ok ||
	    ParseUnsigned(value(15), &constraintCount) != ArenaStatus_Ok ||
	    ParseMeasurement(value(17), mode, &primaryValue) != ArenaStatus_Ok ||
	    ParseMeasurement(value(20), mode, &meanMilliseconds) != ArenaStatus_Ok ||
	    ParseMeasurement(value(21), mode, &workUnitsPerSecond) != ArenaStatus_Ok ||
	    ParseUnsigned64(value(22), &invalidTransformCount) != ArenaStatus_Ok || value(30).empty())
		return PipelineError(error, ArenaStatus_InvalidResult, "normalized_summary_metric");
	if (accumulator.rowCount == 0)
	{
		if (value(30).size() >= accumulator.physicsSettings.size() ||
		    value(31).size() >= accumulator.buildSettings.size())
			return PipelineError(error, ArenaStatus_InvalidResult, "normalized_summary_settings_capacity");
		accumulator.bodyCount = bodyCount;
		accumulator.shapeCount = shapeCount;
		accumulator.queryCount = queryCount;
		accumulator.constraintCount = constraintCount;
		accumulator.invalidTransformCount = 0;
		std::copy(value(30).begin(), value(30).end(), accumulator.physicsSettings.begin());
		accumulator.physicsSettingsSize = static_cast<std::uint32_t>(value(30).size());
		std::copy(value(31).begin(), value(31).end(), accumulator.buildSettings.begin());
		accumulator.buildSettingsSize = static_cast<std::uint32_t>(value(31).size());
	}
	else if (accumulator.bodyCount != bodyCount || accumulator.shapeCount != shapeCount ||
	         accumulator.queryCount != queryCount || accumulator.constraintCount != constraintCount ||
	         value(30) != std::string_view(accumulator.physicsSettings.data(), accumulator.physicsSettingsSize) ||
	         value(31) != std::string_view(accumulator.buildSettings.data(), accumulator.buildSettingsSize))
		return PipelineError(error, ArenaStatus_InvalidResult, "normalized_summary_drift");
	accumulator.invalidTransformCount += invalidTransformCount;
	if (ParseMeasurement(value(49), mode, &accumulator.workloadElapsedMilliseconds[repeatIndex]) != ArenaStatus_Ok)
		return PipelineError(error, ArenaStatus_InvalidResult, "normalized_summary_duration");
	accumulator.seen[repeatIndex] = PresenceStatus_Present;
	accumulator.primaryValues[repeatIndex] = primaryValue;
	accumulator.meanMilliseconds[repeatIndex] = meanMilliseconds;
	accumulator.workUnitsPerSecond[repeatIndex] = workUnitsPerSecond;
	accumulator.rowCount += 1;
	return ArenaStatus_Ok;
}

ArenaStatus FinalizeResults(const wchar_t* repositoryRoot, const Catalog* catalog, const ReleaseCatalog* releaseCatalog,
                            const PreparedRunRequest* request, const RunPathRecord* paths, ResultPipelineRecord* record,
                            StatusRecord* error)
{
	ResultManifestRecord manifest = {};
	if (paths->manifestPath[0] != 0 &&
	    LoadResultManifest(paths->manifestPath.data(), catalog, &manifest, error) != ArenaStatus_Ok)
		return error->code;
	const CaseRecord observationCase = RunObservationCase(request->configuration.benchmarkCase, request->verificationMode);
	const CaseRecord* benchmarkCase = &observationCase;
	std::uint32_t rowsPerRepeat = 0;
	for (std::uint32_t index = 0; index < benchmarkCase->observationCount; ++index)
		rowsPerRepeat += request->configuration.observations[index].sampleIndexCount;
	const std::uint64_t aggregateObservationRows =
	    static_cast<std::uint64_t>(request->totalUnitCount) * request->repeatCount * rowsPerRepeat;
	if (rowsPerRepeat > kObservationRepeatRowCapacity || aggregateObservationRows > kObservationAggregateRowCapacity)
		return PipelineError(error, ArenaStatus_InvalidResult, "observation_result_capacity");
	std::unique_ptr<std::array<ResultSummary, kResultUnitCapacity>> summaries =
	    std::unique_ptr<std::array<ResultSummary, kResultUnitCapacity>>(
	        new (std::nothrow) std::array<ResultSummary, kResultUnitCapacity>{});
	if (summaries == nullptr)
		return PipelineError(error, ArenaStatus_RunFailed, "result_summary_workspace");
	std::uint32_t rawFileCount = 0;
	if (CountRawFiles(paths->rawDirectory.data(), 0, &rawFileCount, error) != ArenaStatus_Ok ||
	    (rawFileCount > request->totalUnitCount || (manifest.executionFailures.empty() && rawFileCount != request->totalUnitCount)))
		return error->code != ArenaStatus_Ok ? error->code
		                                     : PipelineError(error, ArenaStatus_InvalidResult, "raw_file_cardinality");
	std::array<UnitOrderRecord, kResultUnitCapacity> units = {};
	std::uint32_t unitCount = 0;
	for (std::uint32_t engine = 0; engine < request->engineCount; ++engine)
		for (std::uint32_t thread = 0; thread < request->threadCount; ++thread)
			units[unitCount++] = {engine, thread};
	std::sort(units.begin(), units.begin() + unitCount,
	          [catalog, request](const UnitOrderRecord& left, const UnitOrderRecord& right)
	          {
		          const std::string_view leftId =
		              CatalogTextView(catalog, catalog->engines[request->engineIndexes[left.selectedEngineIndex]].id);
		          const std::string_view rightId =
		              CatalogTextView(catalog, catalog->engines[request->engineIndexes[right.selectedEngineIndex]].id);
		          if (leftId != rightId)
			          return leftId < rightId;
		          return request->threadCounts[left.selectedThreadIndex] <
				         request->threadCounts[right.selectedThreadIndex];
	          });
	std::array<wchar_t, kRunPathCapacity> normalizedTemporary = {};
	std::array<wchar_t, kRunPathCapacity> normalizedFinal = {};
	std::array<wchar_t, kRunPathCapacity> summaryTemporary = {};
	std::array<wchar_t, kRunPathCapacity> summaryFinal = {};
	std::array<wchar_t, kRunPathCapacity> observationsTemporary = {};
	std::array<wchar_t, kRunPathCapacity> observationsFinal = {};
	if (BuildRootOutputPath(paths, L"normalized.csv.tmp", &normalizedTemporary) != ArenaStatus_Ok ||
	    BuildRootOutputPath(paths, L"normalized.csv", &normalizedFinal) != ArenaStatus_Ok ||
	    BuildRootOutputPath(paths, L"summary.csv.tmp", &summaryTemporary) != ArenaStatus_Ok ||
	    BuildRootOutputPath(paths, L"summary.csv", &summaryFinal) != ArenaStatus_Ok ||
	    BuildRootOutputPath(paths, L"observations.csv.tmp", &observationsTemporary) != ArenaStatus_Ok ||
	    BuildRootOutputPath(paths, L"observations.csv", &observationsFinal) != ArenaStatus_Ok)
		return PipelineError(error, ArenaStatus_InvalidResult, "result_output_path_capacity");
	DeleteOutput(normalizedTemporary);
	DeleteOutput(summaryTemporary);
	DeleteOutput(observationsTemporary);
	CleanupUnitOutputs(catalog, request, paths, units, unitCount, 0);
	CsvWriter normalizedWriter = {};
	CsvWriter observationsWriter = {};
	if (OpenCsvWriter(normalizedTemporary.data(), &normalizedWriter, error) != ArenaStatus_Ok ||
	    WriteHeader(&normalizedWriter, kNormalizedColumns, error) != ArenaStatus_Ok ||
	    OpenCsvWriter(observationsTemporary.data(), &observationsWriter, error) != ArenaStatus_Ok ||
	    WriteHeader(&observationsWriter, kObservationRootColumns, error) != ArenaStatus_Ok)
	{
		DestroyCsvWriter(&normalizedWriter);
		DestroyCsvWriter(&observationsWriter);
		DeleteOutput(normalizedTemporary);
		DeleteOutput(observationsTemporary);
		return error->code;
	}
	ArenaStatus status = ArenaStatus_Ok;
	for (std::uint32_t unitIndex = 0; unitIndex < unitCount && status == ArenaStatus_Ok; ++unitIndex)
	{
		const UnitOrderRecord& unit = units[unitIndex];
		const std::string_view benchmarkMode = RecordingForThread(request->recordingThreads,
		    request->threadCounts[unit.selectedThreadIndex]) == RecordingMode_On ? "recorded_api" : "headless_api";
		const std::uint32_t engineIndex = request->engineIndexes[unit.selectedEngineIndex];
		const std::uint32_t artifactIndex = request->artifactIndexes[unit.selectedEngineIndex];
		std::array<wchar_t, kRunPathCapacity> rawPath = {};
		std::array<wchar_t, kRunPathCapacity> observationPath = {};
		if (artifactIndex >= releaseCatalog->artifactCount ||
		    BuildUnitPath(paths, catalog, request, unit, L"raw.csv", &rawPath) != ArenaStatus_Ok ||
		    BuildUnitPath(paths, catalog, request, unit, L"observations.csv", &observationPath) != ArenaStatus_Ok)
		{
			status = PipelineError(error, ArenaStatus_InvalidResult, "result_unit_file_missing");
			break;
		}
		for (const ExecutionFailure& failure : manifest.executionFailures)
		{
			if (failure.engineIndex != engineIndex || failure.threadCount != request->threadCounts[unit.selectedThreadIndex] ||
			    failure.outcome != ExecutionOutcome_NotRun) continue;
			const std::string_view engine = CatalogTextView(catalog, catalog->engines[engineIndex].id);
			const std::string tuple = std::string(engine) + "_t" + std::to_string(failure.threadCount) + "_r" + std::to_string(failure.repeatIndex);
			const std::filesystem::path directory = std::filesystem::path(rawPath.data()).parent_path();
			const std::filesystem::path recording = request->recordingKind == RecordingKind_NativeRayHits
			    ? RayImageTuplePath(paths->resultDirectory.data(), engine, failure.threadCount, failure.repeatIndex)
			    : ReplayTuplePath(paths->resultDirectory.data(), engine, failure.threadCount, failure.repeatIndex);
			const std::filesystem::path spool = recording.parent_path() / ".pending" / recording.filename();
			for (const std::filesystem::path& artifact : {directory / (tuple + "_step-timing.csv"),
			    directory / (tuple + "_ray-tracing.csv"), directory / (tuple + "_ray-capabilities.csv"), directory / (tuple + "_ray-process.csv"),
			    benchmark_stack::TracePath(rawPath.data(), failure.repeatIndex), recording, std::filesystem::path(recording.wstring() + L".partial"),
			    spool, std::filesystem::path(spool.wstring() + L".partial")})
				if (GetFileAttributesW(artifact.c_str()) != INVALID_FILE_ATTRIBUTES)
					status = PipelineError(error, ArenaStatus_InvalidResult, "skipped_tuple_artifact_conflict");
		}
		if (status != ArenaStatus_Ok) break;
		ObservationUnitContext observationContext = {};
		observationContext.catalog = catalog;
		observationContext.benchmarkCase = benchmarkCase;
		observationContext.configuration = &request->configuration;
		observationContext.paths = paths;
		observationContext.rootWriter = &observationsWriter;
		observationContext.engineId = CatalogTextView(catalog, catalog->engines[engineIndex].id);
		observationContext.benchmarkMode = benchmarkMode;
		observationContext.threadCount = request->threadCounts[unit.selectedThreadIndex];
		observationContext.expectedRowCount = rowsPerRepeat * request->repeatCount;
		observationContext.repeatCount = request->repeatCount;
		observationContext.engineIndex = engineIndex;
		observationContext.failures = manifest.executionFailures;
		CsvHeader observationHeader = {};
		CsvReadRecord observationRead = {};
		if (GetFileAttributesW(observationPath.data()) != INVALID_FILE_ATTRIBUTES &&
		    (ReadCsvFile(observationPath.data(), &observationHeader, ConsumeObservationRow, &observationContext,
		                 &observationRead, error) != ArenaStatus_Ok ||
		     ValidateExactHeader(&observationHeader, kObservationSidecarColumns, "observation_sidecar_header", error) != ArenaStatus_Ok))
		{
			status = error->code;
			break;
		}
		for (std::uint32_t repeat = 0; repeat < request->repeatCount; ++repeat)
			if (observationContext.repeatRows[repeat] != rowsPerRepeat &&
			    FindExecutionFailure(manifest.executionFailures, engineIndex, observationContext.threadCount, repeat) == nullptr)
				status = PipelineError(error, ArenaStatus_InvalidResult, "observation_sidecar_cardinality");
		if (status != ArenaStatus_Ok)
			break;
		record->observationRowCount += observationContext.rowCount;
		RawContext context = {};
		context.failures = manifest.executionFailures;
		context.repositoryRoot = repositoryRoot;
		context.catalog = catalog;
		context.releaseCatalog = releaseCatalog;
		context.request = request;
		context.paths = paths;
		context.benchmarkCase = benchmarkCase;
		context.configuration = &request->configuration;
		context.engine = &catalog->engines[engineIndex];
		context.artifact = &releaseCatalog->artifacts[artifactIndex];
		context.normalizedWriter = &normalizedWriter;
		context.engineIndex = engineIndex;
		context.threadCount = request->threadCounts[unit.selectedThreadIndex];
		if (MakeRepositoryRelative(repositoryRoot, rawPath.data(), &context.rawRelativePath,
		                           &context.rawRelativePathSize) != ArenaStatus_Ok)
		{
			status = PipelineError(error, ArenaStatus_InvalidResult, "raw_path_outside_repository");
			break;
		}
		CsvHeader rawHeader = {};
		CsvReadRecord rawRead = {};
		if (GetFileAttributesW(rawPath.data()) != INVALID_FILE_ATTRIBUTES &&
		    ReadCsvFile(rawPath.data(), &rawHeader, ConsumeRawRow, &context, &rawRead, error) != ArenaStatus_Ok)
		{
			status = error->code;
			break;
		}
		for (std::uint32_t repeat = 0; repeat < request->repeatCount; ++repeat)
			if (context.accumulator.seen[repeat] == PresenceStatus_Absent &&
			    FindExecutionFailure(manifest.executionFailures, engineIndex, context.threadCount, repeat) == nullptr)
				status = PipelineError(error, ArenaStatus_InvalidResult, "raw_repeat_cardinality");
		if (status != ArenaStatus_Ok)
			break;
		record->maximumRawRowTextUsed = std::max(record->maximumRawRowTextUsed, rawRead.maximumRowTextUsed);
		record->normalizedRowCount += context.accumulator.rowCount;
		(*summaries)[unitIndex] = SummarizeUnit(context.accumulator, engineIndex, context.threadCount);
		if (context.accumulator.rowCount == 0)
		{
			ResultSummary& summary = (*summaries)[unitIndex];
			summary.bodyCount = benchmarkCase->bodyCount;
			summary.shapeCount = benchmarkCase->shapeCount;
			summary.queryCount = benchmarkCase->queryCount;
			summary.constraintCount = benchmarkCase->constraintCount;
			constexpr std::string_view unavailable = "unavailable";
			std::copy(unavailable.begin(), unavailable.end(), summary.physicsSettings.begin());
			std::copy(unavailable.begin(), unavailable.end(), summary.buildSettings.begin());
			summary.physicsSettingsSize = summary.buildSettingsSize = static_cast<std::uint32_t>(unavailable.size());
		}
		std::array<wchar_t, kRunPathCapacity> sliceTemporary = {};
		if (BuildUnitPath(paths, catalog, request, unit, L"summary.csv.tmp", &sliceTemporary) != ArenaStatus_Ok)
		{
			status = PipelineError(error, ArenaStatus_InvalidResult, "slice_summary_path_capacity");
			break;
		}
		std::error_code directoryError;
		std::filesystem::create_directories(std::filesystem::path(sliceTemporary.data()).parent_path(), directoryError);
		if (directoryError)
		{
			status = PipelineError(error, ArenaStatus_RunFailed, "slice_summary_directory_failed");
			break;
		}
		CsvWriter sliceWriter = {};
		if (OpenCsvWriter(sliceTemporary.data(), &sliceWriter, error) != ArenaStatus_Ok ||
		    WriteHeader(&sliceWriter, kSummaryColumns, error) != ArenaStatus_Ok ||
		    WriteResultSummaryRow(&sliceWriter, catalog, benchmarkCase,
		                          std::string_view(paths->runId.data(), paths->runIdSize), benchmarkMode,
		                          (*summaries)[unitIndex], error, ResultMeasurementMode_PhysicalQuality,
		                          &request->configuration) != ArenaStatus_Ok ||
		    FinishCsvWriter(&sliceWriter, error) != ArenaStatus_Ok)
		{
			DestroyCsvWriter(&sliceWriter);
			status = error->code;
			break;
		}
		record->sliceSummaryCount += 1;
	}
	if (status == ArenaStatus_Ok && FinishCsvWriter(&normalizedWriter, error) != ArenaStatus_Ok)
		status = error->code;
	else if (status != ArenaStatus_Ok)
		DestroyCsvWriter(&normalizedWriter);
	if (status == ArenaStatus_Ok && FinishCsvWriter(&observationsWriter, error) != ArenaStatus_Ok)
		status = error->code;
	else if (status != ArenaStatus_Ok)
		DestroyCsvWriter(&observationsWriter);
	if (status == ArenaStatus_Ok)
	{
		std::sort(summaries->begin(), summaries->begin() + unitCount,
		          [catalog, benchmarkCase](const ResultSummary& left, const ResultSummary& right)
		          {
			          if (left.threadCount != right.threadCount)
				          return left.threadCount < right.threadCount;
			          if (left.medianPrimaryValue != right.medianPrimaryValue)
				          return benchmarkCase->primaryMetricDirection == PrimaryMetricDirection_HigherIsBetter
				                     ? left.medianPrimaryValue > right.medianPrimaryValue
									 : left.medianPrimaryValue < right.medianPrimaryValue;
			          return CatalogTextView(catalog, catalog->engines[left.engineIndex].id) <
					         CatalogTextView(catalog, catalog->engines[right.engineIndex].id);
		          });
		CsvWriter summaryWriter = {};
		if (OpenCsvWriter(summaryTemporary.data(), &summaryWriter, error) != ArenaStatus_Ok ||
		    WriteHeader(&summaryWriter, kSummaryColumns, error) != ArenaStatus_Ok)
			status = error->code;
		for (std::uint32_t index = 0; index < unitCount && status == ArenaStatus_Ok; ++index)
			if (WriteResultSummaryRow(&summaryWriter, catalog, benchmarkCase,
			                          std::string_view(paths->runId.data(), paths->runIdSize),
			                          RecordingForThread(request->recordingThreads, (*summaries)[index].threadCount) == RecordingMode_On ? "recorded_api" : "headless_api",
			                          (*summaries)[index], error, ResultMeasurementMode_PhysicalQuality,
			                          &request->configuration) != ArenaStatus_Ok)
				status = error->code;
		if (status == ArenaStatus_Ok && FinishCsvWriter(&summaryWriter, error) != ArenaStatus_Ok)
			status = error->code;
		else if (status != ArenaStatus_Ok)
			DestroyCsvWriter(&summaryWriter);
	}
	if (status == ArenaStatus_Ok && RenameOutput(normalizedTemporary, normalizedFinal, error) != ArenaStatus_Ok)
		status = error->code;
	if (status == ArenaStatus_Ok && RenameOutput(summaryTemporary, summaryFinal, error) != ArenaStatus_Ok)
		status = error->code;
	if (status == ArenaStatus_Ok && RenameOutput(observationsTemporary, observationsFinal, error) != ArenaStatus_Ok)
		status = error->code;
	for (std::uint32_t index = 0; index < unitCount && status == ArenaStatus_Ok; ++index)
	{
		std::array<wchar_t, kRunPathCapacity> temporary = {};
		std::array<wchar_t, kRunPathCapacity> final = {};
		if (BuildUnitPath(paths, catalog, request, units[index], L"summary.csv.tmp", &temporary) != ArenaStatus_Ok ||
		    BuildUnitPath(paths, catalog, request, units[index], L"summary.csv", &final) != ArenaStatus_Ok ||
		    RenameOutput(temporary, final, error) != ArenaStatus_Ok)
			status = error->code;
	}
	if (status != ArenaStatus_Ok)
	{
		DestroyCsvWriter(&normalizedWriter);
		DestroyCsvWriter(&observationsWriter);
		DeleteOutput(normalizedTemporary);
		DeleteOutput(summaryTemporary);
		DeleteOutput(observationsTemporary);
		DeleteOutput(normalizedFinal);
		DeleteOutput(summaryFinal);
		DeleteOutput(observationsFinal);
		CleanupUnitOutputs(catalog, request, paths, units, unitCount, 1);
		*record = {};
		return status;
	}
	record->summaryRowCount = unitCount;
	return ArenaStatus_Ok;
}
} // namespace physics_arena
