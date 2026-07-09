#include "box3d_visual_snapshot.h"

#include <cstdio>
#include <cstring>

namespace box3d_benchmark
{
void CopyBox3DVisualIdentityText(char* target, const char* source)
{
	std::snprintf(target, benchmark_visual::kVisualBridgeTextCapacity, "%s", source);
}

void FillIdentity(const Box3DCaseDescriptor& descriptor, const Box3DCaseConfig& config, benchmark_visual::VisualIdentity* identity)
{
	CopyBox3DVisualIdentityText(identity->caseId, descriptor.caseId);
	CopyBox3DVisualIdentityText(identity->engineId, kEngineId);
	CopyBox3DVisualIdentityText(identity->fixtureSemantic, descriptor.fixtureSemantic);
	CopyBox3DVisualIdentityText(identity->fixtureVersion, descriptor.fixtureVersion);
	identity->threadCount = static_cast<uint32_t>(config.threadCount);
	identity->repeatIndex = static_cast<uint32_t>(config.repeatIndex);
	identity->stepCount = static_cast<uint32_t>(config.stepCount);
	identity->warmupSteps = static_cast<uint32_t>(config.warmupSteps);
	identity->bodyCount = static_cast<uint32_t>(descriptor.bodyCount);
	identity->shapeCount = static_cast<uint32_t>(descriptor.bodyCount);
	identity->staticBoxCount = static_cast<uint32_t>(descriptor.staticBodyCount);
}

uint32_t BuildBox3DVisualSnapshotPayload(
	const Box3DCaseDescriptor& descriptor,
	const Box3DCaseState& state,
	Box3DTransform* caseTransforms,
	Box3DStaticBox* caseBoxes,
	std::vector<uint8_t>& payload)
{
	descriptor.SampleTransforms(state, caseTransforms, descriptor.dynamicBodyCount);
	descriptor.CopyStaticBoxes(caseBoxes, descriptor.staticBodyCount);
	benchmark_visual::VisualSnapshotPayloadHeader header = {};
	FillIdentity(descriptor, state.config, &header.identity);
	header.timing.physicsElapsedMs = state.physicsElapsedMs;
	header.timing.transportElapsedMs = 0.0;
	header.timing.producerWaitMs = 0.0;
	header.timing.renderElapsedMs = 0.0;
	header.timing.presentWaitMs = 0.0;
	header.transformCount = static_cast<uint32_t>(descriptor.dynamicBodyCount);
	header.staticBoxCount = static_cast<uint32_t>(descriptor.staticBodyCount);
	header.completedStepCount = static_cast<uint32_t>(state.completedStepCount);
	header.status = benchmark_visual::VisualTransportStatus_Ok;
	header.dynamicHalfExtentX = descriptor.dynamicHalfExtent;
	header.dynamicHalfExtentY = descriptor.dynamicHalfExtent;
	header.dynamicHalfExtentZ = descriptor.dynamicHalfExtent;
	uint32_t payloadBytes = static_cast<uint32_t>(sizeof(header)) +
		static_cast<uint32_t>(descriptor.dynamicBodyCount * sizeof(benchmark_visual::VisualTransform)) +
		static_cast<uint32_t>(descriptor.staticBodyCount * sizeof(benchmark_visual::VisualStaticBox));
	uint8_t* cursor = payload.data();
	std::memcpy(cursor, &header, sizeof(header));
	cursor += sizeof(header);
	for (int index = 0; index < descriptor.dynamicBodyCount; ++index)
	{
		benchmark_visual::VisualTransform transform =
		{
			caseTransforms[index].positionX,
			caseTransforms[index].positionY,
			caseTransforms[index].positionZ,
			caseTransforms[index].rotationX,
			caseTransforms[index].rotationY,
			caseTransforms[index].rotationZ,
			caseTransforms[index].rotationW,
		};
		std::memcpy(cursor, &transform, sizeof(transform));
		cursor += sizeof(transform);
	}
	for (int index = 0; index < descriptor.staticBodyCount; ++index)
	{
		benchmark_visual::VisualStaticBox staticBox =
		{
			caseBoxes[index].positionX,
			caseBoxes[index].positionY,
			caseBoxes[index].positionZ,
			caseBoxes[index].halfExtentX,
			caseBoxes[index].halfExtentY,
			caseBoxes[index].halfExtentZ,
		};
		std::memcpy(cursor, &staticBox, sizeof(staticBox));
		cursor += sizeof(staticBox);
	}
	return payloadBytes;
}
}
