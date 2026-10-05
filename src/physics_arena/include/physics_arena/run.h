#pragma once

#include "physics_arena/host_windows.h"
#include "physics_arena/invocation.h"
#include "physics_arena/process_windows.h"
#include "physics_arena/release_contracts.h"
#include "physics_arena/run_settings.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace physics_arena
{
enum ResultStorage
{
	ResultStorage_Repository,
	ResultStorage_Local,
	ResultStorage_External,
};
const wchar_t* ResultStorageRoot(ResultStorage storage);
ResultStorage ClassifyResultStorage(const wchar_t* repositoryRoot, const wchar_t* resultDirectory);
int CaseRouteSupported(const Catalog* catalog, const EngineRecord& engine, std::uint32_t caseIndex);
constexpr std::size_t kRunPathCapacity = 4096;
constexpr std::size_t kRunRepeatCapacity = 64;

enum ThreadSelectionMode
{
	ThreadSelectionMode_Default = 0,
	ThreadSelectionMode_Explicit = 1,
	ThreadSelectionMode_Maximum = 2,
};

enum RayRunStage
{
	RayRunStage_Heavy = 0,
	RayRunStage_Preflight = 1,
};

struct RunRequestInput
{
	VerificationMode verificationMode = VerificationMode_On;
	ResultStorage storage = ResultStorage_Local;
	RecordingMode recordingMode;
	RecordingThreadSelection recordingThreadSelection;
	RecordingThreadSet recordingThreads;
	RunSettings settings;
	std::array<std::uint32_t, kEngineCapacity> engineIndexes;
	std::array<std::uint32_t, kThreadCountCapacity> threadCounts;
	std::uint32_t caseIndex;
	std::uint32_t engineCount;
	std::uint32_t threadCount;
	std::uint32_t requestedMaximumThreadCount;
	std::uint32_t repeatCount;
	ThreadSelectionMode threadSelectionMode;
};

struct PreparedRunRequest
{
	VerificationMode verificationMode = VerificationMode_On;
	ResultStorage storage = ResultStorage_Local;
	RecordingMode recordingMode;
	RecordingThreadSet recordingThreads;
	std::uint32_t recordingVersion;
	RecordingKind recordingKind;
	EffectiveRunConfiguration configuration;
	std::array<std::uint32_t, kEngineCapacity> engineIndexes;
	std::array<std::uint32_t, kEngineCapacity> artifactIndexes;
	std::array<std::uint32_t, kThreadCountCapacity> threadCounts;
	std::array<std::uint32_t, kThreadCountCapacity> requestedThreadCounts;
	std::array<std::uint32_t, kThreadCountCapacity> droppedThreadCounts;
	std::array<std::uint32_t, kThreadCountCapacity> generatedThreadCounts;
	std::uint32_t caseIndex;
	std::uint32_t engineCount;
	std::uint32_t threadCount;
	std::uint32_t requestedThreadCount;
	std::uint32_t droppedThreadCount;
	std::uint32_t generatedThreadCount;
	std::uint32_t repeatCount;
	std::uint32_t totalUnitCount;
	std::uint32_t totalProcessCount;
	std::uint32_t hostLogicalThreadCount;
	std::uint32_t requestedMaximumThreadCount;
	std::uint32_t effectiveMaximumThreadCount;
	ThreadSelectionMode threadSelectionMode;
};

struct RunPathRecord
{
	std::uint64_t requiredCaptureBytes;
	std::array<wchar_t, kRunPathCapacity> resultDirectory;
	std::array<wchar_t, kRunPathCapacity> rawDirectory;
	std::array<wchar_t, kRunPathCapacity> manifestPath;
	std::array<char, 256> runId;
	std::array<char, 256> baseRunId;
	std::array<char, 32> timestamp;
	std::array<char, 1024> threadSelectionToken;
	std::array<char, 16> collisionSuffix;
	std::uint32_t runIdSize;
	std::uint32_t baseRunIdSize;
	std::uint32_t timestampSize;
	std::uint32_t threadSelectionTokenSize;
	std::uint32_t collisionSuffixSize;
};

struct RunExecutionControl
{
	std::atomic<std::uint32_t> compressingReplay;
	std::atomic<std::uint32_t> cancellationRequested;
	std::atomic<std::uint32_t> completedUnitCount;
	std::atomic<std::uint32_t> failedUnitCount;
	std::atomic<std::uint32_t> startedProcessCount;
	std::atomic<std::uint32_t> passedRepeatCount;
	std::atomic<std::uint32_t> unverifiedRepeatCount;
	std::atomic<std::uint32_t> qualityFailedRepeatCount;
	std::atomic<std::uint32_t> executionFailedRepeatCount;
	std::atomic<std::uint32_t> skippedRepeatCount;
	std::atomic<std::uint32_t> currentEngineIndex;
	std::atomic<std::uint32_t> currentThreadCount;
	std::atomic<std::uint32_t> currentRepeatIndex;
};

struct RunExecutionResult
{
	std::vector<ExecutionFailure> failures;
	std::uint32_t completedUnitCount;
	std::uint32_t failedUnitCount;
	std::uint32_t startedProcessCount;
	std::uint32_t qualityFailedRepeatCount;
	std::uint32_t passedRepeatCount;
	std::uint32_t unverifiedRepeatCount;
	std::uint32_t executionFailedRepeatCount;
	std::uint32_t skippedRepeatCount;
	std::int32_t exitCode;
	ArenaStatus status;
};

static_assert(sizeof(PreparedRunRequest) + sizeof(RunPathRecord) + sizeof(ProcessSpec) + sizeof(ProcessPollRecord) +
                  sizeof(ProcessRecord) <
              kWorkerStackReservationBytes / 2);

void InitializeRunExecutionControl(RunExecutionControl* control);
void CancelRunExecution(RunExecutionControl* control);
int EngineSupportsThreadCount(const Catalog* catalog, const EngineRecord& engine, std::uint32_t threadCount);

ArenaStatus PrepareRunRequest(const Catalog* catalog, const ReleaseCatalog* releaseCatalog, const HostRecord* host,
                              const RunRequestInput* input, PreparedRunRequest* request, StatusRecord* error);
ArenaStatus RenderRunProcessSpec(const wchar_t* repositoryRoot, const wchar_t* resultDirectory, const Catalog* catalog,
                                 const ReleaseCatalog* releaseCatalog, const PreparedRunRequest* request,
                                 std::uint32_t selectedEngineIndex, std::uint32_t selectedThreadIndex,
                                 std::uint32_t repeatIndex, std::string_view stackEndpoint, ProcessSpec* spec, StatusRecord* error,
                                 RayRunStage rayStage = RayRunStage_Heavy);
ArenaStatus CreateRunPaths(const wchar_t* repositoryRoot, const Catalog* catalog, const HostRecord* host,
                           const PreparedRunRequest* request, RunPathRecord* paths, StatusRecord* error);
ArenaStatus WritePreparedRunManifest(const Catalog* catalog, const ReleaseCatalog* releaseCatalog,
                                     const HostRecord* host, const PreparedRunRequest* request,
                                     const RunPathRecord* paths, StatusRecord* error);
ArenaStatus ExecuteHeadlessRequest(const wchar_t* repositoryRoot, const Catalog* catalog,
                                   const ReleaseCatalog* releaseCatalog, const HostRecord* host,
                                   const PreparedRunRequest* request, const RunPathRecord* paths,
                                   RunExecutionControl* control, InvocationContext* invocation,
                                   EventTransport* transport, RunExecutionResult* result, StatusRecord* error);
}
