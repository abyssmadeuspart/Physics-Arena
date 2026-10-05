#pragma once

#include "Cases/BenchmarkPolygonChaosCaseRegistry.h"
#include "Runtime/BenchmarkPolygonChaosConfig.h"
#include "Runtime/BenchmarkPolygonChaosRuntime.h"

#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "Templates/UniquePtr.h"

namespace BenchmarkPolygonChaos
{

struct ChaosSpatialQuery
{
	float OriginOrCenterX;
	float OriginOrCenterY;
	float OriginOrCenterZ;
	float DirectionX;
	float DirectionY;
	float DirectionZ;
};

struct ChaosDirectionalQueryInput
{
	Chaos::FVec3 Origin;
	Chaos::FVec3 Direction;
};

struct ChaosOverlapQueryInput
{
	Chaos::FAABB3 Bounds;
	Chaos::FVec3 Center;
};

enum class SpatialQueryBatchPhase : uint8
{
	Warmup,
	Measured,
};

struct ChaosSpatialQueryTraceState
{
	CaseExecutionSpec CaseExecution;
	Chaos::FParticleUniqueIndicesMultithreaded UniqueIndices;
	Chaos::FPBDRigidsSOAs Particles;
	Chaos::THandleArray<Chaos::FChaosPhysicsMaterial> PhysicalMaterials;
	Chaos::FPBDRigidsEvolutionGBF Evolution;
	TUniquePtr<Chaos::FChaosPhysicsMaterial> Material;
	Chaos::FVec3 SphereHalfExtents;
	TUniquePtr<ChaosDirectionalQueryInput[]> RayInputs;
	TUniquePtr<ChaosDirectionalQueryInput[]> SphereCastInputs;
	TUniquePtr<ChaosOverlapQueryInput[]> OverlapInputs;
	TUniquePtr<uint8[]> DebugHits;
	TUniquePtr<float[]> DebugHitDistances;
	TUniquePtr<uint64[]> RawBatchCycles;
	TUniquePtr<uint64[]> LaneHitCounts;
	uint64 RayElapsedCycles = 0;
	uint64 SphereCastElapsedCycles = 0;
	uint64 OverlapElapsedCycles = 0;
	uint64 RayHitCount = 0;
	uint64 SphereCastHitCount = 0;
	uint64 OverlapHitCount = 0;
	double WorkloadElapsedMs = 0.0;
	double LatestBatchElapsedMs = 0.0;
	int StaticBodyCount = 0;
	int ThreadCount = 0;
	int CompletedBatchCount = 0;

	ChaosSpatialQueryTraceState(const CaseExecutionSpec& InCaseExecution, const ThreadRuntimeState& RuntimeState);
};

const ChaosCaseDescriptor& SpatialQueryTraceDescriptor();

} // namespace BenchmarkPolygonChaos
