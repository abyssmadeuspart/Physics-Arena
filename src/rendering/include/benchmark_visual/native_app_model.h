#pragma once

#include "benchmark_visual/replay_camera.h"

#include "physics_arena/run.h"

#include <array>
#include <string_view>

namespace benchmark_visual
{
constexpr std::size_t kNativeRunLabelCapacity = 1024;

enum NativeArenaView
{
	NativeArenaView_Run = 0,
	NativeArenaView_Results = 1,
	NativeArenaView_Replay = 2,
};

enum NativeStartupAction
{
	NativeStartupAction_None = 0,
	NativeStartupAction_Run = 1,
	NativeStartupAction_Replay = 2,
	NativeStartupAction_RayImage = 3,
};

struct NativeRenderResolution
{
	std::uint32_t widthPixels;
	std::uint32_t heightPixels;
};

inline constexpr std::array<NativeRenderResolution, 6> kNativeRenderResolutionPresets = {{
    {1280, 720},
    {1366, 768},
    {1600, 900},
    {1920, 1080},
    {2560, 1440},
    {3840, 2160},
}};

inline constexpr physics_arena::PresenceStatus NativeRenderResolutionAvailability(
    const NativeRenderResolution& resolution, std::uint32_t displayWidthPixels, std::uint32_t displayHeightPixels)
{
	return resolution.widthPixels <= displayWidthPixels && resolution.heightPixels <= displayHeightPixels
	           ? physics_arena::PresenceStatus_Present
			   : physics_arena::PresenceStatus_Absent;
}

struct NativeRunSelection
{
	physics_arena::VerificationMode verificationMode = physics_arena::VerificationMode_On;
	physics_arena::ResultStorage storage = physics_arena::ResultStorage_Local;
	physics_arena::RecordingMode recordingMode;
	NativeReplayCameraPreferences replayCamera;
	physics_arena::RunSettings settings;
	std::array<physics_arena::PresenceStatus, physics_arena::kEngineCapacity> engines;
	std::array<std::uint32_t, physics_arena::kEngineCapacity> orderedEngineIndexes;
	std::array<physics_arena::PresenceStatus, physics_arena::kThreadCountCapacity> threads;
	std::array<physics_arena::PresenceStatus, physics_arena::kThreadCountCapacity> recordingThreads;
	std::uint32_t caseIndex;
	std::uint32_t orderedEngineCount;
	std::uint32_t repeatCount;
	std::uint32_t maximumThreadCount;
	std::uint32_t resolutionPresetIndex;
	physics_arena::ThreadSelectionMode threadMode;
};

struct NativeArenaModel
{
	physics_arena::Catalog catalog;
	physics_arena::ReleaseCatalog releaseCatalog;
	physics_arena::HostRecord host;
	NativeRunSelection selection;
	std::array<std::uint32_t, physics_arena::kEngineCapacity> engineDisplayOrder;
	std::array<wchar_t, physics_arena::kRunPathCapacity> repositoryRoot;
	std::array<wchar_t, physics_arena::kRunPathCapacity> resultPath;
	std::array<char, 64> replayEngineId;
	std::uint32_t replayThreadCount;
	std::uint32_t replayRepeatIndex;
	std::uint32_t rayView, rayPhase, rayApi, rayChannel;
	NativeArenaView view;
	NativeStartupAction startupAction;
	physics_arena::ArenaStatus startupStatus;
	physics_arena::StatusRecord startupError;
};

struct NativeRunLabelProjection
{
	std::array<char, kNativeRunLabelCapacity> text;
	std::uint32_t size;
	physics_arena::PresenceStatus canonical;
};

static_assert(sizeof(NativeArenaModel) < physics_arena::kMainStackReservationBytes / 8);

physics_arena::ArenaStatus InitializeNativeArenaModel(const wchar_t* repositoryRoot, int argumentCount,
                                                      const char* const* arguments, NativeArenaModel* model,
                                                      physics_arena::StatusRecord* error);
physics_arena::ArenaStatus PrepareNativeRunRequest(const NativeArenaModel* model,
                                                   physics_arena::PreparedRunRequest* request,
                                                   physics_arena::StatusRecord* error);
physics_arena::ArenaStatus PrepareNativeRunRequest(const NativeArenaModel* model, const NativeRunSelection& selection,
                                                   physics_arena::PreparedRunRequest* request, physics_arena::StatusRecord* error);
physics_arena::ArenaStatus SelectNativeRunCase(NativeArenaModel* model, std::uint32_t caseIndex,
                                               physics_arena::StatusRecord* error);
physics_arena::ArenaStatus SelectNativeRunFamily(NativeArenaModel* model, CaseFixtureKind family,
                                                 physics_arena::StatusRecord* error);
physics_arena::ArenaStatus SelectNativeRunShape(NativeArenaModel* model, CaseShapePreset preset,
                                                physics_arena::StatusRecord* error);
physics_arena::ArenaStatus SelectNativeRunEngines(
    NativeArenaModel* model, const std::array<physics_arena::PresenceStatus, physics_arena::kEngineCapacity>& engines,
    physics_arena::StatusRecord* error);
physics_arena::ArenaStatus ResolveNativeRunThreadAvailability(const NativeArenaModel* model, std::uint32_t threadCount,
                                                              physics_arena::PresenceStatus* availability,
                                                              physics_arena::StatusRecord* error);
void ReconcileNativeRecordingThreads(NativeRunSelection* selection);
void EnableNativeRecording(NativeRunSelection* selection);
physics_arena::ArenaStatus ReconcileNativeRunThreads(NativeArenaModel* model, physics_arena::StatusRecord* error);
physics_arena::ArenaStatus ApplyRecommendedNativeRunThreads(NativeArenaModel* model,
                                                            physics_arena::StatusRecord* error);
physics_arena::ArenaStatus ProjectNativeRunLabel(std::string_view runId, std::uint32_t threadCount,
                                                 std::uint32_t repeatCount, NativeRunLabelProjection* projection);
}
