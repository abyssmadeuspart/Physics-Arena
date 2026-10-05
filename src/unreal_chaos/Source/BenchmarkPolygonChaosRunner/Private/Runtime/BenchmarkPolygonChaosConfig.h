#pragma once

#include "Cases/BenchmarkPolygonChaosCaseRegistry.h"

#include "CoreMinimal.h"

namespace BenchmarkPolygonChaos
{

constexpr const char* kEngineId = "unreal_chaos";
constexpr const char* kEngineRef = "7deeb413d3dc1fc034f48d1aacc0861301829d32";
constexpr double kTimestep = 1.0 / 60.0;

#if UE_BUILD_SHIPPING
constexpr const char* kToolchainId = "unreal_chaos_ubt_win64_shipping";
#else
constexpr const char* kToolchainId = "unreal_chaos_ubt_win64_development";
#endif

enum class ThreadExecutionMode : uint8
{
	SingleThreaded,
	TaskGraphWorkers,
};

enum class ThreadCountSupport : uint8
{
	Unsupported,
	Supported,
};

int RequestedChaosWorkerCount(int ThreadCount);
int RequestedTaskGraphWorkerCount(int ThreadCount);
ThreadExecutionMode ThreadExecutionModeForThreadCount(int ThreadCount);
ThreadCountSupport GetThreadCountSupport(int ThreadCount);

} // namespace BenchmarkPolygonChaos
