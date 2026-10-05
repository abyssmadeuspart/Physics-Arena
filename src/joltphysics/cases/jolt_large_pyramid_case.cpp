#include "jolt_large_pyramid_case.h"

#include "jolt_result_writer.h"
#include "jolt_runner_args.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <new>

JPH_SUPPRESS_WARNINGS

namespace jolt_benchmark
{
using namespace JPH;

int CreateJoltLargePyramidFixture(JoltLargePyramidState* state)
{
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	if (execution.fixtureKind != CaseFixtureKind_LargePyramid)
		return 2;
	const CaseExecutionLargePyramid& fixture = execution.largePyramid;
	BodyInterface& bodyInterface = state->physicsSystem.GetBodyInterface();
	RefConst<Shape> boxShape;
	if (CreateJoltResolvedShape(execution.selectedGeometry, execution, &boxShape) != 0)
		return 2;
	const Quat shapeRotation = JoltShapeRotation(execution.selectedGeometry.axis);
	const float mass = fixture.boxDensity * boxShape->GetVolume();
	state->dynamicBodies.clear();
	state->dynamicBodies.reserve(execution.dynamicBodyCount);
	for (std::uint32_t layer = 0; layer < fixture.rowCount; ++layer)
	{
		const std::uint32_t layerSide = fixture.rowCount - layer;
		for (std::uint32_t depth = 0; depth < layerSide; ++depth)
		{
			for (std::uint32_t column = 0; column < layerSide; ++column)
			{
				const RVec3 position(
				    fixture.baseCenter.x +
				        (static_cast<float>(column) - 0.5f * static_cast<float>(layerSide - 1u)) * fixture.boxSpacing.x,
				    fixture.baseCenter.y + static_cast<float>(layer) * fixture.boxSpacing.y,
				    fixture.baseCenter.z +
				        (static_cast<float>(depth) - 0.5f * static_cast<float>(layerSide - 1u)) * fixture.boxSpacing.z);
				BodyCreationSettings settings(boxShape, position, shapeRotation, EMotionType::Dynamic, Layers::MOVING);
				settings.mAllowSleeping = execution.sleepMode == CaseExecutionToggle_Enabled;
				settings.mFriction = execution.friction;
				settings.mRestitution = execution.restitution;
				settings.mMotionQuality = execution.continuousCollisionMode == CaseExecutionToggle_Enabled
				                              ? EMotionQuality::LinearCast
				                              : EMotionQuality::Discrete;
				settings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
				settings.mMassPropertiesOverride.mMass = mass;
				const BodyID bodyId = bodyInterface.CreateAndAddBody(settings, EActivation::Activate);
				if (bodyId.IsInvalid())
					return 2;
				state->dynamicBodies.push_back(bodyId);
			}
		}
	}
	const RefConst<Shape> projectileShape = new SphereShape(fixture.projectileRadius);
	for (std::uint32_t index = 0; index < fixture.projectileCount; ++index)
	{
		BodyCreationSettings projectileSettings(
		    projectileShape,
		    RVec3(fixture.projectileInitialCenter.x + index * fixture.projectileCenterSpacing.x,
			      fixture.projectileInitialCenter.y + index * fixture.projectileCenterSpacing.y,
			      fixture.projectileInitialCenter.z + index * fixture.projectileCenterSpacing.z),
		    Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING);
		projectileSettings.mAllowSleeping = execution.sleepMode == CaseExecutionToggle_Enabled;
		projectileSettings.mFriction = execution.friction;
		projectileSettings.mRestitution = execution.restitution;
		projectileSettings.mMotionQuality = execution.continuousCollisionMode == CaseExecutionToggle_Enabled
		                                        ? EMotionQuality::LinearCast
		                                        : EMotionQuality::Discrete;
		projectileSettings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
		projectileSettings.mMassPropertiesOverride.mMass = fixture.projectileDensity * 4.1887902047863905f *
		                                                   fixture.projectileRadius * fixture.projectileRadius *
		                                                   fixture.projectileRadius;
		const BodyID bodyId = bodyInterface.CreateAndAddBody(projectileSettings, EActivation::Activate);
		if (bodyId.IsInvalid())
			return 2;
		state->dynamicBodies.push_back(bodyId);
	}

	const RefConst<Shape> floorShape =
	    new BoxShape(Vec3(fixture.floorHalfExtents.x, fixture.floorHalfExtents.y, fixture.floorHalfExtents.z), 0.0f);
	BodyCreationSettings floorSettings(floorShape, RVec3(0.0f, -fixture.floorHalfExtents.y, 0.0f), Quat::sIdentity(),
	                                   EMotionType::Static, Layers::NON_MOVING);
	floorSettings.mFriction = execution.friction;
	floorSettings.mRestitution = execution.restitution;
	const BodyID floorId = bodyInterface.CreateAndAddBody(floorSettings, EActivation::DontActivate);
	if (floorId.IsInvalid())
		return 2;
	state->physicsSystem.OptimizeBroadPhase();
	return state->dynamicBodies.size() == execution.dynamicBodyCount &&
	               state->physicsSystem.GetNumBodies() == execution.bodyCount
	           ? 0
			   : 2;
}

int CreateJoltLargePyramidState(const JoltCaseConfig& config, JoltLargePyramidState* state)
{
	if (state == nullptr || config.caseExecution == nullptr || config.caseExecution->timestepHz == 0)
		return 2;
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
		DestroyJoltLargePyramidState(state);
		return 2;
	}
	state->physicsElapsedMs = 0.0;
	state->latestPhysicsStepMs = 0.0;
	state->completedStepCount = 0;
	state->rawStepDurations.resize(static_cast<std::size_t>(config.stepCount));
	const CaseExecutionSpec& execution = *config.caseExecution;
	state->physicsSystem.Init(execution.bodyCount, 0, execution.dynamicBodyCount * 16, execution.dynamicBodyCount * 16,
	                          state->broadPhaseLayerInterface, state->objectVsBroadPhaseLayerFilter,
	                          state->objectVsObjectLayerFilter);
	state->physicsSystem.SetGravity(Vec3(execution.gravity.x, execution.gravity.y, execution.gravity.z));
	PhysicsSettings settings = state->physicsSystem.GetPhysicsSettings();
	settings.mNumVelocitySteps = execution.nativeSolver.values[CaseSolverField_VelocityIterations];
	settings.mNumPositionSteps = execution.nativeSolver.values[CaseSolverField_PositionIterations];
	settings.mAllowSleeping = execution.sleepMode == CaseExecutionToggle_Enabled;
	state->physicsSystem.SetPhysicsSettings(settings);
	if (CreateJoltLargePyramidFixture(state) != 0)
	{
		DestroyJoltLargePyramidState(state);
		return 2;
	}
	return 0;
}

