#include "benchmark_visual/native_run_preferences.h"
#include "native_run_file_io.h"

#include <nlohmann/json.hpp>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cwchar>
#include <string>
#include <string_view>

namespace benchmark_visual
{
namespace
{
using namespace physics_arena;
using OrderedJson = nlohmann::ordered_json;

constexpr std::size_t kNativeRunPreferencesFileCapacity = 64 * 1024;
constexpr std::wstring_view kNativeRunPreferencesFileName = L"PhysicsArena.user.json";

ArenaStatus PreferencesError(StatusRecord* error, ArenaStatus status, std::string_view detail)
{
	*error = {};
	constexpr std::string_view component = "native_run_preferences";
	const std::string_view statusText = ArenaStatusText(status);
	std::copy(component.begin(), component.end(), error->component.begin());
	error->componentSize = static_cast<std::uint32_t>(component.size());
	std::copy(statusText.begin(), statusText.end(), error->status.begin());
	error->statusSize = static_cast<std::uint32_t>(statusText.size());
	if (detail.size() > error->detail.size())
		detail = "preferences_detail_capacity";
	std::copy(detail.begin(), detail.end(), error->detail.begin());
	error->detailSize = static_cast<std::uint32_t>(detail.size());
	error->code = status;
	return status;
}

std::int32_t FindCaseIndex(const Catalog& catalog, std::string_view id)
{
	for (std::uint32_t index = 0; index < catalog.caseCount; ++index)
		if (CatalogTextView(&catalog, catalog.cases[index].id) == id)
			return static_cast<std::int32_t>(index);
	return -1;
}

std::int32_t FindEngineIndex(const Catalog& catalog, std::string_view id)
{
	for (std::uint32_t index = 0; index < catalog.engineCount; ++index)
		if (CatalogTextView(&catalog, catalog.engines[index].id) == id)
			return static_cast<std::int32_t>(index);
	return -1;
}

ArenaStatus ResolvePreferencesPath(NativeRunPreferencesContext* context, StatusRecord* error)
{
	const DWORD count = GetModuleFileNameW(nullptr, context->path.data(), static_cast<DWORD>(context->path.size()));
	if (count == 0 || static_cast<std::size_t>(count) >= context->path.size())
		return PreferencesError(error, ArenaStatus_RunFailed, "module_path");
	wchar_t* separator = std::wcsrchr(context->path.data(), L'\\');
	if (separator == nullptr)
		return PreferencesError(error, ArenaStatus_RunFailed, "module_directory");
	const std::size_t prefixSize = static_cast<std::size_t>(separator - context->path.data()) + 1;
	if (kNativeRunPreferencesFileName.size() + 1 > context->path.size() - prefixSize)
		return PreferencesError(error, ArenaStatus_InvalidResult, "sidecar_path_capacity");
	std::copy(kNativeRunPreferencesFileName.begin(), kNativeRunPreferencesFileName.end(),
	          context->path.begin() + prefixSize);
	context->path[prefixSize + kNativeRunPreferencesFileName.size()] = L'\0';
	context->activation = PresenceStatus_Present;
	return ArenaStatus_Ok;
}

ArenaStatus ReadPreferences(const NativeRunPreferencesContext& context,
                            std::array<char, kNativeRunPreferencesFileCapacity>* bytes, std::uint32_t* size,
                            PresenceStatus* presence, StatusRecord* error)
{
	*size = 0;
	*presence = PresenceStatus_Absent;
	HANDLE file = CreateFileW(context.path.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
	                          FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		if (GetLastError() == ERROR_FILE_NOT_FOUND)
			return ArenaStatus_Ok;
		return PreferencesError(error, ArenaStatus_RunFailed, "sidecar_open");
	}
	*presence = PresenceStatus_Present;
	LARGE_INTEGER length = {};
	if (GetFileSizeEx(file, &length) == 0 || length.QuadPart <= 0 ||
	    length.QuadPart > static_cast<LONGLONG>(bytes->size()))
	{
		CloseHandle(file);
		return PreferencesError(error, ArenaStatus_InvalidResult, "sidecar_size");
	}
	DWORD read = 0;
	const DWORD expected = static_cast<DWORD>(length.QuadPart);
	const int readStatus = ReadFile(file, bytes->data(), expected, &read, nullptr) != 0 ? 1 : 0;
	CloseHandle(file);
	if (readStatus == 0 || read != expected)
		return PreferencesError(error, ArenaStatus_RunFailed, "sidecar_read");
	*size = read;
	return ArenaStatus_Ok;
}

ArenaStatus ParsePreferences(const OrderedJson& document, NativeArenaModel* model, StatusRecord* error)
{
	if (!document.is_object() || !document.contains("schema_version") ||
	    !document["schema_version"].is_number_unsigned() ||
	    (document["schema_version"].get<std::uint64_t>() < 1 || document["schema_version"].get<std::uint64_t>() > 3) ||
	    !document.contains("case_id") || !document["case_id"].is_string() || !document.contains("engine_ids") ||
	    !document["engine_ids"].is_array() || !document.contains("thread_counts") ||
	    !document["thread_counts"].is_array() || !document.contains("repeat_count") ||
	    !document["repeat_count"].is_number_unsigned() || !document.contains("resolution") ||
	    !document["resolution"].is_object())
		return PreferencesError(error, ArenaStatus_InvalidResult, "sidecar_schema");

	const std::uint32_t schema = document["schema_version"].get<std::uint32_t>();
	const std::size_t storageFieldCount = document.contains("result_storage") ? 1u : 0u;
	const std::size_t verificationFieldCount = document.contains("verification_mode") ? 1u : 0u;
	if ((schema == 1 &&
	     (document.size() != 7 + verificationFieldCount || !document.contains("graphics") || !document["graphics"].is_string())) ||
	    (schema >= 2 && (document.size() != (schema == 2 ? 8u : (document.contains("recording_thread_counts") ? 10u : 9u) + storageFieldCount) + verificationFieldCount || !document.contains("run_configuration") ||
	                     !document["run_configuration"].is_object() || !document.contains("playback_camera") ||
	                     !document["playback_camera"].is_object())))
		return PreferencesError(error, ArenaStatus_InvalidResult, "sidecar_schema");

	const std::string& caseId = document["case_id"].get_ref<const std::string&>();
	const std::int32_t caseIndex = FindCaseIndex(model->catalog, caseId);
	if (caseId.empty() || caseIndex < 0)
		return PreferencesError(error, ArenaStatus_InvalidResult, "case_unavailable");

	NativeRunSelection candidate = model->selection;
	candidate.storage = ResultStorage_Local;
	candidate.verificationMode = VerificationMode_On;
	if (verificationFieldCount != 0)
	{
		const OrderedJson& mode = document["verification_mode"];
		if (!mode.is_string() || (mode != "on" && mode != "off"))
			return PreferencesError(error, ArenaStatus_InvalidResult, "verification_mode");
		candidate.verificationMode = mode == "on" ? VerificationMode_On : VerificationMode_Off;
	}
	if (storageFieldCount != 0)
	{
		const OrderedJson& storage = document["result_storage"];
		if (!storage.is_string() || (storage != "test" && storage != "release"))
			return PreferencesError(error, ArenaStatus_InvalidResult, "result_storage");
		candidate.storage = storage == "release" ? ResultStorage_Repository : ResultStorage_Local;
	}
	candidate.recordingMode = RecordingMode_Off;
	if (schema == 3)
	{
		if (!document.contains("record_replay") || !document["record_replay"].is_string())
			return PreferencesError(error, ArenaStatus_InvalidResult, "record_replay");
		const std::string& mode = document["record_replay"].get_ref<const std::string&>();
		if (mode != "off" && mode != "on")
			return PreferencesError(error, ArenaStatus_InvalidResult, "record_replay");
		candidate.recordingMode = mode == "on" ? RecordingMode_On : RecordingMode_Off;
	}
	candidate.caseIndex = static_cast<std::uint32_t>(caseIndex);
	candidate.engines.fill(PresenceStatus_Absent);
	if (document["engine_ids"].size() > static_cast<std::size_t>(model->catalog.engineCount) ||
	    document["engine_ids"].size() > candidate.engines.size())
		return PreferencesError(error, ArenaStatus_InvalidResult, "engine_ids_capacity");
	for (const OrderedJson& value : document["engine_ids"])
	{
		if (!value.is_string())
			return PreferencesError(error, ArenaStatus_InvalidResult, "engine_id_type");
		const std::string& engineId = value.get_ref<const std::string&>();
		const std::int32_t engineIndex = FindEngineIndex(model->catalog, engineId);
		if (engineId.empty() || engineIndex < 0 ||
		    candidate.engines[static_cast<std::uint32_t>(engineIndex)] == PresenceStatus_Present)
			return PreferencesError(error, ArenaStatus_InvalidResult, "engine_id");
		candidate.engines[static_cast<std::uint32_t>(engineIndex)] = PresenceStatus_Present;
	}

	candidate.threads.fill(PresenceStatus_Absent);
	if (document["thread_counts"].size() > candidate.threads.size())
		return PreferencesError(error, ArenaStatus_InvalidResult, "thread_counts_capacity");
	std::uint32_t previousThreadCount = 0;
	for (const OrderedJson& value : document["thread_counts"])
	{
		if (!value.is_number_unsigned() || value.get<std::uint64_t>() > UINT32_MAX)
			return PreferencesError(error, ArenaStatus_InvalidResult, "thread_count_type");
		const std::uint32_t threadCount = value.get<std::uint32_t>();
		if (threadCount == 0 || threadCount <= previousThreadCount || threadCount > model->host.logicalThreadCount)
			return PreferencesError(error, ArenaStatus_InvalidResult, "thread_count");
		candidate.threads[threadCount - 1] = PresenceStatus_Present;
		previousThreadCount = threadCount;
	}

	candidate.recordingThreads.fill(PresenceStatus_Absent);
	if (schema == 3 && document.contains("recording_thread_counts"))
	{
		const OrderedJson& capture = document["recording_thread_counts"];
		if (!capture.is_array() || capture.size() > kThreadCountCapacity)
			return PreferencesError(error, ArenaStatus_InvalidResult, "recording_thread_counts");
		for (const OrderedJson& value : capture)
		{
			if (!value.is_number_unsigned() || value.get<std::uint64_t>() == 0 || value.get<std::uint64_t>() > kThreadCountCapacity)
				return PreferencesError(error, ArenaStatus_InvalidResult, "recording_thread_count");
			const std::uint32_t count = value.get<std::uint32_t>();
			if (candidate.threads[count - 1] != PresenceStatus_Present || candidate.recordingThreads[count - 1] == PresenceStatus_Present)
				return PreferencesError(error, ArenaStatus_InvalidResult, "recording_thread_subset_or_duplicate");
			candidate.recordingThreads[count - 1] = PresenceStatus_Present;
		}
	}
	else if (candidate.recordingMode == RecordingMode_On)
		candidate.recordingThreads = candidate.threads;

	const std::uint64_t repeatCount = document["repeat_count"].get<std::uint64_t>();
	if (repeatCount == 0 || repeatCount > kRunRepeatCapacity)
		return PreferencesError(error, ArenaStatus_InvalidResult, "repeat_count");
	candidate.repeatCount = static_cast<std::uint32_t>(repeatCount);

	const OrderedJson& resolution = document["resolution"];
	if (resolution.size() != 2 || !resolution.contains("width_pixels") ||
	    !resolution["width_pixels"].is_number_unsigned() ||
	    resolution["width_pixels"].get<std::uint64_t>() > UINT32_MAX || !resolution.contains("height_pixels") ||
	    !resolution["height_pixels"].is_number_unsigned() ||
	    resolution["height_pixels"].get<std::uint64_t>() > UINT32_MAX)
		return PreferencesError(error, ArenaStatus_InvalidResult, "resolution_schema");
	const std::uint32_t width = resolution["width_pixels"].get<std::uint32_t>();
	const std::uint32_t height = resolution["height_pixels"].get<std::uint32_t>();
	std::uint32_t resolutionIndex = static_cast<std::uint32_t>(kNativeRenderResolutionPresets.size());
	for (std::uint32_t index = 0; index < kNativeRenderResolutionPresets.size(); ++index)
		if (kNativeRenderResolutionPresets[index].widthPixels == width &&
		    kNativeRenderResolutionPresets[index].heightPixels == height)
		{
			resolutionIndex = index;
			break;
		}
	if (resolutionIndex >= kNativeRenderResolutionPresets.size())
		return PreferencesError(error, ArenaStatus_InvalidResult, "resolution");
	candidate.resolutionPresetIndex = resolutionIndex;
	if (ResetRunSettings(&model->catalog, candidate.caseIndex, &candidate.settings, error) != ArenaStatus_Ok)
		return error->code;
	candidate.replayCamera = {};
	candidate.replayCamera.distanceScale = 1;
	if (schema >= 2)
	{
		std::array<std::uint32_t, kEngineCapacity> engines = {};
		std::uint32_t count = 0;
		for (std::uint32_t engine = 0; engine < model->catalog.engineCount; ++engine)
			if (candidate.engines[engine] == PresenceStatus_Present)
				engines[count++] = engine;
		EffectiveRunConfiguration configuration = {};
		if (ReadRunConfigurationSnapshot(&model->catalog, document["run_configuration"].dump(), {engines.data(), count},
		                                 &configuration, error, &candidate.settings, RunConfigurationReadPurpose_Editable) != ArenaStatus_Ok ||
		    RunConfigurationTextView(&configuration, configuration.benchmarkCase.id) != caseId ||
		    configuration.benchmarkCase.shapePreset != model->catalog.cases[candidate.caseIndex].shapePreset)
			return PreferencesError(error, ArenaStatus_InvalidResult, "saved_configuration");
		candidate.settings.caseIndex = candidate.caseIndex;
		const OrderedJson& camera = document["playback_camera"];
		if (camera.size() != 4 || !camera.contains("yaw_radians") || !camera["yaw_radians"].is_number() ||
		    !camera.contains("pitch_radians") || !camera["pitch_radians"].is_number() ||
		    !camera.contains("distance_scale") || !camera["distance_scale"].is_number() || !camera.contains("pan") ||
		    !camera["pan"].is_array() || camera["pan"].size() != 3)
			return PreferencesError(error, ArenaStatus_InvalidResult, "playback_camera_schema");
		candidate.replayCamera.yawRadians = camera["yaw_radians"].get<float>();
		candidate.replayCamera.pitchRadians = camera["pitch_radians"].get<float>();
		candidate.replayCamera.distanceScale = camera["distance_scale"].get<float>();
		if (!std::isfinite(candidate.replayCamera.yawRadians) || !std::isfinite(candidate.replayCamera.pitchRadians) ||
		    std::abs(candidate.replayCamera.pitchRadians) > 3.1415927f ||
		    !std::isfinite(candidate.replayCamera.distanceScale) || candidate.replayCamera.distanceScale < 0.01f ||
		    candidate.replayCamera.distanceScale > 100.0f)
			return PreferencesError(error, ArenaStatus_InvalidResult, "playback_camera_range");
		for (std::uint32_t axis = 0; axis < 3; ++axis)
		{
			if (!camera["pan"][axis].is_number())
				return PreferencesError(error, ArenaStatus_InvalidResult, "playback_camera_pan");
			candidate.replayCamera.pan[axis] = camera["pan"][axis].get<float>();
			if (!std::isfinite(candidate.replayCamera.pan[axis]))
				return PreferencesError(error, ArenaStatus_InvalidResult, "playback_camera_pan");
		}
	}

	candidate.orderedEngineIndexes.fill(0);
	candidate.orderedEngineCount = 0;
	candidate.threadMode = ThreadSelectionMode_Explicit;
	candidate.maximumThreadCount = 0;
	const NativeRunSelection previous = model->selection;
	model->selection = candidate;
	StatusRecord reconciliationError = {};
	if (ReconcileNativeRunThreads(model, &reconciliationError) != ArenaStatus_Ok)
	{
		model->selection = previous;
		return PreferencesError(error, ArenaStatus_InvalidResult, "thread_support");
	}
	return ArenaStatus_Ok;
}

}

ArenaStatus LoadNativeRunPreferences(int argumentCount, NativeArenaModel* model, NativeRunPreferencesContext* context,
                                     StatusRecord* error)
{
	if (context != nullptr)
		*context = {};
	if (error != nullptr)
		*error = {};
	if (argumentCount < 0 || model == nullptr || context == nullptr || error == nullptr ||
	    model->startupStatus != ArenaStatus_Ok)
		return error != nullptr ? PreferencesError(error, ArenaStatus_InvalidArgument, "load_preferences_argument")
		                        : ArenaStatus_InvalidArgument;
	if (argumentCount != 0)
		return ArenaStatus_Ok;
	if (ResolvePreferencesPath(context, error) != ArenaStatus_Ok)
		return error->code;
	std::array<char, kNativeRunPreferencesFileCapacity> bytes = {};
	std::uint32_t size = 0;
	PresenceStatus presence = PresenceStatus_Absent;
	if (ReadPreferences(*context, &bytes, &size, &presence, error) != ArenaStatus_Ok)
		return error->code;
	if (presence != PresenceStatus_Present)
		return ArenaStatus_Ok;
	if (AdmitRunConfigurationJsonKeys({bytes.data(), size}, error) != ArenaStatus_Ok)
	{
		if (std::string_view(error->detail.data(), error->detailSize) == "run_configuration_duplicate_key")
			context->activation = PresenceStatus_Absent;
		return error->code;
	}
	try
	{
		const OrderedJson document = OrderedJson::parse(bytes.begin(), bytes.begin() + size);
		const ArenaStatus status = ParsePreferences(document, model, error);
		if (status != ArenaStatus_Ok && (std::string_view(error->detail.data(), error->detailSize) == "verification_mode" ||
		    std::string_view(error->detail.data(), error->detailSize) == "case_unavailable"))
			context->activation = PresenceStatus_Absent;
		return status;
	}
	catch (...)
	{
		return PreferencesError(error, ArenaStatus_InvalidResult, "sidecar_json");
	}
}

ArenaStatus SaveNativeRunPreferences(const NativeRunPreferencesContext* context, const NativeArenaModel* model,
                                     StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (context == nullptr || model == nullptr || error == nullptr)
		return error != nullptr ? PreferencesError(error, ArenaStatus_InvalidArgument, "save_preferences_argument")
		                        : ArenaStatus_InvalidArgument;
	if (context->activation != PresenceStatus_Present)
		return ArenaStatus_Ok;
	if (model->selection.caseIndex >= model->catalog.caseCount ||
	    model->selection.resolutionPresetIndex >= kNativeRenderResolutionPresets.size() ||
	    model->selection.repeatCount == 0 || model->selection.repeatCount > kRunRepeatCapacity)
		return PreferencesError(error, ArenaStatus_InvalidResult, "selection_index");
	for (std::uint32_t count = model->host.logicalThreadCount + 1;
	     count <= static_cast<std::uint32_t>(model->selection.threads.size()); ++count)
		if (model->selection.threads[count - 1] == PresenceStatus_Present)
			return PreferencesError(error, ArenaStatus_InvalidResult, "selection_thread_count");
	try
	{
		OrderedJson document;
		document["schema_version"] = 3;
		document["verification_mode"] = model->selection.verificationMode == VerificationMode_On ? "on" : "off";
		document["result_storage"] = model->selection.storage == ResultStorage_Repository ? "release" : "test";
		document["record_replay"] = model->selection.recordingMode == RecordingMode_On ? "on" : "off";
		document["recording_thread_counts"] = OrderedJson::array();
		for (std::uint32_t slot = 0; slot < model->selection.recordingThreads.size(); ++slot)
			if (model->selection.recordingThreads[slot] == PresenceStatus_Present)
				document["recording_thread_counts"].push_back(slot + 1);
		document["case_id"] = CatalogTextView(&model->catalog, model->catalog.cases[model->selection.caseIndex].id);
		document["engine_ids"] = OrderedJson::array();
		for (std::uint32_t index = 0; index < model->catalog.engineCount; ++index)
			if (model->selection.engines[index] == PresenceStatus_Present)
				document["engine_ids"].push_back(CatalogTextView(&model->catalog, model->catalog.engines[index].id));
		document["thread_counts"] = OrderedJson::array();
		for (std::uint32_t count = 1; count <= model->host.logicalThreadCount; ++count)
			if (model->selection.threads[count - 1] == PresenceStatus_Present)
				document["thread_counts"].push_back(count);
		document["repeat_count"] = model->selection.repeatCount;
		std::array<std::uint32_t, kEngineCapacity> engines = {};
		std::uint32_t engineCount = 0;
		for (std::uint32_t engine = 0; engine < model->catalog.engineCount; ++engine)
			if (model->selection.engines[engine] == PresenceStatus_Present)
				engines[engineCount++] = engine;
		EffectiveRunConfiguration configuration = {};
		std::string snapshot;
		if (ComposeRunSettings(&model->catalog, model->selection.caseIndex, &model->selection.settings,
		                       {engines.data(), engineCount}, &configuration, error) != ArenaStatus_Ok ||
		    WriteRunConfigurationSnapshot(&model->catalog, &configuration, {engines.data(), engineCount}, &snapshot,
		                                  error, &model->selection.settings.caseInputs) != ArenaStatus_Ok)
			return error->code;
		document["run_configuration"] = OrderedJson::parse(snapshot);
		const NativeReplayCameraPreferences camera = {0, 0, 1, {}};
		document["playback_camera"] = {{"yaw_radians", camera.yawRadians},
		                               {"pitch_radians", camera.pitchRadians},
		                               {"distance_scale", camera.distanceScale},
		                               {"pan", camera.pan}};
		const NativeRenderResolution& resolution =
		    kNativeRenderResolutionPresets[model->selection.resolutionPresetIndex];
		document["resolution"] = {
		    {"width_pixels", resolution.widthPixels},
		    {"height_pixels", resolution.heightPixels},
		};
		std::string bytes = document.dump(2);
		bytes.push_back('\n');
		if (bytes.size() > kNativeRunPreferencesFileCapacity)
			return PreferencesError(error, ArenaStatus_InvalidResult, "sidecar_serialize_capacity");
		return WriteNativeRunFile(context->path.data(), bytes, error);
	}
	catch (...)
	{
		return PreferencesError(error, ArenaStatus_InvalidResult, "sidecar_serialize");
	}
}
}
