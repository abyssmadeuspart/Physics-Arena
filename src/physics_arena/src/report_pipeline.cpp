#include "report_pipeline_internal.h"

#include "physics_arena/result_pipeline.h"
#include "physics_arena/case_execution.h"
#include "physics_arena/csv_io.h"
#include "physics_arena/ray_tracing_results.h"

#include <bit>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <memory>
#include <new>

#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace physics_arena
{

std::string_view ReportRunProvenance(const ResultViewModel* model)
{
	const std::string_view mode = ResultViewTextView(model, model->benchmarkMode);
	if (mode == "headless_api")
		return "Headless, no-graphics run";
	if (model->timing.renderSeries == TimingRenderSeries_SampledFrameWall)
		return "Asynchronous live visualization; adds overhead";
	return mode == "visualized_release" ? "Release visual proof run" : mode;
}

ArenaStatus ReportError(StatusRecord* error, ArenaStatus status, std::string_view detail)
{
	*error = {};
	const std::string_view component = "report_pipeline";
	const std::string_view statusText = ArenaStatusText(status);
	std::copy(component.begin(), component.end(), error->component.begin());
	error->componentSize = static_cast<std::uint32_t>(component.size());
	std::copy(statusText.begin(), statusText.end(), error->status.begin());
	error->statusSize = static_cast<std::uint32_t>(statusText.size());
	if (detail.size() > error->detail.size())
		detail = "report_detail_capacity";
	std::copy(detail.begin(), detail.end(), error->detail.begin());
	error->detailSize = static_cast<std::uint32_t>(detail.size());
	error->code = status;
	return status;
}

ArenaStatus FlushReport(ReportWriter* writer, StatusRecord* error)
{
	if (writer->size == 0)
		return ArenaStatus_Ok;
	DWORD written = 0;
	if (WriteFile(writer->handle, writer->bytes.data(), writer->size, &written, nullptr) == 0 ||
	    written != writer->size)
		return ReportError(error, ArenaStatus_InvalidResult, "report_write_failed");
	writer->totalBytes += writer->size;
	writer->size = 0;
	return ArenaStatus_Ok;
}

ArenaStatus WriteReport(ReportWriter* writer, std::string_view text, StatusRecord* error)
{
	while (!text.empty())
	{
		if (writer->size == writer->bytes.size() && FlushReport(writer, error) != ArenaStatus_Ok)
			return error->code;
		const std::size_t count = std::min(text.size(), writer->bytes.size() - writer->size);
		std::copy(text.begin(), text.begin() + count, writer->bytes.begin() + writer->size);
		writer->size += static_cast<std::uint32_t>(count);
		text.remove_prefix(count);
	}
	return ArenaStatus_Ok;
}

ArenaStatus WriteFormat(ReportWriter* writer, StatusRecord* error, const char* format, ...)
{
	std::array<char, kReportWriterCapacity> text = {};
	va_list arguments;
	va_start(arguments, format);
	const int count = std::vsnprintf(text.data(), text.size(), format, arguments);
	va_end(arguments);
	if (count < 0 || static_cast<std::size_t>(count) >= text.size())
		return ReportError(error, ArenaStatus_InvalidResult, "report_format_capacity");
	return WriteReport(writer, std::string_view(text.data(), static_cast<std::size_t>(count)), error);
}

ArenaStatus ChildPath(const wchar_t* directory, const wchar_t* name, std::array<wchar_t, kReportPathCapacity>* path,
                      StatusRecord* error)
{
	const std::size_t directorySize = std::wcslen(directory);
	const std::size_t nameSize = std::wcslen(name);
	if (directorySize + nameSize + 2 > path->size())
		return ReportError(error, ArenaStatus_InvalidArgument, "report_path_capacity");
	std::copy(directory, directory + directorySize, path->begin());
	std::size_t size = directorySize;
	if (size != 0 && (*path)[size - 1] != L'/' && (*path)[size - 1] != L'\\')
		(*path)[size++] = L'\\';
	std::copy(name, name + nameSize, path->begin() + size);
	(*path)[size + nameSize] = L'\0';
	return ArenaStatus_Ok;
}

ArenaStatus OpenAtomicReport(const wchar_t* resultDirectory, const wchar_t* temporaryName, ReportWriter* writer,
                             std::array<wchar_t, kReportPathCapacity>* temporaryPath, StatusRecord* error)
{
	*writer = {};
	if (ChildPath(resultDirectory, temporaryName, temporaryPath, error) != ArenaStatus_Ok)
		return error->code;
	writer->handle =
	    CreateFileW(temporaryPath->data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (writer->handle == INVALID_HANDLE_VALUE)
		return ReportError(error, ArenaStatus_InvalidResult, "report_open_failed");
	return ArenaStatus_Ok;
}

ArenaStatus CommitAtomicReport(const wchar_t* resultDirectory, const wchar_t* finalName, ReportWriter* writer,
                               const std::array<wchar_t, kReportPathCapacity>& temporaryPath, StatusRecord* error)
{
	ArenaStatus status = FlushReport(writer, error);
	if (status == ArenaStatus_Ok && FlushFileBuffers(writer->handle) == 0)
		status = ReportError(error, ArenaStatus_InvalidResult, "report_flush_failed");
	CloseHandle(writer->handle);
	writer->handle = INVALID_HANDLE_VALUE;
	std::array<wchar_t, kReportPathCapacity> finalPath = {};
	if (status == ArenaStatus_Ok && ChildPath(resultDirectory, finalName, &finalPath, error) != ArenaStatus_Ok)
		status = error->code;
	if (status == ArenaStatus_Ok &&
	    MoveFileExW(temporaryPath.data(), finalPath.data(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
		status = ReportError(error, ArenaStatus_InvalidResult, "report_commit_failed");
	if (status != ArenaStatus_Ok)
		DeleteFileW(temporaryPath.data());
	return status;
}

ArenaStatus FormatCell(ReportCell* cell, const char* format, ...)
{
	std::array<char, kReportCellCapacity> text = {};
	va_list arguments;
	va_start(arguments, format);
	const int count = std::vsnprintf(text.data(), text.size(), format, arguments);
	va_end(arguments);
	if (count < 0 || static_cast<std::size_t>(count) >= text.size())
		return ArenaStatus_InvalidResult;
	return SetCell(cell, std::string_view(text.data(), static_cast<std::size_t>(count)));
}

ArenaStatus WritePadding(ReportWriter* writer, std::uint32_t count, StatusRecord* error)
{
	constexpr std::array<char, 128> spaces = []
	{
		std::array<char, 128> value = {};
		value.fill(' ');
		return value;
	}();
	while (count != 0)
	{
		const std::uint32_t part = std::min<std::uint32_t>(count, spaces.size());
		if (WriteReport(writer, std::string_view(spaces.data(), part), error) != ArenaStatus_Ok)
			return error->code;
		count -= part;
	}
	return ArenaStatus_Ok;
}

ArenaStatus WriteMarkdownRow(ReportWriter* writer, const ReportCell* cells, const std::uint32_t* widths,
                             const PresenceStatus* rightAligned, std::uint32_t count, StatusRecord* error)
{
	if (WriteReport(writer, "| ", error) != ArenaStatus_Ok)
		return error->code;
	for (std::uint32_t column = 0; column < count; ++column)
	{
		if (rightAligned[column] == PresenceStatus_Present &&
		    WritePadding(writer, widths[column] - cells[column].size, error) != ArenaStatus_Ok)
			return error->code;
		if (WriteReport(writer, CellView(cells[column]), error) != ArenaStatus_Ok)
			return error->code;
		if (rightAligned[column] == PresenceStatus_Absent &&
		    WritePadding(writer, widths[column] - cells[column].size, error) != ArenaStatus_Ok)
			return error->code;
		if (WriteReport(writer, column + 1 == count ? " |\n" : " | ", error) != ArenaStatus_Ok)
			return error->code;
	}
	return ArenaStatus_Ok;
}

ArenaStatus WriteMarkdownSeparator(ReportWriter* writer, const std::uint32_t* widths,
                                   const PresenceStatus* rightAligned, std::uint32_t count, StatusRecord* error)
{
	if (WriteReport(writer, "| ", error) != ArenaStatus_Ok)
		return error->code;
	for (std::uint32_t column = 0; column < count; ++column)
	{
		const std::uint32_t markerWidth = std::max<std::uint32_t>(widths[column], 3);
		if (rightAligned[column] == PresenceStatus_Absent && WriteReport(writer, ":", error) != ArenaStatus_Ok)
			return error->code;
		const std::uint32_t dashCount =
		    rightAligned[column] == PresenceStatus_Present ? markerWidth - 1 : markerWidth - 1;
		for (std::uint32_t dash = 0; dash < dashCount; ++dash)
			if (WriteReport(writer, "-", error) != ArenaStatus_Ok)
				return error->code;
		if (rightAligned[column] == PresenceStatus_Present && WriteReport(writer, ":", error) != ArenaStatus_Ok)
			return error->code;
		if (WriteReport(writer, column + 1 == count ? " |\n" : " | ", error) != ArenaStatus_Ok)
			return error->code;
	}
	return ArenaStatus_Ok;
}

ArenaStatus AppendCellText(ReportCell* cell, std::string_view text)
{
	if (text.size() > cell->data.size() - cell->size)
		return ArenaStatus_InvalidResult;
	std::copy(text.begin(), text.end(), cell->data.begin() + cell->size);
	cell->size += static_cast<std::uint32_t>(text.size());
	return ArenaStatus_Ok;
}

ArenaStatus AppendCellFormat(ReportCell* cell, const char* format, ...)
{
	std::array<char, 256> text = {};
	va_list arguments;
	va_start(arguments, format);
	const int count = std::vsnprintf(text.data(), text.size(), format, arguments);
	va_end(arguments);
	if (count < 0 || static_cast<std::size_t>(count) >= text.size())
		return ArenaStatus_InvalidResult;
	return AppendCellText(cell, std::string_view(text.data(), static_cast<std::size_t>(count)));
}

ArenaStatus BuildHostLabel(const HostRecord* host, ReportCell* output, PresenceStatus multiLine,
                           std::array<ReportCell, 3>* lines, std::uint32_t* lineCount)
{
	*output = {};
	*lines = {};
	*lineCount = 0;
	ReportCell cpu = {};
	if (AppendCellText(&cpu, HostTextView(host, host->cpuModel)) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	if (host->cpuTopology.size != 0 && (AppendCellText(&cpu, " (") != ArenaStatus_Ok ||
	                                    AppendCellText(&cpu, HostTextView(host, host->cpuTopology)) != ArenaStatus_Ok ||
	                                    AppendCellText(&cpu, ")") != ArenaStatus_Ok))
		return ArenaStatus_InvalidResult;
	if (host->maxClockMhz != 0 &&
	    AppendCellFormat(&cpu, "; max %.2f GHz", host->maxClockMhz / 1000.0) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	(*lines)[(*lineCount)++] = cpu;
	ReportCell memory = {};
	if (AppendCellFormat(&memory, "%u GB", host->totalMemoryGb) != ArenaStatus_Ok ||
	    (host->memoryType.size != 0 &&
	     (AppendCellText(&memory, " ") != ArenaStatus_Ok ||
	      AppendCellText(&memory, HostTextView(host, host->memoryType)) != ArenaStatus_Ok)) ||
	    (host->configuredMemoryClockMhz != 0 &&
	     AppendCellFormat(&memory, " %u MHz", host->configuredMemoryClockMhz) != ArenaStatus_Ok))
		return ArenaStatus_InvalidResult;
	(*lines)[(*lineCount)++] = memory;
	ReportCell board = {};
	if (AppendCellText(&board, HostTextView(host, host->motherboardModel)) != ArenaStatus_Ok ||
	    (host->biosVersion.size != 0 &&
	     (AppendCellText(&board, " BIOS ") != ArenaStatus_Ok ||
	      AppendCellText(&board, HostTextView(host, host->biosVersion)) != ArenaStatus_Ok)) ||
	    AppendCellText(&board, "; ") != ArenaStatus_Ok ||
	    AppendCellText(&board, HostTextView(host, host->osName)) != ArenaStatus_Ok ||
	    (host->osArchitecture.size != 0 &&
	     (AppendCellText(&board, " ") != ArenaStatus_Ok ||
	      AppendCellText(&board, HostTextView(host, host->osArchitecture)) != ArenaStatus_Ok)) ||
	    (host->osVersion.size != 0 && (AppendCellText(&board, " ") != ArenaStatus_Ok ||
	                                   AppendCellText(&board, HostTextView(host, host->osVersion)) != ArenaStatus_Ok)))
		return ArenaStatus_InvalidResult;
	(*lines)[(*lineCount)++] = board;
	for (std::uint32_t index = 0; index < *lineCount; ++index)
	{
		if (index != 0 && AppendCellText(output, "; ") != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		if (AppendCellText(output, CellView((*lines)[index])) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
	}
	(void)multiLine;
	return ArenaStatus_Ok;
}

ArenaStatus BuildThreadCounts(const ResultViewModel* model, ReportCell* output)
{
	*output = {};
	for (std::uint32_t index = 0; index < model->threadCount; ++index)
	{
		if (index != 0 && AppendCellText(output, ", ") != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		if (AppendCellFormat(output, "%u", model->threadCounts[index]) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
	}
	return ArenaStatus_Ok;
}

ArenaStatus BuildBuildLabel(const ResultViewModel* model, const ResultEngineView& engine, ReportCell* output)
{
	const std::string_view setting = ResultViewTextView(model, engine.buildSettings);
	*output = {};
	if (AppendCellText(output, setting) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	return ArenaStatus_Ok;
}

ArenaStatus AddEvidence(std::array<EvidenceRow, kEvidenceRowCapacity>* rows, std::uint32_t* count,
                        std::string_view field, std::string_view value)
{
	if (*count >= rows->size())
		return ArenaStatus_InvalidResult;
	EvidenceRow& row = (*rows)[(*count)++];
	return SetCell(&row.field, field) == ArenaStatus_Ok && SetCell(&row.value, value) == ArenaStatus_Ok
	           ? ArenaStatus_Ok
			   : ArenaStatus_InvalidResult;
}

ArenaStatus FormatThousands(std::uint32_t value, ReportCell* output);

ArenaStatus FormatSummaryCells(const ResultViewModel* model, const ResultSummaryViewRow* row,
                               std::array<ReportCell, 10>* cells)
{
	*cells = {};
	const ResultEngineView& engine = model->engines[row->engineOrdinal];
	ReportCell bodies = {};
	ReportCell shapes = {};
	ReportCell queries = {};
	ReportCell constraints = {};
	if (FormatThousands(row->bodyCount, &bodies) != ArenaStatus_Ok ||
	    FormatThousands(row->shapeCount, &shapes) != ArenaStatus_Ok ||
	    FormatThousands(row->queryCount, &queries) != ArenaStatus_Ok ||
	    FormatThousands(row->constraintCount, &constraints) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	if (SetCell(&(*cells)[0], ResultViewTextView(model, engine.displayName)) != ArenaStatus_Ok ||
	    FormatCell(&(*cells)[1], "%u", row->threadCount) != ArenaStatus_Ok ||
	    FormatCell(&(*cells)[2], "%.3f", row->medianPrimaryValue) != ArenaStatus_Ok ||
	    FormatCell(&(*cells)[3], "%.3f", row->minimumPrimaryValue) != ArenaStatus_Ok ||
	    FormatCell(&(*cells)[4], "%.3f", row->maximumPrimaryValue) != ArenaStatus_Ok ||
	    FormatCell(&(*cells)[5], "%u", row->repeatCount) != ArenaStatus_Ok ||
	    SetCell(&(*cells)[6], CellView(bodies)) != ArenaStatus_Ok ||
	    SetCell(&(*cells)[7], CellView(shapes)) != ArenaStatus_Ok ||
	    SetCell(&(*cells)[8], CellView(queries)) != ArenaStatus_Ok ||
	    SetCell(&(*cells)[9], CellView(constraints)) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	if (row->repeatCount == 0)
		for (std::uint32_t column = 2; column <= 4; ++column)
			SetCell(&(*cells)[column], "unavailable");
	return ArenaStatus_Ok;
}

ArenaStatus WriteXmlText(ReportWriter* writer, std::string_view text, StatusRecord* error)
{
	for (const char value : text)
	{
		std::string_view escaped;
		if (value == '&')
			escaped = "&amp;";
		else if (value == '<')
			escaped = "&lt;";
		else if (value == '>')
			escaped = "&gt;";
		else
		{
			const char character[] = {value};
			escaped = std::string_view(character, 1);
			if (WriteReport(writer, escaped, error) != ArenaStatus_Ok)
				return error->code;
			continue;
		}
		if (WriteReport(writer, escaped, error) != ArenaStatus_Ok)
			return error->code;
	}
	return ArenaStatus_Ok;
}

ArenaStatus FormatThousands(std::uint32_t value, ReportCell* output)
{
	*output = {};
	std::array<char, 32> digits = {};
	const std::to_chars_result result = std::to_chars(digits.data(), digits.data() + digits.size(), value);
	if (result.ec != std::errc())
		return ArenaStatus_InvalidResult;
	const std::size_t count = static_cast<std::size_t>(result.ptr - digits.data());
	for (std::size_t index = 0; index < count; ++index)
	{
		if (index != 0 && (count - index) % 3 == 0 && AppendCellText(output, ",") != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		if (AppendCellText(output, std::string_view(digits.data() + index, 1)) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
	}
	return ArenaStatus_Ok;
}

ArenaStatus PreflightReportInputs(const wchar_t* repositoryRoot, const Catalog* catalog, const ResultViewModel* model,
                                  StatusRecord* error)
{
	(void)repositoryRoot;
	if (model->engineCount == 0 || model->summaryRowCount == 0 ||
	    ValidateHostRecord(&model->host, error) != ArenaStatus_Ok)
		return ReportError(error, ArenaStatus_InvalidResult, "report_model_preflight");
	for (std::uint32_t ordinal = 0; ordinal < model->engineCount; ++ordinal)
	{
		if (model->engines[ordinal].catalogEngineIndex >= catalog->engineCount ||
		    model->engines[ordinal].colorRgb > 0xffffff)
			return ReportError(error, ArenaStatus_InvalidResult, "report_engine_preflight");
	}
	return ArenaStatus_Ok;
}
ArenaStatus AppendUtf8Path(std::array<wchar_t, kReportPathCapacity>* path, std::uint32_t* size, std::string_view text,
                           StatusRecord* error)
{
	if (*size != 0 && (*path)[*size - 1] != L'/' && (*path)[*size - 1] != L'\\')
		(*path)[(*size)++] = L'\\';
	const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
	                                      path->data() + *size, static_cast<int>(path->size() - *size - 1));
	if (count <= 0)
		return ReportError(error, ArenaStatus_InvalidResult, "report_path_encoding");
	*size += static_cast<std::uint32_t>(count);
	(*path)[*size] = L'\0';
	return ArenaStatus_Ok;
}

std::uint32_t FirstAppearanceEngines(const ResultViewModel* model, std::array<std::uint32_t, kEngineCapacity>* ordinals)
{
	std::array<PresenceStatus, kEngineCapacity> seen = {};
	std::uint32_t count = 0;
	for (std::uint32_t row = 0; row < model->summaryRowCount; ++row)
	{
		const std::uint32_t ordinal = model->summaryRows[row].engineOrdinal;
		if (seen[ordinal] == PresenceStatus_Present)
			continue;
		seen[ordinal] = PresenceStatus_Present;
		(*ordinals)[count++] = ordinal;
	}
	return count;
}

std::uint32_t BuildLegendRows(const ResultViewModel* model, std::array<LegendRowView, kEngineCapacity>* rows)
{
	std::array<std::uint32_t, kEngineCapacity> ordered = {};
	for (std::uint32_t ordinal = 0; ordinal < model->engineCount; ++ordinal)
		ordered[ordinal] = ordinal;
	std::sort(ordered.begin(), ordered.begin() + model->engineCount,
	          [model](std::uint32_t left, std::uint32_t right)
	          {
		          const ResultEngineView& leftEngine = model->engines[left];
		          const ResultEngineView& rightEngine = model->engines[right];
		          if (leftEngine.presentationOrder != rightEngine.presentationOrder)
			          return leftEngine.presentationOrder < rightEngine.presentationOrder;
		          return ResultViewTextView(model, leftEngine.id) < ResultViewTextView(model, rightEngine.id);
	          });
	constexpr std::uint32_t kLegendEntriesPerRow = 5;
	std::uint32_t rowCount = 0;
	for (std::uint32_t offset = 0; offset < model->engineCount; offset += kLegendEntriesPerRow)
	{
		LegendRowView& row = (*rows)[rowCount++];
		row.count = std::min<std::uint32_t>(kLegendEntriesPerRow, model->engineCount - offset);
		std::copy(ordered.begin() + offset, ordered.begin() + offset + row.count, row.engineOrdinals.begin());
	}
	return rowCount;
}

ArenaStatus WritePlainMeta(ReportWriter* writer, double x, double y, std::string_view text, StatusRecord* error)
{
	if (WriteFormat(writer, error,
	                "<text x=\"%.0f\" y=\"%.1f\" cl"
	                "ass=\"meta-value\"><tspan>",
	                x, y) != ArenaStatus_Ok ||
	    WriteXmlText(writer, text, error) != ArenaStatus_Ok ||
	    WriteReport(writer, "</tspan></text>\n", error) != ArenaStatus_Ok)
		return error->code;
	return ArenaStatus_Ok;
}

ArenaStatus Sentence(ReportCell* output, std::string_view text)
{
	if (SetCell(output, text.empty() ? std::string_view("not recorded") : text) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	if (output->size == 0 || (output->data[output->size - 1] != '.' && output->data[output->size - 1] != '!' &&
	                          output->data[output->size - 1] != '?'))
		return AppendCellText(output, ".");
	return ArenaStatus_Ok;
}

std::size_t Utf8CharacterSize(std::string_view text)
{
	std::size_t size = 1;
	while (size < text.size() && (static_cast<unsigned char>(text[size]) & 0xc0) == 0x80)
		++size;
	return size;
}

double MetaTextWidth(std::string_view text, MetaTextStyle style)
{
	// conservative glyph advances for the report's Inter, Segoe UI and Arial font stack
	// count UTF-8 characters, with room for wide fallback glyphs and the 650-weight labels
	double width = 0.0;
	while (!text.empty())
	{
		const unsigned char character = static_cast<unsigned char>(text.front());
		double advance = 0.68;
		if (character >= 0x80)
			advance = 2.0;
		else if (character == ' ')
			advance = 0.30;
		else if (std::string_view("ilI.,:;!'|").find(character) != std::string_view::npos)
			advance = 0.32;
		else if (std::string_view("frt()[]{}\"").find(character) != std::string_view::npos)
			advance = 0.43;
		else if (std::string_view("mwMW@%&").find(character) != std::string_view::npos)
			advance = 1.0;
		else if (character >= 'A' && character <= 'Z')
			advance = 0.79;
		else if (character >= '0' && character <= '9')
			advance = 0.65;
		else if (character == '-' || character == '/')
			advance = 0.48;
		else if (character == '=' || character == '+')
			advance = 0.84;
		width += 13.5 * advance * (style == MetaTextStyle_Key ? 1.08 : 1.0);
		text.remove_prefix(Utf8CharacterSize(text));
	}
	return width;
}

std::size_t MetaWrapSize(std::string_view text, double width, MetaTextStyle style)
{
	std::size_t size = 0;
	std::size_t space = 0;
	double used = 0.0;
	while (size < text.size())
	{
		const std::size_t characterSize = Utf8CharacterSize(text.substr(size));
		const double advance = MetaTextWidth(text.substr(size, characterSize), style);
		if (used + advance > width)
			break;
		used += advance;
		size += characterSize;
		if (text[size - 1] == ' ')
			space = size;
	}
	return size == text.size() || space == 0 ? size : space;
}

std::uint32_t MetaTextRows(std::string_view remaining)
{
	std::uint32_t rows = 0;
	do
	{
		remaining.remove_prefix(MetaWrapSize(remaining, 1066.0, MetaTextStyle_Value));
		++rows;
	} while (!remaining.empty());
	return rows;
}

ArenaStatus BuildReportCaseMeta(const Catalog* catalog, const ResultViewModel* model, const ResultSummaryViewRow& row,
                                ReportCaseMeta* output, StatusRecord* error)
{
	*output = {};
	if (AppendCellText(&output->description, ResultViewTextView(model, model->caseDescription)) != ArenaStatus_Ok)
		return ReportError(error, ArenaStatus_InvalidResult, "report_description_capacity");
	const EffectiveRunConfiguration* configuration = ResultViewConfiguration(model);
	if (configuration == nullptr && model->descriptiveCasePresence == PresenceStatus_Present) return ArenaStatus_Ok;
	const CaseRecord& benchmarkCase = ResultViewCaseDefinition(catalog, model);
	CaseExecutionSpec spec = {};
	if (configuration != nullptr)
		spec = configuration->execution;
	else if (DecodeCatalogCaseExecution(catalog, model->caseIndex, &spec, error) != ArenaStatus_Ok)
		return error->code;
	PresenceStatus describedConstraints = PresenceStatus_Absent;
	// the admitted narrative and typed fixture share the catalog owner, without parsing prose
	if ((configuration != nullptr && configuration->mode == RunConfigurationMode_Custom) ||
	    CellView(output->description) == CaseConfigurationTextView(catalog, configuration, benchmarkCase.description))
	{
		if (benchmarkCase.shapePreset != CaseShapePreset_Authored ||
		    (configuration != nullptr && configuration->mode == RunConfigurationMode_Custom))
		{
			ArenaStatus status = ArenaStatus_Ok;
			if (benchmarkCase.fixtureKind == CaseFixtureKind_OpenContainerFallingPile)
				status = AppendCellFormat(&output->description,
				                          " %u falling dynamic bodies inside a %u-static-body open container",
				                          spec.dynamicBodyCount, spec.staticBodyCount);
			else if (benchmarkCase.fixtureKind == CaseFixtureKind_BoxContactIslands)
				status = AppendCellFormat(&output->description, " %u independent contact islands of %u dynamic bodies",
				                          spec.contactIslands.islandGrid[0] * spec.contactIslands.islandGrid[1],
				                          spec.contactIslands.bodyGrid[0] * spec.contactIslands.bodyGrid[1] *
				                              spec.contactIslands.bodyGrid[2]);
			else if (benchmarkCase.fixtureKind == CaseFixtureKind_SpatialQueryTrace)
				status = AppendCellFormat(
				    &output->description,
				    " Runs %u closest rays, %u closest radius-%.2g sphere casts, and %u AABB overlap presence queries per batch",
				    spec.spatialQuery.rayCount, spec.spatialQuery.sphereCastCount,
				    static_cast<double>(spec.spatialQuery.sphereCastRadius), spec.spatialQuery.overlapCount);
			else if (benchmarkCase.fixtureKind == CaseFixtureKind_RagdollStairTumble)
			{
				ReportCell joints = {};
				FormatThousands(spec.ragdoll.ragdollGrid[0] * spec.ragdoll.ragdollGrid[1] * spec.ragdoll.linkCount,
				                &joints);
				status = AppendCellFormat(
				    &output->description,
				    " %u ragdolls with %u parts each tumble down a %u-step staircase using %.*s ball-and-socket joints",
				    spec.ragdoll.ragdollGrid[0] * spec.ragdoll.ragdollGrid[1], spec.ragdoll.partCount,
				    spec.ragdoll.stairCount, static_cast<int>(joints.size), joints.data.data());
			}
			if (status != ArenaStatus_Ok)
				return ReportError(error, status, "report_description_capacity");
		}
		if (benchmarkCase.fixtureKind == CaseFixtureKind_PyramidWall)
			output->fixtureFacts[output->fixtureFactCount++] = {"wall_rows", 90};
		if (benchmarkCase.fixtureKind == CaseFixtureKind_RayTracing)
		{
			output->fixtureFacts[output->fixtureFactCount++] = {"primary_rays", 2073600};
			output->fixtureFacts[output->fixtureFactCount++] = {"meshes", 1024};
			output->fixtureFacts[output->fixtureFactCount++] = {"triangles", 1048576};
			output->fixtureFacts[output->fixtureFactCount++] = {"kinematic", 8192};
		}
		if (benchmarkCase.fixtureKind == CaseFixtureKind_BoxContactIslands)
			output->fixtureFacts[output->fixtureFactCount++] = {"islands", spec.contactIslands.islandGrid[0] *
			                                                                   spec.contactIslands.islandGrid[1]};
		if (benchmarkCase.fixtureKind == CaseFixtureKind_SpatialQueryTrace)
		{
			output->fixtureFacts[output->fixtureFactCount++] = {"rays", spec.spatialQuery.rayCount};
			output->fixtureFacts[output->fixtureFactCount++] = {"sphere_casts", spec.spatialQuery.sphereCastCount};
			output->fixtureFacts[output->fixtureFactCount++] = {"overlaps", spec.spatialQuery.overlapCount};
		}
		if (benchmarkCase.fixtureKind == CaseFixtureKind_RagdollStairTumble &&
		    row.constraintCount == spec.ragdoll.ragdollGrid[0] * spec.ragdoll.ragdollGrid[1] * spec.ragdoll.linkCount)
			describedConstraints = PresenceStatus_Present;
	}
	ReportCell body = {};
	ReportCell shape = {};
	if (FormatThousands(row.bodyCount, &body) != ArenaStatus_Ok ||
	    FormatThousands(row.shapeCount, &shape) != ArenaStatus_Ok ||
	    AppendCellText(&output->caseLine, ResultViewTextView(model, model->caseDisplayName)) != ArenaStatus_Ok ||
	    AppendCellText(&output->caseLine, "; ") != ArenaStatus_Ok ||
	    AppendCellText(&output->caseLine, CellView(body)) != ArenaStatus_Ok ||
	    AppendCellText(&output->caseLine, " bodies, ") != ArenaStatus_Ok ||
	    AppendCellText(&output->caseLine, CellView(shape)) != ArenaStatus_Ok ||
	    AppendCellText(&output->caseLine, " shapes") != ArenaStatus_Ok ||
	    (row.queryCount != 0 &&
	     AppendCellFormat(&output->caseLine, ", %u queries", row.queryCount) != ArenaStatus_Ok) ||
	    (row.constraintCount != 0 && describedConstraints == PresenceStatus_Absent &&
	     AppendCellFormat(&output->caseLine, ", %u constraints", row.constraintCount) != ArenaStatus_Ok))
		return ReportError(error, ArenaStatus_InvalidResult, "report_case_capacity");
	return ArenaStatus_Ok;
}

ArenaStatus BuildReportRunMeta(const ResultViewModel* model, std::span<const std::uint32_t> threads, ReportCell* output)
{
	if (AppendCellText(output, ReportRunProvenance(model)) != ArenaStatus_Ok ||
	    AppendCellText(output, "; ") != ArenaStatus_Ok ||
	    AppendCellText(output, ResultViewTextView(model, model->releaseSourceLabel)) != ArenaStatus_Ok ||
	    AppendCellFormat(output, "; %u %s; %s ", model->repeatCount, model->repeatCount == 1 ? "repeat" : "repeats",
	                     threads.size() == 1 && threads[0] == 1 ? "thread" : "threads") != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	for (std::size_t index = 0; index < threads.size(); ++index)
		if ((index != 0 && AppendCellText(output, ", ") != ArenaStatus_Ok) ||
		    AppendCellFormat(output, "%u", threads[index]) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
	const std::string_view workUnit = ResultViewTextView(model, model->workUnitLabel);
	if (AppendCellFormat(output, "; %u measured ", model->measuredWorkUnitCount) != ArenaStatus_Ok ||
	    AppendCellText(output, workUnit) != ArenaStatus_Ok ||
	    (model->measuredWorkUnitCount != 1 &&
	     AppendCellText(output, workUnit.ends_with("batch") ? "es" : "s") != ArenaStatus_Ok) ||
	    AppendCellFormat(output, " + %u warmup", model->warmupWorkUnitCount) != ArenaStatus_Ok ||
	    (model->timestepHz != 0 && AppendCellFormat(output, "; %u Hz", model->timestepHz) != ArenaStatus_Ok))
		return ArenaStatus_InvalidResult;
	return ArenaStatus_Ok;
}

struct PhysicsSettingView
{
	std::string_view key;
	std::string_view value;
};

PhysicsSettingView NextPhysicsSetting(std::string_view* remaining)
{
	const std::size_t separator = remaining->find(';');
	std::string_view token = remaining->substr(0, separator);
	remaining->remove_prefix(separator == std::string_view::npos ? remaining->size() : separator + 1);
	while (!token.empty() && token.front() == ' ')
		token.remove_prefix(1);
	while (!token.empty() && token.back() == ' ')
		token.remove_suffix(1);
	const std::size_t equals = token.find('=');
	return equals == std::string_view::npos ? PhysicsSettingView{{}, token}
	                                        : PhysicsSettingView{token.substr(0, equals), token.substr(equals + 1)};
}

std::size_t PhysicsFactCapacity(const ResultViewModel* model, std::span<const std::uint32_t> engines)
{
	std::size_t capacity = 0;
	for (const std::uint32_t ordinal : engines)
	{
		const std::string_view settings = ResultViewTextView(model, model->engines[ordinal].physicsSettings);
		capacity += 1 + std::count(settings.begin(), settings.end(), ';');
	}
	return capacity;
}

ArenaStatus BuildPhysicsMeta(const Catalog* catalog, const ResultViewModel* model,
                             const std::array<std::uint32_t, kEngineCapacity>& appearance,
                             std::uint32_t appearanceCount, std::uint32_t filterThreadCount,
                             const ReportCaseMeta& caseMeta, PhysicsMetaProjection* projection)
{
	projection->factCount = 0;
	projection->engineCount = appearanceCount;
	std::copy_n(appearance.begin(), appearanceCount, projection->engines.begin());
	std::sort(projection->engines.begin(), projection->engines.begin() + appearanceCount,
	          [model](std::uint32_t left, std::uint32_t right)
	          {
		          const ResultEngineView& a = model->engines[left];
		          const ResultEngineView& b = model->engines[right];
		          if (a.presentationOrder != b.presentationOrder)
			          return a.presentationOrder < b.presentationOrder;
		          return ResultViewTextView(model, a.id) < ResultViewTextView(model, b.id);
	          });
	for (std::uint32_t index = 0; index < appearanceCount; ++index)
	{
		const ResultEngineView& engine = model->engines[projection->engines[index]];
		std::string_view remaining = ResultViewTextView(model, engine.physicsSettings);
		while (!remaining.empty())
		{
			PhysicsSettingView setting = NextPhysicsSetting(&remaining);
			if (setting.key.empty() && setting.value.empty())
				continue;
			PresenceStatus fixtureMatch = PresenceStatus_Absent;
			for (std::uint32_t fixture = 0; fixture < caseMeta.fixtureFactCount; ++fixture)
			{
				if (setting.key != caseMeta.fixtureFacts[fixture].key)
					continue;
				std::uint32_t count = 0;
				const std::from_chars_result parsed =
				    std::from_chars(setting.value.data(), setting.value.data() + setting.value.size(), count);
				if (parsed.ec == std::errc() && parsed.ptr == setting.value.data() + setting.value.size() &&
				    count == caseMeta.fixtureFacts[fixture].value)
					fixtureMatch = PresenceStatus_Present;
			}
			if (fixtureMatch == PresenceStatus_Present)
				continue;
			if (setting.key == "sleep_threshold" && setting.value == "0")
				setting = {"sleep", "disabled"};
			// Unity Physics 6.5 PhysicsStep.Default uses four solver iterations
			if (setting.key == "solver" && setting.value == "PhysicsStep_default" &&
			    ResultViewTextView(model, engine.id) == "unity_physics" &&
			    ResultViewTextView(model, engine.reportVersion) == "6.5")
				setting.value = "4 iterations (PhysicsStep default)";
			if (setting.key == "worker_count")
			{
				const WorkerCountPolicy policy = catalog->engines[engine.catalogEngineIndex].effectiveWorkerPolicy;
				setting.key = "workers";
				if (filterThreadCount != 0 || model->threadCount == 1)
				{
					const std::uint32_t thread = filterThreadCount != 0 ? filterThreadCount : model->threadCounts[0];
					std::array<char, 10>& worker = projection->workerValues[index];
					const std::to_chars_result formatted = std::to_chars(worker.data(), worker.data() + worker.size(),
					                                                     EffectiveWorkerCount(policy, thread));
					setting.value = std::string_view(worker.data(), formatted.ptr - worker.data());
				}
				else
					setting.value = policy == WorkerCountPolicy_ThreadCount ? "threads" : "threads - 1";
			}
			if (projection->factCount == projection->facts.size())
				return ArenaStatus_InvalidResult;
			std::construct_at(&projection->facts[projection->factCount++],
			                  PhysicsFact{setting.key, setting.value, 1U << index});
		}
	}
	const std::span<PhysicsFact> facts = projection->facts.first(projection->factCount);
	std::sort(facts.begin(), facts.end(),
	          [](const PhysicsFact& left, const PhysicsFact& right)
	          {
		          return left.key != right.key ? left.key < right.key : left.value < right.value;
	          });
	std::uint32_t uniqueCount = 0;
	for (const PhysicsFact& fact : facts)
	{
		if (uniqueCount != 0 && facts[uniqueCount - 1].key == fact.key && facts[uniqueCount - 1].value == fact.value)
			facts[uniqueCount - 1].engineMask |= fact.engineMask;
		else
			facts[uniqueCount++] = fact;
	}
	projection->factCount = uniqueCount;
	return ArenaStatus_Ok;
}

struct MetaLayout
{
	ReportWriter* writer;
	StatusRecord* error;
	double y;
	double width;
	std::uint32_t rows;
	PresenceStatus lineStart;
	ArenaStatus status;
};

void NextMetaLine(MetaLayout* layout)
{
	layout->y += 18.0;
	layout->width = 0.0;
	++layout->rows;
	layout->lineStart = PresenceStatus_Present;
}

void LayoutMetaToken(MetaLayout* layout, std::string_view token, MetaTextStyle style)
{
	const double width = MetaTextWidth(token, style);
	double available = 841.0 - layout->width;
	if (layout->width != 0.0 && width > available)
	{
		NextMetaLine(layout);
		available = 841.0;
	}
	while (!token.empty())
	{
		const std::size_t size = MetaWrapSize(token, available, style);
		const std::string_view part = token.substr(0, size);
		if (layout->writer != nullptr && layout->status == ArenaStatus_Ok)
		{
			if (layout->lineStart == PresenceStatus_Present)
				layout->status = WriteFormat(layout->writer, layout->error, "<tspan x=\"375\" y=\"%.1f\"%s>", layout->y,
				                             style == MetaTextStyle_Key ? " class=\"meta-key\"" : "");
			else
				layout->status =
				    WriteReport(layout->writer, style == MetaTextStyle_Key ? "<tspan class=\"meta-key\">" : "<tspan>",
					            layout->error);
			if (layout->status == ArenaStatus_Ok)
				layout->status = WriteXmlText(layout->writer, part, layout->error);
			if (layout->status == ArenaStatus_Ok)
				layout->status = WriteReport(layout->writer, "</tspan>", layout->error);
		}
		layout->lineStart = PresenceStatus_Absent;
		layout->width += MetaTextWidth(part, style);
		token.remove_prefix(size);
		if (!token.empty())
		{
			NextMetaLine(layout);
			available = 841.0;
		}
	}
}

ArenaStatus PhysicsFactText(const PhysicsFact& fact, std::string_view suffix, std::span<char> buffer,
                            std::string_view* output)
{
	const std::size_t size = fact.key.size() + (fact.key.empty() ? 0 : 1) + fact.value.size() + suffix.size();
	if (size > buffer.size())
		return ArenaStatus_InvalidResult;
	char* cursor = buffer.data();
	cursor = std::copy(fact.key.begin(), fact.key.end(), cursor);
	if (!fact.key.empty())
		*cursor++ = '=';
	cursor = std::copy(fact.value.begin(), fact.value.end(), cursor);
	std::copy(suffix.begin(), suffix.end(), cursor);
	*output = std::string_view(buffer.data(), size);
	return ArenaStatus_Ok;
}

ArenaStatus LayoutPhysicsMeta(ReportWriter* writer, double y, const ResultViewModel* model,
                              const PhysicsMetaProjection& projection, std::uint32_t* rows, StatusRecord* error)
{
	*rows = 0;
	if (projection.factCount == 0) return ArenaStatus_Ok;
	if (writer != nullptr && WriteFormat(writer, error,
	    "<text x=\"150\" y=\"%.1f\" class=\"meta-label\">Engine</text>"
	    "<text x=\"375\" y=\"%.1f\" class=\"meta-label\">Settings</text>\n", y, y) != ArenaStatus_Ok)
		return error->code;
	++*rows;
	y += 18.0;
	std::array<char, kCsvRowTextCapacity + 3> buffer;
	for (std::uint32_t engine = 0; engine < projection.engineCount; ++engine)
	{
		std::uint32_t remaining = 0;
		for (const PhysicsFact& fact : projection.facts.first(projection.factCount))
			remaining += (fact.engineMask & (1U << engine)) != 0 ? 1U : 0U;
		if (remaining == 0) continue;
		MetaLayout layout = {writer, error, y, 0.0, 1, PresenceStatus_Present, ArenaStatus_Ok};
		std::string_view name = ResultViewTextView(model, model->engines[projection.engines[engine]].displayName);
		std::uint32_t nameRows = 0;
		if (writer != nullptr)
			layout.status = WriteFormat(writer, error,
			    "<g class=\"physics-engine\"><text x=\"150\" y=\"%.1f\" class=\"meta-value\" xml:space=\"preserve\">", y);
		do
		{
			const std::size_t size = MetaWrapSize(name, 210.0, MetaTextStyle_Key);
			if (writer != nullptr && layout.status == ArenaStatus_Ok)
			{
				layout.status = WriteFormat(writer, error, "<tspan x=\"150\" y=\"%.1f\" class=\"meta-key\">", y + 18.0 * nameRows);
				if (layout.status == ArenaStatus_Ok) layout.status = WriteXmlText(writer, name.substr(0, size), error);
				if (layout.status == ArenaStatus_Ok) layout.status = WriteReport(writer, "</tspan>", error);
			}
			name.remove_prefix(size);
			++nameRows;
		} while (!name.empty());
		if (writer != nullptr && layout.status == ArenaStatus_Ok)
			layout.status = WriteFormat(writer, error,
			    "</text><text x=\"375\" y=\"%.1f\" class=\"meta-value\" xml:space=\"preserve\">", y);
		for (const PhysicsFact& fact : projection.facts.first(projection.factCount))
		{
			if ((fact.engineMask & (1U << engine)) == 0) continue;
			std::string_view text;
			if (PhysicsFactText(fact, --remaining != 0 ? ", " : "", buffer, &text) != ArenaStatus_Ok)
				return ReportError(error, ArenaStatus_InvalidResult, "report_physics_clause_capacity");
			LayoutMetaToken(&layout, text, MetaTextStyle_Value);
		}
		if (layout.status != ArenaStatus_Ok)
			return layout.status;
		if (writer != nullptr && WriteReport(writer, "</text></g>\n", error) != ArenaStatus_Ok)
			return error->code;
		const std::uint32_t engineRows = std::max(layout.rows, nameRows);
		*rows += engineRows;
		y += 18.0 * engineRows;
	}
	return ArenaStatus_Ok;
}

ArenaStatus CountPhysicsMetaRows(const ResultViewModel* model, const PhysicsMetaProjection& projection,
                                 std::uint32_t* rows, StatusRecord* error)
{
	return LayoutPhysicsMeta(nullptr, 0.0, model, projection, rows, error);
}

ArenaStatus WritePhysicsMeta(ReportWriter* writer, double y, const ResultViewModel* model,
                             const PhysicsMetaProjection& projection, StatusRecord* error)
{
	std::uint32_t rows = 0;
	return LayoutPhysicsMeta(writer, y, model, projection, &rows, error);
}

ArenaStatus WritePhysicsMarkdownText(ReportWriter* writer, std::string_view text, StatusRecord* error)
{
	for (const char character : text)
	{
		if (std::string_view("\\|*`_").find(character) != std::string_view::npos &&
		    WriteReport(writer, "\\", error) != ArenaStatus_Ok)
			return error->code;
		if (WriteXmlText(writer, std::string_view(&character, 1), error) != ArenaStatus_Ok)
			return error->code;
	}
	return ArenaStatus_Ok;
}

ArenaStatus WritePhysicsMarkdown(ReportWriter* writer, const ResultViewModel* model,
                                 const PhysicsMetaProjection& projection, StatusRecord* error)
{
	if (projection.factCount == 0) return ArenaStatus_Ok;
	if (WriteReport(writer, "\n## Physics settings\n\n| Engine | Settings |\n| --- | --- |\n", error) != ArenaStatus_Ok)
		return error->code;
	for (std::uint32_t engine = 0; engine < projection.engineCount; ++engine)
	{
		std::uint32_t remaining = 0;
		for (const PhysicsFact& fact : projection.facts.first(projection.factCount))
			remaining += (fact.engineMask & (1U << engine)) != 0 ? 1U : 0U;
		if (remaining == 0) continue;
		if (WriteReport(writer, "| ", error) != ArenaStatus_Ok ||
		    WritePhysicsMarkdownText(writer, ResultViewTextView(model, model->engines[projection.engines[engine]].displayName), error) != ArenaStatus_Ok ||
		    WriteReport(writer, " | ", error) != ArenaStatus_Ok)
			return error->code;
		PresenceStatus first = PresenceStatus_Present;
		for (const PhysicsFact& fact : projection.facts.first(projection.factCount))
		{
			if ((fact.engineMask & (1U << engine)) == 0) continue;
			if ((first == PresenceStatus_Absent && WriteReport(writer, ", ", error) != ArenaStatus_Ok) ||
			    (!fact.key.empty() && (WritePhysicsMarkdownText(writer, fact.key, error) != ArenaStatus_Ok ||
			                           WriteReport(writer, "=", error) != ArenaStatus_Ok)) ||
			    WritePhysicsMarkdownText(writer, fact.value, error) != ArenaStatus_Ok)
				return error->code;
			first = PresenceStatus_Absent;
		}
		if (WriteReport(writer, " |\n", error) != ArenaStatus_Ok)
			return error->code;
	}
	return ArenaStatus_Ok;
}

ArenaStatus WriteWrappedMeta(ReportWriter* writer, double x, double y, std::string_view remaining, StatusRecord* error)
{
	do
	{
		const std::size_t size = MetaWrapSize(remaining, 1216.0 - x, MetaTextStyle_Value);
		if (WritePlainMeta(writer, x, y, remaining.substr(0, size), error) != ArenaStatus_Ok)
			return error->code;
		remaining.remove_prefix(size);
		y += 18.0;
	} while (!remaining.empty());
	return ArenaStatus_Ok;
}

ArenaStatus WriteBuildMeta(ReportWriter* writer, double x, double y, const ResultViewModel* model,
                           std::uint32_t engineOrdinal, StatusRecord* error)
{
	ReportCell build = {};
	if (BuildBuildLabel(model, model->engines[engineOrdinal], &build) != ArenaStatus_Ok)
		return ReportError(error, ArenaStatus_InvalidResult, "summary_svg_build_label");
	if (WriteFormat(writer, error,
	                "<text x=\"%.0f\" y=\"%.1f\" cl"
	                "ass=\"meta-value\"><tspan cl"
	                "ass=\"meta-key\">",
	                x, y) != ArenaStatus_Ok ||
	    WriteXmlText(writer, ResultViewTextView(model, model->engines[engineOrdinal].displayName), error) !=
	        ArenaStatus_Ok ||
	    WriteReport(writer, ":</tspan><tspan> ", error) != ArenaStatus_Ok ||
	    WriteXmlText(writer, CellView(build), error) != ArenaStatus_Ok ||
	    WriteReport(writer, "</tspan></text>\n", error) != ArenaStatus_Ok)
		return error->code;
	return ArenaStatus_Ok;
}

ArenaStatus AppendWideChild(std::array<wchar_t, kReportPathCapacity>* path, const wchar_t* child)
{
	std::size_t size = std::wcslen(path->data());
	const std::size_t childSize = std::wcslen(child);
	if (size + childSize + 2 > path->size())
		return ArenaStatus_InvalidResult;
	if (size != 0 && (*path)[size - 1] != L'/' && (*path)[size - 1] != L'\\')
		(*path)[size++] = L'\\';
	std::copy(child, child + childSize, path->begin() + size);
	(*path)[size + childSize] = L'\0';
	return ArenaStatus_Ok;
}

ArenaStatus CopyWidePath(std::array<wchar_t, kReportPathCapacity>* path, const wchar_t* source)
{
	const std::size_t size = std::wcslen(source);
	if (size + 1 > path->size())
		return ArenaStatus_InvalidResult;
	std::copy(source, source + size + 1, path->begin());
	return ArenaStatus_Ok;
}

double PositiveAxisCeiling(double observedMaximum)
{
	if (!std::isfinite(observedMaximum) || observedMaximum <= 0.0)
		return 1.0;
	const double magnitude = std::pow(10.0, std::floor(std::log10(observedMaximum)));
	const double normalized = observedMaximum / magnitude;
	for (const double factor : {1.0, 1.2, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0})
	{
		if (normalized <= factor)
			return factor * magnitude;
	}
	return 10.0 * magnitude;
}

namespace
{
ArenaStatus LoadResultPresentationFromSummary(const wchar_t* repositoryRoot, const wchar_t* resultDirectory,
                                              const wchar_t* summaryPath, const Catalog* catalog,
                                              ResultManifestRecord* manifestWorkspace, ResultViewModel* modelWorkspace,
                                              StatusRecord* error)
{
	std::array<wchar_t, kReportPathCapacity> manifestPath = {};
	std::array<wchar_t, kReportPathCapacity> normalizedPath = {};
	std::array<wchar_t, kReportPathCapacity> timingPath = {};
	if (ChildPath(resultDirectory, L"manifest.json", &manifestPath, error) != ArenaStatus_Ok ||
	    ChildPath(resultDirectory, L"normalized.csv", &normalizedPath, error) != ArenaStatus_Ok ||
	    LoadResultManifest(manifestPath.data(), catalog, manifestWorkspace, error) != ArenaStatus_Ok)
		return error->code;
	if (ValidateSavedRayResults(resultDirectory, catalog, manifestWorkspace, error) != ArenaStatus_Ok ||
	    ChildPath(resultDirectory, L"step-timing.csv", &timingPath, error) != ArenaStatus_Ok)
		return error->code;
	if (manifestWorkspace->measurementMode == ResultMeasurementMode_PhysicalQuality)
	{
		std::array<wchar_t, kReportPathCapacity> timingChart = {};
		if (ChildPath(resultDirectory, L"step-timing.svg", &timingChart, error) != ArenaStatus_Ok)
			return error->code;
		if (GetFileAttributesW(timingChart.data()) != INVALID_FILE_ATTRIBUTES)
			return ReportError(error, ArenaStatus_InvalidResult, "quality_run_contains_timing");
	}
	std::unique_ptr<TimingArtifactTotals> totals(new (std::nothrow) TimingArtifactTotals{});
	if (totals == nullptr)
		return ReportError(error, ArenaStatus_RunFailed, "presentation_timing_workspace");
	if (LoadResultViewModel(summaryPath, normalizedPath.data(), timingPath.data(), catalog, manifestWorkspace,
	                        modelWorkspace, error, totals.get()) != ArenaStatus_Ok ||
	    ValidateResultAgreement(normalizedPath.data(), catalog, manifestWorkspace, modelWorkspace, totals.get(),
	                            error) != ArenaStatus_Ok ||
	    PreflightReportInputs(repositoryRoot, catalog, modelWorkspace, error) != ArenaStatus_Ok)
	{
		std::destroy_at(modelWorkspace);
		std::construct_at(modelWorkspace);
		return error->code;
	}
	return ArenaStatus_Ok;
}
} // namespace

ArenaStatus LoadResultPresentation(const wchar_t* repositoryRoot, const wchar_t* resultDirectory,
                                   const Catalog* catalog, ResultManifestRecord* manifestWorkspace,
                                   ResultViewModel* modelWorkspace, StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (repositoryRoot == nullptr || resultDirectory == nullptr || catalog == nullptr || manifestWorkspace == nullptr ||
	    modelWorkspace == nullptr || error == nullptr)
		return error != nullptr ? ReportError(error, ArenaStatus_InvalidArgument, "result_presentation_argument")
		                        : ArenaStatus_InvalidArgument;
	std::array<wchar_t, kReportPathCapacity> summaryPath = {};
	if (ChildPath(resultDirectory, L"summary.csv", &summaryPath, error) != ArenaStatus_Ok)
		return error->code;
	return LoadResultPresentationFromSummary(repositoryRoot, resultDirectory, summaryPath.data(), catalog,
	                                         manifestWorkspace, modelWorkspace, error);
}

ArenaStatus RegenerateResultReports(const wchar_t* repositoryRoot, const wchar_t* resultDirectory,
                                    const Catalog* catalog, ResultManifestRecord* manifestWorkspace,
                                    ResultViewModel* modelWorkspace, TimingProjectionScratch* timingScratch,
                                    ResultIndexWorkspace* indexWorkspace, ReportPipelineRecord* record,
                                    StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (repositoryRoot == nullptr || resultDirectory == nullptr || catalog == nullptr || manifestWorkspace == nullptr ||
	    modelWorkspace == nullptr || timingScratch == nullptr || indexWorkspace == nullptr || record == nullptr ||
	    error == nullptr)
		return error != nullptr ? ReportError(error, ArenaStatus_InvalidArgument, "report_regeneration_argument")
		                        : ArenaStatus_InvalidArgument;
	*record = {};
	std::array<wchar_t, kReportPathCapacity> manifestPath = {};
	std::array<wchar_t, kReportPathCapacity> normalizedPath = {};
	std::array<wchar_t, kReportPathCapacity> summaryPath = {};
	std::array<wchar_t, kReportPathCapacity> candidateSummaryPath = {};
	if (ChildPath(resultDirectory, L"manifest.json", &manifestPath, error) != ArenaStatus_Ok ||
	    ChildPath(resultDirectory, L"normalized.csv", &normalizedPath, error) != ArenaStatus_Ok ||
	    ChildPath(resultDirectory, L"summary.csv", &summaryPath, error) != ArenaStatus_Ok ||
	    ChildPath(resultDirectory, L"summary.csv.regenerate", &candidateSummaryPath, error) != ArenaStatus_Ok ||
	    LoadResultManifest(manifestPath.data(), catalog, manifestWorkspace, error) != ArenaStatus_Ok ||
	    RegenerateSummaryFromNormalized(normalizedPath.data(), candidateSummaryPath.data(), catalog, manifestWorkspace,
	                                    error) != ArenaStatus_Ok ||
	    LoadResultPresentationFromSummary(repositoryRoot, resultDirectory, candidateSummaryPath.data(), catalog,
	                                      manifestWorkspace, modelWorkspace, error) != ArenaStatus_Ok)
	{
		DeleteFileW(candidateSummaryPath.data());
		return error->code;
	}
	if (MoveFileExW(candidateSummaryPath.data(), summaryPath.data(),
	                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
	{
		DeleteFileW(candidateSummaryPath.data());
		return ReportError(error, ArenaStatus_InvalidResult, "summary_commit_failed");
	}
	ArenaStatus status = WriteMarkdownReport(repositoryRoot, resultDirectory, catalog, modelWorkspace, record, error);
	if (status == ArenaStatus_Ok)
		status = WriteSummarySvg(repositoryRoot, resultDirectory, catalog, modelWorkspace, record, error);
	if (status == ArenaStatus_Ok && modelWorkspace->timing.availability == PresenceStatus_Present)
		status =
		    WriteStepTimingSvg(repositoryRoot, resultDirectory, catalog, modelWorkspace, timingScratch, record, error);
	if (status == ArenaStatus_Ok && ClassifyResultStorage(repositoryRoot, resultDirectory) != ResultStorage_Local)
		status = WriteResultIndexes(repositoryRoot, catalog, indexWorkspace, record, error);
	return status;
}
} // namespace physics_arena
