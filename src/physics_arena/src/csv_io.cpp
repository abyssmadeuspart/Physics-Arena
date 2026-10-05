#include "physics_arena/csv_io.h"

#include <csv.hpp>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>

namespace physics_arena
{
std::string_view CsvHeaderTextView(const CsvHeader* header, CsvText text)
{
	if (header == nullptr || text.offset > header->textArenaUsed || text.size > header->textArenaUsed - text.offset)
		return {};
	return std::string_view(header->textArena.data() + text.offset, text.size);
}

std::string_view CsvRowTextView(const CsvRow* row, CsvText text)
{
	if (row == nullptr || text.offset > row->textArenaUsed || text.size > row->textArenaUsed - text.offset)
		return {};
	return std::string_view(row->textArena.data() + text.offset, text.size);
}

namespace
{
template <std::size_t Capacity>
ArenaStatus CsvCopy(std::array<char, Capacity>* destination, std::uint32_t* size, std::string_view value)
{
	if (value.empty() || value.size() >= Capacity)
		return ArenaStatus_InvalidResult;
	std::fill(destination->begin(), destination->end(), '\0');
	std::copy(value.begin(), value.end(), destination->begin());
	*size = static_cast<std::uint32_t>(value.size());
	return ArenaStatus_Ok;
}

ArenaStatus CsvError(StatusRecord* error, ArenaStatus status, std::string_view detail)
{
	*error = {};
	CsvCopy(&error->component, &error->componentSize, "csv_io");
	CsvCopy(&error->status, &error->statusSize, ArenaStatusText(status));
	if (CsvCopy(&error->detail, &error->detailSize, detail) != ArenaStatus_Ok)
		CsvCopy(&error->detail, &error->detailSize, "csv_detail_exceeded_capacity");
	error->code = status;
	return status;
}

template <std::size_t Capacity>
ArenaStatus StoreCsvText(std::array<char, Capacity>* arena, std::uint32_t* used, CsvText* destination,
                         std::string_view value)
{
	if (value.size() > UINT32_MAX || value.size() > arena->size() - *used)
		return ArenaStatus_InvalidResult;
	destination->offset = *used;
	destination->size = static_cast<std::uint32_t>(value.size());
	std::copy(value.begin(), value.end(), arena->begin() + *used);
	*used += destination->size;
	return ArenaStatus_Ok;
}

ArenaStatus WidePathToUtf8(const wchar_t* path, std::array<char, 4096>* output, std::uint32_t* size)
{
	const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, output->data(),
	                                        static_cast<int>(output->size()), nullptr, nullptr);
	if (written <= 1 || written > static_cast<int>(output->size()))
		return ArenaStatus_InvalidResult;
	*size = static_cast<std::uint32_t>(written - 1);
	return ArenaStatus_Ok;
}

ArenaStatus CsvLocationError(StatusRecord* error, ArenaStatus status, const wchar_t* path, std::uint64_t row,
                             std::uint64_t column, std::string_view reason)
{
	std::array<char, 4096> pathUtf8 = {};
	std::uint32_t pathSize = 0;
	if (WidePathToUtf8(path, &pathUtf8, &pathSize) != ArenaStatus_Ok)
		return CsvError(error, status, "csv_path_capacity");
	std::array<char, kDetailCapacity> detail = {};
	const int written =
	    std::snprintf(detail.data(), detail.size(), "file=%.*s row=%llu column=%llu reason=%.*s",
		              static_cast<int>(pathSize), pathUtf8.data(), static_cast<unsigned long long>(row),
		              static_cast<unsigned long long>(column), static_cast<int>(reason.size()), reason.data());
	return written > 0 && static_cast<std::size_t>(written) < detail.size()
	           ? CsvError(error, status, std::string_view(detail.data(), static_cast<std::size_t>(written)))
			   : CsvError(error, status, "csv_location_detail_capacity");
}

ArenaStatus ValidateCsvSyntax(const wchar_t* path, StatusRecord* error)
{
	HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
	                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return CsvError(error, ArenaStatus_InvalidResult, "csv_file_open_failed");
	std::array<char, 65536> bytes = {};
	int state = 0;
	int fieldStart = 1;
	int pendingCarriageReturn = 0;
	std::uint64_t row = 1;
	std::uint64_t column = 1;
	for (;;)
	{
		DWORD read = 0;
		if (ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) == 0)
		{
			CloseHandle(file);
			return CsvError(error, ArenaStatus_InvalidResult, "csv_file_read_failed");
		}
		if (read == 0)
			break;
		for (DWORD index = 0; index < read; ++index)
		{
			const char character = bytes[index];
			const std::uint64_t currentRow = row;
			const std::uint64_t currentColumn = column;
			if (character == '\n')
			{
				++row;
				column = 1;
			}
			else
				++column;
			if (pendingCarriageReturn != 0)
			{
				if (character != '\n')
				{
					CloseHandle(file);
					return CsvLocationError(error, ArenaStatus_InvalidResult, path, currentRow, currentColumn,
					                        "csv_bare_carriage_return");
				}
				pendingCarriageReturn = 0;
				fieldStart = 1;
				continue;
			}
			if (state == 1)
			{
				if (character == '"')
					state = 2;
				continue;
			}
			if (state == 2)
			{
				if (character == '"')
				{
					state = 1;
					continue;
				}
				state = 0;
				if (character == ',')
				{
					fieldStart = 1;
					continue;
				}
				if (character == '\r')
				{
					pendingCarriageReturn = 1;
					continue;
				}
				if (character == '\n')
				{
					fieldStart = 1;
					continue;
				}
				CloseHandle(file);
				return CsvLocationError(error, ArenaStatus_InvalidResult, path, currentRow, currentColumn,
				                        "csv_char_after_quote");
			}
			if (character == '"')
			{
				if (fieldStart == 0)
				{
					CloseHandle(file);
					return CsvLocationError(error, ArenaStatus_InvalidResult, path, currentRow, currentColumn,
					                        "csv_quote_in_unquoted_field");
				}
				state = 1;
				fieldStart = 0;
			}
			else if (character == ',')
				fieldStart = 1;
			else if (character == '\r')
				pendingCarriageReturn = 1;
			else if (character == '\n')
				fieldStart = 1;
			else
				fieldStart = 0;
		}
	}
	CloseHandle(file);
	return state == 1 || pendingCarriageReturn != 0
	           ? CsvLocationError(error, ArenaStatus_InvalidResult, path, row, column, "csv_unterminated_quote_or_cr")
			   : ArenaStatus_Ok;
}

