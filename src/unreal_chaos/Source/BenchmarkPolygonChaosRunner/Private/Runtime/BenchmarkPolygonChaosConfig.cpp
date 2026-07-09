#include "Runtime/BenchmarkPolygonChaosConfig.h"

namespace BenchmarkPolygonChaos
{

    int RequestedChaosWorkerCount(int ThreadCount)
    {
        return ThreadCount > 1 ? ThreadCount - 1 : 0;
    }

    int RequestedTaskGraphWorkerCount(int ThreadCount)
    {
        return ThreadCount > 1 ? ThreadCount - 1 : 1;
    }

    ThreadExecutionMode ThreadExecutionModeForThreadCount(int ThreadCount)
    {
        return ThreadCount > 1 ? ThreadExecutionMode::TaskGraphWorkers
                               : ThreadExecutionMode::SingleThreaded;
    }

} // namespace BenchmarkPolygonChaos
