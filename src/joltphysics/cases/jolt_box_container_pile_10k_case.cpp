#include "jolt_box_container_pile_10k_case.h"

#include "jolt_result_writer.h"
#include "jolt_runner_args.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <new>

JPH_SUPPRESS_WARNINGS

namespace jolt_benchmark
{
using namespace JPH;
using namespace JPH::literals;

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
	std::cerr << inFile << ":" << inLine << ": (" << inExpression << ") " << (inMessage != nullptr ? inMessage : "")
	          << '\n';
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
	return static_cast<BroadPhaseLayer::Type>(inLayer) ==
	               static_cast<BroadPhaseLayer::Type>(BroadPhaseLayers::NON_MOVING)
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

int AddStaticBox(BodyInterface& bodyInterface, const Shape* shape, RVec3Arg position, float friction, float restitution)
{
	BodyCreationSettings settings(shape, position, Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING);
	settings.mFriction = friction;
	settings.mRestitution = restitution;
	const BodyID bodyId = bodyInterface.CreateAndAddBody(settings, EActivation::DontActivate);
	return bodyId.IsInvalid() ? 2 : 0;
}

int CreateFixture(PhysicsSystem& physicsSystem, const CaseExecutionSpec& execution, std::vector<BodyID>* dynamicBodies)
{
	if (dynamicBodies == nullptr || execution.fixtureKind != CaseFixtureKind_OpenContainerFallingPile)
		return 2;
	const CaseExecutionOpenContainer& fixture = execution.openContainer;
	BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
	for (std::uint16_t index = 0; index < fixture.staticBoxCount; ++index)
	{
		const CaseExecutionBox& box = fixture.staticBoxes[index];
		RefConst<Shape> shape = new BoxShape(Vec3(box.halfExtents.x, box.halfExtents.y, box.halfExtents.z), 0.0f);
		if (AddStaticBox(bodyInterface, shape, RVec3(box.center.x, box.center.y, box.center.z), execution.friction,
		                 execution.restitution) != 0)
			return 2;
	}

	RefConst<Shape> boxShape;
	if (CreateJoltResolvedShape(execution.selectedGeometry, execution, &boxShape) != 0)
		return 2;
	const Quat shapeRotation = JoltShapeRotation(execution.selectedGeometry.axis);
	const float mass = fixture.density * boxShape->GetVolume();
	const float originX = -0.5f * static_cast<float>(fixture.dynamicGrid[0] - 1) * fixture.dynamicSpacing.x;
	const float originZ = -0.5f * static_cast<float>(fixture.dynamicGrid[2] - 1) * fixture.dynamicSpacing.z;
	dynamicBodies->clear();
	dynamicBodies->reserve(execution.dynamicBodyCount);

	for (std::uint32_t y = 0; y < fixture.dynamicGrid[1]; ++y)
	{
		for (std::uint32_t z = 0; z < fixture.dynamicGrid[2]; ++z)
		{
			for (std::uint32_t x = 0; x < fixture.dynamicGrid[0]; ++x)
			{
				RVec3 position(Real(originX + static_cast<float>(x) * fixture.dynamicSpacing.x),
				               Real(fixture.dynamicInitialY + static_cast<float>(y) * fixture.dynamicSpacing.y),
				               Real(originZ + static_cast<float>(z) * fixture.dynamicSpacing.z));
				BodyCreationSettings settings(boxShape, position, shapeRotation, EMotionType::Dynamic, Layers::MOVING);
				settings.mAllowSleeping = execution.sleepMode == CaseExecutionToggle_Enabled;
				settings.mFriction = execution.friction;
				settings.mRestitution = execution.restitution;
				settings.mMotionQuality = execution.continuousCollisionMode == CaseExecutionToggle_Enabled
				                              ? EMotionQuality::LinearCast
				                              : EMotionQuality::Discrete;
				settings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
				settings.mMassPropertiesOverride.mMass = mass;
				BodyID bodyId = bodyInterface.CreateAndAddBody(settings, EActivation::Activate);
				if (bodyId.IsInvalid())
					return 2;
				dynamicBodies->push_back(bodyId);
			}
		}
	}
	physicsSystem.OptimizeBroadPhase();
	return dynamicBodies->size() == execution.dynamicBodyCount && physicsSystem.GetNumBodies() == execution.bodyCount
	           ? 0
			   : 2;
}

int ConfigurePhysicsSystem(JoltCaseState* state)
{
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	state->physicsSystem.Init(execution.bodyCount, 0, execution.dynamicBodyCount * 16, execution.dynamicBodyCount * 16,
	                          state->broadPhaseLayerInterface, state->objectVsBroadPhaseLayerFilter,
	                          state->objectVsObjectLayerFilter);
	state->physicsSystem.SetGravity(Vec3(execution.gravity.x, execution.gravity.y, execution.gravity.z));
	PhysicsSettings settings = state->physicsSystem.GetPhysicsSettings();
	settings.mNumVelocitySteps = execution.nativeSolver.values[CaseSolverField_VelocityIterations];
	settings.mNumPositionSteps = execution.nativeSolver.values[CaseSolverField_PositionIterations];
	settings.mAllowSleeping = execution.sleepMode == CaseExecutionToggle_Enabled;
	state->physicsSystem.SetPhysicsSettings(settings);
	return CreateFixture(state->physicsSystem, execution, &state->dynamicBodies);
}

std::chrono::steady_clock::duration::rep StepPhysicsSystemTimed(PhysicsSystem& physicsSystem,
                                                                TempAllocator& tempAllocator, JobSystem* jobSystem,
                                                                float timestep, int collisionSteps, int* stepStatus)
{
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	EPhysicsUpdateError updateError = physicsSystem.Update(timestep, collisionSteps, &tempAllocator, jobSystem);
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
	return (end - start).count();
}

int CreateJoltCaseState(const JoltCaseConfig& config, JoltCaseState* state)
{
	if (state == nullptr || config.caseExecution == nullptr || config.caseExecution->timestepHz == 0)
	{
		return 2;
	}
	state->config = config;
	state->tempAllocator = new TempAllocatorImpl(128 * 1024 * 1024);
	state->singleThreaded = nullptr;
	state->threadPool = nullptr;
	if (config.threadCount <= 1)
	{
		state->singleThreaded = new JobSystemSingleThreaded(cMaxPhysicsJobs);
		state->selectedJobSystem = state->singleThreaded;
	}
	else
	{
		state->threadPool =
		    new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, static_cast<uint>(config.threadCount - 1));
		state->selectedJobSystem = state->threadPool;
	}
	if (state->selectedJobSystem == nullptr || state->selectedJobSystem->GetMaxConcurrency() != config.threadCount)
	{
		DestroyJoltCaseState(state);
		return 2;
	}
	state->physicsElapsedMs = 0.0;
	state->latestPhysicsStepMs = 0.0;
	state->completedStepCount = 0;
	state->rawStepDurations.resize(static_cast<std::size_t>(config.stepCount));
	if (ConfigurePhysicsSystem(state) != 0)
	{
		DestroyJoltCaseState(state);
		return 2;
	}
	return 0;
}

