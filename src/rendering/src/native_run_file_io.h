#pragma once

#include "physics_arena/bench_types.h"

#include <string_view>

namespace benchmark_visual
{
enum NativeRunFilePublication
{
	NativeRunFilePublication_Replace,
	NativeRunFilePublication_Create,
};
physics_arena::ArenaStatus WriteNativeRunFile(const wchar_t* path, std::string_view bytes,
    physics_arena::StatusRecord* error, NativeRunFilePublication mode = NativeRunFilePublication_Replace);
}
