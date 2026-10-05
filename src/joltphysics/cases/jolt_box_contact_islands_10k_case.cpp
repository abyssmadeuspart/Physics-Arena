#include "jolt_box_contact_islands_10k_case.h"

#include "jolt_result_writer.h"
#include "jolt_runner_args.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <new>

namespace jolt_benchmark
{
namespace
{
float IslandOrigin(float spacing, std::uint32_t coordinate, std::uint32_t count)
{
	return spacing * (static_cast<float>(coordinate) - 0.5f * static_cast<float>(count - 1));
}

int AddContactIslandStaticBox(JPH::BodyInterface& bodyInterface, const JPH::Shape* shape, float positionX,
                              float positionY, float positionZ, const CaseExecutionSpec& execution)
{
	JPH::BodyCreationSettings settings(shape, JPH::RVec3(positionX, positionY, positionZ), JPH::Quat::sIdentity(),
	                                   JPH::EMotionType::Static, Layers::NON_MOVING);
	settings.mFriction = execution.friction;
	settings.mRestitution = execution.restitution;
	const JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
	return bodyId.IsInvalid() ? 2 : 0;
}

int CreateContactIslandFixture(JoltContactIslandsState* state)
{
	if (state == nullptr || state->config.caseExecution == nullptr)
		return 2;
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	const CaseExecutionContactIslands& fixture = execution.contactIslands;
	JPH::BodyInterface& bodyInterface = state->physicsSystem.GetBodyInterface();
	JPH::RefConst<JPH::Shape> floorShape = new JPH::BoxShape(
	    JPH::Vec3(fixture.floorHalfExtents.x, fixture.floorHalfExtents.y, fixture.floorHalfExtents.z), 0.0f);
	JPH::RefConst<JPH::Shape> boxShape;
	if (CreateJoltResolvedShape(execution.selectedGeometry, execution, &boxShape) != 0)
		return 2;
	const JPH::Quat shapeRotation = JoltShapeRotation(execution.selectedGeometry.axis);
	const float mass = fixture.density * boxShape->GetVolume();
	for (std::uint32_t groupZ = 0; groupZ < fixture.islandGrid[1]; ++groupZ)
		for (std::uint32_t groupX = 0; groupX < fixture.islandGrid[0]; ++groupX)
			if (AddContactIslandStaticBox(
			        bodyInterface, floorShape, IslandOrigin(fixture.islandSpacing[0], groupX, fixture.islandGrid[0]),
			        -fixture.floorHalfExtents.y, IslandOrigin(fixture.islandSpacing[1], groupZ, fixture.islandGrid[1]),
			        execution) != 0)
				return 2;
	int slot = 0;
	for (std::uint32_t groupZ = 0; groupZ < fixture.islandGrid[1]; ++groupZ)
	{
		for (std::uint32_t groupX = 0; groupX < fixture.islandGrid[0]; ++groupX)
		{
			const float originX = IslandOrigin(fixture.islandSpacing[0], groupX, fixture.islandGrid[0]);
			const float originZ = IslandOrigin(fixture.islandSpacing[1], groupZ, fixture.islandGrid[1]);
			for (std::uint32_t y = 0; y < fixture.bodyGrid[1]; ++y)
			{
				for (std::uint32_t z = 0; z < fixture.bodyGrid[2]; ++z)
				{
					for (std::uint32_t x = 0; x < fixture.bodyGrid[0]; ++x)
					{
						const JPH::RVec3 position(
						    originX + (static_cast<float>(x) - 0.5f * static_cast<float>(fixture.bodyGrid[0] - 1)) *
						                  fixture.bodySpacing.x,
						    fixture.bodyInitialY + static_cast<float>(y) * fixture.bodySpacing.y,
						    originZ + (static_cast<float>(z) - 0.5f * static_cast<float>(fixture.bodyGrid[2] - 1)) *
						                  fixture.bodySpacing.z);
						JPH::BodyCreationSettings settings(boxShape, position, shapeRotation, JPH::EMotionType::Dynamic,
						                                   Layers::MOVING);
						settings.mAllowSleeping = execution.sleepMode == CaseExecutionToggle_Enabled;
						settings.mFriction = execution.friction;
						settings.mRestitution = execution.restitution;
						settings.mMotionQuality = execution.continuousCollisionMode == CaseExecutionToggle_Enabled
						                              ? JPH::EMotionQuality::LinearCast
						                              : JPH::EMotionQuality::Discrete;
						settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
						settings.mMassPropertiesOverride.mMass = mass;
						const JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::Activate);
						if (bodyId.IsInvalid())
							return 2;
						state->dynamicBodies[slot++] = bodyId;
					}
				}
			}
		}
	}
	state->physicsSystem.OptimizeBroadPhase();
	return slot == static_cast<int>(execution.dynamicBodyCount) &&
	               state->physicsSystem.GetNumBodies() == execution.bodyCount
	           ? 0
			   : 2;
}

int UpdateContactIslandWorld(JoltContactIslandsState* state, std::chrono::steady_clock::duration::rep* elapsed)
{
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	const float timestep = 1.0f / static_cast<float>(state->config.caseExecution->timestepHz);
	const JPH::EPhysicsUpdateError update = state->physicsSystem.Update(
	    timestep, static_cast<int>(state->config.caseExecution->nativeSolver.values[CaseSolverField_CollisionSteps]),
	    state->tempAllocator, state->selectedJobSystem);
	const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	if (elapsed != nullptr)
		*elapsed = (end - start).count();
	return update == JPH::EPhysicsUpdateError::None ? 0 : 2;
}
}

