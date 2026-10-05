#include "jolt_runner_args.h"
#include "replay_recording.h"
#include "jolt_case_registry.h"

#include "jolt_box_contact_islands_10k_case.h"
#include "jolt_box_container_pile_10k_case.h"
#include "jolt_large_pyramid_case.h"
#include "jolt_pyramid_wall_case.h"
#include "jolt_ragdoll_stair_tumble_case.h"
#include "jolt_spatial_query_trace_case.h"
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>

namespace jolt_benchmark
{
int BuildResolvedVisualGeometry(const CaseExecutionSpec& execution, const CaseExecutionGeometry& geometry,
                                benchmark_visual::VisualMeshStorage* meshes, benchmark_visual::VisualGeometry* visual)
{
	switch (geometry.shape)
	{
	case CaseExecutionShape_Box:
		*visual = {benchmark_visual::VisualGeometryKind_Box,
		           geometry.halfExtents.x,
		           geometry.halfExtents.y,
		           geometry.halfExtents.z,
		           0,
		           0,
		           0,
		           0,
		           0,
		           0};
		return 0;
	case CaseExecutionShape_Sphere:
		*visual = {benchmark_visual::VisualGeometryKind_Sphere, geometry.radius, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0};
		return 0;
	case CaseExecutionShape_Capsule:
		return benchmark_visual::AppendVisualCapsule(geometry.radius, geometry.halfSegment, meshes, visual);
	case CaseExecutionShape_ConvexHull:
	{
		benchmark_visual::VisualMeshVertex points[kCaseExecutionHullPointCount];
		for (uint32_t index = 0; index < kCaseExecutionHullPointCount; ++index)
			points[index] = {execution.hullPoints[index].x, execution.hullPoints[index].y,
			                 execution.hullPoints[index].z};
		return benchmark_visual::AppendVisualBeveledBox(points, geometry.halfExtents.x, geometry.halfExtents.y,
		                                                geometry.halfExtents.z, meshes, visual);
	}
	default:
		return 2;
	}
}

JPH::Quat JoltShapeRotation(CaseExecutionAxis axis)
{
	const CaseExecutionQuaternion rotation = CaseExecutionAxisRotation(axis);
	return JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w);
}

int CreateJoltResolvedShape(const CaseExecutionGeometry& geometry, const CaseExecutionSpec& execution,
                            JPH::RefConst<JPH::Shape>* shape)
{
	JPH::Shape::ShapeResult result;
	if (geometry.shape == CaseExecutionShape_Box)
		result = JPH::BoxShapeSettings(
		             JPH::Vec3(geometry.halfExtents.x, geometry.halfExtents.y, geometry.halfExtents.z), 0.0f)
		             .Create();
	else if (geometry.shape == CaseExecutionShape_Sphere)
		result = JPH::SphereShapeSettings(geometry.radius).Create();
	else if (geometry.shape == CaseExecutionShape_Capsule)
		result = JPH::CapsuleShapeSettings(geometry.halfSegment, geometry.radius).Create();
	else
	{
		JPH::Vec3 points[kCaseExecutionHullPointCount];
		for (std::uint32_t index = 0; index < kCaseExecutionHullPointCount; ++index)
		{
			const CaseExecutionVector3 point = execution.hullPoints[index];
			points[index] = JPH::Vec3(point.x * geometry.halfExtents.x, point.y * geometry.halfExtents.y,
			                          point.z * geometry.halfExtents.z);
		}
		result = JPH::ConvexHullShapeSettings(points, kCaseExecutionHullPointCount, 0.0f).Create();
	}
	if (result.HasError())
		return 2;
	*shape = result.Get();
	return 0;
}

const JoltCaseDescriptor* const CaseRegistrations[] = {
    &JoltContainerPileCaseDescriptor(),     &JoltContactIslandsCaseDescriptor(),
    &JoltSpatialQueryTraceCaseDescriptor(), &JoltRagdollStairTumbleCaseDescriptor(),
    &JoltLargePyramidCaseDescriptor(), &JoltPyramidWallCaseDescriptor(),
};

int ResolveJoltCase(const CaseExecutionSpec& execution, const JoltCaseDescriptor** descriptor)
{
	const CaseFixtureKind fixtureKind = execution.fixtureKind;
	if (fixtureKind != CaseFixtureKind_SpatialQueryTrace &&
	    (execution.nativeSolver.supportedFields != 35u ||
	     execution.nativeSolver.values[CaseSolverField_CollisionSteps] < 1u ||
	     execution.nativeSolver.values[CaseSolverField_VelocityIterations] > 255u ||
	     execution.nativeSolver.values[CaseSolverField_PositionIterations] > 255u ||
	     execution.nativeSolver.values[CaseSolverField_CollisionSteps] > 2147483647u))
		return 2;
	if (descriptor == nullptr || fixtureKind < CaseFixtureKind_OpenContainerFallingPile ||
	    fixtureKind > CaseFixtureKind_PyramidWall)
	{
		return 2;
	}
	*descriptor = CaseRegistrations[static_cast<std::size_t>(fixtureKind) - 1];
	return 0;
}

