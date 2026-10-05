#pragma once

#include "physics_arena/run.h"
#include "physics_arena/run_repeat_policy.h"

#include <array>
#include <string_view>

namespace physics_arena
{
enum RunProcessDisposition
{
	RunProcessDisposition_Completed,
	RunProcessDisposition_EngineFailure,
	RunProcessDisposition_HarnessFailure,
	RunProcessDisposition_Interrupted,
};
struct RunProcessOutcome
{
	RunProcessDisposition disposition;
	ExecutionFailureReason reason;
	PresenceStatus exitCodePresence;
	std::int32_t exitCode;
	CompletedRepeatEvidence completed;
};
ArenaStatus EmitRunEvent(InvocationContext* invocation, EventTransport* transport, const char* component,
                         const char* status, const char* detail, StatusRecord* error);
ArenaStatus RunOneProcess(const wchar_t* repositoryRoot, const Catalog* catalog, const ReleaseCatalog* releaseCatalog,
                          const PreparedRunRequest* request, const RunPathRecord* paths,
                          std::uint32_t selectedEngineIndex, std::uint32_t selectedThreadIndex,
                          std::uint32_t repeatIndex, RunExecutionControl* control, InvocationContext* invocation,
                          RunExecutionResult* result, StatusRecord* error,
	                          RayRunStage rayStage = RayRunStage_Heavy, RunProcessOutcome* outcome = nullptr);
ArenaStatus EnsureUnitDirectories(const Catalog* catalog, const PreparedRunRequest* request, const RunPathRecord* paths,
                                  std::uint32_t selectedEngineIndex, std::uint32_t selectedThreadIndex,
                                  std::array<wchar_t, kRunPathCapacity>* threadDirectory, StatusRecord* error);
ArenaStatus ValidateRunTimingFile(const wchar_t* path, std::uint32_t expectedRows, double expectedTotal, StatusRecord* error);

ArenaStatus ValidateRawOutput(const wchar_t* repositoryRoot, const Catalog* catalog, const ReleaseCatalog* release,
                              const PreparedRunRequest* request, const RunPathRecord* paths,
                              std::uint32_t selectedEngineIndex, std::uint32_t selectedThreadIndex, std::uint32_t repeat,
                              const wchar_t* threadDirectory, std::span<const ExecutionFailure> failures,
                              RunProcessDisposition* disposition, CompletedRepeatEvidence* completed, StatusRecord* error);
ArenaStatus RunError(StatusRecord* error, ArenaStatus status, std::string_view detail);
ArenaStatus AppendRunUtf8AsWide(std::array<wchar_t, kRunPathCapacity>* output, std::uint32_t* size,
                                std::string_view value);
ArenaStatus BuildRunChildPath(const wchar_t* parent, std::wstring_view child,
                              std::array<wchar_t, kRunPathCapacity>* output);
ArenaStatus BuildRunChildPathUtf8(const wchar_t* parent, std::string_view child,
                                  std::array<wchar_t, kRunPathCapacity>* output);
ArenaStatus EnsureDirectory(const wchar_t* path);
enum RunRecordingLocation
{
	RunRecordingLocation_Published = 0,
	RunRecordingLocation_Spool = 1,
};
ArenaStatus ValidateRunRecording(const Catalog* catalog, const PreparedRunRequest* request, const RunPathRecord* paths,
                                 std::uint32_t selectedEngineIndex, std::uint32_t selectedThreadIndex,
                                 std::uint32_t repeatIndex, StatusRecord* error,
                                 RunRecordingLocation location = RunRecordingLocation_Published);
void CleanupFailedRunRecording(const Catalog* catalog, const PreparedRunRequest* request, const RunPathRecord* paths,
                               std::uint32_t selectedEngineIndex, std::uint32_t selectedThreadIndex,
                               std::uint32_t repeatIndex, StatusRecord* error);
ArenaStatus CompressRunRecording(const Catalog* catalog, const PreparedRunRequest* request, const RunPathRecord* paths,
                                 std::uint32_t selectedEngineIndex, std::uint32_t selectedThreadIndex,
                                 std::uint32_t repeatIndex, RunExecutionControl* control, InvocationContext* invocation,
                                 StatusRecord* error);
ArenaStatus WriteRunManifest(const Catalog* catalog, const ReleaseCatalog* releaseCatalog, const HostRecord* host,
                             const PreparedRunRequest* request, const RunPathRecord* paths, StatusRecord* error);
ArenaStatus PersistExecutionFailures(const Catalog* catalog, const RunPathRecord* paths,
                                      std::span<const ExecutionFailure> failures, StatusRecord* error);
}
