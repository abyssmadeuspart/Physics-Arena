#include "physics_arena/ray_tracing_results.h"
#include "physics_arena/run_finalization.h"

#include "physics_arena/csv_io.h"
#include "physics_arena/stack_stability.h"

#include <algorithm>
#include <memory>
#include <array>
#include <charconv>
#include <cmath>
#include <cwchar>
#include <string_view>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace physics_arena
{
namespace
{
ArenaStatus FinalizationError(StatusRecord* error, ArenaStatus status, std::string_view detail)
{
	*error = {};
	const std::string_view component = "run_finalization";
	const std::string_view statusText = ArenaStatusText(status);
	std::copy(component.begin(), component.end(), error->component.begin());
	error->componentSize = static_cast<std::uint32_t>(component.size());
	std::copy(statusText.begin(), statusText.end(), error->status.begin());
	error->statusSize = static_cast<std::uint32_t>(statusText.size());
	if (detail.size() > error->detail.size())
		detail = "run_finalization_detail_capacity";
	std::copy(detail.begin(), detail.end(), error->detail.begin());
	error->detailSize = static_cast<std::uint32_t>(detail.size());
	error->code = status;
	return status;
}

ArenaStatus TimingPath(const wchar_t* directory, const wchar_t* name, std::array<wchar_t, kRunPathCapacity>* path,
                       StatusRecord* error)
{
	const std::size_t directorySize = std::wcslen(directory);
	const std::size_t nameSize = std::wcslen(name);
	if (directorySize + nameSize + 2 > path->size())
		return FinalizationError(error, ArenaStatus_InvalidResult, "visual_timing_path");
	std::copy(directory, directory + directorySize, path->begin());
	std::size_t size = directorySize;
	if (size != 0 && (*path)[size - 1] != L'\\' && (*path)[size - 1] != L'/')
		(*path)[size++] = L'\\';
	std::copy(name, name + nameSize, path->begin() + size);
	(*path)[size + nameSize] = L'\0';
	return ArenaStatus_Ok;
}

constexpr std::array<std::string_view, 3> kFragmentColumns = {"step_index", "physics_step_ms", "render_frame_ms"};
constexpr std::array<std::string_view, 8> kRootTimingColumns = {"run_id",          "case_id",        "engine_id",
                                                                "thread_count",    "repeat_index",   "step_index",
                                                                "physics_step_ms", "render_frame_ms"};

struct RawTimingTotals
{
	std::array<PresenceStatus, kRunRepeatCapacity> seen;
	std::string_view engineId;
	std::uint32_t threadCount;
	std::int32_t engineColumn, threadColumn, repeatColumn;
	std::array<double, kRunRepeatCapacity> physics;
	std::array<double, kRunRepeatCapacity> render;
	std::int32_t physicsColumn;
	std::int32_t renderColumn;
	std::uint32_t rowCount;
	TimingRenderSeries renderSeries;
	PresenceStatus headerMapped;
};

struct FragmentMergeContext
{
	CsvWriter* writer;
	std::string_view runId;
	std::string_view caseId;
	std::string_view engineId;
	std::array<std::int32_t, kFragmentColumns.size()> headerIndexes;
	double physicsTotal;
	double renderTotal;
	std::uint32_t threadCount;
	std::uint32_t repeatIndex;
	std::uint32_t measuredWorkUnitCount;
	std::uint32_t rowCount;
	TimingRenderSeries renderSeries;
	PresenceStatus headerMapped;
};

ArenaStatus ParseUnsigned(std::string_view text, std::uint32_t* value)
{
	if (text.empty())
		return ArenaStatus_InvalidResult;
	const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), *value);
	return result.ec == std::errc() && result.ptr == text.data() + text.size() ? ArenaStatus_Ok
	                                                                           : ArenaStatus_InvalidResult;
}

ArenaStatus ParseFinite(std::string_view text, double minimum, double* value)
{
	if (text.empty())
		return ArenaStatus_InvalidResult;
	const std::from_chars_result result =
	    std::from_chars(text.data(), text.data() + text.size(), *value, std::chars_format::general);
	return result.ec == std::errc() && result.ptr == text.data() + text.size() && std::isfinite(*value) &&
	               *value >= minimum
	           ? ArenaStatus_Ok
			   : ArenaStatus_InvalidResult;
}

