#include "box3d_case_registry.h"

#include <cstring>

namespace box3d_benchmark
{
const Box3DCaseDescriptor& Box3DContainerPileCaseDescriptor()
{
	static const Box3DCaseDescriptor descriptor =
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
		RunBox3DCaseWarmup,
		CreateBox3DCaseState,
		StepBox3DCase,
		DestroyBox3DCaseState,
		SampleBox3DTransforms,
		CopyBox3DStaticBoxes,
	};
	return descriptor;
}

int ResolveBox3DCase(const char* caseId, const Box3DCaseDescriptor** descriptor)
{
	if (caseId == nullptr || descriptor == nullptr)
	{
		return 2;
	}
	const Box3DCaseDescriptor& containerPile = Box3DContainerPileCaseDescriptor();
	if (std::strcmp(caseId, containerPile.caseId) == 0)
	{
		*descriptor = &containerPile;
		return 0;
	}
	return 2;
}
}
