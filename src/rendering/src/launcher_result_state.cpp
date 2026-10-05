#include "launcher_app_internal.h"
#include "benchmark_visual/native_replay_comparison.h"

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
#include <string_view>

namespace benchmark_visual
{
using namespace physics_arena;
ArenaStatus OpenResultArtifact(PhysicsArenaApp* app, PresenceStatus report)
{
	if (app->results.modelPresence != PresenceStatus_Present)
		return ArenaStatus_InvalidArgument;
	std::array<wchar_t, kRunPathCapacity> target = app->results.selectedDirectory;
	if (report == PresenceStatus_Present && AppendWide(&target, L"report.md") != ArenaStatus_Ok)
	{
		app->results.status =
		    ResultLibraryError(&app->results.error, target.data(), "Open report path exceeds capacity");
		app->renderRequested = PresenceStatus_Present;
		return app->results.status;
	}
	const std::string_view component = report == PresenceStatus_Present ? "result_open_report" : "result_open_folder";
	const HINSTANCE result = ShellExecuteW(static_cast<HWND>(app->platform.nativeHandle), L"open", target.data(),
	                                       nullptr, app->model.repositoryRoot.data(), SW_SHOWNORMAL);
	if (reinterpret_cast<std::intptr_t>(result) > 32)
	{
		if (std::string_view(app->results.error.component.data(), app->results.error.componentSize) == component)
		{
			app->results.error = {};
			app->results.status = ArenaStatus_Ok;
		}
		return ArenaStatus_Ok;
	}
	std::array<char, 128> reason = {};
	std::snprintf(reason.data(), reason.size(), "%s failed (ShellExecute code %lld)",
	              report == PresenceStatus_Present ? "Open report" : "Open folder",
	              static_cast<long long>(reinterpret_cast<std::intptr_t>(result)));
	app->results.status = ResultLibraryError(&app->results.error, target.data(), reason.data());
	std::copy(component.begin(), component.end(), app->results.error.component.begin());
	app->results.error.componentSize = static_cast<std::uint32_t>(component.size());
	app->renderRequested = PresenceStatus_Present;
	return app->results.status;
}

std::string_view IndexText(const ResultIndexText& text)
{
	return std::string_view(text.data.data(), text.size);
}

std::uint32_t AvailableEngineMask(const ResultViewModel& model)
{
	const std::uint32_t count = std::min(model.engineCount, static_cast<std::uint32_t>(kEngineCapacity));
	return count == 0 ? 0u : (1u << count) - 1u;
}

ArenaStatus AppendWide(std::array<wchar_t, kRunPathCapacity>* path, std::wstring_view value)
{
	std::size_t size = std::wcslen(path->data());
	if (size != 0 && (*path)[size - 1] != L'\\' && (*path)[size - 1] != L'/')
	{
		if (size + 2 > path->size())
			return ArenaStatus_InvalidResult;
		(*path)[size++] = L'\\';
	}
	if (value.size() + 1 > path->size() - size)
		return ArenaStatus_InvalidResult;
	std::copy(value.begin(), value.end(), path->begin() + size);
	(*path)[size + value.size()] = L'\0';
	return ArenaStatus_Ok;
}

ArenaStatus AppendUtf8(std::array<wchar_t, kRunPathCapacity>* path, std::string_view value)
{
	if (value.empty() || value.size() > static_cast<std::size_t>(INT_MAX))
		return ArenaStatus_InvalidResult;
	std::array<wchar_t, kResultIndexTextCapacity> wide = {};
	const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
	                                        wide.data(), static_cast<int>(wide.size() - 1));
	return written > 0 ? AppendWide(path, std::wstring_view(wide.data(), static_cast<std::size_t>(written)))
	                   : ArenaStatus_InvalidResult;
}

