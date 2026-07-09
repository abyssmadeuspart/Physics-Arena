#include "physx34_box_container_pile_10k_case.h"

#include <chrono>
#include <cstdio>
#include <cstring>

namespace physx34_benchmark
{
int RequestedWorkerCount(int threadCount)
{
	return threadCount;
}

void ReleaseContext(PhysXContext* context)
{
	if (context->scene != nullptr)
	{
		context->scene->release();
		context->scene = nullptr;
	}
	if (context->material != nullptr)
	{
		context->material->release();
		context->material = nullptr;
	}
	if (context->dispatcher != nullptr)
	{
		context->dispatcher->release();
		context->dispatcher = nullptr;
	}
	if (context->physics != nullptr)
	{
		context->physics->release();
		context->physics = nullptr;
	}
	if (context->foundation != nullptr)
	{
		context->foundation->release();
		context->foundation = nullptr;
	}
}

int InitializeContext(const PhysXCaseConfig& config, PhysXContext* context)
{
	context->foundation = PxCreateFoundation(PX_FOUNDATION_VERSION, context->allocator, context->errorCallback);
	if (context->foundation == nullptr)
	{
		std::fprintf(stderr, "run_failed reason=create_foundation\n");
		return 2;
	}

	physx::PxTolerancesScale scale;
	context->physics = PxCreatePhysics(PX_PHYSICS_VERSION, *context->foundation, scale, false, nullptr);
	if (context->physics == nullptr)
	{
		std::fprintf(stderr, "run_failed reason=create_physics\n");
		return 2;
	}

	const int workerCount = RequestedWorkerCount(config.threadCount);
	context->dispatcher = physx::PxDefaultCpuDispatcherCreate(static_cast<physx::PxU32>(workerCount));
	if (context->dispatcher == nullptr)
	{
		std::fprintf(stderr, "run_failed reason=create_dispatcher workers=%d\n", workerCount);
		return 2;
	}

	physx::PxSceneDesc sceneDesc(context->physics->getTolerancesScale());
	sceneDesc.gravity = physx::PxVec3(0.0f, -10.0f, 0.0f);
	sceneDesc.cpuDispatcher = context->dispatcher;
	sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;
	context->scene = context->physics->createScene(sceneDesc);
	if (context->scene == nullptr)
	{
		std::fprintf(stderr, "run_failed reason=create_scene\n");
		return 2;
	}

	context->material = context->physics->createMaterial(0.5f, 0.5f, 0.0f);
	if (context->material == nullptr)
	{
		std::fprintf(stderr, "run_failed reason=create_material\n");
		return 2;
	}
	return 0;
}

int AddStaticBox(PhysXContext* context, physx::PxVec3 position, float hx, float hy, float hz)
{
	physx::PxRigidStatic* actor = context->physics->createRigidStatic(physx::PxTransform(position));
	if (actor == nullptr)
	{
		std::fprintf(stderr, "run_failed reason=create_static\n");
		return 2;
	}
	physx::PxShape* shape = actor->createShape(physx::PxBoxGeometry(hx, hy, hz), *context->material);
	if (shape == nullptr)
	{
		actor->release();
		std::fprintf(stderr, "run_failed reason=create_static_shape\n");
		return 2;
	}
	context->scene->addActor(*actor);
	shape->release();
	return 0;
}

int AddDynamicBox(PhysXContext* context, physx::PxRigidDynamic** bodies, int bodyIndex, physx::PxVec3 position)
{
	physx::PxRigidDynamic* actor = context->physics->createRigidDynamic(physx::PxTransform(position));
	if (actor == nullptr)
	{
		std::fprintf(stderr, "run_failed reason=create_dynamic\n");
		return 2;
	}
	physx::PxShape* shape = actor->createShape(physx::PxBoxGeometry(kHalfExtent, kHalfExtent, kHalfExtent), *context->material);
	if (shape == nullptr)
	{
		actor->release();
		std::fprintf(stderr, "run_failed reason=create_dynamic_shape\n");
		return 2;
	}
	if (physx::PxRigidBodyExt::updateMassAndInertia(*actor, 1.0f) == false)
	{
		shape->release();
		actor->release();
		std::fprintf(stderr, "run_failed reason=mass_inertia\n");
		return 2;
	}
	actor->setSolverIterationCounts(4, 1);
	actor->setSleepThreshold(0.0f);
	context->scene->addActor(*actor);
	actor->wakeUp();
	shape->release();
	bodies[bodyIndex] = actor;
	return 0;
}

int CreateFixture(PhysXContext* context, physx::PxRigidDynamic** bodies)
{
	if (AddStaticBox(context, physx::PxVec3(0.0f, -0.5f, 0.0f), 14.5f, 0.5f, 14.5f) != 0 ||
		AddStaticBox(context, physx::PxVec3(-14.5f, 11.75f, 0.0f), 0.5f, 12.25f, 14.5f) != 0 ||
		AddStaticBox(context, physx::PxVec3(14.5f, 11.75f, 0.0f), 0.5f, 12.25f, 14.5f) != 0 ||
		AddStaticBox(context, physx::PxVec3(0.0f, 11.75f, -14.5f), 14.5f, 12.25f, 0.5f) != 0 ||
		AddStaticBox(context, physx::PxVec3(0.0f, 11.75f, 14.5f), 14.5f, 12.25f, 0.5f) != 0)
	{
		return 2;
	}
	float originX = -0.5f * static_cast<float>(kPileXCount - 1) * kSpacing;
	float originZ = -0.5f * static_cast<float>(kPileZCount - 1) * kSpacing;
	int bodyIndex = 0;
	for (int y = 0; y < kPileYCount; ++y)
	{
		for (int z = 0; z < kPileZCount; ++z)
		{
			for (int x = 0; x < kPileXCount; ++x)
			{
				physx::PxVec3 position(originX + static_cast<float>(x) * kSpacing, kInitialY + static_cast<float>(y) * kSpacing, originZ + static_cast<float>(z) * kSpacing);
				if (AddDynamicBox(context, bodies, bodyIndex, position) != 0)
				{
					return 2;
				}
				bodyIndex += 1;
			}
		}
	}
	return bodyIndex == kDynamicBodyCount ? 0 : 2;
}

int StepScene(physx::PxScene* scene, int stepCount)
{
	for (int step = 0; step < stepCount; ++step)
	{
		scene->simulate(kTimestep);
		if (scene->fetchResults(true) == false)
		{
			std::fprintf(stderr, "run_failed reason=fetch_results\n");
			return 2;
		}
	}
	return 0;
}

int CreatePhysXCaseState(const PhysXCaseConfig& config, PhysXCaseState* state)
{
	state->config = config;
	state->context.foundation = nullptr;
	state->context.physics = nullptr;
	state->context.dispatcher = nullptr;
	state->context.scene = nullptr;
	state->context.material = nullptr;
	std::memset(state->bodies, 0, sizeof(state->bodies));
	state->completedStepCount = 0;
	state->physicsElapsedMs = 0.0;
	if (InitializeContext(config, &state->context) != 0 || CreateFixture(&state->context, state->bodies) != 0)
	{
		DestroyPhysXCaseState(state);
		return 2;
	}
	return 0;
}

int RunPhysXCaseWarmup(const PhysXCaseConfig& config)
{
	PhysXCaseState state = {};
	int status = CreatePhysXCaseState(config, &state);
	if (status == 0)
	{
		status = StepScene(state.context.scene, config.warmupSteps);
	}
	DestroyPhysXCaseState(&state);
	return status;
}

int StepPhysXCase(PhysXCaseState* state, int stepCount)
{
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	int status = StepScene(state->context.scene, stepCount);
	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	if (status != 0)
	{
		return status;
	}
	state->physicsElapsedMs += std::chrono::duration<double, std::milli>(end - start).count();
	state->completedStepCount += stepCount;
	return 0;
}

void DestroyPhysXCaseState(PhysXCaseState* state)
{
	ReleaseContext(&state->context);
}

int SamplePhysXTransforms(const PhysXCaseState& state, PhysXTransform* transforms, int transformCapacity)
{
	if (transformCapacity < kDynamicBodyCount)
	{
		return 2;
	}
	for (int index = 0; index < kDynamicBodyCount; ++index)
	{
		physx::PxTransform transform = state.bodies[index]->getGlobalPose();
		transforms[index] = {transform.p.x, transform.p.y, transform.p.z, transform.q.x, transform.q.y, transform.q.z, transform.q.w};
	}
	return 0;
}

int CopyPhysXStaticBoxes(PhysXStaticBox* boxes, int boxCapacity)
{
	if (boxCapacity < kStaticBodyCount)
	{
		return 2;
	}
	boxes[0] = {0.0f, -0.5f, 0.0f, 14.5f, 0.5f, 14.5f};
	boxes[1] = {-14.5f, 11.75f, 0.0f, 0.5f, 12.25f, 14.5f};
	boxes[2] = {14.5f, 11.75f, 0.0f, 0.5f, 12.25f, 14.5f};
	boxes[3] = {0.0f, 11.75f, -14.5f, 14.5f, 12.25f, 0.5f};
	boxes[4] = {0.0f, 11.75f, 14.5f, 14.5f, 12.25f, 0.5f};
	return 0;
}
}
