#include "nvidia_physx5_box_contact_islands_10k_case.h"
#include "physx_engine_settings.h"

#include "nvidia_physx5_case_registry.h"
#include "nvidia_physx5_result_writer.h"
#include "nvidia_physx5_runner_args.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <new>

namespace nvidia_physx5_benchmark
{
namespace
{
float ContactIslandOrigin(float spacing, std::uint32_t coordinate, std::uint32_t count)
{
	return spacing * (static_cast<float>(coordinate) - 0.5f * static_cast<float>(count - 1));
}

void ReleaseContactIslandsContext(PhysXContext* context)
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
	if (context->convexMesh != nullptr)
	{
		context->convexMesh->release();
		context->convexMesh = nullptr;
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

int InitializeContactIslandsContext(const PhysXCaseConfig& config, PhysXContext* context)
{
	if (config.caseExecution == nullptr)
		return 2;
	const CaseExecutionSpec& execution = *config.caseExecution;
	context->foundation = PxCreateFoundation(PX_PHYSICS_VERSION, context->allocator, context->errorCallback);
	if (context->foundation == nullptr)
		return 2;
	physx::PxTolerancesScale scale;
	context->physics = PxCreatePhysics(PX_PHYSICS_VERSION, *context->foundation, scale, false, nullptr);
	if (context->physics == nullptr)
		return 2;
	if (InitializePhysXResolvedShape(context, execution) != 0)
		return 2;
	const int workerCount = RequestedWorkerCount(config.threadCount);
	context->dispatcher = physx::PxDefaultCpuDispatcherCreate(static_cast<physx::PxU32>(workerCount));
	if (context->dispatcher == nullptr)
		return 2;
	if (context->dispatcher->getWorkerCount() != static_cast<physx::PxU32>(workerCount))
	{
		std::fprintf(stderr, "run_failed reason=dispatcher_worker_count_mismatch requested=%d effective=%u\n",
		             workerCount, static_cast<unsigned int>(context->dispatcher->getWorkerCount()));
		return 2;
	}
	physx::PxSceneDesc sceneDesc(context->physics->getTolerancesScale());
	sceneDesc.gravity = physx::PxVec3(execution.gravity.x, execution.gravity.y, execution.gravity.z);
	sceneDesc.cpuDispatcher = context->dispatcher;
	benchmark_physx::ApplyEngineSceneSettings(&sceneDesc, execution);
	context->scene = context->physics->createScene(sceneDesc);
	if (context->scene == nullptr)
		return 2;
	context->material = context->physics->createMaterial(execution.friction, execution.friction, execution.restitution);
	return context->material != nullptr ? 0 : 2;
}

int AddContactIslandsStaticBox(PhysXContext* context, const physx::PxVec3& position,
                               const CaseExecutionVector3& halfExtents)
{
	physx::PxRigidStatic* actor = context->physics->createRigidStatic(physx::PxTransform(position));
	if (actor == nullptr)
		return 2;
	physx::PxShape* shape = physx::PxRigidActorExt::createExclusiveShape(
	    *actor, physx::PxBoxGeometry(halfExtents.x, halfExtents.y, halfExtents.z), *context->material);
	if (shape == nullptr)
	{
		actor->release();
		return 2;
	}
	context->scene->addActor(*actor);
	return 0;
}

int AddContactIslandsDynamicBox(PhysXContext* context, PhysXContactIslandsState* state, int bodySlot,
                                const physx::PxVec3& position)
{
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	const CaseExecutionContactIslands& fixture = execution.contactIslands;
	physx::PxRigidDynamic* actor = context->physics->createRigidDynamic(
	    physx::PxTransform(position, PhysXShapeRotation(execution.selectedGeometry.axis)));
	if (actor == nullptr)
		return 2;
	physx::PxShape* shape = AttachPhysXResolvedShape(context, actor, execution.selectedGeometry);
	if (shape == nullptr)
	{
		actor->release();
		return 2;
	}
	if (physx::PxRigidBodyExt::updateMassAndInertia(*actor, fixture.density) == false)
	{
		actor->release();
		return 2;
	}
	benchmark_physx::ApplyEngineBodySettings(actor, execution);
	context->scene->addActor(*actor);
	actor->wakeUp();
	state->bodies[bodySlot] = actor;
	return 0;
}

int CreateContactIslandsFixture(PhysXContactIslandsState* state)
{
	if (state == nullptr || state->config.caseExecution == nullptr)
		return 2;
	const CaseExecutionSpec& execution = *state->config.caseExecution;
	const CaseExecutionContactIslands& fixture = execution.contactIslands;
	for (std::uint32_t groupZ = 0; groupZ < fixture.islandGrid[1]; ++groupZ)
		for (std::uint32_t groupX = 0; groupX < fixture.islandGrid[0]; ++groupX)
			if (AddContactIslandsStaticBox(
			        &state->context,
			        physx::PxVec3(ContactIslandOrigin(fixture.islandSpacing[0], groupX, fixture.islandGrid[0]),
					              -fixture.floorHalfExtents.y,
					              ContactIslandOrigin(fixture.islandSpacing[1], groupZ, fixture.islandGrid[1])),
			        fixture.floorHalfExtents) != 0)
				return 2;
	int bodySlot = 0;
	for (std::uint32_t groupZ = 0; groupZ < fixture.islandGrid[1]; ++groupZ)
	{
		for (std::uint32_t groupX = 0; groupX < fixture.islandGrid[0]; ++groupX)
		{
			const float originX = ContactIslandOrigin(fixture.islandSpacing[0], groupX, fixture.islandGrid[0]);
			const float originZ = ContactIslandOrigin(fixture.islandSpacing[1], groupZ, fixture.islandGrid[1]);
			for (std::uint32_t y = 0; y < fixture.bodyGrid[1]; ++y)
				for (std::uint32_t z = 0; z < fixture.bodyGrid[2]; ++z)
					for (std::uint32_t x = 0; x < fixture.bodyGrid[0]; ++x)
					{
						const physx::PxVec3 position(
						    originX + (static_cast<float>(x) - 0.5f * static_cast<float>(fixture.bodyGrid[0] - 1)) *
						                  fixture.bodySpacing.x,
						    fixture.bodyInitialY + static_cast<float>(y) * fixture.bodySpacing.y,
						    originZ + (static_cast<float>(z) - 0.5f * static_cast<float>(fixture.bodyGrid[2] - 1)) *
						                  fixture.bodySpacing.z);
						if (AddContactIslandsDynamicBox(&state->context, state, bodySlot, position) != 0)
							return 2;
						++bodySlot;
					}
		}
	}
	return bodySlot == static_cast<int>(execution.dynamicBodyCount) ? 0 : 2;
}

int UpdateContactIslandsScene(PhysXContactIslandsState* state, std::chrono::steady_clock::duration::rep* elapsed)
{
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	state->context.scene->simulate(1.0f / static_cast<float>(state->config.caseExecution->timestepHz));
	const int status = state->context.scene->fetchResults(true) != false ? 0 : 2;
	const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	if (elapsed != nullptr)
		*elapsed = (end - start).count();
	return status;
}
}

int CreatePhysXContactIslandsState(const PhysXCaseConfig& config, PhysXContactIslandsState* state)
{
	if (state == nullptr || config.caseExecution == nullptr ||
	    config.caseExecution->fixtureKind != CaseFixtureKind_BoxContactIslands ||
	    config.stepCount != static_cast<int>(config.caseExecution->measuredWorkUnitCount) ||
	    config.warmupSteps != static_cast<int>(config.caseExecution->warmupWorkUnitCount))
		return 2;
	const CaseExecutionSpec& execution = *config.caseExecution;
	state->config = config;
	state->context.foundation = nullptr;
	state->context.physics = nullptr;
	state->context.dispatcher = nullptr;
	state->context.scene = nullptr;
	state->context.material = nullptr;
	state->context.convexMesh = nullptr;
	state->bodies = nullptr;
	state->rawWorkUnitDurations = nullptr;
	state->completedWorkUnitCount = 0;
	state->workloadElapsedMs = 0.0;
	state->latestWorkUnitElapsedMs = 0.0;
	state->bodies = new (std::nothrow) physx::PxRigidDynamic*[execution.dynamicBodyCount]();
	state->rawWorkUnitDurations =
	    new (std::nothrow) std::chrono::steady_clock::duration::rep[execution.measuredWorkUnitCount]();
	if (state->bodies == nullptr || state->rawWorkUnitDurations == nullptr ||
	    InitializeContactIslandsContext(config, &state->context) != 0 || CreateContactIslandsFixture(state) != 0)
	{
		DestroyPhysXContactIslandsState(state);
		return 2;
	}
	return 0;
}

int WarmupPhysXContactIslands(PhysXContactIslandsState* state, int workUnitCount)
{
	if (state == nullptr || state->config.caseExecution == nullptr ||
	    (workUnitCount < 0 || workUnitCount > static_cast<int>(state->config.caseExecution->warmupWorkUnitCount)))
		return 2;
	for (int workUnit = 0; workUnit < workUnitCount; ++workUnit)
		if (UpdateContactIslandsScene(state, nullptr) != 0)
			return 2;
	return 0;
}

int StepPhysXContactIslands(PhysXContactIslandsState* state, int workUnitCount)
{
	if (state == nullptr || workUnitCount < 0 || state->config.caseExecution == nullptr ||
	    workUnitCount >
	        static_cast<int>(state->config.caseExecution->measuredWorkUnitCount) - state->completedWorkUnitCount)
		return 2;
	const int firstWorkUnit = state->completedWorkUnitCount;
	for (int workUnit = 0; workUnit < workUnitCount; ++workUnit)
		if (UpdateContactIslandsScene(state, &state->rawWorkUnitDurations[firstWorkUnit + workUnit]) != 0)
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

void DestroyPhysXContactIslandsState(PhysXContactIslandsState* state)
{
	if (state == nullptr)
		return;
	ReleaseContactIslandsContext(&state->context);
	delete[] state->bodies;
	delete[] state->rawWorkUnitDurations;
	state->bodies = nullptr;
	state->rawWorkUnitDurations = nullptr;
}

int SamplePhysXContactIslandsTransforms(const PhysXContactIslandsState& state, PhysXTransform* transforms,
                                        int transformCapacity)
{
	if (state.config.caseExecution == nullptr || transforms == nullptr ||
	    transformCapacity < static_cast<int>(state.config.caseExecution->dynamicBodyCount))
		return 2;
	for (std::uint32_t slot = 0; slot < state.config.caseExecution->dynamicBodyCount; ++slot)
	{
		const physx::PxTransform transform = state.bodies[slot]->getGlobalPose();
		transforms[slot] = {transform.p.x, transform.p.y, transform.p.z, transform.q.x,
		                    transform.q.y, transform.q.z, transform.q.w};
	}
	return 0;
}

int ValidatePhysXContactIslands(const PhysXContactIslandsState& state, std::uint64_t* invalidTransformCount)
{
	if (invalidTransformCount == nullptr)
		return 2;
	*invalidTransformCount = 0;
	if (state.config.caseExecution == nullptr)
		return 2;
	for (std::uint32_t slot = 0; slot < state.config.caseExecution->dynamicBodyCount; ++slot)
	{
		const physx::PxTransform transform = state.bodies[slot]->getGlobalPose();
		if (transform.isFinite() == false)
			++*invalidTransformCount;
	}
	return 0;
}

int RunPhysXContactIslandsHeadless(const PhysXRunRequest& request)
{
	const PhysXCaseConfig config = {&request.caseExecution, request.threadCount, request.repeatIndex, request.stepCount,
	                                request.warmupSteps};
	PhysXContactIslandsState state = {};
	if (CreatePhysXContactIslandsState(config, &state) != 0)
	{
		std::cerr << "run_failed reason=create_fixture\n";
		return 2;
	}
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
			status = WarmupPhysXContactIslands(&state, 1);
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
		std::uint64_t invalidTransformCount = 0;
		const int validationStatus = ValidatePhysXContactIslands(state, &invalidTransformCount);
		const int caseValid = validationStatus == 0 && invalidTransformCount == 0;
		const int metricValid = state.completedWorkUnitCount == request.stepCount && state.workloadElapsedMs > 0.0 &&
		                        std::isfinite(state.workloadElapsedMs) != 0;
		std::array<char, 256> settings = {};
		FormatPhysXContactIslandsPhysicsSettings(request.caseExecution, request.threadCount, settings.data(),
		                                         settings.size());
		const PhysXResult result = {request.caseExecution.fixtureSemantic,
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
		                            state.rawWorkUnitDurations};
		status = WritePhysXResult(request, result);
	}
	DestroyPhysXContactIslandsState(&state);
	return status;
}

int FormatPhysXContactIslandsPhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
                                             std::size_t settingsCapacity)
{
	if (settings == nullptr || settingsCapacity == 0)
		return 2;
	const CaseExecutionContactIslands& fixture = execution.contactIslands;
	const std::uint32_t islandCount = fixture.islandGrid[0] * fixture.islandGrid[1];
	const int size =
	    std::snprintf(settings, settingsCapacity,
		              "position_iterations=%u; velocity_iterations=%u; sleep=%s; ccd=%s; worker_count=%d; islands=%u",
		              execution.nativeSolver.values[CaseSolverField_PositionIterations],
		              execution.nativeSolver.values[CaseSolverField_VelocityIterations],
		              execution.sleepMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
		              execution.continuousCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
		              RequestedWorkerCount(threadCount), islandCount);
	return size > 0 && static_cast<std::size_t>(size) < settingsCapacity ? 0 : 2;
}

namespace
{
int StepContactIslandsVisual(PhysXCaseView* state, int count)
{
	return state == nullptr || state->value == nullptr
	           ? 2
			   : StepPhysXContactIslands(static_cast<PhysXContactIslandsState*>(state->value), count);
}

int BuildContactIslandsVisualScene(const PhysXCaseView& state, benchmark_visual::VisualGeometry* geometries,
                                   benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                                   benchmark_visual::VisualInstance* instances, int instanceCapacity,
                                   int* geometryCount, int* instanceCount)
{
	if (state.value == nullptr || geometries == nullptr || instances == nullptr || geometryCount == nullptr ||
	    instanceCount == nullptr)
		return 2;
	const PhysXContactIslandsState& value = *static_cast<const PhysXContactIslandsState*>(state.value);
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
	for (std::uint32_t index = 0; index < execution.dynamicBodyCount; ++index)
	{
		const physx::PxTransform transform = value.bodies[index]->getGlobalPose();
		instances[index] = {};
		instances[index].geometryIndex = 0;
		instances[index].stableSlot = static_cast<std::uint32_t>(index);
		instances[index].transformSlot = static_cast<std::uint32_t>(index);
		instances[index].initialTransform = {transform.p.x, transform.p.y, transform.p.z, transform.q.x,
		                                     transform.q.y, transform.q.z, transform.q.w};
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
			instance.initialTransform = {ContactIslandOrigin(fixture.islandSpacing[0], groupX, fixture.islandGrid[0]),
			                             -fixture.floorHalfExtents.y,
			                             ContactIslandOrigin(fixture.islandSpacing[1], groupZ, fixture.islandGrid[1]),
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

int SampleContactIslandsVisualTransforms(const PhysXCaseView& state,
                                         benchmark_visual::VisualStableTransform* transforms, int capacity)
{
	if (state.value == nullptr || transforms == nullptr)
		return 2;
	const PhysXContactIslandsState& value = *static_cast<const PhysXContactIslandsState*>(state.value);
	if (capacity < static_cast<int>(value.config.caseExecution->dynamicBodyCount))
		return 2;
	for (std::uint32_t index = 0; index < value.config.caseExecution->dynamicBodyCount; ++index)
	{
		const physx::PxTransform transform = value.bodies[index]->getGlobalPose();
		transforms[index] = {
		    static_cast<std::uint32_t>(index),
		    {transform.p.x, transform.p.y, transform.p.z, transform.q.x, transform.q.y, transform.q.z, transform.q.w}};
	}
	return 0;
}

}

const PhysXCaseDescriptor& PhysXContactIslandsCaseDescriptor()
{
	static const PhysXCaseDescriptor descriptor = {
	    kEngineId,
	    RunPhysXContactIslandsHeadless,
	    StepContactIslandsVisual,
	    BuildContactIslandsVisualScene,
	    SampleContactIslandsVisualTransforms,
	    nullptr,
	};
	return descriptor;
}
}
