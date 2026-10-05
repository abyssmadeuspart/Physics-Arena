#include "benchmark_visual/native_app_model.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace benchmark_visual
{
namespace
{
using namespace physics_arena;

ArenaStatus ModelError(StatusRecord* error, ArenaStatus status, std::string_view detail)
{
	*error = {};
	constexpr std::string_view component = "native_app_model";
	const std::string_view statusText = ArenaStatusText(status);
	std::copy(component.begin(), component.end(), error->component.begin());
	error->componentSize = static_cast<std::uint32_t>(component.size());
	std::copy(statusText.begin(), statusText.end(), error->status.begin());
	error->statusSize = static_cast<std::uint32_t>(statusText.size());
	if (detail.size() > error->detail.size())
		detail = "native_model_detail_capacity";
	std::copy(detail.begin(), detail.end(), error->detail.begin());
	error->detailSize = static_cast<std::uint32_t>(detail.size());
	error->code = status;
	return status;
}

std::int32_t FindCase(const Catalog* catalog, std::string_view id)
{
	for (std::uint32_t index = 0; index < catalog->caseCount; ++index)
		if (CatalogTextView(catalog, catalog->cases[index].slug) == id ||
		    CatalogTextView(catalog, catalog->cases[index].id) == id)
			return static_cast<std::int32_t>(index);
	return -1;
}

std::int32_t FindEngine(const Catalog* catalog, std::string_view id)
{
	for (std::uint32_t index = 0; index < catalog->engineCount; ++index)
		if (CatalogTextView(catalog, catalog->engines[index].id) == id)
			return static_cast<std::int32_t>(index);
	return -1;
}

std::int32_t FindEngineSet(const Catalog* catalog, std::string_view id)
{
	for (std::uint32_t index = 0; index < catalog->engineSetCount; ++index)
		if (CatalogTextView(catalog, catalog->engineSets[index].id) == id)
			return static_cast<std::int32_t>(index);
	return -1;
}

ArenaStatus ParseUnsigned(std::string_view text, std::uint32_t* value)
{
	if (text.empty())
		return ArenaStatus_InvalidArgument;
	const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), *value);
	return result.ec == std::errc() && result.ptr == text.data() + text.size() && *value != 0
	           ? ArenaStatus_Ok
			   : ArenaStatus_InvalidArgument;
}

PresenceStatus ParseCanonicalUnsigned(std::string_view text, std::uint32_t* value)
{
	if (text.empty() || (text.size() > 1 && text.front() == '0') || ParseUnsigned(text, value) != ArenaStatus_Ok)
		return PresenceStatus_Absent;
	return PresenceStatus_Present;
}

PresenceStatus IsDigits(std::string_view text)
{
	if (text.empty())
		return PresenceStatus_Absent;
	for (const char value : text)
		if (value < '0' || value > '9')
			return PresenceStatus_Absent;
	return PresenceStatus_Present;
}

PresenceStatus IsCanonicalMemoryToken(std::string_view text)
{
	if (text.empty() || text.front() == '-' || text.back() == '-')
		return PresenceStatus_Absent;
	char previous = '\0';
	for (const char value : text)
	{
		if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '-') ||
		    (value == '-' && previous == '-'))
			return PresenceStatus_Absent;
		previous = value;
	}
	return PresenceStatus_Present;
}

PresenceStatus IsCanonicalThreadToken(std::string_view text)
{
	if (text.empty())
		return PresenceStatus_Absent;
	std::size_t offset = 0;
	while (offset < text.size())
	{
		const std::size_t separator = text.find('-', offset);
		const std::size_t end = separator == std::string_view::npos ? text.size() : separator;
		std::uint32_t value = 0;
		if (ParseCanonicalUnsigned(text.substr(offset, end - offset), &value) != PresenceStatus_Present)
			return PresenceStatus_Absent;
		if (separator == std::string_view::npos)
			return PresenceStatus_Present;
		offset = separator + 1;
	}
	return PresenceStatus_Absent;
}

