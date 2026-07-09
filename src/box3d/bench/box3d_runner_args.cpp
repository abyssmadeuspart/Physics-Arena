#include "box3d_runner_args.h"

#include "box3d_case_registry.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace box3d_benchmark
{
Box3DRunRequest DefaultBox3DRunRequest()
{
	Box3DRunRequest request = {};
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

int ParseBox3DRunRequest(int argc, char** argv, Box3DRunRequest* request)
{
	if (request == nullptr)
	{
		return 2;
	}
	*request = DefaultBox3DRunRequest();
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
				std::fprintf(stderr, "invalid_argument name=thread-count value=%s\n", arg + 15);
				return 2;
			}
		}
		else if (std::strncmp(arg, "--step-count=", 13) == 0)
		{
			if (ParseInt(arg + 13, &request->stepCount) != 0 || request->stepCount < 1)
			{
				std::fprintf(stderr, "invalid_argument name=step-count value=%s\n", arg + 13);
				return 2;
			}
		}
		else if (std::strncmp(arg, "--warmup-steps=", 15) == 0)
		{
			if (ParseInt(arg + 15, &request->warmupSteps) != 0)
			{
				std::fprintf(stderr, "invalid_argument name=warmup-steps value=%s\n", arg + 15);
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
		else
		{
			std::fprintf(stderr, "invalid_argument value=%s\n", arg);
			return 2;
		}
	}
	return 0;
}
}
