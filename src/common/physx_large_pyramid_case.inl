#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <new>

namespace PHYSICS_ARENA_PHYSX_NAMESPACE
{
int AcceptPhysXLargePyramidConfig(const PhysXCaseConfig& config)
{
	return config.caseExecution != nullptr && config.caseExecution->fixtureKind == CaseFixtureKind_LargePyramid &&
	               config.stepCount == static_cast<int>(config.caseExecution->measuredWorkUnitCount) &&
	               config.warmupSteps == static_cast<int>(config.caseExecution->warmupWorkUnitCount)
	           ? 0
			   : 2;
}

physx::PxShape* CreatePhysXLargePyramidBoxShape(physx::PxRigidActor* actor, const physx::PxBoxGeometry& geometry,
                                                physx::PxMaterial& material)
{
#if defined(PHYSICS_ARENA_PHYSX5_API)
	return physx::PxRigidActorExt::createExclusiveShape(*actor, geometry, material);
#else
	return actor->createShape(geometry, material);
#endif
}

int AddPhysXLargePyramidDynamic(PhysXLargePyramidCaseState* state, const physx::PxTransform& transform,
                                const CaseExecutionGeometry& geometry, float density, int dynamicIndex)
{
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	physx::PxRigidDynamic* actor = state->context.physics->createRigidDynamic(transform);
	if (actor == nullptr)
		return 2;
	physx::PxShape* shape = AttachPhysXResolvedShape(&state->context, actor, geometry);
	if (shape == nullptr || physx::PxRigidBodyExt::updateMassAndInertia(*actor, density) == false)
	{
		actor->release();
		return 2;
	}
	benchmark_physx::ApplyEngineBodySettings(actor, execution);
	state->context.scene->addActor(*actor);
	actor->wakeUp();
	state->dynamicBodies[dynamicIndex] = actor;
	state->createdDynamicBodyCount += 1;
	return 0;
}

int CreatePhysXLargePyramidFixture(PhysXLargePyramidCaseState* state)
{
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	const CaseExecutionLargePyramid& fixture = execution.largePyramid;
	int dynamicIndex = 0;
	for (std::uint32_t layer = 0; layer < fixture.rowCount; ++layer)
	{
		const std::uint32_t layerSide = fixture.rowCount - layer;
		for (std::uint32_t depth = 0; depth < layerSide; ++depth)
		{
			for (std::uint32_t column = 0; column < layerSide; ++column)
			{
				const physx::PxVec3 position(
				    fixture.baseCenter.x +
				        (static_cast<float>(column) - 0.5f * static_cast<float>(layerSide - 1u)) * fixture.boxSpacing.x,
				    fixture.baseCenter.y + static_cast<float>(layer) * fixture.boxSpacing.y,
				    fixture.baseCenter.z +
				        (static_cast<float>(depth) - 0.5f * static_cast<float>(layerSide - 1u)) * fixture.boxSpacing.z);
				if (AddPhysXLargePyramidDynamic(
				        state, physx::PxTransform(position, PhysXShapeRotation(execution.selectedGeometry.axis)),
				        execution.selectedGeometry, fixture.boxDensity, dynamicIndex++) != 0)
					return 2;
			}
		}
	}
	const CaseExecutionGeometry projectileGeometry = {
	    CaseExecutionShape_Sphere, {}, fixture.projectileRadius, 0.0f, CaseExecutionAxis_Y};
	for (std::uint32_t index = 0; index < fixture.projectileCount; ++index)
	{
		if (AddPhysXLargePyramidDynamic(
		        state,
		        physx::PxTransform(
		            physx::PxVec3(fixture.projectileInitialCenter.x + index * fixture.projectileCenterSpacing.x,
					              fixture.projectileInitialCenter.y + index * fixture.projectileCenterSpacing.y,
					              fixture.projectileInitialCenter.z + index * fixture.projectileCenterSpacing.z)),
		        projectileGeometry, fixture.projectileDensity, dynamicIndex++) != 0)
			return 2;
	}

	state->floorBody = state->context.physics->createRigidStatic(
	    physx::PxTransform(physx::PxVec3(0.0f, -fixture.floorHalfExtents.y, 0.0f)));
	if (state->floorBody == nullptr)
		return 2;
	physx::PxShape* floorShape = CreatePhysXLargePyramidBoxShape(
	    state->floorBody,
	    physx::PxBoxGeometry(fixture.floorHalfExtents.x, fixture.floorHalfExtents.y, fixture.floorHalfExtents.z),
	    *state->context.material);
	if (floorShape == nullptr)
		return 2;
	state->context.scene->addActor(*state->floorBody);
	int shapeCount = static_cast<int>(state->floorBody->getNbShapes());
	for (int index = 0; index < state->createdDynamicBodyCount; ++index)
		shapeCount += static_cast<int>(state->dynamicBodies[index]->getNbShapes());
	return state->createdDynamicBodyCount == static_cast<int>(execution.dynamicBodyCount) &&
	               state->context.scene->getNbActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC) ==
	                   execution.dynamicBodyCount &&
	               state->context.scene->getNbActors(physx::PxActorTypeFlag::eRIGID_STATIC) ==
	                   execution.staticBodyCount &&
	               shapeCount == static_cast<int>(execution.shapeCount)
	           ? 0
			   : 2;
}

