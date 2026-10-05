#include "nvidia_physx5_box_container_pile_10k_case.h"
#include "physx_engine_settings.h"

#include "nvidia_physx5_case_registry.h"
#include "nvidia_physx5_result_writer.h"
#include "nvidia_physx5_runner_args.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <new>

namespace nvidia_physx5_benchmark
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
		if (context->convexMesh != nullptr)
		{
			context->convexMesh->release();
			context->convexMesh = nullptr;
		}
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
	if (config.caseExecution == nullptr)
		return 2;
	const CaseExecutionSpec& execution = *config.caseExecution;
	context->foundation = PxCreateFoundation(PX_PHYSICS_VERSION, context->allocator, context->errorCallback);
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
	if (InitializePhysXResolvedShape(context, execution) != 0)
		return 2;
	context->dispatcher = physx::PxDefaultCpuDispatcherCreate(static_cast<physx::PxU32>(workerCount));
	if (context->dispatcher == nullptr)
	{
		std::fprintf(stderr, "run_failed reason=create_dispatcher workers=%d\n", workerCount);
		return 2;
	}
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
	{
		std::fprintf(stderr, "run_failed reason=create_scene\n");
		return 2;
	}
	context->material = context->physics->createMaterial(execution.friction, execution.friction, execution.restitution);
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
	physx::PxShape* shape =
	    physx::PxRigidActorExt::createExclusiveShape(*actor, physx::PxBoxGeometry(hx, hy, hz), *context->material);
	if (shape == nullptr)
	{
		actor->release();
		std::fprintf(stderr, "run_failed reason=create_static_shape\n");
		return 2;
	}
	context->scene->addActor(*actor);
	return 0;
}

int AddDynamicBox(PhysXContext* context, const CaseExecutionSpec& execution,
                  std::vector<physx::PxRigidDynamic*>* bodies, physx::PxVec3 position)
{
	physx::PxRigidDynamic* actor = context->physics->createRigidDynamic(
	    physx::PxTransform(position, PhysXShapeRotation(execution.selectedGeometry.axis)));
	if (actor == nullptr)
	{
		std::fprintf(stderr, "run_failed reason=create_dynamic\n");
		return 2;
	}
	const CaseExecutionOpenContainer& fixture = execution.openContainer;
	physx::PxShape* shape = AttachPhysXResolvedShape(context, actor, execution.selectedGeometry);
	if (shape == nullptr)
	{
		actor->release();
		std::fprintf(stderr, "run_failed reason=create_dynamic_shape\n");
		return 2;
	}
	if (physx::PxRigidBodyExt::updateMassAndInertia(*actor, fixture.density) == false)
	{
		actor->release();
		std::fprintf(stderr, "run_failed reason=mass_inertia\n");
		return 2;
	}
	benchmark_physx::ApplyEngineBodySettings(actor, execution);
	context->scene->addActor(*actor);
	actor->wakeUp();
	bodies->push_back(actor);
	return 0;
}

int CreateFixture(PhysXContext* context, const CaseExecutionSpec& execution,
                  std::vector<physx::PxRigidDynamic*>* bodies)
{
	if (bodies == nullptr)
		return 2;
	const CaseExecutionOpenContainer& fixture = execution.openContainer;
	for (std::uint16_t index = 0; index < fixture.staticBoxCount; ++index)
	{
		const CaseExecutionBox& box = fixture.staticBoxes[index];
		if (AddStaticBox(context, physx::PxVec3(box.center.x, box.center.y, box.center.z), box.halfExtents.x,
		                 box.halfExtents.y, box.halfExtents.z) != 0)
			return 2;
	}
	bodies->clear();
	bodies->reserve(execution.dynamicBodyCount);
	const float originX = -0.5f * static_cast<float>(fixture.dynamicGrid[0] - 1) * fixture.dynamicSpacing.x;
	const float originZ = -0.5f * static_cast<float>(fixture.dynamicGrid[2] - 1) * fixture.dynamicSpacing.z;
	for (std::uint32_t y = 0; y < fixture.dynamicGrid[1]; ++y)
	{
		for (std::uint32_t z = 0; z < fixture.dynamicGrid[2]; ++z)
		{
			for (std::uint32_t x = 0; x < fixture.dynamicGrid[0]; ++x)
			{
				physx::PxVec3 position(originX + static_cast<float>(x) * fixture.dynamicSpacing.x,
				                       fixture.dynamicInitialY + static_cast<float>(y) * fixture.dynamicSpacing.y,
				                       originZ + static_cast<float>(z) * fixture.dynamicSpacing.z);
				if (AddDynamicBox(context, execution, bodies, position) != 0)
				{
					return 2;
				}
			}
		}
	}
	return bodies->size() == execution.dynamicBodyCount ? 0 : 2;
}

