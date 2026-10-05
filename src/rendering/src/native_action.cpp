#include "benchmark_visual/native_action.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <memory>
#include <string_view>

namespace benchmark_visual
{
namespace
{
using namespace physics_arena;

ArenaStatus ActionError(StatusRecord* error, ArenaStatus status, std::string_view detail)
{
	*error = {};
	constexpr std::string_view component = "native_action";
	const std::string_view statusText = ArenaStatusText(status);
	std::copy(component.begin(), component.end(), error->component.begin());
	error->componentSize = static_cast<std::uint32_t>(component.size());
	std::copy(statusText.begin(), statusText.end(), error->status.begin());
	error->statusSize = static_cast<std::uint32_t>(statusText.size());
	if (detail.size() > error->detail.size())
		detail = "native_action_detail_capacity";
	std::copy(detail.begin(), detail.end(), error->detail.begin());
	error->detailSize = static_cast<std::uint32_t>(detail.size());
	error->code = status;
	return status;
}

void ResetActionRecords(NativeActionState* action)
{
	action->request = {};
	action->paths = {};
	action->invocation = {};
	action->execution = {};
	action->finalization = {};
	action->error = {};
	action->resultDirectory = {};
	action->storageSelection = {};
	action->storageConfirmed = {};
	action->storageResult = {};
	action->storagePhase.store(ReplayStoragePhase_Inspecting, std::memory_order_relaxed);
	action->storageCompleted.store(0, std::memory_order_relaxed);
	action->storageTotal.store(0, std::memory_order_relaxed);
	action->model = nullptr;
	action->workspace = nullptr;
	action->kind = NativeActionKind_None;
}

ArenaStatus FinalizeCompletedAction(NativeActionState* action, StatusRecord* error);

void PublishStorageProgress(void* context, ReplayStoragePhase phase, std::uint32_t completed, std::uint32_t total)
{
	NativeActionState* action = static_cast<NativeActionState*>(context);
	action->storageTotal.store(total, std::memory_order_relaxed);
	action->storageCompleted.store(completed, std::memory_order_relaxed);
	action->storagePhase.store(phase, std::memory_order_release);
}

DWORD WINAPI NativeActionThread(void* opaque)
{
	NativeActionState* action = static_cast<NativeActionState*>(opaque);
	if (action->kind == NativeActionKind_Run || action->kind == NativeActionKind_Report)
	{
		std::destroy_at(action->workspace);
		std::construct_at(action->workspace);
	}
	ArenaStatus status = ArenaStatus_InvalidArgument;
	if (action->kind == NativeActionKind_Storage)
	{
		const ReplayStorageOperation operation = {&action->control.cancellationRequested, action, PublishStorageProgress};
		InvocationEvent event = {};
		StatusRecord eventError = {};
		AppendInvocationEvent(&action->invocation, "recording_storage", "running", "Inspecting recordings", &event, &eventError);
		PushEvent(&action->events, &event);
		status = DeleteReplayStorage(&action->model->catalog, action->storageSelection, action->storageConfirmed,
		                             &action->storageResult, &action->error, &operation);
		std::array<char, 192> summary = {};
		std::snprintf(summary.data(), summary.size(), "%s: removed %u, remaining %zu, failed %zu",
		    status == ArenaStatus_Interrupted ? "Deletion interrupted" : "Deletion finished",
		    action->storageResult.removedCount,
		    action->storageResult.inventoryComplete == PresenceStatus_Present ? action->storageResult.remaining.files.size() : action->storageConfirmed.files.size(),
		    action->storageResult.failures.size());
		AppendInvocationEvent(&action->invocation, "recording_storage", ArenaStatusText(status), summary.data(), &event, &eventError);
		PushEvent(&action->events, &event);
	}
	else if (action->kind == NativeActionKind_Report)
	{
		status = RegenerateResultReports(
		    action->model->repositoryRoot.data(), action->resultDirectory.data(), &action->model->catalog,
		    &action->workspace->finalization.manifest, &action->workspace->finalization.model,
		    &action->workspace->finalization.timingScratch, &action->workspace->finalization.indexes,
		    &action->finalization.reports, &action->error);
	}
	else if (action->kind == NativeActionKind_Run)
	{
		status = ExecuteHeadlessRequest(action->model->repositoryRoot.data(), &action->model->catalog,
		                                &action->model->releaseCatalog, &action->model->host, &action->request,
		                                &action->paths, &action->control, &action->invocation, &action->events,
		                                &action->execution, &action->error);
	}
	if (status == ArenaStatus_Ok && action->kind == NativeActionKind_Run)
	{
		action->phase.store(NativeActionPhase_Finalizing, std::memory_order_release);
		StatusRecord finalizationError = {};
		status = FinalizeCompletedAction(action, &finalizationError);
		if (status != ArenaStatus_Ok)
		{
			std::array<char, kComponentCapacity + 1> component = {};
			std::array<char, kStatusCapacity + 1> statusText = {};
			std::array<char, kDetailCapacity + 1> detail = {};
			std::copy_n(action->error.component.begin(), action->error.componentSize, component.begin());
			std::copy_n(action->error.status.begin(), action->error.statusSize, statusText.begin());
			std::copy_n(action->error.detail.begin(), action->error.detailSize, detail.begin());
			InvocationEvent diagnostic = {};
			StatusRecord diagnosticError = {};
			AppendInvocationEvent(&action->invocation, component.data(), statusText.data(), detail.data(), &diagnostic,
			                      &diagnosticError);
		}
	}
	const StatusRecord actionError = action->error;
	InvocationEvent finish = {};
	StatusRecord finishError = {};
	std::int32_t exitCode = status == ArenaStatus_Ok ? 0 : (status == ArenaStatus_Interrupted ? 130 : 2);
	if (FinishInvocation(&action->invocation, exitCode, &finish, &finishError) != ArenaStatus_Ok &&
	    status == ArenaStatus_Ok)
	{
		status = finishError.code;
		action->error = finishError;
		exitCode = 2;
	}
	else
		action->error = actionError;
	DestroyInvocation(&action->invocation);
	NativeActionPhase completed = NativeActionPhase_Succeeded;
	if (status == ArenaStatus_Ok && action->kind == NativeActionKind_Run)
	{
		if (!action->execution.failures.empty() || action->execution.qualityFailedRepeatCount != 0)
			completed = NativeActionPhase_CompletedWithFailures;
		for (std::uint32_t row = 0; row < action->workspace->finalization.model.summaryRowCount; ++row)
			if (action->workspace->finalization.model.summaryRows[row].outcome == ObservationOutcome_Failed)
				completed = NativeActionPhase_CompletedWithFailures;
	}
	action->phase.store(status == ArenaStatus_Ok ? completed
	                                             : (status == ArenaStatus_Interrupted ? NativeActionPhase_Interrupted
	                                                                                  : NativeActionPhase_Failed),
	                    std::memory_order_release);
	return static_cast<DWORD>(exitCode);
}

ArenaStatus FinalizeCompletedAction(NativeActionState* action, StatusRecord* error)
{
	std::destroy_at(action->workspace);
	std::construct_at(action->workspace);
	const ArenaStatus status =
	    FinalizeFreshRunArtifacts(action->model->repositoryRoot.data(), &action->model->catalog,
		                          &action->model->releaseCatalog, &action->model->host, &action->request,
		                          &action->paths, &action->workspace->finalization, &action->finalization, error);
	action->error = *error;
	return status;
}

ArenaStatus LaunchActionThread(NativeActionState* action, const char* command, StatusRecord* error)
{
	InitializeEventTransport(&action->events);
	InitializeRunExecutionControl(&action->control);
	ArenaStatus status = StartInvocation(action->model->repositoryRoot.data(), command, &action->invocation, error);
	if (status != ArenaStatus_Ok)
	{
		action->error = *error;
		action->phase.store(NativeActionPhase_Failed, std::memory_order_release);
		return status;
	}
	action->phase.store(NativeActionPhase_Executing, std::memory_order_release);
	DWORD threadId = 0;
	HANDLE thread = CreateThread(nullptr, kWorkerStackReservationBytes, NativeActionThread, action,
	                             STACK_SIZE_PARAM_IS_A_RESERVATION, &threadId);
	if (thread != nullptr)
	{
		action->threadHandle = thread;
		return ArenaStatus_Ok;
	}
	InvocationEvent finish = {};
	StatusRecord finishError = {};
	if (FinishInvocation(&action->invocation, 2, &finish, &finishError) != ArenaStatus_Ok)
		DestroyInvocation(&action->invocation);
	ActionError(error, ArenaStatus_RunFailed, "native_action_thread_start");
	action->error = *error;
	action->phase.store(NativeActionPhase_Failed, std::memory_order_release);
	return ArenaStatus_RunFailed;
}
} // namespace

ArenaStatus StartNativeAction(NativeArenaModel* model, const NativeRunSelection& selection, NativeActionWorkspace* workspace, NativeActionState* action,
                              StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (model == nullptr || workspace == nullptr || action == nullptr || error == nullptr ||
	    action->phase.load(std::memory_order_acquire) == NativeActionPhase_Executing || action->threadHandle != nullptr)
		return error != nullptr ? ActionError(error, ArenaStatus_InvalidArgument, "native_action_start_arguments")
		                        : ArenaStatus_InvalidArgument;
	ResetActionRecords(action);
	action->model = model;
	action->workspace = workspace;
	action->kind = NativeActionKind_Run;
	ArenaStatus status = PrepareNativeRunRequest(model, selection, &action->request, error);
	if (status == ArenaStatus_Ok)
		status = CreateRunPaths(model->repositoryRoot.data(), &model->catalog, &model->host, &action->request,
		                        &action->paths, error);
	if (status != ArenaStatus_Ok)
	{
		action->error = *error;
		action->phase.store(NativeActionPhase_Failed, std::memory_order_release);
		return status;
	}
	return LaunchActionThread(action, "PhysicsArena run", error);
}

ArenaStatus StartNativeReportRegeneration(NativeArenaModel* model, const wchar_t* resultDirectory,
                                          NativeActionWorkspace* workspace, NativeActionState* action,
                                          StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (model == nullptr || resultDirectory == nullptr || resultDirectory[0] == L'\0' || workspace == nullptr ||
	    action == nullptr || error == nullptr ||
	    action->phase.load(std::memory_order_acquire) == NativeActionPhase_Executing || action->threadHandle != nullptr)
		return error != nullptr ? ActionError(error, ArenaStatus_InvalidArgument, "native_report_start_arguments")
		                        : ArenaStatus_InvalidArgument;
	const std::size_t pathSize = std::wcslen(resultDirectory);
	if (pathSize + 1 > action->resultDirectory.size())
		return ActionError(error, ArenaStatus_InvalidArgument, "native_report_path_capacity");
	std::array<wchar_t, kRunPathCapacity> selectedDirectory = {};
	std::copy(resultDirectory, resultDirectory + pathSize + 1, selectedDirectory.begin());
	ResetActionRecords(action);
	action->model = model;
	action->workspace = workspace;
	action->kind = NativeActionKind_Report;
	action->resultDirectory = selectedDirectory;
	return LaunchActionThread(action, "PhysicsArena Regenerate Report", error);
}

ArenaStatus StartNativeStorageDeletion(NativeArenaModel* model, const ReplayStorageSelection& selection,
                                       const ReplayStorageInventory& confirmed, NativeActionWorkspace* workspace,
                                       NativeActionState* action, StatusRecord* error)
{
	if (action->threadHandle != nullptr || action->phase.load(std::memory_order_acquire) == NativeActionPhase_Executing)
		return ActionError(error, ArenaStatus_InvalidArgument, "storage_requires_idle_action");
	ResetActionRecords(action);
	action->model = model;
	action->workspace = workspace;
	action->kind = NativeActionKind_Storage;
	action->storageSelection = selection;
	action->storageConfirmed = confirmed;
	return LaunchActionThread(action, "PhysicsArena Delete Recordings", error);
}

NativeActionPhase PollNativeAction(NativeActionState* action, StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (action == nullptr || error == nullptr)
		return NativeActionPhase_Failed;
	NativeActionPhase phase = static_cast<NativeActionPhase>(action->phase.load(std::memory_order_acquire));
	if ((phase == NativeActionPhase_Succeeded || phase == NativeActionPhase_CompletedWithFailures || phase == NativeActionPhase_Failed ||
	     phase == NativeActionPhase_Interrupted) &&
	    action->threadHandle != nullptr &&
	    WaitForSingleObject(static_cast<HANDLE>(action->threadHandle), 0) == WAIT_OBJECT_0)
	{
		CloseHandle(static_cast<HANDLE>(action->threadHandle));
		action->threadHandle = nullptr;
		if (phase != NativeActionPhase_Succeeded && phase != NativeActionPhase_CompletedWithFailures)
			*error = action->error;
	}
	return phase;
}

std::array<char, 192> FormatNativeStorageProgress(const NativeActionState& action)
{
	std::array<char, 192> text = {};
	if (action.control.cancellationRequested.load(std::memory_order_acquire) != 0)
		std::snprintf(text.data(), text.size(), "Cancelling deletion");
	else if (action.storagePhase.load(std::memory_order_acquire) == ReplayStoragePhase_Inspecting)
		std::snprintf(text.data(), text.size(), "Inspecting recordings");
	else
		std::snprintf(text.data(), text.size(), "Deleting recordings %u / %u",
		    action.storageCompleted.load(std::memory_order_relaxed), action.storageTotal.load(std::memory_order_relaxed));
	return text;
}

void CancelNativeAction(NativeActionState* action)
{
	if (action == nullptr)
		return;
	CancelRunExecution(&action->control);
}

void DestroyNativeAction(NativeActionState* action)
{
	if (action == nullptr)
		return;
	if (action->threadHandle != nullptr)
	{
		CancelRunExecution(&action->control);
		WaitForSingleObject(static_cast<HANDLE>(action->threadHandle), INFINITE);
		CloseHandle(static_cast<HANDLE>(action->threadHandle));
		action->threadHandle = nullptr;
	}
}

} // namespace benchmark_visual
