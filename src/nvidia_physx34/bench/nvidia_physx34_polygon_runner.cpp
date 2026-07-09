#include "nvidia_physx34_case_registry.h"
#include "nvidia_physx34_result_writer.h"
#include "nvidia_physx34_runner_args.h"
#include "nvidia_physx34_shared_visual_mode.h"

#include <cstdio>
#include <cstring>

namespace nvidia_physx34_benchmark
{
enum PhysXRunnerMode
{
	PhysXRunnerMode_Headless,
	PhysXRunnerMode_SharedVisual,
};

PhysXRunnerMode SelectRunnerMode(int argc, char** argv)
{
	for (int index = 1; index < argc; ++index)
	{
		if (std::strncmp(argv[index], "--producer-mode=", 16) == 0)
		{
			return PhysXRunnerMode_SharedVisual;
		}
	}
	return PhysXRunnerMode_Headless;
}

int RunHeadless(const PhysXRunRequest& request, const PhysXCaseDescriptor& descriptor)
{
	PhysXCaseConfig config = {};
	config.threadCount = request.threadCount;
	config.repeatIndex = request.repeatIndex;
	config.stepCount = request.stepCount;
	config.warmupSteps = request.warmupSteps;
	if (request.warmupSteps > 0 && descriptor.RunWarmup(config) != 0)
	{
		std::fprintf(stderr, "run_failed reason=create_warmup_fixture\n");
		return 2;
	}
	PhysXCaseState state = {};
	if (descriptor.CreateState(config, &state) != 0)
	{
		std::fprintf(stderr, "run_failed reason=create_fixture\n");
		return 2;
	}
	int stepStatus = descriptor.Step(&state, request.stepCount);
	int resultStatus = stepStatus == 0 ? WritePhysXResultRow(request, descriptor, state) : 2;
	descriptor.DestroyState(&state);
	return resultStatus;
}
}

int main(int argc, char** argv)
{
	if (nvidia_physx34_benchmark::SelectRunnerMode(argc, argv) == nvidia_physx34_benchmark::PhysXRunnerMode_SharedVisual)
	{
		return nvidia_physx34_benchmark::RunSharedVisualMode(argc, argv);
	}
	nvidia_physx34_benchmark::PhysXRunRequest request = {};
	if (nvidia_physx34_benchmark::ParsePhysXRunRequest(argc, argv, &request) != 0)
	{
		return 2;
	}
	const nvidia_physx34_benchmark::PhysXCaseDescriptor* descriptor = nullptr;
	if (nvidia_physx34_benchmark::ResolvePhysXCase(request.caseId, &descriptor) != 0)
	{
		std::fprintf(stderr, "invalid_argument name=case value=%s\n", request.caseId);
		return 2;
	}
	return nvidia_physx34_benchmark::RunHeadless(request, *descriptor);
}
