#include "benchmark_visual/native_run_queue.h"
#include "benchmark_visual/native_run_presets.h"
#include "physics_arena/run_recommendations.h"

#include <algorithm>
#include <limits>

namespace benchmark_visual
{
using namespace physics_arena;

const char* NativeRunQueueEntryStateText(NativeRunQueueEntryState state)
{
	constexpr std::array<const char*, 6> names = {"Pending", "Running", "Completed", "Completed with failures", "Cancelled", "Stopped by failure"};
	return names[state];
}

NativeRunQueueEntry* FindNativeRunQueueEntry(NativeRunQueue* queue, std::uint64_t id)
{
	for (NativeRunQueueEntry& entry : queue->entries)
		if (entry.id == id)
			return &entry;
	return nullptr;
}

ArenaStatus QueueError(StatusRecord* error, std::string_view detail)
{
	*error = {};
	error->code = ArenaStatus_InvalidArgument;
	std::copy(detail.begin(), detail.end(), error->detail.begin());
	error->detailSize = static_cast<std::uint32_t>(detail.size());
	return error->code;
}

std::uint32_t PendingNativeRunQueueCount(const NativeRunQueue& queue)
{
	std::uint32_t count = 0;
	for (const NativeRunQueueEntry& entry : queue.entries)
		count += entry.state == NativeRunQueueEntryState_Pending;
	return count;
}

ArenaStatus AppendNativeRunQueueBatch(NativeRunQueue* queue, const NativeArenaModel* model,
                                    std::span<const NativeRunSelection> selections, StatusRecord* error)
{
	for (const NativeRunSelection& selection : selections)
	{
		PreparedRunRequest request = {};
		if (PrepareNativeRunRequest(model, selection, &request, error) != ArenaStatus_Ok)
			return error->code;
	}
	const std::size_t required = queue->entries.size() + selections.size();
	if (required > queue->entries.max_size() || queue->nextId > UINT64_MAX - selections.size())
		return QueueError(error, "Run queue capacity exhausted");
	if (required > queue->entries.capacity())
		queue->entries.reserve((std::max)(required, queue->entries.capacity() > queue->entries.max_size() / 2
		    ? queue->entries.max_size() : (std::max)(std::size_t(8), queue->entries.capacity() * 2)));
	for (const NativeRunSelection& selection : selections)
	{
		NativeRunQueueEntry entry = {};
		entry.id = queue->nextId++;
		entry.selection = selection;
		queue->entries.push_back(entry);
		queue->selectedId = entry.id;
	}
	return ArenaStatus_Ok;
}

ArenaStatus AppendNativeRunQueue(NativeRunQueue* queue, const NativeArenaModel* model,
                                const NativeRunSelection& selection, StatusRecord* error)
{
	return AppendNativeRunQueueBatch(queue, model, std::span(&selection, 1), error);
}

ArenaStatus AppendRecommendedNativeRuns(NativeRunQueue* queue, const NativeArenaModel* model, StatusRecord* error)
{
	RecommendedRunCases cases = {};
	if (ResolveRecommendedRunCases(&model->catalog, &cases, error) != ArenaStatus_Ok)
		return error->code;
	std::array<NativeRunSelection, kRecommendedRunCount> selections = {};
	for (std::size_t ordinal = 0; ordinal < selections.size(); ++ordinal)
	{
		if (ComposeRecommendedNativeRunSelection(model, cases.indexes[ordinal], model->selection,
		    &selections[ordinal], error) != ArenaStatus_Ok)
			return error->code;
	}
	return AppendNativeRunQueueBatch(queue, model, selections, error);
}

ArenaStatus BeginNativeRunQueueEdit(NativeRunQueue* queue, NativeRunSelection* draft)
{
	NativeRunQueueEntry* entry = FindNativeRunQueueEntry(queue, queue->selectedId);
	if (queue->editingId != 0 || entry == nullptr || entry->state != NativeRunQueueEntryState_Pending)
		return ArenaStatus_InvalidArgument;
	queue->draftBeforeEdit = *draft;
	*draft = entry->selection;
	queue->editingId = entry->id;
	return ArenaStatus_Ok;
}

ArenaStatus CommitNativeRunQueueEdit(NativeRunQueue* queue, const NativeArenaModel* model,
                                    const NativeRunSelection& draft, StatusRecord* error)
{
	NativeRunQueueEntry* entry = FindNativeRunQueueEntry(queue, queue->editingId);
	if (entry == nullptr || entry->state != NativeRunQueueEntryState_Pending)
		return QueueError(error, "No pending queued run is being edited");
	PreparedRunRequest prepared = {};
	if (PrepareNativeRunRequest(model, draft, &prepared, error) != ArenaStatus_Ok)
	{
		entry->error = *error;
		return error->code;
	}
	entry->selection = draft;
	entry->error = {};
	queue->editingId = 0;
	return ArenaStatus_Ok;
}

void CancelNativeRunQueueEdit(NativeRunQueue* queue, NativeRunSelection* draft)
{
	if (queue->editingId == 0)
		return;
	*draft = queue->draftBeforeEdit;
	queue->editingId = 0;
}

ArenaStatus MoveNativeRunQueueEntry(NativeRunQueue* queue, int direction)
{
	for (std::size_t index = 0; index < queue->entries.size(); ++index)
	{
		if (queue->entries[index].id != queue->selectedId || queue->entries[index].state != NativeRunQueueEntryState_Pending)
			continue;
		std::size_t neighbor = index;
		while ((direction < 0 && neighbor != 0) || (direction > 0 && neighbor + 1 < queue->entries.size()))
		{
			neighbor = direction < 0 ? neighbor - 1 : neighbor + 1;
			if (queue->entries[neighbor].state == NativeRunQueueEntryState_Pending)
			{
				std::swap(queue->entries[index], queue->entries[neighbor]);
				return ArenaStatus_Ok;
			}
		}
		break;
	}
	return ArenaStatus_InvalidArgument;
}

ArenaStatus RemoveNativeRunQueueEntry(NativeRunQueue* queue)
{
	for (std::size_t index = 0; index < queue->entries.size(); ++index)
		if (queue->entries[index].id == queue->selectedId && queue->selectedId != queue->editingId &&
		    queue->entries[index].state == NativeRunQueueEntryState_Pending)
		{
			queue->entries.erase(queue->entries.begin() + index);
			queue->selectedId = 0;
			return ArenaStatus_Ok;
		}
	return ArenaStatus_InvalidArgument;
}

void ClearPendingNativeRunQueue(NativeRunQueue* queue)
{
	std::erase_if(queue->entries, [queue](const NativeRunQueueEntry& entry)
	{
		return entry.state == NativeRunQueueEntryState_Pending && entry.id != queue->editingId;
	});
}

ArenaStatus ArmNativeRunQueue(NativeRunQueue* queue, const NativeArenaModel* model, StatusRecord* error)
{
	if (queue->state == NativeRunQueueState_Armed || queue->activeId != 0 || queue->editingId != 0)
		return QueueError(error, "Finish editing before starting the queue");
	if (PendingNativeRunQueueCount(*queue) == 0)
		return QueueError(error, "Add a pending run before starting the queue");
	ArenaStatus status = ArenaStatus_Ok;
	for (NativeRunQueueEntry& entry : queue->entries)
	{
		if (entry.state != NativeRunQueueEntryState_Pending)
			continue;
		PreparedRunRequest prepared = {};
		entry.error = {};
		if (PrepareNativeRunRequest(model, entry.selection, &prepared, &entry.error) != ArenaStatus_Ok)
		{
			status = entry.error.code;
			*error = entry.error;
			queue->selectedId = entry.id;
		}
	}
	if (status == ArenaStatus_Ok)
		queue->state = NativeRunQueueState_Armed;
	return status;
}

std::uint64_t NextNativeRunQueueEntry(NativeRunQueue* queue)
{
	if (queue->state != NativeRunQueueState_Armed || queue->activeId != 0)
		return 0;
	for (NativeRunQueueEntry& entry : queue->entries)
		if (entry.state == NativeRunQueueEntryState_Pending)
			return entry.id == queue->editingId ? 0 : entry.id;
	queue->state = NativeRunQueueState_Completed;
	return 0;
}

void CompleteNativeRunQueueEntry(NativeRunQueue* queue, NativeRunQueueEntryState state)
{
	std::erase_if(queue->entries, [queue](const NativeRunQueueEntry& entry)
	{
		return entry.id == queue->activeId;
	});
	if (queue->selectedId == queue->activeId)
		queue->selectedId = 0;
	queue->activeId = 0;
	if (state == NativeRunQueueEntryState_Failed || state == NativeRunQueueEntryState_Interrupted)
		StopNativeRunQueue(queue);
}

void StopNativeRunQueue(NativeRunQueue* queue)
{
	queue->state = NativeRunQueueState_Stopped;
}
}
