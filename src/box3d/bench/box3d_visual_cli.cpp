#include "box3d_visual_cli.h"

#include "box3d_case_registry.h"

#include <cstdio>
#include <cstring>

namespace box3d_benchmark
{
Box3DSharedVisualCli DefaultBox3DSharedVisualCli()
{
	Box3DSharedVisualCli cli = {};
	cli.producerMode = "";
	cli.connectHost = "127.0.0.1";
	cli.runToken = "";
	cli.engineId = "";
	cli.caseId = "";
	cli.connectPort = 0;
	cli.threadCount = 1;
	cli.repeatIndex = 0;
	cli.stepCount = 300;
	cli.warmupSteps = 0;
	return cli;
}

int ParseInt(const char* value, int minimumValue, int* parsed)
{
	int result = 0;
	for (int index = 0; value[index] != '\0'; ++index)
	{
		if (value[index] < '0' || value[index] > '9')
		{
			return 2;
		}
		result = (result * 10) + (value[index] - '0');
		if (result > 1000000)
		{
			return 2;
		}
	}
	if (result < minimumValue)
	{
		return 2;
	}
	*parsed = result;
	return 0;
}

int ParseBox3DSharedVisualCli(int argc, char** argv, Box3DSharedVisualCli* cli)
{
	if (cli == nullptr)
	{
		return 2;
	}
	*cli = DefaultBox3DSharedVisualCli();
	for (int index = 1; index < argc; ++index)
	{
		const char* arg = argv[index];
		if (std::strncmp(arg, "--producer-mode=", 16) == 0)
		{
			cli->producerMode = arg + 16;
		}
		else if (std::strncmp(arg, "--connect-host=", 15) == 0)
		{
			cli->connectHost = arg + 15;
		}
		else if (std::strncmp(arg, "--connect-port=", 15) == 0)
		{
			if (ParseInt(arg + 15, 1, &cli->connectPort) != 0)
			{
				return 2;
			}
		}
		else if (std::strncmp(arg, "--run-token=", 12) == 0)
		{
			cli->runToken = arg + 12;
		}
		else if (std::strncmp(arg, "--engine=", 9) == 0)
		{
			cli->engineId = arg + 9;
		}
		else if (std::strncmp(arg, "--case=", 7) == 0)
		{
			cli->caseId = arg + 7;
		}
		else if (std::strncmp(arg, "--thread-count=", 15) == 0)
		{
			if (ParseInt(arg + 15, 1, &cli->threadCount) != 0)
			{
				return 2;
			}
		}
		else if (std::strncmp(arg, "--repeat-index=", 15) == 0)
		{
			if (ParseInt(arg + 15, 0, &cli->repeatIndex) != 0)
			{
				return 2;
			}
		}
		else if (std::strncmp(arg, "--step-count=", 13) == 0)
		{
			if (ParseInt(arg + 13, 1, &cli->stepCount) != 0)
			{
				return 2;
			}
		}
		else if (std::strncmp(arg, "--warmup-steps=", 15) == 0)
		{
			if (ParseInt(arg + 15, 0, &cli->warmupSteps) != 0)
			{
				return 2;
			}
		}
		else
		{
			std::fprintf(stderr, "invalid_argument value=%s\n", arg);
			return 2;
		}
	}
	if (std::strcmp(cli->producerMode, "shared-visual") != 0 || std::strcmp(cli->connectHost, "127.0.0.1") != 0 ||
		std::strcmp(cli->engineId, kEngineId) != 0 || cli->runToken[0] == '\0' || cli->connectPort <= 0)
	{
		std::fprintf(stderr, "invalid_argument reason=shared_visual_producer_contract\n");
		return 2;
	}
	return 0;
}
}