void DestroyPhysXLargePyramidCaseState(PhysXLargePyramidCaseState* state)
{
	if (state == nullptr)
		return;
	for (int index = state->createdDynamicBodyCount - 1; index >= 0; --index)
		if (state->dynamicBodies[index] != nullptr)
			state->dynamicBodies[index]->release();
	state->createdDynamicBodyCount = 0;
	if (state->floorBody != nullptr)
	{
		state->floorBody->release();
		state->floorBody = nullptr;
	}
	ReleaseContext(&state->context);
	state->dynamicBodies.reset();
	state->rawStepDurations.reset();
}

int CreatePhysXLargePyramidCaseState(const PhysXCaseConfig& config, PhysXLargePyramidCaseState* state)
{
	if (state == nullptr || AcceptPhysXLargePyramidConfig(config) != 0)
		return 2;
	const CaseExecutionSpec& execution = *config.caseExecution;
	state->config = config;
	state->context.foundation = nullptr;
	state->context.physics = nullptr;
	state->context.dispatcher = nullptr;
	state->context.scene = nullptr;
	state->context.material = nullptr;
	state->context.convexMesh = nullptr;
	state->dynamicBodies.reset(new (std::nothrow) physx::PxRigidDynamic*[execution.dynamicBodyCount]());
	state->rawStepDurations.reset(new (std::nothrow)
	                                  std::chrono::steady_clock::duration::rep[execution.measuredWorkUnitCount]);
	state->floorBody = nullptr;
	state->createdDynamicBodyCount = 0;
	state->completedStepCount = 0;
	state->physicsElapsedMs = 0.0;
	state->latestPhysicsStepMs = 0.0;
	if (state->dynamicBodies == nullptr || state->rawStepDurations == nullptr ||
	    InitializeContext(config, &state->context) != 0 || CreatePhysXLargePyramidFixture(state) != 0)
	{
		DestroyPhysXLargePyramidCaseState(state);
		return 2;
	}
	return 0;
}

int WarmupPhysXLargePyramidCase(PhysXLargePyramidCaseState* state, int workUnitCount)
{
	if (state == nullptr || state->config.caseExecution == nullptr || workUnitCount < 0)
		return 2;
	const float timestep = 1.0f / static_cast<float>(state->config.caseExecution->timestepHz);
	for (int step = 0; step < workUnitCount; ++step)
	{
		state->context.scene->simulate(timestep);
		if (state->context.scene->fetchResults(true) == false)
			return 2;
	}
	return 0;
}

