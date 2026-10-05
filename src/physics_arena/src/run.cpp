#include "physics_arena/ray_tracing_images.h"
#include "run_internal.h"
#include "physics_arena/replay.h"
#include "physics_arena/stack_stability.h"

#include "physics_arena/case_execution.h"
#include "physics_arena/timing_series.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cwchar>
#include <string_view>

namespace physics_arena
{
constexpr std::uint32_t kRunProcessTimeoutMilliseconds = 60U * 60U * 1000U;
constexpr std::uint32_t kRunCancellationGraceMilliseconds = 250U;

template <std::size_t Capacity>
ArenaStatus CopyText(std::array<char, Capacity>* destination, std::uint32_t* size, std::string_view source)
{
	if (source.empty() || source.size() >= Capacity)
		return ArenaStatus_InvalidResult;
	std::fill(destination->begin(), destination->end(), '\0');
	std::copy(source.begin(), source.end(), destination->begin());
	*size = static_cast<std::uint32_t>(source.size());
	return ArenaStatus_Ok;
}
ArenaStatus RunError(StatusRecord* error, ArenaStatus status, std::string_view detail)
{
	*error = {};
	CopyText(&error->component, &error->componentSize, "headless_run");
	CopyText(&error->status, &error->statusSize, ArenaStatusText(status));
	if (CopyText(&error->detail, &error->detailSize, detail) != ArenaStatus_Ok)
		CopyText(&error->detail, &error->detailSize, "run_detail_exceeded_capacity");
	error->code = status;
	return status;
}

template <std::size_t Capacity>
ArenaStatus AppendWide(std::array<wchar_t, Capacity>* output, std::uint32_t* size, std::wstring_view value)
{
	if (value.size() > Capacity - *size - 1)
		return ArenaStatus_InvalidResult;
	std::copy(value.begin(), value.end(), output->begin() + *size);
	*size += static_cast<std::uint32_t>(value.size());
	(*output)[*size] = L'\0';
	return ArenaStatus_Ok;
}

template <std::size_t Capacity>
ArenaStatus AppendUtf8AsWide(std::array<wchar_t, Capacity>* output, std::uint32_t* size, std::string_view value)
{
	if (value.empty())
		return ArenaStatus_Ok;
	if (value.size() > static_cast<std::size_t>(INT_MAX))
		return ArenaStatus_InvalidResult;
	const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
	                                        output->data() + *size, static_cast<int>(Capacity - *size - 1));
	if (written <= 0)
		return ArenaStatus_InvalidResult;
	*size += static_cast<std::uint32_t>(written);
	(*output)[*size] = L'\0';
	return ArenaStatus_Ok;
}

template <std::size_t Capacity> ArenaStatus CopyWide(std::array<wchar_t, Capacity>* output, const wchar_t* value)
{
	if (value == nullptr)
		return ArenaStatus_InvalidArgument;
	const std::size_t size = std::wcslen(value);
	if (size == 0 || size >= Capacity)
		return ArenaStatus_InvalidResult;
	std::copy(value, value + size + 1, output->begin());
	return ArenaStatus_Ok;
}