int RunJoltCaseWarmup(const JoltCaseConfig& config, benchmark_stack::Capture* capture)
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
	int status = 0;
	std::vector<benchmark_visual::VisualStableTransform> transforms(capture != nullptr ? config.caseExecution->dynamicBodyCount : 0);
	for (int step = 0; status == 0 && step <= config.warmupSteps; ++step)
	{
		if (step != 0 && warmupState.physicsSystem.Update(
		        1.0f / static_cast<float>(config.caseExecution->timestepHz),
		        static_cast<int>(config.caseExecution->nativeSolver.values[CaseSolverField_CollisionSteps]),
		        warmupState.tempAllocator, warmupState.selectedJobSystem) != EPhysicsUpdateError::None)
		{
			status = 2;
			break;
		}
		if (capture != nullptr)
		{
			benchmark_stack::BeginFrame(capture);
			status = SampleJoltTransforms(warmupState, transforms.data(), static_cast<int>(transforms.size()));
			if (status == 0)
				status = benchmark_stack::AppendTransforms(capture, step == 0 ? benchmark_stack::Phase_Construction : benchmark_stack::Phase_Warmup,
				    0, step, transforms.data());
		}
	}
	DestroyJoltCaseState(&warmupState);
	return status;
}

int StepJoltCase(JoltCaseState* state, int stepCount)
{
	if (state == nullptr || stepCount < 0 ||
	    stepCount > static_cast<int>(state->rawStepDurations.size()) - state->completedStepCount)
	{
		return 2;
	}
	const int firstStep = state->completedStepCount;
	for (int step = 0; step < stepCount; ++step)
	{
		int stepStatus = 0;
		state->rawStepDurations[firstStep + step] = StepPhysicsSystemTimed(
		    state->physicsSystem, *state->tempAllocator, state->selectedJobSystem,
		    1.0f / static_cast<float>(state->config.caseExecution->timestepHz),
		    static_cast<int>(state->config.caseExecution->nativeSolver.values[CaseSolverField_CollisionSteps]),
		    &stepStatus);
		if (stepStatus != 0)
		{
			return stepStatus;
		}
	}
	for (int step = 0; step < stepCount; ++step)
	{
		const double milliseconds = std::chrono::duration<double, std::milli>(
		                                std::chrono::steady_clock::duration(state->rawStepDurations[firstStep + step]))
		                                .count();
		state->physicsElapsedMs += milliseconds;
		state->latestPhysicsStepMs = milliseconds;
	}
	state->completedStepCount += stepCount;
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

int SampleJoltTransforms(const JoltCaseState& state, benchmark_visual::VisualStableTransform* transforms,
                         int transformCapacity)
{
	const int dynamicBodyCount = static_cast<int>(state.dynamicBodies.size());
	if (transforms == nullptr || transformCapacity < dynamicBodyCount)
	{
		return 2;
	}
	const BodyLockInterface& lockInterface = state.physicsSystem.GetBodyLockInterface();
	for (int index = 0; index < dynamicBodyCount; ++index)
	{
		BodyLockRead lock(lockInterface, state.dynamicBodies[index]);
		if (!lock.Succeeded())
		{
			return 2;
		}
		const Body& body = lock.GetBody();
		RVec3 position = body.GetPosition();
		Quat rotation = body.GetRotation();
		transforms[index] = {static_cast<std::uint32_t>(index),
		                     {
		                         static_cast<float>(position.GetX()),
		                         static_cast<float>(position.GetY()),
		                         static_cast<float>(position.GetZ()),
		                         rotation.GetX(),
		                         rotation.GetY(),
		                         rotation.GetZ(),
		                         rotation.GetW(),
		                     }};
	}
	return 0;
}

std::uint64_t CountJoltContainerPileInvalidTransforms(const JoltCaseState& state)
{
	std::uint64_t invalidTransformCount = 0;
	const BodyLockInterface& lockInterface = state.physicsSystem.GetBodyLockInterface();
	for (std::size_t index = 0; index < state.dynamicBodies.size(); ++index)
	{
		BodyLockRead lock(lockInterface, state.dynamicBodies[index]);
		if (!lock.Succeeded())
			return state.dynamicBodies.size();
		const Body& body = lock.GetBody();
		const RVec3 position = body.GetPosition();
		const Quat rotation = body.GetRotation();
		if (std::isfinite(position.GetX()) == 0 || std::isfinite(position.GetY()) == 0 ||
		    std::isfinite(position.GetZ()) == 0 || std::isfinite(rotation.GetX()) == 0 ||
		    std::isfinite(rotation.GetY()) == 0 || std::isfinite(rotation.GetZ()) == 0 ||
		    std::isfinite(rotation.GetW()) == 0)
			invalidTransformCount += 1;
	}
	return invalidTransformCount;
}

int RunJoltContainerPileHeadless(const JoltRunRequest& request)
{
	const JoltCaseConfig config = {&request.caseExecution, request.threadCount, request.repeatIndex, request.stepCount,
	                               request.warmupSteps};
	JoltCaseState state = {};
	if (CreateJoltCaseState(config, &state) != 0)
	{
		std::cerr << "run_failed reason=create_fixture\n";
		return 2;
	}
	benchmark_stack::Capture capture = {};
	int status = request.verificationMode == VerificationMode_On ? benchmark_stack::OpenCapture(request.stackStream, request.caseExecution,
	    kEngineId, request.threadCount, request.repeatIndex, &capture) : 0;
	if (status == 0) status = RunJoltCaseWarmup(config, request.verificationMode == VerificationMode_On ? &capture : nullptr);
	capture.segment = config.warmupSteps > 0 ? 1 : 0;
	std::vector<benchmark_visual::VisualStableTransform> transforms(request.verificationMode == VerificationMode_On ? request.caseExecution.dynamicBodyCount : 0);
	if (request.verificationMode == VerificationMode_On)
	{
		benchmark_stack::BeginFrame(&capture);
		if (status == 0) status = SampleJoltTransforms(state, transforms.data(), static_cast<int>(transforms.size()));
		if (status == 0) status = benchmark_stack::AppendTransforms(&capture, benchmark_stack::Phase_Construction, capture.segment, 0, transforms.data());
	}
	JoltCaseView recordingState = {&state};
	if (status == 0) status = RecordJoltCase(request, &recordingState, request.verificationMode == VerificationMode_On ? &capture : nullptr);
	const int closed = request.verificationMode == VerificationMode_On ? benchmark_stack::CloseCapture(&capture) : 0;
	if (status == 0) status = closed;
	if (status == 0)
	{
		const std::uint64_t invalidTransformCount = CountJoltContainerPileInvalidTransforms(state);
		const int caseValid = invalidTransformCount == 0;
		const int metricValid = state.completedStepCount == request.stepCount && state.physicsElapsedMs > 0.0 &&
		                        std::isfinite(state.physicsElapsedMs) != 0 &&
		                        state.rawStepDurations.size() == static_cast<std::size_t>(request.stepCount);
		std::array<char, 256> physicsSettings = {};
		FormatJoltContainerPilePhysicsSettings(request.caseExecution, request.threadCount, physicsSettings.data(),
		                                       physicsSettings.size());
		const JoltResult result = {
		    request.caseExecution.fixtureSemantic,
		    request.caseExecution.fixtureRevision,
		    physicsSettings.data(),
		    static_cast<int>(request.caseExecution.bodyCount),
		    static_cast<int>(request.caseExecution.shapeCount),
		    static_cast<int>(request.caseExecution.queryCount),
		    static_cast<int>(request.caseExecution.constraintCount),
		    invalidTransformCount,
		    caseValid != 0 ? "ok" : "invalid_result",
		    caseValid != 0 && metricValid != 0 ? "ok" : "invalid_result",
		    request.threadCount,
		    RequestedWorkerCount(request.threadCount),
		    state.completedStepCount,
		    state.physicsElapsedMs,
		    state.rawStepDurations.data(),
		};
		if (status == 0)
			status = WriteJoltResult(request, result);
	}
	DestroyJoltCaseState(&state);
	return status;
}

int FormatJoltContainerPilePhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                           std::size_t settingsCapacity)
{
	if (settings == nullptr || settingsCapacity == 0)
		return 2;
	const int size = std::snprintf(
	    settings, settingsCapacity,
	    "velocity_iterations=%u; position_iterations=%u; collision_steps=%u; sleep=%s; ccd=%s; linear_damping=0.05; angular_damping=0.05; worker_count=%d",
	    execution.nativeSolver.values[CaseSolverField_VelocityIterations],
	    execution.nativeSolver.values[CaseSolverField_PositionIterations],
	    execution.nativeSolver.values[CaseSolverField_CollisionSteps],
	    execution.sleepMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.continuousCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    RequestedWorkerCount(threadCount));
	return size > 0 && static_cast<std::size_t>(size) < settingsCapacity ? 0 : 2;
}

