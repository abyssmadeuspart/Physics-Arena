#pragma once

#include "Cases/BenchmarkPolygonChaosCase.h"
#include "Runner/BenchmarkPolygonChaosCli.h"
#include "Runtime/BenchmarkPolygonChaosRuntime.h"

namespace BenchmarkPolygonChaos
{

    int WriteProbeCsv(const TCHAR* OutputPath);
    int WriteResult(const RunnerArgs& Args,
                    const ThreadRuntimeState& RuntimeState,
                    const ChaosCaseState& State);

} // namespace BenchmarkPolygonChaos
