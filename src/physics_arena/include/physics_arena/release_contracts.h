#pragma once

#include "physics_arena/host_windows.h"
#include "physics_arena/run_settings.h"
#include "physics_arena/stack_stability.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace physics_arena
{
constexpr std::size_t kReleaseRunnerPrefixCapacity = 64;
constexpr std::size_t kReleaseTextArenaCapacity = 8192;
constexpr std::size_t kResultTextArenaCapacity = 8192;

enum ResultMeasurementMode
{
	ResultMeasurementMode_Timed = 0,
	ResultMeasurementMode_PhysicalQuality = 1,
};

enum TimingRenderSeries
{
	TimingRenderSeries_Absent = 0,
	TimingRenderSeries_SampledFrameWall = 1,
};

enum ReleaseLoadMode
{
	ReleaseLoadMode_RequireAll = 0,
	ReleaseLoadMode_Available = 1,
};

struct ReleaseArtifactRecord
{
	std::uint32_t engineIndex;
	CatalogText manifestPath;
	CatalogText executablePath;
	CatalogText sourceVersion;
	CatalogText toolchainId;
	CatalogText reportVersion;
	std::uint32_t producerPrefixOffset;
	std::uint32_t producerPrefixCount;
};

struct ApplicationArtifactRecord
{
	CatalogText manifestPath;
	CatalogText executablePath;
};

struct ReleaseCatalog
{
	std::array<ReleaseArtifactRecord, kEngineCapacity> artifacts;
	std::array<std::uint32_t, kEngineCapacity> engineArtifactIndexes;
	std::array<PresenceStatus, kEngineCapacity> engineArtifactAvailability;
	ApplicationArtifactRecord application;
	std::array<char, kReleaseTextArenaCapacity> textArena;
	std::array<CatalogText, kReleaseRunnerPrefixCapacity> producerPrefixArguments;
	CatalogText hostRoute;
	std::uint32_t artifactCount;
	std::uint32_t textArenaUsed;
	std::uint32_t producerPrefixCount;
};

struct ResultEngineSnapshot
{
	std::uint32_t engineIndex;
	CatalogText artifactManifestPath;
	CatalogText sourceVersion;
	CatalogText toolchainId;
	CatalogText timingScope;
	CatalogText reportVersion;
};

enum ExecutionStage
{
	ExecutionStage_Benchmark,
	ExecutionStage_Preflight,
};
enum ExecutionOutcome
{
	ExecutionOutcome_Failed,
	ExecutionOutcome_NotRun,
};
enum ExecutionFailureReason
{
	ExecutionFailureReason_ProcessExit,
	ExecutionFailureReason_ProcessTimeout,
	ExecutionFailureReason_InvalidOutput,
	ExecutionFailureReason_Recording,
	ExecutionFailureReason_PreviousRepeatFailed,
};
struct ExecutionFailure
{
	std::uint32_t engineIndex, threadCount, repeatIndex;
	ExecutionStage stage;
	ExecutionOutcome outcome;
	ExecutionFailureReason reason;
	PresenceStatus exitCodePresence;
	std::int32_t exitCode;
	std::array<char, kDetailCapacity> detail;
};

const ExecutionFailure* FindExecutionFailure(std::span<const ExecutionFailure> failures, std::uint32_t engineIndex,
                                              std::uint32_t threadCount, std::uint32_t repeatIndex);
const char* ExecutionFailureReasonName(ExecutionFailureReason reason);

struct ResultManifestRecord
{
	VerificationMode verificationMode = VerificationMode_On;
	std::vector<ExecutionFailure> executionFailures;
	EffectiveRunConfiguration configuration;
	PresenceStatus descriptiveCasePresence;
	CatalogText runId;
	CatalogText hostRoute;
	CatalogText caseId;
	CatalogText benchmarkMode;
	CatalogText workUnitId;
	CatalogText visualRendererManifestPath;
	CatalogText observationsCsv;
	std::array<char, kResultTextArenaCapacity> textArena;
	std::array<std::uint32_t, kThreadCountCapacity> threadCounts;
	std::array<ResultEngineSnapshot, kEngineCapacity> engines;
	HostRecord host;
	std::uint32_t schemaVersion;
	RecordingMode recordingMode;
	RecordingThreadSet recordingThreads;
	std::uint32_t recordingVersion;
	RecordingKind recordingKind;
	std::uint32_t threadCount;
	std::uint32_t measuredWorkUnitCount;
	std::uint32_t warmupWorkUnitCount;
	std::uint32_t repeatCount;
	std::uint32_t engineCount;
	std::uint32_t observationsSchemaVersion;
	std::uint32_t renderWidthPixels;
	std::uint32_t renderHeightPixels;
	PresenceStatus observationsPresence;
	PresenceStatus stabilityPresence;
	StackCriterion stabilityCriterion;
	PresenceStatus renderResolutionPresence;
	TimingRenderSeries timingRenderSeries;
	ResultMeasurementMode measurementMode;
	std::uint32_t textArenaUsed;
};

struct ResultCaseMetadata
{
	CaseRecord record;
	std::string_view primaryMetricId;
	std::string_view primaryMetricUnit;
	std::string_view primaryMetricLabel;
	std::string_view primaryMetricNote;
	PrimaryMetricDirection direction;
};

const EffectiveRunConfiguration* ResultConfiguration(const ResultManifestRecord* manifest);
const CaseRecord& ResultCaseDefinition(const CaseRecord& authored, const ResultManifestRecord* manifest);
std::string_view ResultCaseTextView(const Catalog* catalog, const ResultManifestRecord* manifest, CatalogText text);
ResultCaseMetadata ProjectResultCaseMetadata(const Catalog* catalog, const CaseRecord& record,
                                             ResultMeasurementMode mode,
                                             const EffectiveRunConfiguration* configuration = nullptr);

static_assert(sizeof(Catalog) + sizeof(ReleaseCatalog) + sizeof(ResultManifestRecord) < kMainStackReservationBytes / 4);

std::string_view ReleaseTextView(const ReleaseCatalog* releaseCatalog, CatalogText text);
std::string_view ResultTextView(const ResultManifestRecord* resultManifest, CatalogText text);
inline std::string_view ResultThreadBenchmarkMode(const ResultManifestRecord* manifest, std::uint32_t threadCount)
{
	return manifest->schemaVersion >= 7 ?
	    (RecordingForThread(manifest->recordingThreads, threadCount) == RecordingMode_On ? "recorded_api" : "headless_api") :
	    ResultTextView(manifest, manifest->benchmarkMode);
}
ArenaStatus LoadReleaseCatalog(const wchar_t* repositoryRoot, const Catalog* catalog, ReleaseCatalog* releaseCatalog,
                               StatusRecord* error, ReleaseLoadMode mode = ReleaseLoadMode_RequireAll);
ArenaStatus LoadResultManifest(const wchar_t* manifestPath, const Catalog* catalog,
                               ResultManifestRecord* resultManifest, StatusRecord* error);
}