int WarmupJoltLargePyramidState(JoltLargePyramidState* state, int workUnitCount)
{
	if (state == nullptr || workUnitCount < 0)
		return 2;
	const float timestep = 1.0f / static_cast<float>(state->config.caseExecution->timestepHz);
	for (int step = 0; step < workUnitCount; ++step)
		if (state->physicsSystem.Update(
		        timestep,
		        static_cast<int>(state->config.caseExecution->nativeSolver.values[CaseSolverField_CollisionSteps]),
		        state->tempAllocator, state->selectedJobSystem) != EPhysicsUpdateError::None)
			return 2;
	return 0;
}

int StepJoltLargePyramidState(JoltLargePyramidState* state, int workUnitCount)
{
	if (state == nullptr || workUnitCount < 0 ||
	    workUnitCount > static_cast<int>(state->rawStepDurations.size()) - state->completedStepCount)
		return 2;
	const int firstStep = state->completedStepCount;
	const CaseExecutionLargePyramid& fixture = state->config.caseExecution->largePyramid;
	const float timestep = 1.0f / static_cast<float>(state->config.caseExecution->timestepHz);
	BodyInterface& bodyInterface = state->physicsSystem.GetBodyInterface();
	for (int step = 0; step < workUnitCount; ++step)
	{
		if (firstStep + step == static_cast<int>(fixture.projectileLaunchAfterWorkUnits))
		{
			const std::size_t firstProjectile = state->dynamicBodies.size() - fixture.projectileCount;
			for (std::size_t index = firstProjectile; index < state->dynamicBodies.size(); ++index)
			{
				bodyInterface.SetLinearVelocity(state->dynamicBodies[index], Vec3(fixture.projectileLaunchVelocity.x,
				                                                                  fixture.projectileLaunchVelocity.y,
				                                                                  fixture.projectileLaunchVelocity.z));
				bodyInterface.ActivateBody(state->dynamicBodies[index]);
			}
		}
		const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		const EPhysicsUpdateError updateError = state->physicsSystem.Update(
		    timestep,
		    static_cast<int>(state->config.caseExecution->nativeSolver.values[CaseSolverField_CollisionSteps]),
		    state->tempAllocator, state->selectedJobSystem);
		const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
		if (updateError != EPhysicsUpdateError::None)
			return 2;
		state->rawStepDurations[firstStep + step] = (end - start).count();
	}
	for (int step = 0; step < workUnitCount; ++step)
	{
		const double milliseconds = std::chrono::duration<double, std::milli>(
		                                std::chrono::steady_clock::duration(state->rawStepDurations[firstStep + step]))
		                                .count();
		state->physicsElapsedMs += milliseconds;
		state->latestPhysicsStepMs = milliseconds;
	}
	state->completedStepCount += workUnitCount;
	return 0;
}