int StepScene(physx::PxScene* scene, float timestep, int stepCount)
{
	for (int step = 0; step < stepCount; ++step)
	{
		scene->simulate(timestep);
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
	state->context.convexMesh = nullptr;
	if (config.caseExecution == nullptr || config.caseExecution->timestepHz == 0)
		return 2;
	state->completedStepCount = 0;
	state->physicsElapsedMs = 0.0;
	state->latestPhysicsStepMs = 0.0;
	state->rawStepDurations.resize(static_cast<std::size_t>(config.stepCount));
	if (InitializeContext(config, &state->context) != 0 ||
	    CreateFixture(&state->context, *config.caseExecution, &state->bodies) != 0)
	{
		DestroyPhysXCaseState(state);
		return 2;
	}
	return 0;
}

int RunPhysXCaseWarmup(PhysXCaseState* state, int warmupStepCount, benchmark_stack::Capture* capture)
{
	if (state == nullptr || warmupStepCount < 0)
		return 2;
	const PhysXCaseConfig config = state->config;
	std::vector<benchmark_visual::VisualStableTransform> transforms(capture != nullptr ? config.caseExecution->dynamicBodyCount : 0);
	const PhysXCaseDescriptor* descriptor = nullptr;
	int status = ResolvePhysXCase(*config.caseExecution, &descriptor);
	const PhysXCaseView view = {state};
	for (int step = 0; status == 0 && step <= warmupStepCount; ++step)
	{
		if (step != 0) status = StepScene(state->context.scene, 1.0f / config.caseExecution->timestepHz, 1);
		if (capture != nullptr)
		{
			benchmark_stack::BeginFrame(capture);
			if (status == 0) status = descriptor->sampleTransforms(view, transforms.data(), static_cast<int>(transforms.size()));
			if (status == 0) status = benchmark_stack::AppendTransforms(capture, step == 0 ? benchmark_stack::Phase_Construction : benchmark_stack::Phase_Warmup,
			    0, step, transforms.data());
		}
	}
	DestroyPhysXCaseState(state);
	if (status != 0)
		return status;
	state->rawStepDurations.clear();
	return CreatePhysXCaseState(config, state);
}

int StepPhysXCase(PhysXCaseState* state, int stepCount)
{
	if (state == nullptr || stepCount < 0 ||
	    stepCount > static_cast<int>(state->rawStepDurations.size()) - state->completedStepCount)
		return 2;
	const int firstStep = state->completedStepCount;
	for (int step = 0; step < stepCount; ++step)
	{
		const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		state->context.scene->simulate(1.0f / static_cast<float>(state->config.caseExecution->timestepHz));
		if (state->context.scene->fetchResults(true) == false)
		{
			std::fprintf(stderr, "run_failed reason=fetch_results\n");
			return 2;
		}
		const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
		state->rawStepDurations[firstStep + step] = (end - start).count();
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

void DestroyPhysXCaseState(PhysXCaseState* state)
{
	ReleaseContext(&state->context);
}

int SamplePhysXTransforms(const PhysXCaseState& state, PhysXTransform* transforms, int transformCapacity)
{
	if (transformCapacity < static_cast<int>(state.bodies.size()))
	{
		return 2;
	}
	for (std::size_t index = 0; index < state.bodies.size(); ++index)
	{
		physx::PxTransform transform = state.bodies[index]->getGlobalPose();
		transforms[index] = {transform.p.x, transform.p.y, transform.p.z, transform.q.x,
		                     transform.q.y, transform.q.z, transform.q.w};
	}
	return 0;
}

std::uint64_t CountPhysXContainerPileInvalidTransforms(const PhysXCaseState& state)
{
	std::uint64_t invalidTransformCount = 0;
	for (std::size_t index = 0; index < state.bodies.size(); ++index)
		if (state.bodies[index]->getGlobalPose().isFinite() == false)
			invalidTransformCount += 1;
	return invalidTransformCount;
}

int RunPhysXContainerPileHeadless(const PhysXRunRequest& request)
{
	const PhysXCaseConfig config = {&request.caseExecution, request.threadCount, request.repeatIndex, request.stepCount,
	                                request.warmupSteps};
	PhysXCaseState state = {};
	if (CreatePhysXCaseState(config, &state) != 0)
	{
		std::cerr << "run_failed reason=create_fixture\n";
		return 2;
	}
	benchmark_stack::Capture capture = {};
	const PhysXCaseDescriptor* descriptor = nullptr;
	int status = ResolvePhysXCase(request.caseExecution, &descriptor);
	if (status == 0) status = request.verificationMode == VerificationMode_On ? benchmark_stack::OpenCapture(request.stackStream, request.caseExecution,
	    kEngineId, request.threadCount, request.repeatIndex, &capture) : 0;
	if (status == 0) status = RunPhysXCaseWarmup(&state, request.warmupSteps, request.verificationMode == VerificationMode_On ? &capture : nullptr);
	capture.segment = 1;
	std::vector<benchmark_visual::VisualStableTransform> transforms(request.verificationMode == VerificationMode_On ? request.caseExecution.dynamicBodyCount : 0);
	const PhysXCaseView recordingState = {&state};
	if (request.verificationMode == VerificationMode_On)
	{
		benchmark_stack::BeginFrame(&capture);
		if (status == 0) status = descriptor->sampleTransforms(recordingState, transforms.data(), static_cast<int>(transforms.size()));
		if (status == 0) status = benchmark_stack::AppendTransforms(&capture, benchmark_stack::Phase_Construction, capture.segment, 0, transforms.data());
	}
	PhysXCaseView measuredState = {&state};
	if (status == 0) status = RecordPhysXCase(request, &measuredState, request.verificationMode == VerificationMode_On ? &capture : nullptr);
	const int closed = request.verificationMode == VerificationMode_On ? benchmark_stack::CloseCapture(&capture) : 0;
	if (status == 0) status = closed;
	if (status == 0)
	{
		const std::uint64_t invalidTransformCount = CountPhysXContainerPileInvalidTransforms(state);
		const int caseValid = invalidTransformCount == 0;
		const int metricValid = state.completedStepCount == request.stepCount && state.physicsElapsedMs > 0.0 &&
		                        std::isfinite(state.physicsElapsedMs) != 0 &&
		                        state.rawStepDurations.size() == static_cast<std::size_t>(request.stepCount);
		std::array<char, 256> physicsSettings = {};
		FormatPhysXContainerPilePhysicsSettings(request.caseExecution, request.threadCount, physicsSettings.data(),
		                                        physicsSettings.size());
		const PhysXResult result = {request.caseExecution.fixtureSemantic,
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
		                            state.rawStepDurations.data()};
		if (status == 0)
			status = WritePhysXResult(request, result);
	}
	DestroyPhysXCaseState(&state);
	return status;
}

int FormatPhysXContainerPilePhysicsSettings(const CaseExecutionSpec& execution, int threadCount, char* settings,
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

namespace
{
int StepContainerPileVisual(PhysXCaseView* state, int count)
{
	return state == nullptr || state->value == nullptr
	           ? 2
			   : StepPhysXCase(static_cast<PhysXCaseState*>(state->value), count);
}

int BuildContainerPileVisualScene(const PhysXCaseView& state, benchmark_visual::VisualGeometry* geometries,
                                  benchmark_visual::VisualMeshStorage* meshes, int geometryCapacity,
                                  benchmark_visual::VisualInstance* instances, int instanceCapacity, int* geometryCount,
                                  int* instanceCount)
{
	if (state.value == nullptr || geometries == nullptr || instances == nullptr || geometryCount == nullptr ||
	    instanceCount == nullptr)
		return 2;
	const PhysXCaseState& value = *static_cast<const PhysXCaseState*>(state.value);
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
	const int dynamicBodyCount = static_cast<int>(value.bodies.size());
	for (int index = 0; index < dynamicBodyCount; ++index)
	{
		const physx::PxTransform transform = value.bodies[index]->getGlobalPose();
		instances[index] = {};
		instances[index].geometryIndex = 0;
		instances[index].stableSlot = static_cast<std::uint32_t>(index);
		instances[index].transformSlot = static_cast<std::uint32_t>(index);
		instances[index].initialTransform = {transform.p.x, transform.p.y, transform.p.z, transform.q.x,
		                                     transform.q.y, transform.q.z, transform.q.w};
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

int SampleContainerPileVisualTransforms(const PhysXCaseView& state, benchmark_visual::VisualStableTransform* transforms,
                                        int capacity)
{
	if (state.value == nullptr || transforms == nullptr)
		return 2;
	const PhysXCaseState& value = *static_cast<const PhysXCaseState*>(state.value);
	if (capacity < static_cast<int>(value.bodies.size()))
		return 2;
	for (std::size_t index = 0; index < value.bodies.size(); ++index)
	{
		const physx::PxTransform transform = value.bodies[index]->getGlobalPose();
		transforms[index] = {
		    static_cast<std::uint32_t>(index),
		    {transform.p.x, transform.p.y, transform.p.z, transform.q.x, transform.q.y, transform.q.z, transform.q.w}};
	}
	return 0;
}

}

const PhysXCaseDescriptor& PhysXContainerPileCaseDescriptor()
{
	static const PhysXCaseDescriptor descriptor = {
	    kEngineId,
	    RunPhysXContainerPileHeadless,
	    StepContainerPileVisual,
	    BuildContainerPileVisualScene,
	    SampleContainerPileVisualTransforms,
	    nullptr,
	};
	return descriptor;
}
}
