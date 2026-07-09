#pragma once

#include "nvidia_physx34_box_container_pile_10k_case.h"

namespace nvidia_physx34_benchmark
{
struct PhysXCaseDescriptor
{
	const char* caseId;
	const char* fixtureSemantic;
	const char* fixtureVersion;
	int dynamicBodyCount;
	int staticBodyCount;
	int bodyCount;
	float dynamicHalfExtent;
	float lateralEscapeLimit;
	float maxY;
	int (*RunWarmup)(const PhysXCaseConfig& config);
	int (*CreateState)(const PhysXCaseConfig& config, PhysXCaseState* state);
	int (*Step)(PhysXCaseState* state, int stepCount);
	void (*DestroyState)(PhysXCaseState* state);
	int (*SampleTransforms)(const PhysXCaseState& state, PhysXTransform* transforms, int transformCapacity);
	int (*CopyStaticBoxes)(PhysXStaticBox* boxes, int boxCapacity);
};

int ResolvePhysXCase(const char* caseId, const PhysXCaseDescriptor** descriptor);
const PhysXCaseDescriptor& PhysXContainerPileCaseDescriptor();
}