void DestroyJoltLargePyramidState(JoltLargePyramidState* state)
{
	if (state == nullptr)
		return;
	delete state->threadPool;
	delete state->singleThreaded;
	delete state->tempAllocator;
	state->threadPool = nullptr;
	state->singleThreaded = nullptr;
	state->tempAllocator = nullptr;
	state->selectedJobSystem = nullptr;
	state->dynamicBodies = {};
	state->rawStepDurations = {};
}

int SampleJoltLargePyramidTransforms(const JoltLargePyramidState& state,
                                     benchmark_visual::VisualStableTransform* transforms, int transformCapacity)
{
	const int dynamicBodyCount = static_cast<int>(state.dynamicBodies.size());
	if (transforms == nullptr || transformCapacity < dynamicBodyCount)
		return 2;
	const BodyLockInterface& locks = state.physicsSystem.GetBodyLockInterface();
	for (int index = 0; index < dynamicBodyCount; ++index)
	{
		BodyLockRead lock(locks, state.dynamicBodies[index]);
		if (!lock.Succeeded())
			return 2;
		const RVec3 position = lock.GetBody().GetPosition();
		const Quat rotation = lock.GetBody().GetRotation();
		transforms[index] = {static_cast<std::uint32_t>(index),
		                     {static_cast<float>(position.GetX()), static_cast<float>(position.GetY()),
		                      static_cast<float>(position.GetZ()), rotation.GetX(), rotation.GetY(), rotation.GetZ(),
		                      rotation.GetW()}};
	}
	return 0;
}

