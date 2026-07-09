#include "Cases/BenchmarkPolygonChaosCase.h"
#include "Output/BenchmarkPolygonChaosOutput.h"
#include "Runner/BenchmarkPolygonChaosCli.h"
#include "Runtime/BenchmarkPolygonChaosRuntime.h"
#include "Visual/BenchmarkPolygonChaosSharedVisualMode.h"

#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Modules/ModuleManager.h"
#include "RequiredProgramMainCPPInclude.h"

#include <cstdio>

IMPLEMENT_APPLICATION(BenchmarkPolygonChaosRunner, "BenchmarkPolygonChaosRunner");

enum class BenchmarkPolygonChaosRunnerMode : uint8
{
    Headless,
    SharedVisual,
};

BenchmarkPolygonChaosRunnerMode SelectRunnerMode(const BenchmarkPolygonChaos::CliOptions& Options)
{
    const TCHAR* SharedVisualMode =
        BenchmarkPolygonChaos::OptionValue(Options, TEXT("--producer-mode"), TEXT(""));
    if (FCString::Strcmp(SharedVisualMode, TEXT("shared-visual")) == 0)
    {
        return BenchmarkPolygonChaosRunnerMode::SharedVisual;
    }

    return BenchmarkPolygonChaosRunnerMode::Headless;
}

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
    FCommandLine::Set(TEXT(""));

    BenchmarkPolygonChaos::CliOptions Options = {};
    int ParseStatus = BenchmarkPolygonChaos::ParseArguments(ArgC, ArgV, &Options);
    if (ParseStatus != 0)
    {
        std::fprintf(stderr, "invalid_result reason=arguments\n");
        return 2;
    }

    if (SelectRunnerMode(Options) == BenchmarkPolygonChaosRunnerMode::SharedVisual)
    {
        const int Status = BenchmarkPolygonChaos::RunSharedVisualMode(Options);
        FPlatformMisc::RequestExit(false);
        return Status;
    }

    const TCHAR* ProbeOutput =
        BenchmarkPolygonChaos::OptionValue(Options, TEXT("--probe-output"), TEXT(""));
    if (ProbeOutput[0] != TEXT('\0'))
    {
        return BenchmarkPolygonChaos::WriteProbeCsv(ProbeOutput);
    }

    BenchmarkPolygonChaos::RunnerArgs Args;
    if (BenchmarkPolygonChaos::ParseRunnerArgs(Options, &Args) != 0)
    {
        return 2;
    }

    BenchmarkPolygonChaos::ScopedCoreRuntime CoreRuntime(Args.ThreadCount);
    FTaskTagScope GameThreadScope(ETaskTag::EGameThread);
    const int Status = BenchmarkPolygonChaos::RunHeadless(Args, CoreRuntime.State);
    FPlatformMisc::RequestExit(false);
    return Status;
}