int StepPhysXLargePyramidCase(PhysXLargePyramidCaseState* state, int workUnitCount)
{
	if (state == nullptr || state->config.caseExecution == nullptr || workUnitCount < 0 ||
	    workUnitCount >
	        static_cast<int>(state->config.caseExecution->measuredWorkUnitCount) - state->completedStepCount)
		return 2;
	const int firstStep = state->completedStepCount;
	const CaseExecutionLargePyramid& fixture = state->config.caseExecution->largePyramid;
	const float timestep = 1.0f / static_cast<float>(state->config.caseExecution->timestepHz);
	for (int step = 0; step < workUnitCount; ++step)
	{
		if (firstStep + step == static_cast<int>(fixture.projectileLaunchAfterWorkUnits))
		{
			const int firstProjectile = state->createdDynamicBodyCount - static_cast<int>(fixture.projectileCount);
			for (int index = firstProjectile; index < state->createdDynamicBodyCount; ++index)
			{
				state->dynamicBodies[index]->setLinearVelocity(physx::PxVec3(fixture.projectileLaunchVelocity.x,
				                                                             fixture.projectileLaunchVelocity.y,
				                                                             fixture.projectileLaunchVelocity.z));
				state->dynamicBodies[index]->wakeUp();
			}
		}
		const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		state->context.scene->simulate(timestep);
		if (state->context.scene->fetchResults(true) == false)
			return 2;
		const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
		state->rawStepDurations[firstStep + step] = (end - start).count();
	}
	for (int step = 0; step < workUnitCount; ++step)
	{
		const double milliseconds = std::chrono::duration<double, std::milli>(
		                                std::chrono::steady_clock::duration(state->rawStepDurations[firstStep + step]))
		                                .count();
		if (std::isfinite(milliseconds) == 0 || milliseconds <= 0.0)
			return 2;
		state->physicsElapsedMs += milliseconds;
		state->latestPhysicsStepMs = milliseconds;
	}
	state->completedStepCount += workUnitCount;
	return 0;
}

std::uint64_t CountPhysXLargePyramidInvalidTransforms(const PhysXLargePyramidCaseState& state)
{
	std::uint64_t invalidCount = 0;
	for (int index = 0; index < state.createdDynamicBodyCount; ++index)
		if (state.dynamicBodies[index]->getGlobalPose().isFinite() == false)
			++invalidCount;
	return invalidCount;
}

int FormatPhysXLargePyramidSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                    std::size_t settingsCapacity)
{
	if (settings == nullptr || settingsCapacity == 0)
		return 2;
	const int size = std::snprintf(
	    settings, settingsCapacity, "position_iterations=%u; velocity_iterations=%u; sleep=%s; ccd=%s; worker_count=%d",
	    execution.nativeSolver.values[CaseSolverField_PositionIterations],
	    execution.nativeSolver.values[CaseSolverField_VelocityIterations],
	    execution.sleepMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.continuousCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    RequestedWorkerCount(threadCount));
	return size > 0 && static_cast<std::size_t>(size) < settingsCapacity ? 0 : 2;
}