const JoltCaseDescriptor& DefaultJoltCase()
{
	return *CaseRegistrations[0];
}

int RunJoltCase(const JoltRunRequest& request, const JoltCaseDescriptor& descriptor)
{
	return descriptor.runCase(request);
}

int RecordJoltCase(const JoltRunRequest& request, JoltCaseView* state, benchmark_stack::Capture* capture)
{
	using namespace benchmark_visual;
	using namespace benchmark_replay;
	const JoltCaseDescriptor* descriptor = nullptr;
	if (ResolveJoltCase(request.caseExecution, &descriptor) != 0)
		return 2;
	const CaseExecutionSpec& execution = request.caseExecution;
	std::vector<VisualGeometry> geometries;
	std::vector<VisualInstance> instances;
	std::vector<VisualStableTransform> transforms;
	std::vector<VisualDebugPrimitive> debug;
	VisualMeshStorage mesh = {};
	RecordingWriter* writer = nullptr;
	RecordingStatus status = RecordingStatus_Ok;
	if (capture != nullptr)
		transforms.resize(execution.dynamicBodyCount);
	if (request.recordingMode == RecordingMode_On)
	{
		geometries.resize(kMaxSceneGeometries);
		instances.resize(execution.visualInstanceCount);
		transforms.resize(execution.dynamicBodyCount);
		debug.resize(execution.visualDebugPrimitiveCount);
		int geometryCount = 0;
		int instanceCount = 0;
		if (descriptor->buildScene(*state, geometries.data(), &mesh, static_cast<int>(geometries.size()),
		                           instances.data(), static_cast<int>(instances.size()), &geometryCount,
		                           &instanceCount) != 0)
			return 2;
		VisualScene scene = {};
		SetRecordingSceneIdentity(execution, descriptor->engineId, request.threadCount, request.repeatIndex, &scene);
		scene.geometries = geometries.data();
		scene.instances = instances.data();
		scene.geometryCount = static_cast<std::uint32_t>(geometryCount);
		scene.instanceCount = static_cast<std::uint32_t>(instanceCount);
		scene.vertices = mesh.vertices.data();
		scene.vertexCount = static_cast<std::uint32_t>(mesh.vertices.size());
		scene.indices = mesh.indices.data();
		scene.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
		scene.edges = mesh.edges.data();
		scene.edgeCount = static_cast<std::uint32_t>(mesh.edges.size());
		writer = new RecordingWriter{};
		const WorkKind kind =
		    execution.fixtureKind == CaseFixtureKind_SpatialQueryTrace ? WorkKind_QueryBatch : WorkKind_Dynamics;
		status = BeginRecording(std::filesystem::u8path(request.recordingPath), scene, kind,
		                        kind == WorkKind_Dynamics ? 1.0 / execution.timestepHz : 0, writer);
	}
	for (std::uint32_t ordinal = 0; status == RecordingStatus_Ok && ordinal <= execution.measuredWorkUnitCount;
	     ++ordinal)
	{
		if (ordinal != 0 && descriptor->stepWorkUnits(state, 1) != 0)
		{
			status = RecordingStatus_Invalid;
			break;
		}
		if (capture != nullptr && ordinal != 0)
		{
			benchmark_stack::BeginFrame(capture);
			if (descriptor->sampleTransforms(*state, transforms.data(), static_cast<int>(transforms.size())) != 0)
			{
				capture->status = 2;
				status = RecordingStatus_Invalid;
				break;
			}
			if (benchmark_stack::AppendTransforms(capture, benchmark_stack::Phase_Measured, capture->segment, ordinal, transforms.data()) != 0)
			{
				status = RecordingStatus_Invalid;
				break;
			}
		}
		if (request.recordingMode == RecordingMode_Off)
			continue;
		if (((capture == nullptr || ordinal == 0) &&
		     descriptor->sampleTransforms(*state, transforms.data(), static_cast<int>(transforms.size())) != 0) ||
		    (!debug.empty() &&
		     descriptor->buildDebugPrimitives(*state, debug.data(), static_cast<int>(debug.size())) != 0))
		{
			status = RecordingStatus_Invalid;
			break;
		}
		status = AppendFrame(writer, ordinal, transforms.data(), static_cast<std::uint32_t>(transforms.size()),
		                     debug.data(), static_cast<std::uint32_t>(debug.size()));
	}
	if (request.recordingMode == RecordingMode_On)
	{
		if (status == RecordingStatus_Ok)
			status = CompleteRecording(writer);
		else
			AbortRecording(writer);
		delete writer;
	}
	if (status != RecordingStatus_Ok)
		std::fprintf(stderr, "run_failed reason=recording status=%d\n", status);
	return status == RecordingStatus_Ok ? 0 : 2;
}
}