int CreateJoltContactIslandsState(const JoltCaseConfig& config, JoltContactIslandsState* state)
{
	if (state == nullptr || config.caseExecution == nullptr ||
	    config.caseExecution->fixtureKind != CaseFixtureKind_BoxContactIslands ||
	    config.stepCount != static_cast<int>(config.caseExecution->measuredWorkUnitCount) ||
	    config.warmupSteps != static_cast<int>(config.caseExecution->warmupWorkUnitCount))
		return 2;
	const CaseExecutionSpec& execution = *config.caseExecution;
	state->config = config;
	state->tempAllocator = new JPH::TempAllocatorImpl(128 * 1024 * 1024);
	state->singleThreaded = nullptr;
	state->threadPool = nullptr;
	if (config.threadCount <= 1)
	{
		state->singleThreaded = new JPH::JobSystemSingleThreaded(JPH::cMaxPhysicsJobs);
		state->selectedJobSystem = state->singleThreaded;
	}
	else
	{
		state->threadPool = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
		                                                 static_cast<JPH::uint>(config.threadCount - 1));
		state->selectedJobSystem = state->threadPool;
	}
	state->dynamicBodies = new (std::nothrow) JPH::BodyID[execution.dynamicBodyCount]();
	state->rawWorkUnitDurations =
	    new (std::nothrow) std::chrono::steady_clock::duration::rep[execution.measuredWorkUnitCount]();
	if (state->tempAllocator == nullptr || state->selectedJobSystem == nullptr || state->dynamicBodies == nullptr ||
	    state->rawWorkUnitDurations == nullptr)
	{
		DestroyJoltContactIslandsState(state);
		return 2;
	}
	if (state->selectedJobSystem->GetMaxConcurrency() != config.threadCount)
	{
		DestroyJoltContactIslandsState(state);
		return 2;
	}
	state->physicsSystem.Init(execution.bodyCount, 0, execution.dynamicBodyCount * 16, execution.dynamicBodyCount * 16,
	                          state->broadPhaseLayerInterface, state->objectVsBroadPhaseLayerFilter,
	                          state->objectVsObjectLayerFilter);
	state->physicsSystem.SetGravity(JPH::Vec3(execution.gravity.x, execution.gravity.y, execution.gravity.z));
	JPH::PhysicsSettings settings = state->physicsSystem.GetPhysicsSettings();
	settings.mNumVelocitySteps = execution.nativeSolver.values[CaseSolverField_VelocityIterations];
	settings.mNumPositionSteps = execution.nativeSolver.values[CaseSolverField_PositionIterations];
	settings.mAllowSleeping = execution.sleepMode == CaseExecutionToggle_Enabled;
	state->physicsSystem.SetPhysicsSettings(settings);
	if (CreateContactIslandFixture(state) != 0)
	{
		DestroyJoltContactIslandsState(state);
		return 2;
	}
	return 0;
}