PresenceStatus ParseCanonicalRunId(std::string_view runId, std::string_view* timestamp, std::string_view* memory,
                                   std::uint32_t* repeats, std::uint32_t* variant)
{
	constexpr std::size_t timestampSize = 15;
	constexpr std::string_view threadMarker = "_threads-";
	if (runId.size() <= timestampSize + threadMarker.size() || runId[4] != '-' || runId[7] != '-' || runId[10] != '_' ||
	    IsDigits(runId.substr(0, 4)) != PresenceStatus_Present ||
	    IsDigits(runId.substr(5, 2)) != PresenceStatus_Present ||
	    IsDigits(runId.substr(8, 2)) != PresenceStatus_Present ||
	    IsDigits(runId.substr(11, 4)) != PresenceStatus_Present)
		return PresenceStatus_Absent;
	std::uint32_t month = 0;
	std::uint32_t day = 0;
	std::uint32_t hour = 0;
	std::uint32_t minute = 0;
	std::from_chars(runId.data() + 5, runId.data() + 7, month);
	std::from_chars(runId.data() + 8, runId.data() + 10, day);
	std::from_chars(runId.data() + 11, runId.data() + 13, hour);
	std::from_chars(runId.data() + 13, runId.data() + 15, minute);
	if (month == 0 || month > 12 || day == 0 || day > 31 || hour > 23 || minute > 59 || runId[timestampSize] != '_')
		return PresenceStatus_Absent;
	const std::size_t threadsOffset = runId.find(threadMarker, timestampSize + 1);
	if (threadsOffset == std::string_view::npos)
		return PresenceStatus_Absent;
	*memory = runId.substr(timestampSize + 1, threadsOffset - timestampSize - 1);
	if (IsCanonicalMemoryToken(*memory) != PresenceStatus_Present)
		return PresenceStatus_Absent;
	const std::size_t repeatOffset = runId.find("_r", threadsOffset + threadMarker.size());
	if (repeatOffset == std::string_view::npos ||
	    IsCanonicalThreadToken(
	        runId.substr(threadsOffset + threadMarker.size(), repeatOffset - threadsOffset - threadMarker.size())) !=
	        PresenceStatus_Present)
		return PresenceStatus_Absent;
	const std::size_t variantOffset = runId.find('-', repeatOffset + 2);
	const std::size_t repeatEnd = variantOffset == std::string_view::npos ? runId.size() : variantOffset;
	if (ParseCanonicalUnsigned(runId.substr(repeatOffset + 2, repeatEnd - repeatOffset - 2), repeats) !=
	    PresenceStatus_Present)
		return PresenceStatus_Absent;
	*variant = 0;
	if (variantOffset != std::string_view::npos)
	{
		if (ParseCanonicalUnsigned(runId.substr(variantOffset + 1), variant) != PresenceStatus_Present || *variant < 2)
			return PresenceStatus_Absent;
	}
	*timestamp = runId.substr(0, timestampSize);
	return PresenceStatus_Present;
}

ArenaStatus StoreWidePath(std::string_view value, std::array<wchar_t, kRunPathCapacity>* output, StatusRecord* error)
{
	if (value.empty() || value.size() > static_cast<std::size_t>(INT_MAX))
		return ModelError(error, ArenaStatus_InvalidArgument, "result_path");
	const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
	                                        output->data(), static_cast<int>(output->size() - 1));
	if (written <= 0)
		return ModelError(error, ArenaStatus_InvalidArgument, "result_path_encoding");
	(*output)[written] = L'\0';
	return ArenaStatus_Ok;
}

void ClearSelection(NativeRunSelection* selection)
{
	selection->engines.fill(PresenceStatus_Absent);
	selection->orderedEngineIndexes.fill(0);
	selection->orderedEngineCount = 0;
	selection->threads.fill(PresenceStatus_Absent);
}

ArenaStatus AppendEngineSelection(const Catalog* catalog, std::string_view id, NativeRunSelection* selection,
                                  StatusRecord* error)
{
	const std::int32_t index = FindEngine(catalog, id);
	if (index < 0 || selection->orderedEngineCount >= selection->orderedEngineIndexes.size() ||
	    selection->engines[static_cast<std::uint32_t>(index)] == PresenceStatus_Present)
		return ModelError(error, ArenaStatus_InvalidArgument, "engine_selection");
	const std::uint32_t engineIndex = static_cast<std::uint32_t>(index);
	selection->engines[engineIndex] = PresenceStatus_Present;
	selection->orderedEngineIndexes[selection->orderedEngineCount++] = engineIndex;
	return ArenaStatus_Ok;
}

ArenaStatus AppendEngineSetSelection(const Catalog* catalog, std::string_view id, NativeRunSelection* selection,
                                     StatusRecord* error)
{
	const std::int32_t setIndex = FindEngineSet(catalog, id);
	if (setIndex < 0)
		return ModelError(error, ArenaStatus_InvalidArgument, "engine_set_selection");
	const EngineSetRecord& set = catalog->engineSets[static_cast<std::uint32_t>(setIndex)];
	if (selection->orderedEngineCount > selection->orderedEngineIndexes.size() ||
	    set.engineCount > selection->orderedEngineIndexes.size() - selection->orderedEngineCount)
		return ModelError(error, ArenaStatus_InvalidArgument, "engine_set_selection_capacity");
	for (std::uint32_t index = 0; index < set.engineCount; ++index)
	{
		const std::uint32_t engineIndex = catalog->values[set.engineIndexesOffset + index];
		if (engineIndex >= catalog->engineCount)
			return ModelError(error, ArenaStatus_InvalidResult, "engine_set_index");
		if (selection->engines[engineIndex] == PresenceStatus_Present)
			return ModelError(error, ArenaStatus_InvalidArgument, "duplicate_engine");
	}
	for (std::uint32_t index = 0; index < set.engineCount; ++index)
	{
		const std::uint32_t engineIndex = catalog->values[set.engineIndexesOffset + index];
		selection->engines[engineIndex] = PresenceStatus_Present;
		selection->orderedEngineIndexes[selection->orderedEngineCount++] = engineIndex;
	}
	return ArenaStatus_Ok;
}