std::uint64_t CountJoltLargePyramidInvalidTransforms(const JoltLargePyramidState& state)
{
	std::uint64_t invalidTransformCount = 0;
	const BodyLockInterface& locks = state.physicsSystem.GetBodyLockInterface();
	for (std::size_t index = 0; index < state.dynamicBodies.size(); ++index)
	{
		BodyLockRead lock(locks, state.dynamicBodies[index]);
		if (!lock.Succeeded())
			return state.dynamicBodies.size();
		const RVec3 position = lock.GetBody().GetPosition();
		const Quat rotation = lock.GetBody().GetRotation();
		if (std::isfinite(position.GetX()) == 0 || std::isfinite(position.GetY()) == 0 ||
		    std::isfinite(position.GetZ()) == 0 || std::isfinite(rotation.GetX()) == 0 ||
		    std::isfinite(rotation.GetY()) == 0 || std::isfinite(rotation.GetZ()) == 0 ||
		    std::isfinite(rotation.GetW()) == 0)
			++invalidTransformCount;
	}
	return invalidTransformCount;
}

int FormatJoltLargePyramidPhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
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

int RunJoltLargePyramidHeadless(const JoltRunRequest& request)
{
	const JoltCaseConfig config = {&request.caseExecution, request.threadCount, request.repeatIndex, request.stepCount,
	                               request.warmupSteps};
	JoltLargePyramidState state = {};
	if (CreateJoltLargePyramidState(config, &state) != 0)
		return 2;
	benchmark_stack::Capture capture = {};
	std::vector<benchmark_visual::VisualStableTransform> capturedTransforms(request.verificationMode == VerificationMode_On ? request.caseExecution.dynamicBodyCount : 0);
	const JoltCaseDescriptor* captureDescriptor = nullptr;
	int status = ResolveJoltCase(request.caseExecution, &captureDescriptor);
	if (status == 0)
		status = request.verificationMode == VerificationMode_On ? benchmark_stack::OpenCapture(request.stackStream, request.caseExecution,
		    captureDescriptor->engineId, request.threadCount, request.repeatIndex, &capture) : 0;
	JoltCaseView recordingState = {&state};
	for (std::uint32_t ordinal = 0; status == 0 && ordinal <= request.caseExecution.warmupWorkUnitCount; ++ordinal)
	{
		if (ordinal != 0)
			status = WarmupJoltLargePyramidState(&state, 1);
		if (request.verificationMode == VerificationMode_On)
		{
			benchmark_stack::BeginFrame(&capture);
			if (status == 0)
				status = captureDescriptor->sampleTransforms(recordingState, capturedTransforms.data(), static_cast<int>(capturedTransforms.size()));
			if (status == 0)
				status = benchmark_stack::AppendTransforms(&capture, ordinal == 0 ? benchmark_stack::Phase_Construction : benchmark_stack::Phase_Warmup,
				    0, ordinal, capturedTransforms.data());
		}
	}
	if (status == 0)
		status = RecordJoltCase(request, &recordingState, request.verificationMode == VerificationMode_On ? &capture : nullptr);
	const int captureStatus = request.verificationMode == VerificationMode_On ? benchmark_stack::CloseCapture(&capture) : 0;
	if (status == 0)
		status = captureStatus;
	if (status == 0)
	{
		const std::uint64_t invalidTransformCount = CountJoltLargePyramidInvalidTransforms(state);
		const int caseValid = invalidTransformCount == 0;
		const int metricValid = state.completedStepCount == request.stepCount && state.physicsElapsedMs > 0.0 &&
		                        std::isfinite(state.physicsElapsedMs) != 0;
		std::array<char, 256> physicsSettings = {};
		FormatJoltLargePyramidPhysicsSettings(request.caseExecution, request.threadCount, physicsSettings.data(),
		                                      physicsSettings.size());
		const JoltResult result = {request.caseExecution.fixtureSemantic,
		                           request.caseExecution.fixtureRevision,
		                           physicsSettings.data(),
		                           static_cast<int>(request.caseExecution.bodyCount),
		                           static_cast<int>(request.caseExecution.shapeCount),
		                           0,
		                           0,
		                           invalidTransformCount,
		                           caseValid != 0 ? "ok" : "invalid_result",
		                           caseValid != 0 && metricValid != 0 ? "ok" : "invalid_result",
		                           request.threadCount,
		                           RequestedWorkerCount(request.threadCount),
		                           state.completedStepCount,
		                           state.physicsElapsedMs,
		                           state.rawStepDurations.data(),
		                           nullptr,
		                           0};
		status = WriteJoltResult(request, result);
	}
	DestroyJoltLargePyramidState(&state);
	return status;
}

