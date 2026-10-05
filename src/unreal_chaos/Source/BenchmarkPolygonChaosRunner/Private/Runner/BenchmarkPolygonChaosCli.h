#pragma once

#include "Runtime/BenchmarkPolygonChaosConfig.h"

#include "CoreMinimal.h"
#include "replay_recording.h"


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

enum VerificationMode
{
	VerificationMode_On = 0,
	VerificationMode_Off,
};

struct RunnerArgs
{
	CaseExecutionSpec CaseExecution = {};
	VerificationMode Verification = VerificationMode_On;
	const ChaosCaseDescriptor* CaseDescriptor = nullptr;
	const TCHAR* RecordingPath = TEXT("");
	benchmark_replay::RecordingMode RecordingMode = benchmark_replay::RecordingMode_Off;
	const TCHAR* StackStream = nullptr;
	const TCHAR* OutputPath = TEXT("polygon_results.csv");
	const TCHAR* StepTimingOutputPath = TEXT("");
	int ThreadCount = 1;
	int StepCount = 0;
	int WarmupSteps = 0;
	int RepeatIndex = 0;
};

int ParseArguments(int ArgC, TCHAR* ArgV[], CliOptions* Options);
const TCHAR* OptionValue(const CliOptions& Options, const TCHAR* Name, const TCHAR* Fallback);
int ParseRunnerArgs(const CliOptions& Options, RunnerArgs* Args);

} // namespace BenchmarkPolygonChaos