ArenaStatus ParseReplayPreset(int argumentCount, const char* const* arguments, NativeArenaModel* model,
                              StatusRecord* error)
{
	const int rayImage = std::string_view(arguments[0]) == "ray-image";
	std::uint32_t seen = 0;
	for (int index = 1; index < argumentCount; ++index)
	{
		const std::string_view option = arguments[index];
		std::uint32_t bit = option == "--result"         ? 1
		                    : option == "--engine"       ? 2
		                    : option == "--thread-count" ? 4
		                    : option == "--repeat-index" ? 8
		                    : rayImage != 0 && option == "--view" ? 16
		                    : rayImage != 0 && option == "--channel" ? 32
		                    : rayImage != 0 && option == "--phase" ? 64
		                    : rayImage != 0 && option == "--api" ? 128 : 0;
		if (bit == 0 || (seen & bit) != 0 || index + 1 == argumentCount)
			return ModelError(error, ArenaStatus_InvalidArgument, "replay_arguments");
		seen |= bit;
		const std::string_view value = arguments[++index];
		if (bit == 1)
		{
			if (StoreWidePath(value, &model->resultPath, error) != ArenaStatus_Ok)
				return error->code;
		}
		else if (bit == 2)
		{
			if (value.empty() || value.size() >= model->replayEngineId.size() || FindEngine(&model->catalog, value) < 0)
				return ModelError(error, ArenaStatus_InvalidArgument, "replay_engine");
			std::copy(value.begin(), value.end(), model->replayEngineId.begin());
		}
		else if (bit == 32)
		{
			if (value != "shaded" && value != "depth" && value != "normals" && value != "errors") return ModelError(error, ArenaStatus_InvalidArgument, "ray_channel");
			model->rayChannel = value == "shaded" ? 0 : value == "depth" ? 1 : value == "normals" ? 2 : 3;
		}
		else if (bit == 64)
		{
			if (value != "primary-coherent" && value != "updated-primary") return ModelError(error, ArenaStatus_InvalidArgument, "ray_phase");
			model->rayPhase = value == "primary-coherent" ? 0 : 8;
		}
		else if (bit == 128)
		{
			if (value != "ordinary" && value != "native-batch") return ModelError(error, ArenaStatus_InvalidArgument, "ray_api");
			model->rayApi = value == "ordinary" ? 0 : 1;
		}
		else
		{
			std::uint32_t number = 0;
			const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
			if (value.empty() || parsed.ec != std::errc() || parsed.ptr != value.data() + value.size() ||
			    (bit == 4 && number == 0) || (bit == 16 && number >= 6))
				return ModelError(error, ArenaStatus_InvalidArgument, "replay_tuple_number");
			if (bit == 4)
				model->replayThreadCount = number;
			else if (bit == 16) model->rayView = number;
			else
				model->replayRepeatIndex = number;
		}
	}
	if ((seen & 15) != 15 || (rayImage != 0 && (seen & 48) != 48))
		return ModelError(error, ArenaStatus_InvalidArgument, "replay_identity_required");
	model->view = NativeArenaView_Replay;
	model->startupAction = rayImage != 0 ? NativeStartupAction_RayImage : NativeStartupAction_Replay;
	return ArenaStatus_Ok;
}