ArenaStatus ResolveResultDirectory(const NativeArenaModel& model, const wchar_t* supplied,
                                   std::array<wchar_t, kRunPathCapacity>* output)
{
	if (supplied == nullptr || supplied[0] == L'\0')
		return ArenaStatus_InvalidArgument;
	std::array<wchar_t, kRunPathCapacity> candidate = {};
	if ((supplied[0] != L'\0' && supplied[1] == L':') || (supplied[0] == L'\\' && supplied[1] == L'\\'))
	{
		const std::size_t size = std::wcslen(supplied);
		if (size + 1 > candidate.size())
			return ArenaStatus_InvalidResult;
		std::copy(supplied, supplied + size + 1, candidate.begin());
	}
	else
	{
		candidate = model.repositoryRoot;
		if (AppendWide(&candidate, supplied) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
	}
	std::array<wchar_t, kRunPathCapacity> normalizedRoot = {};
	std::array<wchar_t, kRunPathCapacity> normalizedPath = {};
	if (GetFullPathNameW(model.repositoryRoot.data(), static_cast<DWORD>(normalizedRoot.size()), normalizedRoot.data(),
	                     nullptr) == 0 ||
	    GetFullPathNameW(candidate.data(), static_cast<DWORD>(normalizedPath.size()), normalizedPath.data(), nullptr) ==
	        0)
		return ArenaStatus_InvalidResult;
	const std::size_t rootSize = std::wcslen(normalizedRoot.data());
	if (_wcsnicmp(normalizedRoot.data(), normalizedPath.data(), rootSize) != 0 ||
	    (normalizedPath[rootSize] != L'\0' && normalizedPath[rootSize] != L'\\' && normalizedPath[rootSize] != L'/'))
		return ArenaStatus_InvalidArgument;
	const DWORD attributes = GetFileAttributesW(normalizedPath.data());
	if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
		return ArenaStatus_InvalidResult;
	*output = normalizedPath;
	return ArenaStatus_Ok;
}

ArenaStatus BuildRecordDirectory(const NativeArenaModel& model, const ResultIndexRecord& record,
                                 std::array<wchar_t, kRunPathCapacity>* output)
{
	*output = model.repositoryRoot;
	return AppendWide(output, ResultStorageRoot(record.storage)) == ArenaStatus_Ok &&
	               AppendUtf8(output, IndexText(record.caseSlug)) == ArenaStatus_Ok &&
	               AppendUtf8(output, IndexText(record.cpuSlug)) == ArenaStatus_Ok &&
	               AppendUtf8(output, IndexText(record.runSlug)) == ArenaStatus_Ok
	           ? ArenaStatus_Ok
			   : ArenaStatus_InvalidResult;
}

PresenceStatus SelectResultRecord(PhysicsArenaApp* app, const wchar_t* directory)
{
	std::array<wchar_t, kRunPathCapacity> normalized = {};
	const DWORD normalizedSize =
	    GetFullPathNameW(directory, static_cast<DWORD>(normalized.size()), normalized.data(), nullptr);
	if (normalizedSize == 0 || normalizedSize >= normalized.size())
		return PresenceStatus_Absent;
	const ResultIndexWorkspace& indexes = app->workspace.finalization.indexes;
	for (std::uint32_t index = 0; index < indexes.recordCount; ++index)
	{
		std::array<wchar_t, kRunPathCapacity> candidate = {};
		std::array<wchar_t, kRunPathCapacity> normalizedCandidate = {};
		if (BuildRecordDirectory(app->model, indexes.records[index], &candidate) != ArenaStatus_Ok)
			continue;
		const DWORD candidateSize = GetFullPathNameW(candidate.data(), static_cast<DWORD>(normalizedCandidate.size()),
		                                             normalizedCandidate.data(), nullptr);
		if (candidateSize != 0 && candidateSize < normalizedCandidate.size() &&
		    _wcsicmp(normalizedCandidate.data(), normalized.data()) == 0)
		{
			app->results.selectedRecordIndex = index;
			return PresenceStatus_Present;
		}
	}
	return PresenceStatus_Absent;
}

void RebuildSummaryRows(PhysicsArenaApp* app)
{
	NativeResultsState& state = app->results;
	const ResultViewModel& model = app->workspace.finalization.model;
	state.summaryRowCount = 0;
	if (model.measurementMode == ResultMeasurementMode_PhysicalQuality)
		return;
	for (std::uint32_t index = 0; index < model.summaryRowCount; ++index)
	{
		const ResultSummaryViewRow& row = model.summaryRows[index];
		if (row.engineOrdinal >= kEngineCapacity || (state.summarySelectedEngineMask & (1u << row.engineOrdinal)) == 0)
			continue;
		if (state.summaryChartMode == NativeSummaryChartMode_Compare &&
		    row.threadCount != model.threadCounts[state.summaryThreadIndex])
			continue;
		state.summaryRowIndexes[state.summaryRowCount++] = index;
	}
	std::copy_n(state.summaryRowIndexes.begin(), state.summaryRowCount, state.summaryPlotRowIndexes.begin());
	std::sort(
	    state.summaryPlotRowIndexes.begin(), state.summaryPlotRowIndexes.begin() + state.summaryRowCount,
	    [&model, &state](std::uint32_t leftIndex, std::uint32_t rightIndex)
	    {
		    const ResultSummaryViewRow& left = model.summaryRows[leftIndex];
		    const ResultSummaryViewRow& right = model.summaryRows[rightIndex];
		    if (state.summaryChartMode == NativeSummaryChartMode_Compare)
		    {
			    if (state.summaryOrder == NativeSummaryOrder_EngineName)
			    {
				    const std::string_view leftName =
				        ResultViewTextView(&model, model.engines[left.engineOrdinal].displayName);
				    const std::string_view rightName =
				        ResultViewTextView(&model, model.engines[right.engineOrdinal].displayName);
				    if (leftName != rightName)
					    return leftName < rightName;
			    }
			    else
			    {
				    const ResultMetricDescriptor& metric = model.metrics[state.metricIndex];
				    const double leftValue = ResultMetricValue(&left, metric.id);
				    const double rightValue = ResultMetricValue(&right, metric.id);
				    if (leftValue != rightValue)
				    {
					    const PresenceStatus ascending = (metric.lowerIsBetter == PresenceStatus_Present) ==
						                                         (state.summaryOrder == NativeSummaryOrder_FastestFirst)
						                                     ? PresenceStatus_Present
						                                     : PresenceStatus_Absent;
					    return ascending == PresenceStatus_Present ? leftValue < rightValue : leftValue > rightValue;
				    }
			    }
			    if (left.engineOrdinal != right.engineOrdinal)
				    return left.engineOrdinal < right.engineOrdinal;
			    return leftIndex < rightIndex;
		    }
		    if (left.engineOrdinal != right.engineOrdinal)
			    return left.engineOrdinal < right.engineOrdinal;
		    if (left.threadCount != right.threadCount)
			    return left.threadCount < right.threadCount;
		    return leftIndex < rightIndex;
	    });
	for (std::uint32_t index = 1; index < state.summaryRowCount; ++index)
	{
		const std::uint32_t selectedIndex = state.summaryRowIndexes[index];
		const ResultSummaryViewRow& selected = model.summaryRows[selectedIndex];
		std::uint32_t position = index;
		while (position != 0)
		{
			const std::uint32_t previousIndex = state.summaryRowIndexes[position - 1];
			const ResultSummaryViewRow& previous = model.summaryRows[previousIndex];
			double selectedValue = 0.0;
			double previousValue = 0.0;
			switch (state.sortColumn)
			{
			case 0:
			{
				const std::string_view selectedName =
				    ResultViewTextView(&model, model.engines[selected.engineOrdinal].provenanceLabel);
				const std::string_view previousName =
				    ResultViewTextView(&model, model.engines[previous.engineOrdinal].provenanceLabel);
				int order = selectedName.compare(previousName);
				if (order == 0)
					order = ResultViewTextView(&model, model.engines[selected.engineOrdinal].id)
					            .compare(ResultViewTextView(&model, model.engines[previous.engineOrdinal].id));
				selectedValue = order;
				previousValue = 0;
				break;
			}
			case 1:
				selectedValue = selected.threadCount;
				previousValue = previous.threadCount;
				break;
			case 2:
			case 3:
			case 4:
			{
				const ResultMetricId family =
				    model.metrics[state.metricIndex].id >= ResultMetric_MedianWorkUnitMilliseconds
				        ? ResultMetric_MedianWorkUnitMilliseconds
				        : ResultMetric_MedianPrimaryValue;
				const ResultMetricId statistic = static_cast<ResultMetricId>(family + state.sortColumn - 2);
				selectedValue = ResultMetricValue(&selected, statistic);
				previousValue = ResultMetricValue(&previous, statistic);
				break;
			}
			default:
				selectedValue = selected.outcome;
				previousValue = previous.outcome;
				break;
			}
			const PresenceStatus before =
			    selectedValue == previousValue
			        ? (selectedIndex < previousIndex ? PresenceStatus_Present : PresenceStatus_Absent)
			        : (state.sortDirection >= 0
			               ? (selectedValue < previousValue ? PresenceStatus_Present : PresenceStatus_Absent)
			               : (selectedValue > previousValue ? PresenceStatus_Present : PresenceStatus_Absent));
			if (before != PresenceStatus_Present)
				break;
			state.summaryRowIndexes[position] = previousIndex;
			position -= 1;
		}
		state.summaryRowIndexes[position] = selectedIndex;
	}
	if (state.summaryRowCount == 0)
	{
		state.selectedResultPresence = PresenceStatus_Absent;
		return;
	}
	for (std::uint32_t index = 0; index < state.summaryRowCount; ++index)
	{
		const ResultSummaryViewRow& row = model.summaryRows[state.summaryRowIndexes[index]];
		if (row.engineOrdinal == state.selectedEngineIndex && row.threadCount == model.threadCounts[state.selectedThreadIndex])
		{
			state.selectedResultPresence = PresenceStatus_Present;
			return;
		}
	}
	const ResultSummaryViewRow& row = model.summaryRows[state.summaryRowIndexes[0]];
	std::uint32_t thread = 0;
	while (thread < model.threadCount && model.threadCounts[thread] != row.threadCount)
		++thread;
	SelectAnalysisResult(app, row.engineOrdinal, thread, kRunRepeatCapacity);

}

void SetSummaryEngineSelection(PhysicsArenaApp* app, std::uint32_t selectedMask)
{
	NativeResultsState& state = app->results;
	const std::uint32_t availableMask = AvailableEngineMask(app->workspace.finalization.model);
	selectedMask &= availableMask;
	const std::uint32_t newlySelected = selectedMask & ~state.summarySelectedEngineMask;
	state.summarySelectedEngineMask = selectedMask;
	state.summaryHiddenEngineMask = selectedMask == availableMask ? 0u : state.summaryHiddenEngineMask & ~newlySelected;
	state.summaryFitRequest = PresenceStatus_Present;
	RebuildSummaryRows(app);
}

void RefreshCaseDataDetail(PhysicsArenaApp* app)
{
	ResultViewModel& model = app->workspace.finalization.model;
	NativeResultsState& state = app->results;
	if (model.observationCount == 0)
	{
		model.observationDetail = {};
		state.status = ArenaStatus_Ok;
		state.error = {};
		return;
	}
	ObservationDetailSelection selection = {model.engines[state.caseDataEngineIndex].catalogEngineIndex,
	                                        model.threadCounts[state.caseDataThreadIndex], state.caseDataRepeatIndex};
	StatusRecord error = {};
	const CaseRecord& benchmarkCase = ResultViewCaseDefinition(&app->model.catalog, &model);
	if (ProjectObservationDetail(&model.observations, &app->model.catalog, &benchmarkCase, &selection,
	                             &model.observationDetail, &error, ResultViewConfiguration(&model)) != ArenaStatus_Ok)
	{
		state.status = error.code;
		state.error = error;
	}
	else
	{
		state.status = ArenaStatus_Ok;
		state.error = {};
	}
}

ReplayAvailability CachedReplayAvailability(const PhysicsArenaApp* app, std::uint32_t engine,
                                            std::uint32_t thread, std::uint32_t repeat)
{
	const ResultManifestRecord& manifest = app->workspace.finalization.manifest;
	if (manifest.recordingMode == RecordingMode_Off ||
	    RecordingForThread(manifest.recordingThreads, manifest.threadCounts[thread]) == RecordingMode_Off)
		return ReplayAvailability_NotRecorded;
	ReplayAvailability availability = ReplayAvailability_Unavailable;
	for (const ReplayStorageFile& file : app->results.runStorage.files)
	{
		if (file.engineOrdinal != engine || file.threadOrdinal != thread || file.repeatIndex != repeat)
			continue;
		if (file.kind == ReplayStorageKind_Recording)
			return file.availability;
		availability = ReplayAvailability_Temporary;
	}
	return availability;
}

void SelectAnalysisResult(PhysicsArenaApp* app, std::uint32_t engine, std::uint32_t thread, std::uint32_t repeat)
{
	NativeResultsState& state = app->results;
	const ResultViewModel& model = app->workspace.finalization.model;
	const PresenceStatus changed = state.selectedEngineIndex != engine || state.selectedThreadIndex != thread ? PresenceStatus_Present : PresenceStatus_Absent;
	state.selectedResultPresence = PresenceStatus_Present;
	state.selectedEngineIndex = engine;
	state.selectedThreadIndex = thread;
	state.selectedRepeatIndex = repeat;
	state.selectedReplayAvailability = CachedReplayAvailability(app, engine, thread, repeat < model.repeatCount ? repeat : 0);
	if (repeat >= model.repeatCount)
		for (std::uint32_t index = 1; index < model.repeatCount && state.selectedReplayAvailability != ReplayAvailability_Available; ++index)
		{
			const ReplayAvailability availability = CachedReplayAvailability(app, engine, thread, index);
			if (availability == ReplayAvailability_Available || state.selectedReplayAvailability == ReplayAvailability_NotRecorded)
				state.selectedReplayAvailability = availability;
		}
	if (changed != PresenceStatus_Present)
		return;
	state.repeatFitRequest = PresenceStatus_Present;
	std::array<wchar_t, kRunPathCapacity> path = state.selectedDirectory;
	if (AppendWide(&path, L"normalized.csv") != ArenaStatus_Ok)
	{
		state.repeatStatus = ResultLibraryError(&state.repeatError, path.data(), "Repeat source path exceeds capacity");
		state.repeats = {};
		return;
	}
	state.repeatStatus = ProjectResultRepeats(path.data(), &app->model.catalog, &app->workspace.finalization.manifest,
	                                        engine, model.threadCounts[thread], &state.repeats, &state.repeatError, &model);
}

void ResetResultPresentationState(PhysicsArenaApp* app)
{
	CloseRayTracingView(app);
	app->results.rayImageRequest = {};
	app->results.replayError = {};
	const ResultViewModel& model = app->workspace.finalization.model;
	app->results.mode = model.measurementMode == ResultMeasurementMode_PhysicalQuality
	                        ? NativeResultMode_CaseData
	                        : NativeResultMode_EnginePerformance;
	app->results.metricIndex = ResultMetric_MedianPrimaryValue;
	app->results.timingSelectedEngineMask =
	    model.timing.availability == PresenceStatus_Present ? AvailableEngineMask(model) : 0;
	app->results.timingHiddenEngineMask = 0;
	app->results.timingThreadIndex = 0;
	app->results.timingRepeatIndex = 0;
	app->results.replayEngineIndex = 0;
	app->results.replayThreadIndex = 0;
	app->results.replayRepeatIndex = 0;
	app->results.caseDataEngineIndex = 0;
	app->results.caseDataThreadIndex = 0;
	app->results.caseDataRepeatIndex = 0;
	app->results.summarySelectedEngineMask = AvailableEngineMask(model);
	app->results.summaryThreadIndex = model.threadCount - 1;
	app->results.summaryHiddenEngineMask = 0;
	app->results.summaryChartMode = NativeSummaryChartMode_Compare;
	app->results.summaryLayout = NativeSummaryLayout_ChartAndTable;
	app->results.summaryOrder = NativeSummaryOrder_FastestFirst;
	app->results.summaryFitRequest = PresenceStatus_Present;
	app->results.timingFitRequest = PresenceStatus_Present;
	app->results.caseDataPage = model.measurementMode == ResultMeasurementMode_PhysicalQuality ? NativeCaseDataPage_Observations : NativeCaseDataPage_About;

	app->results.selectedEngineIndex = model.engineCount;
	app->results.repeatMetricIndex = ResultViewTextView(&model, model.workUnitId) == "query_batch" ? 1 : 0;
	RebuildSummaryRows(app);
	SelectAnalysisResult(app, model.summaryRows[app->results.summaryRowIndexes[0]].engineOrdinal, app->results.summaryThreadIndex, kRunRepeatCapacity);
}

void RefreshSelectedReplayStorage(PhysicsArenaApp* app)
{
	if (app->results.modelPresence != PresenceStatus_Present)
		return;
	NativeResultsState& state = app->results;
	state.tupleStorage = {};
	state.recordingBytes.fill(0);
	const std::array<std::uint32_t, 3> choice = state.recordingPickerActive == PresenceStatus_Present ? state.recordingChoice :
	    std::array<std::uint32_t, 3>{state.replayEngineIndex, state.replayThreadIndex, state.replayRepeatIndex};
	for (const ReplayStorageFile& file : state.runStorage.files)
	{
		if (file.engineOrdinal == state.replayEngineIndex && file.threadOrdinal == state.replayThreadIndex && file.repeatIndex == state.replayRepeatIndex)
			AddReplayStorageFile(&state.tupleStorage, file);
		if (file.engineOrdinal == choice[0] && file.threadOrdinal == choice[1])
			state.recordingBytes[file.repeatIndex] += file.bytes;
	}
	state.replayAvailability = CachedReplayAvailability(app, state.replayEngineIndex, state.replayThreadIndex, state.replayRepeatIndex);
	for (std::uint32_t repeat = 0; repeat < app->workspace.finalization.model.repeatCount; ++repeat)
		state.recordingAvailability[repeat] = CachedReplayAvailability(app, choice[0], choice[1], repeat);
	state.recordingRefreshPending = PresenceStatus_Absent;
	if (state.selectedResultPresence == PresenceStatus_Present)
		SelectAnalysisResult(app, state.selectedEngineIndex, state.selectedThreadIndex, state.selectedRepeatIndex);
}

void RebuildRecordingMembership(PhysicsArenaApp* app)
{
	NativeResultsState& state = app->results;
	state.recordingRunCount = 0;
	std::vector<std::filesystem::path> runs;
	for (const ReplayStorageFile& file : state.libraryStorage.files)
		if (file.kind == ReplayStorageKind_Recording)
			runs.push_back(ReplayStorageRunDirectory(file));
	std::sort(runs.begin(), runs.end(),
	          [](const std::filesystem::path& left, const std::filesystem::path& right)
	          {
		          return _wcsicmp(left.c_str(), right.c_str()) < 0;
	          });
	runs.erase(std::unique(runs.begin(), runs.end(),
	    [](const std::filesystem::path& left, const std::filesystem::path& right)
	    {
		    return _wcsicmp(left.c_str(), right.c_str()) == 0;
	    }), runs.end());
	const ResultIndexWorkspace& indexes = app->workspace.finalization.indexes;
	for (std::uint32_t index = 0; index < indexes.recordCount; ++index)
	{
		std::array<wchar_t, kRunPathCapacity> directory = {};
		if (BuildRecordDirectory(app->model, indexes.records[index], &directory) == ArenaStatus_Ok &&
		    std::binary_search(runs.begin(), runs.end(), std::filesystem::path(directory.data()),
		        [](const std::filesystem::path& left, const std::filesystem::path& right)
		        {
			        return _wcsicmp(left.c_str(), right.c_str()) < 0;
		        }))
			state.recordingRunIndexes[state.recordingRunCount++] = index;
	}
}

void ApplyStorageDeletion(PhysicsArenaApp* app)
{
	NativeResultsState& state = app->results;
	const ReplayStorageResult& result = app->action.storageResult;
	const ReplayStorageSelection& selection = app->action.storageSelection;
	if (result.inventoryComplete != PresenceStatus_Present)
		return;
	if (selection.scope == ReplayStorageScope_Leftovers)
	{
		for (NativeRecordingLeftover& leftover : state.leftovers)
			if (leftover.directory == selection.resultDirectories.front())
				leftover.storage = result.remaining;
		return;
	}
	std::vector<std::filesystem::path> directories = selection.resultDirectories;
	std::sort(directories.begin(), directories.end(),
	          [](const std::filesystem::path& left, const std::filesystem::path& right)
	          {
		          return _wcsicmp(left.c_str(), right.c_str()) < 0;
	          });
	ReplayStorageInventory updated = {};
	updated.files.reserve(state.libraryStorage.files.size() + result.remaining.files.size());
	for (const ReplayStorageFile& file : state.libraryStorage.files)
	{
		const int selected = std::binary_search(directories.begin(), directories.end(), ReplayStorageRunDirectory(file),
		    [](const std::filesystem::path& left, const std::filesystem::path& right)
		    {
			    return _wcsicmp(left.c_str(), right.c_str()) < 0;
		    }) &&
		    (selection.scope != ReplayStorageScope_Tuple || (file.engineOrdinal == selection.engineOrdinal &&
		     file.threadOrdinal == selection.threadOrdinal && file.repeatIndex == selection.repeatIndex));
		if (selected == 0)
			AddReplayStorageFile(&updated, file);
	}
	for (const ReplayStorageFile& file : result.remaining.files)
		AddReplayStorageFile(&updated, file);
	state.libraryStorage = std::move(updated);
	state.runStorage = {};
	for (const ReplayStorageFile& file : state.libraryStorage.files)
		if (_wcsicmp(ReplayStorageRunDirectory(file).c_str(), state.selectedDirectory.data()) == 0)
			AddReplayStorageFile(&state.runStorage, file);
	RefreshSelectedReplayStorage(app);
	RebuildRecordingMembership(app);
}

ArenaStatus PrepareStorageDeletion(PhysicsArenaApp* app, ReplayStorageScope scope,
                                   const std::filesystem::path& unfinishedDirectory)
{
	if (app->action.threadHandle != nullptr)
		return ArenaStatus_InvalidArgument;
	NativeResultsState& state = app->results;
	state.deletionSelection = {};
	state.deletionPath.clear();
	state.deletionSelection.scope = scope;
	state.deletionSelection.engineOrdinal = state.replayEngineIndex;
	state.deletionSelection.threadOrdinal = state.replayThreadIndex;
	state.deletionSelection.repeatIndex = state.replayRepeatIndex;
	if (scope == ReplayStorageScope_Library)
	{
		const ResultIndexWorkspace& indexes = app->workspace.finalization.indexes;
		for (std::uint32_t index = 0; index < indexes.recordCount; ++index)
		{
			std::array<wchar_t, kRunPathCapacity> directory = {};
			if (BuildRecordDirectory(app->model, indexes.records[index], &directory) != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
			state.deletionSelection.resultDirectories.emplace_back(directory.data());
		}
	}
	else
	{
		state.deletionSelection.resultDirectories.emplace_back(
		    scope == ReplayStorageScope_Leftovers ? unfinishedDirectory
			                                      : std::filesystem::path(state.selectedDirectory.data()));
		state.deletionPath = state.deletionSelection.resultDirectories.front().string();
	}
	state.deletionSelection.validationDirectory = state.selectedDirectory.data();
	state.deletionPreview = scope == ReplayStorageScope_Library ? state.libraryStorage :
	    (scope == ReplayStorageScope_Tuple ? state.tupleStorage : state.runStorage);
	if (scope == ReplayStorageScope_Leftovers)
	{
		for (const NativeRecordingLeftover& leftover : state.leftovers)
			if (leftover.directory == unfinishedDirectory)
				state.deletionPreview = leftover.storage;
	}
	state.status = ArenaStatus_Ok;
	state.error = {};
	return state.status;
}

ArenaStatus LoadResultDirectory(PhysicsArenaApp* app, const wchar_t* directory, StatusRecord* error)
{
	std::array<wchar_t, kRunPathCapacity> previous = app->results.selectedDirectory;
	const PresenceStatus previousPresence = app->results.modelPresence;
	if (app->rayView != nullptr || app->results.rayLoadRequest != NativeRayLoadRequest_None)
		app->ui.replayPage = NativeReplayPage_Browser;
	CloseRayTracingView(app);
	if (_wcsicmp(previous.data(), directory) != 0 ||
	    (app->replay != nullptr && app->replay->comparison != nullptr &&
	     (app->replay->comparison->peer != nullptr ||
	      app->replay->comparison->picker != ReplayComparisonPickerState_Closed)))
	{
		CloseReplayView(app);
	}
	app->results.modelPresence = PresenceStatus_Absent;
	const ArenaStatus status =
	    LoadResultPresentation(app->model.repositoryRoot.data(), directory, &app->model.catalog,
		                       &app->workspace.finalization.manifest, &app->workspace.finalization.model, error);
	if (status != ArenaStatus_Ok)
	{
		if (previousPresence == PresenceStatus_Present &&
		    SelectResultRecord(app, previous.data()) == PresenceStatus_Present)
		{
			StatusRecord restoreError = {};
			if (LoadResultPresentation(app->model.repositoryRoot.data(), previous.data(), &app->model.catalog,
			                           &app->workspace.finalization.manifest, &app->workspace.finalization.model,
			                           &restoreError) == ArenaStatus_Ok)
			{
				app->results.modelPresence = PresenceStatus_Present;
				app->results.selectedDirectory = previous;
				RefreshCaseDataDetail(app);
			}
		}
		if (app->results.modelPresence != PresenceStatus_Present)
		{
			CloseReplayView(app);
			CloseRayTracingView(app);
		}
		app->results.status = status;
		app->results.error = *error;
		return status;
	}
	const std::size_t size = std::wcslen(directory);
	std::copy(directory, directory + size + 1, app->results.selectedDirectory.begin());
	if (SelectResultRecord(app, directory) == PresenceStatus_Absent)
		app->results.selectedRecordIndex = app->workspace.finalization.indexes.recordCount;
	app->results.modelPresence = PresenceStatus_Present;
	app->results.status = ArenaStatus_Ok;
	app->results.error = {};
	if (previousPresence != PresenceStatus_Present || _wcsicmp(previous.data(), directory) != 0)
	{
		ResetResultPresentationState(app);
	}
	else
	{
		const ResultViewModel& model = app->workspace.finalization.model;
		NativeResultsState& state = app->results;
		const std::uint32_t engine = state.selectedEngineIndex < model.engineCount ? state.selectedEngineIndex : 0;
		if (state.selectedThreadIndex >= model.threadCount) state.selectedThreadIndex = 0;
		if (state.selectedRepeatIndex >= model.repeatCount) state.selectedRepeatIndex = model.repeatCount;
		if (state.caseDataEngineIndex >= model.engineCount) state.caseDataEngineIndex = 0;
		if (state.caseDataThreadIndex >= model.threadCount) state.caseDataThreadIndex = 0;
		if (state.caseDataRepeatIndex >= model.repeatCount) state.caseDataRepeatIndex = 0;
		if (state.replayEngineIndex >= model.engineCount) state.replayEngineIndex = 0;
		if (state.replayThreadIndex >= model.threadCount) state.replayThreadIndex = 0;
		if (state.replayRepeatIndex >= model.repeatCount) state.replayRepeatIndex = 0;
		state.selectedEngineIndex = model.engineCount;
		SelectAnalysisResult(app, engine, app->results.selectedThreadIndex, app->results.selectedRepeatIndex);
		RebuildSummaryRows(app);
		RefreshCaseDataDetail(app);
	}
	ReplayStorageSelection storageSelection = {{directory}, ReplayStorageScope_Run, 0, 0, 0};
	const ArenaStatus storageStatus = InspectReplayStorage(&app->model.catalog, storageSelection, &app->results.runStorage, error);
	if (storageStatus != ArenaStatus_Ok)
	{
		app->results.runStorage = {};
		return storageStatus;
	}
	for (ReplayStorageFile& file : app->results.runStorage.files)
	{
		if (file.kind != ReplayStorageKind_Recording)
			continue;
		file.availability = InspectReplayAvailability(&app->model.catalog, &app->workspace.finalization.manifest,
		    directory, file.engineOrdinal, file.threadOrdinal, file.repeatIndex);
		for (ReplayStorageFile& cached : app->results.libraryStorage.files)
			if (_wcsicmp(cached.path.c_str(), file.path.c_str()) == 0 && cached.bytes == file.bytes)
				cached.availability = file.availability;
	}
	RefreshSelectedReplayStorage(app);
	return ArenaStatus_Ok;
}

ArenaStatus ResultLibraryError(StatusRecord* error, const wchar_t* path, std::string_view reason)
{
	*error = {};
	error->code = ArenaStatus_InvalidResult;
	constexpr std::string_view component = "result_library";
	constexpr std::string_view status = "invalid_result";
	std::copy(component.begin(), component.end(), error->component.begin());
	error->componentSize = static_cast<std::uint32_t>(component.size());
	std::copy(status.begin(), status.end(), error->status.begin());
	error->statusSize = static_cast<std::uint32_t>(status.size());
	std::array<char, kRunPathCapacity * 3> utf8 = {};
	WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), static_cast<int>(utf8.size()), nullptr, nullptr);
	const int size = std::snprintf(error->detail.data(), error->detail.size(), "%.*s: %s",
	                               static_cast<int>(reason.size()), reason.data(), utf8.data());
	error->detailSize = static_cast<std::uint32_t>(
	    std::min<std::size_t>(static_cast<std::size_t>(std::max(size, 0)), error->detail.size() - 1));
	return error->code;
}

