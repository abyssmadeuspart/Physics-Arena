#pragma once

#include "box3d_box_container_pile_10k_case.h"

namespace box3d_benchmark
{
struct Box3DCaseDescriptor
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
	int (*RunWarmup)(const Box3DCaseConfig& config);
	int (*CreateState)(const Box3DCaseConfig& config, Box3DCaseState* state);
	int (*Step)(Box3DCaseState* state, int stepCount);
	void (*DestroyState)(Box3DCaseState* state);
	void (*SampleTransforms)(const Box3DCaseState& state, Box3DTransform* transforms, int transformCapacity);
	void (*CopyStaticBoxes)(Box3DStaticBox* boxes, int boxCapacity);
};

int ResolveBox3DCase(const char* caseId, const Box3DCaseDescriptor** descriptor);
const Box3DCaseDescriptor& Box3DContainerPileCaseDescriptor();
}