ArenaStatus ParseRunPreset(const Catalog* catalog, int argumentCount, const char* const* arguments,
                           NativeRunSelection* selection, StatusRecord* error)
{
	if (argumentCount < 2)
		return ModelError(error, ArenaStatus_InvalidArgument, "missing_case");
	const std::int32_t caseIndex = FindCase(catalog, arguments[1]);
	if (caseIndex < 0)
		return ModelError(error, ArenaStatus_InvalidArgument, "case_selection");
	selection->caseIndex = static_cast<std::uint32_t>(caseIndex);
	selection->repeatCount = catalog->cases[selection->caseIndex].fullRepeats;
	ClearSelection(selection);
	if (ResetRunSettings(catalog, selection->caseIndex, &selection->settings, error) != ArenaStatus_Ok)
		return error->code;
	RunSettingsArgumentState settingsSeen = {};
	PresenceStatus explicitEngines = PresenceStatus_Absent;
	PresenceStatus explicitThreads = PresenceStatus_Absent;
	PresenceStatus explicitRecording = PresenceStatus_Absent;
	PresenceStatus explicitVerification = PresenceStatus_Absent;
	selection->verificationMode = VerificationMode_On;
	selection->recordingMode = RecordingMode_Off;
	for (int index = 2; index < argumentCount; ++index)
	{
		const std::string_view option = arguments[index];
		if ((option == "--engine" || option == "--engine-set" || option == "--thread-count" ||
		     option == "--max-thread-count" || option == "--repeats" || option == "--timestep-hz" ||
		     option == "--warmup-steps" || option == "--measured-steps" || (option == "--solver" || option == "--physics") ||
		     option == "--record-replay" || option == "--verify") &&
		    index + 1 >= argumentCount)
			return ModelError(error, ArenaStatus_InvalidArgument, "missing_option_value");
		if (option == "--engine")
		{
			explicitEngines = PresenceStatus_Present;
			if (AppendEngineSelection(catalog, arguments[++index], selection, error) != ArenaStatus_Ok)
				return error->code;
		}
		else if (option == "--engine-set")
		{
			explicitEngines = PresenceStatus_Present;
			if (AppendEngineSetSelection(catalog, arguments[++index], selection, error) != ArenaStatus_Ok)
				return error->code;
		}
		else if (option == "--thread-count")
		{
			std::uint32_t value = 0;
			if (ParseUnsigned(arguments[++index], &value) != ArenaStatus_Ok || value > selection->threads.size())
				return ModelError(error, ArenaStatus_InvalidArgument, "thread_count");
			const std::uint32_t slot = value - 1;
			if (selection->threads[slot] == PresenceStatus_Present)
				return ModelError(error, ArenaStatus_InvalidArgument, "thread_selection");
			selection->threads[slot] = PresenceStatus_Present;
			explicitThreads = PresenceStatus_Present;
		}
		else if (option == "--max-thread-count")
		{
			if (explicitThreads == PresenceStatus_Present ||
			    ParseUnsigned(arguments[++index], &selection->maximumThreadCount) != ArenaStatus_Ok)
				return ModelError(error, ArenaStatus_InvalidArgument, "maximum_thread_count");
			selection->threadMode = ThreadSelectionMode_Maximum;
		}
		else if (option == "--repeats")
		{
			if (ParseUnsigned(arguments[++index], &selection->repeatCount) != ArenaStatus_Ok)
				return ModelError(error, ArenaStatus_InvalidArgument, "repeat_count");
		}
		else if (option == "--timestep-hz" || option == "--warmup-steps" || option == "--measured-steps" ||
		         (option == "--solver" || option == "--physics"))
		{
			if (ApplyRunSettingsArgument(catalog, option, arguments[++index], &selection->settings, &settingsSeen,
			                             error) != ArenaStatus_Ok)
				return error->code;
		}
		else if (option == "--verify")
		{
			const std::string_view value = arguments[++index];
			if (explicitVerification == PresenceStatus_Present || (value != "on" && value != "off"))
				return ModelError(error, ArenaStatus_InvalidArgument, "invalid_or_duplicate_verify");
			explicitVerification = PresenceStatus_Present;
			selection->verificationMode = value == "on" ? VerificationMode_On : VerificationMode_Off;
		}
		else if (option == "--record-replay")
		{
			const std::string_view value = arguments[++index];
			if (explicitRecording == PresenceStatus_Present || (value != "on" && value != "off"))
				return ModelError(error, ArenaStatus_InvalidArgument, "invalid_or_duplicate_record_replay");
			explicitRecording = PresenceStatus_Present;
			selection->recordingMode = value == "on" ? RecordingMode_On : RecordingMode_Off;
		}
		else
			return ModelError(error, ArenaStatus_InvalidArgument, "unknown_run_option");
	}
	if (explicitThreads == PresenceStatus_Present)
	{
		if (selection->threadMode == ThreadSelectionMode_Maximum)
			return ModelError(error, ArenaStatus_InvalidArgument, "thread_mode_conflict");
		selection->threadMode = ThreadSelectionMode_Explicit;
	}
	if (explicitEngines != PresenceStatus_Present)
		selection->engines.fill(PresenceStatus_Absent);
	for (std::uint32_t index = 0; index < catalog->engineCount; ++index)
		if ((settingsSeen.solverFields[index] | settingsSeen.physicsFields[index]) != 0 && explicitEngines == PresenceStatus_Present &&
		    selection->engines[index] != PresenceStatus_Present)
			return ModelError(error, ArenaStatus_InvalidArgument, "settings_engine_not_selected");
	return ArenaStatus_Ok;
}

ArenaStatus BuildRequestInput(const NativeArenaModel* model, const NativeRunSelection& selection, RunRequestInput* input, StatusRecord* error)
{
	*input = {};
	input->storage = selection.storage;
	input->caseIndex = selection.caseIndex;
	input->settings = selection.settings;
	input->recordingMode = selection.recordingMode;
	input->verificationMode = selection.verificationMode;
	input->recordingThreadSelection = RecordingThreadSelection_Explicit;
	for (std::uint32_t slot = 0; slot < selection.recordingThreads.size(); ++slot)
		if (selection.recordingThreads[slot] == PresenceStatus_Present)
			input->recordingThreads.counts[input->recordingThreads.count++] = slot + 1;
	input->repeatCount = selection.repeatCount;
	input->threadSelectionMode = selection.threadMode;
	if (input->threadSelectionMode == ThreadSelectionMode_Maximum)
		input->requestedMaximumThreadCount = selection.maximumThreadCount;
	if (selection.orderedEngineCount != 0)
	{
		if (selection.orderedEngineCount > selection.orderedEngineIndexes.size())
			return ModelError(error, ArenaStatus_InvalidResult, "ordered_engine_capacity");
		std::array<PresenceStatus, kEngineCapacity> ordered = {};
		for (std::uint32_t index = 0; index < selection.orderedEngineCount; ++index)
		{
			const std::uint32_t engineIndex = selection.orderedEngineIndexes[index];
			if (engineIndex >= model->catalog.engineCount || ordered[engineIndex] == PresenceStatus_Present ||
			    selection.engines[engineIndex] != PresenceStatus_Present)
				return ModelError(error, ArenaStatus_InvalidResult, "ordered_engine_selection");
			ordered[engineIndex] = PresenceStatus_Present;
			input->engineIndexes[input->engineCount++] = engineIndex;
		}
		for (std::uint32_t index = 0; index < model->catalog.engineCount; ++index)
			if (selection.engines[index] != ordered[index])
				return ModelError(error, ArenaStatus_InvalidResult, "ordered_engine_presence");
	}
	else
	{
		for (std::uint32_t index = 0; index < model->catalog.engineCount; ++index)
			if (selection.engines[index] == PresenceStatus_Present)
				input->engineIndexes[input->engineCount++] = index;
	}
	if (input->threadSelectionMode == ThreadSelectionMode_Explicit)
	{
		for (std::uint32_t slot = 0; slot < selection.threads.size(); ++slot)
			if (selection.threads[slot] == PresenceStatus_Present)
				input->threadCounts[input->threadCount++] = slot + 1;
	}
	if (input->engineCount > input->engineIndexes.size() || input->threadCount > input->threadCounts.size())
		return ModelError(error, ArenaStatus_InvalidResult, "selection_capacity");
	return ArenaStatus_Ok;
}

