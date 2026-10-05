#pragma once

#include "stack_state_capture.h"

#include "benchmark_visual/visual_snapshot.h"
#include "case_execution_wire.h"

#include <cstddef>
#include <cstdint>

namespace nvidia_physx34_benchmark
{
struct PhysXRunRequest;

int BuildResolvedVisualGeometry(const CaseExecutionSpec& execution, const CaseExecutionGeometry& geometry,
                                benchmark_visual::VisualMeshStorage* meshes, benchmark_visual::VisualGeometry* visual);

struct PhysXCaseConfig
{
	const CaseExecutionSpec* caseExecution;
	int threadCount;
	int repeatIndex;
	int stepCount;
	int warmupSteps;
};

struct PhysXCaseView
{
	void* value;
};

struct PhysXCaseDescriptor
{
	const char* engineId;
	int (*runCase)(const PhysXRunRequest& request);
	int (*stepWorkUnits)(PhysXCaseView* state, int workUnitCount);
	int (*buildScene)(const PhysXCaseView& state, benchmark_visual::VisualGeometry* geometries,
	                  benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
	                  benchmark_visual::VisualInstance* instances, int instanceCapacity, int* geometryCount,
	                  int* instanceCount);
	int (*sampleTransforms)(const PhysXCaseView& state, benchmark_visual::VisualStableTransform* transforms,
	                        int capacity);
	int (*buildDebugPrimitives)(const PhysXCaseView& state, benchmark_visual::VisualDebugPrimitive* primitives,
	                            int primitiveCapacity);
};

int ResolvePhysXCase(const CaseExecutionSpec& execution, const PhysXCaseDescriptor** descriptor);
int RequestedWorkerCount(int threadCount);
int RunPhysXCase(const PhysXRunRequest& request, const PhysXCaseDescriptor& descriptor);
int RecordPhysXCase(const PhysXRunRequest& request, PhysXCaseView* state, benchmark_stack::Capture* capture = nullptr);
}
