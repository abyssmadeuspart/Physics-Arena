#pragma once
#include "physics_arena/run_repeat_policy.h"

#include "physics_arena/csv_io.h"
#include "physics_arena/result_pipeline.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <optional>
#include <string_view>

namespace physics_arena
{
constexpr std::size_t kResultUnitCapacity = kEngineCapacity * kThreadCountCapacity;
constexpr std::size_t kObservationAggregateRowCapacity =
    kResultUnitCapacity * kRunRepeatCapacity * kObservationRepeatRowCapacity;

inline constexpr std::array<std::string_view, 21> kRawColumns =
    std::to_array<std::string_view>({"raw_schema_version",
	                                 "repeat_index",
	                                 "fixture_semantic",
	                                 "fixture_revision",
	                                 "physics_settings",
	                                 "body_count",
	                                 "shape_count",
	                                 "query_count",
	                                 "constraint_count",
	                                 "invalid_transform_count",
	                                 "case_status",
	                                 "metric_status",
	                                 "effective_thread_count",
	                                 "effective_worker_count",
	                                 "actual_taskgraph_worker_count",
	                                 "completed_work_unit_count",
	                                 "workload_elapsed_ms",
	                                 "render_elapsed_ms",
	                                 "present_wait_ms",
	                                 "visual_validation_status",
	                                 "proof_path"});

inline constexpr std::array<std::string_view, 5> kObservationSidecarColumns =
    std::to_array<std::string_view>({"repeat_index", "metric_id", "phase_id", "sample_index", "value"});

inline constexpr std::array<std::string_view, 14> kObservationRootColumns = std::to_array<std::string_view>(
    {"run_id", "engine_id", "case_id", "benchmark_mode", "thread_count", "repeat_index", "metric_id", "phase_id",
	 "sample_index", "value", "unit", "role", "expected_value", "validation_status"});

inline constexpr std::array<std::string_view, 55> kNormalizedColumns =
    std::to_array<std::string_view>({"run_id",
	                                 "engine_id",
	                                 "engine_ref",
	                                 "host_route",
	                                 "toolchain_id",
	                                 "case_id",
	                                 "benchmark_mode",
	                                 "thread_count",
	                                 "work_unit_id",
	                                 "measured_work_unit_count",
	                                 "warmup_work_unit_count",
	                                 "repeat_index",
	                                 "body_count",
	                                 "shape_count",
	                                 "query_count",
	                                 "constraint_count",
	                                 "primary_metric_id",
	                                 "primary_metric_value",
	                                 "primary_metric_unit",
	                                 "primary_metric_direction",
	                                 "mean_ms_per_work_unit",
	                                 "work_units_per_second",
	                                 "invalid_transform_count",
	                                 "case_status",
	                                 "metric_status",
	                                 "runtime_route",
	                                 "timing_scope",
	                                 "effective_thread_count",
	                                 "fixture_semantic",
	                                 "fixture_revision",
	                                 "physics_settings",
	                                 "build_settings",
	                                 "requested_thread_count",
	                                 "requested_worker_count",
	                                 "requested_taskgraph_worker_count",
	                                 "actual_taskgraph_worker_count",
	                                 "effective_worker_count",
	                                 "main_thread_participates",
	                                 "unity_editor_version",
	                                 "unity_physics_package_version",
	                                 "entities_package_version",
	                                 "burst_package_version",
	                                 "collections_package_version",
	                                 "build_target",
	                                 "backend",
	                                 "burst_enabled_state",
	                                 "safety_check_state",
	                                 "package_lock_sha256",
	                                 "completed_work_unit_count",
	                                 "workload_elapsed_ms",
	                                 "render_elapsed_ms",
	                                 "present_wait_ms",
	                                 "visual_validation_status",
	                                 "proof_path",
	                                 "raw_path"});

inline constexpr std::array<std::string_view, 22> kSummaryColumns =
    std::to_array<std::string_view>({"run_id",
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
	                                 "build_settings"});

struct UnitOrderRecord
{
	std::uint32_t selectedEngineIndex;
	std::uint32_t selectedThreadIndex;
};

struct RowWriter
{
	CsvWriter* writer;
	StatusRecord* error;
	std::uint32_t fieldIndex;
	std::uint32_t fieldCount;
};

struct ResultAccumulator
{
	std::array<RepeatFailureCause, kRunRepeatCapacity> repeatFailures;
	std::array<PresenceStatus, kRunRepeatCapacity> seen;
	std::array<double, kRunRepeatCapacity> primaryValues;
	std::array<double, kRunRepeatCapacity> meanMilliseconds;
	std::array<double, kRunRepeatCapacity> workUnitsPerSecond;
	std::array<double, kRunRepeatCapacity> workloadElapsedMilliseconds;
	std::array<char, kCatalogTextValueCapacity> physicsSettings;
	std::array<char, kCatalogTextValueCapacity> buildSettings;
	std::uint32_t physicsSettingsSize;
	std::uint32_t buildSettingsSize;
	std::uint32_t rowCount;
	std::uint32_t bodyCount;
	std::uint32_t shapeCount;
	std::uint32_t queryCount;
	std::uint32_t constraintCount;
	std::uint64_t invalidTransformCount;
};

struct ResultSummary
{
	std::uint32_t engineIndex;
	std::uint32_t threadCount;
	std::uint32_t repeatCount;
	std::uint32_t bodyCount;
	std::uint32_t shapeCount;
	std::uint32_t queryCount;
	std::uint32_t constraintCount;
	std::uint64_t invalidTransformCount;
	double minimumPrimaryValue;
	double medianPrimaryValue;
	double maximumPrimaryValue;
	std::array<char, kCatalogTextValueCapacity> physicsSettings;
	std::array<char, kCatalogTextValueCapacity> buildSettings;
	std::uint32_t physicsSettingsSize;
	std::uint32_t buildSettingsSize;
};

struct RawContext
{
	std::array<PresenceStatus, kRunRepeatCapacity> inputSeen;
	std::optional<std::uint32_t> rejectedRepeat;
	std::span<const ExecutionFailure> failures;
	const EffectiveRunConfiguration* configuration;
	const wchar_t* repositoryRoot;
	const Catalog* catalog;
	const ReleaseCatalog* releaseCatalog;
	const PreparedRunRequest* request;
	const RunPathRecord* paths;
	const CaseRecord* benchmarkCase;
	const EngineRecord* engine;
	const ReleaseArtifactRecord* artifact;
	CsvWriter* normalizedWriter;
	ResultAccumulator accumulator;
	std::array<char, kRunPathCapacity> rawRelativePath;
	std::uint32_t rawRelativePathSize;
	std::uint32_t engineIndex;
	std::uint32_t threadCount;
	PresenceStatus headerValidated;
};

struct ObservationUnitContext
{
	std::array<RepeatFailureCause, kRunRepeatCapacity> repeatFailures;
	std::array<std::bitset<kObservationRepeatRowCapacity>, kRunRepeatCapacity> inputSeen;
	std::optional<std::uint32_t> rejectedRepeat;
	std::array<std::uint32_t, kRunRepeatCapacity> repeatRows;
	std::uint32_t repeatCount;
	std::uint32_t engineIndex;
	std::span<const ExecutionFailure> failures;
	const EffectiveRunConfiguration* configuration;
	const Catalog* catalog;
	const CaseRecord* benchmarkCase;
	const RunPathRecord* paths;
	CsvWriter* rootWriter;
	std::string_view engineId;
	std::string_view benchmarkMode;
	std::uint32_t threadCount;
	std::uint32_t expectedRowCount;
	std::uint32_t rowCount;
	PresenceStatus headerValidated;
};

struct NormalizedValidationContext
{
	const Catalog* catalog;
	const ResultManifestRecord* manifest;
	std::array<std::array<std::array<PresenceStatus, kRunRepeatCapacity>, kThreadCountCapacity>, kEngineCapacity> seen;
	std::uint32_t rowCount;
	PresenceStatus headerValidated;
};

struct NormalizedSummaryContext
{
	NormalizedValidationContext validation;
	std::array<ResultAccumulator, kThreadCountCapacity> accumulators;
	std::uint32_t targetEngineOrdinal;
};

template <std::size_t Count>
ArenaStatus WriteHeader(CsvWriter* writer, const std::array<std::string_view, Count>& columns, StatusRecord* error)
{
	for (std::size_t index = 0; index < columns.size(); ++index)
		if (WriteCsvField(writer, columns[index],
		                  index + 1 == columns.size() ? CsvFieldTerminator_EndRow : CsvFieldTerminator_MoreFields,
		                  error) != ArenaStatus_Ok)
			return error->code;
	return ArenaStatus_Ok;
}

ArenaStatus PipelineError(StatusRecord* error, ArenaStatus status, std::string_view detail);
ArenaStatus CompareSerializedMetric(double actual, double expected);
ArenaStatus ParseUnsigned(std::string_view text, std::uint32_t* value);
ArenaStatus ParseUnsigned64(std::string_view text, std::uint64_t* value);
ArenaStatus ParsePositiveDouble(std::string_view text, double* value);
std::string_view PrimaryDirectionText(PrimaryMetricDirection direction);
ArenaStatus ValidateNormalizedRow(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error);
ArenaStatus ValidateNormalizedCoverage(const NormalizedValidationContext& context, StatusRecord* error);
ArenaStatus AccumulateNormalizedSummaryRow(const CsvHeader* header, const CsvRow* row, void* opaque,
                                           StatusRecord* error);
ResultSummary SummarizeUnit(const ResultAccumulator& accumulator, std::uint32_t engineIndex, std::uint32_t threadCount);
ArenaStatus WriteResultSummaryRow(CsvWriter* writer, const Catalog* catalog, const CaseRecord* benchmarkCase,
                                  std::string_view runId, std::string_view benchmarkMode, const ResultSummary& summary,
                                  StatusRecord* error,
                                  ResultMeasurementMode mode = ResultMeasurementMode_PhysicalQuality,
                                  const EffectiveRunConfiguration* configuration = nullptr);
void DeleteOutput(const std::array<wchar_t, kRunPathCapacity>& path);
ArenaStatus RenameOutput(const std::array<wchar_t, kRunPathCapacity>& temporary,
                         const std::array<wchar_t, kRunPathCapacity>& final, StatusRecord* error);
ArenaStatus FinalizeResults(const wchar_t* repositoryRoot, const Catalog* catalog, const ReleaseCatalog* releaseCatalog,
                            const PreparedRunRequest* request, const RunPathRecord* paths, ResultPipelineRecord* record,
                            StatusRecord* error);
} // namespace physics_arena
