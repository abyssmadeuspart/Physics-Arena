#include "Cases/BenchmarkPolygonChaosBoxContactIslandsCase.h"

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
namespace
{
double ContactIslandOrigin(float Spacing, uint32 Coordinate, uint32 Count)
{
	return static_cast<double>(Spacing) * (static_cast<double>(Coordinate) - 0.5 * static_cast<double>(Count - 1));
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

void SetParticleBounds(Chaos::FGeometryParticleHandle* Particle, const Chaos::FVec3& HalfExtents)
{
	Particle->SetLocalBounds(Chaos::FAABB3(-HalfExtents, HalfExtents));
	Particle->UpdateWorldSpaceState(Chaos::FRigidTransform3(Particle->GetX(), Particle->GetR()), Chaos::FVec3(0));
	Particle->SetHasBounds(true);
}

void AddContactIslandFloor(ChaosContactIslandsState* State, double PositionX, double PositionZ)
{
	const CaseExecutionContactIslands& Fixture = State->CaseExecution.contactIslands;
	Chaos::FGeometryParticleHandle* Particle = State->Evolution.CreateStaticParticles(1)[0];
	const Chaos::FVec3 HalfExtents = MetersToChaosUnits(
	    Chaos::FVec3(Fixture.floorHalfExtents.x, Fixture.floorHalfExtents.y, Fixture.floorHalfExtents.z));
	Particle->SetGeometry(Chaos::FImplicitObjectPtr(new Chaos::TBox<Chaos::FReal, 3>(-HalfExtents, HalfExtents)));
	Particle->SetX(MetersToChaosUnits(Chaos::FVec3(PositionX, -Fixture.floorHalfExtents.y, PositionZ)));
	Particle->SetR(Chaos::FRotation3::FromIdentity());
	SetParticleBounds(Particle, HalfExtents);
	SetParticleShapesToCollide(Particle);
	State->Evolution.EnableParticle(Particle);
}

void AddContactIslandBody(ChaosContactIslandsState* State, const Chaos::FVec3& Position,
                          const ChaosResolvedShape& Shape)
{
	const CaseExecutionContactIslands& Fixture = State->CaseExecution.contactIslands;
	Chaos::FPBDRigidParticleHandle* Particle = State->Evolution.CreateDynamicParticles(1)[0];
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

void AdvanceContactIslands(ChaosContactIslandsState* State)
{
	const double Timestep = 1.0 / static_cast<double>(State->CaseExecution.timestepHz);
	State->Evolution.AdvanceOneTimeStep(Timestep);
	State->Evolution.EndFrame(Timestep);
}
}

ChaosContactIslandsState::ChaosContactIslandsState(const CaseExecutionSpec& InCaseExecution,
                                                   const ThreadRuntimeState& RuntimeState)
    : CaseExecution(InCaseExecution), Particles(UniqueIndices),
      Evolution(Particles, PhysicalMaterials, nullptr, nullptr, nullptr, nullptr,
	            ThreadExecutionModeForThreadCount(RuntimeState.RequestedThreadCount) ==
	                ThreadExecutionMode::SingleThreaded)
{
	DynamicBodies.Reserve(static_cast<int>(CaseExecution.dynamicBodyCount));
}

int CreateContactIslandsState(ChaosContactIslandsState* State)
{
	if (State == nullptr || State->CaseExecution.fixtureKind != CaseFixtureKind_BoxContactIslands)
	{
		return 2;
	}
	const CaseExecutionSpec& CaseExecution = State->CaseExecution;
	const CaseExecutionContactIslands& Fixture = CaseExecution.contactIslands;
	ChaosResolvedShape Shape;
	if (CreateChaosResolvedShape(CaseExecution, CaseExecution.selectedGeometry, &Shape) != 0)
		return 2;
	State->Evolution.SetNumPositionIterations(
	    static_cast<int32>(State->CaseExecution.nativeSolver.values[CaseSolverField_PositionIterations]));
	State->Evolution.SetNumVelocityIterations(
	    static_cast<int32>(State->CaseExecution.nativeSolver.values[CaseSolverField_VelocityIterations]));
	State->Evolution.SetNumProjectionIterations(
	    static_cast<int32>(State->CaseExecution.nativeSolver.values[CaseSolverField_ProjectionIterations]));
	Chaos::FCollisionDetectorSettings DetectorSettings =
	    State->Evolution.GetCollisionConstraints().GetDetectorSettings();
	DetectorSettings.BoundsExpansion = 3.0f;
	DetectorSettings.bDeferNarrowPhase = false;
	DetectorSettings.bAllowManifolds = true;
	State->Evolution.GetCollisionConstraints().SetDetectorSettings(DetectorSettings);
	State->Evolution.GetGravityForces().SetAcceleration(
	    MetersToChaosUnits(Chaos::FVec3(CaseExecution.gravity.x, CaseExecution.gravity.y, CaseExecution.gravity.z)), 0);
	State->Material = MakeUnique<Chaos::FChaosPhysicsMaterial>();
	State->Material->Friction = CaseExecution.friction;
	State->Material->Restitution = CaseExecution.restitution;

	for (uint32 GroupZ = 0; GroupZ < Fixture.islandGrid[1]; ++GroupZ)
	{
		for (uint32 GroupX = 0; GroupX < Fixture.islandGrid[0]; ++GroupX)
		{
			AddContactIslandFloor(State, ContactIslandOrigin(Fixture.islandSpacing[0], GroupX, Fixture.islandGrid[0]),
			                      ContactIslandOrigin(Fixture.islandSpacing[1], GroupZ, Fixture.islandGrid[1]));
		}
	}

	for (uint32 GroupZ = 0; GroupZ < Fixture.islandGrid[1]; ++GroupZ)
	{
		for (uint32 GroupX = 0; GroupX < Fixture.islandGrid[0]; ++GroupX)
		{
			const double OriginX = ContactIslandOrigin(Fixture.islandSpacing[0], GroupX, Fixture.islandGrid[0]);
			const double OriginZ = ContactIslandOrigin(Fixture.islandSpacing[1], GroupZ, Fixture.islandGrid[1]);
			for (uint32 Y = 0; Y < Fixture.bodyGrid[1]; ++Y)
			{
				for (uint32 Z = 0; Z < Fixture.bodyGrid[2]; ++Z)
				{
					for (uint32 X = 0; X < Fixture.bodyGrid[0]; ++X)
					{
						AddContactIslandBody(
						    State,
						    Chaos::FVec3(OriginX + (static_cast<double>(X) -
							                        0.5 * static_cast<double>(Fixture.bodyGrid[0] - 1)) *
							                           Fixture.bodySpacing.x,
							             Fixture.bodyInitialY + static_cast<double>(Y) * Fixture.bodySpacing.y,
							             OriginZ + (static_cast<double>(Z) -
							                        0.5 * static_cast<double>(Fixture.bodyGrid[2] - 1)) *
							                           Fixture.bodySpacing.z),
						    Shape);
					}
				}
			}
		}
	}

	State->RawWorkUnitCycles.SetNumUninitialized(static_cast<int>(CaseExecution.measuredWorkUnitCount));
	return State->DynamicBodies.Num() == static_cast<int>(CaseExecution.dynamicBodyCount) ? 0 : 2;
}

int WarmupContactIslands(ChaosContactIslandsState* State, int WorkUnitCount)
{
	if (State == nullptr || WorkUnitCount < 0 || WorkUnitCount > static_cast<int>(State->CaseExecution.warmupWorkUnitCount))
	{
		return 2;
	}
	for (int WorkUnit = 0; WorkUnit < WorkUnitCount; ++WorkUnit)
	{
		AdvanceContactIslands(State);
	}
	return 0;
}

int StepContactIslandsTimed(ChaosContactIslandsState* State, int WorkUnitCount)
{
	if (State == nullptr || WorkUnitCount < 0 ||
	    WorkUnitCount > State->RawWorkUnitCycles.Num() - State->CompletedWorkUnitCount)
	{
		return 2;
	}
	const int FirstWorkUnit = State->CompletedWorkUnitCount;
	for (int WorkUnit = 0; WorkUnit < WorkUnitCount; ++WorkUnit)
	{
		const uint64 Start = FPlatformTime::Cycles64();
		AdvanceContactIslands(State);
		const uint64 End = FPlatformTime::Cycles64();
		State->RawWorkUnitCycles[FirstWorkUnit + WorkUnit] = End - Start;
	}
	for (int WorkUnit = 0; WorkUnit < WorkUnitCount; ++WorkUnit)
	{
		const double Milliseconds = FPlatformTime::ToMilliseconds64(State->RawWorkUnitCycles[FirstWorkUnit + WorkUnit]);
		State->WorkloadElapsedMs += Milliseconds;
		State->LatestWorkUnitElapsedMs = Milliseconds;
	}
	State->CompletedWorkUnitCount += WorkUnitCount;
	return 0;
}

int ValidateContactIslands(const ChaosContactIslandsState& State, uint64* InvalidTransformCount)
{
	if (InvalidTransformCount == nullptr)
	{
		return 2;
	}
	*InvalidTransformCount = 0;
	for (int Slot = 0; Slot < State.DynamicBodies.Num(); ++Slot)
	{
		const Chaos::FVec3 Position = State.DynamicBodies[Slot]->GetX();
		const Chaos::FRotation3 Rotation = State.DynamicBodies[Slot]->GetR();
		if (!std::isfinite(Position.X) || !std::isfinite(Position.Y) || !std::isfinite(Position.Z) ||
		    !std::isfinite(Rotation.X) || !std::isfinite(Rotation.Y) || !std::isfinite(Rotation.Z) ||
		    !std::isfinite(Rotation.W))
		{
			++*InvalidTransformCount;
		}
	}
	return 0;
}

int RunContactIslandsHeadless(const RunnerArgs& Args, const ThreadRuntimeState& RuntimeState)
{
	ChaosContactIslandsState State(Args.CaseExecution, RuntimeState);
	ChaosCaseView RecordingState = {&State};
	if (CreateContactIslandsState(&State) != 0)
	{
		std::fprintf(stderr, "run_failed reason=create_fixture\n");
		return 2;
	}
	benchmark_stack::Capture Capture = {};
	if (Args.Verification == VerificationMode_On && benchmark_stack::OpenCapture(TCHAR_TO_UTF8(Args.StackStream), Args.CaseExecution,
		Args.CaseDescriptor->EngineId, Args.ThreadCount, Args.RepeatIndex, &Capture) != 0)
		return 2;
	std::vector<ChaosVisualStableTransform> RawTransforms(Args.Verification == VerificationMode_On ? Args.CaseExecution.dynamicBodyCount : 0);
	for (int Ordinal = 0; Ordinal <= Args.WarmupSteps; ++Ordinal)
	{
		if ((Ordinal != 0 && WarmupContactIslands(&State, 1) != 0) ||
			(Args.Verification == VerificationMode_On && CaptureChaosFrame(Args, RecordingState, RawTransforms.data(), &Capture,
				Ordinal == 0 ? benchmark_stack::Phase_Construction : benchmark_stack::Phase_Warmup, Ordinal) != 0))
		{
			Capture.status = 2;
			benchmark_stack::CloseCapture(&Capture);
			return 2;
		}
	}
	int Status = RecordChaosCase(Args, &RecordingState, Args.Verification == VerificationMode_On ? &Capture : nullptr);
	if (Status != 0) Capture.status = 2;
	if ((Args.Verification == VerificationMode_On && benchmark_stack::CloseCapture(&Capture) != 0)) Status = 2;
	if (Status == 0)
	{
		uint64 InvalidTransformCount = 0;
		const int ValidationStatus = ValidateContactIslands(State, &InvalidTransformCount);
		const bool CaseValid = ValidationStatus == 0 && InvalidTransformCount == 0 &&
		                       State.DynamicBodies.Num() == static_cast<int>(Args.CaseExecution.dynamicBodyCount) &&
		                       RuntimeState.WorkerStatus == ThreadWorkerStatus::Ok;
		const bool MetricValid = State.CompletedWorkUnitCount == Args.StepCount && State.WorkloadElapsedMs > 0.0 &&
		                         std::isfinite(State.WorkloadElapsedMs) &&
		                         State.RawWorkUnitCycles.Num() == Args.StepCount;
		char Settings[256] = {};
		if (FormatContactIslandsVisualPhysicsSettings(Args.CaseExecution, RuntimeState, Settings, sizeof(Settings)) !=
		    0)
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
		    State.CompletedWorkUnitCount,
		    State.WorkloadElapsedMs,
		    &State.RawWorkUnitCycles,
		};
		Status = WriteChaosResult(Args, Result);
	}
	return Status;
}

int SampleContactIslandsVisualTransforms(const ChaosCaseView& State, ChaosVisualStableTransform* Transforms,
                                         int Capacity)
{
	if (State.Handle == nullptr || Transforms == nullptr)
	{
		return 2;
	}
	const ChaosContactIslandsState* CaseState = static_cast<const ChaosContactIslandsState*>(State.Handle);
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

int BuildContactIslandsVisualScene(const ChaosCaseView& State, ChaosVisualGeometry* Geometries,

                                   benchmark_visual::VisualMeshStorage* Meshes, int GeometryCapacity,
                                   ChaosVisualInstance* Instances, int InstanceCapacity, int* GeometryCount,
                                   int* InstanceCount)
{
	if (State.Handle == nullptr || Geometries == nullptr || Instances == nullptr || GeometryCount == nullptr ||
	    InstanceCount == nullptr)
	{
		return 2;
	}
	const ChaosContactIslandsState* CaseState = static_cast<const ChaosContactIslandsState*>(State.Handle);
	const CaseExecutionSpec& CaseExecution = CaseState->CaseExecution;
	const CaseExecutionContactIslands& Fixture = CaseExecution.contactIslands;
	if (GeometryCapacity < 2 || InstanceCapacity < static_cast<int>(CaseExecution.bodyCount))
	{
		return 2;
	}
	if (BuildResolvedVisualGeometry(CaseExecution, CaseExecution.selectedGeometry, Meshes, &Geometries[0]) != 0)
		return 2;
	Geometries[1] = {2u, Fixture.floorHalfExtents.x, Fixture.floorHalfExtents.y, Fixture.floorHalfExtents.z};
	TArray<ChaosVisualStableTransform> Transforms;
	Transforms.SetNumUninitialized(CaseState->DynamicBodies.Num());
	if (SampleContactIslandsVisualTransforms(State, Transforms.GetData(), Transforms.Num()) != 0)
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
	int Index = 0;
	for (uint32 GroupZ = 0; GroupZ < Fixture.islandGrid[1]; ++GroupZ)
	{
		for (uint32 GroupX = 0; GroupX < Fixture.islandGrid[0]; ++GroupX)
		{
			const int InstanceIndex = CaseState->DynamicBodies.Num() + Index;
			Instances[InstanceIndex] = {
			    1u,
			    static_cast<uint32>(InstanceIndex),
			    MAX_uint32,
			    {
			        static_cast<float>(ContactIslandOrigin(Fixture.islandSpacing[0], GroupX, Fixture.islandGrid[0])),
			        -Fixture.floorHalfExtents.y,
			        static_cast<float>(ContactIslandOrigin(Fixture.islandSpacing[1], GroupZ, Fixture.islandGrid[1])),
			        0.0f,
			        0.0f,
			        0.0f,
			        1.0f,
			    },
			};
			Index += 1;
		}
	}
	*GeometryCount = 2;
	*InstanceCount = static_cast<int>(CaseExecution.bodyCount);
	return Index == static_cast<int>(CaseExecution.staticBodyCount) ? 0 : 2;
}

int FormatContactIslandsVisualPhysicsSettings(const CaseExecutionSpec& CaseExecution,
                                              const ThreadRuntimeState& RuntimeState, char* Settings, int Capacity)
{
	if (Settings == nullptr || Capacity <= 0)
	{
		return 2;
	}
	const int Size = std::snprintf(
	    Settings, Capacity,
	    "position_iterations=%u; velocity_iterations=%u; projection_iterations=%u; sleep=%s; ccd=%s; unit_scale=100_chaos_units_per_meter; worker_count=%d; islands=%u",
	    CaseExecution.nativeSolver.values[CaseSolverField_PositionIterations],
	    CaseExecution.nativeSolver.values[CaseSolverField_VelocityIterations],
	    CaseExecution.nativeSolver.values[CaseSolverField_ProjectionIterations],
	    CaseExecution.sleepMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    CaseExecution.continuousCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    RuntimeState.EffectiveChaosWorkerCount,
	    CaseExecution.contactIslands.islandGrid[0] * CaseExecution.contactIslands.islandGrid[1]);
	return Size > 0 && Size < Capacity ? 0 : 2;
}

int StepContactIslandsVisual(ChaosCaseView* State, int WorkUnitCount)
{
	return State == nullptr || State->Handle == nullptr
	           ? 2
			   : StepContactIslandsTimed(static_cast<ChaosContactIslandsState*>(State->Handle), WorkUnitCount);
}

const ChaosCaseDescriptor& ContactIslandsDescriptor()
{
	static const ChaosCaseDescriptor Descriptor = {
	    kEngineId,
	    RunContactIslandsHeadless,
	    StepContactIslandsVisual,
	    BuildContactIslandsVisualScene,
	    SampleContactIslandsVisualTransforms,
	    nullptr,
	};
	return Descriptor;
}

} // namespace BenchmarkPolygonChaos
