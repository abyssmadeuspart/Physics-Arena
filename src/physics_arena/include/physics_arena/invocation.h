#pragma once

#include "physics_arena/catalog.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace physics_arena
{
constexpr std::size_t kEventTransportCapacity = 256;
constexpr std::size_t kEventTransportTextCapacity = 131072;
constexpr std::size_t kInvocationPathCapacity = 1024;

struct InvocationPath
{
	std::array<char, kInvocationPathCapacity> data;
	std::uint32_t size;
};

struct EventTimestamp
{
	std::array<char, 32> data;
	std::uint32_t size;
};

struct EventComponent
{
	std::array<char, kComponentCapacity> data;
	std::uint32_t size;
};

struct EventStatus
{
	std::array<char, kStatusCapacity> data;
	std::uint32_t size;
};

struct EventDetail
{
	std::array<char, kDetailCapacity> data;
	std::uint32_t size;
};

struct InvocationEvent
{
	EventTimestamp timestampUtc;
	EventComponent component;
	EventStatus status;
	EventDetail detail;
	std::uint64_t sequence;
};

struct EventTransportRecord
{
	std::uint64_t sequence;
	std::uint32_t textOffset;
	std::uint32_t timestampSize;
	std::uint32_t componentSize;
	std::uint32_t statusSize;
	std::uint32_t detailSize;
};

struct EventTransport
{
	std::array<EventTransportRecord, kEventTransportCapacity> events;
	std::array<char, kEventTransportTextCapacity> textArena;
	std::atomic<std::uint32_t> writeIndex;
	std::atomic<std::uint32_t> readIndex;
	std::atomic<std::uint64_t> textWritePosition;
	std::atomic<std::uint64_t> textReadPosition;
};

struct InvocationContext
{
	InvocationPath directoryPath;
	InvocationPath logPath;
	InvocationPath eventsPath;
	InvocationPath relativeLogPath;
	void* logHandle;
	void* eventsHandle;
	std::uint64_t sequence;
	AvailabilityStatus status;
};

static_assert(sizeof(EventTransport) + sizeof(InvocationContext) < kMainStackReservationBytes / 4);

void InitializeEventTransport(EventTransport* transport);
ArenaStatus PushEvent(EventTransport* transport, const InvocationEvent* event);
ArenaStatus PopEvent(EventTransport* transport, InvocationEvent* event);

ArenaStatus StartInvocation(const wchar_t* repositoryRoot, const char* command, InvocationContext* context,
                            StatusRecord* error);
ArenaStatus AppendInvocationEvent(InvocationContext* context, const char* component, const char* status,
                                  const char* detail, InvocationEvent* event, StatusRecord* error);
ArenaStatus FinishInvocation(InvocationContext* context, std::int32_t exitCode, InvocationEvent* event,
                             StatusRecord* error);
void DestroyInvocation(InvocationContext* context);
}