ArenaStatus ApplyPreparedSelection(const PreparedRunRequest& request, NativeArenaModel* model, StatusRecord* error)
{
	if (request.threadCount > model->selection.threads.size())
		return ModelError(error, ArenaStatus_InvalidResult, "prepared_thread_capacity");
	for (std::uint32_t thread = 0; thread < request.threadCount; ++thread)
		if (request.threadCounts[thread] == 0 || request.threadCounts[thread] > model->selection.threads.size())
			return ModelError(error, ArenaStatus_InvalidResult, "prepared_thread_value");

	model->selection.engines.fill(PresenceStatus_Absent);
	model->selection.threads.fill(PresenceStatus_Absent);
	for (std::uint32_t index = 0; index < request.engineCount; ++index)
		model->selection.engines[request.engineIndexes[index]] = PresenceStatus_Present;
	for (std::uint32_t thread = 0; thread < request.threadCount; ++thread)
		model->selection.threads[request.threadCounts[thread] - 1] = PresenceStatus_Present;
	model->selection.maximumThreadCount = request.requestedMaximumThreadCount;
	model->selection.recordingThreads.fill(PresenceStatus_Absent);
	for (std::uint32_t index = 0; index < request.recordingThreads.count; ++index)
		model->selection.recordingThreads[request.recordingThreads.counts[index] - 1] = PresenceStatus_Present;
	model->selection.threadMode = request.threadSelectionMode;
	return ArenaStatus_Ok;
}
} // namespace

ArenaStatus ProjectNativeRunLabel(std::string_view runId, std::uint32_t threadCount, std::uint32_t repeatCount,
                                  NativeRunLabelProjection* projection)
{
	if (projection != nullptr)
		*projection = {};
	if (projection == nullptr || runId.empty() || runId.size() >= projection->text.size() || threadCount == 0 ||
	    repeatCount == 0)
		return ArenaStatus_InvalidArgument;
	std::string_view timestamp = {};
	std::string_view memory = {};
	std::uint32_t parsedRepeats = 0;
	std::uint32_t variant = 0;
	if (ParseCanonicalRunId(runId, &timestamp, &memory, &parsedRepeats, &variant) != PresenceStatus_Present ||
	    parsedRepeats != repeatCount)
	{
		std::copy(runId.begin(), runId.end(), projection->text.begin());
		projection->size = static_cast<std::uint32_t>(runId.size());
		return ArenaStatus_Ok;
	}
	const int written = variant == 0
	    ? std::snprintf(projection->text.data(), projection->text.size(), "%.*s %c%c:%c%c", 10, timestamp.data(),
	                    timestamp[11], timestamp[12], timestamp[13], timestamp[14])
	    : std::snprintf(projection->text.data(), projection->text.size(), "%.*s %c%c:%c%c | variant %u", 10, timestamp.data(),
	                    timestamp[11], timestamp[12], timestamp[13], timestamp[14], variant);
	if (written <= 0 || written >= static_cast<int>(projection->text.size()))
		return ArenaStatus_InvalidResult;
	projection->size = static_cast<std::uint32_t>(written);
	projection->canonical = PresenceStatus_Present;
	return ArenaStatus_Ok;
}

ArenaStatus PrepareNativeRunRequest(const NativeArenaModel* model, const NativeRunSelection& selection, PreparedRunRequest* request, StatusRecord* error)
{
	if (request != nullptr)
		*request = {};
	if (error != nullptr)
		*error = {};
	if (model == nullptr || request == nullptr || error == nullptr || model->startupStatus != ArenaStatus_Ok)
		return error != nullptr ? ModelError(error, ArenaStatus_InvalidArgument, "prepare_native_request")
		                        : ArenaStatus_InvalidArgument;
	RunRequestInput input = {};
	if (BuildRequestInput(model, selection, &input, error) != ArenaStatus_Ok)
		return error->code;
	if (input.engineCount == 0)
		return ModelError(error, ArenaStatus_InvalidArgument, "Select at least one engine");
	return PrepareRunRequest(&model->catalog, &model->releaseCatalog, &model->host, &input, request, error);
}

ArenaStatus PrepareNativeRunRequest(const NativeArenaModel* model, PreparedRunRequest* request, StatusRecord* error)
{
	const NativeRunSelection empty = {};
	return PrepareNativeRunRequest(model, model != nullptr ? model->selection : empty, request, error);
}

