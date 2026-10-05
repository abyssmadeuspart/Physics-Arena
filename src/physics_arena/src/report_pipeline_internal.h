#pragma once

#include "physics_arena/report_pipeline.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <malloc.h>
#include <span>
#include <string_view>
#include <vector>

namespace physics_arena
{
constexpr std::size_t kReportPathCapacity = 4096;
constexpr std::size_t kReportWriterCapacity = 16384;
constexpr std::size_t kReportCellCapacity = 1024;
constexpr std::size_t kEvidenceRowCapacity = 32;

struct ReportWriter
{
	void* handle;
	std::array<char, kReportWriterCapacity> bytes;
	std::uint32_t size;
	std::uint64_t totalBytes;
};

struct ReportCell
{
	std::array<char, kReportCellCapacity> data;
	std::uint32_t size;
};

struct ReportOutcomeLine
{
	std::uint32_t rowIndex;
	ReportCell text;
};

struct ReportOutcomeInput
{
	std::array<ReportCell, kEngineCapacity> failureLines;
	std::vector<ReportOutcomeLine> tupleLines;
	PresenceStatus presence;
};

struct ReportOutcomeCounts
{
	std::uint32_t passed;
	std::uint32_t failed;
	std::uint32_t skipped;
	std::uint32_t unassessed;
	std::uint32_t unverified;
};

struct ReportOutcomeEngine
{
	ReportOutcomeCounts counts;
	ReportCell countText;
	ReportCell detail;
	std::uint32_t engineOrdinal;
	std::uint32_t height;
	ObservationOutcome outcome;
};

struct ReportOutcomePresentation
{
	ReportCell headline;
	ReportCell criterion;
	std::array<ReportOutcomeEngine, kEngineCapacity> engines;
	std::uint32_t engineCount;
	std::uint32_t height;
};

ArenaStatus BuildReportOutcomePresentation(const ResultViewModel* model, const ReportOutcomeInput& input,
                                           std::uint32_t filterEngineOrdinal, std::uint32_t filterThreadCount,
                                           ReportOutcomePresentation* output, StatusRecord* error);
ArenaStatus WriteReportOutcomeSvg(ReportWriter* writer, double y, const ResultViewModel* model,
                                  const ReportOutcomeInput& input, const ReportOutcomePresentation& presentation,
                                  std::uint32_t filterThreadCount, StatusRecord* error);

ArenaStatus LoadReportOutcomes(const wchar_t* resultDirectory, const ResultViewModel* model,
                               ReportOutcomeInput* output, StatusRecord* error);
ObservationOutcome SummaryReportOutcome(const ResultViewModel* model, const ReportOutcomeInput& input,
                                        const ResultSummaryViewRow& row);

struct EvidenceRow
{
	ReportCell field;
	ReportCell value;
};

struct LegendRowView
{
	std::array<std::uint32_t, kEngineCapacity> engineOrdinals;
	std::uint32_t count;
};

struct ReportFixtureFact
{
	std::string_view key;
	std::uint32_t value;
};

struct ReportCaseMeta
{
	ReportCell caseLine;
	ReportCell description;
	std::array<ReportFixtureFact, 4> fixtureFacts;
	std::uint32_t fixtureFactCount;
};

struct PhysicsFact
{
	std::string_view key;
	std::string_view value;
	std::uint32_t engineMask;
};

struct PhysicsMetaProjection
{
	std::span<PhysicsFact> facts;
	std::array<std::uint32_t, kEngineCapacity> engines;
	std::array<std::array<char, 10>, kEngineCapacity> workerValues;
	std::uint32_t engineCount;
	std::uint32_t factCount;
};

enum MetaTextStyle
{
	MetaTextStyle_Value,
	MetaTextStyle_Key,
};

template <typename Cell> ArenaStatus SetCell(Cell* cell, std::string_view text)
{
	cell->size = 0;
	for (const char value : text)
	{
		if (value == '|')
		{
			if (cell->size + 2 > cell->data.size())
				return ArenaStatus_InvalidResult;
			cell->data[cell->size++] = '\\';
			cell->data[cell->size++] = '|';
		}
		else
		{
			if (cell->size == cell->data.size())
				return ArenaStatus_InvalidResult;
			cell->data[cell->size++] = value;
		}
	}
	return ArenaStatus_Ok;
}

template <typename Cell> std::string_view CellView(const Cell& cell)
{
	return std::string_view(cell.data.data(), cell.size);
}

ArenaStatus WriteSummarySvgFile(const wchar_t* repositoryRoot, const wchar_t* resultDirectory, const Catalog* catalog,
                                const ResultViewModel* model, ReportPipelineRecord* record,
                                std::uint32_t filterEngineOrdinal, std::uint32_t filterThreadCount,
                                const wchar_t* temporaryName, const wchar_t* finalName, StatusRecord* error);
std::string_view ReportRunProvenance(const ResultViewModel* model);
ArenaStatus ReportError(StatusRecord* error, ArenaStatus status, std::string_view detail);
ArenaStatus FlushReport(ReportWriter* writer, StatusRecord* error);
ArenaStatus WriteReport(ReportWriter* writer, std::string_view text, StatusRecord* error);
ArenaStatus WriteFormat(ReportWriter* writer, StatusRecord* error, const char* format, ...);
ArenaStatus ChildPath(const wchar_t* directory, const wchar_t* name, std::array<wchar_t, kReportPathCapacity>* path,
                      StatusRecord* error);
ArenaStatus OpenAtomicReport(const wchar_t* resultDirectory, const wchar_t* temporaryName, ReportWriter* writer,
                             std::array<wchar_t, kReportPathCapacity>* temporaryPath, StatusRecord* error);
ArenaStatus CommitAtomicReport(const wchar_t* resultDirectory, const wchar_t* finalName, ReportWriter* writer,
                               const std::array<wchar_t, kReportPathCapacity>& temporaryPath, StatusRecord* error);
ArenaStatus FormatCell(ReportCell* cell, const char* format, ...);
ArenaStatus WriteMarkdownRow(ReportWriter* writer, const ReportCell* cells, const std::uint32_t* widths,
                             const PresenceStatus* rightAligned, std::uint32_t count, StatusRecord* error);
ArenaStatus WriteMarkdownSeparator(ReportWriter* writer, const std::uint32_t* widths,
                                   const PresenceStatus* rightAligned, std::uint32_t count, StatusRecord* error);
ArenaStatus AppendCellText(ReportCell* cell, std::string_view text);
ArenaStatus AppendCellFormat(ReportCell* cell, const char* format, ...);
ArenaStatus BuildHostLabel(const HostRecord* host, ReportCell* output, PresenceStatus multiLine,
                           std::array<ReportCell, 3>* lines, std::uint32_t* lineCount);
ArenaStatus BuildThreadCounts(const ResultViewModel* model, ReportCell* output);
ArenaStatus BuildBuildLabel(const ResultViewModel* model, const ResultEngineView& engine, ReportCell* output);
ArenaStatus AddEvidence(std::array<EvidenceRow, kEvidenceRowCapacity>* rows, std::uint32_t* count,
                        std::string_view field, std::string_view value);
ArenaStatus FormatSummaryCells(const ResultViewModel* model, const ResultSummaryViewRow* row,
                               std::array<ReportCell, 10>* cells);
ArenaStatus WriteXmlText(ReportWriter* writer, std::string_view text, StatusRecord* error);
ArenaStatus FormatThousands(std::uint32_t value, ReportCell* output);
ArenaStatus PreflightReportInputs(const wchar_t* repositoryRoot, const Catalog* catalog, const ResultViewModel* model,
                                  StatusRecord* error);
ArenaStatus AppendUtf8Path(std::array<wchar_t, kReportPathCapacity>* path, std::uint32_t* size, std::string_view text,
                           StatusRecord* error);
std::uint32_t FirstAppearanceEngines(const ResultViewModel* model,
                                     std::array<std::uint32_t, kEngineCapacity>* ordinals);
std::uint32_t BuildLegendRows(const ResultViewModel* model, std::array<LegendRowView, kEngineCapacity>* rows);
ArenaStatus WriteEngineLogoDefinitions(const wchar_t* repositoryRoot, ReportWriter* writer,
                                       const ResultViewModel* model, StatusRecord* error);
ArenaStatus WriteEngineLogo(ReportWriter* writer, const ResultViewModel* model, std::uint32_t engineOrdinal, double x,
                            double centerY, double size, double* width, StatusRecord* error);
ArenaStatus WritePlainMeta(ReportWriter* writer, double x, double y, std::string_view text, StatusRecord* error);
std::uint32_t MetaTextRows(std::string_view text);
double MetaTextWidth(std::string_view text, MetaTextStyle style);
std::size_t MetaWrapSize(std::string_view text, double width, MetaTextStyle style);
ArenaStatus BuildReportCaseMeta(const Catalog* catalog, const ResultViewModel* model, const ResultSummaryViewRow& row,
                                ReportCaseMeta* output, StatusRecord* error);
ArenaStatus BuildReportRunMeta(const ResultViewModel* model, std::span<const std::uint32_t> threads,
                               ReportCell* output);
std::size_t PhysicsFactCapacity(const ResultViewModel* model, std::span<const std::uint32_t> engines);
ArenaStatus BuildPhysicsMeta(const Catalog* catalog, const ResultViewModel* model,
                             const std::array<std::uint32_t, kEngineCapacity>& appearance,
                             std::uint32_t appearanceCount, std::uint32_t filterThreadCount,
                             const ReportCaseMeta& caseMeta, PhysicsMetaProjection* projection);
ArenaStatus CountPhysicsMetaRows(const ResultViewModel* model, const PhysicsMetaProjection& projection,
                                 std::uint32_t* rows, StatusRecord* error);
ArenaStatus WritePhysicsMeta(ReportWriter* writer, double y, const ResultViewModel* model,
                             const PhysicsMetaProjection& projection, StatusRecord* error);
ArenaStatus WritePhysicsMarkdown(ReportWriter* writer, const ResultViewModel* model,
                                 const PhysicsMetaProjection& projection, StatusRecord* error);
ArenaStatus WriteWrappedMeta(ReportWriter* writer, double x, double y, std::string_view text, StatusRecord* error);
ArenaStatus WriteBuildMeta(ReportWriter* writer, double x, double y, const ResultViewModel* model,
                           std::uint32_t engineOrdinal, StatusRecord* error);
ArenaStatus AppendWideChild(std::array<wchar_t, kReportPathCapacity>* path, const wchar_t* child);
ArenaStatus CopyWidePath(std::array<wchar_t, kReportPathCapacity>* path, const wchar_t* source);
double PositiveAxisCeiling(double observedMaximum);
}
