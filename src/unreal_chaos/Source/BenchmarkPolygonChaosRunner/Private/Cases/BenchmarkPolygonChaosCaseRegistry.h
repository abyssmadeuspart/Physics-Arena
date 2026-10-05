#pragma once

#include "CoreMinimal.h"
#include "Windows/WindowsHWrapper.h"
#pragma push_macro("near")
#pragma push_macro("far")
#include "stack_state_capture.h"
#pragma pop_macro("far")
#pragma pop_macro("near")
#include "case_execution_wire.h"
#include "benchmark_visual/visual_snapshot.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ParticleHandle.h"

namespace BenchmarkPolygonChaos
{
struct ChaosResolvedShape
{
	Chaos::FImplicitObjectPtr Geometry;
	Chaos::FVec3 HalfExtents;
	Chaos::FVec3 UnitInertia;
	Chaos::FVec3 CenterOfMass;
	Chaos::FRotation3 RotationOfMass;
	Chaos::FRotation3 BodyRotation;
	double Volume;
};

int CreateChaosResolvedShape(const CaseExecutionSpec& Execution, const CaseExecutionGeometry& Geometry,
                             ChaosResolvedShape* Shape);
Chaos::FRotation3 ChaosShapeRotation(CaseExecutionAxis Axis);
void SetChaosResolvedMass(Chaos::FPBDRigidParticleHandle* Particle, const ChaosResolvedShape& Shape, double Mass);

struct RunnerArgs;
struct ThreadRuntimeState;
struct ChaosCaseView;
struct ChaosVisualTransform;
struct ChaosVisualGeometry;
struct ChaosVisualInstance;
struct ChaosVisualStableTransform;
struct ChaosVisualDebugPrimitive;

struct ChaosCaseDescriptor
{
	const char* EngineId;
	int (*RunHeadless)(const RunnerArgs& Args, const ThreadRuntimeState& RuntimeState);
	int (*StepVisual)(ChaosCaseView* State, int WorkUnitCount);
	int (*BuildVisualScene)(const ChaosCaseView& State, ChaosVisualGeometry* Geometries,

	                        benchmark_visual::VisualMeshStorage* Meshes, int GeometryCapacity,
	                        ChaosVisualInstance* Instances, int InstanceCapacity, int* GeometryCount,
	                        int* InstanceCount);
	int (*SampleVisualTransforms)(const ChaosCaseView& State, ChaosVisualStableTransform* Transforms, int Capacity);
	int (*BuildVisualDebugPrimitives)(const ChaosCaseView& State, ChaosVisualDebugPrimitive* Primitives, int Capacity);
};

struct ChaosCaseView
{
	void* Handle = nullptr;
};

struct ChaosVisualTransform
{
	float PositionX;
	float PositionY;
	float PositionZ;
	float RotationX;
	float RotationY;
	float RotationZ;
	float RotationW;
};

struct ChaosVisualGeometry
{
	uint32 Kind;
	float ParameterX;
	float ParameterY;
	float ParameterZ;
	uint32 VertexOffset;
	uint32 VertexCount;
	uint32 IndexOffset;
	uint32 IndexCount;
	uint32 EdgeOffset;
	uint32 EdgeCount;
};

int BuildResolvedVisualGeometry(const CaseExecutionSpec& Execution, const CaseExecutionGeometry& Geometry,
                                benchmark_visual::VisualMeshStorage* Meshes, ChaosVisualGeometry* Visual);

struct ChaosVisualInstance
{
	uint32 GeometryIndex;
	uint32 StableSlot;
	uint32 TransformSlot;
	ChaosVisualTransform InitialTransform;
};

struct ChaosVisualStableTransform
{
	uint32 StableSlot;
	ChaosVisualTransform Transform;
};

struct ChaosVisualDebugPrimitive
{
	uint32 Kind;
	uint32 MaterialIndex;
	float OriginOrCenterX;
	float OriginOrCenterY;
	float OriginOrCenterZ;
	float EndOrHalfExtentsX;
	float EndOrHalfExtentsY;
	float EndOrHalfExtentsZ;
	float Radius;
	uint32 Reserved;
};

static_assert(sizeof(ChaosVisualDebugPrimitive) == 40);

int ResolveChaosCaseDescriptor(const CaseExecutionSpec& Execution, const ChaosCaseDescriptor** Descriptor);
int CaptureChaosFrame(const RunnerArgs& Args, const ChaosCaseView& State, ChaosVisualStableTransform* Transforms,
                      benchmark_stack::Capture* Capture, benchmark_stack::Phase Phase, uint32 Step);
int RecordChaosCase(const RunnerArgs& Args, ChaosCaseView* State, benchmark_stack::Capture* Capture = nullptr);
int RunChaosCase(const RunnerArgs& Args, const ThreadRuntimeState& RuntimeState);
int BuildChaosVisualScene(const ChaosCaseDescriptor& Descriptor, const ChaosCaseView& State,
                          ChaosVisualGeometry* Geometries,

                          benchmark_visual::VisualMeshStorage* Meshes, int GeometryCapacity,
                          ChaosVisualInstance* Instances, int InstanceCapacity, int* GeometryCount, int* InstanceCount);
int SampleChaosVisualTransforms(const ChaosCaseDescriptor& Descriptor, const ChaosCaseView& State,
                                ChaosVisualStableTransform* Transforms, int Capacity);
int BuildChaosVisualDebugPrimitives(const ChaosCaseDescriptor& Descriptor, const ChaosCaseView& State,
                                    ChaosVisualDebugPrimitive* Primitives, int Capacity);
} // namespace BenchmarkPolygonChaos
