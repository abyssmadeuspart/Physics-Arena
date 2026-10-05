#include "nvidia_physx34_runner_args.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nvidia_physx34_benchmark
{
int ParseInt(const char* value, int* parsed)
{
	char* end = nullptr;
	long result = std::strtol(value, &end, 10);
	if (end == value || *end != '\0' || result < 0 || result > 1000000)
	{
		return 2;
	}
	*parsed = static_cast<int>(result);
	return 0;
}

int ParsePhysXRunRequest(int argc, char** argv, PhysXRunRequest* request)
{
	if (request == nullptr)
	{
		return 2;
	}
	*request = {};
	request->outputPath = "polygon_results.csv";
	request->threadCount = 1;
	const char* caseContract = nullptr;
	int caseContractCount = 0;
	int verificationCount = 0;
	for (int index = 1; index < argc; ++index)
	{
		const char* arg = argv[index];
		if (std::strncmp(arg, "--case-contract=", 16) == 0)
		{
			caseContract = arg + 16;
			caseContractCount += 1;
		}
		else if (std::strncmp(arg, "--stack-stream=", 15) == 0 && arg[15] != '\0' && request->stackStream == nullptr)
		{
			request->stackStream = arg + 15;
		}
		else if (std::strncmp(arg, "--verify=", 9) == 0)
		{
			if (++verificationCount != 1 || (std::strcmp(arg + 9, "on") != 0 && std::strcmp(arg + 9, "off") != 0))
				return 2;
			request->verificationMode = std::strcmp(arg + 9, "on") == 0 ? VerificationMode_On : VerificationMode_Off;
		}
		else if (std::strncmp(arg, "--thread-count=", 15) == 0)
		{
			if (ParseInt(arg + 15, &request->threadCount) != 0 || request->threadCount < 1)
			{
				std::fprintf(stderr, "invalid_argument name=thread-count value=%s\n", arg + 15);
				return 2;
			}
		}
		else if (std::strncmp(arg, "--repeat-index=", 15) == 0)
		{
			if (ParseInt(arg + 15, &request->repeatIndex) != 0)
			{
				std::fprintf(stderr, "invalid_argument name=repeat-index value=%s\n", arg + 15);
				return 2;
			}
		}
		else if (std::strncmp(arg, "--output=", 9) == 0)
		{
			request->outputPath = arg + 9;
		}
		else if (std::strncmp(arg, "--step-timing-output=", 21) == 0 && arg[21] != '\0')
		{
			request->stepTimingOutputPath = arg + 21;
		}
		else if (std::strncmp(arg, "--recording-output=", 19) == 0 && arg[19] != '\0' &&
		         request->recordingPath == nullptr)
		{
			request->recordingPath = arg + 19;
			request->recordingMode = benchmark_replay::RecordingMode_On;
		}
		else
		{
			std::fprintf(stderr, "invalid_argument value=%s\n", arg);
			return 2;
		}
	}
	std::uint32_t byteCount = 0;
	if (caseContract == nullptr || caseContractCount != 1 ||
	    DecodeCaseExecutionHex(caseContract, static_cast<std::uint32_t>(std::strlen(caseContract)),
	                           request->caseExecutionBytes.data(),
	                           static_cast<std::uint32_t>(request->caseExecutionBytes.size()), &byteCount,
	                           &request->caseExecution) != CaseExecutionDecodeStatus_Ok)
		return 2;
	request->stepCount = static_cast<int>(request->caseExecution.measuredWorkUnitCount);
	request->warmupSteps = static_cast<int>(request->caseExecution.warmupWorkUnitCount);
	const int requiresStream = request->verificationMode == VerificationMode_On && benchmark_stack::TargetFixture(request->caseExecution.fixtureKind) != 0;
	if (requiresStream != 0 ? request->stackStream == nullptr || std::strncmp(request->stackStream, "\\\\.\\pipe\\", 9) != 0 || request->stackStream[9] == '\0' : request->stackStream != nullptr) return 2;
	return 0;
}
}
