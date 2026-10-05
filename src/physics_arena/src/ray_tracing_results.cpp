#include "physics_arena/ray_tracing_results.h"
#include "physics_arena/csv_io.h"
#include "physics_arena/run.h"
#include "physics_arena/release_contracts.h"
#include <iomanip>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>

#include <charconv>
#include <cmath>

namespace physics_arena
{
namespace
{
constexpr std::array<std::string_view, 19> kColumns = {
    "engine_id", "thread_count", "repeat_index", "view",     "suite",       "phase",           "api",
    "status",    "queries",      "hit_rays",     "query_ms", "update_ms",   "conditioning_ms", "validation_ms",
    "errors",    "written",      "setup_ms",     "suite_ms", "buffer_bytes"};
struct ReadContext
{
	std::vector<RayResultRow>* rows;
};
ArenaStatus Invalid(StatusRecord* error, std::string_view detail)
{
	*error = {};
	error->code = ArenaStatus_InvalidResult;
	constexpr std::string_view component = "ray_tracing_results", status = "invalid_result";
	std::copy(component.begin(), component.end(), error->component.begin());
	error->componentSize = static_cast<std::uint32_t>(component.size());
	std::copy(status.begin(), status.end(), error->status.begin());
	error->statusSize = static_cast<std::uint32_t>(status.size());
	std::copy(detail.begin(), detail.end(), error->detail.begin());
	error->detailSize = static_cast<std::uint32_t>(detail.size());
	return error->code;
}
template <typename T> int Number(std::string_view text, T* value)
{
	const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), *value);
	return !text.empty() && parsed.ec == std::errc() && parsed.ptr == text.data() + text.size();
}
ArenaStatus ReadRow(const CsvHeader* header, const CsvRow* input, void* context, StatusRecord* error)
{
	if (header->fieldCount != kColumns.size() || input->fieldCount != kColumns.size())
		return Invalid(error, "column_count");
	std::array<std::string_view, 19> fields = {};
	for (std::size_t index = 0; index < fields.size(); ++index)
	{
		if (CsvHeaderTextView(header, header->fields[index]) != kColumns[index])
			return Invalid(error, "column_name");
		fields[index] = CsvRowTextView(input, input->fields[index]);
	}
	RayResultRow row = {};
	if (fields[0] != "entasis" && fields[0] != "box3d")
		return Invalid(error, "engine_scope");
	std::copy(fields[0].begin(), fields[0].end(), row.engine.begin());
	if (!Number(fields[1], &row.threads) || !Number(fields[2], &row.repeat) || !Number(fields[3], &row.view) ||
	    !Number(fields[4], &row.suite) || row.threads == 0 || row.threads > 256 || row.view >= 6 || row.suite >= 1000000)
		return Invalid(error, "tuple_or_view");
	std::uint32_t phase = 0;
	while (phase < benchmark_ray::Phase_Count &&
	       fields[5] != benchmark_ray::PhaseName(static_cast<benchmark_ray::Phase>(phase)))
		++phase;
	if (phase == benchmark_ray::Phase_Count)
		return Invalid(error, "phase");
	row.phase = static_cast<benchmark_ray::Phase>(phase);
	if (fields[6] == "ordinary")
		row.api = benchmark_ray::Api_Ordinary;
	else if (fields[6] == "native-batch")
		row.api = benchmark_ray::Api_NativeBatch;
	else
		return Invalid(error, "api");
	if (fields[7] == "supported")
		row.capability = RayCapability_Supported;
	else if (fields[7] == "unsupported")
		row.capability = RayCapability_Unsupported;
	else if (fields[7] == "native_semantics_differ")
		row.capability = RayCapability_NativeSemanticsDiffer;
	else if (fields[7] == "failed")
		row.capability = RayCapability_Failed;
	else if (fields[7] == "execution_failed")
		row.capability = RayCapability_ExecutionFailed;
	else if (fields[7] == "not_run")
		row.capability = RayCapability_NotRun;
	else
		return Invalid(error, "capability");
	if (row.capability == RayCapability_Unsupported)
	{
		for (std::size_t index = 8; index < fields.size(); ++index)
			if (!fields[index].empty())
				return Invalid(error, "unsupported_has_measurement");
	}
	else
	{
		const int partial = row.capability == RayCapability_ExecutionFailed || row.capability == RayCapability_NotRun;
		for (std::size_t index = 8; index < fields.size(); ++index)
			if (!fields[index].empty())
				row.observedFields |= static_cast<std::uint16_t>(1u << (index - 8));
		if (!Number(fields[8], &row.queries) ||
		    (!(partial != 0 && fields[9].empty()) && !Number(fields[9], &row.hitRays)) ||
		    (!(partial != 0 && fields[10].empty()) && !Number(fields[10], &row.queryMs)) ||
		    (!(partial != 0 && fields[11].empty()) && !Number(fields[11], &row.updateMs)) ||
		    (!(partial != 0 && fields[12].empty()) && !Number(fields[12], &row.conditioningMs)) ||
		    (!(partial != 0 && fields[13].empty()) && !Number(fields[13], &row.validationMs)) ||
		    (!(partial != 0 && fields[14].empty()) && !Number(fields[14], &row.errors)) ||
		    (!(partial != 0 && fields[15].empty()) && !Number(fields[15], &row.written)) ||
		    (!(partial != 0 && fields[16].empty()) && !Number(fields[16], &row.setupMs)) ||
		    (!(partial != 0 && fields[17].empty()) && !Number(fields[17], &row.suiteMs)) ||
		    (!(partial != 0 && fields[18].empty()) && !Number(fields[18], &row.bufferBytes)))
			return Invalid(error, "numeric_field");
		if (row.capability == RayCapability_NotRun && row.observedFields != 1)
			return Invalid(error, "not_run_has_measurement");
		for (double value : {row.queryMs, row.updateMs, row.conditioningMs, row.validationMs, row.setupMs, row.suiteMs})
			if (!std::isfinite(value) || value < 0)
				return Invalid(error, "nonfinite_or_negative_time");
		if (row.hitRays > row.queries || row.written > row.queries || row.bufferBytes > benchmark_ray::kActiveByteLimit)
			return Invalid(error, "workload_count");
		if (RayMeasurementComplete(row.capability) != 0 &&
		    (row.written != row.queries || (row.capability == RayCapability_Supported && row.errors != 0) ||
		     (row.capability == RayCapability_Failed && row.errors == 0) || (row.queries != 0 && row.queryMs <= 0)))
			return Invalid(error, "invalid_supported_result");
		if (row.capability == RayCapability_Unsupported &&
		    (row.queries != 0 || row.hitRays != 0 || row.written != 0 || row.queryMs != 0 || row.updateMs != 0))
			return Invalid(error, "unsupported_has_measurement");
	}
	ReadContext* reader = static_cast<ReadContext*>(context);
	if (reader->rows->size() >= 1000000)
		return Invalid(error, "row_capacity");
	reader->rows->push_back(row);
	return ArenaStatus_Ok;
}
ArenaStatus ReadMemoryRow(const CsvHeader* header, const CsvRow* input, void* context, StatusRecord* error)
{
	constexpr std::array<std::string_view, 4> columns = {"engine_id", "thread_count", "repeat_index",
	                                                     "peak_process_committed_bytes"};
	if (header->fieldCount != columns.size() || input->fieldCount != columns.size())
		return Invalid(error, "memory_columns");
	std::array<std::string_view, 4> fields = {};
	for (std::size_t index = 0; index < columns.size(); ++index)
	{
		if (CsvHeaderTextView(header, header->fields[index]) != columns[index])
			return Invalid(error, "memory_column_name");
		fields[index] = CsvRowTextView(input, input->fields[index]);
	}
	RayProcessMemory row = {};
	if ((fields[0] != "box3d" && fields[0] != "entasis") || !Number(fields[1], &row.threads) ||
	    !Number(fields[2], &row.repeat) || row.threads == 0 || row.threads > 256)
		return Invalid(error, "memory_identity");
	std::copy(fields[0].begin(), fields[0].end(), row.engine.begin());
	if (!fields[3].empty())
	{
		if (!Number(fields[3], &row.peakCommittedBytes) || row.peakCommittedBytes == 0)
			return Invalid(error, "memory_count");
		row.available = PresenceStatus_Present;
	}
	std::vector<RayProcessMemory>* rows = static_cast<std::vector<RayProcessMemory>*>(context);
	if (rows->size() >= 1000000)
		return Invalid(error, "memory_row_capacity");
	rows->push_back(row);
	return ArenaStatus_Ok;
}

