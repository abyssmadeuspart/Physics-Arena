#pragma once

#include "box3d/box3d.h"

namespace box3d_benchmark
{
constexpr int kPileXCount = 25;
constexpr int kPileYCount = 16;
constexpr int kPileZCount = 25;
constexpr int kDynamicBodyCount = kPileXCount * kPileYCount * kPileZCount;
constexpr int kStaticBodyCount = 5;
constexpr int kBodyCount = kDynamicBodyCount + kStaticBodyCount;
constexpr float kHalfExtent = 0.5f;
constexpr float kSpacing = 1.02f;
constexpr float kInitialY = 24.51f;
constexpr float kTimestep = 1.0f / 60.0f;
constexpr float kOpenContainerLateralEscape = 15.5f;
constexpr float kOpenContainerMaxY = 96.0f;
constexpr const char* kCaseId = "box_container_pile_10k";
constexpr const char* kEngineId = "box3d";
constexpr const char* kEngineRef = "8441b4a06d6d09dcfb0b0f704df4d847d1437b92";
constexpr const char* kFixtureSemantic = "open_container_falling_pile";
constexpr const char* kFixtureVersion = "box3d_open_container_v1";

struct Box3DTransform
{
	float positionX;
	float positionY;
	float positionZ;
	float rotationX;
	float rotationY;
	float rotationZ;
	float rotationW;
};

struct Box3DStaticBox
{
	float positionX;
	float positionY;
	float positionZ;
	float halfExtentX;
	float halfExtentY;
	float halfExtentZ;
};

struct Box3DCaseConfig
{
	int threadCount;
	int repeatIndex;
	int stepCount;
	int warmupSteps;
};

struct Box3DCaseState
{
	Box3DCaseConfig config;
	b3WorldId worldId;
	b3BodyId dynamicBodies[kDynamicBodyCount];
	double physicsElapsedMs;
	int completedStepCount;
};

int RequestedWorkerCount(int threadCount);
int CreateBox3DCaseState(const Box3DCaseConfig& config, Box3DCaseState* state);
int RunBox3DCaseWarmup(const Box3DCaseConfig& config);
int StepBox3DCase(Box3DCaseState* state, int stepCount);
void DestroyBox3DCaseState(Box3DCaseState* state);
void SampleBox3DTransforms(const Box3DCaseState& state, Box3DTransform* transforms, int transformCapacity);
void CopyBox3DStaticBoxes(Box3DStaticBox* boxes, int boxCapacity);
}
