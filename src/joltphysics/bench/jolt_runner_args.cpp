#include "jolt_runner_args.h"

#include "jolt_case_registry.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace jolt_benchmark
{
JoltRunRequest DefaultJoltRunRequest()
{
	JoltRunRequest request = {};
	request.caseId = kCaseId;
	request.outputPath = "polygon_results.csv";
	request.threadCount = 1;
	request.stepCount = 300;
	request.warmupSteps = 0;
	request.repeatIndex = 0;
	return request;
}

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

int ParseJoltRunRequest(int argc, char** argv, JoltRunRequest* request)
{
	if (request == nullptr)
	{
		return 2;
	}
	*request = DefaultJoltRunRequest();
	for (int index = 1; index < argc; ++index)
	{
		const char* arg = argv[index];
		if (std::strncmp(arg, "--case=", 7) == 0)
		{
			request->caseId = arg + 7;
		}
		else if (std::strncmp(arg, "--thread-count=", 15) == 0)
		{
			if (ParseInt(arg + 15, &request->threadCount) != 0 || request->threadCount < 1)
			{
				std::cerr << "invalid_argument name=thread-count value=" << arg + 15 << '\n';
				return 2;
			}
		}
		else if (std::strncmp(arg, "--step-count=", 13) == 0)
		{
			if (ParseInt(arg + 13, &request->stepCount) != 0 || request->stepCount < 1)
			{
				std::cerr << "invalid_argument name=step-count value=" << arg + 13 << '\n';
				return 2;
			}
		}
		else if (std::strncmp(arg, "--warmup-steps=", 15) == 0)
		{
			if (ParseInt(arg + 15, &request->warmupSteps) != 0)
			{
				std::cerr << "invalid_argument name=warmup-steps value=" << arg + 15 << '\n';
				return 2;
			}
		}
		else if (std::strncmp(arg, "--repeat-index=", 15) == 0)
		{
			if (ParseInt(arg + 15, &request->repeatIndex) != 0)
			{
				std::cerr << "invalid_argument name=repeat-index value=" << arg + 15 << '\n';
				return 2;
			}
		}
		else if (std::strncmp(arg, "--output=", 9) == 0)
		{
			request->outputPath = arg + 9;
		}
		else
		{
			std::cerr << "invalid_argument value=" << arg << '\n';
			return 2;
		}
	}
	return 0;
}
}