int WarmupJoltContactIslands(JoltContactIslandsState* state, int workUnitCount)
{
	if (state == nullptr || state->config.caseExecution == nullptr ||
	    (workUnitCount < 0 || workUnitCount > static_cast<int>(state->config.caseExecution->warmupWorkUnitCount)))
		return 2;
	for (int workUnit = 0; workUnit < workUnitCount; ++workUnit)
		if (UpdateContactIslandWorld(state, nullptr) != 0)
			return 2;
	return 0;
}

int StepJoltContactIslands(JoltContactIslandsState* state, int workUnitCount)
{
	if (state == nullptr || workUnitCount < 0 || state->config.caseExecution == nullptr ||
	    workUnitCount >
	        static_cast<int>(state->config.caseExecution->measuredWorkUnitCount) - state->completedWorkUnitCount)
		return 2;
	const int firstWorkUnit = state->completedWorkUnitCount;
	for (int workUnit = 0; workUnit < workUnitCount; ++workUnit)
		if (UpdateContactIslandWorld(state, &state->rawWorkUnitDurations[firstWorkUnit + workUnit]) != 0)
			return 2;
	for (int workUnit = 0; workUnit < workUnitCount; ++workUnit)
	{
		const double milliseconds =
		    std::chrono::duration<double, std::milli>(
		        std::chrono::steady_clock::duration(state->rawWorkUnitDurations[firstWorkUnit + workUnit]))
		        .count();
		state->workloadElapsedMs += milliseconds;
		state->latestWorkUnitElapsedMs = milliseconds;
	}
	state->completedWorkUnitCount += workUnitCount;
	return 0;
}

void DestroyJoltContactIslandsState(JoltContactIslandsState* state)
{
	if (state == nullptr)
		return;
	delete[] state->rawWorkUnitDurations;
	delete[] state->dynamicBodies;
	delete state->threadPool;
	delete state->singleThreaded;
	delete state->tempAllocator;
	state->rawWorkUnitDurations = nullptr;
	state->dynamicBodies = nullptr;
	state->threadPool = nullptr;
	state->singleThreaded = nullptr;
	state->tempAllocator = nullptr;
	state->selectedJobSystem = nullptr;
}

int SampleJoltContactIslandsTransforms(const JoltContactIslandsState& state,
                                       benchmark_visual::VisualStableTransform* transforms, int transformCapacity)
{
	if (state.config.caseExecution == nullptr || transforms == nullptr ||
	    transformCapacity < static_cast<int>(state.config.caseExecution->dynamicBodyCount))
		return 2;
	const JPH::BodyLockInterface& locks = state.physicsSystem.GetBodyLockInterface();
	for (std::uint32_t slot = 0; slot < state.config.caseExecution->dynamicBodyCount; ++slot)
	{
		JPH::BodyLockRead lock(locks, state.dynamicBodies[slot]);
		if (!lock.Succeeded())
			return 2;
		const JPH::Body& body = lock.GetBody();
		const JPH::RVec3 position = body.GetPosition();
		const JPH::Quat rotation = body.GetRotation();
		transforms[slot] = {static_cast<std::uint32_t>(slot),
		                    {static_cast<float>(position.GetX()), static_cast<float>(position.GetY()),
		                     static_cast<float>(position.GetZ()), rotation.GetX(), rotation.GetY(), rotation.GetZ(),
		                     rotation.GetW()}};
	}
	return 0;
}

