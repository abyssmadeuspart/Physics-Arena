#pragma once

#include "physics_arena/result_view_model.h"

#include <array>
#include <cstdint>

namespace physics_arena
{
constexpr std::size_t kResultIndexCapacity = 256;
constexpr std::size_t kResultIndexTextCapacity = 1024;

struct ResultIndexText
{
	std::array<char, kResultIndexTextCapacity> data;
	std::uint32_t size;
};

struct ResultIndexRecord
{
	ResultStorage storage;
	ResultIndexText caseSlug;
	ResultIndexText caseName;
	ResultIndexText cpuSlug;
	ResultIndexText cpuName;
	ResultIndexText runSlug;
	ResultIndexText hostLabel;
	std::array<std::uint32_t, kThreadCountCapacity> threadCounts;
	std::uint32_t repeatCount;
	std::uint32_t threadCount;
	std::uint32_t engineCount;
};

struct ResultIndexWorkspace
{
	std::array<ResultIndexRecord, kResultIndexCapacity> records;
	std::uint32_t recordCount;
};

struct ReportPipelineRecord
{
	std::uint64_t summarySvgBytes;
	std::uint64_t markdownBytes;
	std::uint64_t timingSvgBytes;
	std::uint32_t indexFileCount;
};

ArenaStatus WriteMarkdownReport(const wchar_t* repositoryRoot, const wchar_t* resultDirectory, const Catalog* catalog,
                                const ResultViewModel* model, ReportPipelineRecord* record, StatusRecord* error);
ArenaStatus WriteSummarySvg(const wchar_t* repositoryRoot, const wchar_t* resultDirectory, const Catalog* catalog,
                            const ResultViewModel* model, ReportPipelineRecord* record, StatusRecord* error);
ArenaStatus WriteStepTimingSvg(const wchar_t* repositoryRoot, const wchar_t* resultDirectory, const Catalog* catalog,
                               const ResultViewModel* model, TimingProjectionScratch* timingScratch,
                               ReportPipelineRecord* record, StatusRecord* error);
ArenaStatus WriteResultIndexes(const wchar_t* repositoryRoot, const Catalog* catalog, ResultIndexWorkspace* workspace,
                               ReportPipelineRecord* record, StatusRecord* error);
ArenaStatus BuildResultIndexRecord(const Catalog* catalog, const ResultManifestRecord& manifest,
                                   std::string_view caseDirectory, std::string_view cpuDirectory,
                                   std::string_view runDirectory, ResultIndexRecord* record, StatusRecord* error);
ArenaStatus DiscoverResultIndex(const wchar_t* repositoryRoot, const Catalog* catalog, ResultIndexWorkspace* workspace,
                                StatusRecord* error);
ArenaStatus LoadResultPresentation(const wchar_t* repositoryRoot, const wchar_t* resultDirectory,
                                   const Catalog* catalog, ResultManifestRecord* manifestWorkspace,
                                   ResultViewModel* modelWorkspace, StatusRecord* error);
ArenaStatus RegenerateResultReports(const wchar_t* repositoryRoot, const wchar_t* resultDirectory,
                                    const Catalog* catalog, ResultManifestRecord* manifestWorkspace,
                                    ResultViewModel* modelWorkspace, TimingProjectionScratch* timingScratch,
                                    ResultIndexWorkspace* indexWorkspace, ReportPipelineRecord* record,
                                    StatusRecord* error);
}
