#include "box3d_query_executor.h"

#include <condition_variable>
#include <mutex>
#include <new>
#include <thread>

namespace box3d_benchmark
{
struct Box3DQueryExecutor
{
	void* context;
	Box3DQueryLane execute;
	std::thread* workers;
	std::uint64_t* laneHitCounts;
	std::mutex mutex;
	std::condition_variable workCondition;
	std::condition_variable completionCondition;
	std::uint64_t generation;
	int threadCount;
	int startedWorkerCount;
	int completedWorkerCount;
	int work;
	int stopping;
};

void QueryWorker(Box3DQueryExecutor* executor, int laneIndex)
{
	std::uint64_t observedGeneration = 0;
	std::unique_lock<std::mutex> lock(executor->mutex);
	for (;;)
	{
		executor->workCondition.wait(lock,
		                             [executor, observedGeneration]
		                             {
			                             return executor->generation != observedGeneration;
		                             });
		observedGeneration = executor->generation;
		const int work = executor->work;
		if (executor->stopping != 0)
			return;
		lock.unlock();
		executor->laneHitCounts[laneIndex] = executor->execute(executor->context, work, laneIndex);
		lock.lock();
		++executor->completedWorkerCount;
		if (executor->completedWorkerCount == executor->threadCount - 1)
			executor->completionCondition.notify_one();
	}
}

void DestroyBox3DQueryExecutor(Box3DQueryExecutor* executor)
{
	if (executor == nullptr)
		return;
	if (executor->startedWorkerCount > 0)
	{
		{
			std::lock_guard<std::mutex> lock(executor->mutex);
			executor->stopping = 1;
			++executor->generation;
		}
		executor->workCondition.notify_all();
		for (int index = 0; index < executor->startedWorkerCount; ++index)
			if (executor->workers[index].joinable())
				executor->workers[index].join();
	}
	delete[] executor->workers;
	delete[] executor->laneHitCounts;
	delete executor;
}

int CreateBox3DQueryExecutor(int threadCount, Box3DQueryLane execute, void* context, Box3DQueryExecutor** output)
{
	Box3DQueryExecutor* executor = new (std::nothrow) Box3DQueryExecutor{};
	if (executor == nullptr)
		return 2;
	executor->context = context;
	executor->execute = execute;
	executor->threadCount = threadCount;
	executor->work = 0;
	executor->laneHitCounts = new (std::nothrow) std::uint64_t[executor->threadCount]();
	const int workerCount = executor->threadCount - 1;
	executor->workers = workerCount > 0 ? new (std::nothrow) std::thread[workerCount] : nullptr;
	if (executor->laneHitCounts == nullptr || (workerCount > 0 && executor->workers == nullptr))
	{
		DestroyBox3DQueryExecutor(executor);
		return 2;
	}
	try
	{
		for (int workerIndex = 0; workerIndex < workerCount; ++workerIndex)
		{
			executor->workers[workerIndex] = std::thread(QueryWorker, executor, workerIndex + 1);
			++executor->startedWorkerCount;
		}
	}
	catch (...)
	{
		DestroyBox3DQueryExecutor(executor);
		return 2;
	}
	*output = executor;
	return 0;
}

std::uint64_t DispatchBox3DQuery(Box3DQueryExecutor* executor, int work)
{
	if (executor->threadCount == 1)
		return executor->execute(executor->context, work, 0);
	{
		std::lock_guard<std::mutex> lock(executor->mutex);
		executor->work = work;
		executor->completedWorkerCount = 0;
		++executor->generation;
	}
	executor->workCondition.notify_all();
	executor->laneHitCounts[0] = executor->execute(executor->context, work, 0);
	{
		std::unique_lock<std::mutex> lock(executor->mutex);
		executor->completionCondition.wait(lock,
		                                   [executor]
		                                   {
			                                   return executor->completedWorkerCount == executor->threadCount - 1;
		                                   });
	}
	std::uint64_t hitCount = 0;
	for (int laneIndex = 0; laneIndex < executor->threadCount; ++laneIndex)
		hitCount += executor->laneHitCounts[laneIndex];
	return hitCount;
}

}
