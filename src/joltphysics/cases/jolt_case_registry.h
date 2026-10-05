#pragma once

#include "stack_state_capture.h"

#include "benchmark_visual/visual_snapshot.h"
#include "case_execution_wire.h"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <cstddef>
#include <cstdint>

namespace jolt_benchmark
{
struct JoltRunRequest;

int BuildResolvedVisualGeometry(const CaseExecutionSpec& execution, const CaseExecutionGeometry& geometry,
                                benchmark_visual::VisualMeshStorage* meshes, benchmark_visual::VisualGeometry* visual);

int CreateJoltResolvedShape(const CaseExecutionGeometry& geometry, const CaseExecutionSpec& execution,
                            JPH::RefConst<JPH::Shape>* shape);
JPH::Quat JoltShapeRotation(CaseExecutionAxis axis);

struct JoltCaseConfig
{
	const CaseExecutionSpec* caseExecution;
	int threadCount;
	int repeatIndex;
	int stepCount;
	int warmupSteps;
};

struct JoltCaseView
{
	void* value;
};

struct JoltCaseDescriptor
{
	const char* engineId;
	int (*runCase)(const JoltRunRequest& request);
	int (*stepWorkUnits)(JoltCaseView* state, int workUnitCount);
	int (*buildScene)(const JoltCaseView& state, benchmark_visual::VisualGeometry* geometries,
	                  benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
	                  benchmark_visual::VisualInstance* instances, int instanceCapacity, int* geometryCount,
	                  int* instanceCount);
	int (*sampleTransforms)(const JoltCaseView& state, benchmark_visual::VisualStableTransform* transforms,
	                        int transformCapacity);
	int (*buildDebugPrimitives)(const JoltCaseView& state, benchmark_visual::VisualDebugPrimitive* primitives,
	                            int primitiveCapacity);
};

int ResolveJoltCase(const CaseExecutionSpec& execution, const JoltCaseDescriptor** descriptor);
const JoltCaseDescriptor& DefaultJoltCase();
void InitializeJoltRuntime();
void ShutdownJoltRuntime();
int RequestedWorkerCount(int threadCount);
int RunJoltCase(const JoltRunRequest& request, const JoltCaseDescriptor& descriptor);
int RecordJoltCase(const JoltRunRequest& request, JoltCaseView* state, benchmark_stack::Capture* capture = nullptr);
}
