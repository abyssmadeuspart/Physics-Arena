#pragma once

namespace box3d_benchmark
{
struct Box3DSharedVisualCli
{
	const char* producerMode;
	const char* connectHost;
	const char* runToken;
	const char* engineId;
	const char* caseId;
	int connectPort;
	int threadCount;
	int repeatIndex;
	int stepCount;
	int warmupSteps;
};

Box3DSharedVisualCli DefaultBox3DSharedVisualCli();
int ParseBox3DSharedVisualCli(int argc, char** argv, Box3DSharedVisualCli* cli);
}
