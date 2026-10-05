#include "physx34_case_registry.h"
#include "physx34_runner_args.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
	physx34_benchmark::PhysXRunRequest request = {};
	if (physx34_benchmark::ParsePhysXRunRequest(argc, argv, &request) != 0)
	{
		return 2;
	}
	const physx34_benchmark::PhysXCaseDescriptor* descriptor = nullptr;
	if (physx34_benchmark::ResolvePhysXCase(request.caseExecution, &descriptor) != 0)
	{
		std::fprintf(stderr, "invalid_argument name=case-contract\n");
		return 2;
	}
	return physx34_benchmark::RunPhysXCase(request, *descriptor);
}