ArenaStatus ConsumeRawTimingTotal(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error)
{
	RawTimingTotals* totals = static_cast<RawTimingTotals*>(opaque);
	if (totals->headerMapped != PresenceStatus_Present)
	{
		totals->engineColumn = totals->threadColumn = totals->repeatColumn = -1;
		totals->physicsColumn = -1;
		totals->renderColumn = -1;
		for (std::uint32_t field = 0; field < header->fieldCount; ++field)
		{
			const std::string_view name = CsvHeaderTextView(header, header->fields[field]);
			if (name == "engine_id")
				totals->engineColumn = static_cast<std::int32_t>(field);
			else if (name == "thread_count")
				totals->threadColumn = static_cast<std::int32_t>(field);
			else if (name == "repeat_index")
				totals->repeatColumn = static_cast<std::int32_t>(field);
			else if (name == "workload_elapsed_ms")
				totals->physicsColumn = static_cast<std::int32_t>(field);
			else if (name == "render_elapsed_ms")
				totals->renderColumn = static_cast<std::int32_t>(field);
		}
		if (totals->engineColumn < 0 || totals->threadColumn < 0 || totals->repeatColumn < 0 || totals->physicsColumn < 0 ||
		    (totals->renderSeries != TimingRenderSeries_Absent && totals->renderColumn < 0))
			return FinalizationError(error, ArenaStatus_InvalidResult, "timing_raw_header");
		totals->headerMapped = PresenceStatus_Present;
	}
	std::uint32_t thread = 0, repeat = 0;
	if (ParseUnsigned(CsvRowTextView(row, row->fields[totals->threadColumn]), &thread) != ArenaStatus_Ok ||
	    ParseUnsigned(CsvRowTextView(row, row->fields[totals->repeatColumn]), &repeat) != ArenaStatus_Ok || repeat >= kRunRepeatCapacity)
		return FinalizationError(error, ArenaStatus_InvalidResult, "timing_raw_identity");
	if (CsvRowTextView(row, row->fields[totals->engineColumn]) != totals->engineId || thread != totals->threadCount)
		return ArenaStatus_Ok;
	if (totals->seen[repeat] == PresenceStatus_Present)
		return FinalizationError(error, ArenaStatus_InvalidResult, "timing_raw_duplicate");
	 totals->seen[repeat] = PresenceStatus_Present;
	if (totals->rowCount >= totals->physics.size())
		return FinalizationError(error, ArenaStatus_InvalidResult, "timing_raw_repeat_capacity");
	double render = 0.0;
	if (ParseFinite(CsvRowTextView(row, row->fields[totals->physicsColumn]), 0.000000001,
	                &totals->physics[repeat]) != ArenaStatus_Ok ||
	    (totals->renderSeries != TimingRenderSeries_Absent &&
	     ParseFinite(CsvRowTextView(row, row->fields[totals->renderColumn]), 0.0, &render) != ArenaStatus_Ok))
		return FinalizationError(error, ArenaStatus_InvalidResult, "timing_raw_total");
	totals->render[repeat] = render;
	totals->rowCount += 1;
	return ArenaStatus_Ok;
}

ArenaStatus WriteRootText(CsvWriter* writer, std::string_view value, std::uint32_t field, StatusRecord* error)
{
	return WriteCsvField(
	    writer, value,
	    field == kRootTimingColumns.size() - 1 ? CsvFieldTerminator_EndRow : CsvFieldTerminator_MoreFields, error);
}

ArenaStatus WriteRootUnsigned(CsvWriter* writer, std::uint32_t value, std::uint32_t field, StatusRecord* error)
{
	std::array<char, 32> text = {};
	const std::to_chars_result result = std::to_chars(text.data(), text.data() + text.size(), value);
	return result.ec == std::errc() ? WriteRootText(writer, std::string_view(text.data(), result.ptr), field, error)
	                                : FinalizationError(error, ArenaStatus_InvalidResult, "timing_root_unsigned");
}

