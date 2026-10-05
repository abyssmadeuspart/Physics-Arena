#include "launcher_app_internal.h"
#include "benchmark_visual/native_replay_comparison.h"
#include "benchmark_visual/native_run_preferences.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <new>
#include <string_view>

namespace benchmark_visual
{
using namespace physics_arena;
void ApplyNativeRunQueueCommand(PhysicsArenaApp* app, NativeUiCommand command)
{
	NativeRunQueue& queue = app->queue;
	if (command == NativeUiCommand_AddQueue)
		AppendNativeRunQueue(&queue, &app->model, app->model.selection, &app->uiError);
	else if (command == NativeUiCommand_AddRecommendedQueue)
		AppendRecommendedNativeRuns(&queue, &app->model, &app->uiError);
	else if (command == NativeUiCommand_EditQueue)
	{
		if (BeginNativeRunQueueEdit(&queue, &app->model.selection) == ArenaStatus_Ok)
			app->ui.compactRunPage = NativeCompactRunPage_Setup;
	}
	else if (command == NativeUiCommand_UpdateQueue)
		CommitNativeRunQueueEdit(&queue, &app->model, app->model.selection, &app->uiError);
	else if (command == NativeUiCommand_CancelQueueEdit)
		CancelNativeRunQueueEdit(&queue, &app->model.selection);
	else if (command == NativeUiCommand_RemoveQueue)
		RemoveNativeRunQueueEntry(&queue);
	else if (command == NativeUiCommand_MoveQueueUp || command == NativeUiCommand_MoveQueueDown)
		MoveNativeRunQueueEntry(&queue, command == NativeUiCommand_MoveQueueUp ? -1 : 1);
	else if (command == NativeUiCommand_ClearPendingQueue && queue.editingId == 0)
		ClearPendingNativeRunQueue(&queue);
	app->renderRequested = PresenceStatus_Present;
}

void AdvanceNativeRunQueue(PhysicsArenaApp* app)
{
	if (app->closeAfterAction == PresenceStatus_Present || ActiveAction(app->action) == PresenceStatus_Present)
		return;
	const NativeRunQueueState previous = app->queue.state;
	const std::uint64_t id = NextNativeRunQueueEntry(&app->queue);
	if (id == 0)
	{
		if (previous == NativeRunQueueState_Armed && app->queue.state == NativeRunQueueState_Completed)
		{
			const std::array<wchar_t, kRunPathCapacity> expected = app->results.selectedDirectory;
			StatusRecord error = {};
			const ArenaStatus status = RefreshResultLibrary(app, &error);
			if (status != ArenaStatus_Ok)
			{
				app->results.status = status;
				app->results.error = error;
			}
			else
			{
				std::array<wchar_t, kRunPathCapacity> expectedPath = {};
				std::array<wchar_t, kRunPathCapacity> loadedPath = {};
				const DWORD expectedSize = GetFullPathNameW(expected.data(), static_cast<DWORD>(expectedPath.size()),
				                                            expectedPath.data(), nullptr);
				const DWORD loadedSize = GetFullPathNameW(app->results.selectedDirectory.data(),
				                                          static_cast<DWORD>(loadedPath.size()), loadedPath.data(), nullptr);
				if (app->results.modelPresence == PresenceStatus_Present &&
				    SelectResultRecord(app, expected.data()) == PresenceStatus_Present &&
				    expectedSize != 0 && expectedSize < expectedPath.size() &&
				    loadedSize != 0 && loadedSize < loadedPath.size() &&
				    _wcsicmp(expectedPath.data(), loadedPath.data()) == 0)
				{
					app->model.view = NativeArenaView_Results;
					app->resultViewSelectionPending = PresenceStatus_Present;
				}
				else
					app->results.status = ResultLibraryError(&app->results.error, expected.data(),
					                                        "Completed queue's final result could not be admitted and loaded");
			}
			if (app->results.status != ArenaStatus_Ok)
				app->uiError = app->results.error;
			app->renderRequested = PresenceStatus_Present;
			app->interactionRenderFrames = 3;
		}
		return;
	}
	NativeRunQueueEntry* entry = FindNativeRunQueueEntry(&app->queue, id);
	const NativeRunSelection snapshot = entry->selection;
	app->uiError = {};
	CloseReplayView(app);
	const ArenaStatus status = StartNativeAction(&app->model, snapshot, &app->workspace, &app->action, &app->uiError);
	if (status != ArenaStatus_Ok)
	{
		entry->error = app->uiError;
		app->queue.selectedId = id;
		StopNativeRunQueue(&app->queue);
	}
	else
	{
		entry->state = NativeRunQueueEntryState_Active;
		app->queue.activeId = id;
		app->results.modelPresence = PresenceStatus_Absent;
		app->results.libraryPresence = PresenceStatus_Absent;
		app->lastActionPhase = NativeActionPhase_Executing;
	}
	app->renderRequested = PresenceStatus_Present;
}

void DrainEvents(PhysicsArenaApp* app)
{
	InvocationEvent event = {};
	PresenceStatus drained = PresenceStatus_Absent;
	while (PopEvent(&app->action.events, &event) == ArenaStatus_Ok)
	{
		AppendNativeEvent(app->events, &event);
		drained = PresenceStatus_Present;
	}
	if (drained == PresenceStatus_Present)
		app->renderRequested = PresenceStatus_Present;
}

void CompleteActionUi(PhysicsArenaApp* app, NativeActionPhase current)
{
	if (ActiveAction(app->action) == PresenceStatus_Present)
		return;
	if (app->lastActionPhase == current)
		return;
	app->lastActionPhase = current;
	if (app->action.kind == NativeActionKind_Run && app->queue.activeId != 0)
	{
		app->results.selectedDirectory = app->action.paths.resultDirectory;
		if ((current == NativeActionPhase_Failed || current == NativeActionPhase_Interrupted) &&
		    app->uiError.code == ArenaStatus_Ok)
			app->uiError = app->action.error;
		const NativeRunQueueEntryState state = current == NativeActionPhase_Succeeded ? NativeRunQueueEntryState_Completed :
		    current == NativeActionPhase_CompletedWithFailures ? NativeRunQueueEntryState_CompletedWithFailures :
		    current == NativeActionPhase_Interrupted ? NativeRunQueueEntryState_Interrupted : NativeRunQueueEntryState_Failed;
		CompleteNativeRunQueueEntry(&app->queue, state);
		if (app->queue.state != NativeRunQueueState_Armed && app->closeAfterAction != PresenceStatus_Present)
		{
			StatusRecord error = {};
			RefreshResultLibrary(app, &error);
		}
		app->renderRequested = PresenceStatus_Present;
		app->interactionRenderFrames = 3;
		return;
	}
	if (app->action.kind == NativeActionKind_Storage)
	{
		app->results.storageFailurePaths.clear();
		app->results.storageFailurePaths.reserve(app->action.storageResult.failures.size());
		for (const ReplayStorageFailure& failure : app->action.storageResult.failures)
			app->results.storageFailurePaths.push_back(failure.path.string());
		ApplyStorageDeletion(app);
		app->results.status = app->action.error.code;
		app->results.error = app->action.error;
		app->renderRequested = PresenceStatus_Present;
		app->interactionRenderFrames = 3;
		return;
	}
	app->renderRequested = PresenceStatus_Present;
	app->interactionRenderFrames = 3;
	if (current == NativeActionPhase_Succeeded || current == NativeActionPhase_CompletedWithFailures)
	{
		app->results.modelPresence = PresenceStatus_Present;
		app->results.libraryPresence = PresenceStatus_Present;
		app->results.status = ArenaStatus_Ok;
		app->results.error = {};
		if (app->action.kind == NativeActionKind_Run)
		{
			app->results.modelPresence = PresenceStatus_Absent;
			app->results.selectedDirectory = app->action.paths.resultDirectory;
			app->model.view = NativeArenaView_Results;
			app->resultViewSelectionPending = PresenceStatus_Present;
		}
		StatusRecord libraryError = {};
		RefreshResultLibrary(app, &libraryError);
	}
	else if (current == NativeActionPhase_Failed || current == NativeActionPhase_Interrupted)
	{
		if (app->uiError.code == ArenaStatus_Ok)
			app->uiError = app->action.error;
		if (app->results.selectedDirectory[0] != L'\0')
		{
			StatusRecord restoreError = {};
			RefreshResultLibrary(app, &restoreError);
		}
		if (app->action.kind == NativeActionKind_Report)
		{
			std::array<char, kDetailCapacity> reason = {};
			std::snprintf(reason.data(), reason.size(), "Regenerate report failed: %.*s",
			              static_cast<int>(app->action.error.detailSize), app->action.error.detail.data());
			app->results.status =
			    ResultLibraryError(&app->results.error, app->action.resultDirectory.data(), reason.data());
		}
	}
}

int RenderFrame(PhysicsArenaApp* app)
{
	if (ImGuiRaylibBeginFrame(app->platform) != RenderViewerStatus_Ok)
		return NativeUiCommand_None;
	const NativeUiCommand command = DrawPhysicsArenaUi(app);
	if (app->replay != nullptr && app->model.view == NativeArenaView_Replay)
		DrawReplayScene(app);
	ImGuiRaylibEndFrame();
	RendererRaylibPresent();
	return command;
}

int RunPhysicsArena(int argc, char** argv)
{
	PhysicsArenaApp app = {};
	app.action.phase.store(NativeActionPhase_Idle, std::memory_order_relaxed);
	app.lastActionPhase = NativeActionPhase_Idle;
	std::array<wchar_t, kRunPathCapacity> repositoryRoot = {};
	if (FindRepositoryRoot(&repositoryRoot) != ArenaStatus_Ok)
		return RenderViewerStatus_InvalidArgument;
	StatusRecord error = {};
	NativeRunPreferencesContext preferences = {};
	InitializeNativeArenaModel(repositoryRoot.data(), argc > 1 ? argc - 1 : 0, argc > 1 ? argv + 1 : nullptr,
	                           &app.model, &error);
	if (app.model.startupStatus == ArenaStatus_Ok)
	{
		StatusRecord preferencesError = {};
		if (LoadNativeRunPreferences(argc > 1 ? argc - 1 : 0, &app.model, &preferences, &preferencesError) !=
		    ArenaStatus_Ok)
			app.uiError = preferencesError;
		InitializeResults(&app);
	}
	const RenderWindowDesc window = {"Physics Arena", 1120, 760};
	if (PlatformRaylibCreate(&app.platform, window) != RenderViewerStatus_Ok)
		return RenderViewerStatus_WindowCreateFailed;
	if (RendererRaylibCreate(app.platform) != RenderViewerStatus_Ok)
	{
		PlatformRaylibDestroy(&app.platform);
		return RenderViewerStatus_RendererInitFailed;
	}
	app.events = new (std::nothrow) NativeEventStore{};
	if (app.events == nullptr)
	{
		RendererRaylibDestroy();
		PlatformRaylibDestroy(&app.platform);
		return RenderViewerStatus_RendererResourceFailed;
	}
	app.timingComparisonSamples = new (std::nothrow) TimingComparisonSampleStore;
	if (app.timingComparisonSamples == nullptr)
	{
		delete app.events;
		RendererRaylibDestroy();
		PlatformRaylibDestroy(&app.platform);
		return RenderViewerStatus_RendererResourceFailed;
	}
	app.backbufferWidth = app.platform.width;
	app.backbufferHeight = app.platform.height;
	app.renderRequested = PresenceStatus_Present;
	for (;;)
	{
		const unsigned int eventBefore = app.platform.eventSerial;
		const int waitMilliseconds =
		    app.resizeRenderFrames > 0 || app.interactionRenderFrames > 0 ||
		            (app.replay != nullptr && app.replay->comparison != nullptr &&
		             app.replay->comparison->picker == ReplayComparisonPickerState_Scanning) ||
		            (app.replay != nullptr && app.replay->clock.state == ReplayPlayState_Playing &&
					 app.replay->clock.visibility == ReplayVisibility_Visible)
		        ? 16
		        : (ActiveAction(app.action) == PresenceStatus_Present ? 50 : 750);
		const int platformStatus = PlatformRaylibWait(&app.platform, waitMilliseconds);
		if (platformStatus != RenderViewerStatus_Ok)
		{
			GraphicalWindowError(&app.uiError, "window_event_failed");
			StopNativeRunQueue(&app.queue);
			CancelNativeAction(&app.action);
			app.closeAfterAction = PresenceStatus_Present;
		}
		if (app.platform.width > 0 && app.platform.height > 0 &&
		    (app.platform.width != app.backbufferWidth || app.platform.height != app.backbufferHeight))
		{
			if (RendererRaylibResize(app.platform) == RenderViewerStatus_Ok)
			{
				app.backbufferWidth = app.platform.width;
				app.backbufferHeight = app.platform.height;
				app.resizeRenderFrames = 3;
				app.renderRequested = PresenceStatus_Present;
			}
		}
		if (app.platform.eventSerial != eventBefore)
		{
			app.renderRequested = PresenceStatus_Present;
			app.interactionRenderFrames = 3;
		}
		if (app.platform.windowEventStatus == RenderWindowEventStatus_CloseRequested)
		{
			StopNativeRunQueue(&app.queue);
			app.platform.windowEventStatus = RenderWindowEventStatus_Running;
			if (ActiveAction(app.action) == PresenceStatus_Present)
			{
				app.closeAfterAction = PresenceStatus_Present;
				CancelNativeAction(&app.action);
				app.renderRequested = PresenceStatus_Present;
			}
			else
				break;
		}
		if (ActiveAction(app.action) == PresenceStatus_Present)
		{
			PollNativeAction(&app.action, &error);
			DrainEvents(&app);
			const std::uint64_t now = GetTickCount64();
			if (now - app.lastProgressRenderTick >= 250)
			{
				app.lastProgressRenderTick = now;
				app.renderRequested = PresenceStatus_Present;
			}
		}
		const NativeActionPhase current =
		    static_cast<NativeActionPhase>(app.action.phase.load(std::memory_order_acquire));
		CompleteActionUi(&app, current);
		AdvanceNativeRunQueue(&app);
		UpdateReplayView(&app);
		if (app.closeAfterAction == PresenceStatus_Present && ActiveAction(app.action) != PresenceStatus_Present)
			break;
		if (app.renderRequested != PresenceStatus_Present || IsIconic(static_cast<HWND>(app.platform.nativeHandle)))
			continue;
		NativeUiCommand command = NativeUiCommand_None;
		if (app.model.startupAction == NativeStartupAction_Replay || app.model.startupAction == NativeStartupAction_RayImage)
		{
			const NativeStartupAction startup = app.model.startupAction;
			app.model.startupAction = NativeStartupAction_None;
			const ResultViewModel& result = app.workspace.finalization.model;
			app.results.replayEngineIndex = result.engineCount;
			app.results.replayThreadIndex = result.threadCount;
			app.results.replayRepeatIndex = app.model.replayRepeatIndex;
			for (std::uint32_t index = 0; index < result.engineCount; ++index)
				if (ResultViewTextView(&result, result.engines[index].id) == app.model.replayEngineId.data())
					app.results.replayEngineIndex = index;
			for (std::uint32_t index = 0; index < result.threadCount; ++index)
				if (result.threadCounts[index] == app.model.replayThreadCount)
					app.results.replayThreadIndex = index;
			ArenaStatus openStatus = ArenaStatus_Ok;
			if (startup == NativeStartupAction_RayImage &&
			    (app.results.replayEngineIndex >= result.engineCount || app.results.replayThreadIndex >= result.threadCount ||
			     app.results.replayRepeatIndex >= result.repeatCount))
				openStatus = GraphicalWindowError(&app.results.replayError, "ray_replay_tuple_invalid");
			else if (startup == NativeStartupAction_RayImage)
			{
				app.results.selectedEngineIndex = app.results.replayEngineIndex;
				app.results.selectedThreadIndex = app.results.replayThreadIndex;
				app.results.selectedRepeatIndex = app.results.replayRepeatIndex;
				app.ui.replayPage = NativeReplayPage_Player;
				app.model.view = NativeArenaView_Replay;
				RequestRayTracingImage(&app, app.model.rayView, app.model.rayPhase, app.model.rayApi, app.model.rayChannel);
			}
			else openStatus = OpenReplayView(&app, NativeReplayStartup_Paused);
			if (openStatus != ArenaStatus_Ok)
			{
				app.model.startupStatus = app.results.replayError.code;
				app.model.startupError = app.results.replayError;
				if (app.results.replayEngineIndex >= result.engineCount ||
				    app.results.replayThreadIndex >= result.threadCount ||
				    app.results.replayRepeatIndex >= result.repeatCount)
				{
					app.results.selectedEngineIndex = 0;
					app.results.selectedThreadIndex = 0;
					app.results.selectedRepeatIndex = 0;
					CloseRayTracingView(&app);
					app.results.replayEngineIndex = 0;
					app.results.replayThreadIndex = 0;
					app.results.replayRepeatIndex = 0;
					app.results.replayError = {};
				}
			}
		}
		if (app.model.startupAction == NativeStartupAction_Run)
		{
			app.model.startupAction = NativeStartupAction_None;
			command = NativeUiCommand_Start;
		}
		else
		{
			command = static_cast<NativeUiCommand>(RenderFrame(&app));
			// drain trickled input, then allow hover and popup size measurement to render
			const ImGuiContext& context = *ImGui::GetCurrentContext();
			const ImGuiHoveredFlags hoverFlags = context.NavHighlightItemUnderNav && context.NavCursorVisible
			                                         ? context.Style.HoverFlagsForTooltipNav
			                                         : context.Style.HoverFlagsForTooltipMouse;
			const float hoverDelay =
			    (hoverFlags & ImGuiHoveredFlags_DelayNormal) != 0
			        ? context.Style.HoverDelayNormal
			        : ((hoverFlags & ImGuiHoveredFlags_DelayShort) != 0 ? context.Style.HoverDelayShort : 0.0f);
			if (context.InputEventsQueue.Size != 0 ||
			    (context.HoverItemDelayId != 0 &&
			     (context.HoverItemDelayTimer < hoverDelay ||
			      ((hoverFlags & ImGuiHoveredFlags_Stationary) != 0 &&
			       context.HoverItemUnlockedStationaryId != context.HoverItemDelayId))))
				app.interactionRenderFrames = 3;
			if (app.resizeRenderFrames <= 1 &&
			    (context.InputEventsQueue.Size == 0 ||
			     (app.platform.frameInputRequired != 0 &&
			      context.InputEventsQueue[0].EventId > app.platform.frameInputEventId)) &&
			    (app.platform.frameInputRequired != 0 || (HIWORD(GetQueueStatus(QS_INPUT)) & QS_INPUT) == 0) &&
			    (command == NativeUiCommand_None || (command == NativeUiCommand_LoadReplay &&
			     app.workspace.finalization.manifest.recordingKind == RecordingKind_NativeRayHits)))
			{
				PlatformRaylibCompleteFrame(&app.platform);
				if (app.results.rayLoadingPresented == PresenceStatus_Present)
				{
					CompleteRayTracingRequest(&app);
					app.interactionRenderFrames = 3;
				}
			}
			if (app.resizeRenderFrames > 0)
				app.resizeRenderFrames -= 1;
			if (app.interactionRenderFrames > 0)
				app.interactionRenderFrames -= 1;
			app.renderRequested = app.resizeRenderFrames > 0 || app.interactionRenderFrames > 0 ||
			                              app.resultViewSelectionPending == PresenceStatus_Present ||
			                              app.results.rayLoadRequest != NativeRayLoadRequest_None
			                          ? PresenceStatus_Present
			                          : PresenceStatus_Absent;
		}
		if (command == NativeUiCommand_LoadPreset || command == NativeUiCommand_SavePreset ||
		    command == NativeUiCommand_RecommendedPreset)
			ApplyRunPresetCommand(&app, command);
		else if (command >= NativeUiCommand_AddQueue && command <= NativeUiCommand_ClearPendingQueue)
			ApplyNativeRunQueueCommand(&app, command);
		else if (command == NativeUiCommand_Start)
		{
			app.uiError = {};
			if (RunWorkspaceBusy(&app) != PresenceStatus_Present && app.queue.editingId == 0)
			{
				ArenaStatus status = ArenaStatus_Ok;
				if (PendingNativeRunQueueCount(app.queue) == 0)
					status = AppendNativeRunQueue(&app.queue, &app.model, app.model.selection, &app.uiError);
				if (status == ArenaStatus_Ok && ArmNativeRunQueue(&app.queue, &app.model, &app.uiError) == ArenaStatus_Ok)
				{
					ResetNativeActivity(&app);
					AdvanceNativeRunQueue(&app);
				}
			}
			app.renderRequested = PresenceStatus_Present;
		}
		else if (command == NativeUiCommand_Cancel)
		{
			StopNativeRunQueue(&app.queue);
			CancelNativeAction(&app.action);
			if (ActiveAction(app.action) != PresenceStatus_Present) RefreshResultLibrary(&app, &error);
			app.renderRequested = PresenceStatus_Present;
		}
		else if (command == NativeUiCommand_Retry)
		{
			InitializeNativeArenaModel(repositoryRoot.data(), argc > 1 ? argc - 1 : 0, argc > 1 ? argv + 1 : nullptr,
			                           &app.model, &error);
			if (app.model.startupStatus == ArenaStatus_Ok)
			{
				StatusRecord preferencesError = {};
				if (LoadNativeRunPreferences(argc > 1 ? argc - 1 : 0, &app.model, &preferences, &preferencesError) !=
				    ArenaStatus_Ok)
					app.uiError = preferencesError;
				InitializeResults(&app);
			}
			app.renderRequested = PresenceStatus_Present;
		}
		else if (command == NativeUiCommand_RefreshResults && RunWorkspaceBusy(&app) != PresenceStatus_Present)
		{
			if (RefreshResultLibrary(&app, &error) == ArenaStatus_Ok && app.action.kind == NativeActionKind_Report)
				app.uiError = {};
			app.renderRequested = PresenceStatus_Present;
		}
		else if (command == NativeUiCommand_RegenerateReport && RunWorkspaceBusy(&app) != PresenceStatus_Present)
		{
			const PresenceStatus previousModel = app.results.modelPresence;
			const PresenceStatus previousLibrary = app.results.libraryPresence;
			app.results.modelPresence = PresenceStatus_Absent;
			app.results.libraryPresence = PresenceStatus_Absent;
			const ArenaStatus reportStatus = StartNativeReportRegeneration(
			    &app.model, app.results.selectedDirectory.data(), &app.workspace, &app.action, &app.results.error);
			app.results.status = reportStatus;
			if (reportStatus != ArenaStatus_Ok)
			{
				std::array<char, kDetailCapacity> reason = {};
				std::snprintf(reason.data(), reason.size(), "Regenerate report failed: %.*s",
				              static_cast<int>(app.results.error.detailSize), app.results.error.detail.data());
				app.results.status =
				    ResultLibraryError(&app.results.error, app.results.selectedDirectory.data(), reason.data());
				app.results.modelPresence = previousModel;
				app.results.libraryPresence = previousLibrary;
			}
			else
			{
				ResetNativeActivity(&app);
				app.lastActionPhase = NativeActionPhase_Executing;
			}
			app.renderRequested = PresenceStatus_Present;
		}
		else if (command == NativeUiCommand_DeleteRecordings && RunWorkspaceBusy(&app) != PresenceStatus_Present)
		{
			app.uiError = {};
			CloseRayTracingView(&app);
			if (app.replay != nullptr)
			{
				const ReplayStorageSelection& selection = app.results.deletionSelection;
				for (const std::filesystem::path& directory : selection.resultDirectories)
				{
					const NativeReplayComparison* comparison = app.replay->comparison;
					const int peerAffected = comparison != nullptr && comparison->peer != nullptr &&
					    _wcsicmp(directory.c_str(), comparison->peer->directory.c_str()) == 0 && (selection.scope != ReplayStorageScope_Tuple ||
					    comparison->peer->tuple == std::array<std::uint32_t, 3>{selection.engineOrdinal, selection.threadOrdinal, selection.repeatIndex});
					if (peerAffected != 0)
					{
						CloseReplayView(&app);
						break;
					}
					if (_wcsicmp(directory.c_str(), app.results.selectedDirectory.data()) == 0 &&
					    (selection.scope != ReplayStorageScope_Tuple || app.replay->selection ==
					        std::array<std::uint32_t, 3>{selection.engineOrdinal, selection.threadOrdinal, selection.repeatIndex}))
					{
						CloseReplayView(&app);
						break;
					}
				}
			}
			app.ui.replayPage = NativeReplayPage_Browser;
			app.results.status =
			    StartNativeStorageDeletion(&app.model, app.results.deletionSelection, app.results.deletionPreview,
				                           &app.workspace, &app.action, &app.results.error);
			if (app.results.status == ArenaStatus_Ok)
			{
				ResetNativeActivity(&app);
				app.lastActionPhase = NativeActionPhase_Executing;
			}
			app.renderRequested = PresenceStatus_Present;
		}
		else if (command == NativeUiCommand_OpenReplay)
		{
			PauseReplayView(&app);
			app.results.replayOpenPending = PresenceStatus_Present;
			app.ui.replayPage = NativeReplayPage_Player;
			app.model.view = NativeArenaView_Replay;
			app.renderRequested = PresenceStatus_Present;
		}
		else if (command == NativeUiCommand_LoadReplay)
		{
			app.results.replayOpenPending = PresenceStatus_Absent;
			OpenReplayView(&app, NativeReplayStartup_Playing);
		}
		else if (command == NativeUiCommand_CloseReplay)
		{
			CloseReplayView(&app);
			app.model.view = NativeArenaView_Results;
		}
		else if (command == NativeUiCommand_OpenResultFolder)
			OpenResultArtifact(&app, PresenceStatus_Absent);
		else if (command == NativeUiCommand_OpenReport)
			OpenResultArtifact(&app, PresenceStatus_Present);
		if (app.results.recordingRefreshPending == PresenceStatus_Present)
		{
			RefreshSelectedReplayStorage(&app);
			app.renderRequested = PresenceStatus_Present;
		}
	}
	StatusRecord saveError = {};
	const ArenaStatus saveStatus = SaveNativeRunPreferences(&preferences, &app.model, &saveError);
	DestroyNativeAction(&app.action);
	CloseReplayView(&app);
	CloseRayTracingView(&app);
	delete app.timingComparisonSamples;
	delete app.events;
	RendererRaylibDestroy();
	PlatformRaylibDestroy(&app.platform);
	return saveStatus == ArenaStatus_Ok ? RenderViewerStatus_Ok : RenderViewerStatus_RouteUnavailable;
}
}
