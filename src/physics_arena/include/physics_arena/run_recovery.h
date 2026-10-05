#pragma once

#include "physics_arena/run_finalization.h"
#include "physics_arena/run_repeat_policy.h"

#include <array>
#include <cstdint>

namespace physics_arena
{
constexpr std::size_t kRecoveryTupleCapacity = kEngineCapacity * kThreadCountCapacity * kRunRepeatCapacity;

enum RecoveryFinalArtifactState
{
	RecoveryFinalArtifactState_Absent = 0,
	RecoveryFinalArtifactState_Present = 1,
};

struct RecoveryTuple
{
	std::uint32_t selectedEngineIndex;
	std::uint32_t selectedThreadIndex;
	std::uint32_t repeatIndex;
	PresenceStatus pendingCompression;
	PresenceStatus obsoleteSuppression;
};

struct RecoveryInventory
{
	std::array<RecoveryTuple, kRecoveryTupleCapacity> missingTuples;
	PreparedRunRequest request;
	std::vector<ExecutionFailure> executionFailures;
	std::vector<CompletedRepeatEvidence> completedFailures;
	std::uint32_t terminalRepeatCount;
	std::uint32_t missingRepeatCount;
	std::uint32_t completedRepeatCount;
	std::uint32_t completedUnitCount;
	std::uint32_t normalizedRowCount;
	std::uint32_t summaryRowCount;
	std::uint64_t timingRowCount;
	std::uint64_t observationRowCount;
	RecoveryFinalArtifactState finalArtifactState;
};

struct RecoveryExecutionRecord
{
	RunFinalizationRecord finalization;
	std::uint32_t startedProcessCount;
	std::uint32_t completedRepeatCount;
	std::int32_t exitCode;
	ArenaStatus status;
};

struct RecoveryWorkspace
{
	ResultManifestRecord manifest;
	RunFinalizationWorkspace finalization;
};

ArenaStatus InspectRunRecovery(const wchar_t* repositoryRoot, const Catalog* catalog,
                               const ReleaseCatalog* releaseCatalog, const HostRecord* liveHost,
                               const RunPathRecord* paths, RecoveryWorkspace* workspace, RecoveryInventory* inventory,
                               StatusRecord* error);
ArenaStatus ResumeRunRecovery(const wchar_t* repositoryRoot, const Catalog* catalog,
                              const ReleaseCatalog* releaseCatalog, const HostRecord* liveHost,
                              const RunPathRecord* paths, RunExecutionControl* control, InvocationContext* invocation,
                              RecoveryWorkspace* workspace, RecoveryInventory* inventory,
                              RecoveryExecutionRecord* record, StatusRecord* error);
}
