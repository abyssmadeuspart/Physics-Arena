#pragma once

#include "benchmark_visual/native_app_model.h"

#include <vector>

namespace benchmark_visual
{
enum NativeRunQueueState
{
	NativeRunQueueState_Idle,
	NativeRunQueueState_Armed,
	NativeRunQueueState_Stopped,
	NativeRunQueueState_Completed,
};

enum NativeRunQueueEntryState
{
	NativeRunQueueEntryState_Pending,
	NativeRunQueueEntryState_Active,
	NativeRunQueueEntryState_Completed,
	NativeRunQueueEntryState_CompletedWithFailures,
	NativeRunQueueEntryState_Interrupted,
	NativeRunQueueEntryState_Failed,
};

struct NativeRunQueueEntry
{
	NativeRunSelection selection;
	physics_arena::StatusRecord error;
	std::uint64_t id;
	NativeRunQueueEntryState state;
};

struct NativeRunQueue
{
	std::vector<NativeRunQueueEntry> entries;
	NativeRunSelection draftBeforeEdit;
	std::uint64_t nextId = 1;
	std::uint64_t selectedId, editingId, activeId;
	NativeRunQueueState state;
};

const char* NativeRunQueueEntryStateText(NativeRunQueueEntryState state);
NativeRunQueueEntry* FindNativeRunQueueEntry(NativeRunQueue* queue, std::uint64_t id);
std::uint32_t PendingNativeRunQueueCount(const NativeRunQueue& queue);
physics_arena::ArenaStatus AppendNativeRunQueue(NativeRunQueue* queue, const NativeArenaModel* model,
                                                const NativeRunSelection& selection, physics_arena::StatusRecord* error);
physics_arena::ArenaStatus AppendRecommendedNativeRuns(NativeRunQueue* queue, const NativeArenaModel* model,
                                                       physics_arena::StatusRecord* error);
physics_arena::ArenaStatus BeginNativeRunQueueEdit(NativeRunQueue* queue, NativeRunSelection* draft);
physics_arena::ArenaStatus CommitNativeRunQueueEdit(NativeRunQueue* queue, const NativeArenaModel* model,
                                                    const NativeRunSelection& draft, physics_arena::StatusRecord* error);
void CancelNativeRunQueueEdit(NativeRunQueue* queue, NativeRunSelection* draft);
physics_arena::ArenaStatus MoveNativeRunQueueEntry(NativeRunQueue* queue, int direction);
physics_arena::ArenaStatus RemoveNativeRunQueueEntry(NativeRunQueue* queue);
void ClearPendingNativeRunQueue(NativeRunQueue* queue);
physics_arena::ArenaStatus ArmNativeRunQueue(NativeRunQueue* queue, const NativeArenaModel* model,
                                             physics_arena::StatusRecord* error);
std::uint64_t NextNativeRunQueueEntry(NativeRunQueue* queue);
void CompleteNativeRunQueueEntry(NativeRunQueue* queue, NativeRunQueueEntryState state);
void StopNativeRunQueue(NativeRunQueue* queue);
}
