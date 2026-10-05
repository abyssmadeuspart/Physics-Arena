#pragma once

#include "benchmark_visual/native_app_model.h"
#include "physics_arena/run_finalization.h"
#include "physics_arena/replay.h"

#include <atomic>

namespace benchmark_visual
{
enum NativeActionPhase
{
	NativeActionPhase_Idle = 0,
	NativeActionPhase_Executing = 1,
	NativeActionPhase_Finalizing = 2,
	NativeActionPhase_Succeeded = 3,
	NativeActionPhase_Failed = 4,
	NativeActionPhase_Interrupted = 5,
	NativeActionPhase_CompletedWithFailures = 6,
};

enum NativeActionKind
{
	NativeActionKind_None = 0,
	NativeActionKind_Run = 1,
	NativeActionKind_Report = 2,
	NativeActionKind_Storage = 3,
};

struct NativeActionWorkspace
{
	physics_arena::RunFinalizationWorkspace finalization;
};

struct NativeActionState
{
	physics_arena::PreparedRunRequest request;
	physics_arena::RunPathRecord paths;
	physics_arena::RunExecutionControl control;
	physics_arena::InvocationContext invocation;
	physics_arena::EventTransport events;
	physics_arena::RunExecutionResult execution;
	physics_arena::RunFinalizationRecord finalization;
	physics_arena::StatusRecord error;
	std::array<wchar_t, physics_arena::kRunPathCapacity> resultDirectory;
	physics_arena::ReplayStorageSelection storageSelection;
	physics_arena::ReplayStorageInventory storageConfirmed;
	physics_arena::ReplayStorageResult storageResult;
	std::atomic<std::uint32_t> storagePhase;
	std::atomic<std::uint32_t> storageCompleted;
	std::atomic<std::uint32_t> storageTotal;
	NativeArenaModel* model;
	NativeActionWorkspace* workspace;
	void* threadHandle;
	NativeActionKind kind;
	std::atomic<std::uint32_t> phase;
};

static_assert(sizeof(NativeActionWorkspace) == sizeof(physics_arena::RunFinalizationWorkspace));
static_assert(sizeof(NativeActionState) < physics_arena::kWorkerStackReservationBytes / 4);

physics_arena::ArenaStatus StartNativeAction(NativeArenaModel* model, const NativeRunSelection& selection,
                                             NativeActionWorkspace* workspace, NativeActionState* action,
                                             physics_arena::StatusRecord* error);
physics_arena::ArenaStatus StartNativeReportRegeneration(NativeArenaModel* model, const wchar_t* resultDirectory,
                                                         NativeActionWorkspace* workspace, NativeActionState* action,
                                                         physics_arena::StatusRecord* error);
physics_arena::ArenaStatus StartNativeStorageDeletion(NativeArenaModel* model,
                                                      const physics_arena::ReplayStorageSelection& selection,
                                                      const physics_arena::ReplayStorageInventory& confirmed,
                                                      NativeActionWorkspace* workspace, NativeActionState* action,
                                                      physics_arena::StatusRecord* error);
NativeActionPhase PollNativeAction(NativeActionState* action, physics_arena::StatusRecord* error);
std::array<char, 192> FormatNativeStorageProgress(const NativeActionState& action);
void CancelNativeAction(NativeActionState* action);
void DestroyNativeAction(NativeActionState* action);
}
