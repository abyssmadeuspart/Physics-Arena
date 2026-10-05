#pragma once

#include "benchmark_visual/native_app_model.h"
#include <filesystem>
#include <string>
#include <vector>

namespace benchmark_visual
{
enum NativeRunPresetSaveMode
{
	NativeRunPresetSaveMode_Create,
	NativeRunPresetSaveMode_Replace,
};

physics_arena::ArenaStatus ComposeRecommendedNativeRunSelection(const NativeArenaModel* model,
    std::uint32_t caseIndex, const NativeRunSelection& base, NativeRunSelection* candidate,
    physics_arena::StatusRecord* error);
physics_arena::ArenaStatus ResolveNativeRunPresetDirectory(const std::filesystem::path& executable,
    std::filesystem::path* directory, physics_arena::StatusRecord* error);
physics_arena::ArenaStatus AdmitNativeRunPresetName(const std::filesystem::path& directory,
    std::wstring_view name, std::filesystem::path* path, physics_arena::StatusRecord* error);
physics_arena::ArenaStatus EnumerateNativeRunPresets(const std::filesystem::path& directory,
    std::vector<std::wstring>* names, physics_arena::StatusRecord* error);
physics_arena::ArenaStatus SaveNamedNativeRunPreset(const std::filesystem::path& directory,
    std::wstring_view name, NativeRunPresetSaveMode mode, const NativeArenaModel* model,
    physics_arena::StatusRecord* error);
physics_arena::ArenaStatus LoadNativeRunPreset(const wchar_t* path, NativeArenaModel* model,
                                              physics_arena::StatusRecord* error);
physics_arena::ArenaStatus SaveNativeRunPreset(const wchar_t* path, const NativeArenaModel* model,
    physics_arena::StatusRecord* error, NativeRunPresetSaveMode mode = NativeRunPresetSaveMode_Replace);
}
