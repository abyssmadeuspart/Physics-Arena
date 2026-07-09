#include "jolt_case_registry.h"
#include "jolt_result_writer.h"
#include "jolt_runner_args.h"
#include "jolt_shared_visual_mode.h"

#include <cstring>
#include <iostream>

namespace jolt_benchmark
{
enum JoltRunnerMode
{
	JoltRunnerMode_Headless,
	JoltRunnerMode_SharedVisual,
};

JoltRunnerMode SelectRunnerMode(int argc, char** argv)
{
	for (int index = 1; index < argc; ++index)
	{
		if (std::strncmp(argv[index], "--producer-mode=", 16) == 0)
		{
			return JoltRunnerMode_SharedVisual;
		}
	}
	return JoltRunnerMode_Headless;
}

int RunHeadless(const JoltRunRequest& request, const JoltCaseDescriptor& descriptor)
{
	JoltCaseConfig config = {};
	config.threadCount = request.threadCount;
	config.repeatIndex = request.repeatIndex;
	config.stepCount = request.stepCount;
	config.warmupSteps = request.warmupSteps;
	if (descriptor.RunWarmup(config) != 0)
	{
		std::cerr << "run_failed reason=create_warmup_fixture\n";
		return 2;
	}
	JoltCaseState state = {};
	if (descriptor.CreateState(config, &state) != 0)
	{
		std::cerr << "run_failed reason=create_fixture\n";
		return 2;
	}
	int stepStatus = descriptor.Step(&state, request.stepCount);
	int resultStatus = stepStatus == 0 ? WriteJoltResultRow(request, descriptor, state) : 2;
	descriptor.DestroyState(&state);
	return resultStatus;
}
}

int main(int argc, char** argv)
{
	if (jolt_benchmark::SelectRunnerMode(argc, argv) == jolt_benchmark::JoltRunnerMode_SharedVisual)
	{
		return jolt_benchmark::RunSharedVisualMode(argc, argv);
	}
	jolt_benchmark::JoltRunRequest request = {};
	if (jolt_benchmark::ParseJoltRunRequest(argc, argv, &request) != 0)
	{
		return 2;
	}
	const jolt_benchmark::JoltCaseDescriptor* descriptor = nullptr;
	if (jolt_benchmark::ResolveJoltCase(request.caseId, &descriptor) != 0)
	{
		std::cerr << "invalid_argument name=case value=" << request.caseId << '\n';
		return 2;
	}
	jolt_benchmark::InitializeJoltRuntime();
	int result = jolt_benchmark::RunHeadless(request, *descriptor);
	jolt_benchmark::ShutdownJoltRuntime();
	return result;
}
