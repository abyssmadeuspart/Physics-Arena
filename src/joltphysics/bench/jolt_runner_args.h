#pragma once

namespace jolt_benchmark
{
struct JoltRunRequest
{
	const char* caseId;
	const char* outputPath;
	int threadCount;
	int stepCount;
	int warmupSteps;
	int repeatIndex;
};

JoltRunRequest DefaultJoltRunRequest();
int ParseJoltRunRequest(int argc, char** argv, JoltRunRequest* request);
}