constexpr std::array<std::string_view, 13> kProbeColumns = {
    "engine_id",     "thread_count",     "repeat_index",   "view",     "capability",
    "api",           "status",           "queries",        "hit_rays", "reference_mismatches",
    "native_errors", "overflow_reports", "world_colliders"};
ArenaStatus ReadProbeRow(const CsvHeader* header, const CsvRow* input, void* context, StatusRecord* error)
{
	if (header->fieldCount != kProbeColumns.size() || input->fieldCount != kProbeColumns.size())
		return Invalid(error, "probe_columns");
	std::array<std::string_view, 13> fields = {};
	for (std::size_t index = 0; index < fields.size(); ++index)
	{
		if (CsvHeaderTextView(header, header->fields[index]) != kProbeColumns[index])
			return Invalid(error, "probe_column_name");
		fields[index] = CsvRowTextView(input, input->fields[index]);
	}
	RayProbeRow row = {};
	if ((fields[0] != "entasis" && fields[0] != "box3d") || fields[5] != "ordinary")
		return Invalid(error, "probe_engine_api");
	std::copy(fields[0].begin(), fields[0].end(), row.engine.begin());
	if (!Number(fields[1], &row.threads) || !Number(fields[2], &row.repeat) || !Number(fields[3], &row.view) ||
	    !Number(fields[7], &row.queries) || !Number(fields[8], &row.hitRays) || !Number(fields[9], &row.mismatches) ||
	    !Number(fields[10], &row.nativeErrors) || !Number(fields[11], &row.overflows) ||
	    !Number(fields[12], &row.worldColliders) || row.threads == 0 || row.threads > 256 || row.view >= 6 ||
	    row.queries != 512 || row.hitRays > row.queries || row.mismatches > row.queries ||
	    row.nativeErrors > row.queries || row.overflows > row.queries || row.worldColliders == 0 ||
	    row.worldColliders > 128)
		return Invalid(error, "probe_counts");
	std::uint32_t probe = 0;
	while (probe < benchmark_ray::Probe_Count &&
	       fields[4] != benchmark_ray::ProbeName(static_cast<benchmark_ray::Probe>(probe)))
		++probe;
	if (probe == benchmark_ray::Probe_Count)
		return Invalid(error, "probe_name");
	row.probe = static_cast<benchmark_ray::Probe>(probe);
	std::uint32_t capability = 0;
	while (capability <= RayCapability_Failed && fields[6] != RayCapabilityName(static_cast<RayCapability>(capability)))
		++capability;
	if (capability > RayCapability_Failed)
		return Invalid(error, "probe_status");
	row.capability = static_cast<RayCapability>(capability);
	if (row.capability == RayCapability_Supported && (row.mismatches != 0 || row.nativeErrors != 0))
		return Invalid(error, "probe_supported_errors");
	std::vector<RayProbeRow>* rows = static_cast<std::vector<RayProbeRow>*>(context);
	if (rows->size() >= 1000000)
		return Invalid(error, "probe_row_limit");
	rows->push_back(row);
	return ArenaStatus_Ok;
}
}

