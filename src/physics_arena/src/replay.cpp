#include "physics_arena/replay.h"
#include "physics_arena/release_contracts.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace physics_arena
{
using namespace benchmark_replay;
using namespace benchmark_visual;

ArenaStatus ReplayError(StatusRecord* error, std::string_view detail)
{
	if (error != nullptr)
	{
		*error = {};
		const std::string_view component = "replay";
		const std::string_view status = "invalid_result";
		std::copy(component.begin(), component.end(), error->component.begin());
		error->componentSize = static_cast<std::uint32_t>(component.size());
		std::copy(status.begin(), status.end(), error->status.begin());
		error->statusSize = static_cast<std::uint32_t>(status.size());
		std::copy(detail.begin(), detail.end(), error->detail.begin());
		error->detailSize = static_cast<std::uint32_t>(detail.size());
		error->code = ArenaStatus_InvalidResult;
	}
	return ArenaStatus_InvalidResult;
}

namespace
{

int ReadAt(std::FILE* file, std::uint64_t offset, void* output, std::size_t size)
{
	if (offset > static_cast<std::uint64_t>(INT64_MAX))
		return 0;
#ifdef _WIN32
	if (_fseeki64(file, static_cast<std::int64_t>(offset), SEEK_SET) != 0)
		return 0;
#else
	if (fseeko(file, static_cast<off_t>(offset), SEEK_SET) != 0)
		return 0;
#endif
	return std::fread(output, 1, size, file) == size ? 1 : 0;
}

ArenaStatus ReadHeader(const std::array<std::uint8_t, kHeaderBytes>& header, ReplayRecording* recording,
                       StatusRecord* error)
{
	const std::uint8_t* input = header.data();
	if (std::memcmp(input, "BPREPLAY", 8) != 0)
		return ReplayError(error, "recording_magic");
	input += 8;
	recording->formatVersion = GetU32(input);
	if (recording->formatVersion != kFormatVersion && recording->formatVersion != kCompressedReplayVersion)
		return ReplayError(error, "unsupported_recording_format");
	if (GetU32(input) != kHeaderBytes)
		return ReplayError(error, "recording_header_size");
	VisualScene& scene = recording->scene;
	char* texts[] = {scene.identity.caseId, scene.identity.engineId, scene.identity.fixtureSemantic,
	                 scene.identity.fixtureVersion};
	for (char* text : texts)
	{
		std::memcpy(text, input, 64);
		input += 64;
		if (text[0] == 0 || std::memchr(text, 0, 64) == nullptr)
			return ReplayError(error, "recording_identity_text");
	}
	int* counts[] = {&scene.identity.threadCount, &scene.identity.repeatIndex, &scene.identity.stepCount,
	                 &scene.identity.warmupSteps, &scene.identity.bodyCount,   &scene.identity.shapeCount};
	for (int* count : counts)
	{
		const std::uint32_t value = GetU32(input);
		if (value > INT32_MAX)
			return ReplayError(error, "recording_identity_count");
		*count = static_cast<int>(value);
	}
	recording->workKind = static_cast<WorkKind>(GetU32(input));
	recording->timestep = GetDouble(input);
	scene.geometryCount = GetU32(input);
	scene.instanceCount = GetU32(input);
	scene.dynamicTransformCount = GetU32(input);
	scene.debugPrimitiveCount = GetU32(input);
	scene.vertexCount = GetU32(input);
	scene.indexCount = GetU32(input);
	scene.edgeCount = GetU32(input);
	if (CalculateLayout(scene, recording->workKind, recording->timestep, &recording->layout) != RecordingStatus_Ok)
		return ReplayError(error, "recording_counts_or_work_kind");
	RecordingLayout& layout = recording->layout;
	if (recording->formatVersion == kCompressedReplayVersion)
		layout.frameStride -= std::uint64_t{scene.dynamicTransformCount} * 4;
	if (GetU64(input) != kHeaderBytes || GetU64(input) != layout.sceneBytes || GetU64(input) != layout.framesOffset ||
	    GetU64(input) != layout.frameStride || GetU64(input) != layout.frameCount)
		return ReplayError(error, "recording_offsets_or_stride");
	const std::uint64_t trailerOffset = GetU64(input);
	if (recording->formatVersion == kFormatVersion && trailerOffset != layout.trailerOffset)
		return ReplayError(error, "recording_offsets_or_stride");
	if (trailerOffset < layout.framesOffset || trailerOffset > INT64_MAX - kTrailerBytes)
		return ReplayError(error, "recording_payload_bounds");
	layout.trailerOffset = trailerOffset;
	layout.fileBytes = trailerOffset + kTrailerBytes;
	return ArenaStatus_Ok;
}

int SameIdentity(const VisualRunIdentity& left, const VisualRunIdentity& right)
{
	return std::strncmp(left.caseId, right.caseId, 64) == 0 && std::strncmp(left.engineId, right.engineId, 64) == 0 &&
	               std::strncmp(left.fixtureSemantic, right.fixtureSemantic, 64) == 0 &&
	               std::strncmp(left.fixtureVersion, right.fixtureVersion, 64) == 0 &&
	               left.threadCount == right.threadCount && left.repeatIndex == right.repeatIndex &&
	               left.stepCount == right.stepCount && left.warmupSteps == right.warmupSteps &&
	               left.bodyCount == right.bodyCount && left.shapeCount == right.shapeCount
	           ? 1
			   : 0;
}

ArenaStatus ReadScene(ReplayRecording* recording, StatusRecord* error)
{
	std::vector<std::uint8_t> encoded(static_cast<std::size_t>(recording->layout.sceneBytes));
	if (ReadAt(recording->file, kHeaderBytes, encoded.data(), encoded.size()) == 0)
		return ReplayError(error, "truncated_recording_scene");
	const std::uint8_t* input = encoded.data();
	VisualScene& scene = recording->scene;
	VisualCameraPolicy& camera = scene.camera;
	camera.direction = GetVector(input);
	camera.up = GetVector(input);
	camera.minimum = GetVector(input);
	camera.maximum = GetVector(input);
	camera.eye = GetVector(input);
	camera.target = GetVector(input);
	camera.eyeOffset = GetVector(input);
	camera.targetOffset = GetVector(input);
	camera.verticalFovDegrees = GetFloat(input);
	camera.viewportFill = GetFloat(input);
	camera.nearPlane = GetFloat(input);
	camera.farPlane = GetFloat(input);
	camera.stableSlot = GetU32(input);
	camera.mode = static_cast<VisualCameraMode>(GetU32(input));
	recording->geometries.resize(scene.geometryCount);
	for (VisualGeometry& geometry : recording->geometries)
	{
		geometry.kind = GetU32(input);
		geometry.parameterX = GetFloat(input);
		geometry.parameterY = GetFloat(input);
		geometry.parameterZ = GetFloat(input);
		geometry.vertexOffset = GetU32(input);
		geometry.vertexCount = GetU32(input);
		geometry.indexOffset = GetU32(input);
		geometry.indexCount = GetU32(input);
		geometry.edgeOffset = GetU32(input);
		geometry.edgeCount = GetU32(input);
	}
	recording->instances.resize(scene.instanceCount);
	for (VisualInstance& instance : recording->instances)
	{
		instance = {};
		instance.geometryIndex = GetU32(input);
		instance.stableSlot = GetU32(input);
		instance.materialIndex = GetU32(input);
		instance.transformSlot = GetU32(input);
		instance.initialTransform = GetTransform(input);
	}
	recording->mesh.vertices.resize(scene.vertexCount);
	for (VisualMeshVertex& vertex : recording->mesh.vertices)
	{
		vertex.x = GetFloat(input);
		vertex.y = GetFloat(input);
		vertex.z = GetFloat(input);
	}
	recording->mesh.indices.resize(scene.indexCount);
	for (std::uint32_t& index : recording->mesh.indices)
		index = GetU32(input);
	recording->mesh.edges.resize(scene.edgeCount);
	for (VisualMeshEdge& edge : recording->mesh.edges)
	{
		edge.a = GetU32(input);
		edge.b = GetU32(input);
	}
	scene.geometries = recording->geometries.data();
	scene.instances = recording->instances.data();
	scene.vertices = recording->mesh.vertices.data();
	scene.indices = recording->mesh.indices.data();
	scene.edges = recording->mesh.edges.data();
	if (ValidateScene(&scene) != VisualBridgeStatus_Ok)
		return ReplayError(error, "recording_scene_bounds");
	return ArenaStatus_Ok;
}

int ValidTransform(const VisualTransform& value)
{
	const double norm = static_cast<double>(value.rotationX) * value.rotationX +
	                    static_cast<double>(value.rotationY) * value.rotationY +
	                    static_cast<double>(value.rotationZ) * value.rotationZ +
	                    static_cast<double>(value.rotationW) * value.rotationW;
	return std::isfinite(value.positionX) && std::isfinite(value.positionY) && std::isfinite(value.positionZ) &&
	               std::isfinite(norm) && norm > 0.99 && norm < 1.01
	           ? 1
			   : 0;
}

int ValidDebug(const VisualDebugPrimitive& value)
{
	if (value.kind > VisualDebugPrimitiveKind_AabbOverlap || (value.materialIndex != 6 && value.materialIndex != 7) ||
	    !std::isfinite(value.originOrCenterX) || !std::isfinite(value.originOrCenterY) ||
	    !std::isfinite(value.originOrCenterZ) || !std::isfinite(value.endOrHalfExtentsX) ||
	    !std::isfinite(value.endOrHalfExtentsY) || !std::isfinite(value.endOrHalfExtentsZ) ||
	    !std::isfinite(value.radius) || value.reserved != 0 ||
	    (value.kind == VisualDebugPrimitiveKind_SphereCast ? value.radius <= 0 : value.radius != 0))
		return 0;
	if (value.kind == VisualDebugPrimitiveKind_AabbOverlap &&
	    (value.endOrHalfExtentsX <= 0 || value.endOrHalfExtentsY <= 0 || value.endOrHalfExtentsZ <= 0))
		return 0;
	return 1;
}
}

std::filesystem::path ReplayTuplePath(const std::filesystem::path& resultDirectory, std::string_view engineId,
                                      std::uint32_t threadCount, std::uint32_t repeatIndex)
{
	std::array<char, 128> name = {};
	std::snprintf(name.data(), name.size(), "%.*s_t%u_r%u.bpr", static_cast<int>(engineId.size()), engineId.data(),
	              threadCount, repeatIndex);
	return resultDirectory / "replays" / name.data();
}

void CloseReplay(ReplayRecording* recording)
{
	CloseCompressedReplay(recording);
	if (recording->file != nullptr)
		std::fclose(recording->file);
	*recording = {};
}

ArenaStatus OpenResultReplay(const Catalog* catalog, const ResultManifestRecord* manifest,
                             const std::filesystem::path& resultDirectory, std::uint32_t engineOrdinal,
                             std::uint32_t threadOrdinal, std::uint32_t repeatIndex, ReplayRecording* recording,
                             StatusRecord* error)
{
	if (catalog == nullptr || manifest == nullptr || recording == nullptr || error == nullptr ||
	    manifest->recordingMode != RecordingMode_On || engineOrdinal >= manifest->engineCount ||
	    threadOrdinal >= manifest->threadCount || repeatIndex >= manifest->repeatCount)
		return ReplayError(error, "recorded_result_tuple_required");
	if (manifest->recordingKind == RecordingKind_NativeRayHits)
		return ReplayError(error, "ray_recording_requires_image_view");
	if (RecordingForThread(manifest->recordingThreads, manifest->threadCounts[threadOrdinal]) == RecordingMode_Off)
		return ReplayError(error, "selected_thread_not_recorded");
	const std::uint32_t engineIndex = manifest->engines[engineOrdinal].engineIndex;
	if (engineIndex >= catalog->engineCount)
		return ReplayError(error, "recording_engine_identity");
	const std::string_view engineId = CatalogTextView(catalog, catalog->engines[engineIndex].id);
	std::array<char, kVisualBridgeTextCapacity> engineText = {};
	if (engineId.size() >= engineText.size())
		return ReplayError(error, "recording_engine_identity");
	std::copy(engineId.begin(), engineId.end(), engineText.begin());
	const std::filesystem::path path =
	    ReplayTuplePath(resultDirectory, engineId, manifest->threadCounts[threadOrdinal], repeatIndex);
	for (std::filesystem::path parent = path; !parent.empty(); parent = parent.parent_path())
	{
		const DWORD attributes = GetFileAttributesW(parent.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
			return ReplayError(error, "recording_path_missing_or_reparse");
		if (parent == parent.root_path())
			break;
	}
	if (GetFileAttributesW((path.wstring() + L".partial").c_str()) != INVALID_FILE_ATTRIBUTES)
		return ReplayError(error, "recording_partial_present");
	const CaseExecutionSpec& execution = manifest->configuration.execution;
	VisualScene expected = {};
	SetRecordingSceneIdentity(execution, engineText.data(), manifest->threadCounts[threadOrdinal], repeatIndex,
	                          &expected);
	ArenaStatus status = OpenReplay(path, &expected.identity, recording, error);
	if (status != ArenaStatus_Ok)
		return status;
	const WorkKind kind =
	    execution.fixtureKind == CaseFixtureKind_SpatialQueryTrace ? WorkKind_QueryBatch : WorkKind_Dynamics;
	if (recording->formatVersion != manifest->recordingVersion ||
	    recording->scene.instanceCount != execution.visualInstanceCount ||
	    recording->scene.dynamicTransformCount != execution.dynamicBodyCount ||
	    recording->scene.debugPrimitiveCount != execution.visualDebugPrimitiveCount || recording->workKind != kind ||
	    recording->timestep != (kind == WorkKind_QueryBatch ? 0 : 1.0 / execution.timestepHz))
	{
		CloseReplay(recording);
		return ReplayError(error, "recording_effective_configuration_mismatch");
	}
	return ArenaStatus_Ok;
}

ArenaStatus OpenReplay(const std::filesystem::path& path, const benchmark_visual::VisualRunIdentity* expectedIdentity,
                       ReplayRecording* recording, StatusRecord* error)
{
	if (recording == nullptr || recording->file != nullptr ||
	    (path.extension() != ".bpr" && path.extension() != ".partial"))
		return ReplayError(error, "recording_path_or_open_state");
	std::error_code filesystemError;
	const std::uint64_t fileBytes = std::filesystem::file_size(path, filesystemError);
	if (filesystemError || fileBytes < kHeaderBytes + kTrailerBytes)
		return ReplayError(error, "missing_or_truncated_recording");
#ifdef _WIN32
	if (_wfopen_s(&recording->file, path.c_str(), L"rb") != 0)
		recording->file = nullptr;
#else
	recording->file = std::fopen(path.c_str(), "rb");
#endif
	if (recording->file == nullptr)
		return ReplayError(error, "recording_open_failed");
	std::array<std::uint8_t, kHeaderBytes> header = {};
	ArenaStatus status = ReadAt(recording->file, 0, header.data(), header.size()) == 0
	                         ? ReplayError(error, "truncated_recording_header")
	                         : ReadHeader(header, recording, error);
	if (status == ArenaStatus_Ok &&
	    (fileBytes != recording->layout.fileBytes ||
	     (expectedIdentity != nullptr && SameIdentity(recording->scene.identity, *expectedIdentity) == 0)))
		status = ReplayError(error, "recording_length_or_tuple_mismatch");
	if (status == ArenaStatus_Ok)
	{
		std::array<std::uint8_t, kTrailerBytes> trailer = {};
		const std::uint8_t* input = trailer.data() + 8;
		if (ReadAt(recording->file, recording->layout.trailerOffset, trailer.data(), trailer.size()) == 0 ||
		    std::memcmp(trailer.data(), recording->formatVersion == kFormatVersion ? "BPRDONE1" : "BPRDONE2", 8) != 0 ||
		    GetU64(input) != recording->layout.frameCount || GetU64(input) != recording->layout.fileBytes)
			status = ReplayError(error, "recording_completion_trailer");
	}
	if (status == ArenaStatus_Ok)
		status = ReadScene(recording, error);
	if (status == ArenaStatus_Ok)
	{
		std::array<std::uint64_t, (kMaxSceneInstances + 63) / 64> stableSlots = {};
		for (const VisualInstance& instance : recording->instances)
		{
			if (ValidTransform(instance.initialTransform) == 0 || instance.materialIndex > 7)
			{
				status = ReplayError(error, "recording_initial_transform_or_material");
				break;
			}
			if (instance.stableSlot >= recording->scene.instanceCount ||
			    (instance.transformSlot != UINT32_MAX && instance.stableSlot >= recording->scene.dynamicTransformCount))
			{
				status = ReplayError(error, "recording_scene_stable_slot_range");
				break;
			}
			const std::uint64_t bit = std::uint64_t{1} << (instance.stableSlot % 64);
			std::uint64_t& word = stableSlots[instance.stableSlot / 64];
			if ((word & bit) != 0)
			{
				status = ReplayError(error, "recording_scene_duplicate_stable_slot");
				break;
			}
			word |= bit;
		}
	}
	if (status == ArenaStatus_Ok)
	{
		recording->encodedFrame.resize(static_cast<std::size_t>(recording->layout.frameStride));
		recording->frame.transforms.resize(recording->scene.dynamicTransformCount);
		recording->frame.debug.resize(recording->scene.debugPrimitiveCount);
		if (recording->formatVersion == kCompressedReplayVersion)
			status = OpenCompressedReplay(recording, error);
		if (status == ArenaStatus_Ok)
			status = SeekReplay(recording, 0, error);
	}
	if (status != ArenaStatus_Ok)
		CloseReplay(recording);
	return status;
}

ArenaStatus SeekReplay(ReplayRecording* recording, std::uint64_t ordinal, StatusRecord* error)
{
	if (recording == nullptr || recording->file == nullptr || ordinal >= recording->layout.frameCount)
		return ReplayError(error, "recording_ordinal_out_of_range");
	if (recording->frame.presence == PresenceStatus_Present && recording->frame.ordinal == ordinal)
		return ArenaStatus_Ok;
	recording->frame.presence = PresenceStatus_Absent;
	if (recording->formatVersion == kCompressedReplayVersion)
	{
		const ArenaStatus status = ReadCompressedReplayFrame(recording, ordinal, error);
		if (status != ArenaStatus_Ok)
			return status;
	}
	else
	{
		const std::uint64_t offset = recording->layout.framesOffset + ordinal * recording->layout.frameStride;
		if (ReadAt(recording->file, offset, recording->encodedFrame.data(), recording->encodedFrame.size()) == 0)
			return ReplayError(error, "truncated_recording_frame");
	}
	const std::uint8_t* input = recording->encodedFrame.data();
	if (GetU64(input) != ordinal)
		return ReplayError(error, "recording_frame_ordinal_mismatch");
	if (recording->formatVersion == kCompressedReplayVersion)
		for (const VisualInstance& instance : recording->instances)
			if (instance.transformSlot != UINT32_MAX)
				recording->frame.transforms[instance.transformSlot].stableSlot = instance.stableSlot;
	for (VisualStableTransform& transform : recording->frame.transforms)
	{
		if (recording->formatVersion == kFormatVersion)
			transform.stableSlot = GetU32(input);
		transform.transform = GetTransform(input);
		if (ValidTransform(transform.transform) == 0)
			return ReplayError(error, "recording_frame_invalid_transform");
	}
	for (const VisualInstance& instance : recording->instances)
		if (instance.transformSlot != UINT32_MAX &&
		    recording->frame.transforms[instance.transformSlot].stableSlot != instance.stableSlot)
			return ReplayError(error, "recording_frame_stable_slot_mismatch");
	for (VisualDebugPrimitive& value : recording->frame.debug)
	{
		value.kind = GetU32(input);
		value.materialIndex = GetU32(input);
		value.originOrCenterX = GetFloat(input);
		value.originOrCenterY = GetFloat(input);
		value.originOrCenterZ = GetFloat(input);
		value.endOrHalfExtentsX = GetFloat(input);
		value.endOrHalfExtentsY = GetFloat(input);
		value.endOrHalfExtentsZ = GetFloat(input);
		value.radius = GetFloat(input);
		value.reserved = GetU32(input);
		if (ValidDebug(value) == 0)
			return ReplayError(error, "recording_frame_invalid_debug_primitive");
	}
	recording->frame.ordinal = ordinal;
	recording->frame.presence = PresenceStatus_Present;
	return ArenaStatus_Ok;
}

void InitializeReplayClock(const ReplayRecording& recording, ReplayClock* clock)
{
	*clock = {};
	clock->speed = 1;
	clock->framesPerSecond = recording.workKind == WorkKind_QueryBatch ? 30 : 1 / recording.timestep;
	clock->finalOrdinal = recording.layout.frameCount - 1;
}

void SeekReplayClock(ReplayClock* clock, std::uint64_t ordinal, double nowSeconds)
{
	clock->ordinal = std::min(ordinal, clock->finalOrdinal);
	clock->anchorOrdinal = clock->ordinal;
	clock->anchorSeconds = nowSeconds;
	clock->state = ReplayPlayState_Paused;
}

std::uint64_t AdvanceReplayClock(ReplayClock* clock, double nowSeconds)
{
	if (clock->state == ReplayPlayState_Paused || clock->visibility == ReplayVisibility_Minimized)
		return clock->ordinal;
	const double elapsed = std::max(0.0, nowSeconds - clock->anchorSeconds);
	const double position =
	    static_cast<double>(clock->anchorOrdinal) + std::floor(elapsed * clock->speed * clock->framesPerSecond);
	if (position >= static_cast<double>(clock->finalOrdinal))
	{
		if (clock->loop == ReplayLoopMode_Off)
		{
			clock->ordinal = clock->finalOrdinal;
			clock->state = ReplayPlayState_Paused;
		}
		else
			clock->ordinal =
			    static_cast<std::uint64_t>(std::fmod(position, static_cast<double>(clock->finalOrdinal + 1)));
	}
	else
		clock->ordinal = static_cast<std::uint64_t>(position);
	return clock->ordinal;
}

void PlayReplayClock(ReplayClock* clock, ReplayPlayState state, double nowSeconds)
{
	AdvanceReplayClock(clock, nowSeconds);
	clock->anchorOrdinal = clock->ordinal;
	clock->anchorSeconds = nowSeconds;
	clock->state = state;
}

void SetReplaySpeed(ReplayClock* clock, double speed, double nowSeconds)
{
	if (speed != 0.25 && speed != 0.5 && speed != 1 && speed != 2 && speed != 4)
		return;
	PlayReplayClock(clock, clock->state, nowSeconds);
	clock->speed = speed;
}

void SetReplayVisibility(ReplayClock* clock, ReplayVisibility visibility, double nowSeconds)
{
	if (clock->visibility == visibility)
		return;
	PlayReplayClock(clock, clock->state, nowSeconds);
	clock->visibility = visibility;
}
}
