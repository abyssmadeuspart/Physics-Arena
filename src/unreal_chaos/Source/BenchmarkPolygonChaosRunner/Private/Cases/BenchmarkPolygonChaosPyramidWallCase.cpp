#include "Cases/BenchmarkPolygonChaosPyramidWallCase.h"

#include "Output/BenchmarkPolygonChaosOutput.h"
#include "Runtime/BenchmarkPolygonChaosUnits.h"

#include "Chaos/Box.h"
#include "Chaos/CollisionFilterData.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/ShapeInstance.h"
#include "HAL/PlatformTime.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <array>

namespace BenchmarkPolygonChaos
{
ChaosPyramidWallState::ChaosPyramidWallState(const CaseExecutionSpec& InCaseExecution,
                                               const ThreadRuntimeState& RuntimeState)
    : CaseExecution(InCaseExecution), Particles(UniqueIndices),
      Evolution(Particles, PhysicalMaterials, nullptr, nullptr, nullptr, nullptr,
	            ThreadExecutionModeForThreadCount(RuntimeState.RequestedThreadCount) ==
	                ThreadExecutionMode::SingleThreaded)
{
}

void InitializePyramidWallEvolution(ChaosPyramidWallState* State)
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

void SetPyramidWallShapeFilter(Chaos::FPerShapeData& Shape)
{
	const Chaos::Filter::FShapeFilterData Data =
	    Chaos::Filter::FShapeFilterBuilder::BuildBlockAll(Chaos::EFilterFlags::SimpleCollision);
	Shape.SetShapeFilterData(Data);
}

void SetPyramidWallParticleCollision(Chaos::FGeometryParticleHandle* Particle)
{
	for (const TUniquePtr<Chaos::FPerShapeData>& Shape : Particle->ShapesArray())
	{
		SetPyramidWallShapeFilter(*Shape);
	}
}

void SetPyramidWallParticleBounds(Chaos::FGeometryParticleHandle* Particle, const Chaos::FVec3& HalfExtents)
{
	Particle->SetLocalBounds(Chaos::FAABB3(-HalfExtents, HalfExtents));
	Particle->UpdateWorldSpaceState(Chaos::FRigidTransform3(Particle->GetX(), Particle->GetR()), Chaos::FVec3(0));
	Particle->SetHasBounds(true);
}

void ConfigurePyramidWallDynamicParticle(ChaosPyramidWallState* State, Chaos::FPBDRigidParticleHandle* Particle,
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
	Particle->SetLinearEtherDrag(0.0);
	Particle->SetAngularEtherDrag(0.0);
	Particle->SetSleepType(State->CaseExecution.sleepMode == CaseExecutionToggle_Disabled
	                           ? Chaos::ESleepType::NeverSleep
							   : Chaos::ESleepType::MaterialSleep);
	Particle->SetCCDEnabled(State->CaseExecution.continuousCollisionMode == CaseExecutionToggle_Enabled);
	State->Evolution.SetPhysicsMaterial(Particle, MakeSerializable(State->Material));
	SetPyramidWallParticleBounds(Particle, Shape.HalfExtents);
	SetPyramidWallParticleCollision(Particle);
	State->Evolution.EnableParticle(Particle);
	State->DynamicBodies[BodyIndex] = Particle;
	State->CreatedDynamicBodyCount += 1;
}

int AddPyramidWallBox(ChaosPyramidWallState* State, const Chaos::FVec3& Position, int BodyIndex,
                       const ChaosResolvedShape& Shape)
{
	const CaseExecutionPyramidWall& Fixture = State->CaseExecution.pyramidWall;
	Chaos::FPBDRigidParticleHandle* Particle = State->Evolution.CreateDynamicParticles(1)[0];
	const double Mass = Fixture.density * Shape.Volume;
	ConfigurePyramidWallDynamicParticle(State, Particle, Position, Shape, Mass, BodyIndex);
	return 0;
}

int AddPyramidWallFloor(ChaosPyramidWallState* State)
{
	const CaseExecutionPyramidWall& Fixture = State->CaseExecution.pyramidWall;
	Chaos::FGeometryParticleHandle* Particle = State->Evolution.CreateStaticParticles(1)[0];
	const Chaos::FVec3 HalfExtents = MetersToChaosUnits(
	    Chaos::FVec3(Fixture.floorHalfExtents.x, Fixture.floorHalfExtents.y, Fixture.floorHalfExtents.z));
	Particle->SetGeometry(Chaos::FImplicitObjectPtr(new Chaos::TBox<Chaos::FReal, 3>(-HalfExtents, HalfExtents)));
	Particle->SetX(MetersToChaosUnits(Chaos::FVec3(0.0, -Fixture.floorHalfExtents.y, 0.0)));
	Particle->SetR(Chaos::FRotation3::FromIdentity());
	State->Evolution.SetPhysicsMaterial(Particle, MakeSerializable(State->Material));
	SetPyramidWallParticleBounds(Particle, HalfExtents);
	SetPyramidWallParticleCollision(Particle);
	State->Evolution.EnableParticle(Particle);
	const Chaos::TSerializablePtr<Chaos::FChaosPhysicsMaterial> Material = State->Evolution.GetPhysicsMaterial(Particle);
	if (Particle->GetX() != MetersToChaosUnits(Chaos::FVec3(0.0, -Fixture.floorHalfExtents.y, 0.0)) ||
	    !Particle->GetR().Equals(Chaos::FRotation3::FromIdentity(), 1e-6) || Particle->ShapesArray().Num() != 1 ||
	    Material->Friction != State->CaseExecution.friction || Material->StaticFriction != State->CaseExecution.friction ||
	    Material->Restitution != State->CaseExecution.restitution)
		return 2;
	State->CreatedStaticBodyCount += 1;
	return 0;
}

int CreatePyramidWallState(ChaosPyramidWallState* State)
{
	if (State == nullptr || State->CaseExecution.fixtureKind != CaseFixtureKind_PyramidWall)
	{
		return 2;
	}
	State->DynamicBodies.SetNumUninitialized(static_cast<int>(State->CaseExecution.dynamicBodyCount));
	for (uint32 Ordinal = 0; State->Verification == VerificationMode_On && Ordinal < std::min<uint32>(4, State->CaseExecution.measuredWorkUnitCount); ++Ordinal)
		State->ObservationInputs[Ordinal].resize(State->CaseExecution.dynamicBodyCount);
	InitializePyramidWallEvolution(State);
	State->Material = MakeUnique<Chaos::FChaosPhysicsMaterial>();
	State->Material->Friction = State->CaseExecution.friction;
	State->Material->StaticFriction = State->CaseExecution.friction;
	State->Material->FrictionCombineMode = Chaos::FChaosPhysicsMaterial::ECombineMode::Avg;
	State->Material->RestitutionCombineMode = Chaos::FChaosPhysicsMaterial::ECombineMode::Avg;
	State->Material->LinearEtherDrag = 0.0;
	State->Material->AngularEtherDrag = 0.0;
	State->Material->Restitution = State->CaseExecution.restitution;
	const CaseExecutionPyramidWall& Fixture = State->CaseExecution.pyramidWall;
	int BodyIndex = 0;
	ChaosResolvedShape Shape;
	if (CreateChaosResolvedShape(State->CaseExecution, State->CaseExecution.selectedGeometry, &Shape) != 0)
		return 2;
	const double ExpectedMass = 8.0 * Fixture.halfExtent * Fixture.halfExtent * Fixture.halfExtent * Fixture.density;
	const double ExpectedInertia = (2.0 / 3.0) * ExpectedMass * Fixture.halfExtent * Fixture.halfExtent;
	const double LengthSquared = kChaosUnitsPerBenchmarkMeter * kChaosUnitsPerBenchmarkMeter;
	for (uint32 Index = 0; Index < State->CaseExecution.dynamicBodyCount; ++Index)
	{
		const CaseExecutionVector3 Initial = PyramidWallPosition(Fixture, Index);
		const Chaos::FVec3 Position(Initial.x, Initial.y, Initial.z);
		if (AddPyramidWallBox(State, Position, BodyIndex++, Shape) != 0)
			return 2;
		const Chaos::FPBDRigidParticleHandle* Particle = State->DynamicBodies[Index];
		const Chaos::FVec3 NativePosition = ChaosUnitsToMeters(Particle->GetX());
		const double Mass = Particle->M();
		const Chaos::FVec3 Inertia = Chaos::FVec3(Particle->I()) / LengthSquared;
		if ((NativePosition - Position).Size() > 1e-6 || Particle->GetP() != Particle->GetX() ||
		    !Particle->GetR().Equals(Chaos::FRotation3::FromIdentity(), 1e-6) ||
		    !Particle->GetQ().Equals(Particle->GetR(), 1e-6) || Particle->GetV().SizeSquared() != 0.0 || Particle->GetW().SizeSquared() != 0.0 ||
		    !std::isfinite(Mass) || Mass <= 0.0 || std::abs(Mass - ExpectedMass) > 1e-5 * ExpectedMass ||
		    !std::isfinite(Inertia.X) || !std::isfinite(Inertia.Y) || !std::isfinite(Inertia.Z) ||
		    std::abs(Inertia.X - ExpectedInertia) > 1e-5 * ExpectedInertia ||
		    std::abs(Inertia.Y - ExpectedInertia) > 1e-5 * ExpectedInertia ||
		    std::abs(Inertia.Z - ExpectedInertia) > 1e-5 * ExpectedInertia ||
		    Particle->LinearEtherDrag() != 0.0 || Particle->AngularEtherDrag() != 0.0 || Particle->CCDEnabled() != (State->CaseExecution.continuousCollisionMode == CaseExecutionToggle_Enabled) ||
		    Particle->ObjectState() != Chaos::EObjectStateType::Dynamic ||
		    (Particle->SleepType() == Chaos::ESleepType::NeverSleep) != (State->CaseExecution.sleepMode == CaseExecutionToggle_Disabled) ||
		    Particle->ShapesArray().Num() != 1)
			return 2;
		const Chaos::TSerializablePtr<Chaos::FChaosPhysicsMaterial> Material = State->Evolution.GetPhysicsMaterial(Particle);
		if (Material->Friction != State->CaseExecution.friction || Material->StaticFriction != State->CaseExecution.friction ||
		    Material->Restitution != State->CaseExecution.restitution)
			return 2;
		if (State->Verification == VerificationMode_On) State->InitialPotentialEnergy -= Mass * (State->CaseExecution.gravity.x * NativePosition.X +
		    State->CaseExecution.gravity.y * NativePosition.Y + State->CaseExecution.gravity.z * NativePosition.Z);
	}
	if (AddPyramidWallFloor(State) != 0)
	{
		return 2;
	}
	return BodyIndex == static_cast<int>(State->CaseExecution.dynamicBodyCount) &&
	               State->CreatedDynamicBodyCount == BodyIndex &&
	               State->CreatedStaticBodyCount == static_cast<int>(State->CaseExecution.staticBodyCount) &&
	               State->Particles.GetAllParticlesView().Num() == static_cast<int>(State->CaseExecution.bodyCount) &&
	               State->CaseExecution.bodyCount == State->CaseExecution.shapeCount
	           ? 0
			   : 2;
}

int WarmupPyramidWallState(ChaosPyramidWallState* State, int WorkUnitCount)
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

void CapturePyramidWallObservation(ChaosPyramidWallState* State, int Ordinal)
{
	const uint64 Start = FPlatformTime::Cycles64();
	for (uint32 Index = 0; Index < State->CaseExecution.dynamicBodyCount; ++Index)
	{
		const Chaos::FPBDRigidParticleHandle* Particle = State->DynamicBodies[Index];
		State->ObservationInputs[Ordinal][Index] = {Particle->GetX(), Particle->GetR(), Particle->GetV(), Particle->GetW(),
			Particle->RotationOfMass(), Chaos::FVec3(Particle->I()), Particle->M(),
			static_cast<uint32>(Particle->SleepType() == Chaos::ESleepType::NeverSleep) |
			(static_cast<uint32>(Particle->ObjectState() == Chaos::EObjectStateType::Dynamic) << 1)};
	}
	State->Observations[Ordinal].elapsedMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Start);
}

