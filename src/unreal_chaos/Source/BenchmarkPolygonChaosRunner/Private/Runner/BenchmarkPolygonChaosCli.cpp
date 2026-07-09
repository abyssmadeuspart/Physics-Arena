#include "Runner/BenchmarkPolygonChaosCli.h"

#include "Containers/StringConv.h"
#include "Misc/Char.h"

#include <cstdio>

namespace BenchmarkPolygonChaos
{

    int AddOption(CliOptions* Options, TCHAR* Name, TCHAR* Value)
    {
        if (Options->Count >= 48)
        {
            return 2;
        }

        Options->Items[Options->Count] = { Name, Value };
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
        Args->CaseId = OptionValue(Options, TEXT("--case"), Args->CaseId);
        Args->OutputPath = OptionValue(Options, TEXT("--output"), Args->OutputPath);
        if (ParseNonNegativeInt(OptionValue(Options, TEXT("--thread-count"), TEXT("1")),
                                &Args->ThreadCount) != 0 ||
            Args->ThreadCount < 1)
        {
            std::fprintf(stderr, "invalid_argument name=thread-count\n");
            return 2;
        }

        if (ParseNonNegativeInt(OptionValue(Options, TEXT("--step-count"), TEXT("300")),
                                &Args->StepCount) != 0 ||
            Args->StepCount < 1)
        {
            std::fprintf(stderr, "invalid_argument name=step-count\n");
            return 2;
        }

        if (ParseNonNegativeInt(OptionValue(Options, TEXT("--warmup-steps"), TEXT("0")),
                                &Args->WarmupSteps) != 0)
        {
            std::fprintf(stderr, "invalid_argument name=warmup-steps\n");
            return 2;
        }

        if (ParseNonNegativeInt(OptionValue(Options, TEXT("--repeat-index"), TEXT("0")),
                                &Args->RepeatIndex) != 0)
        {
            std::fprintf(stderr, "invalid_argument name=repeat-index\n");
            return 2;
        }

        if (FCString::Strcmp(Args->CaseId, kCaseIdText) != 0)
        {
            std::fprintf(
                stderr, "invalid_argument name=case value=%s\n", TCHAR_TO_UTF8(Args->CaseId));
            return 2;
        }

        return 0;
    }

} // namespace BenchmarkPolygonChaos
