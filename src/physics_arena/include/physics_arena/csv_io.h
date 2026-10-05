#pragma once

#include "physics_arena/bench_types.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace physics_arena
{
constexpr std::size_t kCsvFieldCapacity = 64;
constexpr std::size_t kCsvHeaderTextCapacity = 4096;
constexpr std::size_t kCsvRowTextCapacity = 16384;
constexpr std::size_t kCsvWriterBufferCapacity = 4096;

struct CsvText
{
	std::uint32_t offset;
	std::uint32_t size;
};

struct CsvHeader
{
	std::array<CsvText, kCsvFieldCapacity> fields;
	std::array<char, kCsvHeaderTextCapacity> textArena;
	std::uint32_t fieldCount;
	std::uint32_t textArenaUsed;
};

struct CsvRow
{
	std::array<CsvText, kCsvFieldCapacity> fields;
	std::array<char, kCsvRowTextCapacity> textArena;
	std::uint64_t sourceByteOffset;
	std::uint32_t rowNumber;
	std::uint32_t fieldCount;
	std::uint32_t textArenaUsed;
};

struct CsvReadRecord
{
	std::uint64_t sourceSize;
	std::uint32_t rowCount;
	std::uint32_t maximumRowTextUsed;
};

enum CsvFieldTerminator
{
	CsvFieldTerminator_MoreFields = 0,
	CsvFieldTerminator_EndRow = 1,
};

struct CsvWriter
{
	void* handle;
	std::array<char, kCsvWriterBufferCapacity> bytes;
	std::uint32_t size;
	std::uint32_t fieldCount;
	ArenaStatus status;
};

using CsvRowConsumer = ArenaStatus (*)(const CsvHeader*, const CsvRow*, void*, StatusRecord*);

std::string_view CsvHeaderTextView(const CsvHeader* header, CsvText text);
std::string_view CsvRowTextView(const CsvRow* row, CsvText text);
ArenaStatus ReadCsvFile(const wchar_t* path, CsvHeader* header, CsvRowConsumer consumer, void* context,
                        CsvReadRecord* record, StatusRecord* error);
ArenaStatus ReadCsvFileRange(const wchar_t* path, std::uint64_t sourceByteOffset, std::uint32_t maximumRowCount,
                             CsvHeader* header, CsvRowConsumer consumer, void* context, CsvReadRecord* record,
                             StatusRecord* error);
ArenaStatus OpenCsvWriter(const wchar_t* path, CsvWriter* writer, StatusRecord* error);
ArenaStatus WriteCsvField(CsvWriter* writer, std::string_view value, CsvFieldTerminator terminator,
                          StatusRecord* error);
ArenaStatus FinishCsvWriter(CsvWriter* writer, StatusRecord* error);
void DestroyCsvWriter(CsvWriter* writer);
}