void ReducePyramidWall(ChaosPyramidWallState* State, int Ordinal)
{
	const uint64 Start = FPlatformTime::Cycles64();
	PyramidWallObservation& Sample = State->Observations[Ordinal];
	for (uint32 Index = 0; Index < State->CaseExecution.dynamicBodyCount; ++Index)
	{
		const ChaosWallBodyInput& Particle = State->ObservationInputs[Ordinal][Index];
		const Chaos::FVec3 P = ChaosUnitsToMeters(Particle.Position);
		const Chaos::FRotation3 Q = Particle.Rotation;
		const Chaos::FVec3 Linear = ChaosUnitsToMeters(Particle.Linear);
		const Chaos::FVec3 Angular = Particle.Angular;
		const Chaos::FVec3 Local = (Q * Particle.InertiaRotation).UnrotateVector(Angular);
		const Chaos::FVec3 Inertia = Particle.Inertia / (kChaosUnitsPerBenchmarkMeter * kChaosUnitsPerBenchmarkMeter);
		const double Energy = 0.5 * (Local.X * Local.X * Inertia.X + Local.Y * Local.Y * Inertia.Y + Local.Z * Local.Z * Inertia.Z);
		const int SleepMatches = ((Particle.SleepFlags & 1) != 0) ==
		    (State->CaseExecution.sleepMode == CaseExecutionToggle_Disabled) &&
		    (State->CaseExecution.sleepMode == CaseExecutionToggle_Enabled || (Particle.SleepFlags & 2) != 0) ? 1 : 0;
		AccumulatePyramidWallObservation(State->CaseExecution.pyramidWall, Index,
		    {static_cast<float>(P.X), static_cast<float>(P.Y), static_cast<float>(P.Z)},
		    {static_cast<float>(Q.X), static_cast<float>(Q.Y), static_cast<float>(Q.Z), static_cast<float>(Q.W)},
		    {static_cast<float>(Linear.X), static_cast<float>(Linear.Y), static_cast<float>(Linear.Z)},
		    {static_cast<float>(Angular.X), static_cast<float>(Angular.Y), static_cast<float>(Angular.Z)},
		    Particle.Mass, Energy, SleepMatches, State->CaseExecution.gravity, &Sample);
	}
	FinishPyramidWallObservation(State->CaseExecution.dynamicBodyCount, &Sample);
	Sample.elapsedMs += FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Start);
}