ArenaStatus ReadCsvHeader(const wchar_t* path, csv::CSVReader* reader, CsvHeader* header, StatusRecord* error)
{
	const std::vector<std::string>& columns = reader->get_col_names();
	if (columns.empty() || columns.size() > header->fields.size())
		return CsvLocationError(error, ArenaStatus_InvalidResult, path, 1, 1, "csv_header_capacity");
	for (const std::string& column : columns)
	{
		if (column.empty())
			return CsvLocationError(error, ArenaStatus_InvalidResult, path, 1, header->fieldCount + 1,
			                        "csv_header_empty");
		for (std::uint32_t prior = 0; prior < header->fieldCount; ++prior)
			if (CsvHeaderTextView(header, header->fields[prior]) == column)
				return CsvLocationError(error, ArenaStatus_InvalidResult, path, 1, header->fieldCount + 1,
				                        "csv_header_duplicate");
		if (StoreCsvText(&header->textArena, &header->textArenaUsed, &header->fields[header->fieldCount++], column) !=
		    ArenaStatus_Ok)
			return CsvError(error, ArenaStatus_InvalidResult, "csv_header_text_capacity");
	}
	return ArenaStatus_Ok;
}

ArenaStatus ConsumeCsvRow(const wchar_t* path, const CsvHeader* header, const csv::CSVRow& externalRow,
                          std::uint64_t sourceOffsetBase, CsvRowConsumer consumer, void* context, CsvReadRecord* record,
                          StatusRecord* error)
{
	if (externalRow.size() != header->fieldCount)
		return CsvLocationError(error, ArenaStatus_InvalidResult, path, record->rowCount + 2, externalRow.size() + 1,
		                        "csv_row_field_count");
	CsvRow row = {};
	row.rowNumber = record->rowCount + 1;
	row.sourceByteOffset = sourceOffsetBase + externalRow.byte_offset();
	row.fieldCount = static_cast<std::uint32_t>(externalRow.size());
	for (std::uint32_t fieldIndex = 0; fieldIndex < row.fieldCount; ++fieldIndex)
	{
		const csv::string_view value = externalRow[fieldIndex].get_sv();
		if (StoreCsvText(&row.textArena, &row.textArenaUsed, &row.fields[fieldIndex],
		                 std::string_view(value.data(), value.size())) != ArenaStatus_Ok)
			return CsvError(error, ArenaStatus_InvalidResult, "csv_row_text_capacity");
	}
	record->maximumRowTextUsed = std::max(record->maximumRowTextUsed, row.textArenaUsed);
	if (consumer(header, &row, context, error) != ArenaStatus_Ok)
		return error->code;
	record->rowCount += 1;
	return ArenaStatus_Ok;
}

ArenaStatus FlushCsv(CsvWriter* writer, StatusRecord* error)
{
	if (writer->status != ArenaStatus_Ok)
		return writer->status;
	if (writer->size == 0)
		return ArenaStatus_Ok;
	DWORD written = 0;
	if (WriteFile(static_cast<HANDLE>(writer->handle), writer->bytes.data(), writer->size, &written, nullptr) == 0 ||
	    written != writer->size)
	{
		writer->status = ArenaStatus_RunFailed;
		return CsvError(error, ArenaStatus_RunFailed, "csv_write_failed");
	}
	writer->size = 0;
	return ArenaStatus_Ok;
}

