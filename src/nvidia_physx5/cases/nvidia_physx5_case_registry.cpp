#include "nvidia_physx5_case_registry.h"

#include <cstring>

namespace nvidia_physx5_benchmark
{
const PhysXCaseDescriptor& PhysXContainerPileCaseDescriptor()
{
	static const PhysXCaseDescriptor descriptor =
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
		RunPhysXCaseWarmup,
		CreatePhysXCaseState,
		StepPhysXCase,
		DestroyPhysXCaseState,
		SamplePhysXTransforms,
		CopyPhysXStaticBoxes,
	};
	return descriptor;
}

int ResolvePhysXCase(const char* caseId, const PhysXCaseDescriptor** descriptor)
{
	if (caseId == nullptr || descriptor == nullptr)
	{
		return 2;
	}
	const PhysXCaseDescriptor& containerPile = PhysXContainerPileCaseDescriptor();
	if (std::strcmp(caseId, containerPile.caseId) == 0)
	{
		*descriptor = &containerPile;
		return 0;
	}
	return 2;
}
}