int StepJoltLargePyramidVisual(JoltCaseView* state, int workUnitCount)
{
	return state == nullptr || state->value == nullptr
	           ? 2
			   : StepJoltLargePyramidState(static_cast<JoltLargePyramidState*>(state->value), workUnitCount);
}

int BuildJoltLargePyramidVisualScene(const JoltCaseView& state, benchmark_visual::VisualGeometry* geometries,
                                     benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                                     benchmark_visual::VisualInstance* instances, int instanceCapacity,
                                     int* geometryCount, int* instanceCount)
{
	if (state.value == nullptr || geometries == nullptr || geometryCapacity < 3 || instances == nullptr ||
	    geometryCount == nullptr || instanceCount == nullptr)
		return 2;
	const JoltLargePyramidState& value = *static_cast<const JoltLargePyramidState*>(state.value);
	const CaseExecutionSpec& execution = *value.config.caseExecution;
	const CaseExecutionLargePyramid& fixture = execution.largePyramid;
	if (instanceCapacity < static_cast<int>(execution.visualInstanceCount))
		return 2;
	if (BuildResolvedVisualGeometry(execution, execution.selectedGeometry, meshes, &geometries[0]) != 0)
		return 2;
	geometries[1] = {
	    benchmark_visual::VisualGeometryKind_Sphere, fixture.projectileRadius, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0};
	geometries[2] = {benchmark_visual::VisualGeometryKind_Box,
	                 fixture.floorHalfExtents.x,
	                 fixture.floorHalfExtents.y,
	                 fixture.floorHalfExtents.z,
	                 0,
	                 0,
	                 0,
	                 0,
	                 0,
	                 0};
	const BodyLockInterface& locks = value.physicsSystem.GetBodyLockInterface();
	const int dynamicBodyCount = static_cast<int>(value.dynamicBodies.size());
	const int firstProjectile = dynamicBodyCount - static_cast<int>(fixture.projectileCount);
	for (int index = 0; index < dynamicBodyCount; ++index)
	{
		BodyLockRead lock(locks, value.dynamicBodies[index]);
		if (!lock.Succeeded())
			return 2;
		const RVec3 position = lock.GetBody().GetPosition();
		const Quat rotation = lock.GetBody().GetRotation();
		instances[index] = {};
		instances[index].geometryIndex = index >= firstProjectile ? 1u : 0u;
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
	benchmark_visual::VisualInstance& floor = instances[dynamicBodyCount];
	floor = {};
	floor.geometryIndex = 2u;
	floor.stableSlot = static_cast<std::uint32_t>(dynamicBodyCount);
	floor.transformSlot = UINT32_MAX;
	floor.initialTransform = {0.0f, -fixture.floorHalfExtents.y, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
	*geometryCount = 3;
	*instanceCount = static_cast<int>(execution.visualInstanceCount);
	return 0;
}

int SampleJoltLargePyramidVisualTransforms(const JoltCaseView& state,
                                           benchmark_visual::VisualStableTransform* transforms, int capacity)
{
	return state.value == nullptr ? 2
	                              : SampleJoltLargePyramidTransforms(
	                                    *static_cast<const JoltLargePyramidState*>(state.value), transforms, capacity);
}

const JoltCaseDescriptor& JoltLargePyramidCaseDescriptor()
{
	static const JoltCaseDescriptor descriptor = {
	    "joltphysics",
	    RunJoltLargePyramidHeadless,
	    StepJoltLargePyramidVisual,
	    BuildJoltLargePyramidVisualScene,
	    SampleJoltLargePyramidVisualTransforms,
	    nullptr,
	};
	return descriptor;
}
}
