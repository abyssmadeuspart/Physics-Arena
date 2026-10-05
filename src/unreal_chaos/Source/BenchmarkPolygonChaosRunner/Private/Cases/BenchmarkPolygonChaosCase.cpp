#include "Cases/BenchmarkPolygonChaosCase.h"

#include "Output/BenchmarkPolygonChaosOutput.h"
#include "Runtime/BenchmarkPolygonChaosUnits.h"

#include "Chaos/Box.h"
#include "Chaos/CollisionFilterData.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/ShapeInstance.h"
#include "HAL/PlatformTime.h"

#include <cmath>
#include <cstdio>

namespace BenchmarkPolygonChaos
{

ChaosCaseState::ChaosCaseState(const CaseExecutionSpec& InCaseExecution, const ThreadRuntimeState& RuntimeState)
    : CaseExecution(InCaseExecution), Particles(UniqueIndices),
      Evolution(Particles, PhysicalMaterials, nullptr, nullptr, nullptr, nullptr,
	            ThreadExecutionModeForThreadCount(RuntimeState.RequestedThreadCount) ==
	                ThreadExecutionMode::SingleThreaded)
{
	DynamicBodies.Reserve(static_cast<int>(CaseExecution.dynamicBodyCount));
}

void SetShapeFilterToCollide(Chaos::FPerShapeData& Shape)
{
	const Chaos::Filter::FShapeFilterData ShapeFilterData =
	    Chaos::Filter::FShapeFilterBuilder::BuildBlockAll(Chaos::EFilterFlags::SimpleCollision);
	Shape.SetShapeFilterData(ShapeFilterData);
}

void SetParticleShapesToCollide(Chaos::FGeometryParticleHandle* Particle)
{
	for (const TUniquePtr<Chaos::FPerShapeData>& Shape : Particle->ShapesArray())
	{
		SetShapeFilterToCollide(*Shape);
	}
}

void InitEvolutionSettings(Chaos::FPBDRigidsEvolutionGBF& Evolution, const CaseExecutionSpec& CaseExecution)
{
	Evolution.SetNumPositionIterations(
	    static_cast<int32>(CaseExecution.nativeSolver.values[CaseSolverField_PositionIterations]));
	Evolution.SetNumVelocityIterations(
	    static_cast<int32>(CaseExecution.nativeSolver.values[CaseSolverField_VelocityIterations]));
	Evolution.SetNumProjectionIterations(
	    static_cast<int32>(CaseExecution.nativeSolver.values[CaseSolverField_ProjectionIterations]));

	Chaos::FCollisionDetectorSettings DetectorSettings = Evolution.GetCollisionConstraints().GetDetectorSettings();
	DetectorSettings.BoundsExpansion = 3.0f;
	DetectorSettings.bDeferNarrowPhase = false;
	DetectorSettings.bAllowManifolds = true;
	Evolution.GetCollisionConstraints().SetDetectorSettings(DetectorSettings);
	Evolution.GetGravityForces().SetAcceleration(
	    MetersToChaosUnits(Chaos::FVec3(CaseExecution.gravity.x, CaseExecution.gravity.y, CaseExecution.gravity.z)), 0);
}

void SetParticleBounds(Chaos::FGeometryParticleHandle* Particle, const Chaos::FVec3& HalfExtents)
{
	Particle->SetLocalBounds(Chaos::FAABB3(-HalfExtents, HalfExtents));
	Particle->UpdateWorldSpaceState(Chaos::FRigidTransform3(Particle->GetX(), Particle->GetR()), Chaos::FVec3(0));
	Particle->SetHasBounds(true);
}

void AddStaticBox(ChaosCaseState* State, const CaseExecutionBox& Box)
{
	Chaos::FGeometryParticleHandle* Particle = State->Evolution.CreateStaticParticles(1)[0];
	const Chaos::FVec3 HalfExtents =
	    MetersToChaosUnits(Chaos::FVec3(Box.halfExtents.x, Box.halfExtents.y, Box.halfExtents.z));
	Particle->SetGeometry(Chaos::FImplicitObjectPtr(new Chaos::TBox<Chaos::FReal, 3>(-HalfExtents, HalfExtents)));
	Particle->SetX(MetersToChaosUnits(Chaos::FVec3(Box.center.x, Box.center.y, Box.center.z)));
	Particle->SetR(Chaos::FRotation3::FromIdentity());
	SetParticleBounds(Particle, HalfExtents);
	SetParticleShapesToCollide(Particle);
	State->Evolution.EnableParticle(Particle);
}

void AddDynamicBody(ChaosCaseState* State, const Chaos::FVec3& Position, const ChaosResolvedShape& Shape)
{
	Chaos::FPBDRigidParticleHandle* Particle = State->Evolution.CreateDynamicParticles(1)[0];
	const CaseExecutionOpenContainer& Fixture = State->CaseExecution.openContainer;
	const Chaos::FVec3 ChaosPosition = MetersToChaosUnits(Position);
	Particle->SetGeometry(Shape.Geometry);
	Particle->SetX(ChaosPosition);
	Particle->SetP(ChaosPosition);
	Particle->SetR(Shape.BodyRotation);
	Particle->SetQ(Shape.BodyRotation);
	Particle->SetV(Chaos::FVec3(0));
	Particle->SetW(Chaos::FVec3(0));
	SetChaosResolvedMass(Particle, Shape, Fixture.density * Shape.Volume);
	Particle->SetGravityEnabled(true);
	Particle->SetSleepType(State->CaseExecution.sleepMode == CaseExecutionToggle_Disabled
	                           ? Chaos::ESleepType::NeverSleep
							   : Chaos::ESleepType::MaterialSleep);
	Particle->SetCCDEnabled(State->CaseExecution.continuousCollisionMode == CaseExecutionToggle_Enabled);
	State->Evolution.SetPhysicsMaterial(Particle, MakeSerializable(State->Material));
	SetParticleBounds(Particle, Shape.HalfExtents);
	SetParticleShapesToCollide(Particle);
	State->Evolution.EnableParticle(Particle);
	State->DynamicBodies.Add(Particle);
}

int CreateChaosCaseState(ChaosCaseState* State)
{
	const CaseExecutionSpec& CaseExecution = State->CaseExecution;
	const CaseExecutionOpenContainer& Fixture = CaseExecution.openContainer;
	ChaosResolvedShape Shape;
	if (CreateChaosResolvedShape(CaseExecution, CaseExecution.selectedGeometry, &Shape) != 0)
		return 2;
	InitEvolutionSettings(State->Evolution, CaseExecution);
	State->Material = MakeUnique<Chaos::FChaosPhysicsMaterial>();
	State->Material->Friction = CaseExecution.friction;
	State->Material->Restitution = CaseExecution.restitution;

	for (uint16 Index = 0; Index < Fixture.staticBoxCount; ++Index)
	{
		AddStaticBox(State, Fixture.staticBoxes[Index]);
	}

	const double OriginX = -0.5 * static_cast<double>(Fixture.dynamicGrid[0] - 1) * Fixture.dynamicSpacing.x;
	const double OriginZ = -0.5 * static_cast<double>(Fixture.dynamicGrid[2] - 1) * Fixture.dynamicSpacing.z;
	for (uint32 Y = 0; Y < Fixture.dynamicGrid[1]; ++Y)
	{
		for (uint32 Z = 0; Z < Fixture.dynamicGrid[2]; ++Z)
		{
			for (uint32 X = 0; X < Fixture.dynamicGrid[0]; ++X)
			{
				AddDynamicBody(State,
				               Chaos::FVec3(OriginX + static_cast<double>(X) * Fixture.dynamicSpacing.x,
				                            Fixture.dynamicInitialY + static_cast<double>(Y) * Fixture.dynamicSpacing.y,
				                            OriginZ + static_cast<double>(Z) * Fixture.dynamicSpacing.z),
				               Shape);
			}
		}
	}

	return State->DynamicBodies.Num() == static_cast<int>(CaseExecution.dynamicBodyCount) ? 0 : 2;
}

void StepChaosCaseUntimed(ChaosCaseState* State, int StepCount)
{
	for (int Step = 0; Step < StepCount; ++Step)
	{
		const double Timestep = 1.0 / static_cast<double>(State->CaseExecution.timestepHz);
		State->Evolution.AdvanceOneTimeStep(Timestep);
		State->Evolution.EndFrame(Timestep);
		State->CompletedStepCount += 1;
	}
}

int StepChaosCaseTimed(ChaosCaseState* State, int StepCount)
{
	if (StepCount < 0 || StepCount > State->RawStepCycles.Num() - State->CompletedStepCount)
	{
		return 2;
	}
	const int FirstStep = State->CompletedStepCount;
	for (int Step = 0; Step < StepCount; ++Step)
	{
		const double Timestep = 1.0 / static_cast<double>(State->CaseExecution.timestepHz);
		const uint64 Start = FPlatformTime::Cycles64();
		State->Evolution.AdvanceOneTimeStep(Timestep);
		State->Evolution.EndFrame(Timestep);
		const uint64 End = FPlatformTime::Cycles64();
		State->RawStepCycles[FirstStep + Step] = End - Start;
	}
	for (int Step = 0; Step < StepCount; ++Step)
	{
		const double Milliseconds = FPlatformTime::ToMilliseconds64(State->RawStepCycles[FirstStep + Step]);
		State->PhysicsElapsedMs += Milliseconds;
		State->LatestPhysicsStepMs = Milliseconds;
	}
	State->CompletedStepCount += StepCount;
	return 0;
}

int RunWarmup(const RunnerArgs& Args, const ThreadRuntimeState& RuntimeState,
              benchmark_stack::Capture* Capture, ChaosVisualStableTransform* Transforms)
{
	if (Args.WarmupSteps <= 0)
	{
		return 0;
	}

	ChaosCaseState WarmupState(Args.CaseExecution, RuntimeState);
	if (CreateChaosCaseState(&WarmupState) != 0)
	{
		return 2;
	}

	const ChaosCaseView View = {&WarmupState};
	for (int Step = 0; Step <= Args.WarmupSteps; ++Step)
	{
		if (Step != 0) StepChaosCaseUntimed(&WarmupState, 1);
		if (Args.Verification == VerificationMode_On && CaptureChaosFrame(Args, View, Transforms, Capture,
		    Step == 0 ? benchmark_stack::Phase_Construction : benchmark_stack::Phase_Warmup, Step) != 0) return 2;
	}
	return 0;
}

int RunHeadless(const RunnerArgs& Args, const ThreadRuntimeState& RuntimeState)
{
	ChaosCaseState State(Args.CaseExecution, RuntimeState);
	ChaosCaseView RecordingState = {&State};
	State.RawStepCycles.SetNumUninitialized(Args.StepCount);
	benchmark_stack::Capture Capture = {};
	std::vector<ChaosVisualStableTransform> Transforms(Args.Verification == VerificationMode_On ? Args.CaseExecution.dynamicBodyCount : 0);
	int Status = Args.Verification == VerificationMode_On ? benchmark_stack::OpenCapture(TCHAR_TO_UTF8(Args.StackStream), Args.CaseExecution,
	    kEngineId, Args.ThreadCount, Args.RepeatIndex, &Capture) : 0;
	if (Status == 0) Status = RunWarmup(Args, RuntimeState, Args.Verification == VerificationMode_On ? &Capture : nullptr, Transforms.data());
	Capture.segment = Args.WarmupSteps > 0 ? 1 : 0;
	if (Status == 0) Status = CreateChaosCaseState(&State);
	if (Status == 0 && Args.Verification == VerificationMode_On) Status = CaptureChaosFrame(Args, RecordingState, Transforms.data(), &Capture, benchmark_stack::Phase_Construction, 0);
	if (Status == 0) Status = RecordChaosCase(Args, &RecordingState, Args.Verification == VerificationMode_On ? &Capture : nullptr);
	const int Closed = Args.Verification == VerificationMode_On ? benchmark_stack::CloseCapture(&Capture) : 0;
	if (Status != 0 || Closed != 0) return 2;
	const uint64 InvalidTransformCount = CountContainerInvalidTransforms(State);
	const bool CaseValid = RuntimeState.WorkerStatus == ThreadWorkerStatus::Ok && InvalidTransformCount == 0;
	const bool MetricValid = State.CompletedStepCount == Args.StepCount && State.PhysicsElapsedMs > 0.0 &&
	                         std::isfinite(State.PhysicsElapsedMs) && State.RawStepCycles.Num() == Args.StepCount;
	char Settings[256] = {};
	if (FormatContainerVisualPhysicsSettings(Args.CaseExecution, RuntimeState, Settings, sizeof(Settings)) != 0)
	{
		return 2;
	}
	const ChaosResult Result = {
	    Args.CaseExecution.fixtureSemantic,
	    Args.CaseExecution.fixtureRevision,
	    Settings,
	    static_cast<int>(Args.CaseExecution.bodyCount),
	    static_cast<int>(Args.CaseExecution.shapeCount),
	    static_cast<int>(Args.CaseExecution.queryCount),
	    static_cast<int>(Args.CaseExecution.constraintCount),
	    InvalidTransformCount,
	    CaseValid ? "ok" : "invalid_result",
	    CaseValid && MetricValid ? "ok" : "invalid_result",
	    Args.ThreadCount,
	    RuntimeState.EffectiveChaosWorkerCount,
	    State.CompletedStepCount,
	    State.PhysicsElapsedMs,
	    &State.RawStepCycles,
	};
	return WriteChaosResult(Args, Result);
}

uint64 CountContainerInvalidTransforms(const ChaosCaseState& State)
{
	uint64 InvalidTransformCount = 0;
	for (Chaos::FPBDRigidParticleHandle* Particle : State.DynamicBodies)
	{
		const Chaos::FVec3 Position = Particle->GetX();
		const Chaos::FRotation3 Rotation = Particle->GetR();
		if (!std::isfinite(Position.X) || !std::isfinite(Position.Y) || !std::isfinite(Position.Z) ||
		    !std::isfinite(Rotation.X) || !std::isfinite(Rotation.Y) || !std::isfinite(Rotation.Z) ||
		    !std::isfinite(Rotation.W))
		{
			InvalidTransformCount += 1;
		}
	}
	return InvalidTransformCount;
}

int SampleContainerVisualTransforms(const ChaosCaseView& State, ChaosVisualStableTransform* Transforms, int Capacity)
{
	if (State.Handle == nullptr || Transforms == nullptr)
	{
		return 2;
	}
	const ChaosCaseState* CaseState = static_cast<const ChaosCaseState*>(State.Handle);
	if (Capacity < CaseState->DynamicBodies.Num() ||
	    CaseState->DynamicBodies.Num() != static_cast<int>(CaseState->CaseExecution.dynamicBodyCount))
	{
		return 2;
	}
	for (int Index = 0; Index < CaseState->DynamicBodies.Num(); ++Index)
	{
		const Chaos::FVec3 Position = ChaosUnitsToMeters(CaseState->DynamicBodies[Index]->GetX());
		const Chaos::FRotation3 Rotation = CaseState->DynamicBodies[Index]->GetR();
		Transforms[Index] = {
		    static_cast<uint32>(Index),
		    {
		        static_cast<float>(Position.X),
		        static_cast<float>(Position.Y),
		        static_cast<float>(Position.Z),
		        static_cast<float>(Rotation.X),
		        static_cast<float>(Rotation.Y),
		        static_cast<float>(Rotation.Z),
		        static_cast<float>(Rotation.W),
		    },
		};
	}
	return 0;
}

int BuildContainerVisualScene(const ChaosCaseView& State, ChaosVisualGeometry* Geometries,

                              benchmark_visual::VisualMeshStorage* Meshes, int GeometryCapacity,
                              ChaosVisualInstance* Instances, int InstanceCapacity, int* GeometryCount,
                              int* InstanceCount)
{
	if (State.Handle == nullptr || Geometries == nullptr || Instances == nullptr || GeometryCount == nullptr ||
	    InstanceCount == nullptr)
	{
		return 2;
	}
	const ChaosCaseState* CaseState = static_cast<const ChaosCaseState*>(State.Handle);
	const CaseExecutionSpec& CaseExecution = CaseState->CaseExecution;
	const CaseExecutionOpenContainer& Fixture = CaseExecution.openContainer;
	if (GeometryCapacity < 1 + Fixture.staticBoxCount || InstanceCapacity < static_cast<int>(CaseExecution.bodyCount))
	{
		return 2;
	}
	if (BuildResolvedVisualGeometry(CaseExecution, CaseExecution.selectedGeometry, Meshes, &Geometries[0]) != 0)
		return 2;
	for (uint16 Index = 0; Index < Fixture.staticBoxCount; ++Index)
	{
		const CaseExecutionVector3& HalfExtents = Fixture.staticBoxes[Index].halfExtents;
		Geometries[Index + 1] = {2u, HalfExtents.x, HalfExtents.y, HalfExtents.z};
	}
	TArray<ChaosVisualStableTransform> Transforms;
	Transforms.SetNumUninitialized(CaseState->DynamicBodies.Num());
	if (SampleContainerVisualTransforms(State, Transforms.GetData(), Transforms.Num()) != 0)
	{
		return 2;
	}
	for (int Index = 0; Index < CaseState->DynamicBodies.Num(); ++Index)
	{
		Instances[Index] = {
		    0u,
		    static_cast<uint32>(Index),
		    static_cast<uint32>(Index),
		    Transforms[Index].Transform,
		};
	}
	const int DynamicBodyCount = CaseState->DynamicBodies.Num();
	for (uint16 Index = 0; Index < Fixture.staticBoxCount; ++Index)
	{
		const CaseExecutionBox& Box = Fixture.staticBoxes[Index];
		Instances[DynamicBodyCount + Index] = {
		    static_cast<uint32>(Index + 1),
		    static_cast<uint32>(DynamicBodyCount + Index),
		    MAX_uint32,
		    {
		        Box.center.x,
		        Box.center.y,
		        Box.center.z,
		        0.0f,
		        0.0f,
		        0.0f,
		        1.0f,
		    },
		};
	}
	*GeometryCount = 1 + Fixture.staticBoxCount;
	*InstanceCount = static_cast<int>(CaseExecution.bodyCount);
	return 0;
}

int FormatContainerVisualPhysicsSettings(const CaseExecutionSpec& execution, const ThreadRuntimeState& RuntimeState,
                                         char* Settings, int Capacity)
{
	if (Settings == nullptr || Capacity <= 0)
	{
		return 2;
	}
	const int Size = std::snprintf(
	    Settings, Capacity,
	    "position_iterations=%u; velocity_iterations=%u; projection_iterations=%u; sleep=%s; ccd=%s; unit_scale=100_chaos_units_per_meter; worker_count=%d",
	    execution.nativeSolver.values[CaseSolverField_PositionIterations],
	    execution.nativeSolver.values[CaseSolverField_VelocityIterations],
	    execution.nativeSolver.values[CaseSolverField_ProjectionIterations],
	    execution.sleepMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.continuousCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    RuntimeState.EffectiveChaosWorkerCount);
	return Size > 0 && Size < Capacity ? 0 : 2;
}

int StepContainerVisual(ChaosCaseView* State, int WorkUnitCount)
{
	return State == nullptr || State->Handle == nullptr
	           ? 2
			   : StepChaosCaseTimed(static_cast<ChaosCaseState*>(State->Handle), WorkUnitCount);
}

const ChaosCaseDescriptor& ContainerPileDescriptor()
{
	static const ChaosCaseDescriptor Descriptor = {
	    kEngineId, RunHeadless, StepContainerVisual, BuildContainerVisualScene, SampleContainerVisualTransforms,
	    nullptr,
	};
	return Descriptor;
}

} // namespace BenchmarkPolygonChaos
