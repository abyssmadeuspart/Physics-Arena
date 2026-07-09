#pragma once

namespace physx34_benchmark
{
struct PhysXRunRequest
{
	const char* caseId;
	const char* outputPath;
	int threadCount;
	int stepCount;
	int warmupSteps;
	int repeatIndex;
};

PhysXRunRequest DefaultPhysXRunRequest();
int ParsePhysXRunRequest(int argc, char** argv, PhysXRunRequest* request);
}