int RayMeasurementComplete(RayCapability capability)
{
	return capability == RayCapability_Supported || capability == RayCapability_Failed;
}

const char* RayCapabilityName(RayCapability capability)
{
	constexpr std::array<const char*, 6> names = {"supported", "unsupported", "native_semantics_differ", "failed", "execution_failed", "not_run"};
	return capability <= RayCapability_NotRun ? names[capability] : "invalid";
}

ArenaStatus WriteRayProcessMemory(const std::filesystem::path& directory, std::string_view engine,
                                  std::uint32_t threads, std::uint32_t repeat, void* process, StatusRecord* error)
{
	PROCESS_MEMORY_COUNTERS counters = {};
	const int available =
	    process != nullptr && K32GetProcessMemoryInfo(static_cast<HANDLE>(process), &counters, sizeof(counters)) != 0;
	const std::filesystem::path path = directory / (std::string(engine) + "_t" + std::to_string(threads) + "_r" +
	                                                std::to_string(repeat) + "_ray-process.csv");
	std::ofstream output(path, std::ios::trunc);
	output << "engine_id,thread_count,repeat_index,peak_process_committed_bytes\n"
	       << engine << ',' << threads << ',' << repeat << ',';
	if (available != 0)
		output << counters.PeakPagefileUsage;
	output << '\n';
	output.close();
	return output ? ArenaStatus_Ok : Invalid(error, "ray_process_memory_write");
}

ArenaStatus ReadRayProcessMemory(const std::filesystem::path& path, std::vector<RayProcessMemory>* rows,
                                 StatusRecord* error)
{
	rows->clear();
	CsvHeader header = {};
	CsvReadRecord record = {};
	return ReadCsvFile(path.c_str(), &header, ReadMemoryRow, rows, &record, error);
}

ArenaStatus ReadRayCapabilities(const std::filesystem::path& path, std::vector<RayProbeRow>* rows, StatusRecord* error)
{
	rows->clear();
	CsvHeader header = {};
	CsvReadRecord record = {};
	return ReadCsvFile(path.c_str(), &header, ReadProbeRow, rows, &record, error);
}

ArenaStatus ReadRayTracingResults(const std::filesystem::path& path, std::vector<RayResultRow>* rows,
                                  StatusRecord* error)
{
	rows->clear();
	ReadContext context = {rows};
	CsvHeader header = {};
	CsvReadRecord record = {};
	return ReadCsvFile(path.c_str(), &header, ReadRow, &context, &record, error);
}