ArenaStatus ConsumeTimingFragment(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error)
{
	FragmentMergeContext* context = static_cast<FragmentMergeContext*>(opaque);
	if (context->headerMapped != PresenceStatus_Present)
	{
		context->headerIndexes.fill(-1);
		if (header->fieldCount != kFragmentColumns.size())
			return FinalizationError(error, ArenaStatus_InvalidResult, "timing_fragment_header");
		for (std::uint32_t field = 0; field < header->fieldCount; ++field)
		{
			const std::string_view name = CsvHeaderTextView(header, header->fields[field]);
			std::uint32_t expected = 0;
			while (expected < kFragmentColumns.size() && kFragmentColumns[expected] != name)
				++expected;
			if (expected == kFragmentColumns.size() || context->headerIndexes[expected] >= 0)
				return FinalizationError(error, ArenaStatus_InvalidResult, "timing_fragment_header");
			context->headerIndexes[expected] = static_cast<std::int32_t>(field);
		}
		context->headerMapped = PresenceStatus_Present;
	}
	if (context->rowCount >= context->measuredWorkUnitCount)
		return FinalizationError(error, ArenaStatus_InvalidResult, "timing_fragment_row_overflow");
	const std::string_view stepText = CsvRowTextView(row, row->fields[context->headerIndexes[0]]);
	const std::string_view physicsText = CsvRowTextView(row, row->fields[context->headerIndexes[1]]);
	const std::string_view renderText = CsvRowTextView(row, row->fields[context->headerIndexes[2]]);
	std::uint32_t stepIndex = 0;
	double physics = 0.0;
	double render = 0.0;
	const PresenceStatus renderPresence = renderText.empty() ? PresenceStatus_Absent : PresenceStatus_Present;
	if (ParseUnsigned(stepText, &stepIndex) != ArenaStatus_Ok || stepIndex != context->rowCount + 1 ||
	    ParseFinite(physicsText, 0.000000001, &physics) != ArenaStatus_Ok ||
	    (context->renderSeries == TimingRenderSeries_Absent
	         ? renderPresence != PresenceStatus_Absent
			 : (renderPresence == PresenceStatus_Present && ParseFinite(renderText, 0.0, &render) != ArenaStatus_Ok)))
		return FinalizationError(error, ArenaStatus_InvalidResult, "timing_fragment_row");
	if (WriteRootText(context->writer, context->runId, 0, error) != ArenaStatus_Ok ||
	    WriteRootText(context->writer, context->caseId, 1, error) != ArenaStatus_Ok ||
	    WriteRootText(context->writer, context->engineId, 2, error) != ArenaStatus_Ok ||
	    WriteRootUnsigned(context->writer, context->threadCount, 3, error) != ArenaStatus_Ok ||
	    WriteRootUnsigned(context->writer, context->repeatIndex, 4, error) != ArenaStatus_Ok ||
	    WriteRootText(context->writer, stepText, 5, error) != ArenaStatus_Ok ||
	    WriteRootText(context->writer, physicsText, 6, error) != ArenaStatus_Ok ||
	    WriteRootText(context->writer, renderText, 7, error) != ArenaStatus_Ok)
		return error->code;
	context->physicsTotal += physics;
	context->renderTotal += render;
	context->rowCount += 1;
	return ArenaStatus_Ok;
}

ArenaStatus UnitArtifactPath(const Catalog* catalog, const PreparedRunRequest* request, const RunPathRecord* paths,
                             std::uint32_t engineSelection, std::uint32_t threadSelection, std::uint32_t repeatIndex,
                             PresenceStatus timingFragment, std::array<wchar_t, kRunPathCapacity>* output,
                             StatusRecord* error)
{
	const std::string_view engineId =
	    CatalogTextView(catalog, catalog->engines[request->engineIndexes[engineSelection]].id);
	std::array<wchar_t, kIdentifierCapacity> wideEngineId = {};
	const int wideEngineSize =
	    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, engineId.data(), static_cast<int>(engineId.size()),
		                    wideEngineId.data(), static_cast<int>(wideEngineId.size() - 1));
	if (wideEngineSize <= 0)
		return FinalizationError(error, ArenaStatus_InvalidResult, "timing_engine_id_encoding");
	std::array<wchar_t, kRunPathCapacity> engineDirectory = {};
	std::array<wchar_t, kRunPathCapacity> threadDirectory = {};
	std::array<wchar_t, 64> name = {};
	std::array<wchar_t, 32> threadName = {};
	std::swprintf(threadName.data(), threadName.size(), L"t%u", request->threadCounts[threadSelection]);
	if (TimingPath(paths->rawDirectory.data(), wideEngineId.data(), &engineDirectory, error) != ArenaStatus_Ok ||
	    TimingPath(engineDirectory.data(), threadName.data(), &threadDirectory, error) != ArenaStatus_Ok)
		return error->code;
	if (timingFragment == PresenceStatus_Present)
		std::swprintf(name.data(), name.size(), L"%.*S_t%u_r%u_step-timing.csv", static_cast<int>(engineId.size()),
		              engineId.data(), request->threadCounts[threadSelection], repeatIndex);
	else
		std::swprintf(name.data(), name.size(), L"%.*S_t%u_raw.csv", static_cast<int>(engineId.size()), engineId.data(),
		              request->threadCounts[threadSelection]);
	return TimingPath(threadDirectory.data(), name.data(), output, error);
}

