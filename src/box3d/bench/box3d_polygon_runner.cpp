#include "box3d_case_registry.h"
#include "box3d_result_writer.h"
#include "box3d_runner_args.h"
#include "box3d_shared_visual_mode.h"

#include <cstdio>
#include <cstring>

namespace box3d_benchmark
{
enum Box3DRunnerMode
{
	Box3DRunnerMode_Headless,
	Box3DRunnerMode_SharedVisual,
};

Box3DRunnerMode SelectRunnerMode(int argc, char** argv)
{
	for (int index = 1; index < argc; ++index)
	{
		if (std::strncmp(argv[index], "--producer-mode=", 16) == 0)
		{
			return Box3DRunnerMode_SharedVisual;
		}
	}
	return Box3DRunnerMode_Headless;
}

int RunHeadless(const Box3DRunRequest& request, const Box3DCaseDescriptor& descriptor)
{
	Box3DCaseConfig config = {};
	config.threadCount = request.threadCount;
	config.repeatIndex = request.repeatIndex;
	config.stepCount = request.stepCount;
	config.warmupSteps = request.warmupSteps;
	if (descriptor.RunWarmup(config) != 0)
	{
		std::fprintf(stderr, "run_failed reason=create_warmup_fixture\n");
		return 2;
	}
	Box3DCaseState state = {};
	if (descriptor.CreateState(config, &state) != 0)
	{
		std::fprintf(stderr, "run_failed reason=create_fixture\n");
		return 2;
	}
	int stepStatus = descriptor.Step(&state, request.stepCount);
	int resultStatus = stepStatus == 0 ? WriteBox3DResultRow(request, descriptor, state) : 2;
	descriptor.DestroyState(&state);
	return resultStatus;
}
}

int main(int argc, char** argv)
{
	if (box3d_benchmark::SelectRunnerMode(argc, argv) == box3d_benchmark::Box3DRunnerMode_SharedVisual)
	{
		return box3d_benchmark::RunSharedVisualMode(argc, argv);
	}
	box3d_benchmark::Box3DRunRequest request = {};
	if (box3d_benchmark::ParseBox3DRunRequest(argc, argv, &request) != 0)
	{
		return 2;
	}
	const box3d_benchmark::Box3DCaseDescriptor* descriptor = nullptr;
	if (box3d_benchmark::ResolveBox3DCase(request.caseId, &descriptor) != 0)
	{
		std::fprintf(stderr, "invalid_argument name=case value=%s\n", request.caseId);
		return 2;
	}
	return box3d_benchmark::RunHeadless(request, *descriptor);
}
