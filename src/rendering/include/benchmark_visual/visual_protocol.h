#pragma once

#include <stdint.h>

namespace benchmark_visual
{
constexpr uint32_t kProtocolVersion = 1u;
constexpr int kVisualBridgeMaxTransforms = 10006;
constexpr int kVisualBridgeMaxStaticBoxes = 64;
constexpr int kVisualBridgeTextCapacity = 64;
constexpr uint32_t kMaxTransforms = 10006u;
constexpr uint32_t kMaxStaticBoxes = 64u;
constexpr uint32_t kMaxFramePayloadBytes = 2u * 1024u * 1024u;
constexpr uint32_t kTextCapacity = 64u;

enum VisualBridgeStatus
{
	VisualBridgeStatus_Ok = 0,
	VisualBridgeStatus_CapacityExceeded = 2,
	VisualBridgeStatus_InvalidArgument = 3,
	VisualBridgeStatus_UnsupportedCase = 4,
	VisualBridgeStatus_ProducerFailed = 5,
	VisualBridgeStatus_ProtocolError = 6,
};

enum VisualRendererStatus
{
	VisualRendererStatus_NotStarted = 0,
	VisualRendererStatus_FrameSubmitted = 1,
	VisualRendererStatus_WindowHidden = 2,
	VisualRendererStatus_RenderSkipped = 3,
};

enum VisualTransportKind : uint32_t
{
	VisualTransportKind_SharedVisualTransport = 1u,
};

struct VisualRunConfig
{
	char caseId[kVisualBridgeTextCapacity];
	char engineId[kVisualBridgeTextCapacity];
	uint32_t threadCount;
	uint32_t repeatIndex;
	uint32_t stepCount;
	uint32_t warmupSteps;
};

struct VisualRunIdentity
{
	char caseId[kVisualBridgeTextCapacity];
	char engineId[kVisualBridgeTextCapacity];
	char fixtureSemantic[kVisualBridgeTextCapacity];
	char fixtureVersion[kVisualBridgeTextCapacity];
	int threadCount;
	int repeatIndex;
	int stepCount;
	int warmupSteps;
	int bodyCount;
	int shapeCount;
	int staticBoxCount;
};

struct VisualIdentity
{
	char caseId[kVisualBridgeTextCapacity];
	char engineId[kVisualBridgeTextCapacity];
	char fixtureSemantic[kVisualBridgeTextCapacity];
	char fixtureVersion[kVisualBridgeTextCapacity];
	uint32_t threadCount;
	uint32_t repeatIndex;
	uint32_t stepCount;
	uint32_t warmupSteps;
	uint32_t bodyCount;
	uint32_t shapeCount;
	uint32_t staticBoxCount;
};

struct VisualTransform
{
	float positionX;
	float positionY;
	float positionZ;
	float rotationX;
	float rotationY;
	float rotationZ;
	float rotationW;
};

struct VisualStaticBox
{
	float positionX;
	float positionY;
	float positionZ;
	float halfExtentX;
	float halfExtentY;
	float halfExtentZ;
};

struct VisualRendererDiagnostics
{
	int rendererStatus;
	int skippedFrameCount;
	double frameElapsedMs;
	double presentWaitMs;
};

struct VisualTiming
{
	double physicsElapsedMs;
	double transportElapsedMs;
	double producerWaitMs;
	double renderElapsedMs;
	double presentWaitMs;
};

struct VisualSnapshot
{
	VisualRunIdentity identity;
	VisualTransform* transforms;
	int transformCapacity;
	int transformCount;
	const VisualStaticBox* staticBoxes;
	int staticBoxCapacity;
	int staticBoxCount;
	float dynamicHalfExtentX;
	float dynamicHalfExtentY;
	float dynamicHalfExtentZ;
	int stepIndex;
	int completedStepCount;
	double physicsElapsedMs;
	VisualTiming timing;
	VisualRendererDiagnostics renderer;
	int status;
};

int ValidateSnapshotCapacity(int transformCount);
int ValidateSnapshot(VisualSnapshot snapshot);
}
