#pragma once

#include "physics_arena/catalog.h"

#include <array>
#include <cstdint>

namespace physics_arena
{
constexpr std::size_t kMemoryModuleCapacity = 32;
constexpr std::size_t kHostTextArenaCapacity = 16384;

enum HostMetadataSource
{
	HostMetadataSource_Unknown = 0,
	HostMetadataSource_WindowsCim = 1,
	HostMetadataSource_ProcessorTopology = 2,
};

struct HostText
{
	std::uint32_t offset;
	std::uint32_t size;
};

struct MemoryModuleRecord
{
	HostText type;
	HostText manufacturer;
	HostText partNumber;
	HostText slot;
	std::uint32_t capacityGb;
	std::uint32_t configuredClockMhz;
	std::uint32_t speedMhz;
};

struct HostRecord
{
	HostText cpuModel;
	HostText cpuTopology;
	HostText memoryType;
	HostText osName;
	HostText osVersion;
	HostText osBuild;
	HostText osArchitecture;
	HostText motherboardModel;
	HostText biosVersion;
	HostText biosDate;
	std::array<MemoryModuleRecord, kMemoryModuleCapacity> memoryModules;
	std::array<char, kHostTextArenaCapacity> textArena;
	std::uint32_t physicalCoreCount;
	std::uint32_t logicalThreadCount;
	std::uint32_t performanceCoreCount;
	std::uint32_t efficiencyCoreCount;
	std::uint32_t maxClockMhz;
	std::uint32_t currentClockMhz;
	std::uint32_t totalMemoryGb;
	std::uint32_t configuredMemoryClockMhz;
	std::uint32_t memoryModuleCount;
	HostMetadataSource cpuCountSource;
	HostMetadataSource cpuModelSource;
	HostMetadataSource memorySource;
	HostMetadataSource operatingSystemSource;
	HostMetadataSource motherboardSource;
	std::uint32_t textArenaUsed;
};

std::string_view HostTextView(const HostRecord* host, HostText text);
ArenaStatus CollectWindowsHost(HostRecord* host, StatusRecord* error);
ArenaStatus ValidateHostRecord(const HostRecord* host, StatusRecord* error);
}