ArenaStatus CountTimingFragments(const wchar_t* directory, std::uint32_t depth, std::uint32_t* count,
                                 StatusRecord* error)
{
	if (depth > 8)
		return FinalizationError(error, ArenaStatus_InvalidResult, "timing_fragment_depth");
	std::array<wchar_t, kRunPathCapacity> pattern = {};
	if (TimingPath(directory, L"*", &pattern, error) != ArenaStatus_Ok)
		return error->code;
	WIN32_FIND_DATAW data = {};
	HANDLE search = FindFirstFileW(pattern.data(), &data);
	if (search == INVALID_HANDLE_VALUE)
		return FinalizationError(error, ArenaStatus_InvalidResult, "timing_fragment_directory");
	ArenaStatus status = ArenaStatus_Ok;
	do
	{
		const std::wstring_view name(data.cFileName);
		if (name == L"." || name == L"..")
			continue;
		if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		{
			std::array<wchar_t, kRunPathCapacity> child = {};
			if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
			    TimingPath(directory, data.cFileName, &child, error) != ArenaStatus_Ok ||
			    CountTimingFragments(child.data(), depth + 1, count, error) != ArenaStatus_Ok)
			{
				status = error->code != ArenaStatus_Ok
				             ? error->code
				             : FinalizationError(error, ArenaStatus_InvalidResult, "timing_fragment_directory");
				break;
			}
		}
		else if (name.find(L"_step-timing.csv") != std::wstring_view::npos)
			*count += 1;
	} while (FindNextFileW(search, &data) != 0);
	FindClose(search);
	return status;
}

