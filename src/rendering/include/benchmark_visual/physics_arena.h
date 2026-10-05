#pragma once


#include "benchmark_visual/native_action.h"
#include "benchmark_visual/native_run_queue.h"
#include "benchmark_visual/native_run_presets.h"
#include "physics_arena/replay.h"
#include "benchmark_visual/visual_renderer.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace benchmark_visual
{
constexpr std::size_t kNativeEventRowCapacity = 4096;
constexpr std::size_t kNativeEventTextCapacity = 1024 * 1024;
constexpr std::size_t kTimingComparisonSampleCapacity =
    physics_arena::kEngineCapacity * physics_arena::kTimingProjectionStepCapacity;
using TimingComparisonSampleStore = std::array<double, kTimingComparisonSampleCapacity>;

enum NativeRayLoadRequest
{
	NativeRayLoadRequest_None,
	NativeRayLoadRequest_CaseData,
	NativeRayLoadRequest_Image,
};

enum NativeUiCommand
{
	NativeUiCommand_None = 0,
	NativeUiCommand_Start = 1,
	NativeUiCommand_Cancel = 2,
	NativeUiCommand_Retry = 3,
	NativeUiCommand_RefreshResults = 4,
	NativeUiCommand_RegenerateReport = 5,
	NativeUiCommand_OpenResultFolder = 6,
	NativeUiCommand_OpenReport = 7,
	NativeUiCommand_OpenReplay = 8,
	NativeUiCommand_CloseReplay = 9,
	NativeUiCommand_DeleteRecordings = 10,
	NativeUiCommand_LoadPreset = 11,
	NativeUiCommand_SavePreset = 12,
	NativeUiCommand_LoadReplay = 13,
	NativeUiCommand_AddQueue,
	NativeUiCommand_AddRecommendedQueue,
	NativeUiCommand_EditQueue,
	NativeUiCommand_UpdateQueue,
	NativeUiCommand_CancelQueueEdit,
	NativeUiCommand_RemoveQueue,
	NativeUiCommand_MoveQueueUp,
	NativeUiCommand_MoveQueueDown,
	NativeUiCommand_ClearPendingQueue,
	NativeUiCommand_RecommendedPreset,
};

enum NativeResultMode
{
	NativeResultMode_EnginePerformance = 0,
	NativeResultMode_ThreadScaling = 1,
	NativeResultMode_StepTiming = 2,
	NativeResultMode_Repeats = 3,
	NativeResultMode_CaseData = 4,
	NativeResultMode_Recordings = 5,
};

enum NativeSummaryChartMode
{
	NativeSummaryChartMode_Compare = 0,
	NativeSummaryChartMode_Scaling = 1,
};

enum NativeSummaryLayout
{
	NativeSummaryLayout_ChartAndTable = 0,
	NativeSummaryLayout_ChartOnly = 1,
	NativeSummaryLayout_TableOnly = 2,
};

enum NativeSummaryOrder
{
	NativeSummaryOrder_FastestFirst = 0,
	NativeSummaryOrder_SlowestFirst = 1,
	NativeSummaryOrder_EngineName = 2,
};

enum NativeLogView
{
	NativeLogView_Activity = 0,
	NativeLogView_WarningsErrors = 1,
	NativeLogView_FullDetails = 2,
};

enum NativeFollowStatus
{
	NativeFollowStatus_Following = 0,
	NativeFollowStatus_Paused = 1,
};

enum NativeGraphicalWindowStatus
{
	NativeGraphicalWindowStatus_Inactive = 0,
	NativeGraphicalWindowStatus_Fixed = 1,
	NativeGraphicalWindowStatus_Faulted = 2,
};

enum NativeDockPage
{
	NativeDockPage_Output = 0,
	NativeDockPage_Warnings = 1,
};

enum NativeRecordingPage
{
	NativeRecordingPage_Run,
	NativeRecordingPage_Library,
	NativeRecordingPage_Unfinished,
};

enum NativeCaseDataPage
{
	NativeCaseDataPage_About = 0,
	NativeCaseDataPage_Configuration = 1,
	NativeCaseDataPage_Observations = 2,
	NativeCaseDataPage_Provenance = 3,
};

enum NativeRunPage
{
	NativeRunPage_Simulation = 0,
	NativeRunPage_Fixture = 1,
	NativeRunPage_About = 2,
};

enum NativeReplayStartup
{
	NativeReplayStartup_Paused = 0,
	NativeReplayStartup_Playing = 1,
};

enum NativeCompactRunPage
{
	NativeCompactRunPage_Setup,
	NativeCompactRunPage_Configuration,
	NativeCompactRunPage_Queue,
};

enum NativeReplayPage
{
	NativeReplayPage_Browser,
	NativeReplayPage_Player,
};

struct NativeWorkspaceState
{
	RenderViewport scene;
	float expandedDockHeight;
	float dockDragStartHeight;
	NativeDockPage dockPage;
	NativeRecordingPage recordingPage;
	NativeRunPage runPage;
	NativeReplayPage replayPage;
	physics_arena::PresenceStatus dockCollapsed;
	NativeCompactRunPage compactRunPage;
	physics_arena::PresenceStatus resultsDetailsVisible;
	physics_arena::PresenceStatus replayDetailsVisible;
	physics_arena::PresenceStatus layoutInitialized;
	physics_arena::PresenceStatus sceneHovered;
	physics_arena::PresenceStatus sceneFocused;
	physics_arena::PresenceStatus sceneActive;
};

struct NativeEventRow
{
	std::uint32_t timestampOffset;
	std::uint32_t componentOffset;
	std::uint32_t statusOffset;
	std::uint32_t detailOffset;
	std::uint32_t timestampSize;
	std::uint32_t componentSize;
	std::uint32_t statusSize;
	std::uint32_t detailSize;
	std::uint64_t sequence;
};

struct NativeEventStore
{
	std::array<NativeEventRow, kNativeEventRowCapacity> rows;
	std::array<char, kNativeEventTextCapacity> textArena;
	std::uint32_t rowCount;
	std::uint32_t textUsed;
	std::uint32_t droppedRowCount;
	std::uint64_t generation;
};

struct NativeResultIssue
{
	std::string path;
	physics_arena::StatusRecord reason;
};

struct NativeRecordingLeftover
{
	std::filesystem::path directory;
	std::string displayPath;
	physics_arena::StatusRecord reason;
	physics_arena::ReplayStorageInventory storage;
};

struct NativeResultsState
{
	physics_arena::ReplayStorageInventory tupleStorage;
	physics_arena::ReplayStorageInventory runStorage;
	physics_arena::ReplayStorageInventory libraryStorage;
	std::array<std::uint32_t, physics_arena::kResultIndexCapacity> recordingRunIndexes;
	std::uint32_t recordingRunCount;
	physics_arena::ReplayStorageSelection deletionSelection;
	physics_arena::ReplayStorageInventory deletionPreview;
	std::vector<NativeRecordingLeftover> leftovers;
	std::vector<NativeResultIssue> issues;
	std::vector<std::string> storageFailurePaths;
	physics_arena::ReplayAvailability replayAvailability;
	physics_arena::ReplayAvailability selectedReplayAvailability;
	std::string deletionPath;
	physics_arena::ResultRepeatProjection repeats;
	physics_arena::StatusRecord repeatError;
	physics_arena::ArenaStatus repeatStatus;
	physics_arena::PresenceStatus selectedResultPresence;
	std::uint32_t selectedEngineIndex;
	std::uint32_t selectedThreadIndex;
	std::uint32_t selectedRepeatIndex;
	std::uint32_t repeatMetricIndex;
	NativeCaseDataPage caseDataPage;
	physics_arena::PresenceStatus recordingPickerPending;
	physics_arena::PresenceStatus recordingPickerActive;
	physics_arena::PresenceStatus recordingAutoPlaySingle;
	physics_arena::PresenceStatus recordingRefreshPending;
	physics_arena::PresenceStatus replayOpenPending;
	NativeRayLoadRequest rayLoadRequest;
	physics_arena::PresenceStatus rayLoadingPresented;
	std::array<std::uint32_t, 4> rayImageRequest;
	physics_arena::StatusRecord replayError;
	std::array<std::uint32_t, 3> recordingChoice;
	std::array<physics_arena::ReplayAvailability, physics_arena::kRunRepeatCapacity> recordingAvailability;
	std::array<std::uint64_t, physics_arena::kRunRepeatCapacity> recordingBytes;
	physics_arena::PresenceStatus deletionPending;

	std::array<std::uint32_t, physics_arena::kResultSummaryCapacity> summaryRowIndexes;
	std::array<std::uint32_t, physics_arena::kResultSummaryCapacity> summaryPlotRowIndexes;
	std::array<wchar_t, physics_arena::kRunPathCapacity> selectedDirectory;
	physics_arena::StatusRecord error;
	physics_arena::StatusRecord discoveryError;
	std::uint32_t skippedResultCount;
	std::uint32_t unfinishedResultCount;
	physics_arena::ArenaStatus status;
	physics_arena::PresenceStatus libraryPresence;
	physics_arena::PresenceStatus modelPresence;
	std::uint32_t selectedRecordIndex;
	std::uint32_t metricIndex;
	std::uint32_t timingSelectedEngineMask;
	std::uint32_t timingHiddenEngineMask;
	std::uint32_t timingThreadIndex;
	std::uint32_t timingRepeatIndex;
	std::uint32_t replayEngineIndex;
	std::uint32_t replayThreadIndex;
	std::uint32_t replayRepeatIndex;
	std::uint32_t caseDataEngineIndex;
	std::uint32_t caseDataThreadIndex;
	std::uint32_t caseDataRepeatIndex;
	std::uint32_t summaryRowCount;
	std::uint32_t summaryThreadIndex;
	std::uint32_t summarySelectedEngineMask;
	std::uint32_t summaryHiddenEngineMask;
	int sortColumn;
	int sortDirection;
	NativeResultMode mode;
	NativeSummaryChartMode summaryChartMode;
	NativeSummaryLayout summaryLayout;
	NativeSummaryOrder summaryOrder;
	physics_arena::PresenceStatus summaryFitRequest;
	physics_arena::PresenceStatus timingFitRequest;
	physics_arena::PresenceStatus repeatFitRequest;
};

struct NativeReplayComparison;

struct NativeReplayView
{
	PhysicsSceneResources* sceneResources;
	NativeReplayComparison* comparison;
	ReplayAppearance appearance;
	physics_arena::ReplayRecording recording;
	physics_arena::ReplayClock clock;
	ReplayCameraContext cameraContext;
	std::array<std::uint32_t, 3> selection;
	std::array<char, 32> ordinalText;
	physics_arena::PresenceStatus ordinalEditing;
	physics_arena::PresenceStatus ordinalError;
};

struct NativeRayTracingView;

enum NativeRunPresetPopup
{
	NativeRunPresetPopup_Closed,
	NativeRunPresetPopup_Editing,
	NativeRunPresetPopup_Published,
};

struct NativeRunPresetUi
{
	std::filesystem::path directory;
	std::vector<std::wstring> names;
	std::wstring selectedName;
	std::array<char, 1024> name;
	NativeRunPresetSaveMode saveMode;
	NativeRunPresetPopup popup;
};

struct PhysicsArenaApp
{
	RenderPlatformState platform;
	NativeWorkspaceState ui;
	RenderWindowRestoreRecord graphicalWindowRestore;
	NativeArenaModel model;
	NativeRunQueue queue;
	NativeActionWorkspace workspace;
	NativeActionState action;
	NativeReplayView* replay;
	NativeRayTracingView* rayView;
	NativeEventStore* events;
	NativeResultsState results;
	NativeRunPresetUi presets;
	TimingComparisonSampleStore* timingComparisonSamples;
	physics_arena::StatusRecord uiError;
	physics_arena::PresenceStatus renderRequested;
	physics_arena::PresenceStatus resultViewSelectionPending;
	physics_arena::PresenceStatus closeAfterAction;
	NativeActionPhase lastActionPhase;
	NativeGraphicalWindowStatus graphicalWindowStatus;
	NativeLogView logView;
	NativeFollowStatus followStatus;
	std::uint64_t selectedEventSequence;
	std::uint64_t lastFollowGeneration;
	std::uint64_t lastProgressRenderTick;
	int backbufferWidth;
	int backbufferHeight;
	int resizeRenderFrames;
	int interactionRenderFrames;
};

static_assert(sizeof(PhysicsArenaApp) < physics_arena::kMainStackReservationBytes * 3 / 4);

int RunPhysicsArena(int argc, char** argv);
physics_arena::PresenceStatus VisibleEvent(const NativeEventStore* store, const NativeEventRow& row,
                                           NativeLogView view);
// the caller owns the complete copy and releases it with delete[]
physics_arena::ArenaStatus CreateVisibleEventCopy(const PhysicsArenaApp* app, char** output,
                                                  physics_arena::StatusRecord* error);
void AppendClipboard(char* output, std::size_t* size, const NativeEventStore* store, const NativeEventRow& row);
void FormatSelectedEvent(const PhysicsArenaApp* app, std::array<char, 65536>* output);
void ResetNativeActivity(PhysicsArenaApp* app);
void AppendNativeEvent(NativeEventStore* store, const physics_arena::InvocationEvent* event);
void DrawNativeObservability(PhysicsArenaApp* app);
physics_arena::ArenaStatus GraphicalWindowError(physics_arena::StatusRecord* error, std::string_view detail);
}
