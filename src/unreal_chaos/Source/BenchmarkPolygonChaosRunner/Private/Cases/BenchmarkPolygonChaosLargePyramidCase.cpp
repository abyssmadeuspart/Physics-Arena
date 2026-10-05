#include "Cases/BenchmarkPolygonChaosLargePyramidCase.h"

#include "Output/BenchmarkPolygonChaosOutput.h"
#include "Runtime/BenchmarkPolygonChaosUnits.h"

#include "Chaos/Box.h"
#include "Chaos/CollisionFilterData.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/ShapeInstance.h"
#include "Chaos/Sphere.h"
#include "HAL/PlatformTime.h"

#include <cmath>
#include <cstdio>

namespace BenchmarkPolygonChaos
{
ChaosLargePyramidState::ChaosLargePyramidState(const CaseExecutionSpec& InCaseExecution,
                                               const ThreadRuntimeState& RuntimeState)
    : CaseExecution(InCaseExecution), Particles(UniqueIndices),
      Evolution(Particles, PhysicalMaterials, nullptr, nullptr, nullptr, nullptr,
	            ThreadExecutionModeForThreadCount(RuntimeState.RequestedThreadCount) ==
	                ThreadExecutionMode::SingleThreaded)
{
	DynamicBodies.SetNumUninitialized(static_cast<int>(CaseExecution.dynamicBodyCount));
}

void InitializeLargePyramidEvolution(ChaosLargePyramidState* State)
{
	State->Evolution.SetNumPositionIterations(
	    static_cast<int32>(State->CaseExecution.nativeSolver.values[CaseSolverField_PositionIterations]));
	State->Evolution.SetNumVelocityIterations(
	    static_cast<int32>(State->CaseExecution.nativeSolver.values[CaseSolverField_VelocityIterations]));
	State->Evolution.SetNumProjectionIterations(
	    static_cast<int32>(State->CaseExecution.nativeSolver.values[CaseSolverField_ProjectionIterations]));
	Chaos::FCollisionDetectorSettings Settings = State->Evolution.GetCollisionConstraints().GetDetectorSettings();
	Settings.BoundsExpansion = 3.0f;
	Settings.bDeferNarrowPhase = false;
	Settings.bAllowManifolds = true;
	State->Evolution.GetCollisionConstraints().SetDetectorSettings(Settings);
	State->Evolution.GetGravityForces().SetAcceleration(
	    MetersToChaosUnits(Chaos::FVec3(State->CaseExecution.gravity.x, State->CaseExecution.gravity.y,
		                                State->CaseExecution.gravity.z)),
	    0);
}

void SetLargePyramidShapeFilter(Chaos::FPerShapeData& Shape)
{
	const Chaos::Filter::FShapeFilterData Data =
	    Chaos::Filter::FShapeFilterBuilder::BuildBlockAll(Chaos::EFilterFlags::SimpleCollision);
	Shape.SetShapeFilterData(Data);
}

void SetLargePyramidParticleCollision(Chaos::FGeometryParticleHandle* Particle)
{
	for (const TUniquePtr<Chaos::FPerShapeData>& Shape : Particle->ShapesArray())
	{
		SetLargePyramidShapeFilter(*Shape);
	}
}

void SetLargePyramidParticleBounds(Chaos::FGeometryParticleHandle* Particle, const Chaos::FVec3& HalfExtents)
{
	Particle->SetLocalBounds(Chaos::FAABB3(-HalfExtents, HalfExtents));
	Particle->UpdateWorldSpaceState(Chaos::FRigidTransform3(Particle->GetX(), Particle->GetR()), Chaos::FVec3(0));
	Particle->SetHasBounds(true);
}

void ConfigureLargePyramidDynamicParticle(ChaosLargePyramidState* State, Chaos::FPBDRigidParticleHandle* Particle,
                                          const Chaos::FVec3& Position, const ChaosResolvedShape& Shape, double Mass,
                                          int BodyIndex)
{
	const Chaos::FVec3 ChaosPosition = MetersToChaosUnits(Position);
	Particle->SetX(ChaosPosition);
	Particle->SetP(ChaosPosition);
	Particle->SetGeometry(Shape.Geometry);
	Particle->SetR(Shape.BodyRotation);
	Particle->SetQ(Shape.BodyRotation);
	Particle->SetV(Chaos::FVec3(0));
	Particle->SetW(Chaos::FVec3(0));
	SetChaosResolvedMass(Particle, Shape, Mass);
	Particle->SetGravityEnabled(true);
	Particle->SetSleepType(State->CaseExecution.sleepMode == CaseExecutionToggle_Disabled
	                           ? Chaos::ESleepType::NeverSleep
							   : Chaos::ESleepType::MaterialSleep);
	Particle->SetCCDEnabled(State->CaseExecution.continuousCollisionMode == CaseExecutionToggle_Enabled);
	State->Evolution.SetPhysicsMaterial(Particle, MakeSerializable(State->Material));
	SetLargePyramidParticleBounds(Particle, Shape.HalfExtents);
	SetLargePyramidParticleCollision(Particle);
	State->Evolution.EnableParticle(Particle);
	State->DynamicBodies[BodyIndex] = Particle;
	State->CreatedDynamicBodyCount += 1;
}

int AddLargePyramidBox(ChaosLargePyramidState* State, const Chaos::FVec3& Position, int BodyIndex,
                       const ChaosResolvedShape& Shape)
{
	const CaseExecutionLargePyramid& Fixture = State->CaseExecution.largePyramid;
	Chaos::FPBDRigidParticleHandle* Particle = State->Evolution.CreateDynamicParticles(1)[0];
	const double Mass = Fixture.boxDensity * Shape.Volume;
	ConfigureLargePyramidDynamicParticle(State, Particle, Position, Shape, Mass, BodyIndex);
	return 0;
}

int AddLargePyramidProjectile(ChaosLargePyramidState* State, const Chaos::FVec3& Position, int BodyIndex)
{
	const CaseExecutionLargePyramid& Fixture = State->CaseExecution.largePyramid;
	Chaos::FPBDRigidParticleHandle* Particle = State->Evolution.CreateDynamicParticles(1)[0];
	const CaseExecutionGeometry Geometry = {
	    CaseExecutionShape_Sphere, {}, Fixture.projectileRadius, 0.0f, CaseExecutionAxis_Y};
	ChaosResolvedShape Shape;
	if (CreateChaosResolvedShape(State->CaseExecution, Geometry, &Shape) != 0)
		return 2;
	const double Mass = Fixture.projectileDensity * 4.0 / 3.0 * PI * Fixture.projectileRadius *
	                    Fixture.projectileRadius * Fixture.projectileRadius;
	ConfigureLargePyramidDynamicParticle(State, Particle, Position, Shape, Mass, BodyIndex);
	return 0;
}

int AddLargePyramidFloor(ChaosLargePyramidState* State)
{
	const CaseExecutionLargePyramid& Fixture = State->CaseExecution.largePyramid;
	Chaos::FGeometryParticleHandle* Particle = State->Evolution.CreateStaticParticles(1)[0];
	const Chaos::FVec3 HalfExtents = MetersToChaosUnits(
	    Chaos::FVec3(Fixture.floorHalfExtents.x, Fixture.floorHalfExtents.y, Fixture.floorHalfExtents.z));
	Particle->SetGeometry(Chaos::FImplicitObjectPtr(new Chaos::TBox<Chaos::FReal, 3>(-HalfExtents, HalfExtents)));
	Particle->SetX(MetersToChaosUnits(Chaos::FVec3(0.0, -Fixture.floorHalfExtents.y, 0.0)));
	Particle->SetR(Chaos::FRotation3::FromIdentity());
	SetLargePyramidParticleBounds(Particle, HalfExtents);
	SetLargePyramidParticleCollision(Particle);
	State->Evolution.EnableParticle(Particle);
	State->CreatedStaticBodyCount += 1;
	return 0;
}

int CreateLargePyramidState(ChaosLargePyramidState* State)
{
	if (State == nullptr || State->CaseExecution.fixtureKind != CaseFixtureKind_LargePyramid)
	{
		return 2;
	}
	InitializeLargePyramidEvolution(State);
	State->Material = MakeUnique<Chaos::FChaosPhysicsMaterial>();
	State->Material->Friction = State->CaseExecution.friction;
	State->Material->Restitution = State->CaseExecution.restitution;
	const CaseExecutionLargePyramid& Fixture = State->CaseExecution.largePyramid;
	int BodyIndex = 0;
	ChaosResolvedShape Shape;
	if (CreateChaosResolvedShape(State->CaseExecution, State->CaseExecution.selectedGeometry, &Shape) != 0)
		return 2;
	for (uint32 Layer = 0; Layer < Fixture.rowCount; ++Layer)
	{
		const uint32 LayerSide = Fixture.rowCount - Layer;
		for (uint32 Depth = 0; Depth < LayerSide; ++Depth)
		{
			for (uint32 Column = 0; Column < LayerSide; ++Column)
			{
				const Chaos::FVec3 Position(
				    Fixture.baseCenter.x +
				        (static_cast<double>(Column) - 0.5 * static_cast<double>(LayerSide - 1)) * Fixture.boxSpacing.x,
				    Fixture.baseCenter.y + static_cast<double>(Layer) * Fixture.boxSpacing.y,
				    Fixture.baseCenter.z +
				        (static_cast<double>(Depth) - 0.5 * static_cast<double>(LayerSide - 1)) * Fixture.boxSpacing.z);
				if (AddLargePyramidBox(State, Position, BodyIndex++, Shape) != 0)
					return 2;
			}
		}
	}
	for (uint32 ProjectileIndex = 0; ProjectileIndex < Fixture.projectileCount; ++ProjectileIndex)
	{
		const Chaos::FVec3 Position(Fixture.projectileInitialCenter.x +
		                                static_cast<double>(ProjectileIndex) * Fixture.projectileCenterSpacing.x,
		                            Fixture.projectileInitialCenter.y +
		                                static_cast<double>(ProjectileIndex) * Fixture.projectileCenterSpacing.y,
		                            Fixture.projectileInitialCenter.z +
		                                static_cast<double>(ProjectileIndex) * Fixture.projectileCenterSpacing.z);
		if (AddLargePyramidProjectile(State, Position, BodyIndex++) != 0)
			return 2;
	}
	if (AddLargePyramidFloor(State) != 0)
	{
		return 2;
	}
	return BodyIndex == static_cast<int>(State->CaseExecution.dynamicBodyCount) &&
	               State->CreatedDynamicBodyCount == BodyIndex &&
	               State->CreatedStaticBodyCount == static_cast<int>(State->CaseExecution.staticBodyCount)
	           ? 0
			   : 2;
}

int WarmupLargePyramidState(ChaosLargePyramidState* State, int WorkUnitCount)
{
	if (State == nullptr || WorkUnitCount < 0)
		return 2;
	const double Timestep = 1.0 / static_cast<double>(State->CaseExecution.timestepHz);
	for (int Step = 0; Step < WorkUnitCount; ++Step)
	{
		State->Evolution.AdvanceOneTimeStep(Timestep);
		State->Evolution.EndFrame(Timestep);
	}
	return 0;
}

int StepLargePyramidStateTimed(ChaosLargePyramidState* State, int WorkUnitCount)
{
	if (State == nullptr || WorkUnitCount < 0 || WorkUnitCount > State->RawStepCycles.Num() - State->CompletedStepCount)
	{
		return 2;
	}
	const int FirstStep = State->CompletedStepCount;
	const CaseExecutionLargePyramid& Fixture = State->CaseExecution.largePyramid;
	const double Timestep = 1.0 / static_cast<double>(State->CaseExecution.timestepHz);
	for (int Step = 0; Step < WorkUnitCount; ++Step)
	{
		if (FirstStep + Step == static_cast<int>(Fixture.projectileLaunchAfterWorkUnits))
		{
			const int FirstProjectile = State->DynamicBodies.Num() - static_cast<int>(Fixture.projectileCount);
			for (int ProjectileIndex = FirstProjectile; ProjectileIndex < State->DynamicBodies.Num(); ++ProjectileIndex)
			{
				Chaos::FPBDRigidParticleHandle* Projectile = State->DynamicBodies[ProjectileIndex];
				Projectile->SetV(MetersToChaosUnits(Chaos::FVec3(Fixture.projectileLaunchVelocity.x,
				                                                 Fixture.projectileLaunchVelocity.y,
				                                                 Fixture.projectileLaunchVelocity.z)));
				State->Evolution.SetParticleObjectState(Projectile, Chaos::EObjectStateType::Dynamic);
				State->Evolution.WakeParticle(Projectile);
			}
		}
		const uint64 Start = FPlatformTime::Cycles64();
		State->Evolution.AdvanceOneTimeStep(Timestep);
		State->Evolution.EndFrame(Timestep);
		State->RawStepCycles[FirstStep + Step] = FPlatformTime::Cycles64() - Start;
	}
	for (int Step = 0; Step < WorkUnitCount; ++Step)
	{
		const double Milliseconds = FPlatformTime::ToMilliseconds64(State->RawStepCycles[FirstStep + Step]);
		if (!std::isfinite(Milliseconds) || Milliseconds <= 0.0)
			return 2;
		State->PhysicsElapsedMs += Milliseconds;
		State->LatestPhysicsStepMs = Milliseconds;
	}
	State->CompletedStepCount += WorkUnitCount;
	return 0;
}

uint64 CountLargePyramidInvalidTransforms(const ChaosLargePyramidState& State)
{
	uint64 Count = 0;
	for (Chaos::FPBDRigidParticleHandle* Particle : State.DynamicBodies)
	{
		const Chaos::FVec3 Position = Particle->GetX();
		const Chaos::FRotation3 Rotation = Particle->GetR();
		if (!std::isfinite(Position.X) || !std::isfinite(Position.Y) || !std::isfinite(Position.Z) ||
		    !std::isfinite(Rotation.X) || !std::isfinite(Rotation.Y) || !std::isfinite(Rotation.Z) ||
		    !std::isfinite(Rotation.W))
			++Count;
	}
	return Count;
}

int FormatLargePyramidSettings(const CaseExecutionSpec& execution, const ThreadRuntimeState& RuntimeState,
                               char* Settings, int Capacity)
{
	if (Settings == nullptr || Capacity <= 0)
		return 2;
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

int RunLargePyramidHeadless(const RunnerArgs& Args, const ThreadRuntimeState& RuntimeState)
{
	ChaosLargePyramidState State(Args.CaseExecution, RuntimeState);
	ChaosCaseView RecordingState = {&State};
	State.RawStepCycles.SetNumUninitialized(Args.StepCount);
	if (CreateLargePyramidState(&State) != 0) return 2;
	benchmark_stack::Capture Capture = {};
	if (Args.Verification == VerificationMode_On && benchmark_stack::OpenCapture(TCHAR_TO_UTF8(Args.StackStream), Args.CaseExecution,
		Args.CaseDescriptor->EngineId, Args.ThreadCount, Args.RepeatIndex, &Capture) != 0)
		return 2;
	std::vector<ChaosVisualStableTransform> RawTransforms(Args.Verification == VerificationMode_On ? Args.CaseExecution.dynamicBodyCount : 0);
	for (int Ordinal = 0; Ordinal <= Args.WarmupSteps; ++Ordinal)
	{
		if ((Ordinal != 0 && WarmupLargePyramidState(&State, 1) != 0) ||
			(Args.Verification == VerificationMode_On && CaptureChaosFrame(Args, RecordingState, RawTransforms.data(), &Capture,
				Ordinal == 0 ? benchmark_stack::Phase_Construction : benchmark_stack::Phase_Warmup, Ordinal) != 0))
		{
			Capture.status = 2;
			benchmark_stack::CloseCapture(&Capture);
			return 2;
		}
	}
	const int CaptureStatus = RecordChaosCase(Args, &RecordingState, Args.Verification == VerificationMode_On ? &Capture : nullptr);
	if (CaptureStatus != 0) Capture.status = 2;
	if ((Args.Verification == VerificationMode_On && benchmark_stack::CloseCapture(&Capture) != 0) || CaptureStatus != 0) return 2;
	const uint64 InvalidCount = CountLargePyramidInvalidTransforms(State);
	const int CaseValid = RuntimeState.WorkerStatus == ThreadWorkerStatus::Ok && InvalidCount == 0 ? 1 : 0;
	const int MetricValid = State.CompletedStepCount == Args.StepCount && State.PhysicsElapsedMs > 0.0 &&
	                                std::isfinite(State.PhysicsElapsedMs)
	                            ? 1
	                            : 0;
	char Settings[256] = {};
	if (FormatLargePyramidSettings(Args.CaseExecution, RuntimeState, Settings, sizeof(Settings)) != 0)
	{
		return 2;
	}
	const ChaosResult Result = {
	    Args.CaseExecution.fixtureSemantic,
	    Args.CaseExecution.fixtureRevision,
	    Settings,
	    static_cast<int>(Args.CaseExecution.bodyCount),
	    static_cast<int>(Args.CaseExecution.shapeCount),
	    0,
	    0,
	    InvalidCount,
	    CaseValid != 0 ? "ok" : "invalid_result",
	    CaseValid != 0 && MetricValid != 0 ? "ok" : "invalid_result",
	    Args.ThreadCount,
	    RuntimeState.EffectiveChaosWorkerCount,
	    State.CompletedStepCount,
	    State.PhysicsElapsedMs,
	    &State.RawStepCycles,
	    nullptr,
	    0,
	};
	return WriteChaosResult(Args, Result);
}

int StepLargePyramidVisual(ChaosCaseView* State, int WorkUnitCount)
{
	return State == nullptr || State->Handle == nullptr
	           ? 2
			   : StepLargePyramidStateTimed(static_cast<ChaosLargePyramidState*>(State->Handle), WorkUnitCount);
}

ChaosVisualTransform LargePyramidVisualTransform(const Chaos::FPBDRigidParticleHandle* Particle)
{
	const Chaos::FVec3 Position = ChaosUnitsToMeters(Particle->GetX());
	const Chaos::FRotation3 Rotation = Particle->GetR();
	return {
	    static_cast<float>(Position.X), static_cast<float>(Position.Y), static_cast<float>(Position.Z),
	    static_cast<float>(Rotation.X), static_cast<float>(Rotation.Y), static_cast<float>(Rotation.Z),
	    static_cast<float>(Rotation.W),
	};
}

int BuildLargePyramidVisualScene(const ChaosCaseView& State, ChaosVisualGeometry* Geometries,

                                 benchmark_visual::VisualMeshStorage* Meshes, int GeometryCapacity,
                                 ChaosVisualInstance* Instances, int InstanceCapacity, int* GeometryCount,
                                 int* InstanceCount)
{
	if (State.Handle == nullptr || Geometries == nullptr || GeometryCapacity < 3 || Instances == nullptr ||
	    GeometryCount == nullptr || InstanceCount == nullptr)
	{
		return 2;
	}
	const ChaosLargePyramidState& Value = *static_cast<const ChaosLargePyramidState*>(State.Handle);
	const CaseExecutionSpec& Execution = Value.CaseExecution;
	const CaseExecutionLargePyramid& Fixture = Execution.largePyramid;
	if (InstanceCapacity < static_cast<int>(Execution.visualInstanceCount))
		return 2;
	if (BuildResolvedVisualGeometry(Execution, Execution.selectedGeometry, Meshes, &Geometries[0]) != 0)
		return 2;
	Geometries[1] = {1u, Fixture.projectileRadius, 0.0f, 0.0f};
	Geometries[2] = {2u, Fixture.floorHalfExtents.x, Fixture.floorHalfExtents.y, Fixture.floorHalfExtents.z};
	const int FirstProjectile = Value.DynamicBodies.Num() - static_cast<int>(Fixture.projectileCount);
	for (int Index = 0; Index < Value.DynamicBodies.Num(); ++Index)
	{
		Instances[Index] = {
		    Index >= FirstProjectile ? 1u : 0u,
		    static_cast<uint32>(Index),
		    static_cast<uint32>(Index),
		    LargePyramidVisualTransform(Value.DynamicBodies[Index]),
		};
	}
	const int FloorSlot = Value.DynamicBodies.Num();
	Instances[FloorSlot] = {
	    2u,
	    static_cast<uint32>(FloorSlot),
	    MAX_uint32,
	    {0.0f, -Fixture.floorHalfExtents.y, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
	};
	*GeometryCount = 3;
	*InstanceCount = static_cast<int>(Execution.visualInstanceCount);
	return 0;
}

int SampleLargePyramidVisualTransforms(const ChaosCaseView& State, ChaosVisualStableTransform* Transforms, int Capacity)
{
	if (State.Handle == nullptr || Transforms == nullptr)
		return 2;
	const ChaosLargePyramidState& Value = *static_cast<const ChaosLargePyramidState*>(State.Handle);
	if (Capacity < Value.DynamicBodies.Num())
		return 2;
	for (int Index = 0; Index < Value.DynamicBodies.Num(); ++Index)
	{
		Transforms[Index] = {
		    static_cast<uint32>(Index),
		    LargePyramidVisualTransform(Value.DynamicBodies[Index]),
		};
	}
	return 0;
}

const ChaosCaseDescriptor& LargePyramidDescriptor()
{
	static const ChaosCaseDescriptor Descriptor = {
	    kEngineId,
	    RunLargePyramidHeadless,
	    StepLargePyramidVisual,
	    BuildLargePyramidVisualScene,
	    SampleLargePyramidVisualTransforms,
	    nullptr,
	};
	return Descriptor;
}
} // namespace BenchmarkPolygonChaos
