#pragma once

#include "nvidia_physx34_runner_args.h"

#include <chrono>
#include <cstdint>

namespace nvidia_physx34_benchmark
{
enum PhysXObservationValueType
{
	PhysXObservationValueType_Uint64 = 1,
	PhysXObservationValueType_Float64 = 2,
};

struct PhysXObservationRow
{
	const char* metricId;
	const char* phaseId;
	std::uint32_t sampleIndex;
	PhysXObservationValueType valueType;
	std::uint64_t valueBits;
};

struct PhysXResult
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
	const PhysXObservationRow* observations;
	std::uint32_t observationCount;
};

int WritePhysXResult(const PhysXRunRequest& request, const PhysXResult& result);
}
