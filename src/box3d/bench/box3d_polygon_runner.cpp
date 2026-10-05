#include "box3d_case_registry.h"
#include "box3d_runner_args.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
	box3d_benchmark::Box3DRunRequest request = {};
	if (box3d_benchmark::ParseBox3DRunRequest(argc, argv, &request) != 0)
	{
		return 2;
	}
	const box3d_benchmark::Box3DCaseDescriptor* descriptor = nullptr;
	if (box3d_benchmark::ResolveBox3DCase(request.caseExecution, &descriptor) != 0)
	{
		std::fprintf(stderr, "invalid_argument name=case-contract\n");
		return 2;
	}
	return box3d_benchmark::RunBox3DCase(request, *descriptor);
}
