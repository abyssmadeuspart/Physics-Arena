#include "Cases/BenchmarkPolygonChaosRagdollStairTumbleCase.h"

#include "Output/BenchmarkPolygonChaosOutput.h"
#include "Runtime/BenchmarkPolygonChaosUnits.h"

#include "Chaos/Box.h"
#include "Chaos/CollisionFilterData.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/ShapeInstance.h"
#include "Chaos/Sphere.h"

#include <cmath>
#include <cstdio>

namespace BenchmarkPolygonChaos
{
namespace
{
constexpr double kRagdollPi = 3.14159265358979323846;

Chaos::FVec3 ToChaosVector(const CaseExecutionVector3& Vector)
{
	return Chaos::FVec3(Vector.x, Vector.y, Vector.z);
}

Chaos::FRotation3 RagdollRotation(const CaseExecutionRagdoll& Fixture, uint32 RagdollIndex)
{
	const double Yaw = Fixture.yawPatternDegrees[RagdollIndex % Fixture.yawPatternCount] * kRagdollPi / 180.0;
	return Chaos::FRotation3(FQuat(FVector(0.0, 1.0, 0.0), Yaw) *
	                         FQuat(FVector(1.0, 0.0, 0.0), Fixture.pitchDegrees * kRagdollPi / 180.0));
}

Chaos::FVec3 RagdollBase(const CaseExecutionRagdoll& Fixture, uint32 Row, uint32 Column)
{
	return Chaos::FVec3(
	    (static_cast<double>(Column) - 0.5 * static_cast<double>(Fixture.ragdollGrid[1] - 1)) * Fixture.columnSpacing,
	    static_cast<double>(Fixture.stairCount - 1 - Row) * Fixture.stairRise + Fixture.baseHeightOffset,
	    (static_cast<double>(Row) - 0.5 * static_cast<double>(Fixture.stairCount - 1)) * Fixture.rowSpacing);
}

int SameRagdollGeometry(const CaseExecutionRagdollPart& Left, const CaseExecutionRagdollPart& Right)
{
	if (Left.shape != Right.shape || Left.halfSegment != Right.halfSegment || Left.axis != Right.axis)
	{
		return 0;
	}
	if (Left.shape == CaseExecutionShape_Sphere || Left.shape == CaseExecutionShape_Capsule)
	{
		return Left.radius == Right.radius ? 1 : 0;
	}
	return Left.halfExtents.x == Right.halfExtents.x && Left.halfExtents.y == Right.halfExtents.y &&
	               Left.halfExtents.z == Right.halfExtents.z
	           ? 1
			   : 0;
}

uint32 RagdollGeometryIndex(const CaseExecutionRagdoll& Fixture, uint16 PartIndex)
{
	uint32 GeometryIndex = 0;
	for (uint16 SourceIndex = 0; SourceIndex < Fixture.partCount; ++SourceIndex)
	{
		int Duplicate = 0;
		for (uint16 PreviousIndex = 0; PreviousIndex < SourceIndex; ++PreviousIndex)
		{
			if (SameRagdollGeometry(Fixture.parts[SourceIndex], Fixture.parts[PreviousIndex]) != 0)
			{
				Duplicate = 1;
				break;
			}
		}
		if (Duplicate != 0)
		{
			continue;
		}
		if (SameRagdollGeometry(Fixture.parts[PartIndex], Fixture.parts[SourceIndex]) != 0)
		{
			return GeometryIndex;
		}
		++GeometryIndex;
	}
	return MAX_uint32;
}

int RagdollPartGeometryCount(const CaseExecutionRagdoll& Fixture)
{
	int GeometryCount = 0;
	for (uint16 PartIndex = 0; PartIndex < Fixture.partCount; ++PartIndex)
	{
		if (RagdollGeometryIndex(Fixture, PartIndex) == static_cast<uint32>(GeometryCount))
		{
			++GeometryCount;
		}
	}
	return GeometryCount;
}

void SetRagdollShapeFilterToCollide(Chaos::FPerShapeData& Shape)
{
	const Chaos::Filter::FShapeFilterData ShapeFilterData =
	    Chaos::Filter::FShapeFilterBuilder::BuildBlockAll(Chaos::EFilterFlags::SimpleCollision);
	Shape.SetShapeFilterData(ShapeFilterData);
}

void SetRagdollParticleShapesToCollide(Chaos::FGeometryParticleHandle* Particle)
{
	for (const TUniquePtr<Chaos::FPerShapeData>& Shape : Particle->ShapesArray())
	{
		SetRagdollShapeFilterToCollide(*Shape);
	}
}

void SetRagdollParticleBounds(Chaos::FGeometryParticleHandle* Particle, const Chaos::FVec3& HalfExtents)
{
	Particle->SetLocalBounds(Chaos::FAABB3(-HalfExtents, HalfExtents));
	Particle->UpdateWorldSpaceState(Chaos::FRigidTransform3(Particle->GetX(), Particle->GetR()), Chaos::FVec3(0));
	Particle->SetHasBounds(true);
}

void InitializeRagdollEvolution(ChaosRagdollCaseState* State)
{
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
	State->Evolution.GetGravityForces().SetAcceleration(MetersToChaosUnits(ToChaosVector(State->CaseExecution.gravity)),
	                                                    0);
}

void AddRagdollStaticBox(ChaosRagdollCaseState* State, const CaseExecutionBox& Box)
{
	Chaos::FGeometryParticleHandle* Particle = State->Evolution.CreateStaticParticles(1)[0];
	const Chaos::FVec3 ChaosPosition = MetersToChaosUnits(ToChaosVector(Box.center));
	const Chaos::FVec3 ChaosHalfExtents = MetersToChaosUnits(ToChaosVector(Box.halfExtents));
	Particle->SetGeometry(
	    Chaos::FImplicitObjectPtr(new Chaos::TBox<Chaos::FReal, 3>(-ChaosHalfExtents, ChaosHalfExtents)));
	Particle->SetX(ChaosPosition);
	Particle->SetR(Chaos::FRotation3::FromIdentity());
	SetRagdollParticleBounds(Particle, ChaosHalfExtents);
	SetRagdollParticleShapesToCollide(Particle);
	State->Evolution.EnableParticle(Particle);
	State->CreatedStaticBodyCount += 1;
}

void AddRagdollDynamicBody(ChaosRagdollCaseState* State, int BodyIndex, const ChaosResolvedShape& Shape,
                           const Chaos::FVec3& Position, const Chaos::FRotation3& Rotation, double LinearVelocityZ)
{
	const CaseExecutionRagdoll& Fixture = State->CaseExecution.ragdoll;
	Chaos::FPBDRigidParticleHandle* Particle = State->Evolution.CreateDynamicParticles(1)[0];
	const Chaos::FVec3 ChaosPosition = MetersToChaosUnits(Position);
	Particle->SetGeometry(Shape.Geometry);
	Particle->SetX(ChaosPosition);
	Particle->SetP(ChaosPosition);
	Particle->SetR(Rotation);
	Particle->SetQ(Rotation);
	Particle->SetV(MetersToChaosUnits(Chaos::FVec3(0.0, 0.0, LinearVelocityZ)));
	Particle->SetW(Chaos::FVec3(0));
	const double Mass = Fixture.partMass;
	SetChaosResolvedMass(Particle, Shape, Mass);
	Particle->SetGravityEnabled(true);
	Particle->SetLinearEtherDrag(Fixture.linearDamping);
	Particle->SetAngularEtherDrag(Fixture.angularDamping);
	Particle->SetSleepType(State->CaseExecution.sleepMode == CaseExecutionToggle_Disabled
	                           ? Chaos::ESleepType::NeverSleep
							   : Chaos::ESleepType::MaterialSleep);
	Particle->SetCCDEnabled(State->CaseExecution.continuousCollisionMode == CaseExecutionToggle_Enabled);
	State->Evolution.SetPhysicsMaterial(Particle, MakeSerializable(State->Material));
	SetRagdollParticleBounds(Particle, Shape.HalfExtents);
	SetRagdollParticleShapesToCollide(Particle);
	State->Evolution.EnableParticle(Particle);
	State->DynamicBodies[BodyIndex] = Particle;
	State->CreatedDynamicBodyCount += 1;
}

void AddRagdollJoint(ChaosRagdollCaseState* State, uint32 RagdollIndex, const CaseExecutionRagdollLink& Link)
{
	const CaseExecutionRagdoll& Fixture = State->CaseExecution.ragdoll;
	Chaos::FPBDJointSettings Settings;
	Settings.ConnectorTransforms[0] = Chaos::FRigidTransform3(
	    MetersToChaosUnits(Chaos::FVec3(Link.childLocalAnchor.x, Link.childLocalAnchor.y, Link.childLocalAnchor.z)),
	    Chaos::FRotation3::FromIdentity());
	Settings.ConnectorTransforms[1] = Chaos::FRigidTransform3(
	    MetersToChaosUnits(Chaos::FVec3(Link.parentLocalAnchor.x, Link.parentLocalAnchor.y, Link.parentLocalAnchor.z)),
	    Chaos::FRotation3::FromIdentity());
	Settings.LinearMotionTypes = Chaos::TVector<Chaos::EJointMotionType, 3>(
	    Chaos::EJointMotionType::Locked, Chaos::EJointMotionType::Locked, Chaos::EJointMotionType::Locked);
	Settings.AngularMotionTypes = Chaos::TVector<Chaos::EJointMotionType, 3>(
	    Chaos::EJointMotionType::Free, Chaos::EJointMotionType::Free, Chaos::EJointMotionType::Free);
	Settings.bCollisionEnabled = Fixture.linkedCollisionMode == CaseExecutionToggle_Enabled;
	Settings.bUseLinearSolver = true;
	Settings.bProjectionEnabled = false;
	Settings.bSoftLinearLimitsEnabled = false;
	Settings.bSoftTwistLimitsEnabled = false;
	Settings.bSoftSwingLimitsEnabled = false;
	Settings.Sanitize();
	Chaos::FPBDRigidParticleHandle* ChildParticle =
	    State->DynamicBodies[RagdollIndex * Fixture.partCount + Link.childPart];
	Chaos::FPBDRigidParticleHandle* ParentParticle =
	    State->DynamicBodies[RagdollIndex * Fixture.partCount + Link.parentPart];
	State->Evolution.CreateJointConstraint(
	    Chaos::FParticlePair(ChildParticle, ParentParticle), Settings);
}

int CreateRagdollFixture(ChaosRagdollCaseState* State)
{
	if (State->Verification == VerificationMode_On) State->QualityTransforms.SetNumUninitialized(static_cast<int>(State->CaseExecution.dynamicBodyCount));
	if (State == nullptr || State->CaseExecution.fixtureKind != CaseFixtureKind_RagdollStairTumble)
	{
		return 2;
	}
	const CaseExecutionSpec& CaseExecution = State->CaseExecution;
	const CaseExecutionRagdoll& Fixture = CaseExecution.ragdoll;
	InitializeRagdollEvolution(State);
	State->Material = MakeUnique<Chaos::FChaosPhysicsMaterial>();
	State->Material->Friction = CaseExecution.friction;
	State->Material->Restitution = CaseExecution.restitution;
	for (uint32 Row = 0; Row < Fixture.stairCount; ++Row)
	{
		const CaseExecutionBox Stair = {
		    {
		        0.0f,
		        static_cast<float>(static_cast<double>(Fixture.stairCount - 1 - Row) * Fixture.stairRise -
				                   Fixture.stairHalfHeight),
		        static_cast<float>((static_cast<double>(Row) - 0.5 * static_cast<double>(Fixture.stairCount - 1)) *
				                   Fixture.stairDepth),
		    },
		    {
		        Fixture.stairHalfWidth,
		        Fixture.stairHalfHeight,
		        Fixture.stairHalfDepth,
		    },
		};
		AddRagdollStaticBox(State, Stair);
	}
	for (uint16 Index = 0; Index < Fixture.extraStaticBoxCount; ++Index)
	{
		AddRagdollStaticBox(State, Fixture.extraStaticBoxes[Index]);
	}
	ChaosResolvedShape PartShapes[kCaseExecutionRagdollPartCapacity];
	for (uint16 PartIndex = 0; PartIndex < Fixture.partCount; ++PartIndex)
	{
		const CaseExecutionGeometry Geometry = CaseExecutionPartGeometry(Fixture.parts[PartIndex]);
		int SharedIndex = -1;
		for (uint16 Previous = 0; Previous < PartIndex; ++Previous)
			if (CaseExecutionSameGeometry(Geometry, CaseExecutionPartGeometry(Fixture.parts[Previous])) != 0)
			{
				SharedIndex = Previous;
				break;
			}
		if (SharedIndex >= 0)
			PartShapes[PartIndex] = PartShapes[SharedIndex];
		else if (CreateChaosResolvedShape(CaseExecution, Geometry, &PartShapes[PartIndex]) != 0)
			return 2;
	}
	for (uint32 Row = 0; Row < Fixture.ragdollGrid[0]; ++Row)
	{
		for (uint32 Column = 0; Column < Fixture.ragdollGrid[1]; ++Column)
		{
			const uint32 RagdollIndex = Row * Fixture.ragdollGrid[1] + Column;
			const Chaos::FRotation3 Rotation = RagdollRotation(Fixture, RagdollIndex);
			const Chaos::FVec3 Base = RagdollBase(Fixture, Row, Column);
			for (uint16 PartIndex = 0; PartIndex < Fixture.partCount; ++PartIndex)
			{
				const CaseExecutionRagdollPart& Part = Fixture.parts[PartIndex];
				AddRagdollDynamicBody(State, static_cast<int>(RagdollIndex * Fixture.partCount + PartIndex),
				                      PartShapes[PartIndex], Base + Rotation.RotateVector(ToChaosVector(Part.center)),
				                      Rotation * PartShapes[PartIndex].BodyRotation,
				                      Row == 0 ? Fixture.triggerRowSpeed : Fixture.followerRowSpeed);
			}
		}
	}
	const uint32 RagdollCount = Fixture.ragdollGrid[0] * Fixture.ragdollGrid[1];
	for (uint32 RagdollIndex = 0; RagdollIndex < RagdollCount; ++RagdollIndex)
	{
		for (uint16 LinkIndex = 0; LinkIndex < Fixture.linkCount; ++LinkIndex)
		{
			AddRagdollJoint(State, RagdollIndex, Fixture.links[LinkIndex]);
		}
	}
	return State->CreatedDynamicBodyCount == static_cast<int>(CaseExecution.dynamicBodyCount) &&
	               State->CreatedStaticBodyCount == static_cast<int>(CaseExecution.staticBodyCount) &&
	               State->Particles.GetDynamicParticles().Size() == CaseExecution.dynamicBodyCount &&
	               State->Particles.GetNonDisabledStaticParticles().Size() == CaseExecution.staticBodyCount &&
	               State->Evolution.GetJointCombinedConstraints().NumConstraints() == CaseExecution.constraintCount
	           ? 0
			   : 2;
}

void AdvanceRagdoll(ChaosRagdollCaseState* State)
{
	const double Timestep = 1.0 / static_cast<double>(State->CaseExecution.timestepHz);
	State->Evolution.AdvanceOneTimeStep(Timestep);
	State->Evolution.EndFrame(Timestep);
}

int StepRagdollUntimed(ChaosRagdollCaseState* State, int StepCount)
{
	if (State == nullptr || StepCount != static_cast<int>(State->CaseExecution.warmupWorkUnitCount))
	{
		return 2;
	}
	for (int Step = 0; Step < StepCount; ++Step)
	{
		AdvanceRagdoll(State);
	}
	return 0;
}

int StepRagdollQuality(ChaosRagdollCaseState* State, int StepCount)
{
	if (State == nullptr || StepCount < 0 ||
	    StepCount > static_cast<int>(State->CaseExecution.measuredWorkUnitCount) - State->CompletedStepCount)
	{
		return 2;
	}
	for (int Step = 0; Step < StepCount; ++Step)
	{
		AdvanceRagdoll(State);
		++State->CompletedStepCount;
		if (State->Verification == VerificationMode_On)
		{
			for (uint32 Index = 0; Index < State->CaseExecution.dynamicBodyCount; ++Index)
			{
				benchmark_visual::VisualStableTransform& Transform = State->QualityTransforms[Index];
				if (State->DynamicBodies[Index] == nullptr)
				{
					Transform.stableSlot = UINT32_MAX;
					continue;
				}
				const Chaos::FVec3 Position = ChaosUnitsToMeters(State->DynamicBodies[Index]->GetX());
				const Chaos::FRotation3 Rotation = State->DynamicBodies[Index]->GetR();
				Transform = {Index,
				             {static_cast<float>(Position.X), static_cast<float>(Position.Y),
				              static_cast<float>(Position.Z), static_cast<float>(Rotation.X),
				              static_cast<float>(Rotation.Y), static_cast<float>(Rotation.Z),
				              static_cast<float>(Rotation.W)}};
			}
			AccumulateRagdollQuality(State->CaseExecution, State->QualityTransforms.GetData(),
			                         State->CaseExecution.dynamicBodyCount, State->CompletedStepCount, &State->Quality);
		}
	}
	return 0;
}

uint64 CountRagdollInvalidTransforms(const ChaosRagdollCaseState& State)
{
	uint64 InvalidCount = 0;
	for (uint32 Index = 0; Index < State.CaseExecution.dynamicBodyCount; ++Index)
	{
		if (State.DynamicBodies[Index] == nullptr)
		{
			++InvalidCount;
			continue;
		}
		const Chaos::FVec3 Position = State.DynamicBodies[Index]->GetX();
		const Chaos::FRotation3 Rotation = State.DynamicBodies[Index]->GetR();
		if (!std::isfinite(Position.X) || !std::isfinite(Position.Y) || !std::isfinite(Position.Z) ||
		    !std::isfinite(Rotation.X) || !std::isfinite(Rotation.Y) || !std::isfinite(Rotation.Z) ||
		    !std::isfinite(Rotation.W))
		{
			InvalidCount += 1;
		}
	}
	return InvalidCount;
}

int RunRagdollWarmup(const CaseExecutionSpec& CaseExecution, int WarmupSteps, const ThreadRuntimeState& RuntimeState, VerificationMode Verification)
{
	ChaosRagdollCaseState WarmupState(CaseExecution, RuntimeState);
	WarmupState.Verification = Verification;
	if (CreateRagdollFixture(&WarmupState) != 0)
	{
		return 2;
	}
	return StepRagdollUntimed(&WarmupState, WarmupSteps);
}

int FormatRagdollPhysicsSettings(const CaseExecutionSpec& execution, const ThreadRuntimeState& RuntimeState,
                                 char* Settings, int Capacity)
{
	if (Settings == nullptr || Capacity <= 0)
	{
		return 2;
	}
	const int Size = std::snprintf(
	    Settings, Capacity,
	    "position_iterations=%u; velocity_iterations=%u; projection_iterations=%u; sleep=%s; ccd=%s; linked_collision=%s; linear_damping=%.9g; angular_damping=%.9g; unit_scale=100_chaos_units_per_meter; worker_count=%d",
	    execution.nativeSolver.values[CaseSolverField_PositionIterations],
	    execution.nativeSolver.values[CaseSolverField_VelocityIterations],
	    execution.nativeSolver.values[CaseSolverField_ProjectionIterations],
	    execution.sleepMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.continuousCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.ragdoll.linkedCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled",
	    execution.ragdoll.linearDamping, execution.ragdoll.angularDamping, RuntimeState.EffectiveChaosWorkerCount);
	return Size > 0 && Size < Capacity ? 0 : 2;
}

int RunRagdollHeadless(const RunnerArgs& Args, const ThreadRuntimeState& RuntimeState)
{
	if (Args.CaseExecution.fixtureKind != CaseFixtureKind_RagdollStairTumble ||
	    Args.WarmupSteps != static_cast<int>(Args.CaseExecution.warmupWorkUnitCount) ||
	    Args.StepCount != static_cast<int>(Args.CaseExecution.measuredWorkUnitCount) ||
	    RunRagdollWarmup(Args.CaseExecution, Args.WarmupSteps, RuntimeState, Args.Verification) != 0)
	{
		return 2;
	}
	ChaosRagdollCaseState State(Args.CaseExecution, RuntimeState);
	State.Verification = Args.Verification;
	ChaosCaseView RecordingState = {&State};
	if (CreateRagdollFixture(&State) != 0 || RecordChaosCase(Args, &RecordingState) != 0)
	{
		return 2;
	}
	const uint64 InvalidCount = CountRagdollInvalidTransforms(State);
	const bool CaseValid = RuntimeState.WorkerStatus == ThreadWorkerStatus::Ok;
	const bool MetricValid = State.CompletedStepCount == static_cast<int>(Args.CaseExecution.measuredWorkUnitCount) &&
	                         (Args.Verification == VerificationMode_Off || State.Quality.jointSampleCount != 0);
	char Settings[256] = {};
	if (FormatRagdollPhysicsSettings(Args.CaseExecution, RuntimeState, Settings, sizeof(Settings)) != 0)
	{
		return 2;
	}
	ChaosObservationRow Observations[kRagdollQualityObservationCount];
	if (Args.Verification == VerificationMode_On && State.Quality.jointSampleCount == 0)
		return 2;
	for (uint32 Index = 0; Args.Verification == VerificationMode_On && Index < kRagdollQualityObservationCount; ++Index)
	{
		const RagdollQualityObservation Value = RagdollQualityValue(State.Quality, Index);
		Observations[Index] = {Value.id, "final", static_cast<uint32>(State.CompletedStepCount),
		                       static_cast<ChaosObservationValueType>(Value.valueType), Value.bits};
	}
	const ChaosResult Result = {
	    Args.CaseExecution.fixtureSemantic,
	    Args.CaseExecution.fixtureRevision,
	    Settings,
	    static_cast<int>(Args.CaseExecution.bodyCount),
	    static_cast<int>(Args.CaseExecution.shapeCount),
	    static_cast<int>(Args.CaseExecution.queryCount),
	    static_cast<int>(Args.CaseExecution.constraintCount),
	    InvalidCount,
	    CaseValid ? "ok" : "invalid_result",
	    CaseValid && MetricValid ? "ok" : "invalid_result",
	    Args.ThreadCount,
	    RuntimeState.EffectiveChaosWorkerCount,
	    State.CompletedStepCount,
	    std::numeric_limits<double>::quiet_NaN(),
	    nullptr,
	    Observations,
	    Args.Verification == VerificationMode_On ? static_cast<int>(kRagdollQualityObservationCount) : 0,
	};
	return WriteChaosResult(Args, Result);
}

int StepRagdollVisual(ChaosCaseView* State, int WorkUnitCount)
{
	return State == nullptr || State->Handle == nullptr
	           ? 2
			   : StepRagdollQuality(static_cast<ChaosRagdollCaseState*>(State->Handle), WorkUnitCount);
}

int SampleRagdollVisualTransforms(const ChaosCaseView& State, ChaosVisualStableTransform* Transforms, int Capacity)
{
	if (State.Handle == nullptr || Transforms == nullptr)
	{
		return 2;
	}
	const ChaosRagdollCaseState& CaseState = *static_cast<const ChaosRagdollCaseState*>(State.Handle);
	if (Capacity < static_cast<int>(CaseState.CaseExecution.dynamicBodyCount))
	{
		return 2;
	}
	for (uint32 Index = 0; Index < CaseState.CaseExecution.dynamicBodyCount; ++Index)
	{
		const Chaos::FVec3 Position = ChaosUnitsToMeters(CaseState.DynamicBodies[Index]->GetX());
		const Chaos::FRotation3 Rotation = CaseState.DynamicBodies[Index]->GetR();
		Transforms[Index] = {
		    Index,
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

int BuildRagdollVisualScene(const ChaosCaseView& State, ChaosVisualGeometry* Geometries,

                            benchmark_visual::VisualMeshStorage* Meshes, int GeometryCapacity,
                            ChaosVisualInstance* Instances, int InstanceCapacity, int* GeometryCount,
                            int* InstanceCount)
{
	if (State.Handle == nullptr || Geometries == nullptr || Instances == nullptr || GeometryCount == nullptr ||
	    InstanceCount == nullptr)
	{
		return 2;
	}
	const ChaosRagdollCaseState& CaseState = *static_cast<const ChaosRagdollCaseState*>(State.Handle);
	const CaseExecutionSpec& CaseExecution = CaseState.CaseExecution;
	const CaseExecutionRagdoll& Fixture = CaseExecution.ragdoll;
	const int PartGeometryCount = RagdollPartGeometryCount(Fixture);
	if (Fixture.extraStaticBoxCount == 0 || GeometryCapacity < PartGeometryCount + 2 ||
	    InstanceCapacity < static_cast<int>(CaseExecution.visualInstanceCount))
	{
		return 2;
	}
	int NextGeometry = 0;
	for (uint16 PartIndex = 0; PartIndex < Fixture.partCount; ++PartIndex)
	{
		const uint32 GeometryIndex = RagdollGeometryIndex(Fixture, PartIndex);
		if (GeometryIndex != static_cast<uint32>(NextGeometry))
		{
			continue;
		}
		const CaseExecutionRagdollPart& Part = Fixture.parts[PartIndex];
		if (BuildResolvedVisualGeometry(CaseExecution, CaseExecutionPartGeometry(Part), Meshes,
		                                &Geometries[NextGeometry++]) != 0)
			return 2;
	}
	const uint32 StairGeometryIndex = static_cast<uint32>(NextGeometry);
	Geometries[NextGeometry++] = {
	    2u,
	    Fixture.stairHalfWidth,
	    Fixture.stairHalfHeight,
	    Fixture.stairHalfDepth,
	};
	const uint32 RunoutGeometryIndex = static_cast<uint32>(NextGeometry);
	const CaseExecutionBox& Runout = Fixture.extraStaticBoxes[0];
	Geometries[NextGeometry++] = {
	    2u,
	    Runout.halfExtents.x,
	    Runout.halfExtents.y,
	    Runout.halfExtents.z,
	};
	for (uint32 Index = 0; Index < CaseExecution.dynamicBodyCount; ++Index)
	{
		const Chaos::FVec3 Position = ChaosUnitsToMeters(CaseState.DynamicBodies[Index]->GetX());
		const Chaos::FRotation3 Rotation = CaseState.DynamicBodies[Index]->GetR();
		Instances[Index] = {
		    RagdollGeometryIndex(Fixture, static_cast<uint16>(Index % Fixture.partCount)),
		    Index,
		    Index,
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
	for (uint32 Row = 0; Row < Fixture.stairCount; ++Row)
	{
		const uint32 Slot = CaseExecution.dynamicBodyCount + Row;
		Instances[Slot] = {
		    StairGeometryIndex,
		    Slot,
		    MAX_uint32,
		    {
		        0.0f,
		        static_cast<float>(static_cast<double>(Fixture.stairCount - 1 - Row) * Fixture.stairRise -
				                   Fixture.stairHalfHeight),
		        static_cast<float>((static_cast<double>(Row) - 0.5 * static_cast<double>(Fixture.stairCount - 1)) *
				                   Fixture.stairDepth),
		        0.0f,
		        0.0f,
		        0.0f,
		        1.0f,
		    },
		};
	}
	const uint32 RunoutSlot = CaseExecution.visualInstanceCount - 1;
	Instances[RunoutSlot] = {
	    RunoutGeometryIndex,
	    RunoutSlot,
	    MAX_uint32,
	    {
	        Runout.center.x,
	        Runout.center.y,
	        Runout.center.z,
	        0.0f,
	        0.0f,
	        0.0f,
	        1.0f,
	    },
	};
	*GeometryCount = NextGeometry;
	*InstanceCount = static_cast<int>(CaseExecution.visualInstanceCount);
	return 0;
}

int BuildRagdollVisualDebugPrimitives(const ChaosCaseView& State, ChaosVisualDebugPrimitive* Primitives, int Capacity)
{
	if (State.Handle == nullptr || Primitives == nullptr)
	{
		return 2;
	}
	const ChaosRagdollCaseState& CaseState = *static_cast<const ChaosRagdollCaseState*>(State.Handle);
	const CaseExecutionSpec& CaseExecution = CaseState.CaseExecution;
	const CaseExecutionRagdoll& Fixture = CaseExecution.ragdoll;
	const int DebugCount = static_cast<int>(CaseExecution.visualDebugPrimitiveCount);
	if (DebugCount > Fixture.extraStaticBoxCount || Capacity < DebugCount)
	{
		return 2;
	}
	const int FirstDebugBox = Fixture.extraStaticBoxCount - DebugCount;
	for (int Index = 0; Index < DebugCount; ++Index)
	{
		const CaseExecutionBox& Box = Fixture.extraStaticBoxes[FirstDebugBox + Index];
		Primitives[Index] = {
		    2u,   6u, Box.center.x, Box.center.y, Box.center.z, Box.halfExtents.x, Box.halfExtents.y, Box.halfExtents.z,
		    0.0f, 0u,
		};
	}
	return 0;
}

}

ChaosRagdollCaseState::ChaosRagdollCaseState(const CaseExecutionSpec& InCaseExecution,
                                             const ThreadRuntimeState& RuntimeState)
    : CaseExecution(InCaseExecution), Particles(UniqueIndices),
      Evolution(Particles, PhysicalMaterials, nullptr, nullptr, nullptr, nullptr,
	            ThreadExecutionModeForThreadCount(RuntimeState.RequestedThreadCount) ==
	                ThreadExecutionMode::SingleThreaded)
{
	DynamicBodies.SetNumUninitialized(static_cast<int>(CaseExecution.dynamicBodyCount));

}

const ChaosCaseDescriptor& RagdollStairTumbleDescriptor()
{
	static const ChaosCaseDescriptor Descriptor = {
	    kEngineId,
	    RunRagdollHeadless,
	    StepRagdollVisual,
	    BuildRagdollVisualScene,
	    SampleRagdollVisualTransforms,
	    BuildRagdollVisualDebugPrimitives,
	};
	return Descriptor;
}

} // namespace BenchmarkPolygonChaos