ArenaStatus SelectNativeRunCase(NativeArenaModel* model, std::uint32_t caseIndex, StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (model == nullptr || error == nullptr || caseIndex >= model->catalog.caseCount ||
	    model->selection.caseIndex >= model->catalog.caseCount)
		return error != nullptr ? ModelError(error, ArenaStatus_InvalidArgument, "select_native_case")
		                        : ArenaStatus_InvalidArgument;
	if (caseIndex == model->selection.caseIndex)
		return ArenaStatus_Ok;
	const NativeRunSelection previous = model->selection;
	model->selection.caseIndex = caseIndex;
	if (ResetRunSettings(&model->catalog, caseIndex, &model->selection.settings, error) != ArenaStatus_Ok)
	{
		model->selection = previous;
		return error->code;
	}
	for (std::uint32_t index = 0; index < model->catalog.engineCount; ++index)
		if (CaseRouteSupported(&model->catalog, model->catalog.engines[index], caseIndex) == 0)
			model->selection.engines[index] = PresenceStatus_Absent;
	model->selection.orderedEngineCount = 0;
	for (std::uint32_t index = 0; index < previous.orderedEngineCount; ++index)
	{
		const std::uint32_t engineIndex = previous.orderedEngineIndexes[index];
		if (model->selection.engines[engineIndex] == PresenceStatus_Present)
			model->selection.orderedEngineIndexes[model->selection.orderedEngineCount++] = engineIndex;
	}
	if (ReconcileNativeRunThreads(model, error) == ArenaStatus_Ok)
		return ArenaStatus_Ok;
	model->selection = previous;
	return error->code;
}

ArenaStatus SelectNativeRunFamily(NativeArenaModel* model, CaseFixtureKind family, StatusRecord* error)
{
	if (model == nullptr || error == nullptr || model->selection.caseIndex >= model->catalog.caseCount)
		return error != nullptr ? ModelError(error, ArenaStatus_InvalidArgument, "case_family_selection")
		                        : ArenaStatus_InvalidArgument;
	const CaseShapePreset preset = model->catalog.cases[model->selection.caseIndex].shapePreset;
	std::uint32_t authoredIndex = model->catalog.caseCount;
	for (std::uint32_t index = 0; index < model->catalog.caseCount; ++index)
	{
		const CaseRecord& candidate = model->catalog.cases[index];
		if (candidate.fixtureKind == family && candidate.shapePreset == CaseShapePreset_Authored)
			authoredIndex = index;
		if (candidate.fixtureKind == family && candidate.shapePreset == preset)
			return SelectNativeRunCase(model, index, error);
	}
	if (authoredIndex < model->catalog.caseCount)
		return SelectNativeRunCase(model, authoredIndex, error);
	return ModelError(error, ArenaStatus_InvalidArgument, "case_family_selection");
}

ArenaStatus SelectNativeRunShape(NativeArenaModel* model, CaseShapePreset preset, StatusRecord* error)
{
	if (model == nullptr || error == nullptr || model->selection.caseIndex >= model->catalog.caseCount)
		return error != nullptr ? ModelError(error, ArenaStatus_InvalidArgument, "case_shape_selection")
		                        : ArenaStatus_InvalidArgument;
	const CaseFixtureKind family = model->catalog.cases[model->selection.caseIndex].fixtureKind;
	for (std::uint32_t index = 0; index < model->catalog.caseCount; ++index)
	{
		const CaseRecord& candidate = model->catalog.cases[index];
		if (candidate.fixtureKind == family && candidate.shapePreset == preset)
			return SelectNativeRunCase(model, index, error);
	}
	return ModelError(error, ArenaStatus_InvalidArgument, "case_shape_unavailable_for_selected_family");
}

ArenaStatus SelectNativeRunEngines(NativeArenaModel* model, const std::array<PresenceStatus, kEngineCapacity>& engines,
                                   StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (model == nullptr || error == nullptr || model->startupStatus != ArenaStatus_Ok)
		return error != nullptr ? ModelError(error, ArenaStatus_InvalidArgument, "select_native_engines")
		                        : ArenaStatus_InvalidArgument;
	const NativeRunSelection previous = model->selection;
	model->selection.engines = engines;
	for (std::uint32_t index = 0; index < model->catalog.engineCount; ++index)
	{
		if (engines[index] == PresenceStatus_Present &&
		    CaseRouteSupported(&model->catalog, model->catalog.engines[index], model->selection.caseIndex) == 0)
		{
			model->selection = previous;
			return ModelError(error, ArenaStatus_InvalidArgument, "unsupported_case_engine");
		}
	}
	model->selection.orderedEngineIndexes.fill(0);
	model->selection.orderedEngineCount = 0;
	if (ReconcileNativeRunThreads(model, error) == ArenaStatus_Ok)
		return ArenaStatus_Ok;
	model->selection = previous;
	return error->code;
}

ArenaStatus ResolveNativeRunThreadAvailability(const NativeArenaModel* model, std::uint32_t threadCount,
                                               PresenceStatus* availability, StatusRecord* error)
{
	if (availability != nullptr)
		*availability = PresenceStatus_Absent;
	if (error != nullptr)
		*error = {};
	if (model == nullptr || availability == nullptr || error == nullptr || model->startupStatus != ArenaStatus_Ok ||
	    threadCount == 0 || threadCount > model->selection.threads.size())
		return error != nullptr ? ModelError(error, ArenaStatus_InvalidArgument, "resolve_thread_availability")
		                        : ArenaStatus_InvalidArgument;
	const int caseSupport = CaseSupportsThreadCount(&model->catalog, model->selection.caseIndex, threadCount);
	if (caseSupport < 0)
		return ModelError(error, ArenaStatus_InvalidResult, "case_thread_range");
	if (caseSupport == 0)
		return ArenaStatus_Ok;
	if (threadCount > model->host.logicalThreadCount)
		return ArenaStatus_Ok;
	for (std::uint32_t index = 0; index < model->catalog.engineCount; ++index)
	{
		if (model->selection.engines[index] != PresenceStatus_Present)
			continue;
		const EngineRecord& engine = model->catalog.engines[index];
		const int support = EngineSupportsThreadCount(&model->catalog, engine, threadCount);
		if (support < 0)
			return ModelError(error, ArenaStatus_InvalidResult,
			                  engine.threadSupportMode == ThreadSupportMode_Explicit ? "engine_thread_range"
			                                                                         : "unknown_thread_support_mode");
		if (support == 0)
			return ArenaStatus_Ok;
	}
	*availability = PresenceStatus_Present;
	return ArenaStatus_Ok;
}

