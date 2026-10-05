#pragma once

#include "benchmark_visual/native_app_model.h"

#include <array>

namespace benchmark_visual
{
struct NativeRunPreferencesContext
{
	std::array<wchar_t, physics_arena::kRunPathCapacity> path;
	physics_arena::PresenceStatus activation;
};

physics_arena::ArenaStatus LoadNativeRunPreferences(int argumentCount, NativeArenaModel* model,
                                                    NativeRunPreferencesContext* context,
                                                    physics_arena::StatusRecord* error);
physics_arena::ArenaStatus SaveNativeRunPreferences(const NativeRunPreferencesContext* context,
                                                    const NativeArenaModel* model, physics_arena::StatusRecord* error);
}
