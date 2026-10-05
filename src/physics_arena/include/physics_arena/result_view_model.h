#pragma once

#include "physics_arena/observation_results.h"
#include "physics_arena/run.h"
#include "physics_arena/stack_stability.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace physics_arena
{
constexpr std::size_t kResultSummaryCapacity = kEngineCapacity * kThreadCountCapacity;
constexpr std::size_t kResultViewTextArenaCapacity = 65536;
constexpr std::size_t kResultMetricCapacity = 6;
constexpr std::size_t kEngineProvenanceLabelCapacity = kCatalogTextValueCapacity * 2 + 2;

enum ResultMetricId
{
	ResultMetric_MedianPrimaryValue = 0,
	ResultMetric_MinimumPrimaryValue = 1,
	ResultMetric_MaximumPrimaryValue = 2,
	ResultMetric_MedianWorkUnitMilliseconds = 3,
	ResultMetric_MinimumWorkUnitMilliseconds = 4,
	ResultMetric_MaximumWorkUnitMilliseconds = 5,
};

struct ResultViewText
{
	std::uint32_t offset;
	std::uint32_t size;
};

enum ResultRepeatDisposition
{
	ResultRepeatDisposition_Measured,
	ResultRepeatDisposition_ExecutionFailed,
	ResultRepeatDisposition_Skipped,
};

struct ResultRepeatRow
{
	ResultRepeatDisposition disposition;
	PresenceStatus measurement;
	ObservationOutcome outcome;
	double primaryValue;
	double meanWorkUnitMilliseconds;
	double workUnitsPerSecond;
	double workloadElapsedMilliseconds;
	std::uint64_t invalidTransformCount;
	std::uint32_t completedWorkUnitCount;
	std::uint32_t repeatIndex;
};

struct ResultRepeatProjection
{
	std::array<ResultRepeatRow, kRunRepeatCapacity> rows;
	std::uint32_t rowCount;
	std::uint32_t engineOrdinal;
	std::uint32_t threadCount;
	ResultMeasurementMode measurementMode;
};

struct ResultViewModel;

ArenaStatus ProjectResultRepeats(const wchar_t* normalizedPath, const Catalog* catalog,
                                 const ResultManifestRecord* manifest, std::uint32_t engineOrdinal,
                                 std::uint32_t threadCount, ResultRepeatProjection* projection,
                                 StatusRecord* error, const ResultViewModel* outcomeModel = nullptr);

struct EngineProvenanceLabelProjection
{
	std::array<char, kEngineProvenanceLabelCapacity> text;
	std::uint32_t size;
};

struct ResultEngineView
{
	ResultViewText id;
	ResultViewText displayName;
	ResultViewText reportVersion;
	ResultViewText provenanceLabel;
	ResultViewText physicsSettings;
	ResultViewText buildSettings;
	std::uint32_t catalogEngineIndex;
	std::uint32_t presentationOrder;
	std::uint32_t colorRgb;
	std::uint32_t physicsSettingsThreadCount;
};

struct ResultSummaryViewRow
{
	StackAssessment stability;
	std::uint32_t engineOrdinal;
	std::uint32_t threadCount;
	std::uint32_t repeatCount;
	std::uint32_t warmupWorkUnitCount;
	std::uint32_t bodyCount;
	std::uint32_t shapeCount;
	std::uint32_t queryCount;
	std::uint32_t constraintCount;
	std::int64_t invalidTransformCount;
	double minimumPrimaryValue;
	double medianPrimaryValue;
	double maximumPrimaryValue;
	double minimumWorkUnitMilliseconds;
	double medianWorkUnitMilliseconds;
	double maximumWorkUnitMilliseconds;
	ObservationOutcome outcome;
};

struct ResultGroupView
{
	ResultViewText label;
};

struct ResultObservationView
{
	ResultViewText label;
	ResultViewText unit;
	std::uint32_t resultGroupOrdinal;
	ObservationValueType valueType;
	ObservationRole role;
	PresenceStatus expectedValuePresence;
};

struct ResultMetricDescriptor
{
	ResultViewText label;
	ResultViewText axisLabel;
	ResultViewText unitLabel;
	ResultMetricId id;
	PresenceStatus lowerIsBetter;
};

struct TimingComparisonTrace
{
	double medianPhysicsStepMilliseconds;
	double p95PhysicsStepMilliseconds;
	double p99PhysicsStepMilliseconds;
	double maximumPhysicsStepMilliseconds;
	std::uint32_t engineOrdinal;
	std::uint32_t worstStepIndex;
};

struct TimingComparisonCache
{
	TimingProjection projection;
	std::array<TimingComparisonTrace, kEngineCapacity> traces;
	std::uint32_t selectedEngineMask;
	std::uint32_t threadCount;
	std::uint32_t repeatIndex;
	std::uint32_t traceCount;
	TimingProjectionMode mode;
	AvailabilityStatus availability;
	std::uint64_t generation;
};

struct ResultViewModel
{
	VerificationMode verificationMode = VerificationMode_On;
	StackCriterion requiredStabilityCriterion;
	std::vector<StackStabilityResult> stabilityResults;
	PresenceStatus stabilityRequired;
	std::vector<ExecutionFailure> executionFailures;
	std::array<ResultSummaryViewRow, kResultSummaryCapacity> summaryRows;
	std::array<std::uint32_t, kResultSummaryCapacity> displayRowIndexes;
	std::array<ResultEngineView, kEngineCapacity> engines;
	std::array<std::uint32_t, kThreadCountCapacity> threadCounts;
	std::array<ResultMetricDescriptor, kResultMetricCapacity> metrics;
	std::array<ResultGroupView, kObservationPerCaseCapacity> resultGroups;
	std::array<ResultObservationView, kObservationPerCaseCapacity> observationViews;
	TimingArtifactIndex timing;
	TimingComparisonCache timingComparison;
	ObservationResultModel observations;
	ObservationDetailProjection observationDetail;
	HostRecord host;
	EffectiveRunConfiguration savedConfiguration;
	PresenceStatus descriptiveCasePresence;
	std::uint32_t resultSchemaVersion;
	std::array<char, kResultViewTextArenaCapacity> textArena;
	ResultViewText runId;
	ResultViewText caseId;
	ResultViewText caseDisplayName;
	ResultViewText caseDescription;
	ResultViewText benchmarkMode;
	ResultViewText runProvenance;
	ResultViewText hostRoute;
	ResultViewText reportTitle;
	ResultViewText chartLabel;
	ResultViewText chartNote;
	ResultViewText releaseSourceLabel;
	ResultViewText workUnitId;
	ResultViewText workUnitLabel;
	ResultViewText primaryMetricId;
	ResultViewText primaryMetricUnit;
	std::uint32_t summaryRowCount;
	std::uint32_t displayRowCount;
	std::uint32_t engineCount;
	std::uint32_t threadCount;
	std::uint32_t metricCount;
	std::uint32_t measuredWorkUnitCount;
	std::uint32_t warmupWorkUnitCount;
	std::uint32_t repeatCount;
	std::uint32_t timestepHz;
	std::uint32_t bodyCount;
	std::uint32_t shapeCount;
	std::uint32_t queryCount;
	std::uint32_t constraintCount;
	std::uint32_t resultGroupCount;
	std::uint32_t observationCount;
	std::uint32_t reportIndex;
	std::uint32_t caseIndex;
	std::uint32_t textArenaUsed;
	std::uint32_t renderWidthPixels;
	std::uint32_t renderHeightPixels;
	PrimaryMetricDirection primaryMetricDirection;
	ResultMeasurementMode measurementMode;
	PresenceStatus publicationReportPresence;
	PresenceStatus renderResolutionPresence;
};

static_assert(sizeof(ResultViewModel) < kMainStackReservationBytes / 2);

ArenaStatus ProjectEngineProvenanceLabel(std::string_view engineId, std::string_view displayName,
                                         std::string_view reportVersion, EngineProvenanceLabelProjection* projection,
                                         StatusRecord* error);
std::uint32_t EffectiveWorkerCount(WorkerCountPolicy policy, std::uint32_t threadCount);
std::string_view ResultViewTextView(const ResultViewModel* model, ResultViewText text);
const EffectiveRunConfiguration* ResultViewConfiguration(const ResultViewModel* model);
const CaseRecord& ResultViewCaseDefinition(const Catalog* catalog, const ResultViewModel* model);
double ResultMetricValue(const ResultSummaryViewRow* row, ResultMetricId metric);
ArenaStatus LoadResultViewModel(const wchar_t* summaryPath, const wchar_t* normalizedPath, const wchar_t* timingPath,
                                const Catalog* catalog, const ResultManifestRecord* manifest, ResultViewModel* model,
                                StatusRecord* error, TimingArtifactTotals* totals = nullptr);
}
