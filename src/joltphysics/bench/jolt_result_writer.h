#pragma once

#include "jolt_runner_args.h"

#include <chrono>
#include <cstdint>

namespace jolt_benchmark
{
enum JoltObservationValueType
{
	JoltObservationValueType_Uint64 = 1,
	JoltObservationValueType_Float64 = 2,
};

struct JoltObservationRow
{
	const char* metricId;
	const char* phaseId;
	std::uint32_t sampleIndex;
	JoltObservationValueType valueType;
	std::uint64_t valueBits;
};

struct JoltResult
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
	const JoltObservationRow* observations;
	std::uint32_t observationCount;
};

int WriteJoltResult(const JoltRunRequest& request, const JoltResult& result);
}