ArenaStatus ValidateRaySecondaryWorkload(const std::vector<RayResultRow>& rows, const std::filesystem::path& corpus,
                                         std::uint32_t viewCount, StatusRecord* error)
{
	using namespace benchmark_ray;
	std::array<std::array<std::uint32_t, Phase_Count>, kViewCount> counts = {};
	for (std::uint32_t view = 0; view < viewCount; ++view)
		for (Phase phase : {Phase_Shadow, Phase_Reflection, Phase_Ambient})
		{
			PhaseMetadata metadata = {};
			if (ReadPhaseMetadata(PhasePath(corpus, view, phase), &metadata) != Status_Ok || metadata.view != view ||
			    metadata.phase != phase)
				return Invalid(error, "secondary_corpus_metadata");
			counts[view][phase] = metadata.rays;
		}
	for (const RayResultRow& row : rows)
		if (RayMeasurementComplete(row.capability) != 0 &&
		    (row.phase == Phase_Shadow || row.phase == Phase_Reflection || row.phase == Phase_Ambient) &&
		    (row.view >= viewCount || row.queries != counts[row.view][row.phase]))
			return Invalid(error, "secondary_workload_mismatch");
	return ArenaStatus_Ok;
}

ArenaStatus ValidateRaySample(const RayResultRow& row, std::uint32_t queryCount, StatusRecord* error)
{
	using namespace benchmark_ray;
	if (row.capability == RayCapability_NativeSemanticsDiffer)
		return Invalid(error, "phase_semantics_status");
	if (RayMeasurementComplete(row.capability) == 0)
		return ArenaStatus_Ok;
	if ((row.phase == Phase_Primary || row.phase == Phase_Shuffled || row.phase == Phase_Updated) &&
	    row.queries != queryCount)
		return Invalid(error, "primary_count");
	if (row.phase == Phase_Filtered && row.queries != std::min(queryCount, 262144u))
		return Invalid(error, "filtered_count");
	if (IsEnumeration(row.phase) != 0 && row.queries != std::min(queryCount, 65536u))
		return Invalid(error, "enumeration_count");
	if (row.phase == Phase_Updated && row.updateMs <= 0)
		return Invalid(error, "update_publication_time");
	return ArenaStatus_Ok;
}

ArenaStatus ValidateRayTupleIdentities(const std::vector<RayResultRow>& rows, std::string_view engine,
                                       std::uint32_t threads, std::uint32_t repeat, const CaseExecutionSpec& execution, PresenceStatus requireComplete,
                                       StatusRecord* error)
{
	using namespace benchmark_ray;
	std::vector<std::array<std::array<std::uint32_t, Api_Count>, Phase_Count>> seen(execution.measuredWorkUnitCount);
	for (const RayResultRow& row : rows)
	{
		if (std::string_view(row.engine.data()) != engine || row.threads != threads || row.repeat != repeat ||
		    row.suite >= execution.measuredWorkUnitCount || row.view != row.suite % execution.rayTracing.viewCount || row.phase >= Phase_Count || row.api >= Api_Count ||
		    seen[row.suite][row.phase][row.api]++ != 0)
			return Invalid(error, "tuple_identity_or_duplicate");
	}
	if (requireComplete == PresenceStatus_Absent)
		return ArenaStatus_Ok;
	for (const std::array<std::array<std::uint32_t, Api_Count>, Phase_Count>& suite : seen)
		for (const std::array<std::uint32_t, Api_Count>& phase : suite)
			for (std::uint32_t count : phase)
				if (count != 1)
					return Invalid(error, "missing_phase");
	return ArenaStatus_Ok;
}

ArenaStatus ValidateRayTracingTuple(const std::vector<RayResultRow>& rows, std::string_view engine,
                                    std::uint32_t threads, std::uint32_t repeat, const CaseExecutionSpec& execution, StatusRecord* error, PresenceStatus requireComplete)
{
	if (ValidateRayTupleIdentities(rows, engine, threads, repeat, execution, requireComplete, error) != ArenaStatus_Ok)
		return error->code;
	for (const RayResultRow& row : rows)
		if (ValidateRaySample(row, execution.queryCount, error) != ArenaStatus_Ok)
			return error->code;
	return ArenaStatus_Ok;
}

ArenaStatus ValidateRayAuxiliaryTuple(const std::vector<RayProbeRow>& probes,
                                      const std::vector<RayProcessMemory>& memory, std::string_view engine,
                                      std::uint32_t threads, std::uint32_t repeat, StatusRecord* error, PresenceStatus requireComplete)
{
	if (requireComplete == PresenceStatus_Present && (memory.size() != 1 || std::string_view(memory[0].engine.data()) != engine || memory[0].threads != threads ||
	    memory[0].repeat != repeat))
		return Invalid(error, "memory_tuple");
	if (memory.size() > 1 || (!memory.empty() && (std::string_view(memory[0].engine.data()) != engine || memory[0].threads != threads || memory[0].repeat != repeat)))
		return Invalid(error, "memory_tuple");
	std::array<std::array<std::uint32_t, benchmark_ray::Probe_Count>, 6> seen = {};
	for (const RayProbeRow& row : probes)
	{
		if (std::string_view(row.engine.data()) != engine || row.threads != threads || row.repeat != repeat ||
		    row.view >= 6 || row.probe >= benchmark_ray::Probe_Count || seen[row.view][row.probe]++ != 0)
			return Invalid(error, "probe_tuple_failure");
	}
	if (requireComplete == PresenceStatus_Absent)
		return ArenaStatus_Ok;
	for (const std::array<std::uint32_t, benchmark_ray::Probe_Count>& view : seen)
		for (std::uint32_t count : view)
			if (count != 1)
				return Invalid(error, "probe_missing_row");
	return ArenaStatus_Ok;
}

