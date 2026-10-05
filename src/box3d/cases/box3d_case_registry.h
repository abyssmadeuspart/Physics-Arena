#pragma once

#include "benchmark_visual/visual_snapshot.h"
#include "case_execution_wire.h"
#include <box3d/box3d.h>
#include <box3d/collision.h>

#include <cstddef>
#include <cstdint>

namespace box3d_benchmark
{
struct Box3DRunRequest;

int BuildResolvedVisualGeometry(const CaseExecutionSpec& execution, const CaseExecutionGeometry& geometry,
                                benchmark_visual::VisualMeshStorage* meshes, benchmark_visual::VisualGeometry* visual);

struct Box3DResolvedShape
{
	CaseExecutionShape kind;
	b3BoxHull box;
	b3HullData* hull;
	b3Sphere sphere;
	b3Capsule capsule;
	b3Quat rotation;
	float unitMass;
};

int CreateBox3DResolvedShape(const CaseExecutionGeometry& geometry, const CaseExecutionSpec& execution,
                             Box3DResolvedShape* shape);
void DestroyBox3DResolvedShape(Box3DResolvedShape* shape);
int AttachBox3DResolvedShape(b3BodyId body, const b3ShapeDef& definition, const Box3DResolvedShape& shape);

struct Box3DCaseConfig
{
	const CaseExecutionSpec* caseExecution;
	int threadCount;
	int repeatIndex;
	int stepCount;
	int warmupSteps;
};

struct Box3DCaseView
{
	void* value;
};

struct Box3DCaseDescriptor
{
	const char* engineId;
	int (*runCase)(const Box3DRunRequest& request);
	int (*stepWorkUnits)(Box3DCaseView* state, int workUnitCount);
	int (*buildScene)(const Box3DCaseView& state, benchmark_visual::VisualGeometry* geometries,
	                  benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
	                  benchmark_visual::VisualInstance* instances, int instanceCapacity, int* geometryCount,
	                  int* instanceCount);
	int (*sampleTransforms)(const Box3DCaseView& state, benchmark_visual::VisualStableTransform* transforms,
	                        int transformCapacity);
	int (*buildDebugPrimitives)(const Box3DCaseView& state, benchmark_visual::VisualDebugPrimitive* primitives,
	                            int primitiveCapacity);
};

int ResolveBox3DCase(const CaseExecutionSpec& execution, const Box3DCaseDescriptor** descriptor);
const Box3DCaseDescriptor& DefaultBox3DCase();
int RequestedWorkerCount(int threadCount);
int RunBox3DCase(const Box3DRunRequest& request, const Box3DCaseDescriptor& descriptor);
}
