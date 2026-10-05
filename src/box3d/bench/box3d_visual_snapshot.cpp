#include "box3d_visual_snapshot.h"
#include "replay_recording.h"

#include <array>
#include <vector>

namespace box3d_benchmark
{
int RecordBox3DCase(const Box3DRunRequest& request, Box3DCaseView* state, benchmark_stack::Capture* capture)
{
	using namespace benchmark_visual;
	using namespace benchmark_replay;
	const Box3DCaseDescriptor* descriptor = nullptr;
	if (ResolveBox3DCase(request.caseExecution, &descriptor) != 0)
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