ArenaStatus ValidateSavedRayResults(const std::filesystem::path& directory, const Catalog* catalog,
                                    const ResultManifestRecord* manifest, StatusRecord* error)
{
	if (manifest->configuration.execution.fixtureKind != CaseFixtureKind_RayTracing)
		return ArenaStatus_Ok;
	std::vector<RayResultRow> rows;
	std::vector<RayProbeRow> probes;
	std::vector<RayProcessMemory> memory;
	if (ReadRayTracingResults(directory / "ray-tracing.csv", &rows, error) != ArenaStatus_Ok ||
	    ValidateRaySecondaryWorkload(rows, directory / "ray-corpus", manifest->configuration.execution.rayTracing.viewCount, error) != ArenaStatus_Ok ||
	    ReadRayCapabilities(directory / "ray-capabilities.csv", &probes, error) != ArenaStatus_Ok ||
	    ReadRayProcessMemory(directory / "ray-process.csv", &memory, error) != ArenaStatus_Ok)
		return error->code;
	const std::uint64_t tuples =
	    static_cast<std::uint64_t>(manifest->engineCount) * manifest->threadCount * manifest->repeatCount;
	if (rows.size() > tuples * manifest->measuredWorkUnitCount * benchmark_ray::Phase_Count * benchmark_ray::Api_Count ||
	    probes.size() > tuples * 6 * benchmark_ray::Probe_Count || memory.size() > tuples)
		return Invalid(error, "saved_ray_cardinality");
	std::size_t admittedRows = 0, admittedProbes = 0, admittedMemory = 0;
	for (std::uint32_t engineIndex = 0; engineIndex < manifest->engineCount; ++engineIndex)
	{
		const std::string_view engine =
		    CatalogTextView(catalog, catalog->engines[manifest->engines[engineIndex].engineIndex].id);
		for (std::uint32_t threadIndex = 0; threadIndex < manifest->threadCount; ++threadIndex)
			for (std::uint32_t repeat = 0; repeat < manifest->repeatCount; ++repeat)
			{
				const std::uint32_t threads = manifest->threadCounts[threadIndex];
				const ExecutionFailure* failure = FindExecutionFailure(manifest->executionFailures, manifest->engines[engineIndex].engineIndex, threads, repeat);
				const PresenceStatus required = failure == nullptr ? PresenceStatus_Present : PresenceStatus_Absent;
				std::vector<RayResultRow> tupleRows;
				for (const RayResultRow& row : rows)
					if (std::string_view(row.engine.data()) == engine && row.threads == threads && row.repeat == repeat)
						tupleRows.push_back(row);
				if (ValidateRayTracingTuple(tupleRows, engine, threads, repeat, manifest->configuration.execution, error, required) != ArenaStatus_Ok)
					return error->code;
				std::vector<RayProbeRow> tupleProbes;
				std::vector<RayProcessMemory> tupleMemory;
				for (const RayProbeRow& row : probes)
					if (std::string_view(row.engine.data()) == engine && row.threads == threads && row.repeat == repeat)
						tupleProbes.push_back(row);
				for (const RayProcessMemory& row : memory)
					if (std::string_view(row.engine.data()) == engine && row.threads == threads && row.repeat == repeat)
						tupleMemory.push_back(row);
				if (ValidateRayAuxiliaryTuple(tupleProbes, tupleMemory, engine, threads, repeat, error, required) !=
				    ArenaStatus_Ok)
					return error->code;
				if (failure != nullptr && failure->outcome == ExecutionOutcome_NotRun &&
				    (!tupleRows.empty() || !tupleProbes.empty() || !tupleMemory.empty()))
					return Invalid(error, "not_run_has_ray_work");
				admittedRows += tupleRows.size();
				admittedProbes += tupleProbes.size();
				admittedMemory += tupleMemory.size();
			}
	}
	if (admittedRows != rows.size() || admittedProbes != probes.size() || admittedMemory != memory.size())
		return Invalid(error, "saved_ray_outside_matrix");
	return ArenaStatus_Ok;
}