int StepPyramidWallStateTimed(ChaosPyramidWallState* State, int WorkUnitCount)
{
	if (State == nullptr || WorkUnitCount < 0 || WorkUnitCount > State->RawStepCycles.Num() - State->CompletedStepCount)
	{
		return 2;
	}
	const int FirstStep = State->CompletedStepCount;
	const double Timestep = 1.0 / static_cast<double>(State->CaseExecution.timestepHz);
	for (int Step = 0; Step < WorkUnitCount; ++Step)
	{
		const uint64 Start = FPlatformTime::Cycles64();
		State->Evolution.AdvanceOneTimeStep(Timestep);
		State->Evolution.EndFrame(Timestep);
		State->RawStepCycles[FirstStep + Step] = FPlatformTime::Cycles64() - Start;
		const int Ordinal = PyramidWallObservationIndex(FirstStep + Step + 1, State->CaseExecution.measuredWorkUnitCount);
		if (State->Verification == VerificationMode_On && Ordinal >= 0)
			CapturePyramidWallObservation(State, Ordinal);
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

int FormatPyramidWallSettings(const CaseExecutionSpec& execution, const ThreadRuntimeState& RuntimeState,
                               char* Settings, int Capacity)
{
	if (Settings == nullptr || Capacity <= 0)
		return 2;
	const int Size = std::snprintf(
	    Settings, Capacity,
	    "position_iterations=%u; velocity_iterations=%u; projection_iterations=%u; sleep=%s; ccd=%s; damping=0; unit_scale=100_chaos_units_per_meter; worker_count=%d",
	    execution.nativeSolver.values[CaseSolverField_PositionIterations],
	    execution.nativeSolver.values[CaseSolverField_VelocityIterations],
	    execution.nativeSolver.values[CaseSolverField_ProjectionIterations],
	    execution.sleepMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.continuousCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    RuntimeState.EffectiveChaosWorkerCount);
	return Size > 0 && Size < Capacity ? 0 : 2;
}

int RunPyramidWallHeadless(const RunnerArgs& Args, const ThreadRuntimeState& RuntimeState)
{
	ChaosPyramidWallState State(Args.CaseExecution, RuntimeState);
	ChaosCaseView RecordingState = {&State};
	State.RawStepCycles.SetNumUninitialized(Args.StepCount);
	State.Verification = Args.Verification;
	if (CreatePyramidWallState(&State) != 0) return 2;
	benchmark_stack::Capture Capture = {};
	if (Args.Verification == VerificationMode_On && benchmark_stack::OpenCapture(TCHAR_TO_UTF8(Args.StackStream), Args.CaseExecution,
		Args.CaseDescriptor->EngineId, Args.ThreadCount, Args.RepeatIndex, &Capture) != 0)
		return 2;
	std::vector<ChaosVisualStableTransform> RawTransforms(Args.Verification == VerificationMode_On ? Args.CaseExecution.dynamicBodyCount : 0);
	for (int Ordinal = 0; Ordinal <= Args.WarmupSteps; ++Ordinal)
	{
		if ((Ordinal != 0 && WarmupPyramidWallState(&State, 1) != 0) ||
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
	for (uint32 Ordinal = 0; Args.Verification == VerificationMode_On && Ordinal < std::min<uint32>(4, Args.CaseExecution.measuredWorkUnitCount); ++Ordinal)
		ReducePyramidWall(&State, Ordinal);
	constexpr const char* ids[] = {"centre_of_mass_height", "lateral_rms", "translational_energy", "rotational_energy",
	                               "potential_energy", "floor_penetration", "escaped_body_count", "invalid_body_count",
	                               "observation_elapsed_ms"};
	const std::uint32_t sampleCount = Args.Verification == VerificationMode_On ? std::min(4u, Args.CaseExecution.measuredWorkUnitCount) : 0;
	std::array<ChaosObservationRow, 37> rows = {};
	std::uint64_t invalid = 0;
	for (std::uint32_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
	{
		const PyramidWallObservation& sample = State.Observations[sampleIndex];
		invalid += sample.invalidBodies;
		const double values[] = {sample.centreOfMassHeight, sample.lateralRms, sample.translationalEnergy,
		                         sample.rotationalEnergy, sample.potentialEnergy, sample.floorPenetration,
		                         0.0, 0.0, sample.elapsedMs};
		for (std::uint32_t field = 0; field < 9; ++field)
		{
			ChaosObservationRow& row = rows[(field < 8 ? field * sampleCount : 8 * sampleCount + 1) + sampleIndex];
			row = {ids[field], "observation", PyramidWallObservationStep(Args.CaseExecution.measuredWorkUnitCount, sampleIndex), ChaosObservationValueType::Float64, 0};
			std::memcpy(&row.ValueBits, &values[field], sizeof(double));
			if (field == 6 || field == 7)
			{
				row.ValueType = ChaosObservationValueType::Uint64;
				row.ValueBits = field == 6 ? sample.escapedBodies : sample.invalidBodies;
			}
		}
	}
	rows[8 * sampleCount] = {"initial_potential_energy", "construction", 0, ChaosObservationValueType::Float64, 0};
	std::memcpy(&rows[8 * sampleCount].ValueBits, &State.InitialPotentialEnergy, sizeof(double));
	if (Args.Verification == VerificationMode_Off)
	{
		for (const Chaos::FPBDRigidParticleHandle* Body : State.DynamicBodies)
		{
			if (Body == nullptr || !std::isfinite(Body->GetX().X) || !std::isfinite(Body->GetX().Y) || !std::isfinite(Body->GetX().Z) ||
			    !std::isfinite(Body->GetR().X) || !std::isfinite(Body->GetR().Y) || !std::isfinite(Body->GetR().Z) || !std::isfinite(Body->GetR().W)) ++invalid;
		}
	}
	const uint64 InvalidCount = invalid;
	const int CaseValid = RuntimeState.WorkerStatus == ThreadWorkerStatus::Ok && InvalidCount == 0 ? 1 : 0;
	const int MetricValid = State.CompletedStepCount == Args.StepCount && State.PhysicsElapsedMs > 0.0 &&
	                                std::isfinite(State.PhysicsElapsedMs)
	                            ? 1
	                            : 0;
	char Settings[256] = {};
	if (FormatPyramidWallSettings(Args.CaseExecution, RuntimeState, Settings, sizeof(Settings)) != 0)
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
	    rows.data(),
	    Args.Verification == VerificationMode_On ? static_cast<int>(9 * sampleCount + 1) : 0,
	};
	return WriteChaosResult(Args, Result);
}

int StepPyramidWallVisual(ChaosCaseView* State, int WorkUnitCount)
{
	return State == nullptr || State->Handle == nullptr
	           ? 2
			   : StepPyramidWallStateTimed(static_cast<ChaosPyramidWallState*>(State->Handle), WorkUnitCount);
}

ChaosVisualTransform PyramidWallVisualTransform(const Chaos::FPBDRigidParticleHandle* Particle)
{
	const Chaos::FVec3 Position = ChaosUnitsToMeters(Particle->GetX());
	const Chaos::FRotation3 Rotation = Particle->GetR();
	return {
	    static_cast<float>(Position.X), static_cast<float>(Position.Y), static_cast<float>(Position.Z),
	    static_cast<float>(Rotation.X), static_cast<float>(Rotation.Y), static_cast<float>(Rotation.Z),
	    static_cast<float>(Rotation.W),
	};
}

int BuildPyramidWallVisualScene(const ChaosCaseView& State, ChaosVisualGeometry* Geometries,

                                 benchmark_visual::VisualMeshStorage* Meshes, int GeometryCapacity,
                                 ChaosVisualInstance* Instances, int InstanceCapacity, int* GeometryCount,
                                 int* InstanceCount)
{
	if (State.Handle == nullptr || Geometries == nullptr || GeometryCapacity < 2 || Instances == nullptr ||
	    GeometryCount == nullptr || InstanceCount == nullptr)
	{
		return 2;
	}
	const ChaosPyramidWallState& Value = *static_cast<const ChaosPyramidWallState*>(State.Handle);
	const CaseExecutionSpec& Execution = Value.CaseExecution;
	const CaseExecutionPyramidWall& Fixture = Execution.pyramidWall;
	if (InstanceCapacity < static_cast<int>(Execution.visualInstanceCount))
		return 2;
	if (BuildResolvedVisualGeometry(Execution, Execution.selectedGeometry, Meshes, &Geometries[0]) != 0)
		return 2;
	Geometries[1] = {2u, Fixture.floorHalfExtents.x, Fixture.floorHalfExtents.y, Fixture.floorHalfExtents.z};
	for (int Index = 0; Index < Value.DynamicBodies.Num(); ++Index)
	{
		Instances[Index] = {
		    0u,
		    static_cast<uint32>(Index),
		    static_cast<uint32>(Index),
		    PyramidWallVisualTransform(Value.DynamicBodies[Index]),
		};
	}
	const int FloorSlot = Value.DynamicBodies.Num();
	Instances[FloorSlot] = {
	    1u,
	    static_cast<uint32>(FloorSlot),
	    MAX_uint32,
	    {0.0f, -Fixture.floorHalfExtents.y, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
	};
	*GeometryCount = 2;
	*InstanceCount = static_cast<int>(Execution.visualInstanceCount);
	return 0;
}

int SamplePyramidWallVisualTransforms(const ChaosCaseView& State, ChaosVisualStableTransform* Transforms, int Capacity)
{
	if (State.Handle == nullptr || Transforms == nullptr)
		return 2;
	const ChaosPyramidWallState& Value = *static_cast<const ChaosPyramidWallState*>(State.Handle);
	if (Capacity < Value.DynamicBodies.Num())
		return 2;
	for (int Index = 0; Index < Value.DynamicBodies.Num(); ++Index)
	{
		Transforms[Index] = {
		    static_cast<uint32>(Index),
		    PyramidWallVisualTransform(Value.DynamicBodies[Index]),
		};
	}
	return 0;
}

const ChaosCaseDescriptor& PyramidWallDescriptor()
{
	static const ChaosCaseDescriptor Descriptor = {
	    kEngineId,
	    RunPyramidWallHeadless,
	    StepPyramidWallVisual,
	    BuildPyramidWallVisualScene,
	    SamplePyramidWallVisualTransforms,
	    nullptr,
	};
	return Descriptor;
}
} // namespace BenchmarkPolygonChaos
