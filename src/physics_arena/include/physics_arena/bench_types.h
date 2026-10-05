#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace physics_arena
{
constexpr std::size_t kEngineCapacity = 16;
constexpr std::size_t kThreadCountCapacity = 128;
constexpr std::size_t kIdentifierCapacity = 128;
constexpr std::size_t kFixedTextCapacity = 256;
constexpr std::size_t kComponentCapacity = 64;
constexpr std::size_t kStatusCapacity = 32;
constexpr std::size_t kDetailCapacity = 768;
constexpr std::size_t kMainStackReservationBytes = 8 * 1024 * 1024;
constexpr std::size_t kWorkerStackReservationBytes = 2 * 1024 * 1024;

enum ArenaStatus
{
	ArenaStatus_Ok = 0,
	ArenaStatus_InvalidArgument = 2,
	ArenaStatus_InvalidResult = 3,
	ArenaStatus_ToolMissing = 4,
	ArenaStatus_ReferenceMismatch = 5,
	ArenaStatus_BuildFailed = 6,
	ArenaStatus_RunFailed = 7,
	ArenaStatus_Interrupted = 8,
};

enum PresenceStatus
{
	PresenceStatus_Absent = 0,
	PresenceStatus_Present = 1,
};

enum RecordingMode
{
	RecordingMode_Off = 0,
	RecordingMode_On = 1,
};

enum VerificationMode
{
	VerificationMode_On = 0,
	VerificationMode_Off = 1,
};

enum RecordingKind
{
	RecordingKind_Transforms = 0,
	RecordingKind_NativeRayHits = 1,
};

enum RecordingThreadSelection
{
	RecordingThreadSelection_All = 0,
	RecordingThreadSelection_Explicit = 1,
};

struct RecordingThreadSet
{
	std::array<std::uint32_t, kThreadCountCapacity> counts;
	std::uint32_t count;
};

inline RecordingMode RecordingForThread(const RecordingThreadSet& threads, std::uint32_t threadCount)
{
	for (std::uint32_t index = 0; index < threads.count; ++index)
		if (threads.counts[index] == threadCount)
			return RecordingMode_On;
	return RecordingMode_Off;
}

enum AvailabilityStatus
{
	AvailabilityStatus_Unknown = 0,
	AvailabilityStatus_Available = 1,
	AvailabilityStatus_Unavailable = 2,
};

struct FixedIdentifier
{
	std::array<char, kIdentifierCapacity> data;
	std::uint32_t size;
};

struct FixedText
{
	std::array<char, kFixedTextCapacity> data;
	std::uint32_t size;
};

struct StatusRecord
{
	std::array<char, kComponentCapacity> component;
	std::array<char, kStatusCapacity> status;
	std::array<char, kDetailCapacity> detail;
	std::uint32_t componentSize;
	std::uint32_t statusSize;
	std::uint32_t detailSize;
	ArenaStatus code;
};

struct RunSelection
{
	FixedIdentifier caseId;
	std::array<FixedIdentifier, kEngineCapacity> engineIds;
	std::array<std::uint32_t, kThreadCountCapacity> threadCounts;
	std::uint32_t engineCount;
	std::uint32_t threadCount;
	std::uint32_t repeatCount;
};

const char* ArenaStatusText(ArenaStatus status);
}