ArenaStatus DiscoverLocalResults(PhysicsArenaApp* app, const std::array<wchar_t, kRunPathCapacity>& directory,
                                 std::uint32_t depth, ResultStorage storage, std::array<ResultIndexText, 3>* names, std::uint32_t* skipped,
                                 StatusRecord* error)
{
	const DWORD attributes = GetFileAttributesW(directory.data());
	if (attributes == INVALID_FILE_ATTRIBUTES)
	{
		if (depth == 0 && GetLastError() == ERROR_FILE_NOT_FOUND)
			return ArenaStatus_Ok;
		if (depth == 0 && GetLastError() == ERROR_PATH_NOT_FOUND)
			return ArenaStatus_Ok;
		*skipped += 1;
		return ResultLibraryError(error, directory.data(), "Cannot read directory");
	}
	if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
	{
		*skipped += 1;
		return ResultLibraryError(error, directory.data(), "Skipped reparse point");
	}
	if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
	{
		*skipped += 1;
		return ResultLibraryError(error, directory.data(), "Expected result directory");
	}
	if (depth == 3)
	{
		std::array<wchar_t, kRunPathCapacity> manifestPath = directory;
		if (AppendWide(&manifestPath, L"manifest.json") != ArenaStatus_Ok)
		{
			*skipped += 1;
			return ResultLibraryError(error, directory.data(), "Result path capacity");
		}
		const DWORD manifestAttributes = GetFileAttributesW(manifestPath.data());
		if (manifestAttributes == INVALID_FILE_ATTRIBUTES ||
		    (manifestAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
		{
			*skipped += 1;
			return ResultLibraryError(error, manifestPath.data(), "Missing, unreadable or linked manifest");
		}
		ResultManifestRecord manifest = {};
		StatusRecord manifestError = {};
		if (LoadResultManifest(manifestPath.data(), &app->model.catalog, &manifest, &manifestError) != ArenaStatus_Ok)
		{
			*skipped += 1;
			return ResultLibraryError(error, manifestPath.data(),
			                          std::string_view(manifestError.detail.data(), manifestError.detailSize));
		}
		ResultIndexRecord record = {};
		const ArenaStatus status =
		    BuildResultIndexRecord(&app->model.catalog, manifest, IndexText((*names)[0]), IndexText((*names)[1]),
			                       IndexText((*names)[2]), &record, &manifestError);
		if (status != ArenaStatus_Ok)
		{
			*skipped += 1;
			return ResultLibraryError(error, directory.data(),
			                          std::string_view(manifestError.detail.data(), manifestError.detailSize));
		}
		constexpr std::array<const wchar_t*, 7> finalArtifacts = {
		    L"normalized.csv",   L"summary.csv",     L"summary.svg",     L"report.md",
		    L"observations.csv", L"step-timing.csv", L"step-timing.svg",
		};
		std::uint32_t presentArtifacts = 0;
		for (std::uint32_t index = 0; index < finalArtifacts.size(); ++index)
		{
			std::array<wchar_t, kRunPathCapacity> file = directory;
			if (AppendWide(&file, finalArtifacts[index]) != ArenaStatus_Ok)
			{
				*skipped += 1;
				return ResultLibraryError(error, directory.data(), "Result path capacity");
			}
			const DWORD fileAttributes = GetFileAttributesW(file.data());
			if (fileAttributes == INVALID_FILE_ATTRIBUTES)
			{
				const DWORD fileError = GetLastError();
				if (fileError == ERROR_FILE_NOT_FOUND || fileError == ERROR_PATH_NOT_FOUND)
					continue;
				*skipped += 1;
				return ResultLibraryError(error, file.data(), "Cannot inspect result artifact");
			}
			if ((fileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
			{
				*skipped += 1;
				return ResultLibraryError(error, file.data(), "Directory or linked result artifact");
			}
			HANDLE readable =
			    CreateFileW(file.data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
				            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (readable == INVALID_HANDLE_VALUE)
			{
				*skipped += 1;
				return ResultLibraryError(error, file.data(), "Cannot read result artifact");
			}
			CloseHandle(readable);
			presentArtifacts |= 1u << index;
		}
		const std::uint32_t requiredArtifacts =
		    manifest.measurementMode == ResultMeasurementMode_PhysicalQuality ? 5 : 4;
		const std::uint32_t completedMask = (1u << requiredArtifacts) - 1;
		if ((presentArtifacts & completedMask) != completedMask)
		{
			NativeRecordingLeftover leftover = {};
			leftover.directory = directory.data();
			leftover.displayPath = leftover.directory.string();
			ResultLibraryError(&leftover.reason, directory.data(), "Unfinished run: completed measurement/report artifacts are missing");
			ReplayStorageSelection selection = {{leftover.directory}, ReplayStorageScope_Leftovers, 0, 0, 0};
			InspectReplayStorage(&app->model.catalog, selection, &leftover.storage, &manifestError);
			app->results.leftovers.push_back(std::move(leftover));
		}
		if (presentArtifacts == 0)
		{
			app->results.unfinishedResultCount += 1;
			return ArenaStatus_Ok;
		}
		if (manifest.measurementMode == ResultMeasurementMode_PhysicalQuality &&
		    (presentArtifacts & ((1u << 5) | (1u << 6))) != 0)
		{
			*skipped += 1;
			return ResultLibraryError(error, directory.data(), "Unexpected timing in physical-quality result");
		}
		for (std::uint32_t index = 0; index < requiredArtifacts; ++index)
		{
			if ((presentArtifacts & (1u << index)) != 0)
				continue;
			std::array<wchar_t, kRunPathCapacity> file = directory;
			AppendWide(&file, finalArtifacts[index]);
			*skipped += 1;
			return ResultLibraryError(error, file.data(), "Missing completed result artifact");
		}
		ResultIndexWorkspace& indexes = app->workspace.finalization.indexes;
		if (indexes.recordCount == indexes.records.size())
		{
			*skipped += 1;
			return ResultLibraryError(error, directory.data(), "Local library exceeds 256 records");
		}
		record.storage = storage;
		indexes.records[indexes.recordCount] = record;
		indexes.recordCount += 1;
		return ArenaStatus_Ok;
	}
	std::array<wchar_t, kRunPathCapacity> pattern = directory;
	if (AppendWide(&pattern, L"*") != ArenaStatus_Ok)
	{
		*skipped += 1;
		return ResultLibraryError(error, directory.data(), "Local library path capacity");
	}
	WIN32_FIND_DATAW entry = {};
	HANDLE search = FindFirstFileW(pattern.data(), &entry);
	if (search == INVALID_HANDLE_VALUE)
	{
		if (GetLastError() == ERROR_FILE_NOT_FOUND)
			return ArenaStatus_Ok;
		*skipped += 1;
		return ResultLibraryError(error, directory.data(), "Cannot enumerate local library");
	}
	do
	{
		if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || std::wcscmp(entry.cFileName, L".") == 0 ||
		    std::wcscmp(entry.cFileName, L"..") == 0 ||
		    (depth == 0 && storage == ResultStorage_Repository && _wcsicmp(entry.cFileName, L"local") == 0))
			continue;
		std::array<wchar_t, kRunPathCapacity> child = directory;
		ResultIndexText& name = (*names)[depth];
		const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, entry.cFileName, -1, name.data.data(),
		                                     static_cast<int>(name.data.size()), nullptr, nullptr);
		if (size <= 1 || AppendWide(&child, entry.cFileName) != ArenaStatus_Ok)
		{
			*skipped += 1;
			ResultLibraryError(error, directory.data(), "Local library name capacity");
			continue;
		}
		name.size = static_cast<std::uint32_t>(size - 1);
		const ArenaStatus childStatus = DiscoverLocalResults(app, child, depth + 1, storage, names, skipped, error);
		if (childStatus != ArenaStatus_Ok && app->results.issues.size() < app->workspace.finalization.indexes.records.size())
			app->results.issues.push_back({std::filesystem::path(child.data()).string(), *error});
	} while (FindNextFileW(search, &entry) != 0);
	const DWORD findError = GetLastError();
	FindClose(search);
	if (findError != ERROR_NO_MORE_FILES)
	{
		*skipped += 1;
		return ResultLibraryError(error, directory.data(), "Local library enumeration failed");
	}
	return ArenaStatus_Ok;
}

ArenaStatus RefreshResultLibrary(PhysicsArenaApp* app, StatusRecord* error)
{
	if (app->replay != nullptr && app->replay->comparison != nullptr &&
	    (app->replay->comparison->peer != nullptr ||
	     app->replay->comparison->picker != ReplayComparisonPickerState_Closed))
		CloseReplayView(app);
	ResultIndexWorkspace& indexes = app->workspace.finalization.indexes;
	ResultIndexRecord previous = {};
	if (app->results.libraryPresence == PresenceStatus_Present &&
	    app->results.selectedRecordIndex < indexes.recordCount)
		previous = indexes.records[app->results.selectedRecordIndex];
	indexes.recordCount = 0;
	std::array<wchar_t, kRunPathCapacity> root = app->model.repositoryRoot;
	std::array<ResultIndexText, 3> names = {};
	std::uint32_t skipped = 0;
	app->results.unfinishedResultCount = 0;
	app->results.leftovers.clear();
	app->results.issues.clear();
	app->results.libraryStorage = {};
	app->results.recordingRunCount = 0;
	*error = {};
	ArenaStatus status = AppendWide(&root, L"results");
	if (status == ArenaStatus_Ok)
		status = DiscoverLocalResults(app, root, 0, ResultStorage_Repository, &names, &skipped, error);
	else
	{
		skipped += 1;
		status = ResultLibraryError(error, root.data(), "Local library path capacity");
	}
	std::array<wchar_t, kRunPathCapacity> localRoot = app->model.repositoryRoot;
	if (AppendWide(&localRoot, ResultStorageRoot(ResultStorage_Local)) == ArenaStatus_Ok)
	{
		const ArenaStatus localStatus = DiscoverLocalResults(app, localRoot, 0, ResultStorage_Local, &names, &skipped, error);
		if (localStatus != ArenaStatus_Ok) status = localStatus;
	}
	if (indexes.recordCount != 0)
	{
		ReplayStorageSelection selection = {};
		selection.scope = ReplayStorageScope_Library;
		for (std::uint32_t index = 0; index < indexes.recordCount; ++index)
		{
			std::array<wchar_t, kRunPathCapacity> directory = {};
			if (BuildRecordDirectory(app->model, indexes.records[index], &directory) == ArenaStatus_Ok)
				selection.resultDirectories.emplace_back(directory.data());
		}
		StatusRecord storageError = {};
		if (InspectReplayStorage(&app->model.catalog, selection, &app->results.libraryStorage, &storageError) !=
		    ArenaStatus_Ok)
		{
			*error = storageError;
			++skipped;
		}
	}
	std::sort(indexes.records.begin(), indexes.records.begin() + indexes.recordCount,
	          [](const ResultIndexRecord& left, const ResultIndexRecord& right)
	          {
		          if (IndexText(left.caseSlug) != IndexText(right.caseSlug))
			          return IndexText(left.caseSlug) < IndexText(right.caseSlug);
		          if (IndexText(left.cpuSlug) != IndexText(right.cpuSlug))
			          return IndexText(left.cpuSlug) < IndexText(right.cpuSlug);
		          return IndexText(left.runSlug) < IndexText(right.runSlug);
	          });
	RebuildRecordingMembership(app);
	// the worker clears index storage, and the selected path still identifies its case/CPU group
	std::array<wchar_t, kRunPathCapacity> selectedGroup = {};
	const DWORD selectedSize =
	    app->results.selectedDirectory[0] != L'\0'
	        ? GetFullPathNameW(app->results.selectedDirectory.data(), static_cast<DWORD>(selectedGroup.size()),
	                           selectedGroup.data(), nullptr)
	        : 0;
	if (selectedSize != 0 && selectedSize < selectedGroup.size())
	{
		wchar_t* runName = std::wcsrchr(selectedGroup.data(), L'\\');
		if (runName != nullptr)
		{
			*runName = L'\0';
			for (std::uint32_t index = 0; index < indexes.recordCount; ++index)
			{
				std::array<wchar_t, kRunPathCapacity> candidate = {};
				std::array<wchar_t, kRunPathCapacity> candidateGroup = {};
				if (BuildRecordDirectory(app->model, indexes.records[index], &candidate) != ArenaStatus_Ok)
					continue;
				const DWORD size = GetFullPathNameW(candidate.data(), static_cast<DWORD>(candidateGroup.size()),
				                                    candidateGroup.data(), nullptr);
				if (size == 0 || size >= candidateGroup.size())
					continue;
				wchar_t* candidateRun = std::wcsrchr(candidateGroup.data(), L'\\');
				if (candidateRun == nullptr)
					continue;
				*candidateRun = L'\0';
				if (_wcsicmp(selectedGroup.data(), candidateGroup.data()) == 0)
				{
					previous = indexes.records[index];
					break;
				}
			}
		}
	}
	app->results.libraryPresence = PresenceStatus_Present;
	const StatusRecord discoveryError = *error;
	if (indexes.recordCount != 0)
	{
		std::uint32_t selected = indexes.recordCount - 1;
		for (std::uint32_t index = 0; index < indexes.recordCount; ++index)
			if (indexes.records[index].storage == previous.storage && IndexText(indexes.records[index].caseSlug) == IndexText(previous.caseSlug) &&
			    IndexText(indexes.records[index].cpuSlug) == IndexText(previous.cpuSlug))
				selected = index;
		if (SelectResultRecord(app, app->results.selectedDirectory.data()) == PresenceStatus_Present)
			selected = app->results.selectedRecordIndex;
		std::array<wchar_t, kRunPathCapacity> directory = {};
		status = BuildRecordDirectory(app->model, indexes.records[selected], &directory);
		if (status == ArenaStatus_Ok)
			status = LoadResultDirectory(app, directory.data(), error);
	}
	else
	{
		CloseReplayView(app);
		CloseRayTracingView(app);
		app->results.modelPresence = PresenceStatus_Absent;
		app->results.selectedRecordIndex = 0;
		app->results.status = ArenaStatus_Ok;
		app->results.error = {};
	}
	app->results.skippedResultCount = skipped;
	app->results.discoveryError = discoveryError;
	return status;
}

void InitializeResults(PhysicsArenaApp* app)
{
	app->resultViewSelectionPending =
	    app->model.view == NativeArenaView_Results ? PresenceStatus_Present : PresenceStatus_Absent;
	StatusRecord error = {};
	RefreshResultLibrary(app, &error);
	std::array<wchar_t, kRunPathCapacity> selected = {};
	if (app->model.resultPath[0] != L'\0')
	{
		if (ResolveResultDirectory(app->model, app->model.resultPath.data(), &selected) != ArenaStatus_Ok ||
		    LoadResultDirectory(app, selected.data(), &error) != ArenaStatus_Ok)
		{
			app->results.status = error.code == ArenaStatus_Ok ? ArenaStatus_InvalidResult : error.code;
			app->results.error = error;
		}
	}
}

}