RayPhaseStatistics SummarizeRayPhase(const std::vector<RayResultRow>& rows, std::string_view engine,
                                     std::uint32_t threads, benchmark_ray::Phase phase, benchmark_ray::Api api,
                                     std::uint32_t requestedRepeats, std::uint32_t measuredSuites, std::span<const ExecutionFailure> failures, std::uint32_t engineIndex)
{
	RayPhaseStatistics result = {};
	result.requestedRepeats = requestedRepeats;
	result.requestedSamples = requestedRepeats * measuredSuites;
	std::array<std::uint32_t, kRunRepeatCapacity> observedCounts = {}, measuredCounts = {};
	constexpr std::array<int, 6> priority = {1, 0, 2, 3, 5, 4};
	std::vector<double> durations;
	std::vector<double> updates, combined;
	durations.reserve(result.requestedSamples);
	updates.reserve(result.requestedSamples);
	combined.reserve(result.requestedSamples);
	for (const RayResultRow& row : rows)
	{
		if (std::string_view(row.engine.data()) != engine || row.threads != threads || row.phase != phase ||
		    row.api != api)
			continue;
		++result.observedSamples;
		++observedCounts[row.repeat];
		result.errors += row.errors;
		if (result.capabilityPresence == PresenceStatus_Absent || priority[row.capability] > priority[result.capability])
			result.capability = row.capability;
		result.capabilityPresence = PresenceStatus_Present;
		if (RayMeasurementComplete(row.capability) == 0)
			continue;
		result.queries += row.queries;
		result.hitRays += row.hitRays;
		result.totalMs += row.queryMs;
		durations.push_back(row.queryMs);
		updates.push_back(row.updateMs);
		combined.push_back(row.updateMs + row.queryMs);
		++measuredCounts[row.repeat];
	}
	const std::uint32_t allSuites = measuredSuites;
	result.completeCoverage = PresenceStatus_Present;
	for (std::uint32_t repeat = 0; repeat < requestedRepeats; ++repeat)
	{
		const ExecutionFailure* failure = FindExecutionFailure(failures, engineIndex, threads, repeat);
		if (failure != nullptr)
		{
			if (failure->outcome == ExecutionOutcome_NotRun)
				++result.notRunRepeats;
			else
				++result.executionFailedRepeats;
			result.completeCoverage = PresenceStatus_Absent;
		}
		if (observedCounts[repeat] != allSuites)
		{
			result.completeCoverage = PresenceStatus_Absent;
			if (observedCounts[repeat] == 0)
				++result.missingRepeats;
			else
				++result.partialRepeats;
		}
		if (measuredCounts[repeat] == allSuites)
			++result.repeats;
	}
	if (durations.empty())
		return result;
	std::sort(durations.begin(), durations.end());
	result.samples = static_cast<std::uint32_t>(durations.size());
	const std::size_t middle = durations.size() / 2;
	result.medianMs = durations.size() % 2 == 0 ? (durations[middle - 1] + durations[middle]) * .5 : durations[middle];
	std::sort(updates.begin(), updates.end());
	std::sort(combined.begin(), combined.end());
	result.updateMedianMs = updates.size() % 2 == 0 ? (updates[middle - 1] + updates[middle]) * .5 : updates[middle];
	result.combinedMedianMs =
	    combined.size() % 2 == 0 ? (combined[middle - 1] + combined[middle]) * .5 : combined[middle];
	result.p95Ms = durations[static_cast<std::size_t>(std::ceil(.95 * static_cast<double>(durations.size()))) - 1];
	result.raysPerSecond = result.totalMs > 0 ? static_cast<double>(result.queries) * 1000 / result.totalMs : 0;
	result.missRays = result.queries - result.hitRays;
	if (result.queries != 0)
	{
		result.proportions = PresenceStatus_Present;
		result.hitPercent = 100.0 * static_cast<double>(result.hitRays) / static_cast<double>(result.queries);
		result.missPercent = 100.0 * static_cast<double>(result.missRays) / static_cast<double>(result.queries);
	}
	if (result.repeats != 0)
		result.repeatMedianMinMs = result.repeatRaysPerSecondMin = std::numeric_limits<double>::max();
	for (std::uint32_t repeat = 0; repeat < requestedRepeats; ++repeat)
	{
		if (measuredCounts[repeat] != allSuites)
			continue;
		durations.clear();
		std::uint64_t queries = 0;
		double milliseconds = 0;
		for (const RayResultRow& row : rows)
			if (std::string_view(row.engine.data()) == engine && row.threads == threads && row.phase == phase &&
			    row.api == api && row.repeat == repeat && RayMeasurementComplete(row.capability) != 0)
			{
				durations.push_back(row.queryMs);
				queries += row.queries;
				milliseconds += row.queryMs;
			}
		std::sort(durations.begin(), durations.end());
		const std::size_t mid = durations.size() / 2;
		const double median = durations.size() % 2 == 0 ? (durations[mid - 1] + durations[mid]) * .5 : durations[mid];
		const double throughput = milliseconds > 0 ? static_cast<double>(queries) * 1000 / milliseconds : 0;
		result.repeatMedianMinMs = std::min(result.repeatMedianMinMs, median);
		result.repeatMedianMaxMs = std::max(result.repeatMedianMaxMs, median);
		result.repeatRaysPerSecondMin = std::min(result.repeatRaysPerSecondMin, throughput);
		result.repeatRaysPerSecondMax = std::max(result.repeatRaysPerSecondMax, throughput);
	}
	return result;
}

