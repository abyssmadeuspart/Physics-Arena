#include "Runtime/BenchmarkPolygonChaosConfig.h"

namespace BenchmarkPolygonChaos
{

int RequestedChaosWorkerCount(int ThreadCount)
{
	return ThreadCount;
}

int RequestedTaskGraphWorkerCount(int ThreadCount)
{
	return ThreadCount;
}

ThreadExecutionMode ThreadExecutionModeForThreadCount(int ThreadCount)
{
	return ThreadCount > 1 ? ThreadExecutionMode::TaskGraphWorkers : ThreadExecutionMode::SingleThreaded;
}

ThreadCountSupport GetThreadCountSupport(int ThreadCount)
{
	return ThreadCount > 0 ? ThreadCountSupport::Supported : ThreadCountSupport::Unsupported;
}

} // namespace BenchmarkPolygonChaos
