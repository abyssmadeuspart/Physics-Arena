#pragma once

namespace nvidia_physx5_benchmark
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
