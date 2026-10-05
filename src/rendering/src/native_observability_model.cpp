#include "benchmark_visual/physics_arena.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <string_view>

namespace benchmark_visual
{
physics_arena::PresenceStatus VisibleEvent(const NativeEventStore* store, const NativeEventRow& row, NativeLogView view)
{
	if (view != NativeLogView_WarningsErrors)
		return physics_arena::PresenceStatus_Present;
	const std::string_view status(store->textArena.data() + row.statusOffset, row.statusSize);
	return status == "ok" || status == "running" ? physics_arena::PresenceStatus_Absent
	                                             : physics_arena::PresenceStatus_Present;
}

void AppendClipboard(char* output, std::size_t* size, const NativeEventStore* store, const NativeEventRow& row)
{
	const std::array<std::string_view, 4> fields = {{
	    {store->textArena.data() + row.timestampOffset, row.timestampSize},
	    {store->textArena.data() + row.statusOffset, row.statusSize},
	    {store->textArena.data() + row.componentOffset, row.componentSize},
	    {store->textArena.data() + row.detailOffset, row.detailSize},
	}};
	for (std::size_t index = 0; index < fields.size(); ++index)
	{
		std::memcpy(output + *size, fields[index].data(), fields[index].size());
		*size += fields[index].size();
		output[(*size)++] = index + 1 == fields.size() ? '\n' : '\t';
	}
	output[*size] = '\0';
}

physics_arena::ArenaStatus CreateVisibleEventCopy(const PhysicsArenaApp* app, char** output,
                                                  physics_arena::StatusRecord* error)
{
	std::size_t required = 1;
	for (std::uint32_t index = 0; index < app->events->rowCount; ++index)
	{
		const NativeEventRow& row = app->events->rows[index];
		if (VisibleEvent(app->events, row, app->logView) != physics_arena::PresenceStatus_Present)
			continue;
		required += row.timestampSize + row.statusSize + row.componentSize + row.detailSize + 4;
	}
	*output = new (std::nothrow) char[required];
	if (*output == nullptr)
	{
		*error = {};
		error->code = physics_arena::ArenaStatus_RunFailed;
		constexpr std::string_view component = "activity_copy";
		constexpr std::string_view status = "allocation_failed";
		constexpr std::string_view detail = "Copy visible failed: cannot allocate complete activity text";
		std::copy(component.begin(), component.end(), error->component.begin());
		error->componentSize = static_cast<std::uint32_t>(component.size());
		std::copy(status.begin(), status.end(), error->status.begin());
		error->statusSize = static_cast<std::uint32_t>(status.size());
		std::copy(detail.begin(), detail.end(), error->detail.begin());
		error->detailSize = static_cast<std::uint32_t>(detail.size());
		return error->code;
	}
	std::size_t size = 0;
	(*output)[0] = '\0';
	for (std::uint32_t index = 0; index < app->events->rowCount; ++index)
		if (VisibleEvent(app->events, app->events->rows[index], app->logView) == physics_arena::PresenceStatus_Present)
			AppendClipboard(*output, &size, app->events, app->events->rows[index]);
	return physics_arena::ArenaStatus_Ok;
}

void FormatSelectedEvent(const PhysicsArenaApp* app, std::array<char, 65536>* output)
{
	*output = {};
	std::size_t size = 0;
	for (std::uint32_t index = 0; index < app->events->rowCount; ++index)
	{
		if (app->events->rows[index].sequence != app->selectedEventSequence)
			continue;
		AppendClipboard(output->data(), &size, app->events, app->events->rows[index]);
		break;
	}
}

void ResetNativeActivity(PhysicsArenaApp* app)
{
	app->events->rowCount = 0;
	app->events->textUsed = 0;
	app->events->droppedRowCount = 0;
	app->events->generation = 0;
	app->selectedEventSequence = 0;
	app->lastFollowGeneration = 0;
	app->followStatus = NativeFollowStatus_Following;
}

void AppendNativeEvent(NativeEventStore* store, const physics_arena::InvocationEvent* event)
{
	if (store == nullptr || event == nullptr)
		return;
	const std::uint32_t required =
	    event->timestampUtc.size + event->component.size + event->status.size + event->detail.size;
	if (required > store->textArena.size())
	{
		store->droppedRowCount += 1;
		return;
	}
	if (store->rowCount == store->rows.size() || required > store->textArena.size() - store->textUsed)
	{
		store->droppedRowCount += store->rowCount;
		store->rowCount = 0;
		store->textUsed = 0;
	}
	NativeEventRow& row = store->rows[store->rowCount++];
	row = {};
	row.sequence = event->sequence;
	const auto append = [store](const char* text, std::uint32_t size, std::uint32_t* offset)
	{
		*offset = store->textUsed;
		std::copy(text, text + size, store->textArena.begin() + store->textUsed);
		store->textUsed += size;
	};
	append(event->timestampUtc.data.data(), event->timestampUtc.size, &row.timestampOffset);
	append(event->component.data.data(), event->component.size, &row.componentOffset);
	append(event->status.data.data(), event->status.size, &row.statusOffset);
	append(event->detail.data.data(), event->detail.size, &row.detailOffset);
	row.timestampSize = event->timestampUtc.size;
	row.componentSize = event->component.size;
	row.statusSize = event->status.size;
	row.detailSize = event->detail.size;
	store->generation += 1;
}
} // namespace benchmark_visual