int ValidateJoltContactIslands(const JoltContactIslandsState& state, std::uint64_t* invalidTransformCount)
{
	if (invalidTransformCount == nullptr)
		return 2;
	*invalidTransformCount = 0;
	if (state.config.caseExecution == nullptr)
		return 2;
	const JPH::BodyLockInterface& locks = state.physicsSystem.GetBodyLockInterface();
	for (std::uint32_t slot = 0; slot < state.config.caseExecution->dynamicBodyCount; ++slot)
	{
		JPH::BodyLockRead lock(locks, state.dynamicBodies[slot]);
		if (!lock.Succeeded())
			return 2;
		const JPH::Body& body = lock.GetBody();
		const JPH::RVec3 position = body.GetPosition();
		const JPH::Quat rotation = body.GetRotation();
		const double px = position.GetX();
		const double py = position.GetY();
		const double pz = position.GetZ();
		if (std::isfinite(px) == 0 || std::isfinite(py) == 0 || std::isfinite(pz) == 0 ||
		    std::isfinite(rotation.GetX()) == 0 || std::isfinite(rotation.GetY()) == 0 ||
		    std::isfinite(rotation.GetZ()) == 0 || std::isfinite(rotation.GetW()) == 0)
			*invalidTransformCount += 1;
	}
	return 0;
}

int RunJoltContactIslandsHeadless(const JoltRunRequest& request)
{
	const JoltCaseConfig config = {&request.caseExecution, request.threadCount, request.repeatIndex, request.stepCount,
	                               request.warmupSteps};
	JoltContactIslandsState state = {};
	if (CreateJoltContactIslandsState(config, &state) != 0)
	{
		std::cerr << "run_failed reason=create_fixture\n";
		return 2;
	}
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
			status = WarmupJoltContactIslands(&state, 1);
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
		std::uint64_t invalidTransformCount = 0;
		const int validationStatus = ValidateJoltContactIslands(state, &invalidTransformCount);
		const int caseValid = validationStatus == 0 && invalidTransformCount == 0;
		const int metricValid = state.completedWorkUnitCount == request.stepCount && state.workloadElapsedMs > 0.0 &&
		                        std::isfinite(state.workloadElapsedMs) != 0;
		std::array<char, 256> settings = {};
		FormatJoltContactIslandsPhysicsSettings(request.caseExecution, request.threadCount, settings.data(),
		                                        settings.size());
		const JoltResult result = {
		    request.caseExecution.fixtureSemantic,
		    request.caseExecution.fixtureRevision,
		    settings.data(),
		    static_cast<int>(request.caseExecution.bodyCount),
		    static_cast<int>(request.caseExecution.shapeCount),
		    static_cast<int>(request.caseExecution.queryCount),
		    static_cast<int>(request.caseExecution.constraintCount),
		    invalidTransformCount,
		    caseValid != 0 ? "ok" : "invalid_result",
		    caseValid != 0 && metricValid != 0 ? "ok" : "invalid_result",
		    request.threadCount,
		    RequestedWorkerCount(request.threadCount),
		    state.completedWorkUnitCount,
		    state.workloadElapsedMs,
		    state.rawWorkUnitDurations,
		};
		status = WriteJoltResult(request, result);
	}
	DestroyJoltContactIslandsState(&state);
	return status;
}

int FormatJoltContactIslandsPhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                            std::size_t settingsCapacity)
{
	if (settings == nullptr || settingsCapacity == 0)
		return 2;
	const CaseExecutionContactIslands& fixture = execution.contactIslands;
	const std::uint32_t islandCount = fixture.islandGrid[0] * fixture.islandGrid[1];
	const int size = std::snprintf(
	    settings, settingsCapacity,
	    "velocity_iterations=%u; position_iterations=%u; collision_steps=%u; sleep=%s; ccd=%s; linear_damping=0.05; angular_damping=0.05; worker_count=%d; islands=%u",
	    execution.nativeSolver.values[CaseSolverField_VelocityIterations],
	    execution.nativeSolver.values[CaseSolverField_PositionIterations],
	    execution.nativeSolver.values[CaseSolverField_CollisionSteps],
	    execution.sleepMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.continuousCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    RequestedWorkerCount(threadCount), islandCount);
	return size > 0 && static_cast<std::size_t>(size) < settingsCapacity ? 0 : 2;
}

