#include "jolt_case_registry.h"
#include "jolt_runner_args.h"

#include <cstring>
#include <iostream>

int main(int argc, char** argv)
{
	jolt_benchmark::JoltRunRequest request = {};
	if (jolt_benchmark::ParseJoltRunRequest(argc, argv, &request) != 0)
	{
		return 2;
	}
	const jolt_benchmark::JoltCaseDescriptor* descriptor = nullptr;
	if (jolt_benchmark::ResolveJoltCase(request.caseExecution, &descriptor) != 0)
	{
		std::cerr << "invalid_argument name=case-contract\n";
		return 2;
	}
	jolt_benchmark::InitializeJoltRuntime();
	int result = jolt_benchmark::RunJoltCase(request, *descriptor);
	jolt_benchmark::ShutdownJoltRuntime();
	return result;
}