void ReconcileNativeRecordingThreads(NativeRunSelection* selection)
{
	for (std::uint32_t slot = 0; slot < selection->threads.size(); ++slot)
		if (selection->threads[slot] != PresenceStatus_Present)
			selection->recordingThreads[slot] = PresenceStatus_Absent;
}

void EnableNativeRecording(NativeRunSelection* selection)
{
	selection->recordingMode = RecordingMode_On;
	if (std::find(selection->recordingThreads.begin(), selection->recordingThreads.end(), PresenceStatus_Present) !=
	    selection->recordingThreads.end())
		return;
	for (std::uint32_t slot = 0; slot < selection->threads.size(); ++slot)
		if (selection->threads[slot] == PresenceStatus_Present)
		{
			selection->recordingThreads[slot] = PresenceStatus_Present;
			break;
		}
}

ArenaStatus ReconcileNativeRunThreads(NativeArenaModel* model, StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (model == nullptr || error == nullptr || model->startupStatus != ArenaStatus_Ok)
		return error != nullptr ? ModelError(error, ArenaStatus_InvalidArgument, "reconcile_native_threads")
		                        : ArenaStatus_InvalidArgument;
	const std::array<PresenceStatus, kThreadCountCapacity> previousThreads = model->selection.threads;
	const ThreadSelectionMode previousMode = model->selection.threadMode;
	std::uint32_t retainedCount = 0;
	for (std::uint32_t slot = 0; slot < model->selection.threads.size(); ++slot)
	{
		if (model->selection.threads[slot] != PresenceStatus_Present)
			continue;
		PresenceStatus available = PresenceStatus_Absent;
		if (ResolveNativeRunThreadAvailability(model, slot + 1, &available, error) != ArenaStatus_Ok)
		{
			model->selection.threads = previousThreads;
			model->selection.threadMode = previousMode;
			return error->code;
		}
		if (available != PresenceStatus_Present)
			model->selection.threads[slot] = PresenceStatus_Absent;
		else
			retainedCount += 1;
	}
	if (retainedCount == 0 && ApplyRecommendedNativeRunThreads(model, error) != ArenaStatus_Ok)
	{
		model->selection.threads = previousThreads;
		model->selection.threadMode = previousMode;
		return error->code;
	}
	model->selection.threadMode = ThreadSelectionMode_Explicit;
	ReconcileNativeRecordingThreads(&model->selection);
	return ArenaStatus_Ok;
}

ArenaStatus ApplyRecommendedNativeRunThreads(NativeArenaModel* model, StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (model == nullptr || error == nullptr || model->startupStatus != ArenaStatus_Ok)
		return error != nullptr ? ModelError(error, ArenaStatus_InvalidArgument, "apply_recommended_threads")
		                        : ArenaStatus_InvalidArgument;

	RunRequestInput input = {};
	if (BuildRequestInput(model, model->selection, &input, error) != ArenaStatus_Ok)
		return error->code;
	input.threadCounts.fill(0);
	input.threadCount = 0;
	input.requestedMaximumThreadCount = 0;
	input.threadSelectionMode = ThreadSelectionMode_Default;
	input.recordingMode = RecordingMode_Off;
	input.recordingThreads = {};
	PreparedRunRequest prepared = {};
	if (PrepareRunRequest(&model->catalog, &model->releaseCatalog, &model->host, &input, &prepared, error) !=
	    ArenaStatus_Ok)
		return error->code;
	if (prepared.threadCount > model->selection.threads.size())
		return ModelError(error, ArenaStatus_InvalidResult, "recommended_thread_capacity");
	for (std::uint32_t thread = 0; thread < prepared.threadCount; ++thread)
		if (prepared.threadCounts[thread] == 0 || prepared.threadCounts[thread] > model->selection.threads.size())
			return ModelError(error, ArenaStatus_InvalidResult, "recommended_thread_value");

	model->selection.threads.fill(PresenceStatus_Absent);
	for (std::uint32_t thread = 0; thread < prepared.threadCount; ++thread)
		model->selection.threads[prepared.threadCounts[thread] - 1] = PresenceStatus_Present;
	model->selection.threadMode = ThreadSelectionMode_Explicit;
	ReconcileNativeRecordingThreads(&model->selection);
	return ArenaStatus_Ok;
}

