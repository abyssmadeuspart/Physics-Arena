#include "box3d_box_container_pile_10k_case.h"

#include <chrono>
#include <cstdio>
#include <cstring>

namespace box3d_benchmark
{
constexpr Box3DStaticBox kStaticBoxes[kStaticBodyCount] =
{
	{ 0.0f, -0.5f, 0.0f, 14.5f, 0.5f, 14.5f },
	{ -14.5f, 11.75f, 0.0f, 0.5f, 12.25f, 14.5f },
	{ 14.5f, 11.75f, 0.0f, 0.5f, 12.25f, 14.5f },
	{ 0.0f, 11.75f, -14.5f, 14.5f, 12.25f, 0.5f },
	{ 0.0f, 11.75f, 14.5f, 14.5f, 12.25f, 0.5f },
};


int RequestedWorkerCount(int threadCount)
{
	return threadCount > 1 ? threadCount - 1 : 0;
}

b3BodyId AddStaticBox(b3WorldId worldId, b3ShapeDef* shapeDef, b3Pos position, float hx, float hy, float hz)
{
	b3BodyDef bodyDef = b3DefaultBodyDef();
	bodyDef.position = position;
	b3BodyId body = b3CreateBody(worldId, &bodyDef);
	b3BoxHull hull = b3MakeBoxHull(hx, hy, hz);
	b3CreateHullShape(body, shapeDef, &hull.base);
	return body;
}

int CreateFixture(b3WorldId worldId, b3BodyId* bodies)
{
	b3ShapeDef shapeDef = b3DefaultShapeDef();
	shapeDef.baseMaterial.friction = 0.5f;
	shapeDef.baseMaterial.restitution = 0.0f;
	shapeDef.density = 1.0f;

	AddStaticBox(worldId, &shapeDef, {0.0, -0.5, 0.0}, 14.5f, 0.5f, 14.5f);
	AddStaticBox(worldId, &shapeDef, {-14.5, 11.75, 0.0}, 0.5f, 12.25f, 14.5f);
	AddStaticBox(worldId, &shapeDef, {14.5, 11.75, 0.0}, 0.5f, 12.25f, 14.5f);
	AddStaticBox(worldId, &shapeDef, {0.0, 11.75, -14.5}, 14.5f, 12.25f, 0.5f);
	AddStaticBox(worldId, &shapeDef, {0.0, 11.75, 14.5}, 14.5f, 12.25f, 0.5f);

	b3BodyDef bodyDef = b3DefaultBodyDef();
	bodyDef.type = b3_dynamicBody;
	bodyDef.enableSleep = false;
	b3BoxHull boxHull = b3MakeBoxHull(kHalfExtent, kHalfExtent, kHalfExtent);
	float originX = -0.5f * static_cast<float>(kPileXCount - 1) * kSpacing;
	float originZ = -0.5f * static_cast<float>(kPileZCount - 1) * kSpacing;
	int bodyIndex = 0;

	for (int y = 0; y < kPileYCount; ++y)
	{
		for (int z = 0; z < kPileZCount; ++z)
		{
			for (int x = 0; x < kPileXCount; ++x)
			{
				bodyDef.position = {
					originX + static_cast<float>(x) * kSpacing,
					kInitialY + static_cast<float>(y) * kSpacing,
					originZ + static_cast<float>(z) * kSpacing,
				};
				b3BodyId bodyId = b3CreateBody(worldId, &bodyDef);
				b3CreateHullShape(bodyId, &shapeDef, &boxHull.base);
				bodies[bodyIndex++] = bodyId;
			}
		}
	}

	return bodyIndex == kDynamicBodyCount ? 0 : 2;
}

b3WorldId CreateBenchmarkWorld(const Box3DCaseConfig& config)
{
	b3WorldDef worldDef = b3DefaultWorldDef();
	worldDef.enableContinuous = false;
	worldDef.workerCount = RequestedWorkerCount(config.threadCount);
	worldDef.capacity.staticShapeCount = kStaticBodyCount;
	worldDef.capacity.dynamicShapeCount = kDynamicBodyCount;
	worldDef.capacity.staticBodyCount = kStaticBodyCount;
	worldDef.capacity.dynamicBodyCount = kDynamicBodyCount;
	worldDef.capacity.contactCount = kDynamicBodyCount * 16;
	return b3CreateWorld(&worldDef);
}

double StepWorldTimed(b3WorldId worldId)
{
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	b3World_Step(worldId, kTimestep, 4);
	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	return std::chrono::duration<double, std::milli>(end - start).count();
}

int CreateBox3DCaseState(const Box3DCaseConfig& config, Box3DCaseState* state)
{
	if (state == nullptr)
	{
		return 2;
	}
	state->config = config;
	state->physicsElapsedMs = 0.0;
	state->completedStepCount = 0;
	for (int index = 0; index < kDynamicBodyCount; ++index)
	{
		state->dynamicBodies[index] = {};
	}
	state->worldId = CreateBenchmarkWorld(config);
	if (CreateFixture(state->worldId, state->dynamicBodies) != 0)
	{
		b3DestroyWorld(state->worldId);
		state->worldId = {};
		return 2;
	}
	return 0;
}

int RunBox3DCaseWarmup(const Box3DCaseConfig& config)
{
	if (config.warmupSteps <= 0)
	{
		return 0;
	}
	Box3DCaseState warmupState = {};
	int createStatus = CreateBox3DCaseState(config, &warmupState);
	if (createStatus != 0)
	{
		return createStatus;
	}
	for (int step = 0; step < config.warmupSteps; ++step)
	{
		b3World_Step(warmupState.worldId, kTimestep, 4);
	}
	DestroyBox3DCaseState(&warmupState);
	return 0;
}

int StepBox3DCase(Box3DCaseState* state, int stepCount)
{
	if (state == nullptr || stepCount < 0)
	{
		return 2;
	}
	for (int step = 0; step < stepCount; ++step)
	{
		state->physicsElapsedMs += StepWorldTimed(state->worldId);
		state->completedStepCount += 1;
	}
	return 0;
}

void DestroyBox3DCaseState(Box3DCaseState* state)
{
	if (state == nullptr)
	{
		return;
	}
	b3DestroyWorld(state->worldId);
	state->worldId = {};
}

void SampleBox3DTransforms(const Box3DCaseState& state, Box3DTransform* transforms, int transformCapacity)
{
	if (transforms == nullptr || transformCapacity < kDynamicBodyCount)
	{
		return;
	}
	for (int index = 0; index < kDynamicBodyCount; ++index)
	{
		b3Pos position = b3Body_GetPosition(state.dynamicBodies[index]);
		b3Quat rotation = b3Body_GetRotation(state.dynamicBodies[index]);
		transforms[index] = {
			position.x,
			position.y,
			position.z,
			rotation.v.x,
			rotation.v.y,
			rotation.v.z,
			rotation.s,
		};
	}
}

void CopyBox3DStaticBoxes(Box3DStaticBox* boxes, int boxCapacity)
{
	if (boxes == nullptr || boxCapacity < kStaticBodyCount)
	{
		return;
	}
	for (int index = 0; index < kStaticBodyCount; ++index)
	{
		boxes[index] = kStaticBoxes[index];
	}
}

}
