#pragma once

namespace box3d_benchmark
{
struct Box3DRunRequest
{
	const char* caseId;
	const char* outputPath;
	int threadCount;
	int stepCount;
	int warmupSteps;
	int repeatIndex;
};

Box3DRunRequest DefaultBox3DRunRequest();
int ParseBox3DRunRequest(int argc, char** argv, Box3DRunRequest* request);
}
