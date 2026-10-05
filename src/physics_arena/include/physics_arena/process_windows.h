#pragma once

#include "physics_arena/bench_types.h"

#include <array>
#include <cstdint>

namespace physics_arena
{
constexpr std::size_t kProcessPathCapacity = 1024;
constexpr std::size_t kProcessCommandCapacity = 32768;
constexpr std::size_t kProcessOutputCapacity = 8192;
constexpr std::size_t kProcessEnvironmentCapacity = 32768;

enum ProcessState
{
	ProcessState_Idle = 0,
	ProcessState_Running = 1,
	ProcessState_Exited = 2,
	ProcessState_Failed = 3,
};

enum ProcessCleanupStatus
{
	ProcessCleanupStatus_Unknown = 0,
	ProcessCleanupStatus_Empty = 1,
	ProcessCleanupStatus_Active = 2,
	ProcessCleanupStatus_QueryFailed = 3,
	ProcessCleanupStatus_TimedOut = 4,
};

struct ProcessSpec
{
	std::array<wchar_t, kProcessPathCapacity> executablePath;
	std::array<wchar_t, kProcessPathCapacity> workingDirectory;
	std::array<wchar_t, kProcessCommandCapacity> commandLine;
	std::array<wchar_t, kProcessEnvironmentCapacity> environmentBlock;
	std::uint32_t timeoutMilliseconds;
	std::uint32_t cancellationGraceMilliseconds;
	std::uint32_t environmentSize;
};

struct ProcessPollRecord
{
	std::array<char, kProcessOutputCapacity> output;
	std::array<char, kProcessOutputCapacity> errorOutput;
	std::uint32_t outputSize;
	std::uint32_t errorOutputSize;
	std::int32_t exitCode;
	ProcessState state;
	ProcessCleanupStatus cleanupStatus;
};

enum ProcessTermination
{
	ProcessTermination_Native,
	ProcessTermination_Cancelled,
	ProcessTermination_Timeout,
};

struct ProcessRecord
{
	void* processHandle;
	void* jobHandle;
	void* outputReadHandle;
	void* errorReadHandle;
	ProcessTermination termination;
	std::uint64_t startedTick;
	std::uint64_t cancellationTick;
	std::uint64_t rootExitTick;
	std::uint32_t timeoutMilliseconds;
	std::uint32_t cancellationGraceMilliseconds;
	std::uint32_t processId;
	std::int32_t exitCode;
	std::int32_t rootExitCode;
	ProcessState state;
	ProcessCleanupStatus cleanupStatus;
	PresenceStatus cancellationRequested;
	PresenceStatus rootExited;
};

static_assert(sizeof(ProcessSpec) + sizeof(ProcessPollRecord) + sizeof(ProcessRecord) <
              kWorkerStackReservationBytes / 4);

ArenaStatus StartProcess(const ProcessSpec* spec, ProcessRecord* process, StatusRecord* error);
ArenaStatus PollProcess(ProcessRecord* process, ProcessPollRecord* poll, StatusRecord* error);
ArenaStatus RequestProcessCancellation(ProcessRecord* process, StatusRecord* error);
ProcessCleanupStatus DestroyProcess(ProcessRecord* process);
ArenaStatus FinalizeProcessOwnership(ProcessRecord* process, ArenaStatus primaryStatus, StatusRecord* error);
}
