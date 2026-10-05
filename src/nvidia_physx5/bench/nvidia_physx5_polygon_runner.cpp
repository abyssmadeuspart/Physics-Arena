#include "nvidia_physx5_case_registry.h"
#include "nvidia_physx5_runner_args.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
	nvidia_physx5_benchmark::PhysXRunRequest request = {};
	if (nvidia_physx5_benchmark::ParsePhysXRunRequest(argc, argv, &request) != 0)
	{
		return 2;
	}
	const nvidia_physx5_benchmark::PhysXCaseDescriptor* descriptor = nullptr;
	if (nvidia_physx5_benchmark::ResolvePhysXCase(request.caseExecution, &descriptor) != 0)
	{
		std::fprintf(stderr, "invalid_argument name=case-contract\n");
		return 2;
	}
	return nvidia_physx5_benchmark::RunPhysXCase(request, *descriptor);
}
