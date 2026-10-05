#pragma once

#include "benchmark_visual/visual_snapshot.h"
#include "case_execution_wire.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>

namespace benchmark_replay
{
using benchmark_visual::VisualScene;
using benchmark_visual::VisualStableTransform;
using benchmark_visual::VisualDebugPrimitive;

constexpr std::uint32_t kFormatVersion = 1;
constexpr std::uint32_t kHeaderBytes = 384;
constexpr std::uint32_t kCameraBytes = 120;
constexpr std::uint32_t kTrailerBytes = 24;
constexpr std::uint32_t kFileBufferBytes = 65536;

enum RecordingMode
{
	RecordingMode_Off = 0,
	RecordingMode_On = 1,
};

enum WorkKind : std::uint32_t
{
	WorkKind_Dynamics = 1,
	WorkKind_QueryBatch = 2,
};

enum RecordingStatus
{
	RecordingStatus_Ok = 0,
	RecordingStatus_Invalid = 1,
	RecordingStatus_IoFailed = 2,
	RecordingStatus_Incomplete = 3,
	RecordingStatus_UnsupportedVersion = 4,
};

enum WriterState
{
	WriterState_Closed = 0,
	WriterState_Open = 1,
	WriterState_Failed = 2,
	WriterState_Published = 3,
};

struct RecordingLayout
{
	std::uint64_t sceneBytes;
	std::uint64_t framesOffset;
	std::uint64_t frameStride;
	std::uint64_t frameCount;
	std::uint64_t trailerOffset;
	std::uint64_t fileBytes;
};

struct RecordingWriter
{
	std::FILE* file;
	std::filesystem::path finalPath;
	std::filesystem::path partialPath;
	std::vector<std::uint8_t> frameBuffer;
	std::array<char, kFileBufferBytes> fileBuffer;
	RecordingLayout layout;
	std::uint64_t writtenFrames;
	std::uint32_t transformCount;
	std::uint32_t debugCount;
	WriterState state;
};

inline void SetRecordingSceneIdentity(const CaseExecutionSpec& execution, const char* engineId, int threadCount,
                                      int repeatIndex, VisualScene* scene)
{
	std::snprintf(scene->identity.caseId, 64, "%s", execution.caseId);
	std::snprintf(scene->identity.engineId, 64, "%s", engineId);
	std::snprintf(scene->identity.fixtureSemantic, 64, "%s", execution.fixtureSemantic);
	std::snprintf(scene->identity.fixtureVersion, 64, "%u", execution.fixtureRevision);
	scene->identity.threadCount = threadCount;
	scene->identity.repeatIndex = repeatIndex;
	scene->identity.stepCount = static_cast<int>(execution.measuredWorkUnitCount);
	scene->identity.warmupSteps = static_cast<int>(execution.warmupWorkUnitCount);
	scene->identity.bodyCount = static_cast<int>(execution.bodyCount);
	scene->identity.shapeCount = static_cast<int>(execution.shapeCount);
	scene->dynamicTransformCount = execution.dynamicBodyCount;
	scene->debugPrimitiveCount = execution.visualDebugPrimitiveCount;
	const CaseExecutionCamera& source = execution.replayCamera;
	benchmark_visual::VisualCameraPolicy& camera = scene->camera;
	camera.direction = {source.direction.x, source.direction.y, source.direction.z};
	camera.up = {source.up.x, source.up.y, source.up.z};
	camera.minimum = {source.minimum.x, source.minimum.y, source.minimum.z};
	camera.maximum = {source.maximum.x, source.maximum.y, source.maximum.z};
	camera.eye = {source.eye.x, source.eye.y, source.eye.z};
	camera.target = {source.target.x, source.target.y, source.target.z};
	camera.eyeOffset = {source.eyeOffset.x, source.eyeOffset.y, source.eyeOffset.z};
	camera.targetOffset = {source.targetOffset.x, source.targetOffset.y, source.targetOffset.z};
	camera.verticalFovDegrees = source.verticalFovDegrees;
	camera.viewportFill = source.viewportFill;
	camera.nearPlane = source.nearPlane;
	camera.farPlane = source.farPlane;
	camera.stableSlot = source.stableSlot;
	camera.mode = static_cast<benchmark_visual::VisualCameraMode>(source.mode);
}

inline void PutU32(std::uint8_t*& output, std::uint32_t value)
{
	for (unsigned shift = 0; shift < 32; shift += 8)
		*output++ = static_cast<std::uint8_t>(value >> shift);
}

inline void PutU64(std::uint8_t*& output, std::uint64_t value)
{
	for (unsigned shift = 0; shift < 64; shift += 8)
		*output++ = static_cast<std::uint8_t>(value >> shift);
}

inline void PutFloat(std::uint8_t*& output, float value)
{
	std::uint32_t bits = 0;
	std::memcpy(&bits, &value, 4);
	PutU32(output, bits);
}

inline void PutDouble(std::uint8_t*& output, double value)
{
	std::uint64_t bits = 0;
	std::memcpy(&bits, &value, 8);
	PutU64(output, bits);
}

inline std::uint32_t GetU32(const std::uint8_t*& input)
{
	std::uint32_t value = 0;
	for (unsigned shift = 0; shift < 32; shift += 8)
		value |= std::uint32_t{*input++} << shift;
	return value;
}

inline std::uint64_t GetU64(const std::uint8_t*& input)
{
	std::uint64_t value = 0;
	for (unsigned shift = 0; shift < 64; shift += 8)
		value |= std::uint64_t{*input++} << shift;
	return value;
}

inline float GetFloat(const std::uint8_t*& input)
{
	const std::uint32_t bits = GetU32(input);
	float value = 0;
	std::memcpy(&value, &bits, 4);
	return value;
}

inline double GetDouble(const std::uint8_t*& input)
{
	const std::uint64_t bits = GetU64(input);
	double value = 0;
	std::memcpy(&value, &bits, 8);
	return value;
}

inline void PutTransform(std::uint8_t*& output, const benchmark_visual::VisualTransform& value)
{
	PutFloat(output, value.positionX);
	PutFloat(output, value.positionY);
	PutFloat(output, value.positionZ);
	PutFloat(output, value.rotationX);
	PutFloat(output, value.rotationY);
	PutFloat(output, value.rotationZ);
	PutFloat(output, value.rotationW);
}

inline benchmark_visual::VisualTransform GetTransform(const std::uint8_t*& input)
{
	benchmark_visual::VisualTransform value = {};
	value.positionX = GetFloat(input);
	value.positionY = GetFloat(input);
	value.positionZ = GetFloat(input);
	value.rotationX = GetFloat(input);
	value.rotationY = GetFloat(input);
	value.rotationZ = GetFloat(input);
	value.rotationW = GetFloat(input);
	return value;
}

inline void PutVector(std::uint8_t*& output, const benchmark_visual::VisualCameraVector& value)
{
	PutFloat(output, value.x);
	PutFloat(output, value.y);
	PutFloat(output, value.z);
}

inline benchmark_visual::VisualCameraVector GetVector(const std::uint8_t*& input)
{
	benchmark_visual::VisualCameraVector value = {};
	value.x = GetFloat(input);
	value.y = GetFloat(input);
	value.z = GetFloat(input);
	return value;
}

inline RecordingStatus CalculateLayout(const VisualScene& scene, WorkKind kind, double timestep,
                                       RecordingLayout* layout)
{
	using namespace benchmark_visual;
	if (layout == nullptr || scene.identity.stepCount <= 0 || scene.identity.stepCount > 100000 ||
	    scene.identity.threadCount <= 0 || scene.identity.repeatIndex < 0 || scene.identity.warmupSteps < 0 ||
	    scene.identity.bodyCount <= 0 || scene.identity.shapeCount <= 0 || scene.geometryCount == 0 ||
	    scene.geometryCount > kMaxSceneGeometries || scene.instanceCount == 0 ||
	    scene.instanceCount > kMaxSceneInstances || scene.dynamicTransformCount > kMaxTransforms ||
	    scene.dynamicTransformCount > scene.instanceCount || scene.debugPrimitiveCount > kMaxDebugPrimitives ||
	    scene.vertexCount > kMaxSceneVertices || scene.indexCount > kMaxSceneIndices ||
	    scene.edgeCount > kMaxSceneEdges || (kind != WorkKind_Dynamics && kind != WorkKind_QueryBatch) ||
	    (kind == WorkKind_Dynamics ? !std::isfinite(timestep) || timestep <= 0 : timestep != 0))
		return RecordingStatus_Invalid;
	const char* texts[] = {scene.identity.caseId, scene.identity.engineId, scene.identity.fixtureSemantic,
	                       scene.identity.fixtureVersion};
	for (const char* text : texts)
		if (text[0] == 0 || std::memchr(text, 0, kVisualBridgeTextCapacity) == nullptr)
			return RecordingStatus_Invalid;
	layout->sceneBytes = kCameraBytes + std::uint64_t{scene.geometryCount} * 40 +
	                     std::uint64_t{scene.instanceCount} * 44 + std::uint64_t{scene.vertexCount} * 12 +
	                     std::uint64_t{scene.indexCount} * 4 + std::uint64_t{scene.edgeCount} * 8;
	layout->framesOffset = kHeaderBytes + layout->sceneBytes;
	layout->frameStride =
	    8 + std::uint64_t{scene.dynamicTransformCount} * 32 + std::uint64_t{scene.debugPrimitiveCount} * 40;
	layout->frameCount = std::uint64_t(scene.identity.stepCount) + 1;
	if (layout->frameCount >
	    (std::numeric_limits<std::uint64_t>::max() - layout->framesOffset - kTrailerBytes) / layout->frameStride)
		return RecordingStatus_Invalid;
	layout->trailerOffset = layout->framesOffset + layout->frameCount * layout->frameStride;
	layout->fileBytes = layout->trailerOffset + kTrailerBytes;
	return RecordingStatus_Ok;
}

inline void EncodeHeader(const VisualScene& scene, WorkKind kind, double timestep, const RecordingLayout& layout,
                         std::uint8_t* output)
{
	std::memcpy(output, "BPREPLAY", 8);
	output += 8;
	PutU32(output, kFormatVersion);
	PutU32(output, kHeaderBytes);
	const char* texts[] = {scene.identity.caseId, scene.identity.engineId, scene.identity.fixtureSemantic,
	                       scene.identity.fixtureVersion};
	for (const char* text : texts)
	{
		std::memset(output, 0, 64);
		std::memcpy(output, text, std::strlen(text));
		output += 64;
	}
	PutU32(output, static_cast<std::uint32_t>(scene.identity.threadCount));
	PutU32(output, static_cast<std::uint32_t>(scene.identity.repeatIndex));
	PutU32(output, static_cast<std::uint32_t>(scene.identity.stepCount));
	PutU32(output, static_cast<std::uint32_t>(scene.identity.warmupSteps));
	PutU32(output, static_cast<std::uint32_t>(scene.identity.bodyCount));
	PutU32(output, static_cast<std::uint32_t>(scene.identity.shapeCount));
	PutU32(output, kind);
	PutDouble(output, timestep);
	PutU32(output, scene.geometryCount);
	PutU32(output, scene.instanceCount);
	PutU32(output, scene.dynamicTransformCount);
	PutU32(output, scene.debugPrimitiveCount);
	PutU32(output, scene.vertexCount);
	PutU32(output, scene.indexCount);
	PutU32(output, scene.edgeCount);
	PutU64(output, kHeaderBytes);
	PutU64(output, layout.sceneBytes);
	PutU64(output, layout.framesOffset);
	PutU64(output, layout.frameStride);
	PutU64(output, layout.frameCount);
	PutU64(output, layout.trailerOffset);
}

inline void EncodeScene(const VisualScene& scene, std::uint8_t* output)
{
	const benchmark_visual::VisualCameraPolicy& camera = scene.camera;
	PutVector(output, camera.direction);
	PutVector(output, camera.up);
	PutVector(output, camera.minimum);
	PutVector(output, camera.maximum);
	PutVector(output, camera.eye);
	PutVector(output, camera.target);
	PutVector(output, camera.eyeOffset);
	PutVector(output, camera.targetOffset);
	PutFloat(output, camera.verticalFovDegrees);
	PutFloat(output, camera.viewportFill);
	PutFloat(output, camera.nearPlane);
	PutFloat(output, camera.farPlane);
	PutU32(output, camera.stableSlot);
	PutU32(output, camera.mode);
	for (std::uint32_t index = 0; index < scene.geometryCount; ++index)
	{
		const benchmark_visual::VisualGeometry& geometry = scene.geometries[index];
		PutU32(output, geometry.kind);
		PutFloat(output, geometry.parameterX);
		PutFloat(output, geometry.parameterY);
		PutFloat(output, geometry.parameterZ);
		PutU32(output, geometry.vertexOffset);
		PutU32(output, geometry.vertexCount);
		PutU32(output, geometry.indexOffset);
		PutU32(output, geometry.indexCount);
		PutU32(output, geometry.edgeOffset);
		PutU32(output, geometry.edgeCount);
	}
	for (std::uint32_t index = 0; index < scene.instanceCount; ++index)
	{
		const benchmark_visual::VisualInstance& instance = scene.instances[index];
		PutU32(output, instance.geometryIndex);
		PutU32(output, instance.stableSlot);
		PutU32(output, instance.materialIndex);
		PutU32(output, instance.transformSlot);
		PutTransform(output, instance.initialTransform);
	}
	for (std::uint32_t index = 0; index < scene.vertexCount; ++index)
	{
		PutFloat(output, scene.vertices[index].x);
		PutFloat(output, scene.vertices[index].y);
		PutFloat(output, scene.vertices[index].z);
	}
	for (std::uint32_t index = 0; index < scene.indexCount; ++index)
		PutU32(output, scene.indices[index]);
	for (std::uint32_t index = 0; index < scene.edgeCount; ++index)
	{
		PutU32(output, scene.edges[index].a);
		PutU32(output, scene.edges[index].b);
	}
}

inline RecordingStatus AbortRecording(RecordingWriter* writer)
{
	if (writer == nullptr)
		return RecordingStatus_Invalid;
	const int closeStatus = writer->file == nullptr ? 0 : std::fclose(writer->file);
	writer->file = nullptr;
	if (writer->state != WriterState_Published)
		writer->state = WriterState_Failed;
	return closeStatus == 0 ? RecordingStatus_Incomplete : RecordingStatus_IoFailed;
}

inline RecordingStatus BeginRecording(const std::filesystem::path& finalPath, const VisualScene& scene, WorkKind kind,
                                      double timestep, RecordingWriter* writer)
{
	if (writer == nullptr || writer->file != nullptr || finalPath.extension() != ".bpr" ||
	    CalculateLayout(scene, kind, timestep, &writer->layout) != RecordingStatus_Ok || scene.geometries == nullptr ||
	    scene.instances == nullptr || (scene.vertexCount != 0 && scene.vertices == nullptr) ||
	    (scene.indexCount != 0 && scene.indices == nullptr) || (scene.edgeCount != 0 && scene.edges == nullptr))
		return RecordingStatus_Invalid;
	writer->finalPath = finalPath;
	writer->partialPath = finalPath;
	writer->partialPath += ".partial";
	std::error_code error;
	if (std::filesystem::exists(finalPath, error) || error || std::filesystem::exists(writer->partialPath, error) ||
	    error)
		return RecordingStatus_IoFailed;
#ifdef _WIN32
	if (_wfopen_s(&writer->file, writer->partialPath.c_str(), L"wbx") != 0)
		writer->file = nullptr;
#else
	writer->file = std::fopen(writer->partialPath.c_str(), "wbx");
#endif
	if (writer->file == nullptr)
		return RecordingStatus_IoFailed;
	writer->state = WriterState_Open;
	writer->writtenFrames = 0;
	writer->transformCount = scene.dynamicTransformCount;
	writer->debugCount = scene.debugPrimitiveCount;
	writer->frameBuffer.resize(static_cast<std::size_t>(writer->layout.frameStride));
	if (std::setvbuf(writer->file, writer->fileBuffer.data(), _IOFBF, writer->fileBuffer.size()) != 0)
	{
		AbortRecording(writer);
		return RecordingStatus_IoFailed;
	}
	std::vector<std::uint8_t> sceneBytes(static_cast<std::size_t>(writer->layout.framesOffset));
	EncodeHeader(scene, kind, timestep, writer->layout, sceneBytes.data());
	EncodeScene(scene, sceneBytes.data() + kHeaderBytes);
	if (std::fwrite(sceneBytes.data(), 1, sceneBytes.size(), writer->file) != sceneBytes.size())
	{
		AbortRecording(writer);
		return RecordingStatus_IoFailed;
	}
	return RecordingStatus_Ok;
}

inline RecordingStatus AppendFrame(RecordingWriter* writer, std::uint64_t ordinal,
                                   const VisualStableTransform* transforms, std::uint32_t transformCount,
                                   const VisualDebugPrimitive* debug, std::uint32_t debugCount)
{
	if (writer == nullptr)
		return RecordingStatus_Invalid;
	if (writer->state != WriterState_Open || ordinal != writer->writtenFrames || ordinal >= writer->layout.frameCount ||
	    transformCount != writer->transformCount || debugCount != writer->debugCount ||
	    (transformCount != 0 && transforms == nullptr) || (debugCount != 0 && debug == nullptr))
	{
		AbortRecording(writer);
		return RecordingStatus_Invalid;
	}
	std::uint8_t* output = writer->frameBuffer.data();
	PutU64(output, ordinal);
	for (std::uint32_t index = 0; index < transformCount; ++index)
	{
		PutU32(output, transforms[index].stableSlot);
		PutTransform(output, transforms[index].transform);
	}
	for (std::uint32_t index = 0; index < debugCount; ++index)
	{
		const VisualDebugPrimitive& value = debug[index];
		PutU32(output, value.kind);
		PutU32(output, value.materialIndex);
		PutFloat(output, value.originOrCenterX);
		PutFloat(output, value.originOrCenterY);
		PutFloat(output, value.originOrCenterZ);
		PutFloat(output, value.endOrHalfExtentsX);
		PutFloat(output, value.endOrHalfExtentsY);
		PutFloat(output, value.endOrHalfExtentsZ);
		PutFloat(output, value.radius);
		PutU32(output, value.reserved);
	}
	if (std::fwrite(writer->frameBuffer.data(), 1, writer->frameBuffer.size(), writer->file) !=
	    writer->frameBuffer.size())
	{
		AbortRecording(writer);
		return RecordingStatus_IoFailed;
	}
	++writer->writtenFrames;
	return RecordingStatus_Ok;
}

inline RecordingStatus CompleteRecording(RecordingWriter* writer)
{
	if (writer == nullptr)
		return RecordingStatus_Invalid;
	if (writer->state != WriterState_Open || writer->writtenFrames != writer->layout.frameCount)
	{
		AbortRecording(writer);
		return RecordingStatus_Incomplete;
	}
	std::array<std::uint8_t, kTrailerBytes> trailer = {};
	std::memcpy(trailer.data(), "BPRDONE1", 8);
	std::uint8_t* output = trailer.data() + 8;
	PutU64(output, writer->writtenFrames);
	PutU64(output, writer->layout.fileBytes);
	const std::size_t written = std::fwrite(trailer.data(), 1, trailer.size(), writer->file);
	const int flushStatus = std::fflush(writer->file);
	const int closeStatus = std::fclose(writer->file);
	writer->file = nullptr;
	writer->state = WriterState_Failed;
	if (written != trailer.size() || flushStatus != 0 || closeStatus != 0)
		return RecordingStatus_IoFailed;
	std::error_code error;
	if (std::filesystem::exists(writer->finalPath, error) || error)
		return RecordingStatus_IoFailed;
	std::filesystem::rename(writer->partialPath, writer->finalPath, error);
	if (error)
		return RecordingStatus_IoFailed;
	writer->state = WriterState_Published;
	return RecordingStatus_Ok;
}
}
