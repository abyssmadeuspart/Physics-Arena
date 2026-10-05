#pragma once

#include "physics_arena/run.h"

#include <cstdint>

namespace physics_arena
{
struct ResultViewModel;
struct TimingArtifactTotals;

struct ResultPipelineRecord
{
	std::uint32_t normalizedRowCount;
	std::uint32_t summaryRowCount;
	std::uint32_t sliceSummaryCount;
	std::uint32_t observationRowCount;
	std::uint32_t maximumRawRowTextUsed;
};

ArenaStatus FinalizeHeadlessResults(const wchar_t* repositoryRoot, const Catalog* catalog,
                                    const ReleaseCatalog* releaseCatalog, const PreparedRunRequest* request,
                                    const RunPathRecord* paths, ResultPipelineRecord* record, StatusRecord* error);
ArenaStatus FinalizeRunResults(const wchar_t* repositoryRoot, const Catalog* catalog,
                               const ReleaseCatalog* releaseCatalog, const PreparedRunRequest* request,
                               const RunPathRecord* paths, ResultPipelineRecord* record, StatusRecord* error);
ArenaStatus ValidateNormalizedResults(const wchar_t* normalizedPath, const Catalog* catalog,
                                      const ResultManifestRecord* manifest, StatusRecord* error);
ArenaStatus ValidateResultAgreement(const wchar_t* normalizedPath, const Catalog* catalog,
                                    const ResultManifestRecord* manifest, const ResultViewModel* model,
                                    const TimingArtifactTotals* totals, StatusRecord* error);
ArenaStatus RegenerateSummaryFromNormalized(const wchar_t* normalizedPath, const wchar_t* summaryPath,
                                            const Catalog* catalog, const ResultManifestRecord* manifest,
                                            StatusRecord* error);
}
