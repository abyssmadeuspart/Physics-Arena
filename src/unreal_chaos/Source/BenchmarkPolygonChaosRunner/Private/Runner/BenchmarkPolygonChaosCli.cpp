#include "Runner/BenchmarkPolygonChaosCli.h"

#include "Containers/StringConv.h"
#include "Misc/Char.h"

#include <array>
#include <cstdio>

namespace BenchmarkPolygonChaos
{

int AddOption(CliOptions* Options, TCHAR* Name, TCHAR* Value)
{
	if (Options->Count >= 48)
	{
		return 2;
	}

	Options->Items[Options->Count] = {Name, Value};
	Options->Count += 1;
	return 0;
}

int ParseArguments(int ArgC, TCHAR* ArgV[], CliOptions* Options)
{
	Options->Count = 0;
	for (int Index = 1; Index < ArgC; ++Index)
	{
		TCHAR* Token = ArgV[Index];
		if (FCString::Strncmp(Token, TEXT("--"), 2) != 0)
		{
			return 2;
		}

		TCHAR* Equals = FCString::Strchr(Token, TEXT('='));
		if (Equals != nullptr)
		{
			Token[Equals - Token] = TEXT('\0');
			if (AddOption(Options, Token, Equals + 1) != 0)
			{
				return 2;
			}
			continue;
		}

		if (Index + 1 >= ArgC)
		{
			return 2;
		}

		if (AddOption(Options, Token, ArgV[Index + 1]) != 0)
		{
			return 2;
		}
		Index += 1;
	}

	return 0;
}

const TCHAR* OptionValue(const CliOptions& Options, const TCHAR* Name, const TCHAR* Fallback)
{
	for (int Index = 0; Index < Options.Count; ++Index)
	{
		if (FCString::Strcmp(Options.Items[Index].Name, Name) == 0)
		{
			return Options.Items[Index].Value;
		}
	}

	return Fallback;
}

int ParseNonNegativeInt(const TCHAR* Value, int* Parsed)
{
	TCHAR* End = nullptr;
	const int64 Result = FCString::Strtoi64(Value, &End, 10);
	if (End == Value || *End != TEXT('\0') || Result < 0 || Result > 1000000)
	{
		return 2;
	}

	*Parsed = static_cast<int>(Result);
	return 0;
}

int ParseRunnerArgs(const CliOptions& Options, RunnerArgs* Args)
{
	const TCHAR* CaseContract = nullptr;
	int CaseContractCount = 0;
	int RecordingCount = 0;
	int VerificationCount = 0;
	for (int Index = 0; Index < Options.Count; ++Index)
	{
		const TCHAR* Name = Options.Items[Index].Name;
		if (FCString::Strcmp(Name, TEXT("--case-contract")) == 0)
		{
			CaseContract = Options.Items[Index].Value;
			CaseContractCount += 1;
		}
		else if (FCString::Strcmp(Name, TEXT("--stack-stream")) == 0 && Args->StackStream == nullptr)
		{
			Args->StackStream = Options.Items[Index].Value;
			if (FCString::Strncmp(Args->StackStream, TEXT("\\\\.\\pipe\\"), 9) != 0 || Args->StackStream[9] == 0) return 2;
		}
		else if (FCString::Strcmp(Name, TEXT("--verify")) == 0)
		{
			const TCHAR* Value = Options.Items[Index].Value;
			if (++VerificationCount != 1 || (FCString::Strcmp(Value, TEXT("on")) != 0 && FCString::Strcmp(Value, TEXT("off")) != 0)) return 2;
			Args->Verification = FCString::Strcmp(Value, TEXT("on")) == 0 ? VerificationMode_On : VerificationMode_Off;
		}
		else if (FCString::Strcmp(Name, TEXT("--recording-output")) == 0)
		{
			Args->RecordingPath = Options.Items[Index].Value;
			++RecordingCount;
			Args->RecordingMode = benchmark_replay::RecordingMode_On;
		}
		else if (FCString::Strcmp(Name, TEXT("--output")) != 0 &&
		         FCString::Strcmp(Name, TEXT("--step-timing-output")) != 0 &&
		         FCString::Strcmp(Name, TEXT("--thread-count")) != 0 &&
		         FCString::Strcmp(Name, TEXT("--repeat-index")) != 0)
		{
			return 2;
		}
	}
	Args->OutputPath = OptionValue(Options, TEXT("--output"), Args->OutputPath);
	Args->StepTimingOutputPath = OptionValue(Options, TEXT("--step-timing-output"), Args->StepTimingOutputPath);
	if (ParseNonNegativeInt(OptionValue(Options, TEXT("--thread-count"), TEXT("1")), &Args->ThreadCount) != 0 ||
	    Args->ThreadCount < 1)
	{
		std::fprintf(stderr, "invalid_argument name=thread-count\n");
		return 2;
	}

	if (CaseContract == nullptr || CaseContractCount != 1 || RecordingCount > 1 ||
	    (RecordingCount == 1 && Args->RecordingPath[0] == 0))
	{
		return 2;
	}
	FTCHARToUTF8 ContractUtf8(CaseContract);
	std::array<std::uint8_t, kCaseExecutionPayloadCapacity> Bytes = {};
	std::uint32_t ByteCount = 0;
	if (DecodeCaseExecutionHex(ContractUtf8.Get(), static_cast<std::uint32_t>(ContractUtf8.Length()), Bytes.data(),
	                           static_cast<std::uint32_t>(Bytes.size()), &ByteCount,
	                           &Args->CaseExecution) != CaseExecutionDecodeStatus_Ok ||
	    Args->CaseExecution.measuredWorkUnitCount > static_cast<std::uint32_t>(MAX_int32) ||
	    Args->CaseExecution.warmupWorkUnitCount > static_cast<std::uint32_t>(MAX_int32))
	{
		return 2;
	}
	Args->StepCount = static_cast<int>(Args->CaseExecution.measuredWorkUnitCount);
	Args->WarmupSteps = static_cast<int>(Args->CaseExecution.warmupWorkUnitCount);

	if (ParseNonNegativeInt(OptionValue(Options, TEXT("--repeat-index"), TEXT("0")), &Args->RepeatIndex) != 0)
	{
		std::fprintf(stderr, "invalid_argument name=repeat-index\n");
		return 2;
	}

	if (ResolveChaosCaseDescriptor(Args->CaseExecution, &Args->CaseDescriptor) != 0)
	{
		return 2;
	}

	const int RequiresStream = Args->Verification == VerificationMode_On && benchmark_stack::TargetFixture(Args->CaseExecution.fixtureKind) != 0;
	if (RequiresStream != 0 ? Args->StackStream == nullptr : Args->StackStream != nullptr) return 2;
	return 0;
}

} // namespace BenchmarkPolygonChaos