namespace
{
int StepContainerPileVisual(JoltCaseView* state, int workUnitCount)
{
	return state == nullptr || state->value == nullptr
	           ? 2
			   : StepJoltCase(static_cast<JoltCaseState*>(state->value), workUnitCount);
}

int BuildContainerPileVisualScene(const JoltCaseView& state, benchmark_visual::VisualGeometry* geometries,
                                  benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                                  benchmark_visual::VisualInstance* instances, int instanceCapacity, int* geometryCount,
                                  int* instanceCount)
{
	if (state.value == nullptr || geometries == nullptr || instances == nullptr || geometryCount == nullptr ||
	    instanceCount == nullptr)
		return 2;
	const JoltCaseState& value = *static_cast<const JoltCaseState*>(state.value);
	const CaseExecutionSpec& execution = *value.config.caseExecution;
	const CaseExecutionOpenContainer& fixture = execution.openContainer;
	if (geometryCapacity < 1 + fixture.staticBoxCount || instanceCapacity < static_cast<int>(execution.bodyCount))
		return 2;
	if (BuildResolvedVisualGeometry(execution, execution.selectedGeometry, meshes, &geometries[0]) != 0)
		return 2;
	for (std::uint16_t index = 0; index < fixture.staticBoxCount; ++index)
	{
		const CaseExecutionVector3 halfExtents = fixture.staticBoxes[index].halfExtents;
		geometries[index + 1] = {
		    benchmark_visual::VisualGeometryKind_Box, halfExtents.x, halfExtents.y, halfExtents.z, 0, 0, 0, 0, 0, 0};
	}
	const BodyLockInterface& locks = value.physicsSystem.GetBodyLockInterface();
	const int dynamicBodyCount = static_cast<int>(value.dynamicBodies.size());
	for (int index = 0; index < dynamicBodyCount; ++index)
	{
		BodyLockRead lock(locks, value.dynamicBodies[index]);
		if (!lock.Succeeded())
			return 2;
		const RVec3 position = lock.GetBody().GetPosition();
		const Quat rotation = lock.GetBody().GetRotation();
		instances[index] = {};
		instances[index].geometryIndex = 0;
		instances[index].stableSlot = static_cast<std::uint32_t>(index);
		instances[index].transformSlot = static_cast<std::uint32_t>(index);
		instances[index].initialTransform = {static_cast<float>(position.GetX()),
		                                     static_cast<float>(position.GetY()),
		                                     static_cast<float>(position.GetZ()),
		                                     rotation.GetX(),
		                                     rotation.GetY(),
		                                     rotation.GetZ(),
		                                     rotation.GetW()};
	}
	for (std::uint16_t index = 0; index < fixture.staticBoxCount; ++index)
	{
		const CaseExecutionBox& box = fixture.staticBoxes[index];
		benchmark_visual::VisualInstance& instance = instances[dynamicBodyCount + index];
		instance = {};
		instance.geometryIndex = index + 1;
		instance.stableSlot = static_cast<std::uint32_t>(dynamicBodyCount + index);
		instance.transformSlot = UINT32_MAX;
		instance.initialTransform = {box.center.x, box.center.y, box.center.z, 0.0f, 0.0f, 0.0f, 1.0f};
	}
	*geometryCount = 1 + fixture.staticBoxCount;
	*instanceCount = static_cast<int>(execution.bodyCount);
	return 0;
}

int SampleContainerPileVisualTransforms(const JoltCaseView& state, benchmark_visual::VisualStableTransform* transforms,
                                        int capacity)
{
	return SampleJoltTransforms(*static_cast<const JoltCaseState*>(state.value), transforms, capacity);
}

}

const JoltCaseDescriptor& JoltContainerPileCaseDescriptor()
{
	static const JoltCaseDescriptor descriptor = {
	    kEngineId,
	    RunJoltContainerPileHeadless,
	    StepContainerPileVisual,
	    BuildContainerPileVisualScene,
	    SampleContainerPileVisualTransforms,
	    nullptr,
	};
	return descriptor;
}
}
