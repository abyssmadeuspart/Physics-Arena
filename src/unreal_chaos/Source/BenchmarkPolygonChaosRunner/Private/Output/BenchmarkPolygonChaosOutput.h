#pragma once

#include "Runner/BenchmarkPolygonChaosCli.h"

#include "CoreMinimal.h"

namespace BenchmarkPolygonChaos
{

enum class ChaosObservationValueType : uint8
{
	Uint64 = 1,
	Float64 = 2,
};

struct ChaosObservationRow
{
	const char* MetricId;
	const char* PhaseId;
	uint32 SampleIndex;
	ChaosObservationValueType ValueType;
	uint64 ValueBits;
};

struct ChaosResult
{
	const char* FixtureSemantic;
	uint32 FixtureRevision;
	const char* PhysicsSettings;
	int BodyCount;
	int ShapeCount;
	int QueryCount;
	int ConstraintCount;
	uint64 InvalidTransformCount;
	const char* CaseStatus;
	const char* MetricStatus;
	int EffectiveThreadCount;
	int EffectiveWorkerCount;
	int CompletedWorkUnitCount;
	double WorkloadElapsedMs;
	const TArray<uint64>* RawWorkUnitCycles;
	const ChaosObservationRow* Observations;
	int ObservationCount;
};

int WriteProbeCsv(const TCHAR* OutputPath, const char* EngineId, const char* EngineRef, const char* CaseId);
int WriteChaosResult(const RunnerArgs& Args, const ChaosResult& Result);

} // namespace BenchmarkPolygonChaos
