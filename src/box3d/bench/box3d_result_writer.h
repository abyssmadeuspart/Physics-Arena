#pragma once

#include "box3d_runner_args.h"

#include <chrono>
#include <cstdint>

namespace box3d_benchmark
{
enum Box3DObservationValueType
{
	Box3DObservationValueType_Uint64 = 1,
	Box3DObservationValueType_Float64 = 2,
};

struct Box3DObservationRow
{
	const char* metricId;
	const char* phaseId;
	std::uint32_t sampleIndex;
	Box3DObservationValueType valueType;
	std::uint64_t valueBits;
};

struct Box3DResult
{
	const char* fixtureSemantic;
	std::uint32_t fixtureRevision;
	const char* physicsSettings;
	int bodyCount;
	int shapeCount;
	int queryCount;
	int constraintCount;
	std::uint64_t invalidTransformCount;
	const char* caseStatus;
	const char* metricStatus;
	int effectiveThreadCount;
	int effectiveWorkerCount;
	int completedWorkUnitCount;
	double workloadElapsedMs;
	const std::chrono::steady_clock::duration::rep* durations;
	const Box3DObservationRow* observations;
	std::uint32_t observationCount;
};

int WriteBox3DResult(const Box3DRunRequest& request, const Box3DResult& result);
}