namespace
{
int StepContactIslandsVisual(JoltCaseView* state, int workUnitCount)
{
	return state == nullptr || state->value == nullptr
	           ? 2
			   : StepJoltContactIslands(static_cast<JoltContactIslandsState*>(state->value), workUnitCount);
}

int BuildContactIslandsVisualScene(const JoltCaseView& state, benchmark_visual::VisualGeometry* geometries,
                                   benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                                   benchmark_visual::VisualInstance* instances, int instanceCapacity,
                                   int* geometryCount, int* instanceCount)
{
	if (state.value == nullptr || geometries == nullptr || instances == nullptr || geometryCount == nullptr ||
	    instanceCount == nullptr)
		return 2;
	const JoltContactIslandsState& value = *static_cast<const JoltContactIslandsState*>(state.value);
	const CaseExecutionSpec& execution = *value.config.caseExecution;
	const CaseExecutionContactIslands& fixture = execution.contactIslands;
	if (geometryCapacity < 2 || instanceCapacity < static_cast<int>(execution.bodyCount))
		return 2;
	if (BuildResolvedVisualGeometry(execution, execution.selectedGeometry, meshes, &geometries[0]) != 0)
		return 2;
	geometries[1] = {benchmark_visual::VisualGeometryKind_Box,
	                 fixture.floorHalfExtents.x,
	                 fixture.floorHalfExtents.y,
	                 fixture.floorHalfExtents.z,
	                 0,
	                 0,
	                 0,
	                 0,
	                 0,
	                 0};
	const JPH::BodyLockInterface& locks = value.physicsSystem.GetBodyLockInterface();
	for (std::uint32_t slot = 0; slot < execution.dynamicBodyCount; ++slot)
	{
		JPH::BodyLockRead lock(locks, value.dynamicBodies[slot]);
		if (!lock.Succeeded())
			return 2;
		const JPH::RVec3 position = lock.GetBody().GetPosition();
		const JPH::Quat rotation = lock.GetBody().GetRotation();
		instances[slot] = {};
		instances[slot].geometryIndex = 0;
		instances[slot].stableSlot = static_cast<std::uint32_t>(slot);
		instances[slot].transformSlot = static_cast<std::uint32_t>(slot);
		instances[slot].initialTransform = {static_cast<float>(position.GetX()),
		                                    static_cast<float>(position.GetY()),
		                                    static_cast<float>(position.GetZ()),
		                                    rotation.GetX(),
		                                    rotation.GetY(),
		                                    rotation.GetZ(),
		                                    rotation.GetW()};
	}
	int staticIndex = 0;
	for (std::uint32_t groupZ = 0; groupZ < fixture.islandGrid[1]; ++groupZ)
		for (std::uint32_t groupX = 0; groupX < fixture.islandGrid[0]; ++groupX)
		{
			benchmark_visual::VisualInstance& instance = instances[execution.dynamicBodyCount + staticIndex];
			instance = {};
			instance.geometryIndex = 1;
			instance.stableSlot = static_cast<std::uint32_t>(execution.dynamicBodyCount + staticIndex);
			instance.transformSlot = UINT32_MAX;
			instance.initialTransform = {IslandOrigin(fixture.islandSpacing[0], groupX, fixture.islandGrid[0]),
			                             -fixture.floorHalfExtents.y,
			                             IslandOrigin(fixture.islandSpacing[1], groupZ, fixture.islandGrid[1]),
			                             0.0f,
			                             0.0f,
			                             0.0f,
			                             1.0f};
			++staticIndex;
		}
	*geometryCount = 2;
	*instanceCount = static_cast<int>(execution.bodyCount);
	return 0;
}

int SampleContactIslandsVisualTransforms(const JoltCaseView& state, benchmark_visual::VisualStableTransform* transforms,
                                         int capacity)
{
	return SampleJoltContactIslandsTransforms(*static_cast<const JoltContactIslandsState*>(state.value), transforms,
	                                          capacity);
}

}

const JoltCaseDescriptor& JoltContactIslandsCaseDescriptor()
{
	static const JoltCaseDescriptor descriptor = {
	    kEngineId,
	    RunJoltContactIslandsHeadless,
	    StepContactIslandsVisual,
	    BuildContactIslandsVisualScene,
	    SampleContactIslandsVisualTransforms,
	    nullptr,
	};
	return descriptor;
}
}
