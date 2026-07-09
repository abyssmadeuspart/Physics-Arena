#pragma once

#include "jolt_box_container_pile_10k_case.h"

namespace jolt_benchmark
{
struct JoltCaseDescriptor
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
	int (*RunWarmup)(const JoltCaseConfig& config);
	int (*CreateState)(const JoltCaseConfig& config, JoltCaseState* state);
	int (*Step)(JoltCaseState* state, int stepCount);
	void (*DestroyState)(JoltCaseState* state);
	int (*SampleTransforms)(const JoltCaseState& state, JoltTransform* transforms, int transformCapacity);
	void (*CopyStaticBoxes)(JoltStaticBox* boxes, int boxCapacity);
};

int ResolveJoltCase(const char* caseId, const JoltCaseDescriptor** descriptor);
const JoltCaseDescriptor& JoltContainerPileCaseDescriptor();
}
