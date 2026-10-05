#include "report_pipeline_internal.h"

#include "physics_arena/result_pipeline.h"
#include "physics_arena/ray_tracing_results.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace physics_arena
{
namespace
{
ArenaStatus WriteRayDetails(ReportWriter* writer, const wchar_t* directory, const ResultViewModel* model, StatusRecord* error)
{
	if (ResultViewTextView(model, model->workUnitId) != "ray_frame") return ArenaStatus_Ok;
	std::vector<RayResultRow> rows;
	std::vector<RayProbeRow> probes;
	if (ReadRayTracingResults(std::filesystem::path(directory) / "ray-tracing.csv", &rows, error) != ArenaStatus_Ok ||
	    ReadRayCapabilities(std::filesystem::path(directory) / "ray-capabilities.csv", &probes, error) != ArenaStatus_Ok) return error->code;
	ArenaStatus status = WriteReport(writer, "\n## Ray phases\n\nThe headline measures ordinary coherent primary closest rays only. Each phase below uses its own query count. All repeats are pooled for each engine, thread count, phase and API. Query median and p95 describe samples across views and repeats. Repeat ranges include only repeats with all configured measured suite identities. Terminal counts describe execution separately from measured numerical quality\n\nHit/miss percentages use the phase query denominator, with zero-query proportions unavailable. Update + query is the median of each sample's summed costs. Capability probes are outside heavy timings. Native batch denotes the public API, without a SIMD claim\n\nSetup, pose publication, conditioning, validation and buffer bytes are retained in [ray-tracing.csv](ray-tracing.csv). Peak process committed bytes, including native world and adapter storage, are in [ray-process.csv](ray-process.csv), an empty value means unavailable\n\n| Engine | Threads | Phase | API | Status | Samples | Queries | Query median ms | p95 ms | Update median ms | Update + query median ms | Rays/s | Repeats | Repeat median range ms | Repeat rays/s range | Hit % | Miss % | Errors | Failed execution repeats | Not-run repeats | Partial / missing repeats |\n| --- | ---: | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | --- |\n", error);
	for (std::uint32_t engine = 0; status == ArenaStatus_Ok && engine < model->engineCount; ++engine)
		for (std::uint32_t thread = 0; status == ArenaStatus_Ok && thread < model->threadCount; ++thread)
			for (std::uint32_t phase = 0; status == ArenaStatus_Ok && phase < benchmark_ray::Phase_Count; ++phase)
				for (benchmark_ray::Api api : {benchmark_ray::Api_Ordinary, benchmark_ray::Api_NativeBatch})
				{
					const std::string id(ResultViewTextView(model, model->engines[engine].id));
					const RayPhaseStatistics statistics = SummarizeRayPhase(rows, id, model->threadCounts[thread], static_cast<benchmark_ray::Phase>(phase), api, model->repeatCount, model->measuredWorkUnitCount, model->executionFailures, model->engines[engine].catalogEngineIndex);
					if (statistics.samples != 0)
					{
						status = WriteFormat(writer, error, "| %s | %u | %s | %s | %s | %u/%u | %llu | %.6f | %.6f | %.6f | %.6f | %.3f | %u/%u | ", id.c_str(), model->threadCounts[thread], benchmark_ray::PhaseName(static_cast<benchmark_ray::Phase>(phase)), benchmark_ray::ApiName(api), RayPhaseOutcomeText(statistics), statistics.samples, statistics.requestedSamples, static_cast<unsigned long long>(statistics.queries), statistics.medianMs, statistics.p95Ms, statistics.updateMedianMs, statistics.combinedMedianMs, statistics.raysPerSecond, statistics.repeats, statistics.requestedRepeats);
						if (status == ArenaStatus_Ok)
							status = statistics.repeats == 0 ? WriteReport(writer, "unavailable | unavailable | ", error) :
							    WriteFormat(writer, error, "%.6f to %.6f | %.3f to %.3f | ", statistics.repeatMedianMinMs,
							        statistics.repeatMedianMaxMs, statistics.repeatRaysPerSecondMin, statistics.repeatRaysPerSecondMax);
						if (status == ArenaStatus_Ok)
							status = statistics.proportions == PresenceStatus_Present ? WriteFormat(writer, error, "%.3f | %.3f | %llu |", statistics.hitPercent, statistics.missPercent, static_cast<unsigned long long>(statistics.errors)) : WriteFormat(writer, error, "unavailable | unavailable | %llu |", static_cast<unsigned long long>(statistics.errors));
					}
					else status = WriteFormat(writer, error, "| %s | %u | %s | %s | %s | 0/%u | unavailable | unavailable | unavailable | unavailable | unavailable | unavailable | 0/%u | unavailable | unavailable | unavailable | unavailable | unavailable |", id.c_str(), model->threadCounts[thread], benchmark_ray::PhaseName(static_cast<benchmark_ray::Phase>(phase)), benchmark_ray::ApiName(api), RayPhaseOutcomeText(statistics), statistics.requestedSamples, statistics.requestedRepeats);
					if (status == ArenaStatus_Ok)
						status = WriteFormat(writer, error, " %u | %u | %u / %u |\n", statistics.executionFailedRepeats,
						    statistics.notRunRepeats, statistics.partialRepeats, statistics.missingRepeats);
				}
	if (status == ArenaStatus_Ok) status = WriteReport(writer, "\n## Capability probes\n\nFull view and repeat outcomes are retained in [ray-capabilities.csv](ray-capabilities.csv). Native semantic differences remain visible and do not alter the common heavy workload\n\n| Engine | Threads | Repeat | View | Probe | Status | Mismatches | Native errors | Overflow reports |\n| --- | ---: | ---: | ---: | --- | --- | ---: | ---: | ---: |\n", error);
	for (const RayProbeRow& probe : probes)
	{
		if (status != ArenaStatus_Ok) break;
		status = WriteFormat(writer, error, "| %s | %u | %u | %u | %s | %s | %u | %u | %u |\n", probe.engine.data(), probe.threads, probe.repeat, probe.view, benchmark_ray::ProbeName(probe.probe), RayCapabilityName(probe.capability), probe.mismatches, probe.nativeErrors, probe.overflows);
	}
	return status;
}

ArenaStatus AddIndexRecord(const wchar_t* manifestPath, const Catalog* catalog, std::string_view caseDirectory,
                           std::string_view cpuDirectory, std::string_view runDirectory,
                           std::array<ResultIndexRecord, kResultIndexCapacity>* records, std::uint32_t* count,
                           StatusRecord* error)
{
	if (*count >= records->size())
		return ReportError(error, ArenaStatus_InvalidResult, "result_index_capacity");
	ResultManifestRecord manifest = {};
	if (LoadResultManifest(manifestPath, catalog, &manifest, error) != ArenaStatus_Ok)
	{
		*error = {};
		return ArenaStatus_Ok;
	}
	const ArenaStatus status = BuildResultIndexRecord(catalog, manifest, caseDirectory, cpuDirectory, runDirectory,
	                                                  &(*records)[*count], error);
	if (status == ArenaStatus_Ok)
		*count += 1;
	return status;
}

ArenaStatus DiscoverIndexRecords(const wchar_t* repositoryRoot, const Catalog* catalog,
                                 std::array<ResultIndexRecord, kResultIndexCapacity>* records, std::uint32_t* count,
                                 StatusRecord* error)
{
	constexpr std::string_view resultsPrefix = "results/";
	for (std::uint32_t reportIndex = 0; reportIndex < catalog->reportCount; ++reportIndex)
	{
		const std::string_view source = CatalogTextView(catalog, catalog->reports[reportIndex].resultSource);
		if (!source.starts_with(resultsPrefix) || source.starts_with("results/local/"))
			return ReportError(error, ArenaStatus_InvalidResult, "result_index_report_path");
		const std::string_view relative = source.substr(resultsPrefix.size());
		const std::size_t caseEnd = relative.find('/');
		const std::size_t cpuEnd =
		    caseEnd == std::string_view::npos ? std::string_view::npos : relative.find('/', caseEnd + 1);
		if (caseEnd == std::string_view::npos || caseEnd == 0 || cpuEnd == std::string_view::npos ||
		    cpuEnd == caseEnd + 1 || cpuEnd + 1 >= relative.size() ||
		    relative.find('/', cpuEnd + 1) != std::string_view::npos)
			return ReportError(error, ArenaStatus_InvalidResult, "result_index_report_path");
		const std::string_view caseDirectory = relative.substr(0, caseEnd);
		const std::string_view cpuDirectory = relative.substr(caseEnd + 1, cpuEnd - caseEnd - 1);
		const std::string_view runDirectory = relative.substr(cpuEnd + 1);
		std::array<wchar_t, kReportPathCapacity> runPath = {};
		if (CopyWidePath(&runPath, repositoryRoot) != ArenaStatus_Ok)
			return ReportError(error, ArenaStatus_InvalidResult, "result_index_root_path");
		std::uint32_t runPathSize = static_cast<std::uint32_t>(std::wcslen(runPath.data()));
		if (AppendUtf8Path(&runPath, &runPathSize, source, error) != ArenaStatus_Ok)
			return error->code;
		PresenceStatus completed = PresenceStatus_Present;
		for (const wchar_t* required :
		     {L"manifest.json", L"normalized.csv", L"summary.csv", L"summary.svg", L"report.md"})
		{
			std::array<wchar_t, kReportPathCapacity> requiredPath = runPath;
			if (AppendWideChild(&requiredPath, required) != ArenaStatus_Ok ||
			    GetFileAttributesW(requiredPath.data()) == INVALID_FILE_ATTRIBUTES)
			{
				completed = PresenceStatus_Absent;
				break;
			}
		}
		if (completed == PresenceStatus_Absent)
			continue;
		std::array<wchar_t, kReportPathCapacity> manifestPath = runPath;
		if (AppendWideChild(&manifestPath, L"manifest.json") != ArenaStatus_Ok)
			return ReportError(error, ArenaStatus_InvalidResult, "result_index_manifest_path");
		const ArenaStatus addStatus = AddIndexRecord(manifestPath.data(), catalog, caseDirectory, cpuDirectory,
		                                             runDirectory, records, count, error);
		if (addStatus != ArenaStatus_Ok)
			return addStatus;
	}
	std::sort(records->begin(), records->begin() + *count,
	          [](const ResultIndexRecord& left, const ResultIndexRecord& right)
	          {
		          if (CellView(left.caseSlug) != CellView(right.caseSlug))
			          return CellView(left.caseSlug) < CellView(right.caseSlug);
		          if (CellView(left.cpuSlug) != CellView(right.cpuSlug))
			          return CellView(left.cpuSlug) < CellView(right.cpuSlug);
		          return CellView(left.runSlug) < CellView(right.runSlug);
	          });
	return ArenaStatus_Ok;
}

ArenaStatus IndexCells(const ResultIndexRecord& record, std::uint32_t kind, std::array<ReportCell, 6>* cells)
{
	*cells = {};
	ReportCell chart = {};
	ReportCell report = {};
	if (kind == 0)
	{
		FormatCell(&chart, "[summary.svg](%.*s/%.*s/%.*s/summary.svg)", static_cast<int>(record.caseSlug.size),
		           record.caseSlug.data.data(), static_cast<int>(record.cpuSlug.size), record.cpuSlug.data.data(),
		           static_cast<int>(record.runSlug.size), record.runSlug.data.data());
		FormatCell(&report, "[report](%.*s/%.*s/%.*s/report.md)", static_cast<int>(record.caseSlug.size),
		           record.caseSlug.data.data(), static_cast<int>(record.cpuSlug.size), record.cpuSlug.data.data(),
		           static_cast<int>(record.runSlug.size), record.runSlug.data.data());
		SetCell(&(*cells)[0], CellView(record.caseName));
		SetCell(&(*cells)[1], CellView(record.cpuName));
		SetCell(&(*cells)[2], CellView(record.runSlug));
		(*cells)[3] = chart;
		(*cells)[4] = report;
	}
	else if (kind == 1)
	{
		FormatCell(&chart, "[summary.svg](%.*s/%.*s/summary.svg)", static_cast<int>(record.cpuSlug.size),
		           record.cpuSlug.data.data(), static_cast<int>(record.runSlug.size), record.runSlug.data.data());
		FormatCell(&report, "[report](%.*s/%.*s/report.md)", static_cast<int>(record.cpuSlug.size),
		           record.cpuSlug.data.data(), static_cast<int>(record.runSlug.size), record.runSlug.data.data());
		SetCell(&(*cells)[0], CellView(record.cpuName));
		SetCell(&(*cells)[1], CellView(record.runSlug));
		(*cells)[2] = chart;
		(*cells)[3] = report;
	}
	else
	{
		FormatCell(&chart, "[summary.svg](%.*s/summary.svg)", static_cast<int>(record.runSlug.size),
		           record.runSlug.data.data());
		FormatCell(&report, "[report](%.*s/report.md)", static_cast<int>(record.runSlug.size),
		           record.runSlug.data.data());
		ReportCell threads = {};
		for (std::uint32_t index = 0; index < record.threadCount; ++index)
		{
			if (index != 0)
				AppendCellText(&threads, ", ");
			AppendCellFormat(&threads, "%u", record.threadCounts[index]);
		}
		SetCell(&(*cells)[0], CellView(record.runSlug));
		SetCell(&(*cells)[1], CellView(record.hostLabel));
		FormatCell(&(*cells)[2], "%u", record.repeatCount);
		(*cells)[3] = threads;
		(*cells)[4] = chart;
		(*cells)[5] = report;
	}
	return ArenaStatus_Ok;
}

ArenaStatus WriteIndexFile(const wchar_t* directory, std::string_view title, std::string_view intro,
                           const std::array<ResultIndexRecord, kResultIndexCapacity>& records,
                           std::uint32_t recordCount, std::uint32_t kind, std::string_view caseFilter,
                           std::string_view cpuFilter, ReportPipelineRecord* pipelineRecord, StatusRecord* error)
{
	std::array<wchar_t, kReportPathCapacity> temporaryPath = {};
	ReportWriter writer = {};
	if (OpenAtomicReport(directory, L"README.md.tmp", &writer, &temporaryPath, error) != ArenaStatus_Ok)
		return error->code;
	ArenaStatus status = WriteFormat(&writer, error, "# %.*s\n\n%.*s\n\n", static_cast<int>(title.size()), title.data(),
	                                 static_cast<int>(intro.size()), intro.data());
	const std::array<std::array<std::string_view, 6>, 3> names = {
	    {{"Case", "CPU", "Run", "Chart", "Report", ""},
		 {"CPU", "Run", "Chart", "Report", "", ""},
		 {"Run", "Host", "Repeats", "Threads", "Chart", "Report"}}};
	const std::array<std::uint32_t, 3> columnCounts = {5, 4, 6};
	const std::uint32_t columnCount = columnCounts[kind];
	std::array<ReportCell, 6> headers = {};
	std::array<std::uint32_t, 6> widths = {};
	for (std::uint32_t column = 0; column < columnCount; ++column)
	{
		SetCell(&headers[column], names[kind][column]);
		widths[column] = headers[column].size;
	}
	std::uint32_t selectedCount = 0;
	std::array<ReportCell, 6> cells = {};
	for (std::uint32_t index = 0; index < recordCount; ++index)
	{
		if (!caseFilter.empty() && CellView(records[index].caseSlug) != caseFilter)
			continue;
		if (!cpuFilter.empty() && CellView(records[index].cpuSlug) != cpuFilter)
			continue;
		IndexCells(records[index], kind, &cells);
		++selectedCount;
		for (std::uint32_t column = 0; column < columnCount; ++column)
			widths[column] = std::max(widths[column], cells[column].size);
	}
	if (selectedCount == 0)
	{
		const std::array<std::string_view, 3> emptyText = {"No supported release benchmark is currently published",
		                                                   "No runs are available for this case yet",
		                                                   "No runs are available for this CPU yet"};
		status = status == ArenaStatus_Ok
		             ? WriteFormat(&writer, error, "%.*s\n", static_cast<int>(emptyText[kind].size()),
		                           emptyText[kind].data())
		             : status;
	}
	else
	{
		std::array<PresenceStatus, 6> alignment = {};
		if (kind == 2)
			alignment[2] = PresenceStatus_Present;
		if (status == ArenaStatus_Ok)
			status = WriteMarkdownRow(&writer, headers.data(), widths.data(), alignment.data(), columnCount, error);
		if (status == ArenaStatus_Ok)
			status = WriteMarkdownSeparator(&writer, widths.data(), alignment.data(), columnCount, error);
		for (std::uint32_t index = 0; status == ArenaStatus_Ok && index < recordCount; ++index)
		{
			if (!caseFilter.empty() && CellView(records[index].caseSlug) != caseFilter)
				continue;
			if (!cpuFilter.empty() && CellView(records[index].cpuSlug) != cpuFilter)
				continue;
			IndexCells(records[index], kind, &cells);
			status = WriteMarkdownRow(&writer, cells.data(), widths.data(), alignment.data(), columnCount, error);
		}
	}
	if (status == ArenaStatus_Ok)
		status = WriteReport(&writer, "\n", error);
	if (status != ArenaStatus_Ok)
	{
		CloseHandle(writer.handle);
		DeleteFileW(temporaryPath.data());
		return status;
	}
	status = CommitAtomicReport(directory, L"README.md", &writer, temporaryPath, error);
	if (status == ArenaStatus_Ok)
		pipelineRecord->indexFileCount += 1;
	return status;
}

std::uint32_t MarkdownThreadOrdinal(const ResultViewModel* model, std::uint32_t threadCount)
{
	std::uint32_t ordinal = 0;
	while (ordinal < model->threadCount && model->threadCounts[ordinal] != threadCount)
		++ordinal;
	return ordinal;
}

ArenaStatus FormatObservationAggregate(const ResultObservationView& observation, const ObservationAggregate& aggregate,
                                       ReportCell* cell)
{
	if (observation.role == ObservationRole_ValidityZero || observation.role == ObservationRole_ValidityExact)
		return observation.valueType == ObservationValueType_Uint64
		           ? FormatCell(cell, "%llu / %llu",
				                static_cast<unsigned long long>(aggregate.actualTotal.unsignedValue),
				                static_cast<unsigned long long>(aggregate.expectedTotal.unsignedValue))
				   : FormatCell(cell, "%.6g / %.6g", aggregate.actualTotal.float64Value,
				                aggregate.expectedTotal.float64Value);
	if (aggregate.sampleCount == 1)
		return observation.valueType == ObservationValueType_Uint64
		           ? FormatCell(cell, "%llu", static_cast<unsigned long long>(aggregate.minimumActual.unsignedValue))
				   : FormatCell(cell, "%.6g", aggregate.minimumActual.float64Value);
	return observation.valueType == ObservationValueType_Uint64
	           ? FormatCell(cell, "%llu - %llu", static_cast<unsigned long long>(aggregate.minimumActual.unsignedValue),
			                static_cast<unsigned long long>(aggregate.maximumActual.unsignedValue))
			   : FormatCell(cell, "%.6g - %.6g", aggregate.minimumActual.float64Value,
			                aggregate.maximumActual.float64Value);
}

ArenaStatus FormatCaseDataRow(const ResultViewModel* model, std::uint32_t group, const ResultSummaryViewRow& row,
                              std::array<ReportCell, kObservationPerCaseCapacity + 3>* cells,
                              std::uint32_t* columnCount)
{
	*cells = {};
	*columnCount = 0;
	if (SetCell(&(*cells)[(*columnCount)++],
	            ResultViewTextView(model, model->engines[row.engineOrdinal].provenanceLabel)) != ArenaStatus_Ok ||
	    FormatCell(&(*cells)[(*columnCount)++], "%u", row.threadCount) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	const std::uint32_t threadOrdinal = MarkdownThreadOrdinal(model, row.threadCount);
	ObservationOutcome outcome = ObservationOutcome_Ok;
	for (std::uint32_t observation = 0; observation < model->observationCount; ++observation)
	{
		const ResultObservationView& view = model->observationViews[observation];
		if (view.resultGroupOrdinal != group)
			continue;
		const ObservationAggregate* aggregate =
		    ObservationAggregateAt(&model->observations, row.engineOrdinal, threadOrdinal, observation);
		if (aggregate == nullptr ||
		    FormatObservationAggregate(view, *aggregate, &(*cells)[(*columnCount)++]) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		if (aggregate->outcome == ObservationOutcome_Failed)
			outcome = ObservationOutcome_Failed;
	}
	return SetCell(&(*cells)[(*columnCount)++], ObservationOutcomeText(outcome));
}

ArenaStatus WriteCaseDataTables(ReportWriter* writer, const ResultViewModel* model, StatusRecord* error)
{
	for (std::uint32_t group = 0; group < model->resultGroupCount; ++group)
	{
		const std::string_view groupLabel = ResultViewTextView(model, model->resultGroups[group].label);
		if (WriteFormat(writer, error, "\n## %.*s\n\n", static_cast<int>(groupLabel.size()), groupLabel.data()) !=
		    ArenaStatus_Ok)
			return error->code;
		std::array<ReportCell, kObservationPerCaseCapacity + 3> headers = {};
		std::uint32_t columnCount = 0;
		SetCell(&headers[columnCount++], "Engine");
		SetCell(&headers[columnCount++], "Threads");
		for (std::uint32_t observation = 0; observation < model->observationCount; ++observation)
			if (model->observationViews[observation].resultGroupOrdinal == group)
				SetCell(&headers[columnCount++], ResultViewTextView(model, model->observationViews[observation].label));
		SetCell(&headers[columnCount++], "Status");
		std::array<std::uint32_t, kObservationPerCaseCapacity + 3> widths = {};
		std::array<PresenceStatus, kObservationPerCaseCapacity + 3> alignment = {};
		for (std::uint32_t column = 0; column < columnCount; ++column)
		{
			widths[column] = headers[column].size;
			alignment[column] =
			    column != 0 && column + 1 != columnCount ? PresenceStatus_Present : PresenceStatus_Absent;
		}
		std::array<ReportCell, kObservationPerCaseCapacity + 3> cells = {};
		for (std::uint32_t rowIndex = 0; rowIndex < model->displayRowCount; ++rowIndex)
		{
			std::uint32_t formattedCount = 0;
			if (FormatCaseDataRow(model, group, model->summaryRows[model->displayRowIndexes[rowIndex]], &cells,
			                      &formattedCount) != ArenaStatus_Ok ||
			    formattedCount != columnCount)
				return ReportError(error, ArenaStatus_InvalidResult, "markdown_case_data_format");
			for (std::uint32_t column = 0; column < columnCount; ++column)
				widths[column] = std::max(widths[column], cells[column].size);
		}
		if (WriteMarkdownRow(writer, headers.data(), widths.data(), alignment.data(), columnCount, error) !=
		        ArenaStatus_Ok ||
		    WriteMarkdownSeparator(writer, widths.data(), alignment.data(), columnCount, error) != ArenaStatus_Ok)
			return error->code;
		for (std::uint32_t rowIndex = 0; rowIndex < model->displayRowCount; ++rowIndex)
		{
			std::uint32_t formattedCount = 0;
			if (FormatCaseDataRow(model, group, model->summaryRows[model->displayRowIndexes[rowIndex]], &cells,
			                      &formattedCount) != ArenaStatus_Ok ||
			    WriteMarkdownRow(writer, cells.data(), widths.data(), alignment.data(), columnCount, error) !=
			        ArenaStatus_Ok)
				return error->code;
		}
	}
	return ArenaStatus_Ok;
}
} // namespace

ArenaStatus BuildResultIndexRecord(const Catalog* catalog, const ResultManifestRecord& manifest,
                                   std::string_view caseDirectory, std::string_view cpuDirectory,
                                   std::string_view runDirectory, ResultIndexRecord* record, StatusRecord* error)
{
	std::uint32_t caseIndex = 0;
	while (caseIndex < catalog->caseCount &&
	       CatalogTextView(catalog, catalog->cases[caseIndex].id) != ResultTextView(&manifest, manifest.caseId))
		++caseIndex;
	const EffectiveRunConfiguration* configuration = ResultConfiguration(&manifest);
	if (caseIndex == catalog->caseCount && configuration == nullptr)
		return ReportError(error, ArenaStatus_InvalidResult, "result_index_case");
	const CaseRecord& benchmarkCase =
	    configuration != nullptr ? configuration->benchmarkCase : catalog->cases[caseIndex];
	*record = {};
	ResultIndexRecord& output = *record;
	ReportCell host = {};
	std::array<ReportCell, 3> hostLines = {};
	std::uint32_t hostLineCount = 0;
	if (SetCell(&output.caseSlug, CaseConfigurationTextView(catalog, configuration, benchmarkCase.slug)) !=
	        ArenaStatus_Ok ||
	    SetCell(&output.caseName, CaseConfigurationTextView(catalog, configuration, benchmarkCase.displayName)) !=
	        ArenaStatus_Ok ||
	    SetCell(&output.cpuSlug, cpuDirectory) != ArenaStatus_Ok ||
	    SetCell(&output.runSlug, runDirectory) != ArenaStatus_Ok ||
	    SetCell(&output.cpuName, HostTextView(&manifest.host, manifest.host.cpuModel)) != ArenaStatus_Ok ||
	    BuildHostLabel(&manifest.host, &host, PresenceStatus_Absent, &hostLines, &hostLineCount) != ArenaStatus_Ok ||
	    SetCell(&output.hostLabel, CellView(host)) != ArenaStatus_Ok)
		return ReportError(error, ArenaStatus_InvalidResult, "result_index_text_capacity");
	if (CellView(output.caseSlug) != caseDirectory)
		return ReportError(error, ArenaStatus_InvalidResult, "result_index_case_directory");
	output.repeatCount = manifest.repeatCount;
	output.engineCount = manifest.engineCount;
	output.threadCount = manifest.threadCount;
	std::copy(manifest.threadCounts.begin(), manifest.threadCounts.begin() + manifest.threadCount,
	          output.threadCounts.begin());
	return ArenaStatus_Ok;
}

ArenaStatus DiscoverResultIndex(const wchar_t* repositoryRoot, const Catalog* catalog, ResultIndexWorkspace* workspace,
                                StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (repositoryRoot == nullptr || catalog == nullptr || workspace == nullptr || error == nullptr)
		return error != nullptr ? ReportError(error, ArenaStatus_InvalidArgument, "result_discovery_argument")
		                        : ArenaStatus_InvalidArgument;
	*workspace = {};
	return DiscoverIndexRecords(repositoryRoot, catalog, &workspace->records, &workspace->recordCount, error);
}

ArenaStatus WriteOutcomeMarkdownText(ReportWriter* writer, std::string_view text, StatusRecord* error)
{
	std::size_t start = 0;
	for (std::size_t index = 0; index < text.size(); ++index)
	{
		if (std::string_view("|\\*_`[]~").find(text[index]) == std::string_view::npos) continue;
		if (WriteXmlText(writer, text.substr(start, index - start), error) != ArenaStatus_Ok ||
		    WriteFormat(writer, error, "&#%u;", static_cast<unsigned char>(text[index])) != ArenaStatus_Ok)
			return error->code;
		start = index + 1;
	}
	return WriteXmlText(writer, text.substr(start), error);
}

ArenaStatus WriteOutcomeMarkdown(ReportWriter* writer, const ResultViewModel* model,
                                 const ReportOutcomeInput& input, StatusRecord* error)
{
	std::uint32_t count = static_cast<std::uint32_t>(input.tupleLines.size());
	for (const ReportCell& line : input.failureLines) count += line.size != 0 ? 1U : 0U;
	if (count == 0) return ArenaStatus_Ok;
	ArenaStatus status = WriteReport(writer,
	    "\n## Assessment diagnostics\n\n<details>\n<summary>Show saved assessment details</summary>\n\n"
	    "Physical rows identify the determining saved repeat for each engine/thread configuration. "
	    "In diagnostic identities, t is the thread count, r is the zero-based repeat index, and s is the capture segment. "
	    "Execution diagnostics are in [manifest.json](manifest.json)", error);
	if (status == ArenaStatus_Ok && model->stabilityRequired == PresenceStatus_Present)
		status = WriteReport(writer, ", all recorded physical metrics are in [stability.csv](stability.csv)", error);
	if (status == ArenaStatus_Ok)
		status = WriteReport(writer, "\n\n| Engine | Threads | Diagnostic |\n| --- | ---: | --- |\n", error);
	for (std::uint32_t ordinal = 0; status == ArenaStatus_Ok && ordinal < model->engineCount; ++ordinal)
	{
		if (input.failureLines[ordinal].size == 0) continue;
		const std::string_view name = ResultViewTextView(model, model->engines[ordinal].displayName);
		status = WriteReport(writer, "| ", error);
		if (status == ArenaStatus_Ok) status = WriteOutcomeMarkdownText(writer, name, error);
		if (status == ArenaStatus_Ok) status = WriteReport(writer, " | all | ", error);
		if (status == ArenaStatus_Ok) status = WriteOutcomeMarkdownText(writer, CellView(input.failureLines[ordinal]), error);
		if (status == ArenaStatus_Ok) status = WriteReport(writer, " |\n", error);
	}
	for (const ReportOutcomeLine& line : input.tupleLines)
	{
		if (status != ArenaStatus_Ok) break;
		const ResultSummaryViewRow& row = model->summaryRows[line.rowIndex];
		const std::string_view name = ResultViewTextView(model, model->engines[row.engineOrdinal].displayName);
		status = WriteReport(writer, "| ", error);
		if (status == ArenaStatus_Ok) status = WriteOutcomeMarkdownText(writer, name, error);
		if (status == ArenaStatus_Ok) status = WriteFormat(writer, error, " | %u | ", row.threadCount);
		if (status == ArenaStatus_Ok) status = WriteOutcomeMarkdownText(writer, CellView(line.text), error);
		if (status == ArenaStatus_Ok) status = WriteReport(writer, " |\n", error);
	}
	if (status == ArenaStatus_Ok) status = WriteReport(writer, "\n</details>\n", error);
	return status;
}

ArenaStatus WriteMarkdownReport(const wchar_t* repositoryRoot, const wchar_t* resultDirectory, const Catalog* catalog,
                                const ResultViewModel* model, ReportPipelineRecord* record, StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (repositoryRoot == nullptr || resultDirectory == nullptr || catalog == nullptr || model == nullptr ||
	    record == nullptr || error == nullptr || model->summaryRowCount == 0)
		return error != nullptr ? ReportError(error, ArenaStatus_InvalidArgument, "markdown_argument")
		                        : ArenaStatus_InvalidArgument;
	ReportOutcomeInput outcomes = {};
	ReportOutcomePresentation outcomePresentation = {};
	if (LoadReportOutcomes(resultDirectory, model, &outcomes, error) != ArenaStatus_Ok ||
	    BuildReportOutcomePresentation(model, outcomes, UINT32_MAX, 0, &outcomePresentation, error) != ArenaStatus_Ok)
		return error->code;
	std::array<char, kReportPathCapacity> relative = {};
	const std::size_t rootSize = std::wcslen(repositoryRoot);
	const wchar_t* relativeWide = resultDirectory;
	if (_wcsnicmp(repositoryRoot, resultDirectory, rootSize) == 0)
	{
		relativeWide += rootSize;
		while (*relativeWide == L'/' || *relativeWide == L'\\')
			++relativeWide;
	}
	const int relativeSize = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, relativeWide, -1, relative.data(),
	                                             static_cast<int>(relative.size()), nullptr, nullptr);
	if (relativeSize <= 1)
		return ReportError(error, ArenaStatus_InvalidResult, "markdown_relative_path");
	for (int index = 0; index < relativeSize - 1; ++index)
		if (relative[static_cast<std::size_t>(index)] == '\\')
			relative[static_cast<std::size_t>(index)] = '/';
	std::array<EvidenceRow, kEvidenceRowCapacity> evidence = {};
	std::uint32_t evidenceCount = 0;
	ReportCell host = {};
	std::array<ReportCell, 3> hostLines = {};
	std::uint32_t hostLineCount = 0;
	ReportCell threads = {};
	std::array<std::uint32_t, kEngineCapacity> physicsAppearance = {};
	const std::uint32_t physicsAppearanceCount = FirstAppearanceEngines(model, &physicsAppearance);
	if (BuildHostLabel(&model->host, &host, PresenceStatus_Absent, &hostLines, &hostLineCount) != ArenaStatus_Ok ||
	    BuildThreadCounts(model, &threads) != ArenaStatus_Ok || physicsAppearanceCount == 0)
		return ReportError(error, ArenaStatus_InvalidResult, "markdown_metadata_capacity");
	ReportCell value = {};
	FormatCell(&value, "`%.*s`", static_cast<int>(ResultViewTextView(model, model->runId).size()),
	           ResultViewTextView(model, model->runId).data());
	AddEvidence(&evidence, &evidenceCount, "Selected run id", CellView(value));
	FormatCell(&value, "`%s/`", relative.data());
	AddEvidence(&evidence, &evidenceCount, "Result folder", CellView(value));
	FormatCell(&value, "`%.*s`", static_cast<int>(ResultViewTextView(model, model->caseId).size()),
	           ResultViewTextView(model, model->caseId).data());
	AddEvidence(&evidence, &evidenceCount, "Case id", CellView(value));
	FormatCell(&value, "`%.*s`", static_cast<int>(ResultViewTextView(model, model->runProvenance).size()),
	           ResultViewTextView(model, model->runProvenance).data());
	AddEvidence(&evidence, &evidenceCount, "Benchmark mode", CellView(value));
	if (model->renderResolutionPresence == PresenceStatus_Present)
	{
		FormatCell(&value, "`%u x %u px`", model->renderWidthPixels, model->renderHeightPixels);
		AddEvidence(&evidence, &evidenceCount, "Resolution", CellView(value));
	}
	FormatCell(&value, "`%.*s`", static_cast<int>(threads.size), threads.data.data());
	AddEvidence(&evidence, &evidenceCount, "Thread counts", CellView(value));
	ReportCell measuredLabel = {};
	ReportCell warmupLabel = {};
	AppendCellText(&measuredLabel, "Measured ");
	AppendCellText(&measuredLabel, ResultViewTextView(model, model->workUnitLabel));
	if (model->measuredWorkUnitCount != 1)
		AppendCellText(&measuredLabel, ResultViewTextView(model, model->workUnitLabel).ends_with("batch") ? "es" : "s");
	AppendCellText(&warmupLabel, "Warmup ");
	AppendCellText(&warmupLabel, ResultViewTextView(model, model->workUnitLabel));
	if (model->warmupWorkUnitCount != 1)
		AppendCellText(&warmupLabel, ResultViewTextView(model, model->workUnitLabel).ends_with("batch") ? "es" : "s");
	FormatCell(&value, "`%u`", model->measuredWorkUnitCount);
	AddEvidence(&evidence, &evidenceCount, CellView(measuredLabel), CellView(value));
	FormatCell(&value, "`%u`", model->warmupWorkUnitCount);
	AddEvidence(&evidence, &evidenceCount, CellView(warmupLabel), CellView(value));
	FormatCell(&value, "`%u`", model->bodyCount);
	AddEvidence(&evidence, &evidenceCount, "Bodies", CellView(value));
	FormatCell(&value, "`%u`", model->shapeCount);
	AddEvidence(&evidence, &evidenceCount, "Shapes", CellView(value));
	FormatCell(&value, "`%u`", model->queryCount);
	AddEvidence(&evidence, &evidenceCount, "Queries", CellView(value));
	FormatCell(&value, "`%u`", model->constraintCount);
	AddEvidence(&evidence, &evidenceCount, "Constraints", CellView(value));
	if (model->timestepHz != 0)
	{
		FormatCell(&value, "`%u Hz`", model->timestepHz);
		AddEvidence(&evidence, &evidenceCount, "Timestep", CellView(value));
	}
	AddEvidence(&evidence, &evidenceCount, "Host", CellView(host));
	std::array<PresenceStatus, kEngineCapacity> emitted = {};
	for (std::uint32_t rowIndex = 0; rowIndex < model->summaryRowCount; ++rowIndex)
	{
		const std::uint32_t engineOrdinal = model->summaryRows[rowIndex].engineOrdinal;
		if (emitted[engineOrdinal] == PresenceStatus_Present)
			continue;
		emitted[engineOrdinal] = PresenceStatus_Present;
		ReportCell build = {};
		if (BuildBuildLabel(model, model->engines[engineOrdinal], &build) != ArenaStatus_Ok)
			return ReportError(error, ArenaStatus_InvalidResult, "markdown_build_label");
		ReportCell buildValue = {};
		if (AppendCellText(&buildValue, ResultViewTextView(model, model->engines[engineOrdinal].displayName)) !=
		        ArenaStatus_Ok ||
		    AppendCellText(&buildValue, ": ") != ArenaStatus_Ok ||
		    AppendCellText(&buildValue, CellView(build)) != ArenaStatus_Ok ||
		    AddEvidence(&evidence, &evidenceCount, rowIndex == 0 ? "Build settings" : "", CellView(buildValue)) !=
		        ArenaStatus_Ok)
			return ReportError(error, ArenaStatus_InvalidResult, "markdown_build_capacity");
	}
	AddEvidence(&evidence, &evidenceCount, "Data source", "`summary.csv` generated from `normalized.csv`");
	AddEvidence(&evidence, &evidenceCount, "Chart", "`summary.svg`");
	if (model->timing.availability == PresenceStatus_Present)
	{
		AddEvidence(&evidence, &evidenceCount, "Work-unit timing data", "`step-timing.csv`");
		AddEvidence(&evidence, &evidenceCount, "Work-unit timing chart", "`step-timing.svg`");
	}
	if (model->observations.availability == PresenceStatus_Present)
		AddEvidence(&evidence, &evidenceCount, "Case observation data", "`observations.csv`");
	AddEvidence(&evidence, &evidenceCount, "Run route", ResultViewTextView(model, model->hostRoute));
	std::array<ReportCell, 2> evidenceHeaders = {};
	SetCell(&evidenceHeaders[0], "Field");
	SetCell(&evidenceHeaders[1], "Value");
	std::array<std::uint32_t, 2> evidenceWidths = {evidenceHeaders[0].size, evidenceHeaders[1].size};
	for (std::uint32_t index = 0; index < evidenceCount; ++index)
	{
		evidenceWidths[0] = std::max(evidenceWidths[0], evidence[index].field.size);
		evidenceWidths[1] = std::max(evidenceWidths[1], evidence[index].value.size);
	}
	ReportCaseMeta caseMeta = {};
	if (BuildReportCaseMeta(catalog, model, model->summaryRows[0], &caseMeta, error) != ArenaStatus_Ok)
		return error->code;
	PhysicsMetaProjection physics = {};
	const std::size_t factCapacity =
	    PhysicsFactCapacity(model, std::span<std::uint32_t>(physicsAppearance).first(physicsAppearanceCount));
	physics.facts = {static_cast<PhysicsFact*>(_alloca(factCapacity * sizeof(PhysicsFact))), factCapacity};
	if (BuildPhysicsMeta(catalog, model, physicsAppearance, physicsAppearanceCount, 0, caseMeta, &physics) !=
	    ArenaStatus_Ok)
		return ReportError(error, ArenaStatus_InvalidResult, "markdown_physics_capacity");
	const std::array<PresenceStatus, 2> evidenceAlignment = {PresenceStatus_Absent, PresenceStatus_Absent};
	std::array<wchar_t, kReportPathCapacity> temporaryPath = {};
	ReportWriter writer = {};
	if (OpenAtomicReport(resultDirectory, L"report.md.tmp", &writer, &temporaryPath, error) != ArenaStatus_Ok)
		return error->code;
	ArenaStatus status = WriteFormat(&writer, error, "# %.*s Benchmark Report\n\n%.*s\n\n## Evidence\n\n",
	                                 static_cast<int>(ResultViewTextView(model, model->caseDisplayName).size()),
	                                 ResultViewTextView(model, model->caseDisplayName).data(),
	                                 static_cast<int>(caseMeta.description.size), caseMeta.description.data.data());
	if (status == ArenaStatus_Ok && outcomePresentation.height != 0)
	{
		status = WriteFormat(&writer, error, "**%.*s**\n\n%.*s\n\n", static_cast<int>(outcomePresentation.headline.size),
		    outcomePresentation.headline.data.data(), static_cast<int>(outcomePresentation.criterion.size),
		    outcomePresentation.criterion.data.data());
	}
	if (status == ArenaStatus_Ok && model->verificationMode == VerificationMode_Off)
		status = WriteReport(&writer, "Physical observations not collected for stack, Wall and Ragdoll quality checks. Completed repeats are unverified\n\n", error);
	if (status == ArenaStatus_Ok)
		status = WriteMarkdownRow(&writer, evidenceHeaders.data(), evidenceWidths.data(), evidenceAlignment.data(), 2,
		                          error);
	if (status == ArenaStatus_Ok)
		status = WriteMarkdownSeparator(&writer, evidenceWidths.data(), evidenceAlignment.data(), 2, error);
	for (std::uint32_t index = 0; status == ArenaStatus_Ok && index < evidenceCount; ++index)
	{
		const std::array<ReportCell, 2> cells = {evidence[index].field, evidence[index].value};
		status = WriteMarkdownRow(&writer, cells.data(), evidenceWidths.data(), evidenceAlignment.data(), 2, error);
	}
	if (status == ArenaStatus_Ok) status = WritePhysicsMarkdown(&writer, model, physics, error);
	if (model->measurementMode == ResultMeasurementMode_PhysicalQuality && model->verificationMode == VerificationMode_On)
	{
		if (status == ArenaStatus_Ok)
			status = WriteReport(
			    &writer,
			    "\n## Physical quality\n\nJoint sampling runs after each completed simulation step, and coverage below reports valid samples. "
			    "RMS and maximum anchor gaps are measured in meters over the repeat, excluding warmup. "
			    "Gap magnitude has no pass tolerance. Status evaluates state validity and sample coverage only. "
			    "No execution speed was measured\n\nJoint IDs are zero based: ragdoll index = ID / 14, "
			    "link index = ID % 14. Steps are one based. First invalid step 0 means none, "
			    "and its body ID is then unused. Ranges below span repeats, so pair worst-joint "
			    "and step identities within the same repeat in observations.csv or the Case data detail view\n",
			    error);
	}
	else if (model->measurementMode == ResultMeasurementMode_PhysicalQuality)
	{
		if (status == ArenaStatus_Ok)
			status = WriteReport(&writer, "\n## Completed workload\n\nPhysical observations not collected. No execution speed was measured\n\n| Engine | Threads | Completed unverified repeats |\n| --- | ---: | ---: |\n", error);
		for (std::uint32_t index = 0; status == ArenaStatus_Ok && index < model->displayRowCount; ++index)
		{
			const ResultSummaryViewRow& row = model->summaryRows[model->displayRowIndexes[index]];
			const std::string_view engine = ResultViewTextView(model, model->engines[row.engineOrdinal].provenanceLabel);
			status = WriteFormat(&writer, error, "| %.*s | %u | %u |\n", static_cast<int>(engine.size()), engine.data(), row.threadCount, row.repeatCount);
		}
	}
	else
	{
		if (status == ArenaStatus_Ok)
		{
			status = WriteFormat(
			    &writer, error,
			    "\n## Result Summary\n\nMain metric: median %.*s, %.*s. Rows are grouped by thread count and sorted best to worst within each thread group\n\n",
			    static_cast<int>(ResultViewTextView(model, model->chartLabel).size()),
			    ResultViewTextView(model, model->chartLabel).data(),
			    static_cast<int>(ResultViewTextView(model, model->chartNote).size()),
			    ResultViewTextView(model, model->chartNote).data());
		}
		std::array<ReportCell, 10> summaryHeaders = {};
		const std::uint32_t summaryColumnCount = 10;
		SetCell(&summaryHeaders[0], "Engine");
		SetCell(&summaryHeaders[1], "Threads");
		AppendCellText(&summaryHeaders[2], "Median ");
		AppendCellText(&summaryHeaders[2], ResultViewTextView(model, model->primaryMetricId));
		AppendCellText(&summaryHeaders[2], " (");
		AppendCellText(&summaryHeaders[2], ResultViewTextView(model, model->primaryMetricUnit));
		AppendCellText(&summaryHeaders[2], ")");
		SetCell(&summaryHeaders[3], "Minimum");
		SetCell(&summaryHeaders[4], "Maximum");
		SetCell(&summaryHeaders[5], "Repeats");
		SetCell(&summaryHeaders[6], "Bodies");
		SetCell(&summaryHeaders[7], "Shapes");
		SetCell(&summaryHeaders[8], "Queries");
		SetCell(&summaryHeaders[9], "Constraints");
		std::array<std::uint32_t, 10> summaryWidths = {};
		for (std::uint32_t column = 0; column < summaryColumnCount; ++column)
		{
			summaryWidths[column] = summaryHeaders[column].size;
		}
		std::array<ReportCell, 10> summaryCells = {};
		for (std::uint32_t index = 0; index < model->displayRowCount; ++index)
		{
			FormatSummaryCells(model, &model->summaryRows[model->displayRowIndexes[index]], &summaryCells);
			for (std::uint32_t column = 0; column < summaryColumnCount; ++column)
				summaryWidths[column] = std::max(summaryWidths[column], summaryCells[column].size);
		}
		const std::array<PresenceStatus, 10> summaryAlignment = {
		    PresenceStatus_Absent,  PresenceStatus_Present, PresenceStatus_Present, PresenceStatus_Present,
		    PresenceStatus_Present, PresenceStatus_Present, PresenceStatus_Present, PresenceStatus_Present,
		    PresenceStatus_Present, PresenceStatus_Present};
		if (status == ArenaStatus_Ok)
			status = WriteMarkdownRow(&writer, summaryHeaders.data(), summaryWidths.data(), summaryAlignment.data(),
			                          summaryColumnCount, error);
		if (status == ArenaStatus_Ok)
			status = WriteMarkdownSeparator(&writer, summaryWidths.data(), summaryAlignment.data(), summaryColumnCount,
			                                error);
		std::uint32_t priorThread = 0;
		for (std::uint32_t index = 0; status == ArenaStatus_Ok && index < model->displayRowCount; ++index)
		{
			const ResultSummaryViewRow& row = model->summaryRows[model->displayRowIndexes[index]];
			if (priorThread != 0 && row.threadCount != priorThread)
			{
				summaryCells = {};
				status = WriteMarkdownRow(&writer, summaryCells.data(), summaryWidths.data(), summaryAlignment.data(),
				                          summaryColumnCount, error);
			}
			priorThread = row.threadCount;
			if (status == ArenaStatus_Ok && FormatSummaryCells(model, &row, &summaryCells) != ArenaStatus_Ok)
				status = ReportError(error, ArenaStatus_InvalidResult, "markdown_summary_format");
			if (status == ArenaStatus_Ok)
				status = WriteMarkdownRow(&writer, summaryCells.data(), summaryWidths.data(), summaryAlignment.data(),
				                          summaryColumnCount, error);
		}
	}
	if (status == ArenaStatus_Ok) status = WriteOutcomeMarkdown(&writer, model, outcomes, error);
	if (status == ArenaStatus_Ok && !model->executionFailures.empty())
	{
		status = WriteReport(&writer, "\n## Execution outcomes\n\nCompleted with failures. Costs describe completed measurements only, unavailable measurements are not zeroes. Original tuple identities and diagnostics are retained in [manifest.json](manifest.json)\n\n| Engine | Threads | Repeat | Stage | Outcome | Reason | Exit code |\n| --- | ---: | ---: | --- | --- | --- | ---: |\n", error);
		for (const ExecutionFailure& failure : model->executionFailures)
		{
			if (status != ArenaStatus_Ok)
				break;
			const std::string_view engine = CatalogTextView(catalog, catalog->engines[failure.engineIndex].id);
			status = WriteFormat(&writer, error, "| %.*s | %u | %u | %s | %s | %s | ", static_cast<int>(engine.size()), engine.data(), failure.threadCount, failure.repeatIndex + 1,
			    failure.stage == ExecutionStage_Preflight ? "preflight" : "benchmark", failure.outcome == ExecutionOutcome_NotRun ? "not_run" : "failed", ExecutionFailureReasonName(failure.reason));
			if (status == ArenaStatus_Ok)
				status = failure.exitCodePresence == PresenceStatus_Present ? WriteFormat(&writer, error, "%d |\n", failure.exitCode) : WriteReport(&writer, "unavailable |\n", error);
		}
	}
	if (status == ArenaStatus_Ok) status = WriteRayDetails(&writer, resultDirectory, model, error);
	if (status == ArenaStatus_Ok && model->resultGroupCount != 0)
		status = WriteCaseDataTables(&writer, model, error);
	if (status == ArenaStatus_Ok)
		status = WriteReport(
		    &writer,
		    model->measurementMode == ResultMeasurementMode_PhysicalQuality
		        ? "\n## Route Notes\n\nRun coverage is recorded in `normalized.csv` and `summary.csv`."
		        : "\n## Route Notes\n\nThe Result Summary table comes from `summary.csv`, aggregated from `normalized.csv`.",
		    error);
	if (status == ArenaStatus_Ok && model->resultGroupCount != 0)
		status = WriteReport(&writer, " Case Data tables are generated from `observations.csv`.", error);
	if (status == ArenaStatus_Ok)
		status = WriteReport(
		    &writer, " Open `PhysicsArena.exe report <result-dir>` and choose **Regenerate report** to refresh it\n", error);
	if (status != ArenaStatus_Ok)
	{
		CloseHandle(writer.handle);
		DeleteFileW(temporaryPath.data());
		return status;
	}
	status = CommitAtomicReport(resultDirectory, L"report.md", &writer, temporaryPath, error);
	if (status == ArenaStatus_Ok)
		record->markdownBytes = writer.totalBytes;
	return status;
}

ArenaStatus WriteResultIndexes(const wchar_t* repositoryRoot, const Catalog* catalog, ResultIndexWorkspace* workspace,
                               ReportPipelineRecord* record, StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (repositoryRoot == nullptr || catalog == nullptr || workspace == nullptr || record == nullptr ||
	    error == nullptr)
		return error != nullptr ? ReportError(error, ArenaStatus_InvalidArgument, "result_indexes_argument")
		                        : ArenaStatus_InvalidArgument;
	*workspace = {};
	if (DiscoverIndexRecords(repositoryRoot, catalog, &workspace->records, &workspace->recordCount, error) !=
	    ArenaStatus_Ok)
		return error->code;
	const std::uint32_t recordCount = workspace->recordCount;
	const std::array<ResultIndexRecord, kResultIndexCapacity>& records = workspace->records;
	std::array<wchar_t, kReportPathCapacity> resultsRoot = {};
	if (CopyWidePath(&resultsRoot, repositoryRoot) != ArenaStatus_Ok ||
	    AppendWideChild(&resultsRoot, L"results") != ArenaStatus_Ok)
		return ReportError(error, ArenaStatus_InvalidResult, "result_indexes_root");
	ArenaStatus status = WriteIndexFile(
	    resultsRoot.data(), "Benchmark Results",
	    "Published results are grouped by benchmark case, CPU model, and run state. Each run folder contains the manifest, normalized CSV, summary CSV, and SVG chart for that run",
	    records, recordCount, 0, {}, {}, record, error);
	for (std::uint32_t caseIndex = 0; status == ArenaStatus_Ok && caseIndex < recordCount; ++caseIndex)
	{
		if (caseIndex != 0 && CellView(records[caseIndex].caseSlug) == CellView(records[caseIndex - 1].caseSlug))
			continue;
		std::array<wchar_t, kReportPathCapacity> caseDirectory = resultsRoot;
		std::array<wchar_t, kReportPathCapacity> cpuDirectory = {};
		std::uint32_t size = static_cast<std::uint32_t>(std::wcslen(caseDirectory.data()));
		if (AppendUtf8Path(&caseDirectory, &size, CellView(records[caseIndex].caseSlug), error) != ArenaStatus_Ok)
			return error->code;
		status = WriteIndexFile(caseDirectory.data(), CellView(records[caseIndex].caseName),
		                        "Runs for this benchmark case, grouped by CPU model", records, recordCount, 1,
		                        CellView(records[caseIndex].caseSlug), {}, record, error);
		for (std::uint32_t cpuIndex = caseIndex; status == ArenaStatus_Ok && cpuIndex < recordCount; ++cpuIndex)
		{
			if (CellView(records[cpuIndex].caseSlug) != CellView(records[caseIndex].caseSlug))
				break;
			if (cpuIndex != caseIndex && CellView(records[cpuIndex].cpuSlug) == CellView(records[cpuIndex - 1].cpuSlug))
				continue;
			cpuDirectory = caseDirectory;
			size = static_cast<std::uint32_t>(std::wcslen(cpuDirectory.data()));
			if (AppendUtf8Path(&cpuDirectory, &size, CellView(records[cpuIndex].cpuSlug), error) != ArenaStatus_Ok)
				return error->code;
			status =
			    WriteIndexFile(cpuDirectory.data(), CellView(records[cpuIndex].cpuName), "Runs for this CPU model",
				               records, recordCount, 2, CellView(records[caseIndex].caseSlug),
				               CellView(records[cpuIndex].cpuSlug), record, error);
		}
	}
	return status;
}

} // namespace physics_arena