int RunPhysXLargePyramidHeadless(const PhysXRunRequest& request)
{
	const PhysXCaseConfig config = {&request.caseExecution, request.threadCount, request.repeatIndex, request.stepCount,
	                                request.warmupSteps};
	PhysXLargePyramidCaseState state = {};
	if (CreatePhysXLargePyramidCaseState(config, &state) != 0)
		return 2;
	benchmark_stack::Capture capture = {};
	std::vector<benchmark_visual::VisualStableTransform> capturedTransforms(request.verificationMode == VerificationMode_On ? request.caseExecution.dynamicBodyCount : 0);
	const PhysXCaseDescriptor* captureDescriptor = nullptr;
	int status = ResolvePhysXCase(request.caseExecution, &captureDescriptor);
	if (status == 0)
		status = request.verificationMode == VerificationMode_On ? benchmark_stack::OpenCapture(request.stackStream, request.caseExecution,
		    captureDescriptor->engineId, request.threadCount, request.repeatIndex, &capture) : 0;
	PhysXCaseView recordingState = {&state};
	for (std::uint32_t ordinal = 0; status == 0 && ordinal <= request.caseExecution.warmupWorkUnitCount; ++ordinal)
	{
		if (ordinal != 0)
			status = WarmupPhysXLargePyramidCase(&state, 1);
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
		status = RecordPhysXCase(request, &recordingState, request.verificationMode == VerificationMode_On ? &capture : nullptr);
	const int captureStatus = request.verificationMode == VerificationMode_On ? benchmark_stack::CloseCapture(&capture) : 0;
	if (status == 0)
		status = captureStatus;
	if (status == 0)
	{
		const std::uint64_t invalidCount = CountPhysXLargePyramidInvalidTransforms(state);
		std::array<char, 256> settings = {};
		status = FormatPhysXLargePyramidSettings(request.caseExecution, request.threadCount, settings.data(),
		                                         settings.size());
		const int metricValid = state.completedStepCount == request.stepCount && state.physicsElapsedMs > 0.0 &&
		                        std::isfinite(state.physicsElapsedMs) != 0;
		const PhysXResult result = {request.caseExecution.fixtureSemantic,
		                            request.caseExecution.fixtureRevision,
		                            settings.data(),
		                            static_cast<int>(request.caseExecution.bodyCount),
		                            static_cast<int>(request.caseExecution.shapeCount),
		                            0,
		                            0,
		                            invalidCount,
		                            invalidCount == 0 ? "ok" : "invalid_result",
		                            invalidCount == 0 && metricValid != 0 ? "ok" : "invalid_result",
		                            request.threadCount,
		                            RequestedWorkerCount(request.threadCount),
		                            state.completedStepCount,
		                            state.physicsElapsedMs,
		                            state.rawStepDurations.get()};
		if (status == 0)
			status = WritePhysXResult(request, result);
	}
	DestroyPhysXLargePyramidCaseState(&state);
	return status;
}

int StepPhysXLargePyramidVisual(PhysXCaseView* state, int workUnitCount)
{
	return state == nullptr || state->value == nullptr
	           ? 2
			   : StepPhysXLargePyramidCase(static_cast<PhysXLargePyramidCaseState*>(state->value), workUnitCount);
}

int BuildPhysXLargePyramidVisualScene(const PhysXCaseView& state, benchmark_visual::VisualGeometry* geometries,
                                      benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                                      benchmark_visual::VisualInstance* instances, int instanceCapacity,
                                      int* geometryCount, int* instanceCount)
{
	if (state.value == nullptr || geometries == nullptr || geometryCapacity < 3 || instances == nullptr ||
	    geometryCount == nullptr || instanceCount == nullptr)
		return 2;
	const PhysXLargePyramidCaseState& value = *static_cast<const PhysXLargePyramidCaseState*>(state.value);
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
	const std::uint32_t firstProjectile = execution.dynamicBodyCount - fixture.projectileCount;
	for (std::uint32_t index = 0; index < execution.dynamicBodyCount; ++index)
	{
		const physx::PxTransform transform = value.dynamicBodies[index]->getGlobalPose();
		instances[index] = {};
		instances[index].geometryIndex = index >= firstProjectile ? 1u : 0u;
		instances[index].stableSlot = index;
		instances[index].transformSlot = index;
		instances[index].initialTransform = {transform.p.x, transform.p.y, transform.p.z, transform.q.x,
		                                     transform.q.y, transform.q.z, transform.q.w};
	}
	benchmark_visual::VisualInstance& floor = instances[execution.dynamicBodyCount];
	floor = {};
	floor.geometryIndex = 2u;
	floor.stableSlot = execution.dynamicBodyCount;
	floor.transformSlot = UINT32_MAX;
	floor.initialTransform = {0.0f, -fixture.floorHalfExtents.y, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
	*geometryCount = 3;
	*instanceCount = static_cast<int>(execution.visualInstanceCount);
	return 0;
}

int SamplePhysXLargePyramidVisualTransforms(const PhysXCaseView& state,
                                            benchmark_visual::VisualStableTransform* transforms, int capacity)
{
	if (state.value == nullptr || transforms == nullptr)
		return 2;
	const PhysXLargePyramidCaseState& value = *static_cast<const PhysXLargePyramidCaseState*>(state.value);
	const std::uint32_t count = value.config.caseExecution->dynamicBodyCount;
	if (capacity < static_cast<int>(count))
		return 2;
	for (std::uint32_t index = 0; index < count; ++index)
	{
		const physx::PxTransform transform = value.dynamicBodies[index]->getGlobalPose();
		transforms[index] = {
		    index,
		    {transform.p.x, transform.p.y, transform.p.z, transform.q.x, transform.q.y, transform.q.z, transform.q.w}};
	}
	return 0;
}

const PhysXCaseDescriptor& PhysXLargePyramidCaseDescriptor()
{
	static const PhysXCaseDescriptor descriptor = {
	    kEngineId,
	    RunPhysXLargePyramidHeadless,
	    StepPhysXLargePyramidVisual,
	    BuildPhysXLargePyramidVisualScene,
	    SamplePhysXLargePyramidVisualTransforms,
	    nullptr,
	};
	return descriptor;
}
} // namespace PHYSICS_ARENA_PHYSX_NAMESPACE
