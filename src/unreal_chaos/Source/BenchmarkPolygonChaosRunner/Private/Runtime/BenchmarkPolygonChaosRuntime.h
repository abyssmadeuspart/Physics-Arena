#pragma once

#include "Runtime/BenchmarkPolygonChaosConfig.h"

namespace BenchmarkPolygonChaos
{

    enum class ThreadWorkerStatus : uint8
    {
        Ok,
        InvalidTaskGraphWorkerCount,
    };

    struct ThreadRuntimeState
    {
        int RequestedThreadCount = 1;
        int RequestedChaosWorkerCount = 0;
        int RequestedTaskGraphWorkerCount = 1;
        int ActualTaskGraphWorkerCount = 0;
        int EffectiveChaosWorkerCount = 0;
        ThreadWorkerStatus WorkerStatus = ThreadWorkerStatus::Ok;
    };

    const char* ThreadWorkerStatusText(ThreadWorkerStatus Status);

    struct ScopedCoreRuntime
    {
        int StartedTaskGraph = 0;
        ThreadRuntimeState State;

        ScopedCoreRuntime(int ThreadCount);
        ~ScopedCoreRuntime();
    };

} // namespace BenchmarkPolygonChaos
