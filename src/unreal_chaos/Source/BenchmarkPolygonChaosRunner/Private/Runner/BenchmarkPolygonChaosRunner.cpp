#include "Cases/BenchmarkPolygonChaosCaseRegistry.h"
#include "Output/BenchmarkPolygonChaosOutput.h"
#include "Runner/BenchmarkPolygonChaosCli.h"
#include "Runtime/BenchmarkPolygonChaosRuntime.h"

#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Modules/ModuleManager.h"
#include "RequiredProgramMainCPPInclude.h"

#include <cstdio>

IMPLEMENT_APPLICATION(BenchmarkPolygonChaosRunner, "BenchmarkPolygonChaosRunner");

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

	const TCHAR* ProbeOutput = BenchmarkPolygonChaos::OptionValue(Options, TEXT("--probe-output"), TEXT(""));
	if (ProbeOutput[0] != TEXT('\0'))
	{
		return BenchmarkPolygonChaos::WriteProbeCsv(ProbeOutput, BenchmarkPolygonChaos::kEngineId,
		                                            BenchmarkPolygonChaos::kEngineRef, "");
	}

	BenchmarkPolygonChaos::RunnerArgs Args;
	if (BenchmarkPolygonChaos::ParseRunnerArgs(Options, &Args) != 0)
	{
		return 2;
	}

	BenchmarkPolygonChaos::ScopedCoreRuntime CoreRuntime(Args.ThreadCount);
	FTaskTagScope GameThreadScope(ETaskTag::EGameThread);
	const int Status = BenchmarkPolygonChaos::RunChaosCase(Args, CoreRuntime.State);
	FPlatformMisc::RequestExit(false);
	return Status;
}
