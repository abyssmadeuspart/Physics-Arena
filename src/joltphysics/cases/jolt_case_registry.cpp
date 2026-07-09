#include "jolt_case_registry.h"

#include <cstring>

namespace jolt_benchmark
{
const JoltCaseDescriptor& JoltContainerPileCaseDescriptor()
{
	static const JoltCaseDescriptor descriptor =
	{
		kCaseId,
		kFixtureSemantic,
		kFixtureVersion,
		kDynamicBodyCount,
		kStaticBodyCount,
		kBodyCount,
		kHalfExtent,
		kOpenContainerLateralEscape,
		kOpenContainerMaxY,
		RunJoltCaseWarmup,
		CreateJoltCaseState,
		StepJoltCase,
		DestroyJoltCaseState,
		SampleJoltTransforms,
		CopyJoltStaticBoxes,
	};
	return descriptor;
}

int ResolveJoltCase(const char* caseId, const JoltCaseDescriptor** descriptor)
{
	if (caseId == nullptr || descriptor == nullptr)
	{
		return 2;
	}
	const JoltCaseDescriptor& containerPile = JoltContainerPileCaseDescriptor();
	if (std::strcmp(caseId, containerPile.caseId) == 0)
	{
		*descriptor = &containerPile;
		return 0;
	}
	return 2;
}
}