const char* RayPhaseOutcomeText(const RayPhaseStatistics& statistics)
{
	if (statistics.executionFailedRepeats != 0 ||
	    (statistics.capabilityPresence == PresenceStatus_Present && statistics.capability == RayCapability_ExecutionFailed))
		return "Execution failed";
	if (statistics.notRunRepeats != 0 ||
	    (statistics.capabilityPresence == PresenceStatus_Present && statistics.capability == RayCapability_NotRun))
		return "Not run";
	if (statistics.capabilityPresence == PresenceStatus_Absent)
		return "Unavailable";
	if (statistics.capability == RayCapability_Failed)
		return "Failed";
	if (statistics.completeCoverage == PresenceStatus_Absent)
		return "Incomplete";
	if (statistics.capability == RayCapability_NativeSemanticsDiffer)
		return "Different semantics";
	return statistics.capability == RayCapability_Unsupported ? "Unsupported" : "Passed";
}

ArenaStatus MergeRayTracingResults(const Catalog* catalog, const PreparedRunRequest* request,
                                   const RunPathRecord* paths, StatusRecord* error)
{
	if (request->configuration.execution.fixtureKind != CaseFixtureKind_RayTracing)
		return ArenaStatus_Ok;
	ResultManifestRecord manifest = {};
	if (LoadResultManifest(paths->manifestPath.data(), catalog, &manifest, error) != ArenaStatus_Ok)
		return error->code;
	const std::filesystem::path root = paths->resultDirectory.data();
	const CaseExecutionSpec& execution = request->configuration.execution;
	std::array<std::array<std::uint32_t, benchmark_ray::Phase_Count>, 6> expectedQueries = {};
	for (std::uint32_t view = 0; view < execution.rayTracing.viewCount; ++view)
		for (std::uint32_t phase = 0; phase < benchmark_ray::Phase_Count; ++phase)
		{
			benchmark_ray::PhaseMetadata metadata = {};
			if (benchmark_ray::ReadPhaseMetadata(
			        benchmark_ray::PhasePath(root / "ray-corpus", view, static_cast<benchmark_ray::Phase>(phase)),
			        &metadata) != benchmark_ray::Status_Ok ||
			    metadata.view != view || metadata.phase != phase || metadata.width != execution.rayTracing.width ||
			    metadata.height != execution.rayTracing.height)
				return Invalid(error, "phase_corpus_metadata");
			expectedQueries[view][phase] = metadata.rays;
		}
	const std::filesystem::path temporary = root / "ray-tracing.csv.partial";
	std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
	output << std::setprecision(17);
	for (std::size_t index = 0; index < kColumns.size(); ++index)
		output << (index == 0 ? "" : ",") << kColumns[index];
	output << '\n';
	const std::filesystem::path probeTemporary = root / "ray-capabilities.csv.partial";
	std::ofstream probes(probeTemporary, std::ios::binary | std::ios::trunc);
	for (std::size_t index = 0; index < kProbeColumns.size(); ++index)
		probes << (index == 0 ? "" : ",") << kProbeColumns[index];
	probes << '\n';
	const std::filesystem::path memoryTemporary = root / "ray-process.csv.partial";
	std::ofstream memory(memoryTemporary, std::ios::trunc);
	memory << "engine_id,thread_count,repeat_index,peak_process_committed_bytes\n";
	for (std::uint32_t selection = 0; selection < request->engineCount; ++selection)
	{
		const std::string engine(CatalogTextView(catalog, catalog->engines[request->engineIndexes[selection]].id));
		for (std::uint32_t thread = 0; thread < request->threadCount; ++thread)
		{
			const std::uint32_t count = request->threadCounts[thread];
			for (std::uint32_t repeat = 0; repeat < request->repeatCount; ++repeat)
			{
				const std::filesystem::path source =
				    root / "raw" / engine / ("t" + std::to_string(count)) /
				    (engine + "_t" + std::to_string(count) + "_r" + std::to_string(repeat) + "_ray-tracing.csv");
				const ExecutionFailure* failure = FindExecutionFailure(manifest.executionFailures, request->engineIndexes[selection], count, repeat);
				const PresenceStatus required = failure == nullptr ? PresenceStatus_Present : PresenceStatus_Absent;
				std::vector<RayResultRow> rows;
				if (failure == nullptr || std::filesystem::exists(source))
				{
					const ArenaStatus read = ReadRayTracingResults(source, &rows, error);
					if (read != ArenaStatus_Ok)
					{
						if (failure == nullptr || failure->outcome == ExecutionOutcome_NotRun)
							return error->code;
						rows.clear();
					}
					if (ValidateRayTupleIdentities(rows, engine, count, repeat, execution, required, error) != ArenaStatus_Ok)
						return error->code;
				}
				*error = {};
				if (failure != nullptr && failure->outcome == ExecutionOutcome_NotRun && !rows.empty())
					return Invalid(error, "not_run_has_ray_work");
				std::size_t admitted = 0;
				for (const RayResultRow& row : rows)
				{
					ArenaStatus status = ValidateRaySample(row, execution.queryCount, error);
					if (status == ArenaStatus_Ok && RayMeasurementComplete(row.capability) != 0 &&
					    row.queries != expectedQueries[row.view][row.phase])
						status = Invalid(error, "phase_workload_mismatch");
					if (status != ArenaStatus_Ok)
					{
						if (failure == nullptr)
							return error->code;
						*error = {};
						continue;
					}
					rows[admitted++] = row;
				}
				rows.resize(admitted);
				std::vector<RayProcessMemory> memoryRows;
				const std::filesystem::path memorySource =
				    source.parent_path() /
				    (engine + "_t" + std::to_string(count) + "_r" + std::to_string(repeat) + "_ray-process.csv");
				if ((failure == nullptr || std::filesystem::exists(memorySource)) &&
				    ReadRayProcessMemory(memorySource, &memoryRows, error) != ArenaStatus_Ok)
				{
					if (failure == nullptr || failure->outcome == ExecutionOutcome_NotRun)
						return error->code;
					memoryRows.clear();
					*error = {};
				}
				for (const RayProcessMemory& row : memoryRows)
				{
					memory << engine << ',' << count << ',' << repeat << ',';
					if (row.available == PresenceStatus_Present)
						memory << row.peakCommittedBytes;
					memory << '\n';
				}
				std::vector<RayProbeRow> probeRows;
				const std::filesystem::path probeSource =
				    source.parent_path() /
				    (engine + "_t" + std::to_string(count) + "_r" + std::to_string(repeat) + "_ray-capabilities.csv");
				if ((failure == nullptr || std::filesystem::exists(probeSource)) &&
				    ReadRayCapabilities(probeSource, &probeRows, error) != ArenaStatus_Ok)
				{
					if (failure == nullptr || failure->outcome == ExecutionOutcome_NotRun)
						return error->code;
					probeRows.clear();
					*error = {};
				}
				if (failure != nullptr && failure->outcome == ExecutionOutcome_NotRun &&
				    (!rows.empty() || !probeRows.empty() || !memoryRows.empty()))
					return Invalid(error, "not_run_has_ray_work");
				if (ValidateRayAuxiliaryTuple(probeRows, memoryRows, engine, count, repeat, error, required) != ArenaStatus_Ok)
					return error->code;
				for (const RayProbeRow& row : probeRows)
					probes << engine << ',' << count << ',' << repeat << ',' << row.view << ','
					       << benchmark_ray::ProbeName(row.probe) << ",ordinary," << RayCapabilityName(row.capability)
					       << ',' << row.queries << ',' << row.hitRays << ',' << row.mismatches << ','
					       << row.nativeErrors << ',' << row.overflows << ',' << row.worldColliders << '\n';
				for (const RayResultRow& row : rows)
				{
					output << engine << ',' << count << ',' << repeat << ',' << row.view << ',' << row.suite << ','
					       << benchmark_ray::PhaseName(row.phase) << ',' << benchmark_ray::ApiName(row.api) << ','
					       << RayCapabilityName(row.capability);
					if (row.capability == RayCapability_Unsupported)
					{
						output << ",,,,,,,,,,,\n";
						continue;
					}
					for (std::uint32_t field = 0; field < 11; ++field)
					{
						output << ',';
						if ((row.observedFields & (1u << field)) == 0)
							continue;
						switch (field)
						{
						case 0: output << row.queries; break;
						case 1: output << row.hitRays; break;
						case 2: output << row.queryMs; break;
						case 3: output << row.updateMs; break;
						case 4: output << row.conditioningMs; break;
						case 5: output << row.validationMs; break;
						case 6: output << row.errors; break;
						case 7: output << row.written; break;
						case 8: output << row.setupMs; break;
						case 9: output << row.suiteMs; break;
						case 10: output << row.bufferBytes; break;
						}
					}
					output << '\n';
				}
			}
		}
	}
	output.close();
	probes.close();
	memory.close();
	if (!output || !probes || !memory)
		return Invalid(error, "ray_result_merge_write");
	for (const std::pair<std::filesystem::path, std::filesystem::path>& paths :
	     {std::pair{memoryTemporary, root / "ray-process.csv"},
	      std::pair{probeTemporary, root / "ray-capabilities.csv"}, std::pair{temporary, root / "ray-tracing.csv"}})
		if (MoveFileExW(paths.first.c_str(), paths.second.c_str(),
		                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
			return Invalid(error, "ray_result_merge_publish");
	return ArenaStatus_Ok;
}

}
