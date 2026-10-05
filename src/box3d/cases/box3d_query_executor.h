#pragma once

#include <cstdint>

namespace box3d_benchmark
{
struct Box3DQueryExecutor;
using Box3DQueryLane = std::uint64_t (*)(void* context, int work, int lane);
int CreateBox3DQueryExecutor(int threadCount, Box3DQueryLane execute, void* context, Box3DQueryExecutor** output);
std::uint64_t DispatchBox3DQuery(Box3DQueryExecutor* executor, int work);
void DestroyBox3DQueryExecutor(Box3DQueryExecutor* executor);
}
