#include "jolt_box_container_pile_10k_case.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>

JPH_SUPPRESS_WARNINGS

namespace jolt_benchmark
{
using namespace JPH;
using namespace JPH::literals;

constexpr JoltStaticBox kStaticBoxes[kStaticBodyCount] =
{
	{ 0.0f, -0.5f, 0.0f, 14.5f, 0.5f, 14.5f },
	{ -14.5f, 11.75f, 0.0f, 0.5f, 12.25f, 14.5f },
	{ 14.5f, 11.75f, 0.0f, 0.5f, 12.25f, 14.5f },
	{ 0.0f, 11.75f, -14.5f, 14.5f, 12.25f, 0.5f },
	{ 0.0f, 11.75f, 14.5f, 14.5f, 12.25f, 0.5f },
};

void TraceImpl(const char* inFMT, ...)
{
	va_list list;
	va_start(list, inFMT);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), inFMT, list);
	va_end(list);
	std::cerr << buffer << '\n';
}

#ifdef JPH_ENABLE_ASSERTS
bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, uint inLine)
{
	std::cerr << inFile << ":" << inLine << ": (" << inExpression << ") " << (inMessage != nullptr ? inMessage : "") << '\n';
	return true;
}
#endif

bool ObjectLayerPairFilterImpl::ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const
{
	switch (inObject1)
	{
	case Layers::NON_MOVING:
		return inObject2 == Layers::MOVING;
	case Layers::MOVING:
		return true;
	default:
		return false;
	}
}

BPLayerInterfaceImpl::BPLayerInterfaceImpl()
{
	mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
	mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
}

uint BPLayerInterfaceImpl::GetNumBroadPhaseLayers() const
{
	return BroadPhaseLayers::NUM_LAYERS;
}

BroadPhaseLayer BPLayerInterfaceImpl::GetBroadPhaseLayer(ObjectLayer inLayer) const
{
	return mObjectToBroadPhase[inLayer];
}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
const char* BPLayerInterfaceImpl::GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const
{
	return static_cast<BroadPhaseLayer::Type>(inLayer) == static_cast<BroadPhaseLayer::Type>(BroadPhaseLayers::NON_MOVING)
		? "NON_MOVING"
		: "MOVING";
}
#endif

bool ObjectVsBroadPhaseLayerFilterImpl::ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const
{
	switch (inLayer1)
	{
	case Layers::NON_MOVING:
		return inLayer2 == BroadPhaseLayers::MOVING;
	case Layers::MOVING:
		return true;
	default:
		return false;
	}
}

void InitializeJoltRuntime()
{
	RegisterDefaultAllocator();
	Trace = TraceImpl;
	JPH_IF_ENABLE_ASSERTS(AssertFailed = AssertFailedImpl;)
	Factory::sInstance = new Factory();
	RegisterTypes();
}

void ShutdownJoltRuntime()
{
	UnregisterTypes();
	delete Factory::sInstance;
	Factory::sInstance = nullptr;
}

int RequestedWorkerCount(int threadCount)
{
	return threadCount > 1 ? threadCount - 1 : 0;
}

void AddStaticBox(BodyInterface& bodyInterface, const Shape* shape, RVec3Arg position)
{
	BodyCreationSettings settings(shape, position, Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING);
	settings.mFriction = 0.5f;
	settings.mRestitution = 0.0f;
	bodyInterface.CreateAndAddBody(settings, EActivation::DontActivate);
}

int CreateFixture(PhysicsSystem& physicsSystem, BodyID* dynamicBodies)
{
	BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
	RefConst<Shape> floorShape = new BoxShape(Vec3(14.5f, 0.5f, 14.5f), 0.0f);
	RefConst<Shape> sideWallShape = new BoxShape(Vec3(0.5f, 12.25f, 14.5f), 0.0f);
	RefConst<Shape> frontWallShape = new BoxShape(Vec3(14.5f, 12.25f, 0.5f), 0.0f);

	AddStaticBox(bodyInterface, floorShape, RVec3(0, -0.5_r, 0));
	AddStaticBox(bodyInterface, sideWallShape, RVec3(-14.5_r, 11.75_r, 0));
	AddStaticBox(bodyInterface, sideWallShape, RVec3(14.5_r, 11.75_r, 0));
	AddStaticBox(bodyInterface, frontWallShape, RVec3(0, 11.75_r, -14.5_r));
	AddStaticBox(bodyInterface, frontWallShape, RVec3(0, 11.75_r, 14.5_r));

	RefConst<Shape> boxShape = new BoxShape(Vec3::sReplicate(kHalfExtent), 0.0f);
	float originX = -0.5f * static_cast<float>(kPileXCount - 1) * kSpacing;
	float originZ = -0.5f * static_cast<float>(kPileZCount - 1) * kSpacing;
	int bodyIndex = 0;

	for (int y = 0; y < kPileYCount; ++y)
	{
		for (int z = 0; z < kPileZCount; ++z)
		{
			for (int x = 0; x < kPileXCount; ++x)
			{
				RVec3 position(
					Real(originX + static_cast<float>(x) * kSpacing),
					Real(kInitialY + static_cast<float>(y) * kSpacing),
					Real(originZ + static_cast<float>(z) * kSpacing));
				BodyCreationSettings settings(boxShape, position, Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING);
				settings.mAllowSleeping = false;
				settings.mFriction = 0.5f;
				settings.mRestitution = 0.0f;
				settings.mMotionQuality = EMotionQuality::Discrete;
				BodyID bodyId = bodyInterface.CreateAndAddBody(settings, EActivation::Activate);
				dynamicBodies[bodyIndex] = bodyId;
				bodyIndex += 1;
			}
		}
	}
	physicsSystem.OptimizeBroadPhase();
	return bodyIndex == kDynamicBodyCount ? 0 : 2;
}

