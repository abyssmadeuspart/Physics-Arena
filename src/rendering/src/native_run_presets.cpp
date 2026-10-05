#include "benchmark_visual/native_run_presets.h"
#include "native_run_file_io.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>

namespace benchmark_visual
{
namespace
{
using namespace physics_arena;
using OrderedJson = nlohmann::ordered_json;
constexpr std::size_t kPresetCapacity = 64 * 1024;

ArenaStatus PresetError(StatusRecord* error, std::string_view reason)
{
	*error = {};
	constexpr std::string_view component = "run_preset";
	const std::string_view status = ArenaStatusText(ArenaStatus_InvalidResult);
	std::copy(component.begin(), component.end(), error->component.begin());
	error->componentSize = static_cast<std::uint32_t>(component.size());
	std::copy(status.begin(), status.end(), error->status.begin());
	error->statusSize = static_cast<std::uint32_t>(status.size());
	std::copy(reason.begin(), reason.end(), error->detail.begin());
	error->detailSize = static_cast<std::uint32_t>(reason.size());
	error->code = ArenaStatus_InvalidResult;
	return error->code;
}

ArenaStatus AdmitPresetSelection(const NativeArenaModel& model, const NativeRunSelection& selection,
                                StatusRecord* error)
{
	if (selection.caseIndex >= model.catalog.caseCount || selection.repeatCount == 0 || selection.repeatCount > kRunRepeatCapacity ||
	    (selection.recordingMode != RecordingMode_Off && selection.recordingMode != RecordingMode_On) ||
	    (selection.verificationMode != VerificationMode_On && selection.verificationMode != VerificationMode_Off))
		return PresetError(error, "Invalid case, repeat count or recording mode");
	std::uint32_t engineCount = 0;
	for (std::uint32_t index = 0; index < selection.engines.size(); ++index)
	{
		if (selection.engines[index] != PresenceStatus_Present)
			continue;
		if (index >= model.catalog.engineCount || CaseRouteSupported(&model.catalog, model.catalog.engines[index], selection.caseIndex) == 0)
			return PresetError(error, "Engine does not support the selected case");
		++engineCount;
	}
	std::uint32_t captureCount = 0;
	for (std::uint32_t slot = 0; slot < selection.recordingThreads.size(); ++slot)
	{
		if (selection.recordingThreads[slot] != PresenceStatus_Present)
			continue;
		if (selection.threads[slot] != PresenceStatus_Present)
			return PresetError(error, "Replay thread is outside the benchmark selection");
		++captureCount;
	}
	if (selection.recordingMode == RecordingMode_On && captureCount == 0)
		return PresetError(error, "Select at least one replay thread");
	std::uint32_t threadCount = 0;
	for (std::uint32_t slot = 0; slot < selection.threads.size(); ++slot)
	{
		if (selection.threads[slot] != PresenceStatus_Present)
			continue;
		const std::uint32_t count = slot + 1;
		if (count > model.host.logicalThreadCount || CaseSupportsThreadCount(&model.catalog, selection.caseIndex, count) != 1)
			return PresetError(error, "Thread count is not supported by this host or case");
		for (std::uint32_t engine = 0; engine < model.catalog.engineCount; ++engine)
			if (selection.engines[engine] == PresenceStatus_Present && EngineSupportsThreadCount(&model.catalog, model.catalog.engines[engine], count) != 1)
				return PresetError(error, "Thread count is not supported by a selected engine");
		++threadCount;
	}
	return engineCount != 0 && threadCount != 0 ? ArenaStatus_Ok : PresetError(error, "Select at least one engine and thread count");
}

ArenaStatus ParsePreset(const OrderedJson& document, const NativeArenaModel& model, NativeRunSelection* candidate,
                       StatusRecord* error)
{
	if (!document.is_object() || document.size() != (document.contains("recording_thread_counts") ? 7u : 6u) + (document.contains("verification_mode") ? 1u : 0u) || !document.at("case_id").is_string() ||
	    !document.at("engine_ids").is_array() || !document.at("thread_counts").is_array() ||
	    !document.at("repeat_count").is_number_unsigned() || !document.at("record_replay").is_string() ||
	    !document.at("run_configuration").is_object())
		return PresetError(error, "Invalid preset fields");
	const std::string& caseId = document.at("case_id").get_ref<const std::string&>();
	candidate->caseIndex = model.catalog.caseCount;
	for (std::uint32_t index = 0; index < model.catalog.caseCount; ++index)
		if (CatalogTextView(&model.catalog, model.catalog.cases[index].id) == caseId)
			candidate->caseIndex = index;
	const std::uint64_t repeats = document.at("repeat_count").get<std::uint64_t>();
	if (candidate->caseIndex == model.catalog.caseCount || repeats == 0 || repeats > kRunRepeatCapacity)
		return PresetError(error, "Unknown case or repeat count outside 1-64");
	candidate->repeatCount = static_cast<std::uint32_t>(repeats);
	const std::string& recording = document.at("record_replay").get_ref<const std::string&>();
	if (recording != "on" && recording != "off")
		return PresetError(error, "Recording mode must be on or off");
	candidate->recordingMode = recording == "on" ? RecordingMode_On : RecordingMode_Off;
	candidate->verificationMode = VerificationMode_On;
	if (document.contains("verification_mode"))
	{
		const OrderedJson& mode = document.at("verification_mode");
		if (!mode.is_string() || (mode != "on" && mode != "off"))
			return PresetError(error, "Verification mode must be on or off");
		candidate->verificationMode = mode == "on" ? VerificationMode_On : VerificationMode_Off;
	}
	candidate->engines.fill(PresenceStatus_Absent);
	candidate->orderedEngineIndexes.fill(0);
	candidate->orderedEngineCount = 0;
	for (const OrderedJson& value : document.at("engine_ids"))
	{
		const std::string& id = value.get_ref<const std::string&>();
		std::uint32_t engine = model.catalog.engineCount;
		for (std::uint32_t index = 0; index < model.catalog.engineCount; ++index)
			if (CatalogTextView(&model.catalog, model.catalog.engines[index].id) == id)
				engine = index;
		if (engine == model.catalog.engineCount || candidate->engines[engine] == PresenceStatus_Present)
			return PresetError(error, "Unknown or duplicate engine");
		candidate->engines[engine] = PresenceStatus_Present;
		candidate->orderedEngineIndexes[candidate->orderedEngineCount++] = engine;
	}
	candidate->threads.fill(PresenceStatus_Absent);
	for (const OrderedJson& value : document.at("thread_counts"))
	{
		if (!value.is_number_unsigned())
			return PresetError(error, "Thread counts must be positive integers");
		const std::uint64_t count = value.get<std::uint64_t>();
		if (count == 0 || count > candidate->threads.size() || candidate->threads[count - 1] == PresenceStatus_Present)
			return PresetError(error, "Invalid or duplicate thread count");
		candidate->threads[count - 1] = PresenceStatus_Present;
	}
	candidate->recordingThreads.fill(PresenceStatus_Absent);
	if (document.contains("recording_thread_counts"))
	{
		const OrderedJson& capture = document.at("recording_thread_counts");
		if (!capture.is_array() || capture.size() > kThreadCountCapacity)
			return PresetError(error, "Invalid replay thread selection");
		for (const OrderedJson& value : capture)
		{
			if (!value.is_number_unsigned() || value.get<std::uint64_t>() == 0 || value.get<std::uint64_t>() > kThreadCountCapacity)
				return PresetError(error, "Invalid replay thread count");
			const std::uint32_t count = value.get<std::uint32_t>();
			if (candidate->threads[count - 1] != PresenceStatus_Present || candidate->recordingThreads[count - 1] == PresenceStatus_Present)
				return PresetError(error, "Replay threads must be unique selected benchmark threads");
			candidate->recordingThreads[count - 1] = PresenceStatus_Present;
		}
	}
	else if (candidate->recordingMode == RecordingMode_On)
		candidate->recordingThreads = candidate->threads;
	candidate->threadMode = ThreadSelectionMode_Explicit;
	candidate->maximumThreadCount = 0;
	if (AdmitPresetSelection(model, *candidate, error) != ArenaStatus_Ok)
		return error->code;
	EffectiveRunConfiguration configuration = {};
	RunSettings restored = {};
	const std::span<const std::uint32_t> engines(candidate->orderedEngineIndexes.data(), candidate->orderedEngineCount);
	if (ReadRunConfigurationSnapshot(&model.catalog, document.at("run_configuration").dump(), engines,
	    &configuration, error, &restored, RunConfigurationReadPurpose_Editable) != ArenaStatus_Ok)
		return error->code;
	if (RunConfigurationTextView(&configuration, configuration.benchmarkCase.id) != caseId ||
	    configuration.benchmarkCase.shapePreset != model.catalog.cases[candidate->caseIndex].shapePreset)
		return PresetError(error, "Snapshot does not match selected case and shape");
	restored.caseIndex = candidate->caseIndex;
	candidate->settings = restored;
	return ComposeRunSettings(&model.catalog, candidate->caseIndex, &candidate->settings, engines, &configuration, error);
}
ArenaStatus PublishAdmittedNativeRunPreset(const wchar_t* path, const NativeArenaModel* model,
    StatusRecord* error, NativeRunPresetSaveMode mode)
{
	try
	{
		const NativeRunSelection& selected = model->selection;
		OrderedJson document;
		document["case_id"] = CatalogTextView(&model->catalog, model->catalog.cases[selected.caseIndex].id);
		document["engine_ids"] = OrderedJson::array();
		std::array<std::uint32_t, kEngineCapacity> engines = {};
		std::uint32_t count = 0;
		for (std::uint32_t ordinal = 0; ordinal < model->catalog.engineCount; ++ordinal)
		{
			const std::uint32_t engine = selected.orderedEngineCount != 0 && ordinal < selected.orderedEngineCount ?
			    selected.orderedEngineIndexes[ordinal] : model->engineDisplayOrder[ordinal];
			if (selected.orderedEngineCount != 0 && ordinal >= selected.orderedEngineCount)
				break;
			if (selected.engines[engine] != PresenceStatus_Present)
				continue;
			engines[count++] = engine;
			document["engine_ids"].push_back(CatalogTextView(&model->catalog, model->catalog.engines[engine].id));
		}
		document["thread_counts"] = OrderedJson::array();
		for (std::uint32_t slot = 0; slot < selected.threads.size(); ++slot)
			if (selected.threads[slot] == PresenceStatus_Present)
				document["thread_counts"].push_back(slot + 1);
		document["repeat_count"] = selected.repeatCount;
		document["record_replay"] = selected.recordingMode == RecordingMode_On ? "on" : "off";
		document["verification_mode"] = selected.verificationMode == VerificationMode_On ? "on" : "off";
		document["recording_thread_counts"] = OrderedJson::array();
		for (std::uint32_t slot = 0; slot < selected.recordingThreads.size(); ++slot)
			if (selected.recordingThreads[slot] == PresenceStatus_Present)
				document["recording_thread_counts"].push_back(slot + 1);
		EffectiveRunConfiguration configuration = {};
		std::string snapshot;
		if (ComposeRunSettings(&model->catalog, selected.caseIndex, &selected.settings, {engines.data(), count}, &configuration, error) != ArenaStatus_Ok ||
		    WriteRunConfigurationSnapshot(&model->catalog, &configuration, {engines.data(), count}, &snapshot, error, &selected.settings.caseInputs) != ArenaStatus_Ok)
			return error->code;
		document["run_configuration"] = OrderedJson::parse(snapshot);
		const std::string bytes = document.dump(2) + '\n';
		if (bytes.size() > kPresetCapacity)
			return PresetError(error, "Preset exceeds 64 KiB");
		return WriteNativeRunFile(path, bytes, error, mode == NativeRunPresetSaveMode_Create
		    ? NativeRunFilePublication_Create : NativeRunFilePublication_Replace);
	}
	catch (const OrderedJson::exception&)
	{
		return PresetError(error, "Cannot serialize preset");
	}
}

}

physics_arena::ArenaStatus ComposeRecommendedNativeRunSelection(const NativeArenaModel* model,
    std::uint32_t caseIndex, const NativeRunSelection& base, NativeRunSelection* candidate, StatusRecord* error)
{
	*error = {};
	if (caseIndex >= model->catalog.caseCount)
		return PresetError(error, "Unknown recommended case");
	NativeRunSelection selection = base;
	selection.caseIndex = caseIndex;
	selection.repeatCount = model->catalog.cases[caseIndex].fullRepeats;
	selection.storage = ResultStorage_Repository;
	selection.engines.fill(PresenceStatus_Absent);
	selection.threads.fill(PresenceStatus_Absent);
	selection.orderedEngineIndexes.fill(0);
	selection.orderedEngineCount = 0;
	selection.maximumThreadCount = 0;
	if (ResetRunSettings(&model->catalog, caseIndex, &selection.settings, error) != ArenaStatus_Ok)
		return error->code;
	RunRequestInput input = {};
	input.caseIndex = caseIndex;
	input.settings = selection.settings;
	input.repeatCount = selection.repeatCount;
	input.threadSelectionMode = ThreadSelectionMode_Default;
	for (std::uint32_t index = 0; index < model->catalog.engineCount; ++index)
		if (model->releaseCatalog.engineArtifactAvailability[index] == PresenceStatus_Present &&
		    CaseRouteSupported(&model->catalog, model->catalog.engines[index], caseIndex) != 0)
		{
			input.engineIndexes[input.engineCount++] = index;
			selection.engines[index] = PresenceStatus_Present;
		}
	if (input.engineCount == 0)
		return PresetError(error, "Recommended case has no available supported engine");
	PreparedRunRequest prepared = {};
	if (PrepareRunRequest(&model->catalog, &model->releaseCatalog, &model->host, &input, &prepared, error) != ArenaStatus_Ok)
		return error->code;
	for (std::uint32_t thread = 0; thread < prepared.threadCount; ++thread)
		selection.threads[prepared.threadCounts[thread] - 1] = PresenceStatus_Present;
	selection.threadMode = ThreadSelectionMode_Explicit;
	ReconcileNativeRecordingThreads(&selection);
	if (selection.recordingMode == RecordingMode_On)
		EnableNativeRecording(&selection);
	if (PrepareNativeRunRequest(model, selection, &prepared, error) != ArenaStatus_Ok)
		return error->code;
	*candidate = selection;
	return ArenaStatus_Ok;
}

physics_arena::ArenaStatus ResolveNativeRunPresetDirectory(const std::filesystem::path& executable,
    std::filesystem::path* directory, StatusRecord* error)
{
	*error = {};
	if (!executable.is_absolute() || executable.parent_path().empty())
		return PresetError(error, "Cannot resolve executable directory");
	const std::filesystem::path candidate = executable.parent_path() / L"presets";
	if (candidate.native().size() + 1 >= kRunPathCapacity)
		return PresetError(error, "Preset directory exceeds path capacity");
	*directory = candidate;
	return ArenaStatus_Ok;
}

physics_arena::ArenaStatus AdmitNativeRunPresetName(const std::filesystem::path& directory,
    std::wstring_view name, std::filesystem::path* path, StatusRecord* error)
{
	*error = {};
	if (name.empty() || name.size() > 250 || name.back() == L'.' || name.back() == L' ' ||
	    CompareStringOrdinal(name.data(), static_cast<int>(name.size()), L"Recommended", 11, TRUE) == CSTR_EQUAL)
		return PresetError(error, "Enter a custom name without a trailing dot or space");
	for (const wchar_t character : name)
		if (character < 32 || std::wstring_view(L"<>:\"/\\|?*").find(character) != std::wstring_view::npos)
			return PresetError(error, "Preset names cannot contain path separators or reserved filename characters");
	const std::wstring_view stem = name.substr(0, name.find(L'.'));
	for (const std::wstring_view reserved : {L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$"})
		if (CompareStringOrdinal(stem.data(), static_cast<int>(stem.size()), reserved.data(),
		    static_cast<int>(reserved.size()), TRUE) == CSTR_EQUAL)
			return PresetError(error, "This name is reserved by Windows");
	if (stem.size() == 4 && (CompareStringOrdinal(stem.data(), 3, L"COM", 3, TRUE) == CSTR_EQUAL ||
	    CompareStringOrdinal(stem.data(), 3, L"LPT", 3, TRUE) == CSTR_EQUAL) &&
	    ((stem[3] >= L'1' && stem[3] <= L'9') || stem[3] == L'\u00b9' || stem[3] == L'\u00b2' || stem[3] == L'\u00b3'))
		return PresetError(error, "This name is reserved by Windows");
	const std::filesystem::path candidate = directory / (std::wstring(name) + L".json");
	// reserve the existing atomic writer's temporary suffix at name admission
	if (candidate.native().size() + 40 >= kRunPathCapacity)
		return PresetError(error, "Preset path exceeds capacity");
	*path = candidate;
	return ArenaStatus_Ok;
}

physics_arena::ArenaStatus EnumerateNativeRunPresets(const std::filesystem::path& directory,
    std::vector<std::wstring>* names, StatusRecord* error)
{
	*error = {};
	std::error_code code;
	std::filesystem::directory_iterator iterator(directory, code);
	if (code == std::errc::no_such_file_or_directory)
	{
		names->clear();
		return ArenaStatus_Ok;
	}
	if (code)
		return PresetError(error, "Cannot read saved presets directory");
	std::vector<std::wstring> found;
	const std::filesystem::directory_iterator end;
	while (iterator != end)
	{
		const std::filesystem::directory_entry& entry = *iterator;
		const int regular = entry.is_regular_file(code);
		if (code)
			return PresetError(error, "Cannot read saved preset entry");
		const std::wstring extension = entry.path().extension().native();
		if (regular != 0 && CompareStringOrdinal(extension.c_str(), -1, L".json", -1, TRUE) == CSTR_EQUAL)
		{
			const std::wstring name = entry.path().stem().native();
			std::filesystem::path admitted;
			StatusRecord nameError = {};
			if (AdmitNativeRunPresetName(directory, name, &admitted, &nameError) == ArenaStatus_Ok)
				found.push_back(name);
		}
		iterator.increment(code);
		if (code)
			return PresetError(error, "Cannot enumerate saved presets");
	}
	std::sort(found.begin(), found.end(), [](const std::wstring& left, const std::wstring& right)
	{
		return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
	});
	*names = std::move(found);
	return ArenaStatus_Ok;
}

physics_arena::ArenaStatus SaveNamedNativeRunPreset(const std::filesystem::path& directory,
    std::wstring_view name, NativeRunPresetSaveMode mode, const NativeArenaModel* model, StatusRecord* error)
{
	std::filesystem::path path;
	if (AdmitNativeRunPresetName(directory, name, &path, error) != ArenaStatus_Ok ||
	    AdmitPresetSelection(*model, model->selection, error) != ArenaStatus_Ok)
		return error->code;
	std::vector<std::wstring> names;
	if (EnumerateNativeRunPresets(directory, &names, error) != ArenaStatus_Ok)
		return error->code;
	for (const std::wstring& existing : names)
		if (CompareStringOrdinal(name.data(), static_cast<int>(name.size()), existing.c_str(), -1, TRUE) == CSTR_EQUAL)
		{
			if (mode == NativeRunPresetSaveMode_Create)
				return PresetError(error, "A preset with this name exists. Choose Replace preset to replace it");
			path = directory / (existing + L".json");
			break;
		}
	std::error_code code;
	std::filesystem::create_directories(directory, code);
	if (code)
		return PresetError(error, "Cannot create saved presets directory");
	return PublishAdmittedNativeRunPreset(path.c_str(), model, error, mode);
}

physics_arena::ArenaStatus LoadNativeRunPreset(const wchar_t* path, NativeArenaModel* model,
                                              physics_arena::StatusRecord* error)
{
	*error = {};
	try
	{
		std::ifstream input(std::filesystem::path(path), std::ios::binary | std::ios::ate);
		const std::streamoff size = input.tellg();
		if (!input || size <= 0 || size > static_cast<std::streamoff>(kPresetCapacity))
			return PresetError(error, "Cannot read preset or file exceeds 64 KiB");
		std::string bytes(static_cast<std::size_t>(size), '\0');
		input.seekg(0);
		if (!input.read(bytes.data(), size))
			return PresetError(error, "Cannot read complete preset");
		if (physics_arena::AdmitRunConfigurationJsonKeys(bytes, error) != physics_arena::ArenaStatus_Ok)
			return error->code;
		NativeRunSelection candidate = model->selection;
		if (ParsePreset(OrderedJson::parse(bytes), *model, &candidate, error) != physics_arena::ArenaStatus_Ok)
			return error->code;
		model->selection = candidate;
		return physics_arena::ArenaStatus_Ok;
	}
	catch (const OrderedJson::exception&)
	{
		return PresetError(error, "Malformed preset JSON or fields");
	}
}

physics_arena::ArenaStatus SaveNativeRunPreset(const wchar_t* path, const NativeArenaModel* model,
    physics_arena::StatusRecord* error, NativeRunPresetSaveMode mode)
{
	using namespace physics_arena;
	*error = {};
	if (AdmitPresetSelection(*model, model->selection, error) != ArenaStatus_Ok)
		return error->code;
	return PublishAdmittedNativeRunPreset(path, model, error, mode);
}
}
