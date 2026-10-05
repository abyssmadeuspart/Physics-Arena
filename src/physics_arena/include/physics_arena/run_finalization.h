#pragma once

#include "physics_arena/report_pipeline.h"
#include "physics_arena/result_pipeline.h"
#include "physics_arena/stack_stability.h"

namespace physics_arena
{
struct RunFinalizationWorkspace
{
	ResultManifestRecord manifest;
	ResultViewModel model;
	TimingProjectionScratch timingScratch;
	ResultIndexWorkspace indexes;
};

struct RunFinalizationRecord
{
	ResultPipelineRecord results;
	ReportPipelineRecord reports;
};

static_assert(sizeof(RunFinalizationWorkspace) < kMainStackReservationBytes * 3 / 4);



ArenaStatus FinalizeFreshRunArtifacts(const wchar_t* repositoryRoot, const Catalog* catalog,
                                      const ReleaseCatalog* releaseCatalog, const HostRecord* host,
                                      const PreparedRunRequest* request, const RunPathRecord* paths,
                                      RunFinalizationWorkspace* workspace, RunFinalizationRecord* record,
                                      StatusRecord* error);
}