ArenaStatus AppendCsvBytes(CsvWriter* writer, std::string_view value, StatusRecord* error)
{
	for (const char character : value)
	{
		if (writer->size == writer->bytes.size() && FlushCsv(writer, error) != ArenaStatus_Ok)
			return writer->status;
		writer->bytes[writer->size++] = character;
	}
	return ArenaStatus_Ok;
}
} // namespace

ArenaStatus ReadCsvFile(const wchar_t* path, CsvHeader* header, CsvRowConsumer consumer, void* context,
                        CsvReadRecord* record, StatusRecord* error)
{
	if (header != nullptr)
		*header = {};
	if (record != nullptr)
		*record = {};
	if (error != nullptr)
		*error = {};
	if (path == nullptr || header == nullptr || consumer == nullptr || record == nullptr || error == nullptr)
		return error != nullptr ? CsvError(error, ArenaStatus_InvalidArgument, "invalid_csv_read_arguments")
		                        : ArenaStatus_InvalidArgument;
	WIN32_FILE_ATTRIBUTE_DATA attributes = {};
	if (GetFileAttributesExW(path, GetFileExInfoStandard, &attributes) == 0 ||
	    (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		return CsvError(error, ArenaStatus_InvalidResult, "csv_file_missing");
	record->sourceSize = (static_cast<std::uint64_t>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
	if (record->sourceSize == 0)
		return CsvError(error, ArenaStatus_InvalidResult, "csv_file_empty");
	if (ValidateCsvSyntax(path, error) != ArenaStatus_Ok)
		return error->code;
	std::array<char, 4096> pathUtf8 = {};
	std::uint32_t pathSize = 0;
	if (WidePathToUtf8(path, &pathUtf8, &pathSize) != ArenaStatus_Ok)
		return CsvError(error, ArenaStatus_InvalidResult, "csv_path_capacity");
	try
	{
		csv::CSVFormat format;
		format.delimiter(',')
		    .quote('"')
		    .header_row(0)
		    .variable_columns(csv::VariableColumnPolicy::THROW)
		    .threading(false);
		csv::CSVReader reader(csv::string_view(pathUtf8.data(), pathSize), format);
		if (ReadCsvHeader(path, &reader, header, error) != ArenaStatus_Ok)
			return error->code;
		for (csv::CSVRow& externalRow : reader)
			if (ConsumeCsvRow(path, header, externalRow, 0, consumer, context, record, error) != ArenaStatus_Ok)
				return error->code;
	}
	catch (const std::exception&)
	{
		*header = {};
		*record = {};
		return CsvLocationError(error, ArenaStatus_InvalidResult, path, 1, 1, "csv_parse_failed");
	}
	return ArenaStatus_Ok;
}

ArenaStatus ReadCsvFileRange(const wchar_t* path, std::uint64_t sourceByteOffset, std::uint32_t maximumRowCount,
                             CsvHeader* header, CsvRowConsumer consumer, void* context, CsvReadRecord* record,
                             StatusRecord* error)
{
	if (header != nullptr)
		*header = {};
	if (record != nullptr)
		*record = {};
	if (error != nullptr)
		*error = {};
	if (path == nullptr || sourceByteOffset == 0 || maximumRowCount == 0 || header == nullptr || consumer == nullptr ||
	    record == nullptr || error == nullptr)
		return error != nullptr ? CsvError(error, ArenaStatus_InvalidArgument, "invalid_csv_range_arguments")
		                        : ArenaStatus_InvalidArgument;
	WIN32_FILE_ATTRIBUTE_DATA attributes = {};
	if (GetFileAttributesExW(path, GetFileExInfoStandard, &attributes) == 0 ||
	    (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		return CsvError(error, ArenaStatus_InvalidResult, "csv_file_missing");
	record->sourceSize = (static_cast<std::uint64_t>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
	if (sourceByteOffset >= record->sourceSize)
		return CsvError(error, ArenaStatus_InvalidResult, "csv_range_offset");
	std::array<char, 4096> pathUtf8 = {};
	std::uint32_t pathSize = 0;
	if (WidePathToUtf8(path, &pathUtf8, &pathSize) != ArenaStatus_Ok)
		return CsvError(error, ArenaStatus_InvalidResult, "csv_path_capacity");
	try
	{
		csv::CSVFormat headerFormat;
		headerFormat.delimiter(',')
		    .quote('"')
		    .header_row(0)
		    .variable_columns(csv::VariableColumnPolicy::THROW)
		    .threading(false);
		csv::CSVReader headerReader(csv::string_view(pathUtf8.data(), pathSize), headerFormat);
		if (ReadCsvHeader(path, &headerReader, header, error) != ArenaStatus_Ok)
			return error->code;
		std::unique_ptr<std::ifstream> stream =
		    std::make_unique<std::ifstream>(std::filesystem::path(path), std::ios::binary);
		if (!*stream)
			return CsvError(error, ArenaStatus_InvalidResult, "csv_range_open_failed");
		stream->seekg(static_cast<std::streamoff>(sourceByteOffset), std::ios::beg);
		if (!*stream)
			return CsvError(error, ArenaStatus_InvalidResult, "csv_range_seek_failed");
		csv::CSVFormat rangeFormat;
		rangeFormat.delimiter(',')
		    .quote('"')
		    .no_header()
		    .variable_columns(csv::VariableColumnPolicy::THROW)
		    .threading(false);
		csv::CSVReader reader(std::move(stream), rangeFormat);
		for (csv::CSVRow& externalRow : reader)
		{
			if (ConsumeCsvRow(path, header, externalRow, sourceByteOffset, consumer, context, record, error) !=
			    ArenaStatus_Ok)
				return error->code;
			if (record->rowCount == maximumRowCount)
				break;
		}
	}
	catch (const std::exception&)
	{
		*header = {};
		*record = {};
		return CsvLocationError(error, ArenaStatus_InvalidResult, path, 1, 1, "csv_range_parse_failed");
	}
	return record->rowCount == maximumRowCount ? ArenaStatus_Ok
	                                           : CsvError(error, ArenaStatus_InvalidResult, "csv_range_cardinality");
}

ArenaStatus OpenCsvWriter(const wchar_t* path, CsvWriter* writer, StatusRecord* error)
{
	if (writer != nullptr)
		*writer = {};
	if (error != nullptr)
		*error = {};
	if (path == nullptr || writer == nullptr || error == nullptr)
		return error != nullptr ? CsvError(error, ArenaStatus_InvalidArgument, "invalid_csv_writer_arguments")
		                        : ArenaStatus_InvalidArgument;
	HANDLE file =
	    CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return CsvError(error, ArenaStatus_RunFailed, "csv_writer_open_failed");
	writer->handle = file;
	writer->status = ArenaStatus_Ok;
	return ArenaStatus_Ok;
}

ArenaStatus WriteCsvField(CsvWriter* writer, std::string_view value, CsvFieldTerminator terminator, StatusRecord* error)
{
	if (writer == nullptr || writer->handle == nullptr || error == nullptr ||
	    (terminator != CsvFieldTerminator_MoreFields && terminator != CsvFieldTerminator_EndRow))
		return error != nullptr ? CsvError(error, ArenaStatus_InvalidArgument, "invalid_csv_field_arguments")
		                        : ArenaStatus_InvalidArgument;
	int quoted = 0;
	for (const char character : value)
		if (character == ',' || character == '"' || character == '\r' || character == '\n')
			quoted = 1;
	if (quoted != 0 && AppendCsvBytes(writer, "\"", error) != ArenaStatus_Ok)
		return writer->status;
	for (const char character : value)
	{
		if (character == '"' && AppendCsvBytes(writer, "\"", error) != ArenaStatus_Ok)
			return writer->status;
		if (AppendCsvBytes(writer, std::string_view(&character, 1), error) != ArenaStatus_Ok)
			return writer->status;
	}
	if (quoted != 0 && AppendCsvBytes(writer, "\"", error) != ArenaStatus_Ok)
		return writer->status;
	if (AppendCsvBytes(writer, terminator == CsvFieldTerminator_EndRow ? "\n" : ",", error) != ArenaStatus_Ok)
		return writer->status;
	writer->fieldCount = terminator == CsvFieldTerminator_EndRow ? 0 : writer->fieldCount + 1;
	return ArenaStatus_Ok;
}

ArenaStatus FinishCsvWriter(CsvWriter* writer, StatusRecord* error)
{
	if (writer == nullptr || writer->handle == nullptr || error == nullptr)
		return error != nullptr ? CsvError(error, ArenaStatus_InvalidArgument, "invalid_csv_finish_arguments")
		                        : ArenaStatus_InvalidArgument;
	if (writer->fieldCount != 0)
		return CsvError(error, ArenaStatus_InvalidResult, "csv_incomplete_row");
	const ArenaStatus status = FlushCsv(writer, error);
	CloseHandle(static_cast<HANDLE>(writer->handle));
	writer->handle = nullptr;
	return status;
}

void DestroyCsvWriter(CsvWriter* writer)
{
	if (writer == nullptr)
		return;
	if (writer->handle != nullptr)
		CloseHandle(static_cast<HANDLE>(writer->handle));
	*writer = {};
}
} // namespace physics_arena
