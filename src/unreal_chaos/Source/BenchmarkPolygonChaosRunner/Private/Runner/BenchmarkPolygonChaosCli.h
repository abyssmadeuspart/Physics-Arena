#pragma once

#include "Runtime/BenchmarkPolygonChaosConfig.h"

#include "CoreMinimal.h"

namespace BenchmarkPolygonChaos
{

    struct CliOption
    {
        TCHAR* Name;
        TCHAR* Value;
    };

    struct CliOptions
    {
        CliOption Items[48];
        int Count;
    };

    struct RunnerArgs
    {
        const TCHAR* CaseId = kCaseIdText;
        const TCHAR* OutputPath = TEXT("polygon_results.csv");
        int ThreadCount = 1;
        int StepCount = 300;
        int WarmupSteps = 0;
        int RepeatIndex = 0;
    };

    int ParseArguments(int ArgC, TCHAR* ArgV[], CliOptions* Options);
    const TCHAR* OptionValue(const CliOptions& Options, const TCHAR* Name, const TCHAR* Fallback);
    int ParseRunnerArgs(const CliOptions& Options, RunnerArgs* Args);

} // namespace BenchmarkPolygonChaos