int ConfigurePhysicsSystem(JoltCaseState* state)
{
	state->physicsSystem.Init(kBodyCount, 0, 160000, 160000, state->broadPhaseLayerInterface, state->objectVsBroadPhaseLayerFilter, state->objectVsObjectLayerFilter);
	state->physicsSystem.SetGravity(Vec3(0.0f, -10.0f, 0.0f));
	PhysicsSettings settings = state->physicsSystem.GetPhysicsSettings();
	settings.mNumVelocitySteps = 4;
	settings.mNumPositionSteps = 1;
	settings.mAllowSleeping = false;
	state->physicsSystem.SetPhysicsSettings(settings);
	return CreateFixture(state->physicsSystem, state->dynamicBodies);
}

double StepPhysicsSystemTimed(PhysicsSystem& physicsSystem, TempAllocator& tempAllocator, JobSystem* jobSystem, int* stepStatus)
{
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	EPhysicsUpdateError updateError = physicsSystem.Update(kTimestep, 1, &tempAllocator, jobSystem);
	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	if (updateError != EPhysicsUpdateError::None)
	{
		std::cerr << "run_failed reason=physics_update\n";
		*stepStatus = 2;
	}
	else
	{
		*stepStatus = 0;
	}
	return std::chrono::duration<double, std::milli>(end - start).count();
}

int CreateJoltCaseState(const JoltCaseConfig& config, JoltCaseState* state)
{
	if (state == nullptr)
	{
		return 2;
	}
	state->config = config;
	state->tempAllocator = new TempAllocatorImpl(128 * 1024 * 1024);
	state->singleThreaded = new JobSystemSingleThreaded(cMaxPhysicsJobs);
	state->threadPool = new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, static_cast<uint>(config.threadCount > 1 ? config.threadCount - 1 : 1));
	state->selectedJobSystem = config.threadCount <= 1 ? static_cast<JobSystem*>(state->singleThreaded) : static_cast<JobSystem*>(state->threadPool);
	state->physicsElapsedMs = 0.0;
	state->completedStepCount = 0;
	for (int index = 0; index < kDynamicBodyCount; ++index)
	{
		state->dynamicBodies[index] = BodyID();
	}
	if (ConfigurePhysicsSystem(state) != 0)
	{
		DestroyJoltCaseState(state);
		return 2;
	}
	return 0;
}

int RunJoltCaseWarmup(const JoltCaseConfig& config)
{
	if (config.warmupSteps <= 0)
	{
		return 0;
	}
	JoltCaseState warmupState = {};
	if (CreateJoltCaseState(config, &warmupState) != 0)
	{
		return 2;
	}
	int status = StepJoltCase(&warmupState, config.warmupSteps);
	DestroyJoltCaseState(&warmupState);
	return status;
}

int StepJoltCase(JoltCaseState* state, int stepCount)
{
	if (state == nullptr || stepCount < 0)
	{
		return 2;
	}
	for (int step = 0; step < stepCount; ++step)
	{
		int stepStatus = 0;
		state->physicsElapsedMs += StepPhysicsSystemTimed(state->physicsSystem, *state->tempAllocator, state->selectedJobSystem, &stepStatus);
		if (stepStatus != 0)
		{
			return stepStatus;
		}
		state->completedStepCount += 1;
	}
	return 0;
}

void DestroyJoltCaseState(JoltCaseState* state)
{
	if (state == nullptr)
	{
		return;
	}
	delete state->threadPool;
	delete state->singleThreaded;
	delete state->tempAllocator;
	state->threadPool = nullptr;
	state->singleThreaded = nullptr;
	state->tempAllocator = nullptr;
	state->selectedJobSystem = nullptr;
}

int SampleJoltTransforms(const JoltCaseState& state, JoltTransform* transforms, int transformCapacity)
{
	if (transforms == nullptr || transformCapacity < kDynamicBodyCount)
	{
		return 2;
	}
	const BodyLockInterface& lockInterface = state.physicsSystem.GetBodyLockInterface();
	for (int index = 0; index < kDynamicBodyCount; ++index)
	{
		BodyLockRead lock(lockInterface, state.dynamicBodies[index]);
		if (!lock.Succeeded())
		{
			return 2;
		}
		const Body& body = lock.GetBody();
		RVec3 position = body.GetPosition();
		Quat rotation = body.GetRotation();
		transforms[index] = {
			static_cast<float>(position.GetX()),
			static_cast<float>(position.GetY()),
			static_cast<float>(position.GetZ()),
			rotation.GetX(),
			rotation.GetY(),
			rotation.GetZ(),
			rotation.GetW(),
		};
	}
	return 0;
}

void CopyJoltStaticBoxes(JoltStaticBox* boxes, int boxCapacity)
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
