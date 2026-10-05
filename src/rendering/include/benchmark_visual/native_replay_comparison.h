#pragma once

#include "benchmark_visual/visual_renderer.h"
#include "benchmark_visual/replay_camera.h"
#include "physics_arena/replay_comparison.h"
#include "physics_arena/observation_results.h"
#include <array>
#include <string>
#include <vector>

namespace benchmark_visual
{
struct PhysicsArenaApp;
enum ReplayComparisonPickerState
{
	ReplayComparisonPickerState_Closed,
	ReplayComparisonPickerState_Scanning,
	ReplayComparisonPickerState_Ready,
};
enum ReplayComparisonPane
{
	ReplayComparisonPane_Primary,
	ReplayComparisonPane_Peer,
};
enum ReplayComparisonChoiceState
{
	ReplayComparisonChoiceState_Ready,
	ReplayComparisonChoiceState_Unavailable,
};
struct NativeReplayComparisonChoice
{
	std::array<std::uint32_t, 3> tuple;
	std::uint32_t threads;
	std::uint64_t bytes;
	ReplayComparisonChoiceState state;
	std::string reason;
	std::string engineLabel;
};
struct NativeReplayComparisonCandidate
{
	std::uint32_t runIndex;
	physics_arena::StatusRecord error;
	std::vector<NativeReplayComparisonChoice> choices;
};
struct NativeReplayPeer
{
	physics_arena::ResultManifestRecord manifest;
	physics_arena::ReplayRecording recording;
	PhysicsSceneResources* sceneResources;
	std::filesystem::path directory;
	std::array<std::uint32_t, 3> tuple;
	std::string identity;
	std::string engineLabel;
	std::string runLabel;
	physics_arena::ObservationOutcome outcome;
	physics_arena::StatusRecord outcomeError;
};
struct NativeReplayComparison
{
	NativeReplayPeer* peer;
	physics_arena::ResultManifestRecord* scanManifest;
	std::vector<NativeReplayComparisonCandidate> candidates;
	ReplayCameraContext cameraContext;
	RenderViewport primaryViewport;
	RenderViewport peerViewport;
	RenderViewport primaryClip;
	RenderViewport peerClip;
	ReplayComparisonPickerState picker;
	ReplayComparisonPane activePane;
	ReplayComparisonPane failedPane;
	physics_arena::PresenceStatus pairPresence;
	physics_arena::StatusRecord error;
	physics_arena::ObservationOutcome primaryOutcome;
	physics_arena::StatusRecord selectionError;
	physics_arena::StatusRecord primaryOutcomeError;
	std::uint64_t committedOrdinal;
	std::uint32_t scanIndex;
	std::uint32_t runChoice;
	std::array<std::uint32_t, 3> tupleChoice;
};

void BeginReplayComparisonPicker(PhysicsArenaApp* app);
void CancelReplayComparisonPicker(PhysicsArenaApp* app);
void UpdateReplayComparisonPicker(PhysicsArenaApp* app);
void SelectReplayComparisonRun(PhysicsArenaApp* app, std::uint32_t run);
physics_arena::ArenaStatus OpenReplayComparisonPeer(PhysicsArenaApp* app);
physics_arena::ArenaStatus ReadReplayComparisonOrdinal(PhysicsArenaApp* app, std::uint64_t ordinal);
void StopReplayComparison(PhysicsArenaApp* app);
void ReleaseReplayComparison(PhysicsArenaApp* app);
void DrawReplayComparisonPicker(PhysicsArenaApp* app);
void DrawReplayComparisonWorkspace(PhysicsArenaApp* app);
void DrawReplayComparisonScenes(PhysicsArenaApp* app);
}