ArenaStatus InitializeNativeArenaModel(const wchar_t* repositoryRoot, int argumentCount, const char* const* arguments,
                                       NativeArenaModel* model, StatusRecord* error)
{
	if (model != nullptr)
		*model = {};
	if (error != nullptr)
		*error = {};
	if (repositoryRoot == nullptr || argumentCount < 0 || (argumentCount != 0 && arguments == nullptr) ||
	    model == nullptr || error == nullptr)
		return error != nullptr ? ModelError(error, ArenaStatus_InvalidArgument, "initialize_native_model")
		                        : ArenaStatus_InvalidArgument;
	const std::size_t rootSize = std::wcslen(repositoryRoot);
	if (rootSize + 1 > model->repositoryRoot.size())
		return ModelError(error, ArenaStatus_InvalidArgument, "repository_root_capacity");
	std::copy(repositoryRoot, repositoryRoot + rootSize + 1, model->repositoryRoot.begin());
	ArenaStatus status = LoadCatalog(repositoryRoot, &model->catalog, error);
	if (status == ArenaStatus_Ok)
	{
		for (std::uint32_t index = 0; index < model->catalog.engineCount; ++index)
			model->engineDisplayOrder[index] = index;
		std::sort(model->engineDisplayOrder.begin(), model->engineDisplayOrder.begin() + model->catalog.engineCount,
		          [model](std::uint32_t left, std::uint32_t right)
		          {
			          const std::string_view leftName =
			              CatalogTextView(&model->catalog, model->catalog.engines[left].displayName);
			          const std::string_view rightName =
			              CatalogTextView(&model->catalog, model->catalog.engines[right].displayName);
			          const int order =
			              _strnicmp(leftName.data(), rightName.data(), std::min(leftName.size(), rightName.size()));
			          return order != 0 ? order < 0 : leftName.size() < rightName.size();
		          });
	}
	const std::string_view command = argumentCount != 0 ? arguments[0] : "";
	const PresenceStatus replayOnly = command == "replay" || command == "ray-image"
	                                      ? PresenceStatus_Present
	                                      : PresenceStatus_Absent;
	if (status == ArenaStatus_Ok && replayOnly != PresenceStatus_Present)
		status = LoadReleaseCatalog(repositoryRoot, &model->catalog, &model->releaseCatalog, error,
		                            argumentCount == 0 || command == "report" || command == "index"
		                                ? ReleaseLoadMode_Available
		                                : ReleaseLoadMode_RequireAll);
	if (status == ArenaStatus_Ok)
		status = CollectWindowsHost(&model->host, error);
	if (status == ArenaStatus_Ok && model->host.logicalThreadCount > kThreadCountCapacity)
		status = ModelError(error, ArenaStatus_InvalidResult, "host_thread_capacity");
	model->selection.caseIndex = static_cast<std::uint32_t>(
	    std::max(0, FindCase(&model->catalog, CatalogTextView(&model->catalog, model->catalog.defaultPublicCase))));
	model->selection.repeatCount =
	    model->catalog.caseCount != 0 ? model->catalog.cases[model->selection.caseIndex].fullRepeats : 1;
	model->selection.threadMode = ThreadSelectionMode_Default;
	model->selection.resolutionPresetIndex = 0;
	model->selection.replayCamera.distanceScale = 1;
	model->view = NativeArenaView_Run;
	if (status == ArenaStatus_Ok && argumentCount != 0)
	{
		if (command == "run")
		{
			status = ParseRunPreset(&model->catalog, argumentCount, arguments, &model->selection, error);
			if (status == ArenaStatus_Ok)
				model->startupAction = NativeStartupAction_Run;
		}
		else if (command == "replay" || command == "ray-image")
			status = ParseReplayPreset(argumentCount, arguments, model, error);
		else if (command == "report" && argumentCount >= 2 && argumentCount <= 4)
		{
			model->view = NativeArenaView_Results;
			status = StoreWidePath(arguments[1], &model->resultPath, error);
			if (status == ArenaStatus_Ok && argumentCount > 2 &&
			    (std::string_view(arguments[2]) != "--report" || argumentCount != 4))
				status = ModelError(error, ArenaStatus_InvalidArgument, "report_arguments");
		}
		else if (command == "index" && argumentCount == 1)
			model->view = NativeArenaView_Results;
		else if (command != "help" || argumentCount != 1)
			status = ModelError(error, ArenaStatus_InvalidArgument, "public_arguments");
	}
	if (status == ArenaStatus_Ok && model->selection.settings.presence != PresenceStatus_Present)
		status = ResetRunSettings(&model->catalog, model->selection.caseIndex, &model->selection.settings, error);
	if (status == ArenaStatus_Ok && model->view == NativeArenaView_Run)
	{
		PreparedRunRequest prepared = {};
		RunRequestInput input = {};
		if (BuildRequestInput(model, model->selection, &input, error) == ArenaStatus_Ok)
		{
			input.recordingThreadSelection = RecordingThreadSelection_All;
			status = PrepareRunRequest(&model->catalog, &model->releaseCatalog, &model->host, &input, &prepared, error);
		}
		else
			status = error->code;
		if (status == ArenaStatus_Ok)
			status = ApplyPreparedSelection(prepared, model, error);
		if (status == ArenaStatus_Ok && argumentCount == 0)
			status = ApplyRecommendedNativeRunThreads(model, error);
	}
	if (argumentCount == 0 && status == ArenaStatus_ToolMissing)
	{
		model->view = NativeArenaView_Results;
		status = ArenaStatus_Ok;
		*error = {};
	}
	if (status != ArenaStatus_Ok)
		model->startupAction = NativeStartupAction_None;
	model->startupStatus = status;
	model->startupError = *error;
	return status;
}
} // namespace benchmark_visual
