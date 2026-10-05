#include "Runtime/BenchmarkPolygonChaosRuntime.h"

#include "Async/TaskGraphInterfaces.h"
#include "Chaos/Framework/Parallel.h"
#include "CoreGlobals.h"
#include "HAL/PlatformAffinity.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTLS.h"

namespace BenchmarkPolygonChaos
{

const char* ThreadWorkerStatusText(ThreadWorkerStatus Status)
{
	return Status == ThreadWorkerStatus::Ok
	           ? "ok"
			   : (Status == ThreadWorkerStatus::InvalidTaskGraphWorkerCount ? "invalid_taskgraph_worker_count"
			                                                                : "invalid_chaos_execution_mode");
}

ScopedCoreRuntime::ScopedCoreRuntime(int ThreadCount)
{
	State.RequestedThreadCount = ThreadCount;
	State.RequestedChaosWorkerCount = RequestedChaosWorkerCount(ThreadCount);
	State.RequestedTaskGraphWorkerCount = RequestedTaskGraphWorkerCount(ThreadCount);
	PreviousSingleWorkerPhysics = Chaos::bSingleWorkerPhysics ? 1 : 0;
	Chaos::bSingleWorkerPhysics = ThreadCount == 1;
	State.SingleWorkerPhysics = Chaos::bSingleWorkerPhysics ? 1 : 0;

	if (!GIsGameThreadIdInitialized)
	{
		GGameThreadId = FPlatformTLS::GetCurrentThreadId();
		GIsGameThreadIdInitialized = true;
	}

	FPlatformProcess::SetThreadPriority(FPlatformAffinity::GetGameThreadPriority());
	FPlatformProcess::SetThreadAffinityMask(FPlatformAffinity::GetMainGameMask());
	FPlatformProcess::SetupGameThread();

	if (!FTaskGraphInterface::IsRunning())
	{
		FTaskGraphInterface::Startup(State.RequestedTaskGraphWorkerCount);
		FTaskGraphInterface::Get().AttachToThread(ENamedThreads::GameThread);
		StartedTaskGraph = 1;
	}

	State.ActualTaskGraphWorkerCount = FTaskGraphInterface::Get().GetNumWorkerThreads();
	State.EffectiveChaosWorkerCount = ThreadCount > 1 ? State.ActualTaskGraphWorkerCount : 1;
	if ((State.SingleWorkerPhysics != 0) != (ThreadCount == 1))
	{
		State.WorkerStatus = ThreadWorkerStatus::InvalidChaosExecutionMode;
	}
	else if (ThreadCount > 1 && State.ActualTaskGraphWorkerCount != State.RequestedChaosWorkerCount)
	{
		State.WorkerStatus = ThreadWorkerStatus::InvalidTaskGraphWorkerCount;
	}
}

ScopedCoreRuntime::~ScopedCoreRuntime()
{
	if (StartedTaskGraph != 0)
	{
		FTaskGraphInterface::Shutdown();
	}
	Chaos::bSingleWorkerPhysics = PreviousSingleWorkerPhysics != 0;
}

} // namespace BenchmarkPolygonChaos
