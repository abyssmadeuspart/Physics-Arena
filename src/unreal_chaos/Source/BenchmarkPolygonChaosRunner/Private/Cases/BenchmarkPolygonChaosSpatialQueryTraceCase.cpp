#include "Cases/BenchmarkPolygonChaosSpatialQueryTraceCase.h"

#include "Output/BenchmarkPolygonChaosOutput.h"
#include "Runner/BenchmarkPolygonChaosCli.h"
#include "Runtime/BenchmarkPolygonChaosConfig.h"
#include "Runtime/BenchmarkPolygonChaosUnits.h"

#include "Chaos/Box.h"
#include "Chaos/CollisionFilterData.h"
#include "Chaos/ISpatialAcceleration.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/ShapeInstance.h"
#include "Async/ParallelFor.h"
#include "HAL/PlatformTime.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace BenchmarkPolygonChaos
{
namespace
{
float CenteredGridCoordinate(float Base, float Spacing, uint32 Coordinate, uint32 Count)
{
	return Base + Spacing * (static_cast<float>(Coordinate) - 0.5f * static_cast<float>(Count - 1));
}

float UncenteredGridCoordinate(float Base, float Spacing, uint32 Coordinate)
{
	return Base + Spacing * static_cast<float>(Coordinate);
}

void GenerateSpatialQuery(const CaseExecutionSpec& CaseExecution, int Index, ChaosSpatialQuery* Query)
{
	const CaseExecutionSpatialQuery& Fixture = CaseExecution.spatialQuery;
	int FamilyIndex = Index;
	if (Index >= static_cast<int>(Fixture.rayCount + Fixture.sphereCastCount))
	{
		FamilyIndex -= static_cast<int>(Fixture.rayCount + Fixture.sphereCastCount);
	}
	else if (Index >= static_cast<int>(Fixture.rayCount))
	{
		FamilyIndex -= static_cast<int>(Fixture.rayCount);
	}
	const int Sample = FamilyIndex / 2;
	const int IntendedHit = (FamilyIndex & 1) == 0 ? 1 : 0;
	const int Slot = Sample % static_cast<int>(CaseExecution.staticBodyCount);
	const uint32 Ix = static_cast<uint32>(Slot) % Fixture.staticGrid[0];
	const uint32 Iz = (static_cast<uint32>(Slot) / Fixture.staticGrid[0]) % Fixture.staticGrid[2];
	const uint32 Iy = static_cast<uint32>(Slot) / (Fixture.staticGrid[0] * Fixture.staticGrid[2]);
	const float Center[3] = {
	    CenteredGridCoordinate(Fixture.staticBaseCenter.x, Fixture.staticSpacing.x, Ix, Fixture.staticGrid[0]),
	    UncenteredGridCoordinate(Fixture.staticBaseCenter.y, Fixture.staticSpacing.y, Iy),
	    CenteredGridCoordinate(Fixture.staticBaseCenter.z, Fixture.staticSpacing.z, Iz, Fixture.staticGrid[2]),
	};
	const float SceneMinimum[3] = {
	    CenteredGridCoordinate(Fixture.staticBaseCenter.x, Fixture.staticSpacing.x, 0, Fixture.staticGrid[0]) -
	        Fixture.staticHalfExtents.x,
	    Fixture.staticBaseCenter.y - Fixture.staticHalfExtents.y,
	    CenteredGridCoordinate(Fixture.staticBaseCenter.z, Fixture.staticSpacing.z, 0, Fixture.staticGrid[2]) -
	        Fixture.staticHalfExtents.z,
	};
	const float SceneMaximum[3] = {
	    CenteredGridCoordinate(Fixture.staticBaseCenter.x, Fixture.staticSpacing.x, Fixture.staticGrid[0] - 1,
		                       Fixture.staticGrid[0]) +
	        Fixture.staticHalfExtents.x,
	    UncenteredGridCoordinate(Fixture.staticBaseCenter.y, Fixture.staticSpacing.y, Fixture.staticGrid[1] - 1) +
	        Fixture.staticHalfExtents.y,
	    CenteredGridCoordinate(Fixture.staticBaseCenter.z, Fixture.staticSpacing.z, Fixture.staticGrid[2] - 1,
		                       Fixture.staticGrid[2]) +
	        Fixture.staticHalfExtents.z,
	};
	const int Face = Sample % 6;
	const int Axis = Face / 2;
	if (Index >= static_cast<int>(Fixture.rayCount + Fixture.sphereCastCount))
	{
		float OverlapCenter[3] = {Center[0], Center[1], Center[2]};
		if (IntendedHit == 0)
		{
			OverlapCenter[Axis] = SceneMaximum[Axis] + Fixture.missOffset;
		}
		*Query = {
		    OverlapCenter[0], OverlapCenter[1], OverlapCenter[2], 0.0f, 0.0f, 0.0f,
		};
		return;
	}

	const int FaceSign = (Face & 1) != 0 ? 1 : -1;
	const int FirstTransverse = Axis == 0 ? 1 : 0;
	float Origin[3] = {};
	float Direction[3] = {};
	for (int Component = 0; Component < 3; ++Component)
	{
		if (Component == Axis)
		{
			Origin[Component] = FaceSign > 0 ? SceneMaximum[Component] + 5.0f : SceneMinimum[Component] - 5.0f;
			Direction[Component] = FaceSign > 0 ? -1.0f : 1.0f;
		}
		else
		{
			Origin[Component] = IntendedHit == 0 && Component == FirstTransverse
			                        ? SceneMaximum[Component] + Fixture.missOffset
			                        : Center[Component];
		}
	}
	*Query = {
	    Origin[0], Origin[1], Origin[2], Direction[0], Direction[1], Direction[2],
	};
}

void SetSpatialQueryParticleBounds(Chaos::FGeometryParticleHandle* Particle, const Chaos::FVec3& HalfExtents)
{
	Particle->SetLocalBounds(Chaos::FAABB3(-HalfExtents, HalfExtents));
	Particle->UpdateWorldSpaceState(Chaos::FRigidTransform3(Particle->GetX(), Particle->GetR()), Chaos::FVec3(0));
	Particle->SetHasBounds(true);
}

void SetSpatialQueryParticleShapesToCollide(Chaos::FGeometryParticleHandle* Particle)
{
	const Chaos::Filter::FShapeFilterData ShapeFilterData =
	    Chaos::Filter::FShapeFilterBuilder::BuildBlockAll(Chaos::EFilterFlags::SimpleCollision);
	for (const TUniquePtr<Chaos::FPerShapeData>& Shape : Particle->ShapesArray())
	{
		Shape->SetShapeFilterData(ShapeFilterData);
	}
}

class ClosestSpatialQueryVisitor final
    : public Chaos::ISpatialVisitor<Chaos::FAccelerationStructureHandle, Chaos::FReal>
{
  public:
	ClosestSpatialQueryVisitor(const Chaos::FVec3& InStart, const Chaos::FVec3& InDirection, Chaos::FReal InThickness,
	                           Chaos::FReal InMaximumDistance)
	    : Start(InStart), Direction(InDirection), Thickness(InThickness),
	      HitDistance(static_cast<float>(InMaximumDistance))
	{
	}

	bool Overlap(const Chaos::TSpatialVisitorData<Chaos::FAccelerationStructureHandle>&) override
	{
		return true;
	}

	bool Raycast(const Chaos::TSpatialVisitorData<Chaos::FAccelerationStructureHandle>& Instance,
	             Chaos::FQueryFastData& CurrentData) override
	{
		Visit(Instance, CurrentData);
		return true;
	}

	bool Sweep(const Chaos::TSpatialVisitorData<Chaos::FAccelerationStructureHandle>& Instance,
	           Chaos::FQueryFastData& CurrentData) override
	{
		Visit(Instance, CurrentData);
		return true;
	}

	uint8 Hit = 0;
	float HitDistance;

	void Visit(const Chaos::TSpatialVisitorData<Chaos::FAccelerationStructureHandle>& Instance,
	           Chaos::FQueryFastData& CurrentData)
	{
		Chaos::FGeometryParticleHandle* Particle = Instance.Payload.GetGeometryParticleHandle_PhysicsThread();
		if (Particle == nullptr || Particle->GetGeometry() == nullptr)
		{
			return;
		}

		const Chaos::FRigidTransform3 Transform = Particle->GetTransformXR();
		const Chaos::FVec3 LocalStart = Transform.InverseTransformPosition(Start);
		const Chaos::FVec3 LocalDirection = Transform.InverseTransformVectorNoScale(Direction);
		Chaos::FReal Time = CurrentData.CurrentLength;
		Chaos::FVec3 Position(0);
		Chaos::FVec3 Normal(0);
		int32 FaceIndex = INDEX_NONE;
		if (Particle->GetGeometry()->Raycast(LocalStart, LocalDirection, CurrentData.CurrentLength, Thickness, Time,
		                                     Position, Normal, FaceIndex))
		{
			Hit = 1;
			HitDistance = static_cast<float>(Time);
			CurrentData.SetLength(Time);
		}
	}

	Chaos::FVec3 Start;
	Chaos::FVec3 Direction;
	Chaos::FReal Thickness;
};

class AnySpatialOverlapVisitor final : public Chaos::ISpatialVisitor<Chaos::FAccelerationStructureHandle, Chaos::FReal>
{
  public:
	bool Overlap(const Chaos::TSpatialVisitorData<Chaos::FAccelerationStructureHandle>&) override
	{
		Hit = 1;
		return false;
	}

	bool Raycast(const Chaos::TSpatialVisitorData<Chaos::FAccelerationStructureHandle>&,
	             Chaos::FQueryFastData&) override
	{
		return true;
	}

	bool Sweep(const Chaos::TSpatialVisitorData<Chaos::FAccelerationStructureHandle>&, Chaos::FQueryFastData&) override
	{
		return true;
	}

	uint8 Hit = 0;
};

int CaptureSpatialQueryTraceDebugSamples(ChaosSpatialQueryTraceState* State)
{
	const CaseExecutionSpatialQuery& Fixture = State->CaseExecution.spatialQuery;
	const int Samples = static_cast<int>(Fixture.debugSamplesPerFamily);
	Chaos::ISpatialAccelerationCollection<Chaos::FAccelerationStructureHandle, Chaos::FReal, 3>* SpatialAcceleration =
	    State->Evolution.GetSpatialAcceleration();
	if (SpatialAcceleration == nullptr)
	{
		return 2;
	}
	for (int Index = 0; Index < Samples; ++Index)
	{
		const ChaosDirectionalQueryInput& Input = State->RayInputs[Index];
		ClosestSpatialQueryVisitor Visitor(Input.Origin, Input.Direction, 0.0,
		                                   MetersToChaosUnits(Fixture.queryDistance));
		SpatialAcceleration->Raycast(Input.Origin, Input.Direction, MetersToChaosUnits(Fixture.queryDistance), Visitor);
		State->DebugHits[Index] = Visitor.Hit;
		State->DebugHitDistances[Index] = static_cast<float>(ChaosUnitsToMeters(Visitor.HitDistance));
	}
	for (int Index = 0; Index < Samples; ++Index)
	{
		const ChaosDirectionalQueryInput& Input = State->SphereCastInputs[Index];
		ClosestSpatialQueryVisitor Visitor(Input.Origin, Input.Direction, MetersToChaosUnits(Fixture.sphereCastRadius),
		                                   MetersToChaosUnits(Fixture.queryDistance));
		SpatialAcceleration->Sweep(Input.Origin, Input.Direction, MetersToChaosUnits(Fixture.queryDistance),
		                           State->SphereHalfExtents, Visitor);
		const int DebugIndex = Samples + Index;
		State->DebugHits[DebugIndex] = Visitor.Hit;
		State->DebugHitDistances[DebugIndex] = static_cast<float>(ChaosUnitsToMeters(Visitor.HitDistance));
	}
	for (int Index = 0; Index < Samples; ++Index)
	{
		AnySpatialOverlapVisitor Visitor;
		SpatialAcceleration->Overlap(State->OverlapInputs[Index].Bounds, Visitor);
		const int DebugIndex = 2 * Samples + Index;
		State->DebugHits[DebugIndex] = Visitor.Hit;
		State->DebugHitDistances[DebugIndex] = Fixture.queryDistance;
	}
	return 0;
}

int CreateSpatialQueryTraceState(ChaosSpatialQueryTraceState* State, benchmark_replay::RecordingMode RecordingMode)
{
	if (State == nullptr)
	{
		return 2;
	}

	const CaseExecutionSpec& CaseExecution = State->CaseExecution;
	const CaseExecutionSpatialQuery& Fixture = CaseExecution.spatialQuery;
	if (State->ThreadCount <= 0 || Fixture.rayCount < static_cast<uint32>(State->ThreadCount) ||
	    Fixture.sphereCastCount < static_cast<uint32>(State->ThreadCount) ||
	    Fixture.overlapCount < static_cast<uint32>(State->ThreadCount))
	{
		return 2;
	}
	State->Evolution.GetGravityForces().SetAcceleration(
	    MetersToChaosUnits(Chaos::FVec3(CaseExecution.gravity.x, CaseExecution.gravity.y, CaseExecution.gravity.z)), 0);
	State->Material = MakeUnique<Chaos::FChaosPhysicsMaterial>();
	State->Material->Friction = CaseExecution.friction;
	State->Material->Restitution = CaseExecution.restitution;

	State->RayInputs = MakeUnique<ChaosDirectionalQueryInput[]>(static_cast<int>(Fixture.rayCount));
	State->SphereCastInputs = MakeUnique<ChaosDirectionalQueryInput[]>(static_cast<int>(Fixture.sphereCastCount));
	State->OverlapInputs = MakeUnique<ChaosOverlapQueryInput[]>(static_cast<int>(Fixture.overlapCount));
	if (RecordingMode == benchmark_replay::RecordingMode_On)
	{
		State->DebugHits = MakeUnique<uint8[]>(static_cast<int>(CaseExecution.visualDebugPrimitiveCount));
		State->DebugHitDistances = MakeUnique<float[]>(static_cast<int>(CaseExecution.visualDebugPrimitiveCount));
	}
	State->RawBatchCycles = MakeUnique<uint64[]>(static_cast<int>(CaseExecution.measuredWorkUnitCount));
	State->LaneHitCounts = MakeUnique<uint64[]>(State->ThreadCount);
	if (!State->RayInputs || !State->SphereCastInputs || !State->OverlapInputs ||
	    (RecordingMode == benchmark_replay::RecordingMode_On && (!State->DebugHits || !State->DebugHitDistances)) ||
	    !State->RawBatchCycles || !State->LaneHitCounts)
	{
		return 2;
	}
	for (uint32 Index = 0; Index < Fixture.rayCount; ++Index)
	{
		ChaosSpatialQuery Query = {};
		GenerateSpatialQuery(CaseExecution, static_cast<int>(Index), &Query);
		State->RayInputs[Index] = {
		    MetersToChaosUnits(Chaos::FVec3(Query.OriginOrCenterX, Query.OriginOrCenterY, Query.OriginOrCenterZ)),
		    Chaos::FVec3(Query.DirectionX, Query.DirectionY, Query.DirectionZ),
		};
	}
	for (int Index = 0; Index < static_cast<int>(Fixture.sphereCastCount); ++Index)
	{
		ChaosSpatialQuery Query = {};
		GenerateSpatialQuery(CaseExecution, static_cast<int>(Fixture.rayCount) + Index, &Query);
		State->SphereCastInputs[Index] = {
		    MetersToChaosUnits(Chaos::FVec3(Query.OriginOrCenterX, Query.OriginOrCenterY, Query.OriginOrCenterZ)),
		    Chaos::FVec3(Query.DirectionX, Query.DirectionY, Query.DirectionZ),
		};
	}
	const Chaos::FVec3 OverlapHalfExtents = MetersToChaosUnits(
	    Chaos::FVec3(Fixture.overlapHalfExtents.x, Fixture.overlapHalfExtents.y, Fixture.overlapHalfExtents.z));
	for (int Index = 0; Index < static_cast<int>(Fixture.overlapCount); ++Index)
	{
		ChaosSpatialQuery Query = {};
		GenerateSpatialQuery(CaseExecution, static_cast<int>(Fixture.rayCount + Fixture.sphereCastCount) + Index,
		                     &Query);
		const Chaos::FVec3 Center =
		    MetersToChaosUnits(Chaos::FVec3(Query.OriginOrCenterX, Query.OriginOrCenterY, Query.OriginOrCenterZ));
		State->OverlapInputs[Index] = {
		    Chaos::FAABB3(Center - OverlapHalfExtents, Center + OverlapHalfExtents),
		    Center,
		};
	}
	for (int Index = 0; RecordingMode == benchmark_replay::RecordingMode_On &&
	                    Index < static_cast<int>(CaseExecution.visualDebugPrimitiveCount);
	     ++Index)
	{
		State->DebugHits[Index] = 0;
		State->DebugHitDistances[Index] = Fixture.queryDistance;
	}
	State->SphereHalfExtents =
	    MetersToChaosUnits(Chaos::FVec3(Fixture.sphereCastRadius, Fixture.sphereCastRadius, Fixture.sphereCastRadius));

	// Chaos returns created particles through TArray, and the scene owns them
	TArray<Chaos::FGeometryParticleHandle*> StaticBodies =
	    State->Evolution.CreateStaticParticles(static_cast<int>(CaseExecution.staticBodyCount));
	State->StaticBodyCount = StaticBodies.Num();
	if (State->StaticBodyCount != static_cast<int>(CaseExecution.staticBodyCount))
	{
		return 2;
	}
	ChaosResolvedShape Shape;
	if (CreateChaosResolvedShape(CaseExecution, CaseExecution.selectedGeometry, &Shape) != 0)
		return 2;
	for (uint32 Iy = 0; Iy < Fixture.staticGrid[1]; ++Iy)
	{
		for (uint32 Iz = 0; Iz < Fixture.staticGrid[2]; ++Iz)
		{
			for (uint32 Ix = 0; Ix < Fixture.staticGrid[0]; ++Ix)
			{
				const uint32 Slot = (Iy * Fixture.staticGrid[2] + Iz) * Fixture.staticGrid[0] + Ix;
				Chaos::FGeometryParticleHandle* Particle = StaticBodies[Slot];
				Particle->SetGeometry(Shape.Geometry);
				Particle->SetX(MetersToChaosUnits(
				    Chaos::FVec3(CenteredGridCoordinate(Fixture.staticBaseCenter.x, Fixture.staticSpacing.x, Ix,
					                                    Fixture.staticGrid[0]),
					             UncenteredGridCoordinate(Fixture.staticBaseCenter.y, Fixture.staticSpacing.y, Iy),
					             CenteredGridCoordinate(Fixture.staticBaseCenter.z, Fixture.staticSpacing.z, Iz,
					                                    Fixture.staticGrid[2]))));
				Particle->SetR(Shape.BodyRotation);
				SetSpatialQueryParticleBounds(Particle, Shape.HalfExtents);
				SetSpatialQueryParticleShapesToCollide(Particle);
				State->Evolution.SetPhysicsMaterial(Particle, MakeSerializable(State->Material));
				State->Evolution.EnableParticle(Particle);
			}
		}
	}
	State->Evolution.FlushSpatialAcceleration();
	return RecordingMode == benchmark_replay::RecordingMode_On ? CaptureSpatialQueryTraceDebugSamples(State) : 0;
}

enum class SpatialQueryFamily : uint8
{
	Ray,
	SphereCast,
	Overlap,
};

uint64 ExecuteSpatialQueryLane(
    ChaosSpatialQueryTraceState* State,
    Chaos::ISpatialAccelerationCollection<Chaos::FAccelerationStructureHandle, Chaos::FReal, 3>* SpatialAcceleration,
    SpatialQueryFamily Family, int LaneIndex)
{
	const CaseExecutionSpatialQuery& Fixture = State->CaseExecution.spatialQuery;
	const uint32 QueryCount = Family == SpatialQueryFamily::Ray          ? Fixture.rayCount
	                          : Family == SpatialQueryFamily::SphereCast ? Fixture.sphereCastCount
	                                                                     : Fixture.overlapCount;
	const uint32 Start = static_cast<uint32>(static_cast<uint64>(QueryCount) * static_cast<uint32>(LaneIndex) /
	                                         static_cast<uint32>(State->ThreadCount));
	const uint32 End = static_cast<uint32>(static_cast<uint64>(QueryCount) * static_cast<uint32>(LaneIndex + 1) /
	                                       static_cast<uint32>(State->ThreadCount));
	uint64 HitCount = 0;
	if (Family == SpatialQueryFamily::Ray)
	{
		for (uint32 Index = Start; Index < End; ++Index)
		{
			const ChaosDirectionalQueryInput& Input = State->RayInputs[Index];
			ClosestSpatialQueryVisitor Visitor(Input.Origin, Input.Direction, 0.0,
			                                   MetersToChaosUnits(Fixture.queryDistance));
			SpatialAcceleration->Raycast(Input.Origin, Input.Direction, MetersToChaosUnits(Fixture.queryDistance),
			                             Visitor);
			HitCount += Visitor.Hit;
		}
	}
	else if (Family == SpatialQueryFamily::SphereCast)
	{
		for (uint32 Index = Start; Index < End; ++Index)
		{
			const ChaosDirectionalQueryInput& Input = State->SphereCastInputs[Index];
			ClosestSpatialQueryVisitor Visitor(Input.Origin, Input.Direction,
			                                   MetersToChaosUnits(Fixture.sphereCastRadius),
			                                   MetersToChaosUnits(Fixture.queryDistance));
			SpatialAcceleration->Sweep(Input.Origin, Input.Direction, MetersToChaosUnits(Fixture.queryDistance),
			                           State->SphereHalfExtents, Visitor);
			HitCount += Visitor.Hit;
		}
	}
	else
	{
		for (uint32 Index = Start; Index < End; ++Index)
		{
			AnySpatialOverlapVisitor Visitor;
			SpatialAcceleration->Overlap(State->OverlapInputs[Index].Bounds, Visitor);
			HitCount += Visitor.Hit;
		}
	}
	return HitCount;
}

uint64 DispatchSpatialQuery(
    ChaosSpatialQueryTraceState* State,
    Chaos::ISpatialAccelerationCollection<Chaos::FAccelerationStructureHandle, Chaos::FReal, 3>* SpatialAcceleration,
    SpatialQueryFamily Family)
{
	if (State->ThreadCount == 1)
	{
		return ExecuteSpatialQueryLane(State, SpatialAcceleration, Family, 0);
	}
	ParallelFor(State->ThreadCount,
	            [State, SpatialAcceleration, Family](int LaneIndex)
	            {
		            State->LaneHitCounts[LaneIndex] =
		                ExecuteSpatialQueryLane(State, SpatialAcceleration, Family, LaneIndex);
	            });
	uint64 HitCount = 0;
	for (int LaneIndex = 0; LaneIndex < State->ThreadCount; ++LaneIndex)
	{
		HitCount += State->LaneHitCounts[LaneIndex];
	}
	return HitCount;
}

void ExecuteSpatialQueryTraceBatch(ChaosSpatialQueryTraceState* State, SpatialQueryBatchPhase Phase,
                                   uint64* BatchCycles)
{
	Chaos::ISpatialAccelerationCollection<Chaos::FAccelerationStructureHandle, Chaos::FReal, 3>* SpatialAcceleration =
	    State->Evolution.GetSpatialAcceleration();
	const uint64 BatchStart = FPlatformTime::Cycles64();
	const uint64 RayStart = FPlatformTime::Cycles64();
	State->RayHitCount = DispatchSpatialQuery(State, SpatialAcceleration, SpatialQueryFamily::Ray);
	const uint64 RayEnd = FPlatformTime::Cycles64();

	const uint64 SphereCastStart = FPlatformTime::Cycles64();
	State->SphereCastHitCount = DispatchSpatialQuery(State, SpatialAcceleration, SpatialQueryFamily::SphereCast);
	const uint64 SphereCastEnd = FPlatformTime::Cycles64();

	const uint64 OverlapStart = FPlatformTime::Cycles64();
	State->OverlapHitCount = DispatchSpatialQuery(State, SpatialAcceleration, SpatialQueryFamily::Overlap);
	const uint64 OverlapEnd = FPlatformTime::Cycles64();
	const uint64 BatchEnd = FPlatformTime::Cycles64();
	if (Phase == SpatialQueryBatchPhase::Measured)
	{
		State->RayElapsedCycles += RayEnd - RayStart;
		State->SphereCastElapsedCycles += SphereCastEnd - SphereCastStart;
		State->OverlapElapsedCycles += OverlapEnd - OverlapStart;
		*BatchCycles = BatchEnd - BatchStart;
	}
}

int CheckSpatialQueryBatch(const ChaosSpatialQueryTraceState* State, const char* Phase, int Batch)
{
	const CaseExecutionSpatialQuery& Fixture = State->CaseExecution.spatialQuery;
	for (int Family = 0; Family < 3; ++Family)
	{
		const uint32 Count =
		    Family == 0 ? Fixture.rayCount : (Family == 1 ? Fixture.sphereCastCount : Fixture.overlapCount);
		const uint64 Expected = Count / 2 + Count % 2;
		const uint64 Actual =
		    Family == 0 ? State->RayHitCount : (Family == 1 ? State->SphereCastHitCount : State->OverlapHitCount);
		if (Actual != Expected)
		{
			std::fprintf(
			    stderr,
			    "run_failed reason=query_batch engine=unreal_chaos phase=%s batch=%d family=%s expected=%llu actual=%llu\n",
			    Phase, Batch, Family == 0 ? "ray" : (Family == 1 ? "sphere_cast" : "overlap"),
			    static_cast<unsigned long long>(Expected), static_cast<unsigned long long>(Actual));
			return 2;
		}
	}
	return 0;
}

int WarmupSpatialQueryTrace(ChaosSpatialQueryTraceState* State, int BatchCount)
{
	if (State == nullptr || BatchCount != static_cast<int>(State->CaseExecution.warmupWorkUnitCount))
	{
		return 2;
	}
	for (int Batch = 0; Batch < BatchCount; ++Batch)
	{
		ExecuteSpatialQueryTraceBatch(State, SpatialQueryBatchPhase::Warmup, nullptr);
		if (CheckSpatialQueryBatch(State, "warmup", Batch) != 0)
			return 2;
	}
	return 0;
}

int StepSpatialQueryTraceTimed(ChaosSpatialQueryTraceState* State, int BatchCount)
{
	if (State == nullptr || BatchCount < 0 ||
	    BatchCount > static_cast<int>(State->CaseExecution.measuredWorkUnitCount) - State->CompletedBatchCount)
	{
		return 2;
	}
	for (int Batch = 0; Batch < BatchCount; ++Batch)
	{
		const int Slot = State->CompletedBatchCount + Batch;
		ExecuteSpatialQueryTraceBatch(State, SpatialQueryBatchPhase::Measured, &State->RawBatchCycles[Slot]);
		if (CheckSpatialQueryBatch(State, "measured", Slot) != 0)
			return 2;
		const double Milliseconds = FPlatformTime::ToMilliseconds64(State->RawBatchCycles[Slot]);
		State->WorkloadElapsedMs += Milliseconds;
		State->LatestBatchElapsedMs = Milliseconds;
	}
	State->CompletedBatchCount += BatchCount;
	return 0;
}

double QueryRate(int QueryCount, int BatchCount, uint64 ElapsedCycles)
{
	const double Seconds = FPlatformTime::ToMilliseconds64(ElapsedCycles) / 1000.0;
	return static_cast<double>(QueryCount) * static_cast<double>(BatchCount) / Seconds;
}

uint64 DoubleBits(double Value)
{
	uint64 Bits = 0;
	std::memcpy(&Bits, &Value, sizeof(Bits));
	return Bits;
}

void BuildHeadlessObservations(const ChaosSpatialQueryTraceState& State,
                               std::array<ChaosObservationRow, 6>* Observations)
{
	const CaseExecutionSpatialQuery& Fixture = State.CaseExecution.spatialQuery;
	(*Observations)[0] = {
	    "ray_queries_per_second",
	    "final",
	    0,
	    ChaosObservationValueType::Float64,
	    DoubleBits(QueryRate(static_cast<int>(Fixture.rayCount), State.CompletedBatchCount, State.RayElapsedCycles)),
	};
	(*Observations)[1] = {
	    "sphere_cast_queries_per_second",
	    "final",
	    0,
	    ChaosObservationValueType::Float64,
	    DoubleBits(QueryRate(static_cast<int>(Fixture.sphereCastCount), State.CompletedBatchCount,
		                     State.SphereCastElapsedCycles)),
	};
	(*Observations)[2] = {
	    "overlap_queries_per_second",
	    "final",
	    0,
	    ChaosObservationValueType::Float64,
	    DoubleBits(
	        QueryRate(static_cast<int>(Fixture.overlapCount), State.CompletedBatchCount, State.OverlapElapsedCycles)),
	};
	(*Observations)[3] = {
	    "ray_hit_count", "final", 0, ChaosObservationValueType::Uint64, State.RayHitCount,
	};
	(*Observations)[4] = {
	    "sphere_cast_hit_count", "final", 0, ChaosObservationValueType::Uint64, State.SphereCastHitCount,
	};
	(*Observations)[5] = {
	    "overlap_hit_count", "final", 0, ChaosObservationValueType::Uint64, State.OverlapHitCount,
	};
}

int FormatSpatialQueryTraceSettings(const CaseExecutionSpec& CaseExecution, const ThreadRuntimeState& RuntimeState,
                                    char* Settings, int Capacity)
{
	if (Settings == nullptr || Capacity <= 0)
	{
		return 2;
	}
	const int Size =
	    std::snprintf(Settings, Capacity,
		              "query_world=static_only; unit_scale=100_chaos_units_per_meter; worker_count=%d; rays=%u; "
		              "sphere_casts=%u; overlaps=%u",
		              RuntimeState.EffectiveChaosWorkerCount, CaseExecution.spatialQuery.rayCount,
		              CaseExecution.spatialQuery.sphereCastCount, CaseExecution.spatialQuery.overlapCount);
	return Size > 0 && Size < Capacity ? 0 : 2;
}

int RunSpatialQueryTraceHeadless(const RunnerArgs& Args, const ThreadRuntimeState& RuntimeState)
{
	ChaosSpatialQueryTraceState State(Args.CaseExecution, RuntimeState);
	ChaosCaseView RecordingState = {&State};
	if (CreateSpatialQueryTraceState(&State, Args.RecordingMode) != 0)
	{
		std::fprintf(stderr, "run_failed reason=create_fixture\n");
		return 2;
	}
	int Status = WarmupSpatialQueryTrace(&State, Args.WarmupSteps);
	if (Status == 0)
	{
		Status = RecordChaosCase(Args, &RecordingState);
	}
	if (Status != 0)
	{
		return Status;
	}

	const int CaseValidity = RuntimeState.WorkerStatus == ThreadWorkerStatus::Ok &&
	                                 State.StaticBodyCount == static_cast<int>(Args.CaseExecution.staticBodyCount)
	                             ? 1
	                             : 0;
	const int MetricValidity = State.CompletedBatchCount == Args.StepCount && State.WorkloadElapsedMs > 0.0 &&
	                                   std::isfinite(State.WorkloadElapsedMs) && State.RayElapsedCycles > 0 &&
	                                   State.SphereCastElapsedCycles > 0 && State.OverlapElapsedCycles > 0
	                               ? 1
	                               : 0;
	char Settings[256] = {};
	if (FormatSpatialQueryTraceSettings(Args.CaseExecution, RuntimeState, Settings, sizeof(Settings)) != 0)
	{
		return 2;
	}
	std::array<ChaosObservationRow, 6> Observations = {};
	BuildHeadlessObservations(State, &Observations);
	// ChaosResult's shared writer contract requires TArray timing rows
	TArray<uint64> RawBatchCycles;
	RawBatchCycles.SetNumUninitialized(static_cast<int>(Args.CaseExecution.measuredWorkUnitCount));
	for (int Index = 0; Index < static_cast<int>(Args.CaseExecution.measuredWorkUnitCount); ++Index)
	{
		RawBatchCycles[Index] = State.RawBatchCycles[Index];
	}
	const ChaosResult Result = {
	    Args.CaseExecution.fixtureSemantic,
	    Args.CaseExecution.fixtureRevision,
	    Settings,
	    static_cast<int>(Args.CaseExecution.bodyCount),
	    static_cast<int>(Args.CaseExecution.shapeCount),
	    static_cast<int>(Args.CaseExecution.queryCount),
	    static_cast<int>(Args.CaseExecution.constraintCount),
	    0,
	    CaseValidity != 0 ? "ok" : "invalid_result",
	    CaseValidity != 0 && MetricValidity != 0 ? "ok" : "invalid_result",
	    Args.ThreadCount,
	    RuntimeState.EffectiveChaosWorkerCount,
	    State.CompletedBatchCount,
	    State.WorkloadElapsedMs,
	    &RawBatchCycles,
	    Observations.data(),
	    static_cast<int>(Observations.size()),
	};
	return WriteChaosResult(Args, Result);
}

int StepSpatialQueryTraceVisual(ChaosCaseView* State, int WorkUnitCount)
{
	return State == nullptr || State->Handle == nullptr
	           ? 2
			   : StepSpatialQueryTraceTimed(static_cast<ChaosSpatialQueryTraceState*>(State->Handle), WorkUnitCount);
}

int BuildSpatialQueryTraceVisualScene(const ChaosCaseView& State, ChaosVisualGeometry* Geometries,

                                      benchmark_visual::VisualMeshStorage* Meshes, int GeometryCapacity,
                                      ChaosVisualInstance* Instances, int InstanceCapacity, int* GeometryCount,
                                      int* InstanceCount)
{
	if (State.Handle == nullptr || Geometries == nullptr || Instances == nullptr || GeometryCount == nullptr ||
	    InstanceCount == nullptr)
	{
		return 2;
	}
	const ChaosSpatialQueryTraceState& CaseState = *static_cast<const ChaosSpatialQueryTraceState*>(State.Handle);
	const CaseExecutionSpec& CaseExecution = CaseState.CaseExecution;
	const CaseExecutionSpatialQuery& Fixture = CaseExecution.spatialQuery;
	if (GeometryCapacity < 1 || InstanceCapacity < static_cast<int>(CaseExecution.staticBodyCount))
		return 2;
	if (BuildResolvedVisualGeometry(CaseExecution, CaseExecution.selectedGeometry, Meshes, &Geometries[0]) != 0)
		return 2;
	const CaseExecutionQuaternion Rotation = CaseExecutionAxisRotation(CaseExecution.selectedGeometry.axis);
	for (uint32 Iy = 0; Iy < Fixture.staticGrid[1]; ++Iy)
	{
		for (uint32 Iz = 0; Iz < Fixture.staticGrid[2]; ++Iz)
		{
			for (uint32 Ix = 0; Ix < Fixture.staticGrid[0]; ++Ix)
			{
				const uint32 Slot = (Iy * Fixture.staticGrid[2] + Iz) * Fixture.staticGrid[0] + Ix;
				Instances[Slot] = {
				    0u,
				    static_cast<uint32>(Slot),
				    MAX_uint32,
				    {
				        CenteredGridCoordinate(Fixture.staticBaseCenter.x, Fixture.staticSpacing.x, Ix,
						                       Fixture.staticGrid[0]),
				        UncenteredGridCoordinate(Fixture.staticBaseCenter.y, Fixture.staticSpacing.y, Iy),
				        CenteredGridCoordinate(Fixture.staticBaseCenter.z, Fixture.staticSpacing.z, Iz,
						                       Fixture.staticGrid[2]),
				        Rotation.x,
				        Rotation.y,
				        Rotation.z,
				        Rotation.w,
				    },
				};
			}
		}
	}
	*GeometryCount = 1;
	*InstanceCount = static_cast<int>(CaseExecution.staticBodyCount);
	return 0;
}

int SampleNoSpatialQueryTraceTransforms(const ChaosCaseView& State, ChaosVisualStableTransform*, int Capacity)
{
	return State.Handle != nullptr && Capacity == 0 ? 0 : 2;
}

int BuildSpatialQueryTraceDebugPrimitives(const ChaosCaseView& State, ChaosVisualDebugPrimitive* Primitives,
                                          int Capacity)
{
	if (State.Handle == nullptr || Primitives == nullptr)
	{
		return 2;
	}
	const ChaosSpatialQueryTraceState& CaseState = *static_cast<const ChaosSpatialQueryTraceState*>(State.Handle);
	const CaseExecutionSpec& CaseExecution = CaseState.CaseExecution;
	const CaseExecutionSpatialQuery& Fixture = CaseExecution.spatialQuery;
	const int Samples = static_cast<int>(Fixture.debugSamplesPerFamily);
	if (Capacity < static_cast<int>(CaseExecution.visualDebugPrimitiveCount))
		return 2;
	for (int Local = 0; Local < Samples; ++Local)
	{
		for (int Family = 0; Family < 3; ++Family)
		{
			const int PrimitiveIndex = Family * Samples + Local;
			ChaosVisualDebugPrimitive& Primitive = Primitives[PrimitiveIndex];
			Primitive = {};
			Primitive.Kind = static_cast<uint32>(Family);
			Primitive.MaterialIndex = CaseState.DebugHits[PrimitiveIndex] != 0 ? 6u : 7u;
			if (Family == 0)
			{
				const ChaosDirectionalQueryInput& Input = CaseState.RayInputs[Local];
				const Chaos::FVec3 Origin = ChaosUnitsToMeters(Input.Origin);
				const float Distance = CaseState.DebugHitDistances[PrimitiveIndex];
				Primitive.OriginOrCenterX = static_cast<float>(Origin.X);
				Primitive.OriginOrCenterY = static_cast<float>(Origin.Y);
				Primitive.OriginOrCenterZ = static_cast<float>(Origin.Z);
				Primitive.EndOrHalfExtentsX = static_cast<float>(Origin.X + Input.Direction.X * Distance);
				Primitive.EndOrHalfExtentsY = static_cast<float>(Origin.Y + Input.Direction.Y * Distance);
				Primitive.EndOrHalfExtentsZ = static_cast<float>(Origin.Z + Input.Direction.Z * Distance);
			}
			else if (Family == 1)
			{
				const ChaosDirectionalQueryInput& Input = CaseState.SphereCastInputs[Local];
				const Chaos::FVec3 Origin = ChaosUnitsToMeters(Input.Origin);
				const float Distance = CaseState.DebugHitDistances[PrimitiveIndex];
				Primitive.OriginOrCenterX = static_cast<float>(Origin.X);
				Primitive.OriginOrCenterY = static_cast<float>(Origin.Y);
				Primitive.OriginOrCenterZ = static_cast<float>(Origin.Z);
				Primitive.EndOrHalfExtentsX = static_cast<float>(Origin.X + Input.Direction.X * Distance);
				Primitive.EndOrHalfExtentsY = static_cast<float>(Origin.Y + Input.Direction.Y * Distance);
				Primitive.EndOrHalfExtentsZ = static_cast<float>(Origin.Z + Input.Direction.Z * Distance);
				Primitive.Radius = Fixture.sphereCastRadius;
			}
			else
			{
				const Chaos::FVec3 Center = ChaosUnitsToMeters(CaseState.OverlapInputs[Local].Center);
				Primitive.OriginOrCenterX = static_cast<float>(Center.X);
				Primitive.OriginOrCenterY = static_cast<float>(Center.Y);
				Primitive.OriginOrCenterZ = static_cast<float>(Center.Z);
				Primitive.EndOrHalfExtentsX = Fixture.overlapHalfExtents.x;
				Primitive.EndOrHalfExtentsY = Fixture.overlapHalfExtents.y;
				Primitive.EndOrHalfExtentsZ = Fixture.overlapHalfExtents.z;
			}
		}
	}
	return 0;
}

} // namespace

ChaosSpatialQueryTraceState::ChaosSpatialQueryTraceState(const CaseExecutionSpec& InCaseExecution,
                                                         const ThreadRuntimeState& RuntimeState)
    : CaseExecution(InCaseExecution), Particles(UniqueIndices),
      Evolution(Particles, PhysicalMaterials, nullptr, nullptr, nullptr, nullptr,
	            ThreadExecutionModeForThreadCount(RuntimeState.RequestedThreadCount) ==
	                ThreadExecutionMode::SingleThreaded),
      ThreadCount(RuntimeState.RequestedThreadCount)
{
}

const ChaosCaseDescriptor& SpatialQueryTraceDescriptor()
{
	static const ChaosCaseDescriptor Descriptor = {
	    kEngineId,
	    RunSpatialQueryTraceHeadless,
	    StepSpatialQueryTraceVisual,
	    BuildSpatialQueryTraceVisualScene,
	    SampleNoSpatialQueryTraceTransforms,
	    BuildSpatialQueryTraceDebugPrimitives,
	};
	return Descriptor;
}

} // namespace BenchmarkPolygonChaos
