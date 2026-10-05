#include "Cases/BenchmarkPolygonChaosCaseRegistry.h"

#include "Cases/BenchmarkPolygonChaosBoxContactIslandsCase.h"
#include "Cases/BenchmarkPolygonChaosCase.h"
#include "Cases/BenchmarkPolygonChaosLargePyramidCase.h"
#include "Cases/BenchmarkPolygonChaosPyramidWallCase.h"
#include "Cases/BenchmarkPolygonChaosRagdollStairTumbleCase.h"
#include "Cases/BenchmarkPolygonChaosSpatialQueryTraceCase.h"
#include "Runtime/BenchmarkPolygonChaosConfig.h"
#include "Runtime/BenchmarkPolygonChaosUnits.h"
#include "Chaos/Box.h"
#include "Chaos/Sphere.h"
#include "Chaos/Capsule.h"
#include "Chaos/Convex.h"

#include <array>
#include "Runner/BenchmarkPolygonChaosCli.h"
#include "replay_recording.h"

namespace BenchmarkPolygonChaos
{
Chaos::FRotation3 ChaosShapeRotation(CaseExecutionAxis Axis)
{
	const CaseExecutionQuaternion Rotation = CaseExecutionAxisRotation(Axis);
	return Chaos::FRotation3(FQuat(Rotation.x, Rotation.y, Rotation.z, Rotation.w));
}

template <class TShape> int StoreChaosResolvedShape(TShape* NativeShape, ChaosResolvedShape* Shape)
{
	Shape->Geometry = Chaos::FImplicitObjectPtr(NativeShape);
	Shape->Volume = NativeShape->GetVolume() /
	                (kChaosUnitsPerBenchmarkMeter * kChaosUnitsPerBenchmarkMeter * kChaosUnitsPerBenchmarkMeter);
	const Chaos::FMatrix33 Inertia = NativeShape->GetInertiaTensor(1.0);
	Shape->UnitInertia = Chaos::FVec3(Inertia.M[0][0], Inertia.M[1][1], Inertia.M[2][2]);
	Shape->CenterOfMass = NativeShape->GetCenterOfMass();
	Shape->RotationOfMass = NativeShape->GetRotationOfMass();
	Shape->HalfExtents = NativeShape->BoundingBox().Extents() * 0.5;
	return FMath::IsFinite(Shape->Volume) && Shape->Volume > 0.0 && Shape->UnitInertia.X > 0.0 &&
	               Shape->UnitInertia.Y > 0.0 && Shape->UnitInertia.Z > 0.0
	           ? 0
			   : 2;
}

int CreateChaosResolvedShape(const CaseExecutionSpec& Execution, const CaseExecutionGeometry& Geometry,
                             ChaosResolvedShape* Shape)
{
	Shape->BodyRotation = ChaosShapeRotation(Geometry.axis);
	const Chaos::FVec3 HalfExtents =
	    MetersToChaosUnits(Chaos::FVec3(Geometry.halfExtents.x, Geometry.halfExtents.y, Geometry.halfExtents.z));
	switch (Geometry.shape)
	{
	case CaseExecutionShape_Box:
		return StoreChaosResolvedShape(new Chaos::TBox<Chaos::FReal, 3>(-HalfExtents, HalfExtents), Shape);
	case CaseExecutionShape_Sphere:
		return StoreChaosResolvedShape(
		    new Chaos::TSphere<Chaos::FReal, 3>(Chaos::FVec3(0), MetersToChaosUnits(Geometry.radius)), Shape);
	case CaseExecutionShape_Capsule:
		return StoreChaosResolvedShape(
		    new Chaos::FCapsule(Chaos::FVec3(0, -MetersToChaosUnits(Geometry.halfSegment), 0),
			                    Chaos::FVec3(0, MetersToChaosUnits(Geometry.halfSegment), 0),
			                    MetersToChaosUnits(Geometry.radius)),
		    Shape);
	case CaseExecutionShape_ConvexHull:
	{
		TArray<Chaos::FConvex::FVec3Type> Points;
		Points.SetNumUninitialized(kCaseExecutionHullPointCount);
		for (uint32 Index = 0; Index < kCaseExecutionHullPointCount; ++Index)
		{
			const CaseExecutionVector3& Point = Execution.hullPoints[Index];
			Points[Index] =
			    Chaos::FConvex::FVec3Type(Point.x * HalfExtents.X, Point.y * HalfExtents.Y, Point.z * HalfExtents.Z);
		}
		return StoreChaosResolvedShape(new Chaos::FConvex(Points, 0.0), Shape);
	}
	default:
		return 2;
	}
}

void SetChaosResolvedMass(Chaos::FPBDRigidParticleHandle* Particle, const ChaosResolvedShape& Shape, double Mass)
{
	Particle->M() = Mass;
	Particle->InvM() = 1.0 / Mass;
	const Chaos::FVec3 Inertia = Shape.UnitInertia * Mass;
	Particle->I() = Chaos::TVec3<Chaos::FRealSingle>(Inertia.X, Inertia.Y, Inertia.Z);
	Particle->InvI() = Chaos::TVec3<Chaos::FRealSingle>(1.0 / Inertia.X, 1.0 / Inertia.Y, 1.0 / Inertia.Z);
	Particle->SetCenterOfMass(Shape.CenterOfMass);
	Particle->SetRotationOfMass(Shape.RotationOfMass);
}

const std::array<const ChaosCaseDescriptor*, 6>& CaseRegistrations()
{
	static const std::array<const ChaosCaseDescriptor*, 6> Registrations = {
	    &ContainerPileDescriptor(),      &ContactIslandsDescriptor(), &SpatialQueryTraceDescriptor(),
	    &RagdollStairTumbleDescriptor(), &LargePyramidDescriptor(), &PyramidWallDescriptor(),
	};
	return Registrations;
}

int ResolveChaosCaseDescriptor(const CaseExecutionSpec& Execution, const ChaosCaseDescriptor** Descriptor)
{
	const CaseFixtureKind FixtureKind = Execution.fixtureKind;
	if (FixtureKind != CaseFixtureKind_SpatialQueryTrace &&
	    (Execution.nativeSolver.supportedFields != 7u ||
	     Execution.nativeSolver.values[CaseSolverField_PositionIterations] > 2147483647u ||
	     Execution.nativeSolver.values[CaseSolverField_VelocityIterations] > 2147483647u ||
	     Execution.nativeSolver.values[CaseSolverField_ProjectionIterations] > 2147483647u))
		return 2;
	if (Descriptor == nullptr || FixtureKind < CaseFixtureKind_OpenContainerFallingPile ||
	    FixtureKind > CaseFixtureKind_PyramidWall)
	{
		return 2;
	}
	*Descriptor = CaseRegistrations()[static_cast<std::size_t>(FixtureKind) - 1];
	return 0;
}

int RunChaosCase(const RunnerArgs& Args, const ThreadRuntimeState& RuntimeState)
{
	return Args.CaseDescriptor == nullptr ? 2 : Args.CaseDescriptor->RunHeadless(Args, RuntimeState);
}

int BuildResolvedVisualGeometry(const CaseExecutionSpec& Execution, const CaseExecutionGeometry& Geometry,
                                benchmark_visual::VisualMeshStorage* Meshes, ChaosVisualGeometry* Visual)
{
	if (Meshes == nullptr || Visual == nullptr)
		return 2;
	benchmark_visual::VisualGeometry Value = {};
	if (Geometry.shape == CaseExecutionShape_Box)
		Value = {2u, Geometry.halfExtents.x, Geometry.halfExtents.y, Geometry.halfExtents.z};
	else if (Geometry.shape == CaseExecutionShape_Sphere)
		Value = {1u, Geometry.radius, 0.0f, 0.0f};
	else if (Geometry.shape == CaseExecutionShape_Capsule)
	{
		if (benchmark_visual::AppendVisualCapsule(Geometry.radius, Geometry.halfSegment, Meshes, &Value) != 0)
			return 2;
	}
	else if (Geometry.shape == CaseExecutionShape_ConvexHull)
	{
		benchmark_visual::VisualMeshVertex Points[kCaseExecutionHullPointCount] = {};
		for (uint32 Index = 0; Index < kCaseExecutionHullPointCount; ++Index)
			Points[Index] = {Execution.hullPoints[Index].x, Execution.hullPoints[Index].y,
			                 Execution.hullPoints[Index].z};
		if (benchmark_visual::AppendVisualBeveledBox(Points, Geometry.halfExtents.x, Geometry.halfExtents.y,
		                                             Geometry.halfExtents.z, Meshes, &Value) != 0)
			return 2;
	}
	else
		return 2;
	*Visual = {Value.kind,        Value.parameterX,  Value.parameterY, Value.parameterZ, Value.vertexOffset,
	           Value.vertexCount, Value.indexOffset, Value.indexCount, Value.edgeOffset, Value.edgeCount};
	return 0;
}

int BuildChaosVisualScene(const ChaosCaseDescriptor& Descriptor, const ChaosCaseView& State,
                          ChaosVisualGeometry* Geometries,

                          benchmark_visual::VisualMeshStorage* Meshes, int GeometryCapacity,
                          ChaosVisualInstance* Instances, int InstanceCapacity, int* GeometryCount, int* InstanceCount)
{
	return Descriptor.BuildVisualScene(State, Geometries, Meshes, GeometryCapacity, Instances, InstanceCapacity,
	                                   GeometryCount, InstanceCount);
}

int SampleChaosVisualTransforms(const ChaosCaseDescriptor& Descriptor, const ChaosCaseView& State,
                                ChaosVisualStableTransform* Transforms, int Capacity)
{
	return Descriptor.SampleVisualTransforms(State, Transforms, Capacity);
}

int BuildChaosVisualDebugPrimitives(const ChaosCaseDescriptor& Descriptor, const ChaosCaseView& State,
                                    ChaosVisualDebugPrimitive* Primitives, int Capacity)
{
	if (Capacity == 0)
	{
		return 0;
	}
	return Descriptor.BuildVisualDebugPrimitives == nullptr
	           ? 2
			   : Descriptor.BuildVisualDebugPrimitives(State, Primitives, Capacity);
}

benchmark_visual::VisualTransform RecordingTransform(const ChaosVisualTransform& Transform)
{
	return {Transform.PositionX, Transform.PositionY, Transform.PositionZ, Transform.RotationX,
	        Transform.RotationY, Transform.RotationZ, Transform.RotationW};
}

int CaptureChaosFrame(const RunnerArgs& Args, const ChaosCaseView& State, ChaosVisualStableTransform* Transforms,
                      benchmark_stack::Capture* Capture, benchmark_stack::Phase Phase, uint32 Step)
{
	benchmark_stack::BeginFrame(Capture);
	if (Args.CaseDescriptor->SampleVisualTransforms(State, Transforms, static_cast<int>(Capture->poses.size())) != 0)
		return Capture->status = 2;
	for (std::size_t Index = 0; Index < Capture->poses.size(); ++Index)
	{
		const ChaosVisualStableTransform& Pose = Transforms[Index];
		Capture->poses[Index] = {Pose.StableSlot, {Pose.Transform.PositionX, Pose.Transform.PositionY, Pose.Transform.PositionZ},
			{Pose.Transform.RotationX, Pose.Transform.RotationY, Pose.Transform.RotationZ, Pose.Transform.RotationW}};
	}
	return benchmark_stack::AppendFrame(Capture, Phase, Capture->segment, Step);
}

int RecordChaosCase(const RunnerArgs& Args, ChaosCaseView* State, benchmark_stack::Capture* Capture)
{
	using namespace benchmark_visual;
	using namespace benchmark_replay;
	const ChaosCaseDescriptor& Descriptor = *Args.CaseDescriptor;
	const CaseExecutionSpec& Execution = Args.CaseExecution;
	std::vector<ChaosVisualGeometry> NativeGeometries;
	std::vector<ChaosVisualInstance> NativeInstances;
	std::vector<ChaosVisualStableTransform> NativeTransforms;
	std::vector<ChaosVisualDebugPrimitive> NativeDebug;
	std::vector<VisualGeometry> Geometries;
	std::vector<VisualInstance> Instances;
	std::vector<VisualStableTransform> Transforms;
	std::vector<VisualDebugPrimitive> Debug;
	VisualMeshStorage Mesh = {};
	RecordingWriter* Writer = nullptr;
	RecordingStatus Status = RecordingStatus_Ok;
	if (Capture != nullptr)
		NativeTransforms.resize(Execution.dynamicBodyCount);
	if (Args.RecordingMode == RecordingMode_On)
	{
		NativeGeometries.resize(kMaxSceneGeometries);
		NativeInstances.resize(Execution.visualInstanceCount);
		NativeTransforms.resize(Execution.dynamicBodyCount);
		NativeDebug.resize(Execution.visualDebugPrimitiveCount);
		Geometries.resize(kMaxSceneGeometries);
		Transforms.resize(Execution.dynamicBodyCount);
		Debug.resize(Execution.visualDebugPrimitiveCount);
		int GeometryCount = 0;
		int InstanceCount = 0;
		if (Descriptor.BuildVisualScene(*State, NativeGeometries.data(), &Mesh,
		                                static_cast<int>(NativeGeometries.size()), NativeInstances.data(),
		                                static_cast<int>(NativeInstances.size()), &GeometryCount, &InstanceCount) != 0)
			return 2;
		Instances.resize(InstanceCount);
		for (int Index = 0; Index < GeometryCount; ++Index)
		{
			const ChaosVisualGeometry& Value = NativeGeometries[Index];
			Geometries[Index] = {Value.Kind,         Value.ParameterX,  Value.ParameterY,  Value.ParameterZ,
			                     Value.VertexOffset, Value.VertexCount, Value.IndexOffset, Value.IndexCount,
			                     Value.EdgeOffset,   Value.EdgeCount};
		}
		for (int Index = 0; Index < InstanceCount; ++Index)
		{
			const ChaosVisualInstance& Value = NativeInstances[Index];
			VisualInstance& Instance = Instances[Index];
			Instance.geometryIndex = Value.GeometryIndex;
			Instance.stableSlot = Value.StableSlot;
			Instance.transformSlot = Value.TransformSlot;
			Instance.initialTransform = RecordingTransform(Value.InitialTransform);
		}
		VisualScene Scene = {};
		SetRecordingSceneIdentity(Execution, Descriptor.EngineId, Args.ThreadCount, Args.RepeatIndex, &Scene);
		Scene.geometries = Geometries.data();
		Scene.instances = Instances.data();
		Scene.geometryCount = static_cast<uint32>(GeometryCount);
		Scene.instanceCount = static_cast<uint32>(InstanceCount);
		Scene.vertices = Mesh.vertices.data();
		Scene.vertexCount = static_cast<uint32>(Mesh.vertices.size());
		Scene.indices = Mesh.indices.data();
		Scene.indexCount = static_cast<uint32>(Mesh.indices.size());
		Scene.edges = Mesh.edges.data();
		Scene.edgeCount = static_cast<uint32>(Mesh.edges.size());
		Writer = new RecordingWriter{};
		const WorkKind Kind =
		    Execution.fixtureKind == CaseFixtureKind_SpatialQueryTrace ? WorkKind_QueryBatch : WorkKind_Dynamics;
		Status = BeginRecording(std::filesystem::path(Args.RecordingPath), Scene, Kind,
		                        Kind == WorkKind_Dynamics ? 1.0 / Execution.timestepHz : 0, Writer);
	}
	for (uint32 Ordinal = 0; Status == RecordingStatus_Ok && Ordinal <= Execution.measuredWorkUnitCount; ++Ordinal)
	{
		if (Ordinal != 0 && Descriptor.StepVisual(State, 1) != 0)
		{
			Status = RecordingStatus_Invalid;
			break;
		}
		if (Capture != nullptr && Ordinal != 0 && CaptureChaosFrame(Args, *State, NativeTransforms.data(), Capture,
			benchmark_stack::Phase_Measured, Ordinal) != 0)
		{
			Status = RecordingStatus_Invalid;
			break;
		}
		if (Args.RecordingMode == RecordingMode_Off)
			continue;
		if (((Capture == nullptr || Ordinal == 0) && Descriptor.SampleVisualTransforms(*State, NativeTransforms.data(),
		                                      static_cast<int>(NativeTransforms.size())) != 0) ||
		    BuildChaosVisualDebugPrimitives(Descriptor, *State, NativeDebug.data(),
		                                    static_cast<int>(NativeDebug.size())) != 0)
		{
			Status = RecordingStatus_Invalid;
			break;
		}
		for (uint32 Index = 0; Index < Execution.dynamicBodyCount; ++Index)
		{
			Transforms[Index] = {NativeTransforms[Index].StableSlot,
			                     RecordingTransform(NativeTransforms[Index].Transform)};
		}
		for (uint32 Index = 0; Index < Execution.visualDebugPrimitiveCount; ++Index)
		{
			const ChaosVisualDebugPrimitive& Value = NativeDebug[Index];
			Debug[Index] = {Value.Kind,
			                Value.MaterialIndex,
			                Value.OriginOrCenterX,
			                Value.OriginOrCenterY,
			                Value.OriginOrCenterZ,
			                Value.EndOrHalfExtentsX,
			                Value.EndOrHalfExtentsY,
			                Value.EndOrHalfExtentsZ,
			                Value.Radius,
			                Value.Reserved};
		}
		Status = AppendFrame(Writer, Ordinal, Transforms.data(), static_cast<uint32>(Transforms.size()), Debug.data(),
		                     static_cast<uint32>(Debug.size()));
	}
	if (Args.RecordingMode == RecordingMode_On)
	{
		if (Status == RecordingStatus_Ok)
			Status = CompleteRecording(Writer);
		else
			AbortRecording(Writer);
		delete Writer;
	}
	if (Status != RecordingStatus_Ok)
		std::fprintf(stderr, "run_failed reason=recording status=%d\n", Status);
	return Status == RecordingStatus_Ok ? 0 : 2;
}

} // namespace BenchmarkPolygonChaos
