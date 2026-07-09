#pragma once

#include "benchmark_visual/visual_protocol.h"

namespace benchmark_visual
{
constexpr uint32_t kVisualTransportMagic = 0x42565354u;
constexpr uint16_t kVisualTransportVersion = 1u;
constexpr uint32_t kVisualTransportHeaderSize = 20u;

enum VisualFrameType : uint16_t
{
	VisualFrameType_Hello = 1u,
	VisualFrameType_Start = 2u,
	VisualFrameType_Warmup = 3u,
	VisualFrameType_Step = 4u,
	VisualFrameType_Shutdown = 5u,
	VisualFrameType_HelloAck = 6u,
	VisualFrameType_Started = 7u,
	VisualFrameType_Snapshot = 8u,
	VisualFrameType_Completed = 9u,
	VisualFrameType_Error = 10u,
};

enum VisualTransportStatus : uint32_t
{
	VisualTransportStatus_Ok = 0u,
	VisualTransportStatus_InvalidFrame = 1u,
	VisualTransportStatus_InvalidToken = 2u,
	VisualTransportStatus_InvalidIdentity = 3u,
	VisualTransportStatus_UnsupportedCase = 4u,
	VisualTransportStatus_ProducerFailed = 5u,
	VisualTransportStatus_CapacityExceeded = 6u,
};

struct VisualFrameHeader
{
	uint32_t magic;
	uint16_t version;
	uint16_t frameType;
	uint32_t sequence;
	uint32_t payloadBytes;
	uint32_t headerCrc;
};

struct VisualHelloPayload
{
	char runToken[kTextCapacity];
	char engineId[kTextCapacity];
	char caseId[kTextCapacity];
};

struct VisualHelloAckPayload
{
	char engineId[kTextCapacity];
	char caseId[kTextCapacity];
	char producerBuildId[kTextCapacity];
	uint32_t processId;
	uint32_t supportedFrameFlags;
	uint32_t runTokenHash;
	uint32_t status;
};

struct VisualStartPayload
{
	VisualRunConfig config;
	uint32_t fixtureSemanticHash;
	uint32_t fixtureVersionHash;
	uint32_t outputDetailMask;
};

struct VisualStepPayload
{
	uint32_t commandStepCount;
};

struct VisualCompletedPayload
{
	uint32_t status;
	uint32_t completedStepCount;
	double physicsElapsedMs;
};

struct VisualErrorPayload
{
	uint32_t status;
	char diagnostic[kTextCapacity];
};

struct VisualSnapshotPayloadHeader
{
	VisualIdentity identity;
	VisualTiming timing;
	uint32_t transformCount;
	uint32_t staticBoxCount;
	uint32_t completedStepCount;
	uint32_t status;
	uint32_t diagnosticCode;
	float dynamicHalfExtentX;
	float dynamicHalfExtentY;
	float dynamicHalfExtentZ;
};

inline uint32_t VisualTransportHashText(const char* text)
{
	uint32_t hash = 2166136261u;
	for (uint32_t index = 0u; text[index] != '\0'; ++index)
	{
		hash ^= static_cast<uint8_t>(text[index]);
		hash *= 16777619u;
	}
	return hash;
}

inline uint32_t VisualTransportHeaderCrc(VisualFrameHeader header)
{
	header.headerCrc = 0u;
	return header.magic ^ (static_cast<uint32_t>(header.version) << 16u) ^
		static_cast<uint32_t>(header.frameType) ^ header.sequence ^ header.payloadBytes ^ 0x9e3779b9u;
}
}