template <std::size_t Capacity>
ArenaStatus AppendPath(std::array<wchar_t, Capacity>* output, std::uint32_t* size, std::wstring_view value)
{
	if (*size != 0 && (*output)[*size - 1] != L'/' && (*output)[*size - 1] != L'\\' &&
	    AppendWide(output, size, L"\\") != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	return AppendWide(output, size, value);
}

template <std::size_t Capacity>
ArenaStatus AppendPathUtf8(std::array<wchar_t, Capacity>* output, std::uint32_t* size, std::string_view value)
{
	if (*size != 0 && (*output)[*size - 1] != L'/' && (*output)[*size - 1] != L'\\' &&
	    AppendWide(output, size, L"\\") != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	return AppendUtf8AsWide(output, size, value);
}

template <std::size_t Capacity>
ArenaStatus WideToUtf8(const wchar_t* value, std::array<char, Capacity>* output, std::uint32_t* size)
{
	const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, output->data(),
	                                        static_cast<int>(output->size()), nullptr, nullptr);
	if (written <= 1 || written > static_cast<int>(output->size()))
		return ArenaStatus_InvalidResult;
	*size = static_cast<std::uint32_t>(written - 1);
	return ArenaStatus_Ok;
}

int RunnableRoute(RouteStatus status)
{
	return status == RouteStatus_Supported || status == RouteStatus_Working || status == RouteStatus_Experimental;
}

int ContainsThread(const std::array<std::uint32_t, kThreadCountCapacity>& values, std::uint32_t count,
                   std::uint32_t value)
{
	for (std::uint32_t index = 0; index < count; ++index)
		if (values[index] == value)
			return 1;
	return 0;
}

ArenaStatus AppendThread(std::array<std::uint32_t, kThreadCountCapacity>* values, std::uint32_t* count,
                         std::uint32_t value, StatusRecord* error)
{
	if (value == 0 || *count >= values->size())
		return RunError(error, ArenaStatus_InvalidResult, "thread_count_capacity");
	if (ContainsThread(*values, *count, value) != 0)
		return ArenaStatus_Ok;
	(*values)[(*count)++] = value;
	return ArenaStatus_Ok;
}

void SortThreads(std::array<std::uint32_t, kThreadCountCapacity>* values, std::uint32_t count)
{
	std::sort(values->begin(), values->begin() + count);
}

int EngineSupportsThreadCount(const Catalog* catalog, const EngineRecord& engine, std::uint32_t threadCount)
{
	if (engine.threadSupportMode == ThreadSupportMode_Single)
		return threadCount == 1 ? 1 : 0;
	if (engine.threadSupportMode == ThreadSupportMode_HostBounded)
		return 1;
	if (engine.threadSupportMode != ThreadSupportMode_Explicit ||
	    engine.supportedThreadCountsOffset > catalog->valueCount ||
	    engine.supportedThreadCount > catalog->valueCount - engine.supportedThreadCountsOffset)
		return -1;
	for (std::uint32_t index = 0; index < engine.supportedThreadCount; ++index)
		if (catalog->values[engine.supportedThreadCountsOffset + index] == threadCount)
			return 1;
	return 0;
}

ArenaStatus FilterAutomaticThreads(const Catalog* catalog, PreparedRunRequest* request, StatusRecord* error)
{
	std::uint32_t retainedCount = 0;
	for (std::uint32_t threadIndex = 0; threadIndex < request->threadCount; ++threadIndex)
	{
		const std::uint32_t threadCount = request->threadCounts[threadIndex];
		int supported = 1;
		for (std::uint32_t engineIndex = 0; engineIndex < request->engineCount; ++engineIndex)
		{
			const EngineRecord& engine = catalog->engines[request->engineIndexes[engineIndex]];
			const int engineSupport = EngineSupportsThreadCount(catalog, engine, threadCount);
			if (engineSupport < 0)
				return RunError(error, ArenaStatus_InvalidResult,
				                engine.threadSupportMode == ThreadSupportMode_Explicit ? "engine_thread_range"
				                                                                       : "unknown_thread_support_mode");
			if (engineSupport == 0)
			{
				supported = 0;
				break;
			}
		}
		if (supported != 0)
			request->threadCounts[retainedCount++] = threadCount;
		else if (AppendThread(&request->droppedThreadCounts, &request->droppedThreadCount, threadCount, error) !=
		         ArenaStatus_Ok)
			return error->code;
	}
	request->threadCount = retainedCount;
	return ArenaStatus_Ok;
}

ArenaStatus ResolveConfiguredThreads(const Catalog* catalog, const CaseRecord& benchmarkCase, std::uint32_t upperBound,
                                     std::array<std::uint32_t, kThreadCountCapacity>* output,
                                     std::uint32_t* outputCount, StatusRecord* error)
{
	if (benchmarkCase.threadCount == 0 || benchmarkCase.threadCountsOffset > catalog->valueCount ||
	    benchmarkCase.threadCount > catalog->valueCount - benchmarkCase.threadCountsOffset)
		return RunError(error, ArenaStatus_InvalidResult, "case_thread_range");
	for (std::uint32_t index = 0; index < benchmarkCase.threadCount; ++index)
	{
		const std::uint32_t value = catalog->values[benchmarkCase.threadCountsOffset + index];
		if (value == 0)
			return RunError(error, ArenaStatus_InvalidResult, "configured_thread_count_zero");
		if (value <= upperBound && AppendThread(output, outputCount, value, error) != ArenaStatus_Ok)
			return error->code;
	}
	SortThreads(output, *outputCount);
	return *outputCount == 0 ? RunError(error, ArenaStatus_InvalidResult, "resolved_thread_counts_empty")
	                         : ArenaStatus_Ok;
}

ArenaStatus ValidateEngineThreadSupport(const Catalog* catalog, const EngineRecord& engine,
                                        const PreparedRunRequest* request, StatusRecord* error)
{
	for (std::uint32_t threadIndex = 0; threadIndex < request->threadCount; ++threadIndex)
	{
		const std::uint32_t requested = request->threadCounts[threadIndex];
		const int support = EngineSupportsThreadCount(catalog, engine, requested);
		if (support < 0)
			return RunError(error, ArenaStatus_InvalidResult,
			                engine.threadSupportMode == ThreadSupportMode_Explicit ? "engine_thread_range"
			                                                                       : "unknown_thread_support_mode");
		if (support == 0 && engine.threadSupportMode == ThreadSupportMode_Single)
			return RunError(error, ArenaStatus_InvalidResult, "single_thread_engine_selection");
		if (support == 0)
			return RunError(error, ArenaStatus_InvalidResult, "unsupported_thread_count");
	}
	return ArenaStatus_Ok;
}

int CaseRouteSupported(const Catalog* catalog, const EngineRecord& engine, std::uint32_t caseIndex)
{
	if (caseIndex >= catalog->caseCount)
		return 0;
	return (engine.supportedCaseFamilyMask & (1u << catalog->cases[caseIndex].authoredCaseIndex)) != 0 &&
	       RunnableRoute(engine.runStatus);
}

ArenaStatus SelectDefaultEngines(const Catalog* catalog, PreparedRunRequest* request, StatusRecord* error)
{
	const PresenceStatus presence = catalog->defaultReleaseSetPresence;
	const std::uint32_t setIndex = catalog->defaultReleaseSetIndex;
	if (presence != PresenceStatus_Present || setIndex >= catalog->engineSetCount)
		return RunError(error, ArenaStatus_InvalidResult, "default_engine_set_missing");
	const EngineSetRecord& set = catalog->engineSets[setIndex];
	if (set.engineCount == 0 || set.engineCount > request->engineIndexes.size() ||
	    set.engineIndexesOffset > catalog->valueCount ||
	    set.engineCount > catalog->valueCount - set.engineIndexesOffset)
		return RunError(error, ArenaStatus_InvalidResult, "default_engine_set_range");
	request->engineCount = 0;
	for (std::uint32_t index = 0; index < set.engineCount; ++index)
	{
		const std::uint32_t engineIndex = catalog->values[set.engineIndexesOffset + index];
		if (CaseRouteSupported(catalog, catalog->engines[engineIndex], request->caseIndex) != 0)
			request->engineIndexes[request->engineCount++] = engineIndex;
	}
	return ArenaStatus_Ok;
}

template <std::size_t Capacity>
ArenaStatus AppendQuotedArgument(std::array<wchar_t, Capacity>* command, std::uint32_t* size, const wchar_t* argument)
{
	if (*size != 0 && AppendWide(command, size, L" ") != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	const std::size_t argumentSize = std::wcslen(argument);
	int quote = argumentSize == 0 ? 1 : 0;
	for (std::size_t index = 0; index < argumentSize; ++index)
		if (argument[index] == L' ' || argument[index] == L'\t' || argument[index] == L'"')
			quote = 1;
	if (quote == 0)
		return AppendWide(command, size, std::wstring_view(argument, argumentSize));
	if (AppendWide(command, size, L"\"") != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	std::uint32_t slashCount = 0;
	for (std::size_t index = 0; index < argumentSize; ++index)
	{
		const wchar_t character = argument[index];
		if (character == L'\\')
		{
			slashCount += 1;
			continue;
		}
		const std::uint32_t copies = character == L'"' ? slashCount * 2 + 1 : slashCount;
		for (std::uint32_t copy = 0; copy < copies; ++copy)
			if (AppendWide(command, size, L"\\") != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
		slashCount = 0;
		if (AppendWide(command, size, std::wstring_view(&character, 1)) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
	}
	for (std::uint32_t copy = 0; copy < slashCount * 2; ++copy)
		if (AppendWide(command, size, L"\\") != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
	return AppendWide(command, size, L"\"");
}

ArenaStatus BuildEnvironment(const ReleaseCatalog* releaseCatalog, const ReleaseArtifactRecord& artifact,
                             ProcessSpec* spec)
{
	(void)releaseCatalog;
	(void)artifact;
	(void)spec;
	return ArenaStatus_Ok;
}

int SlugCharacter(char character)
{
	return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
}

template <std::size_t Capacity>
ArenaStatus AppendSlug(std::array<char, Capacity>* output, std::uint32_t* size, std::string_view value)
{
	int separator = *size != 0 ? 1 : 0;
	for (char character : value)
	{
		if (character >= 'A' && character <= 'Z')
			character += 'a' - 'A';
		if (SlugCharacter(character) != 0)
		{
			if (separator != 0 && *size != 0 && (*output)[*size - 1] != '-')
			{
				if (*size + 1 >= Capacity)
					return ArenaStatus_InvalidResult;
				(*output)[(*size)++] = '-';
			}
			separator = 0;
			if (*size + 1 >= Capacity)
				return ArenaStatus_InvalidResult;
			(*output)[(*size)++] = character;
		}
		else
			separator = 1;
	}
	while (*size != 0 && (*output)[*size - 1] == '-')
		*size -= 1;
	(*output)[*size] = '\0';
	return ArenaStatus_Ok;
}

ArenaStatus EnsureDirectory(const wchar_t* path)
{
	const DWORD attributes = GetFileAttributesW(path);
	if (attributes != INVALID_FILE_ATTRIBUTES)
		return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? ArenaStatus_Ok : ArenaStatus_InvalidResult;
	return CreateDirectoryW(path, nullptr) != 0 ? ArenaStatus_Ok : ArenaStatus_RunFailed;
}

template <std::size_t Capacity>
ArenaStatus BuildChildPath(const wchar_t* parent, std::wstring_view child, std::array<wchar_t, Capacity>* output)
{
	std::uint32_t size = 0;
	return AppendWide(output, &size, parent) == ArenaStatus_Ok && AppendPath(output, &size, child) == ArenaStatus_Ok
	           ? ArenaStatus_Ok
			   : ArenaStatus_InvalidResult;
}

template <std::size_t Capacity>
ArenaStatus BuildChildPathUtf8(const wchar_t* parent, std::string_view child, std::array<wchar_t, Capacity>* output)
{
	std::uint32_t size = 0;
	return AppendWide(output, &size, parent) == ArenaStatus_Ok && AppendPathUtf8(output, &size, child) == ArenaStatus_Ok
	           ? ArenaStatus_Ok
			   : ArenaStatus_InvalidResult;
}

ArenaStatus AppendRunUtf8AsWide(std::array<wchar_t, kRunPathCapacity>* output, std::uint32_t* size,
                                std::string_view value)
{
	return AppendUtf8AsWide(output, size, value);
}

ArenaStatus BuildRunChildPath(const wchar_t* parent, std::wstring_view child,
                              std::array<wchar_t, kRunPathCapacity>* output)
{
	return BuildChildPath(parent, child, output);
}

ArenaStatus BuildRunChildPathUtf8(const wchar_t* parent, std::string_view child,
                                  std::array<wchar_t, kRunPathCapacity>* output)
{
	return BuildChildPathUtf8(parent, child, output);
}

void InitializeRunExecutionControl(RunExecutionControl* control)
{
	if (control == nullptr)
		return;
	control->cancellationRequested.store(0, std::memory_order_relaxed);
	control->compressingReplay.store(0, std::memory_order_relaxed);
	control->completedUnitCount.store(0, std::memory_order_relaxed);
	control->failedUnitCount.store(0, std::memory_order_relaxed);
	control->startedProcessCount.store(0, std::memory_order_relaxed);
	control->passedRepeatCount.store(0, std::memory_order_relaxed);
	control->unverifiedRepeatCount.store(0, std::memory_order_relaxed);
	control->qualityFailedRepeatCount.store(0, std::memory_order_relaxed);
	control->executionFailedRepeatCount.store(0, std::memory_order_relaxed);
	control->skippedRepeatCount.store(0, std::memory_order_relaxed);
	control->currentEngineIndex.store(0, std::memory_order_relaxed);
	control->currentThreadCount.store(0, std::memory_order_relaxed);
	control->currentRepeatIndex.store(0, std::memory_order_relaxed);
}

void CancelRunExecution(RunExecutionControl* control)
{
	if (control != nullptr)
		control->cancellationRequested.store(1, std::memory_order_release);
}

ArenaStatus PrepareRunRequest(const Catalog* catalog, const ReleaseCatalog* releaseCatalog, const HostRecord* host,
                              const RunRequestInput* input, PreparedRunRequest* request, StatusRecord* error)
{
	if (request != nullptr)
		*request = {};
	if (error != nullptr)
		*error = {};
	if (catalog == nullptr || releaseCatalog == nullptr || host == nullptr || input == nullptr || request == nullptr ||
	    error == nullptr)
		return error != nullptr ? RunError(error, ArenaStatus_InvalidArgument, "invalid_prepare_arguments")
		                        : ArenaStatus_InvalidArgument;
	if (input->caseIndex >= catalog->caseCount || input->repeatCount == 0 || input->repeatCount > kRunRepeatCapacity)
		return RunError(error, ArenaStatus_InvalidArgument, "invalid_run_selection");
	if (ValidateHostRecord(host, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	if (input->storage != ResultStorage_Local && input->storage != ResultStorage_Repository)
		return RunError(error, ArenaStatus_InvalidArgument, "invalid_result_storage");
	request->storage = input->storage;
	request->caseIndex = input->caseIndex;
	if (input->recordingMode != RecordingMode_Off && input->recordingMode != RecordingMode_On)
		return RunError(error, ArenaStatus_InvalidArgument, "invalid_recording_mode");
	request->recordingMode = input->recordingMode;
	if (input->verificationMode != VerificationMode_On && input->verificationMode != VerificationMode_Off)
		return RunError(error, ArenaStatus_InvalidArgument, "invalid_verification_mode");
	request->verificationMode = input->verificationMode;
	request->recordingKind = catalog->cases[input->caseIndex].fixtureKind == CaseFixtureKind_RayTracing
	                             ? RecordingKind_NativeRayHits : RecordingKind_Transforms;
	request->recordingVersion = input->recordingMode == RecordingMode_On
	                                ? (request->recordingKind == RecordingKind_NativeRayHits ? 1u : kCompressedReplayVersion) : 0;
	request->repeatCount = input->repeatCount;
	request->hostLogicalThreadCount = host->logicalThreadCount;
	request->requestedMaximumThreadCount = input->requestedMaximumThreadCount;
	request->threadSelectionMode = input->threadSelectionMode;
	if (input->engineCount == 0)
	{
		if (SelectDefaultEngines(catalog, request, error) != ArenaStatus_Ok)
			return error->code;
	}
	else
	{
		if (input->engineCount > input->engineIndexes.size())
			return RunError(error, ArenaStatus_InvalidArgument, "engine_selection_capacity");
		request->engineCount = input->engineCount;
		for (std::uint32_t index = 0; index < input->engineCount; ++index)
		{
			const std::uint32_t engineIndex = input->engineIndexes[index];
			if (engineIndex >= catalog->engineCount)
				return RunError(error, ArenaStatus_InvalidArgument, "engine_index");
			for (std::uint32_t prior = 0; prior < index; ++prior)
				if (request->engineIndexes[prior] == engineIndex)
					return RunError(error, ArenaStatus_InvalidArgument, "duplicate_engine");
			request->engineIndexes[index] = engineIndex;
		}
	}
	if (ComposeRunSettings(catalog, request->caseIndex, &input->settings,
	                       std::span<const std::uint32_t>(request->engineIndexes.data(), request->engineCount),
	                       &request->configuration, error) != ArenaStatus_Ok)
		return error->code;
	const CaseRecord& benchmarkCase = request->configuration.benchmarkCase;
	if (request->verificationMode == VerificationMode_On && benchmarkCase.fixtureKind == CaseFixtureKind_BoxContactIslands &&
	    benchmarkCase.measuredWorkUnitCount <= benchmarkCase.timestepHz)
		return RunError(error, ArenaStatus_InvalidArgument,
		    "contact_islands_terminal_window requires_measured_steps=" + std::to_string(benchmarkCase.timestepHz + 1) + " final_1s_measured_poses");
	if (benchmarkCase.measuredWorkUnitCount == 0 || benchmarkCase.measuredWorkUnitCount > kTimingProjectionStepCapacity)
		return RunError(error, ArenaStatus_InvalidResult, "case_step_count_timing_capacity");
	if (input->threadSelectionMode == ThreadSelectionMode_Explicit)
	{
		if (input->threadCount == 0 || input->threadCount > input->threadCounts.size())
			return RunError(error, ArenaStatus_InvalidArgument, "explicit_threads_empty_or_capacity");
		for (std::uint32_t index = 0; index < input->threadCount; ++index)
		{
			const std::uint32_t value = input->threadCounts[index];
			if (value == 0 || value > host->logicalThreadCount)
				return RunError(error, ArenaStatus_InvalidResult, "explicit_thread_exceeds_host");
			if (ContainsThread(request->threadCounts, request->threadCount, value) != 0)
				return RunError(error, ArenaStatus_InvalidArgument, "duplicate_thread_count");
			const int caseSupport = CaseSupportsThreadCount(catalog, request->caseIndex, value);
			if (caseSupport < 0)
				return RunError(error, ArenaStatus_InvalidResult, "case_thread_range");
			if (caseSupport == 0)
				return RunError(error, ArenaStatus_InvalidArgument, "case_thread_count_unsupported");
			request->threadCounts[request->threadCount++] = value;
		}
	}
	else if (input->threadSelectionMode == ThreadSelectionMode_Default)
	{
		if (input->threadCount != 0 || input->requestedMaximumThreadCount != 0)
			return RunError(error, ArenaStatus_InvalidArgument, "default_thread_selector_payload");
		if (ResolveConfiguredThreads(catalog, benchmarkCase, host->logicalThreadCount, &request->threadCounts,
		                             &request->threadCount, error) != ArenaStatus_Ok)
			return error->code;
	}
	else if (input->threadSelectionMode == ThreadSelectionMode_Maximum)
	{
		if (input->threadCount != 0 || input->requestedMaximumThreadCount == 0)
			return RunError(error, ArenaStatus_InvalidArgument, "maximum_thread_selector_payload");
		request->effectiveMaximumThreadCount = std::min(input->requestedMaximumThreadCount, host->logicalThreadCount);
		if (ResolveConfiguredThreads(catalog, benchmarkCase, request->effectiveMaximumThreadCount,
		                             &request->threadCounts, &request->threadCount, error) != ArenaStatus_Ok)
			return error->code;
	}
	else
		return RunError(error, ArenaStatus_InvalidArgument, "thread_selection_mode");
	if (input->threadSelectionMode != ThreadSelectionMode_Explicit &&
	    FilterAutomaticThreads(catalog, request, error) != ArenaStatus_Ok)
		return error->code;
	if (benchmarkCase.fixtureKind == CaseFixtureKind_SpatialQueryTrace)
	{
		const CaseExecutionSpatialQuery& query = request->configuration.execution.spatialQuery;
		for (std::uint32_t engine = 0; engine < request->engineCount; ++engine)
		{
			const std::string_view engineId =
			    CatalogTextView(catalog, catalog->engines[request->engineIndexes[engine]].id);
			if (engineId == "entasis")
				continue;
			for (std::uint32_t index = 0; index < request->threadCount; ++index)
				if (std::min({query.rayCount, query.sphereCastCount, query.overlapCount}) <
				    request->threadCounts[index])
					return RunError(error, ArenaStatus_InvalidResult,
					                "setting=query_family_count below_selected_threads");
		}
	}
	if (input->threadSelectionMode == ThreadSelectionMode_Explicit)
	{
		request->requestedThreadCount = request->threadCount;
		std::copy(request->threadCounts.begin(), request->threadCounts.begin() + request->threadCount,
		          request->requestedThreadCounts.begin());
	}
	else
	{
		for (std::uint32_t index = 0; index < benchmarkCase.threadCount; ++index)
		{
			const std::uint32_t configured = catalog->values[benchmarkCase.threadCountsOffset + index];
			if (input->threadSelectionMode == ThreadSelectionMode_Default)
			{
				request->requestedThreadCounts[request->requestedThreadCount++] = configured;
				if (configured > host->logicalThreadCount)
					request->droppedThreadCounts[request->droppedThreadCount++] = configured;
			}
		}
		for (std::uint32_t selectedIndex = 0; selectedIndex < request->threadCount; ++selectedIndex)
		{
			int configured = 0;
			for (std::uint32_t index = 0; index < benchmarkCase.threadCount; ++index)
				if (catalog->values[benchmarkCase.threadCountsOffset + index] == request->threadCounts[selectedIndex])
					configured = 1;
			if (configured == 0)
				request->generatedThreadCounts[request->generatedThreadCount++] = request->threadCounts[selectedIndex];
		}
	}
	if (request->engineCount == 0 || request->threadCount == 0)
		return RunError(error, ArenaStatus_InvalidResult, "empty_run_matrix");
	if (input->recordingThreadSelection != RecordingThreadSelection_All &&
	    input->recordingThreadSelection != RecordingThreadSelection_Explicit)
		return RunError(error, ArenaStatus_InvalidArgument, "invalid_recording_thread_selection");
	if (input->recordingThreadSelection == RecordingThreadSelection_Explicit)
	{
		if (input->recordingThreads.count > kThreadCountCapacity)
			return RunError(error, ArenaStatus_InvalidArgument, "recording_thread_capacity");
		for (std::uint32_t index = 0; index < input->recordingThreads.count; ++index)
		{
			const std::uint32_t count = input->recordingThreads.counts[index];
			if (count == 0 || count > kThreadCountCapacity ||
			    ContainsThread(request->threadCounts, request->threadCount, count) == 0 ||
			    ContainsThread(input->recordingThreads.counts, index, count) != 0)
				return RunError(error, ArenaStatus_InvalidArgument, "recording_threads_must_be_unique_benchmark_threads");
		}
	}
	if (input->recordingMode == RecordingMode_On)
	{
		request->recordingThreads = input->recordingThreadSelection == RecordingThreadSelection_All ?
		    RecordingThreadSet{request->threadCounts, request->threadCount} : input->recordingThreads;
		if (request->recordingThreads.count == 0)
			return RunError(error, ArenaStatus_InvalidArgument, "Select at least one replay thread beside Save replay");
	}
	std::uint64_t processCount = 0;
	for (std::uint32_t selectionIndex = 0; selectionIndex < request->engineCount; ++selectionIndex)
	{
		const std::uint32_t engineIndex = request->engineIndexes[selectionIndex];
		if (engineIndex >= catalog->engineCount)
			return RunError(error, ArenaStatus_InvalidResult, "engine_index");
		const EngineRecord& engine = catalog->engines[engineIndex];
		if (CaseRouteSupported(catalog, engine, request->caseIndex) == 0)
			return RunError(error, ArenaStatus_InvalidResult, "engine_route_preflight");
		if (engine.requestedWorkerPolicy == WorkerCountPolicy_Unknown ||
		    engine.effectiveWorkerPolicy == WorkerCountPolicy_Unknown ||
		    engine.mainThreadParticipation == MainThreadParticipation_Unknown)
			return RunError(error, ArenaStatus_InvalidResult, "engine_worker_policy");
		if (engineIndex >= releaseCatalog->engineArtifactAvailability.size() ||
		    releaseCatalog->engineArtifactAvailability[engineIndex] != PresenceStatus_Present)
			return RunError(error, ArenaStatus_ToolMissing, "release_artifact_missing");
		const std::uint32_t artifactIndex = releaseCatalog->engineArtifactIndexes[engineIndex];
		if (artifactIndex >= releaseCatalog->artifactCount ||
		    releaseCatalog->artifacts[artifactIndex].engineIndex != engineIndex)
			return RunError(error, ArenaStatus_InvalidResult, "release_artifact_mapping");
		request->artifactIndexes[selectionIndex] = artifactIndex;
		if (ValidateEngineThreadSupport(catalog, engine, request, error) != ArenaStatus_Ok)
			return error->code;
		processCount += static_cast<std::uint64_t>(request->threadCount) * request->repeatCount;
	}
	const std::uint64_t unitCount = static_cast<std::uint64_t>(request->engineCount) * request->threadCount;
	if (unitCount > UINT32_MAX || processCount > UINT32_MAX)
		return RunError(error, ArenaStatus_InvalidResult, "run_matrix_capacity");
	request->totalUnitCount = static_cast<std::uint32_t>(unitCount);
	request->totalProcessCount = static_cast<std::uint32_t>(processCount);
	return ArenaStatus_Ok;
}

ArenaStatus RenderRunProcessSpec(const wchar_t* repositoryRoot, const wchar_t* resultDirectory, const Catalog* catalog,
                                 const ReleaseCatalog* releaseCatalog, const PreparedRunRequest* request,
                                 std::uint32_t selectedEngineIndex, std::uint32_t selectedThreadIndex,
                                 std::uint32_t repeatIndex, std::string_view stackEndpoint, ProcessSpec* spec, StatusRecord* error, RayRunStage rayStage)
{
	if (spec != nullptr)
		*spec = {};
	if (error != nullptr)
		*error = {};
	if (repositoryRoot == nullptr || resultDirectory == nullptr || catalog == nullptr || releaseCatalog == nullptr ||
	    request == nullptr || spec == nullptr || error == nullptr || selectedEngineIndex >= request->engineCount ||
	    selectedThreadIndex >= request->threadCount)
		return error != nullptr ? RunError(error, ArenaStatus_InvalidArgument, "invalid_render_arguments")
		                        : ArenaStatus_InvalidArgument;
	const std::uint32_t engineIndex = request->engineIndexes[selectedEngineIndex];
	const std::uint32_t artifactIndex = request->artifactIndexes[selectedEngineIndex];
	if (engineIndex >= catalog->engineCount || artifactIndex >= releaseCatalog->artifactCount)
		return RunError(error, ArenaStatus_InvalidResult, "render_mapping");
	const EngineRecord& engine = catalog->engines[engineIndex];
	const ReleaseArtifactRecord& artifact = releaseCatalog->artifacts[artifactIndex];
	if (artifact.engineIndex != engineIndex)
		return RunError(error, ArenaStatus_InvalidResult, "render_artifact_engine");
	if (repeatIndex >= request->repeatCount)
		return RunError(error, ArenaStatus_InvalidArgument, "repeat_index");
	const std::uint32_t threadCount = request->threadCounts[selectedThreadIndex];
	std::array<wchar_t, kProcessPathCapacity> threadDirectory = {};
	std::uint32_t threadDirectorySize = 0;
	std::array<wchar_t, 32> threadToken = {};
	std::swprintf(threadToken.data(), threadToken.size(), L"t%u", threadCount);
	if (AppendWide(&threadDirectory, &threadDirectorySize, resultDirectory) != ArenaStatus_Ok ||
	    AppendPath(&threadDirectory, &threadDirectorySize, L"raw") != ArenaStatus_Ok ||
	    AppendPathUtf8(&threadDirectory, &threadDirectorySize, CatalogTextView(catalog, engine.id)) != ArenaStatus_Ok ||
	    AppendPath(&threadDirectory, &threadDirectorySize, threadToken.data()) != ArenaStatus_Ok ||
	    CopyWide(&spec->workingDirectory, threadDirectory.data()) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_InvalidResult, "run_working_directory_capacity");
	std::array<wchar_t, kProcessPathCapacity> artifactPath = {};
	std::uint32_t artifactPathSize = 0;
	if (AppendWide(&artifactPath, &artifactPathSize, repositoryRoot) != ArenaStatus_Ok ||
	    AppendPathUtf8(&artifactPath, &artifactPathSize, ReleaseTextView(releaseCatalog, artifact.executablePath)) !=
	        ArenaStatus_Ok)
		return RunError(error, ArenaStatus_InvalidResult, "artifact_path_capacity");
	if (CopyWide(&spec->executablePath, artifactPath.data()) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_InvalidResult, "executable_path_capacity");
	std::array<wchar_t, kProcessPathCapacity> rawName = {};
	std::array<wchar_t, kProcessPathCapacity> rawPath = {};
	std::array<wchar_t, kProcessPathCapacity> timingName = {};
	std::array<wchar_t, kProcessPathCapacity> timingPath = {};
	std::swprintf(rawName.data(), rawName.size(), L"%.*S_t%u_raw.csv",
	              static_cast<int>(CatalogTextView(catalog, engine.id).size()),
	              CatalogTextView(catalog, engine.id).data(), threadCount);
	std::uint32_t rawPathSize = 0;
	if (AppendWide(&rawPath, &rawPathSize, threadDirectory.data()) != ArenaStatus_Ok ||
	    AppendPath(&rawPath, &rawPathSize, rawName.data()) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_InvalidResult, "raw_path_capacity");
	std::swprintf(timingName.data(), timingName.size(), L"%.*S_t%u_r%u_step-timing.csv",
	              static_cast<int>(CatalogTextView(catalog, engine.id).size()),
	              CatalogTextView(catalog, engine.id).data(), threadCount, repeatIndex);
	std::uint32_t timingPathSize = 0;
	if (AppendWide(&timingPath, &timingPathSize, threadDirectory.data()) != ArenaStatus_Ok ||
	    AppendPath(&timingPath, &timingPathSize, timingName.data()) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_InvalidResult, "timing_path_capacity");
	std::uint32_t commandSize = 0;
	if (AppendQuotedArgument(&spec->commandLine, &commandSize, artifactPath.data()) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_InvalidResult, "command_capacity");
	if (artifact.producerPrefixOffset > releaseCatalog->producerPrefixCount ||
	    artifact.producerPrefixCount > releaseCatalog->producerPrefixCount - artifact.producerPrefixOffset)
		return RunError(error, ArenaStatus_InvalidResult, "producer_prefix_range");
	for (std::uint32_t argumentIndex = 0; argumentIndex < artifact.producerPrefixCount; ++argumentIndex)
	{
		std::array<wchar_t, kProcessPathCapacity> rendered = {};
		const CatalogText argument =
		    releaseCatalog->producerPrefixArguments[artifact.producerPrefixOffset + argumentIndex];
		const std::string_view text = ReleaseTextView(releaseCatalog, argument);
		const int converted =
		    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
			                    rendered.data(), static_cast<int>(rendered.size() - 1));
		if (converted <= 0 || AppendQuotedArgument(&spec->commandLine, &commandSize, rendered.data()) != ArenaStatus_Ok)
			return RunError(error, ArenaStatus_InvalidResult, "producer_prefix_argument");
	}
	std::array<char, kCaseExecutionHexCapacity + 1> contractHex = {};
	std::uint32_t contractHexSize = 0;
	if (EncodeEffectiveCaseExecutionHex(&request->configuration, selectedEngineIndex, contractHex.data(),
	                                    static_cast<std::uint32_t>(contractHex.size()), &contractHexSize,
	                                    error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	std::array<wchar_t, kCaseExecutionHexCapacity + 17> contractArgument = {};
	constexpr std::wstring_view contractPrefix = L"--case-contract=";
	std::copy(contractPrefix.begin(), contractPrefix.end(), contractArgument.begin());
	for (std::uint32_t index = 0; index < contractHexSize; ++index)
		contractArgument[contractPrefix.size() + index] = static_cast<wchar_t>(contractHex[index]);
	if (AppendQuotedArgument(&spec->commandLine, &commandSize, contractArgument.data()) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_InvalidResult, "command_capacity");
	if (AppendQuotedArgument(&spec->commandLine, &commandSize,
	                         request->verificationMode == VerificationMode_On ? L"--verify=on" : L"--verify=off") != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_InvalidResult, "command_capacity");
	if (request->verificationMode == VerificationMode_On && benchmark_stack::TargetFixture(request->configuration.execution.fixtureKind) != 0)
	{
		if (stackEndpoint.empty()) return RunError(error, ArenaStatus_InvalidArgument, "stack_endpoint_missing");
		std::array<wchar_t, kProcessPathCapacity> endpointArgument = {};
		std::uint32_t endpointSize = 0;
		if (AppendWide(&endpointArgument, &endpointSize, L"--stack-stream=") != ArenaStatus_Ok ||
		    AppendUtf8AsWide(&endpointArgument, &endpointSize, stackEndpoint) != ArenaStatus_Ok ||
		    AppendQuotedArgument(&spec->commandLine, &commandSize, endpointArgument.data()) != ArenaStatus_Ok)
			return RunError(error, ArenaStatus_InvalidResult, "stack_endpoint_capacity");
	}
	else if (!stackEndpoint.empty()) return RunError(error, ArenaStatus_InvalidArgument, "stack_endpoint_incompatible");
	if (CatalogTextView(catalog, engine.id) == "unity_physics" && !CaseExecutionIsQuery(request->configuration.execution.fixtureKind))
	{
		const EngineRunSettings& profile = request->configuration.selectedEngineSettings[selectedEngineIndex];
		if (profile.solverStabilizationPresence != PresenceStatus_Present)
			return RunError(error, ArenaStatus_InvalidResult, "solver_stabilization_unavailable");
		if (AppendQuotedArgument(&spec->commandLine, &commandSize,
		    profile.solverStabilization == CaseExecutionToggle_Enabled ? L"--contact-solver-stabilization=enabled" :
		        L"--contact-solver-stabilization=disabled") != ArenaStatus_Ok)
			return RunError(error, ArenaStatus_InvalidResult, "command_capacity");
	}
	std::filesystem::path recordingPath;
	if (RecordingForThread(request->recordingThreads, threadCount) == RecordingMode_On)
	{
		recordingPath = request->recordingKind == RecordingKind_NativeRayHits
	                    ? RayImageTuplePath(resultDirectory, CatalogTextView(catalog, engine.id), threadCount, repeatIndex)
	                    : ReplayTuplePath(resultDirectory, CatalogTextView(catalog, engine.id), threadCount, repeatIndex);
		if (request->recordingKind == RecordingKind_NativeRayHits || request->recordingVersion == kCompressedReplayVersion)
			recordingPath = recordingPath.parent_path() / ".pending" / recordingPath.filename();
	}
	std::array<std::array<wchar_t, kProcessPathCapacity>, 5> arguments = {};
	std::swprintf(arguments[0].data(), arguments[0].size(), L"--thread-count=%u", threadCount);
	std::swprintf(arguments[1].data(), arguments[1].size(), L"--repeat-index=%u", repeatIndex);
	std::swprintf(arguments[2].data(), arguments[2].size(), L"--step-timing-output=%ls", timingPath.data());
	std::swprintf(arguments[3].data(), arguments[3].size(), L"--output=%ls", rawPath.data());
	std::swprintf(arguments[4].data(), arguments[4].size(), L"--recording-output=%ls", recordingPath.c_str());
	for (std::uint32_t index = 0; index < arguments.size(); ++index)
	{
		if (index == 4 && RecordingForThread(request->recordingThreads, threadCount) == RecordingMode_Off)
			continue;
		if (index == 2 && request->configuration.execution.fixtureKind == CaseFixtureKind_RagdollStairTumble)
			continue;
		if (AppendQuotedArgument(&spec->commandLine, &commandSize, arguments[index].data()) != ArenaStatus_Ok)
			return RunError(error, ArenaStatus_InvalidResult, "run_argument_capacity");
	}
	if (request->configuration.execution.fixtureKind == CaseFixtureKind_RayTracing)
	{
		const std::wstring corpusArgument = L"--ray-corpus=" + (std::filesystem::path(resultDirectory) / "ray-corpus").wstring();
		if (AppendQuotedArgument(&spec->commandLine, &commandSize, corpusArgument.c_str()) != ArenaStatus_Ok)
			return RunError(error, ArenaStatus_InvalidResult, "ray_corpus_argument_capacity");
		if (rayStage == RayRunStage_Preflight &&
		    AppendQuotedArgument(&spec->commandLine, &commandSize, L"--ray-stage=preflight") != ArenaStatus_Ok)
			return RunError(error, ArenaStatus_InvalidResult, "ray_stage_argument_capacity");
	}
	if (BuildEnvironment(releaseCatalog, artifact, spec) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_InvalidResult, "process_environment");
	spec->timeoutMilliseconds = kRunProcessTimeoutMilliseconds;
	if (rayStage == RayRunStage_Preflight)
		spec->timeoutMilliseconds = 120000;
	spec->cancellationGraceMilliseconds = kRunCancellationGraceMilliseconds;
	return ArenaStatus_Ok;
}

const wchar_t* ResultStorageRoot(ResultStorage storage)
{
	return storage == ResultStorage_Local ? L"results\\local" : L"results";
}

ResultStorage ClassifyResultStorage(const wchar_t* repositoryRoot, const wchar_t* resultDirectory)
{
	const std::filesystem::path root = std::filesystem::path(repositoryRoot).lexically_normal();
	const std::filesystem::path directory = std::filesystem::path(resultDirectory).lexically_normal();
	for (ResultStorage storage : {ResultStorage_Local, ResultStorage_Repository})
	{
		const std::filesystem::path base = root / ResultStorageRoot(storage);
		const std::filesystem::path relative = directory.lexically_relative(base);
		std::uint32_t count = 0;
		int admitted = 1;
		for (const std::filesystem::path& part : relative)
		{
			if (part.empty() || part == L"." || part == L"..") admitted = 0;
			if (storage == ResultStorage_Repository && count == 0 && _wcsicmp(part.c_str(), L"local") == 0) admitted = 0;
			++count;
		}
		if (admitted != 0 && count == 3) return storage;
	}
	return ResultStorage_External;
}

ArenaStatus CreateRunPaths(const wchar_t* repositoryRoot, const Catalog* catalog, const HostRecord* host,
                           const PreparedRunRequest* request, RunPathRecord* paths, StatusRecord* error)
{
	if (paths != nullptr)
		*paths = {};
	if (error != nullptr)
		*error = {};
	if (repositoryRoot == nullptr || catalog == nullptr || host == nullptr || request == nullptr || paths == nullptr ||
	    error == nullptr || request->caseIndex >= catalog->caseCount || request->threadCount == 0)
		return error != nullptr ? RunError(error, ArenaStatus_InvalidArgument, "invalid_run_path_arguments")
		                        : ArenaStatus_InvalidArgument;
	std::array<char, 256> cpuSlug = {};
	std::uint32_t cpuSlugSize = 0;
	if (AppendSlug(&cpuSlug, &cpuSlugSize, HostTextView(host, host->cpuModel)) != ArenaStatus_Ok || cpuSlugSize == 0)
		return RunError(error, ArenaStatus_InvalidResult, "cpu_slug");
	std::array<char, 128> memoryToken = {};
	std::uint32_t memoryTokenSize = 0;
	if (!HostTextView(host, host->memoryType).empty() &&
	    AppendSlug(&memoryToken, &memoryTokenSize, HostTextView(host, host->memoryType)) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_InvalidResult, "memory_slug");
	if (host->configuredMemoryClockMhz != 0)
	{
		const int written = std::snprintf(memoryToken.data() + memoryTokenSize, memoryToken.size() - memoryTokenSize,
		                                  "%s%u", memoryTokenSize == 0 ? "" : "-", host->configuredMemoryClockMhz);
		if (written <= 0 || written >= static_cast<int>(memoryToken.size() - memoryTokenSize))
			return RunError(error, ArenaStatus_InvalidResult, "memory_slug_capacity");
		memoryTokenSize += static_cast<std::uint32_t>(written);
	}
	std::uint32_t threadTokenSize = 0;
	if (CopyText(&paths->threadSelectionToken, &threadTokenSize, "threads") != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_InvalidResult, "thread_slug_capacity");
	int contiguous = 1;
	for (std::uint32_t index = 1; index < request->threadCount; ++index)
		if (request->threadCounts[index] != request->threadCounts[0] + index)
			contiguous = 0;
	for (std::uint32_t index = 0; index < request->threadCount; ++index)
	{
		if (contiguous != 0 && request->threadCount > 2 && index != 0 && index + 1 != request->threadCount)
			continue;
		const int written =
		    std::snprintf(paths->threadSelectionToken.data() + threadTokenSize,
			              paths->threadSelectionToken.size() - threadTokenSize, "-%u", request->threadCounts[index]);
		if (written <= 0 || written >= static_cast<int>(paths->threadSelectionToken.size() - threadTokenSize))
			return RunError(error, ArenaStatus_InvalidResult, "thread_slug_capacity");
		threadTokenSize += static_cast<std::uint32_t>(written);
	}
	paths->threadSelectionTokenSize = threadTokenSize;
	SYSTEMTIME time = {};
	GetLocalTime(&time);
	const int timestampWritten =
	    std::snprintf(paths->timestamp.data(), paths->timestamp.size(), "%04u-%02u-%02u_%02u%02u", time.wYear,
		              time.wMonth, time.wDay, time.wHour, time.wMinute);
	if (timestampWritten <= 0 || timestampWritten >= static_cast<int>(paths->timestamp.size()))
		return RunError(error, ArenaStatus_InvalidResult, "timestamp_capacity");
	paths->timestampSize = static_cast<std::uint32_t>(timestampWritten);
	const int baseWritten = std::snprintf(paths->baseRunId.data(), paths->baseRunId.size(), "%s%s%s_%s_r%u",
	                                      paths->timestamp.data(), memoryTokenSize == 0 ? "" : "_", memoryToken.data(),
	                                      paths->threadSelectionToken.data(), request->repeatCount);
	if (baseWritten <= 0 || baseWritten >= static_cast<int>(paths->baseRunId.size()))
		return RunError(error, ArenaStatus_InvalidResult, "run_id_capacity");
	paths->baseRunIdSize = static_cast<std::uint32_t>(baseWritten);
	std::array<wchar_t, kRunPathCapacity> results = {};
	std::uint32_t resultsSize = 0;
	const std::string_view caseSlug = CatalogTextView(catalog, catalog->cases[request->caseIndex].slug);
	if (AppendWide(&results, &resultsSize, repositoryRoot) != ArenaStatus_Ok ||
	    AppendPath(&results, &resultsSize, L"results") != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_InvalidResult, "results_path_capacity");
	if (EnsureDirectory(results.data()) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_RunFailed, "results_directory");
	if (request->storage == ResultStorage_Local &&
	    (AppendPath(&results, &resultsSize, L"local") != ArenaStatus_Ok ||
	     EnsureDirectory(results.data()) != ArenaStatus_Ok))
		return RunError(error, ArenaStatus_RunFailed, "local_results_directory");
	if (request->recordingMode == RecordingMode_On)
	{
		ULARGE_INTEGER available = {};
		if (GetDiskFreeSpaceExW(results.data(), &available, nullptr, nullptr) == 0)
			return RunError(error, ArenaStatus_RunFailed, "recording_volume_space_unavailable");
		const std::uint64_t required = ProjectReplaySpace(*request).requiredBytes;
		paths->requiredCaptureBytes = required;
		if (available.QuadPart < required)
			return RunError(error, ArenaStatus_RunFailed, "insufficient_capture_volume_space required_bytes=" + std::to_string(required));
	}
	if (AppendPathUtf8(&results, &resultsSize, caseSlug) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_InvalidResult, "case_result_path_capacity");
	if (EnsureDirectory(results.data()) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_RunFailed, "case_result_directory");
	if (AppendPathUtf8(&results, &resultsSize, std::string_view(cpuSlug.data(), cpuSlugSize)) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_InvalidResult, "cpu_result_path_capacity");
	if (EnsureDirectory(results.data()) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_RunFailed, "cpu_result_directory");
	for (std::uint32_t suffixIndex = 0; suffixIndex < 1000; ++suffixIndex)
	{
		int runWritten = 0;
		if (suffixIndex == 0)
		{
			runWritten = std::snprintf(paths->runId.data(), paths->runId.size(), "%s", paths->baseRunId.data());
			paths->collisionSuffix[0] = '\0';
			paths->collisionSuffixSize = 0;
		}
		else
		{
			const int suffixWritten =
			    std::snprintf(paths->collisionSuffix.data(), paths->collisionSuffix.size(), "%u", suffixIndex + 1);
			if (suffixWritten <= 0 || suffixWritten >= static_cast<int>(paths->collisionSuffix.size()))
				return RunError(error, ArenaStatus_InvalidResult, "collision_suffix_capacity");
			paths->collisionSuffixSize = static_cast<std::uint32_t>(suffixWritten);
			runWritten = std::snprintf(paths->runId.data(), paths->runId.size(), "%s-%s", paths->baseRunId.data(),
			                           paths->collisionSuffix.data());
		}
		if (runWritten <= 0 || runWritten >= static_cast<int>(paths->runId.size()))
			return RunError(error, ArenaStatus_InvalidResult, "run_id_capacity");
		paths->runIdSize = static_cast<std::uint32_t>(runWritten);
		if (BuildChildPathUtf8(results.data(), std::string_view(paths->runId.data(), paths->runIdSize),
		                       &paths->resultDirectory) != ArenaStatus_Ok)
			return RunError(error, ArenaStatus_InvalidResult, "result_directory_capacity");
		if (GetFileAttributesW(paths->resultDirectory.data()) == INVALID_FILE_ATTRIBUTES)
			break;
		if (suffixIndex == 999)
			return RunError(error, ArenaStatus_InvalidResult, "run_slug_collision_exhausted");
	}
	if (EnsureDirectory(paths->resultDirectory.data()) != ArenaStatus_Ok ||
	    BuildChildPath(paths->resultDirectory.data(), L"raw", &paths->rawDirectory) != ArenaStatus_Ok ||
	    EnsureDirectory(paths->rawDirectory.data()) != ArenaStatus_Ok ||
	    BuildChildPath(paths->resultDirectory.data(), L"manifest.json", &paths->manifestPath) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_RunFailed, "run_directory_create");
	if (request->recordingMode == RecordingMode_On)
	{
		std::error_code filesystemError;
		std::filesystem::create_directories(
		    std::filesystem::path(paths->resultDirectory.data()) /
		        (request->recordingKind == RecordingKind_NativeRayHits ? "ray-images" : "replays") / ".pending", filesystemError);
		if (filesystemError)
			return RunError(error, ArenaStatus_RunFailed, "recording_directory");
	}
	return ArenaStatus_Ok;
}

} // namespace physics_arena