ArenaStatus MergeTimingFragments(const Catalog* catalog, const PreparedRunRequest* request, const RunPathRecord* paths,
                                 StatusRecord* error)
{
	if (request->configuration.benchmarkCase.fixtureKind == CaseFixtureKind_RagdollStairTumble)
	{
		std::uint32_t fragmentCount = 0;
		if (CountTimingFragments(paths->rawDirectory.data(), 0, &fragmentCount, error) != ArenaStatus_Ok)
			return error->code;
		return fragmentCount == 0 ? ArenaStatus_Ok
		                          : FinalizationError(error, ArenaStatus_InvalidResult, "quality_run_contains_timing");
	}
	ResultManifestRecord manifest = {};
	if (LoadResultManifest(paths->manifestPath.data(), catalog, &manifest, error) != ArenaStatus_Ok)
		return error->code;
	const std::uint64_t expectedFragments =
	    static_cast<std::uint64_t>(request->engineCount) * request->threadCount * request->repeatCount;
	std::uint32_t fragmentCount = 0;
	if (expectedFragments > UINT32_MAX ||
	    CountTimingFragments(paths->rawDirectory.data(), 0, &fragmentCount, error) != ArenaStatus_Ok ||
	    fragmentCount > expectedFragments)
		return error->code != ArenaStatus_Ok
		           ? error->code
				   : FinalizationError(error, ArenaStatus_InvalidResult, "timing_fragment_cardinality");
	std::array<wchar_t, kRunPathCapacity> temporary = {};
	std::array<wchar_t, kRunPathCapacity> final = {};
	if (TimingPath(paths->resultDirectory.data(), L"step-timing.csv.tmp", &temporary, error) != ArenaStatus_Ok ||
	    TimingPath(paths->resultDirectory.data(), L"step-timing.csv", &final, error) != ArenaStatus_Ok)
		return error->code;
	DeleteFileW(temporary.data());
	CsvWriter writer = {};
	if (OpenCsvWriter(temporary.data(), &writer, error) != ArenaStatus_Ok)
		return error->code;
	ArenaStatus status = ArenaStatus_Ok;
	for (std::uint32_t field = 0; field < kRootTimingColumns.size() && status == ArenaStatus_Ok; ++field)
		status = WriteRootText(&writer, kRootTimingColumns[field], field, error);
	const TimingRenderSeries renderSeries = TimingRenderSeries_Absent;
	const std::uint32_t measuredWorkUnitCount = request->configuration.benchmarkCase.measuredWorkUnitCount;
	for (std::uint32_t engine = 0; engine < request->engineCount && status == ArenaStatus_Ok; ++engine)
		for (std::uint32_t thread = 0; thread < request->threadCount && status == ArenaStatus_Ok; ++thread)
		{
			std::array<wchar_t, kRunPathCapacity> rawPath = {};
			if (TimingPath(paths->resultDirectory.data(), L"normalized.csv", &rawPath, error) !=
			    ArenaStatus_Ok)
			{
				status = error->code;
				break;
			}
			RawTimingTotals totals = {};
			totals.renderSeries = renderSeries;
			totals.engineId = CatalogTextView(catalog, catalog->engines[request->engineIndexes[engine]].id);
			totals.threadCount = request->threadCounts[thread];
			CsvHeader rawHeader = {};
			CsvReadRecord rawRead = {};
			if (ReadCsvFile(rawPath.data(), &rawHeader, ConsumeRawTimingTotal, &totals, &rawRead, error) !=
			        ArenaStatus_Ok)
			{
				status = error->code != ArenaStatus_Ok
				             ? error->code
				             : FinalizationError(error, ArenaStatus_InvalidResult, "timing_raw_cardinality");
				break;
			}
			for (std::uint32_t repeat = 0; repeat < request->repeatCount && status == ArenaStatus_Ok; ++repeat)
			{
				if (totals.seen[repeat] != PresenceStatus_Present)
					continue;
				std::array<wchar_t, kRunPathCapacity> fragmentPath = {};
				if (UnitArtifactPath(catalog, request, paths, engine, thread, repeat, PresenceStatus_Present,
				                     &fragmentPath, error) != ArenaStatus_Ok)
				{
					status = error->code;
					break;
				}
				FragmentMergeContext context = {};
				context.writer = &writer;
				context.runId = std::string_view(paths->runId.data(), paths->runIdSize);
				context.caseId =
				    RunConfigurationTextView(&request->configuration, request->configuration.benchmarkCase.id);
				context.engineId = CatalogTextView(catalog, catalog->engines[request->engineIndexes[engine]].id);
				context.threadCount = request->threadCounts[thread];
				context.repeatIndex = repeat;
				context.measuredWorkUnitCount = measuredWorkUnitCount;
				context.renderSeries = renderSeries;
				if (FindExecutionFailure(manifest.executionFailures, request->engineIndexes[engine], request->threadCounts[thread], repeat) != nullptr)
				{
					CsvWriter sink = {};
					if (OpenCsvWriter(L"NUL", &sink, error) != ArenaStatus_Ok)
					{
						status = error->code;
						break;
					}
					FragmentMergeContext partial = context;
					partial.writer = &sink;
					CsvHeader partialHeader = {};
					CsvReadRecord partialRead = {};
					const ArenaStatus partialStatus = ReadCsvFile(fragmentPath.data(), &partialHeader, ConsumeTimingFragment, &partial, &partialRead, error);
					DestroyCsvWriter(&sink);
					if (partialStatus == ArenaStatus_RunFailed)
					{
						status = partialStatus;
						break;
					}
					if (partialStatus != ArenaStatus_Ok || partial.rowCount != measuredWorkUnitCount ||
					    std::abs(partial.physicsTotal - totals.physics[repeat]) > std::max(0.000001, (measuredWorkUnitCount + 1) * 0.0000000005))
					{
						*error = {};
						continue;
					}
				}
				CsvHeader fragmentHeader = {};
				CsvReadRecord fragmentRead = {};
				status = ReadCsvFile(fragmentPath.data(), &fragmentHeader, ConsumeTimingFragment, &context,
				                     &fragmentRead, error);
				const double tolerance = std::max(0.000001, (measuredWorkUnitCount + 1) * 0.0000000005);
				if (status == ArenaStatus_Ok && (context.rowCount != measuredWorkUnitCount ||
				                                 std::abs(context.physicsTotal - totals.physics[repeat]) > tolerance ||
				                                 (renderSeries != TimingRenderSeries_Absent &&
				                                  std::abs(context.renderTotal - totals.render[repeat]) > tolerance)))
					status =
					    FinalizationError(error, ArenaStatus_InvalidResult, "timing_fragment_total_or_cardinality");
			}
		}
	if (status == ArenaStatus_Ok)
		status = FinishCsvWriter(&writer, error);
	else
		DestroyCsvWriter(&writer);
	if (status == ArenaStatus_Ok &&
	    MoveFileExW(temporary.data(), final.data(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
		status = FinalizationError(error, ArenaStatus_RunFailed, "timing_root_publish");
	if (status != ArenaStatus_Ok)
		DeleteFileW(temporary.data());
	return status;
}

void RollbackTimingArtifacts(const RunPathRecord* paths, const wchar_t* timingPath, const wchar_t* temporaryTimingPath)
{
	DeleteFileW(timingPath);
	DeleteFileW(temporaryTimingPath);
	std::array<wchar_t, kRunPathCapacity> timingSvg = {};
	StatusRecord ignored = {};
	if (TimingPath(paths->resultDirectory.data(), L"step-timing.svg", &timingSvg, &ignored) == ArenaStatus_Ok)
		DeleteFileW(timingSvg.data());
}
} // namespace

ArenaStatus FinalizeFreshRunArtifacts(const wchar_t* repositoryRoot, const Catalog* catalog,
                                      const ReleaseCatalog* releaseCatalog, const HostRecord* host,
                                      const PreparedRunRequest* request, const RunPathRecord* paths,
                                      RunFinalizationWorkspace* workspace, RunFinalizationRecord* record,
                                      StatusRecord* error)
{
	if (workspace != nullptr)
		{
			std::destroy_at(workspace);
			std::construct_at(workspace);
		}
	if (record != nullptr)
		*record = {};
	if (error != nullptr)
		*error = {};
	if (repositoryRoot == nullptr || catalog == nullptr || releaseCatalog == nullptr || host == nullptr ||
	    request == nullptr || paths == nullptr || workspace == nullptr || record == nullptr || error == nullptr)
		return error != nullptr ? FinalizationError(error, ArenaStatus_InvalidArgument, "run_finalization_argument")
		                        : ArenaStatus_InvalidArgument;
	ArenaStatus status =
	    FinalizeRunResults(repositoryRoot, catalog, releaseCatalog, request, paths, &record->results, error);
	if (status == ArenaStatus_Ok)
		status = MergeRayTracingResults(catalog, request, paths, error);
	if (status == ArenaStatus_Ok)
		status = MergeTimingFragments(catalog, request, paths, error);
	if (status == ArenaStatus_Ok)
		status = RegenerateResultReports(repositoryRoot, paths->resultDirectory.data(), catalog, &workspace->manifest,
		                                 &workspace->model, &workspace->timingScratch, &workspace->indexes,
		                                 &record->reports, error);
	if (status != ArenaStatus_Ok && benchmark_stack::TargetFixture(request->configuration.execution.fixtureKind) == 0)
	{
		std::array<wchar_t, kRunPathCapacity> timingPath = {};
		std::array<wchar_t, kRunPathCapacity> temporaryTimingPath = {};
		StatusRecord ignored = {};
		if (TimingPath(paths->resultDirectory.data(), L"step-timing.csv", &timingPath, &ignored) == ArenaStatus_Ok &&
		    TimingPath(paths->resultDirectory.data(), L"step-timing.csv.tmp", &temporaryTimingPath, &ignored) ==
		        ArenaStatus_Ok)
			RollbackTimingArtifacts(paths, timingPath.data(), temporaryTimingPath.data());
	}
	return status;
}

} // namespace physics_arena
