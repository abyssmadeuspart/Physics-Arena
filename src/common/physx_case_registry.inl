#include "replay_recording.h"

namespace PHYSICS_ARENA_PHYSX_NAMESPACE
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

int InitializePhysXResolvedShape(PhysXContext* context, const CaseExecutionSpec& execution)
{
	if (execution.shapePreset != CaseShapePreset_ConvexHull)
		return 0;
	physx::PxVec3 points[kCaseExecutionHullPointCount];
	for (std::uint32_t index = 0; index < kCaseExecutionHullPointCount; ++index)
		points[index] =
		    physx::PxVec3(execution.hullPoints[index].x, execution.hullPoints[index].y, execution.hullPoints[index].z);
	physx::PxConvexMeshDesc description;
	description.points.count = kCaseExecutionHullPointCount;
	description.points.stride = sizeof(physx::PxVec3);
	description.points.data = points;
	description.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX;
	const physx::PxCookingParams parameters(context->physics->getTolerancesScale());
#if PX_PHYSICS_VERSION_MAJOR >= 5
	context->convexMesh = PxCreateConvexMesh(parameters, description, context->physics->getPhysicsInsertionCallback());
#else
	physx::PxCooking* cooking = PxCreateCooking(PX_PHYSICS_VERSION, *context->foundation, parameters);
	if (cooking == nullptr)
		return 2;
	context->convexMesh = cooking->createConvexMesh(description, context->physics->getPhysicsInsertionCallback());
	cooking->release();
#endif
	return context->convexMesh != nullptr ? 0 : 2;
}

physx::PxQuat PhysXShapeRotation(CaseExecutionAxis axis)
{
	const CaseExecutionQuaternion rotation = CaseExecutionAxisRotation(axis);
	return physx::PxQuat(rotation.x, rotation.y, rotation.z, rotation.w);
}

physx::PxShape* AttachPhysXResolvedShape(PhysXContext* context, physx::PxRigidActor* actor,
                                         const CaseExecutionGeometry& geometry)
{
	// both creation APIs leave the attached shape owned by the actor, not the caller
	physx::PxGeometryHolder nativeGeometry;
	switch (geometry.shape)
	{
	case CaseExecutionShape_Box:
		nativeGeometry.storeAny(
		    physx::PxBoxGeometry(geometry.halfExtents.x, geometry.halfExtents.y, geometry.halfExtents.z));
		break;
	case CaseExecutionShape_Sphere:
		nativeGeometry.storeAny(physx::PxSphereGeometry(geometry.radius));
		break;
	case CaseExecutionShape_Capsule:
		nativeGeometry.storeAny(physx::PxCapsuleGeometry(geometry.radius, geometry.halfSegment));
		break;
	case CaseExecutionShape_ConvexHull:
		if (context->convexMesh == nullptr)
			return nullptr;
		nativeGeometry.storeAny(physx::PxConvexMeshGeometry(
		    context->convexMesh,
		    physx::PxMeshScale(physx::PxVec3(geometry.halfExtents.x, geometry.halfExtents.y, geometry.halfExtents.z))));
		break;
	default:
		return nullptr;
	}
#if PX_PHYSICS_VERSION_MAJOR >= 5
	physx::PxShape* shape =
	    physx::PxRigidActorExt::createExclusiveShape(*actor, nativeGeometry.any(), *context->material);
#else
	physx::PxShape* shape = actor->createShape(nativeGeometry.any(), *context->material);
#endif
	// PhysX capsules use local X while the resolved body frame uses local Y
	if (shape != nullptr && geometry.shape == CaseExecutionShape_Capsule)
		shape->setLocalPose(physx::PxTransform(physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0.0f, 0.0f, 1.0f))));
	return shape;
}

int ResolvePhysXCase(const CaseExecutionSpec& execution, const PhysXCaseDescriptor** descriptor)
{
	const CaseFixtureKind fixtureKind = execution.fixtureKind;
	if (fixtureKind != CaseFixtureKind_SpatialQueryTrace &&
	    (execution.nativeSolver.supportedFields != 3u ||
	     execution.nativeSolver.values[CaseSolverField_PositionIterations] < 1u ||
	     execution.nativeSolver.values[CaseSolverField_PositionIterations] > 255u ||
#if PX_PHYSICS_VERSION_MAJOR < 5
	     execution.nativeSolver.values[CaseSolverField_VelocityIterations] < 1u ||
#endif
	     execution.nativeSolver.values[CaseSolverField_VelocityIterations] > 255u))
		return 2;
	if (descriptor == nullptr || fixtureKind < CaseFixtureKind_OpenContainerFallingPile ||
	    fixtureKind > CaseFixtureKind_PyramidWall)
		return 2;
	const PhysXCaseDescriptor* registrations[] = {
	    &PhysXContainerPileCaseDescriptor(),     &PhysXContactIslandsCaseDescriptor(),
	    &PhysXSpatialQueryTraceCaseDescriptor(), &PhysXRagdollStairTumbleCaseDescriptor(),
	    &PhysXLargePyramidCaseDescriptor(), &PhysXPyramidWallCaseDescriptor(),
	};
	*descriptor = registrations[static_cast<std::size_t>(fixtureKind) - 1];
	return 0;
}

int RunPhysXCase(const PhysXRunRequest& request, const PhysXCaseDescriptor& descriptor)
{
	return descriptor.runCase(request);
}

int RecordPhysXCase(const PhysXRunRequest& request, PhysXCaseView* state, benchmark_stack::Capture* capture)
{
	using namespace benchmark_visual;
	using namespace benchmark_replay;
	const PhysXCaseDescriptor* descriptor = nullptr;
	if (ResolvePhysXCase(request.caseExecution, &descriptor) != 0)
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
