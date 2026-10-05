#include "physics_arena/release_contracts.h"
#include "physics_arena/stack_stability.h"

#include <nlohmann/json.hpp>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <initializer_list>
#include <string_view>

namespace physics_arena
{
namespace
{
using OrderedJson = nlohmann::ordered_json;
constexpr std::size_t kResultManifestFileCapacity = 262144;
constexpr std::size_t kResultManifestPathCapacity = 4096;

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

ArenaStatus ResultErrorParts(StatusRecord* error, std::initializer_list<std::string_view> parts)
{
	*error = {};
	CopyText(&error->component, &error->componentSize, "result_manifest");
	CopyText(&error->status, &error->statusSize, "invalid_result");
	for (const std::string_view part : parts)
	{
		if (part.size() > error->detail.size() - error->detailSize)
		{
			CopyText(&error->detail, &error->detailSize, "result_detail_exceeded_capacity");
			error->code = ArenaStatus_InvalidResult;
			return ArenaStatus_InvalidResult;
		}
		std::copy(part.begin(), part.end(), error->detail.begin() + error->detailSize);
		error->detailSize += static_cast<std::uint32_t>(part.size());
	}
	error->code = ArenaStatus_InvalidResult;
	return ArenaStatus_InvalidResult;
}

ArenaStatus ResultError(StatusRecord* error, std::string_view detail)
{
	return ResultErrorParts(error, {detail});
}

ArenaStatus LoadDocument(const wchar_t* path, std::string_view location, OrderedJson* document, StatusRecord* error)
{
	HANDLE file =
	    CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return ResultErrorParts(error, {"missing_file=", location});
	LARGE_INTEGER size = {};
	if (GetFileSizeEx(file, &size) == 0 || size.QuadPart <= 0 ||
	    size.QuadPart > static_cast<LONGLONG>(kResultManifestFileCapacity))
	{
		CloseHandle(file);
		return ResultErrorParts(error, {"result_file_capacity=", location});
	}
	std::array<char, kResultManifestFileCapacity> bytes = {};
	DWORD read = 0;
	const DWORD expected = static_cast<DWORD>(size.QuadPart);
	const int readStatus = ReadFile(file, bytes.data(), expected, &read, nullptr) != 0 ? 1 : 0;
	CloseHandle(file);
	if (readStatus == 0 || read != expected)
		return ResultErrorParts(error, {"result_read_failed=", location});
	if (AdmitRunConfigurationJsonKeys({bytes.data(), read}, error) != ArenaStatus_Ok)
		return error->code;
	*document = OrderedJson::parse(bytes.begin(), bytes.begin() + read);
	if (!document->is_object())
		return ResultErrorParts(error, {"manifest_type=", location});
	return ArenaStatus_Ok;
}

ArenaStatus RequiredString(const OrderedJson& object, const char* key, std::string_view location,
                           std::string_view* output, StatusRecord* error)
{
	OrderedJson::const_iterator item = object.find(key);
	if (item == object.end() || !item->is_string() || item->get_ref<const OrderedJson::string_t&>().empty())
		return ResultErrorParts(error, {key, "_type manifest=", location});
	*output = item->get_ref<const OrderedJson::string_t&>();
	return ArenaStatus_Ok;
}

ArenaStatus StoreResultText(ResultManifestRecord* manifest, CatalogText* destination, std::string_view source)
{
	if (source.empty() || source.size() > UINT32_MAX ||
	    manifest->textArenaUsed + source.size() > manifest->textArena.size())
		return ArenaStatus_InvalidResult;
	destination->offset = manifest->textArenaUsed;
	destination->size = static_cast<std::uint32_t>(source.size());
	std::copy(source.begin(), source.end(), manifest->textArena.begin() + manifest->textArenaUsed);
	manifest->textArenaUsed += destination->size;
	return ArenaStatus_Ok;
}

ArenaStatus RelativePath(std::string_view path)
{
	if (path.empty() || path.front() == '/' || path.front() == '\\' ||
	    (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) != 0 && path[1] == ':'))
		return ArenaStatus_InvalidResult;
	std::size_t partStart = 0;
	for (std::size_t index = 0; index <= path.size(); ++index)
	{
		if (index != path.size() && path[index] != '/' && path[index] != '\\')
			continue;
		const std::string_view part = path.substr(partStart, index - partStart);
		if (part.empty() || part == "." || part == "..")
			return ArenaStatus_InvalidResult;
		partStart = index + 1;
	}
	return ArenaStatus_Ok;
}

const CaseRecord* ResultCase(const Catalog& catalog, std::string_view id)
{
	for (std::uint32_t index = 0; index < catalog.caseCount; ++index)
		if (CatalogTextView(&catalog, catalog.cases[index].id) == id)
			return &catalog.cases[index];
	return nullptr;
}

const EngineRecord* ResultEngine(const Catalog& catalog, std::string_view id)
{
	for (std::uint32_t index = 0; index < catalog.engineCount; ++index)
		if (CatalogTextView(&catalog, catalog.engines[index].id) == id)
			return &catalog.engines[index];
	return nullptr;
}

ArenaStatus ResultUnsigned(const OrderedJson& document, const char* key, std::uint32_t minimum, std::uint32_t* output,
                           StatusRecord* error)
{
	OrderedJson::const_iterator value = document.find(key);
	if (value == document.end() || !value->is_number_unsigned() || value->get<std::uint64_t>() < minimum ||
	    value->get<std::uint64_t>() > UINT32_MAX)
		return ResultErrorParts(error, {"manifest_", key, "_value"});
	*output = value->get<std::uint32_t>();
	return ArenaStatus_Ok;
}

ArenaStatus StoreHostText(HostRecord* host, HostText* output, std::string_view value, StatusRecord* error)
{
	if (value.empty() || value.size() > host->textArena.size() - host->textArenaUsed)
		return ResultError(error, "result_host_text_capacity");
	output->offset = host->textArenaUsed;
	output->size = static_cast<std::uint32_t>(value.size());
	std::copy(value.begin(), value.end(), host->textArena.begin() + host->textArenaUsed);
	host->textArenaUsed += output->size;
	return ArenaStatus_Ok;
}

ArenaStatus HostString(const OrderedJson& document, const char* key, std::string_view location, HostRecord* host,
                       HostText* output, StatusRecord* error)
{
	std::string_view value;
	if (RequiredString(document, key, location, &value, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	return StoreHostText(host, output, value, error);
}

ArenaStatus HostSource(const OrderedJson& sources, const char* key, std::string_view expected, StatusRecord* error)
{
	std::string_view value;
	if (RequiredString(sources, key, "result.host.metadata_sources", &value, error) != ArenaStatus_Ok ||
	    value != expected)
		return ResultErrorParts(error, {"invalid_host_source=", key});
	return ArenaStatus_Ok;
}

ArenaStatus ParseHost(const OrderedJson& manifest, HostRecord* output, StatusRecord* error)
{
	HostRecord candidate = {};
	OrderedJson::const_iterator host = manifest.find("host");
	if (host == manifest.end() || !host->is_object())
		return ResultError(error, "host_metadata_not_object");
	OrderedJson::const_iterator cpu = host->find("cpu");
	OrderedJson::const_iterator memory = host->find("memory");
	OrderedJson::const_iterator os = host->find("os");
	OrderedJson::const_iterator motherboard = host->find("motherboard");
	OrderedJson::const_iterator sources = host->find("metadata_sources");
	if (cpu == host->end() || !cpu->is_object() || memory == host->end() || !memory->is_object() || os == host->end() ||
	    !os->is_object() || motherboard == host->end() || !motherboard->is_object() || sources == host->end() ||
	    !sources->is_object() || sources->empty())
		return ResultError(error, "missing_host_field=host_sections");
	if (HostString(*cpu, "model", "result.host.cpu", &candidate, &candidate.cpuModel, error) != ArenaStatus_Ok ||
	    ResultUnsigned(*cpu, "physical_cores", 1, &candidate.physicalCoreCount, error) != ArenaStatus_Ok ||
	    ResultUnsigned(*cpu, "logical_threads", 1, &candidate.logicalThreadCount, error) != ArenaStatus_Ok ||
	    ResultUnsigned(*cpu, "max_clock_mhz", 1, &candidate.maxClockMhz, error) != ArenaStatus_Ok ||
	    ResultUnsigned(*cpu, "current_clock_mhz", 1, &candidate.currentClockMhz, error) != ArenaStatus_Ok ||
	    ResultUnsigned(*memory, "total_gb", 1, &candidate.totalMemoryGb, error) != ArenaStatus_Ok ||
	    ResultUnsigned(*memory, "configured_clock_mhz", 1, &candidate.configuredMemoryClockMhz, error) !=
	        ArenaStatus_Ok ||
	    HostString(*memory, "type", "result.host.memory", &candidate, &candidate.memoryType, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	auto optionalUnsigned = [error](const OrderedJson& document, const char* key, std::uint32_t* value) -> ArenaStatus
	{
		return document.find(key) == document.end() ? ArenaStatus_Ok : ResultUnsigned(document, key, 1, value, error);
	};
	auto optionalText = [&candidate, error](const OrderedJson& document, const char* key, std::string_view location,
	                                        HostText* value) -> ArenaStatus
	{
		return document.find(key) == document.end() ? ArenaStatus_Ok
		                                            : HostString(document, key, location, &candidate, value, error);
	};
	if (optionalUnsigned(*cpu, "p_core_count", &candidate.performanceCoreCount) != ArenaStatus_Ok ||
	    optionalUnsigned(*cpu, "e_core_count", &candidate.efficiencyCoreCount) != ArenaStatus_Ok ||
	    optionalText(*cpu, "topology", "result.host.cpu", &candidate.cpuTopology) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	OrderedJson::const_iterator modules = memory->find("modules");
	if (modules == memory->end() || !modules->is_array() || modules->empty() ||
	    modules->size() > candidate.memoryModules.size())
		return ResultError(error, "missing_host_field=memory.modules");
	for (const OrderedJson& item : *modules)
	{
		if (!item.is_object())
			return ResultError(error, "invalid_host_field=memory.module");
		MemoryModuleRecord& module = candidate.memoryModules[candidate.memoryModuleCount++];
		if (ResultUnsigned(item, "capacity_gb", 1, &module.capacityGb, error) != ArenaStatus_Ok ||
		    ResultUnsigned(item, "configured_clock_mhz", 1, &module.configuredClockMhz, error) != ArenaStatus_Ok ||
		    ResultUnsigned(item, "speed_mhz", 1, &module.speedMhz, error) != ArenaStatus_Ok ||
		    HostString(item, "type", "result.host.memory.module", &candidate, &module.type, error) != ArenaStatus_Ok ||
		    HostString(item, "manufacturer", "result.host.memory.module", &candidate, &module.manufacturer, error) !=
		        ArenaStatus_Ok ||
		    HostString(item, "part_number", "result.host.memory.module", &candidate, &module.partNumber, error) !=
		        ArenaStatus_Ok ||
		    HostString(item, "slot", "result.host.memory.module", &candidate, &module.slot, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
	}
	if (HostString(*os, "name", "result.host.os", &candidate, &candidate.osName, error) != ArenaStatus_Ok ||
	    HostString(*os, "version", "result.host.os", &candidate, &candidate.osVersion, error) != ArenaStatus_Ok ||
	    HostString(*os, "build", "result.host.os", &candidate, &candidate.osBuild, error) != ArenaStatus_Ok ||
	    HostString(*os, "architecture", "result.host.os", &candidate, &candidate.osArchitecture, error) !=
	        ArenaStatus_Ok ||
	    HostString(*motherboard, "model", "result.host.motherboard", &candidate, &candidate.motherboardModel, error) !=
	        ArenaStatus_Ok ||
	    HostString(*motherboard, "bios_version", "result.host.motherboard", &candidate, &candidate.biosVersion,
	               error) != ArenaStatus_Ok ||
	    HostString(*motherboard, "bios_date", "result.host.motherboard", &candidate, &candidate.biosDate, error) !=
	        ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	for (std::pair<const char*, const char*> source :
	     {std::pair<const char*, const char*>{"cpu.model", "windows_cim"},
	      std::pair<const char*, const char*>{"cpu.physical_cores", "windows_processor_topology_api"},
	      std::pair<const char*, const char*>{"cpu.logical_threads", "windows_processor_topology_api"},
	      std::pair<const char*, const char*>{"cpu.max_clock_mhz", "windows_cim"},
	      std::pair<const char*, const char*>{"cpu.current_clock_mhz", "windows_cim"},
	      std::pair<const char*, const char*>{"memory.total_gb", "windows_cim"},
	      std::pair<const char*, const char*>{"memory.type", "windows_cim"},
	      std::pair<const char*, const char*>{"memory.configured_clock_mhz", "windows_cim"},
	      std::pair<const char*, const char*>{"memory.modules", "windows_cim"},
	      std::pair<const char*, const char*>{"os.name", "windows_cim"},
	      std::pair<const char*, const char*>{"os.version", "windows_cim"},
	      std::pair<const char*, const char*>{"os.build", "windows_cim"},
	      std::pair<const char*, const char*>{"os.architecture", "windows_cim"},
	      std::pair<const char*, const char*>{"motherboard.model", "windows_cim"},
	      std::pair<const char*, const char*>{"motherboard.bios_version", "windows_cim"},
	      std::pair<const char*, const char*>{"motherboard.bios_date", "windows_cim"}})
		if (HostSource(*sources, source.first, source.second, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
	if ((candidate.performanceCoreCount != 0 &&
	     HostSource(*sources, "cpu.p_core_count", "windows_processor_topology_api", error) != ArenaStatus_Ok) ||
	    (candidate.efficiencyCoreCount != 0 &&
	     HostSource(*sources, "cpu.e_core_count", "windows_processor_topology_api", error) != ArenaStatus_Ok) ||
	    (candidate.cpuTopology.size != 0 &&
	     HostSource(*sources, "cpu.topology", "windows_processor_topology_api", error) != ArenaStatus_Ok))
		return ArenaStatus_InvalidResult;
	candidate.cpuCountSource = HostMetadataSource_ProcessorTopology;
	candidate.cpuModelSource = HostMetadataSource_WindowsCim;
	candidate.memorySource = HostMetadataSource_WindowsCim;
	candidate.operatingSystemSource = HostMetadataSource_WindowsCim;
	candidate.motherboardSource = HostMetadataSource_WindowsCim;
	if (ValidateHostRecord(&candidate, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	*output = candidate;
	return ArenaStatus_Ok;
}

ArenaStatus ValidateTiming(const OrderedJson& manifest, std::string_view benchmarkMode, ResultManifestRecord* record,
                           StatusRecord* error)
{
	OrderedJson::const_iterator timing = manifest.find("step_timing");
	if (record->measurementMode == ResultMeasurementMode_PhysicalQuality)
		return timing != manifest.end() && timing->is_null() ? ArenaStatus_Ok
		                                                     : ResultError(error, "quality_timing_must_be_absent");
	if (timing == manifest.end() || !timing->is_object() || timing->size() != 6)
		return ResultError(error, "step_timing_required_or_shape");
	std::uint32_t schema = 0;
	std::string_view value;
	if (ResultUnsigned(*timing, "schema_version", 1, &schema, error) != ArenaStatus_Ok || schema != 2 ||
	    RequiredString(*timing, "csv", "step_timing", &value, error) != ArenaStatus_Ok || value != "step-timing.csv" ||
	    RequiredString(*timing, "svg", "step_timing", &value, error) != ArenaStatus_Ok || value != "step-timing.svg" ||
	    RequiredString(*timing, "physics_series", "step_timing", &value, error) != ArenaStatus_Ok ||
	    value != (record->configuration.execution.fixtureKind == CaseFixtureKind_RayTracing
	                  ? "ordinary_coherent_primary_wall" : "per_work_unit_wall") ||
	    RequiredString(*timing, "render_series", "step_timing", &value, error) != ArenaStatus_Ok)
		return ResultError(error, "step_timing_contract");
	record->timingRenderSeries =
	    value == "absent"
	        ? TimingRenderSeries_Absent
	        : (value == "sampled_frame_wall" ? TimingRenderSeries_SampledFrameWall : TimingRenderSeries_Absent);
	if (((benchmarkMode == "headless_api" || benchmarkMode == "recorded_api") && value != "absent") ||
	    (benchmarkMode == "visualized_release" && value != "sampled_frame_wall") ||
	    RequiredString(*timing, "collection", "step_timing", &value, error) != ArenaStatus_Ok ||
	    value != "inline_all_repeats")
		return ResultError(error, "step_timing_mode");
	return ArenaStatus_Ok;
}
} // namespace

ArenaStatus LoadResultManifest(const wchar_t* manifestPath, const Catalog* catalog,
                               ResultManifestRecord* resultManifest, StatusRecord* error)
{
	ResultManifestRecord candidate = {};
	*resultManifest = {};
	*error = {};
	try
	{
		std::array<char, kResultManifestPathCapacity> locationBytes = {};
		const int locationSize =
		    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, manifestPath, -1, locationBytes.data(),
			                    static_cast<int>(locationBytes.size()), nullptr, nullptr);
		const std::string_view location =
		    locationSize > 0 ? std::string_view(locationBytes.data(), static_cast<std::size_t>(locationSize - 1))
			                 : std::string_view("result_manifest");
		OrderedJson manifest;
		if (LoadDocument(manifestPath, location, &manifest, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;

		if (manifest.contains("verification_mode"))
		{
			const OrderedJson& mode = manifest.at("verification_mode");
			if (!mode.is_string() || (mode != "on" && mode != "off"))
				return ResultError(error, "invalid_verification_mode");
			candidate.verificationMode = mode == "on" ? VerificationMode_On : VerificationMode_Off;
		}
		OrderedJson::const_iterator schema = manifest.find("schema_version");
		if (schema == manifest.end() || !schema->is_number_unsigned() ||
		    (schema->get<std::uint64_t>() < 5 || schema->get<std::uint64_t>() > 8))
			return ResultError(error, "result_schema_version");
		candidate.schemaVersion = schema->get<std::uint32_t>();
		std::string_view value;
		if (RequiredString(manifest, "run_id", location, &value, error) != ArenaStatus_Ok ||
		    StoreResultText(&candidate, &candidate.runId, value) != ArenaStatus_Ok ||
		    RequiredString(manifest, "host_route", location, &value, error) != ArenaStatus_Ok ||
		    (value != "linux" && value != "windows") ||
		    StoreResultText(&candidate, &candidate.hostRoute, value) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		if (RequiredString(manifest, "case_id", location, &value, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		const CaseRecord* selectedCase = ResultCase(*catalog, value);
		if (candidate.schemaVersion == 6 && selectedCase == nullptr)
		{
			constexpr std::string_view retired[] = {"box_contact_islands_10k_sphere", "box_contact_islands_10k_capsule", "box_contact_islands_10k_convex_hull"};
			constexpr std::string_view suffix[] = {"Sphere", "Capsule", "Convex Hull"};
			std::uint32_t shape = 0;
			while (shape < std::size(retired) && retired[shape] != value) ++shape;
			if (shape < std::size(retired))
			{
				const CaseRecord* family = ResultCase(*catalog, "box_contact_islands_10k");
				if (family == nullptr) return ResultError(error, "retired_case_family_unavailable");
				EffectiveRunConfiguration& descriptor = candidate.configuration;
				descriptor.benchmarkCase = *family;
				descriptor.textArenaUsed = catalog->textArenaUsed;
				std::copy_n(catalog->textArena.begin(), catalog->textArenaUsed, descriptor.textArena.begin());
				const auto storeDescriptorText = [&descriptor](CatalogText* text, std::string_view input) -> ArenaStatus
				{
					if (input.size() > descriptor.textArena.size() - descriptor.textArenaUsed) return ArenaStatus_InvalidResult;
					*text = {descriptor.textArenaUsed, static_cast<std::uint32_t>(input.size())};
					std::copy(input.begin(), input.end(), descriptor.textArena.begin() + descriptor.textArenaUsed);
					descriptor.textArenaUsed += static_cast<std::uint32_t>(input.size());
					return ArenaStatus_Ok;
				};
				CaseRecord& record = descriptor.benchmarkCase;
				record.shapePreset = static_cast<CaseShapePreset>(shape + 1);
				std::string slug(value);
				std::replace(slug.begin(), slug.end(), '_', '-');
				const std::string name = std::string(CatalogTextView(catalog, family->displayName)) + " (" + std::string(suffix[shape]) + ")";
				if (storeDescriptorText(&record.id, value) != ArenaStatus_Ok || storeDescriptorText(&record.slug, slug) != ArenaStatus_Ok ||
				    storeDescriptorText(&record.displayName, name) != ArenaStatus_Ok ||
				    storeDescriptorText(&record.description, "Historical Contact Islands shape variant. Frozen configuration was not saved") != ArenaStatus_Ok ||
				    storeDescriptorText(&record.fixtureSemantic, case_execution_wire_detail::ExpectedSemantic(CaseFixtureKind_BoxContactIslands, record.shapePreset)) != ArenaStatus_Ok)
					return ResultError(error, "retired_case_descriptor_capacity");
				candidate.descriptiveCasePresence = PresenceStatus_Present;
				selectedCase = &record;
			}
		}
		if ((candidate.schemaVersion < 7 && selectedCase == nullptr) ||
		    StoreResultText(&candidate, &candidate.caseId, value) != ArenaStatus_Ok)
			return ResultErrorParts(error, {"result_case=", value});
		if (candidate.schemaVersion >= 7)
		{
			OrderedJson::const_iterator recordings = manifest.find("recordings");
			if (recordings == manifest.end())
				return ResultError(error, "result_recordings_declaration_required");
			candidate.recordingMode = recordings->is_null() ? RecordingMode_Off : RecordingMode_On;
			if (candidate.schemaVersion == 7 && candidate.recordingMode == RecordingMode_Off)
				return ResultError(error, "schema7_recordings_required");
			if (candidate.recordingMode == RecordingMode_On)
			{
				if (!recordings->is_object()) return ResultError(error, "result_recordings_object");
				const int ray = recordings->contains("kind");
				if (ray != 0 && (candidate.schemaVersion != 8 || recordings->at("kind") != "native_ray_hits"))
					return ResultError(error, "result_recording_kind");
				candidate.recordingKind = ray != 0 ? RecordingKind_NativeRayHits : RecordingKind_Transforms;
				const std::size_t fields = 3u + (candidate.schemaVersion == 8 && recordings->contains("thread_counts") ? 1u : 0u) + static_cast<std::size_t>(ray);
				if (recordings->size() != fields || ResultUnsigned(*recordings, "format_version", 1, &candidate.recordingVersion, error) != ArenaStatus_Ok ||
				    candidate.recordingVersion != (ray != 0 || candidate.schemaVersion == 7 ? 1u : 2u) ||
				    RequiredString(*recordings, "path_pattern", location, &value, error) != ArenaStatus_Ok ||
				    value != (ray != 0 ? "ray-images/{engine_id}_t{thread_count}_r{repeat_index}.rth" : "replays/{engine_id}_t{thread_count}_r{repeat_index}.bpr") ||
				    RequiredString(*recordings, "coverage", location, &value, error) != ArenaStatus_Ok ||
				    value != (ray != 0 ? "first_measured_occurrence_of_each_view" : "initial_and_each_measured_work_unit"))
					return ResultError(error, "result_recordings_required_or_unsupported");
			}
			if (!manifest.contains("run_configuration") || !manifest.at("run_configuration").is_object() ||
			    !manifest.contains("engines") || !manifest.at("engines").is_array() || manifest.at("engines").empty() ||
			    manifest.at("engines").size() > kEngineCapacity)
				return ResultError(error, "result_run_configuration_required");
			std::array<std::uint32_t, kEngineCapacity> engineIndexes = {};
			std::uint32_t engineCount = 0;
			for (const OrderedJson& entry : manifest.at("engines"))
			{
				std::string_view engineId;
				if (RequiredString(entry, "id", location, &engineId, error) != ArenaStatus_Ok)
					return ArenaStatus_InvalidResult;
				const EngineRecord* engine = ResultEngine(*catalog, engineId);
				if (engine == nullptr)
					return ResultError(error, "result_configuration_engine");
				engineIndexes[engineCount++] = static_cast<std::uint32_t>(engine - catalog->engines.data());
			}
			if (ReadRunConfigurationSnapshot(catalog, manifest.at("run_configuration").dump(),
			                                 {engineIndexes.data(), engineCount}, &candidate.configuration,
			                                 error) != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
			selectedCase = &candidate.configuration.benchmarkCase;
			const RecordingKind expectedRecordingKind = selectedCase->fixtureKind == CaseFixtureKind_RayTracing
			                                               ? RecordingKind_NativeRayHits : RecordingKind_Transforms;
			if (candidate.recordingMode == RecordingMode_On && candidate.recordingKind != expectedRecordingKind)
				return ResultError(error, "recording_kind_case_mismatch");
			candidate.recordingKind = expectedRecordingKind;
			if (RunConfigurationTextView(&candidate.configuration, selectedCase->id) !=
			    ResultTextView(&candidate, candidate.caseId))
				return ResultError(error, "result_configuration_case_identity");
		}
		if (RequiredString(manifest, "benchmark_mode", location, &value, error) != ArenaStatus_Ok ||
		    (candidate.schemaVersion >= 7
		         ? value != (candidate.recordingMode == RecordingMode_On ? "recorded_api" : "headless_api")
				 : (value != ResultCaseTextView(catalog, &candidate, selectedCase->benchmarkMode) &&
				    value != "visualized_local" && value != "visualized_release")) ||
		    StoreResultText(&candidate, &candidate.benchmarkMode, value) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		const std::string_view benchmarkMode = ResultTextView(&candidate, candidate.benchmarkMode);
		if (manifest.contains("step_timing") && manifest["step_timing"].is_null())
		{
			if (selectedCase->fixtureKind != CaseFixtureKind_RagdollStairTumble)
				return ResultError(error, "untimed_case_contract");
			candidate.measurementMode = ResultMeasurementMode_PhysicalQuality;
		}
		const PresenceStatus visualMode = benchmarkMode == "visualized_local" || benchmarkMode == "visualized_release"
		                                      ? PresenceStatus_Present
		                                      : PresenceStatus_Absent;
		OrderedJson::const_iterator resolution = manifest.find("render_resolution");
		if (candidate.schemaVersion == 5)
		{
			if (resolution != manifest.end())
				return ResultError(error, "result_schema5_render_resolution");
		}
		else if (visualMode == PresenceStatus_Present)
		{
			if (resolution == manifest.end() || !resolution->is_object() || resolution->size() != 2 ||
			    ResultUnsigned(*resolution, "width_pixels", 1, &candidate.renderWidthPixels, error) != ArenaStatus_Ok ||
			    ResultUnsigned(*resolution, "height_pixels", 1, &candidate.renderHeightPixels, error) !=
			        ArenaStatus_Ok ||
			    candidate.renderWidthPixels > UINT16_MAX || candidate.renderHeightPixels > UINT16_MAX)
				return ResultError(error, "result_render_resolution");
			candidate.renderResolutionPresence = PresenceStatus_Present;
		}
		else if (resolution != manifest.end())
			return ResultError(error, "result_headless_render_resolution");
		OrderedJson::const_iterator threads = manifest.find("thread_counts");
		if (threads == manifest.end() || !threads->is_array() || threads->empty() ||
		    threads->size() > kThreadCountCapacity)
			return ResultError(error, "result_thread_counts_type");
		for (const OrderedJson& thread : *threads)
		{
			if (!thread.is_number_unsigned() || thread.get<std::uint64_t>() == 0 ||
			    thread.get<std::uint64_t>() > UINT32_MAX)
				return ResultError(error, "result_thread_count_value");
			const std::uint32_t count = thread.get<std::uint32_t>();
			if (std::find(candidate.threadCounts.begin(), candidate.threadCounts.begin() + candidate.threadCount,
			              count) != candidate.threadCounts.begin() + candidate.threadCount)
				return ResultError(error, "result_duplicate_thread_count");
			candidate.threadCounts[candidate.threadCount++] = count;
		}
		if (candidate.recordingMode == RecordingMode_On)
		{
			const OrderedJson& recordings = manifest.at("recordings");
			if (!recordings.contains("thread_counts"))
				candidate.recordingThreads = {candidate.threadCounts, candidate.threadCount};
			else
			{
				const OrderedJson& capture = recordings.at("thread_counts");
				if (!capture.is_array() || capture.empty() || capture.size() > kThreadCountCapacity)
					return ResultError(error, "result_recording_threads_required");
				for (const OrderedJson& value : capture)
				{
					if (!value.is_number_unsigned() || value.get<std::uint64_t>() == 0 ||
					    value.get<std::uint64_t>() > kThreadCountCapacity)
						return ResultError(error, "result_recording_thread_value");
					const std::uint32_t count = value.get<std::uint32_t>();
					if (std::find(candidate.threadCounts.begin(), candidate.threadCounts.begin() + candidate.threadCount, count) ==
					        candidate.threadCounts.begin() + candidate.threadCount ||
					    RecordingForThread(candidate.recordingThreads, count) == RecordingMode_On)
						return ResultError(error, "result_recording_thread_subset_or_duplicate");
					candidate.recordingThreads.counts[candidate.recordingThreads.count++] = count;
				}
			}
		}
		if (manifest.contains("step_count") || manifest.contains("warmup_steps"))
			return ResultError(error, "result_legacy_work_unit_fields");
		if (RequiredString(manifest, "work_unit_id", location, &value, error) != ArenaStatus_Ok ||
		    value != ResultCaseTextView(catalog, &candidate, selectedCase->workUnitId) ||
		    StoreResultText(&candidate, &candidate.workUnitId, value) != ArenaStatus_Ok ||
		    ResultUnsigned(manifest, "measured_work_unit_count", 1, &candidate.measuredWorkUnitCount, error) !=
		        ArenaStatus_Ok ||
		    candidate.measuredWorkUnitCount != selectedCase->measuredWorkUnitCount ||
		    ResultUnsigned(manifest, "warmup_work_unit_count", 0, &candidate.warmupWorkUnitCount, error) !=
		        ArenaStatus_Ok ||
		    candidate.warmupWorkUnitCount != selectedCase->warmupWorkUnitCount)
			return ResultError(error, "result_work_unit_contract");
		OrderedJson::const_iterator observations = manifest.find("observations");
		if (observations == manifest.end() || !observations->is_object() || observations->size() != 2 ||
		    ResultUnsigned(*observations, "schema_version", 1, &candidate.observationsSchemaVersion, error) !=
		        ArenaStatus_Ok ||
		    candidate.observationsSchemaVersion != 1 ||
		    RequiredString(*observations, "csv", location, &value, error) != ArenaStatus_Ok ||
		    value != "observations.csv" ||
		    StoreResultText(&candidate, &candidate.observationsCsv, value) != ArenaStatus_Ok)
			return ResultError(error, "result_observations");
		candidate.observationsPresence = PresenceStatus_Present;
		OrderedJson::const_iterator stability = manifest.find("stability");
		if (stability != manifest.end())
		{
			if (candidate.verificationMode == VerificationMode_Off)
				return ResultError(error, "verification_off_stability_contradiction");
			if (!stability->is_object() || stability->size() != 3 ||
			    RequiredString(*stability, "csv", location, &value, error) != ArenaStatus_Ok || value != "stability.csv" ||
			    RequiredString(*stability, "criterion", location, &value, error) != ArenaStatus_Ok ||
			    (value != kStackCriterion && value != kLegacyStackCriterion && value != kContainerCriterion && value != kContactIslandsCriterion && value != kContactIslandsShapeCriterion && value != kPyramidUnforcedShapeCriterion) ||
			    benchmark_stack::TargetFixture(selectedCase->fixtureKind) == 0)
				return ResultError(error, "result_stability_declaration");
			candidate.stabilityCriterion = value == kStackCriterion ? StackCriterion_SupportPlane :
			    value == kContainerCriterion ? StackCriterion_ContainerEscape :
			    value == kPyramidUnforcedShapeCriterion ? StackCriterion_PyramidUnforcedShape :
			    value == kContactIslandsShapeCriterion ? StackCriterion_ContactIslandsShapePreservation :
			    value == kContactIslandsCriterion ? StackCriterion_ContactIslandsStabilization : StackCriterion_LegacyMovement;
			if ((selectedCase->fixtureKind == CaseFixtureKind_OpenContainerFallingPile) !=
			    (candidate.stabilityCriterion == StackCriterion_ContainerEscape) ||
			    ((candidate.stabilityCriterion == StackCriterion_ContactIslandsStabilization || candidate.stabilityCriterion == StackCriterion_ContactIslandsShapePreservation) &&
			     selectedCase->fixtureKind != CaseFixtureKind_BoxContactIslands) ||
			    (candidate.stabilityCriterion == StackCriterion_PyramidUnforcedShape &&
			     selectedCase->fixtureKind != CaseFixtureKind_LargePyramid && selectedCase->fixtureKind != CaseFixtureKind_PyramidWall))
				return ResultError(error, "result_stability_fixture_disagreement");
			if (RequiredString(*stability, "margin_policy", location, &value, error) != ArenaStatus_Ok ||
			    (value != StackMarginPolicyName(candidate.stabilityCriterion) &&
			     !(candidate.stabilityCriterion == StackCriterion_ContactIslandsStabilization && value == kContactIslands10cmMarginPolicy)))
				return ResultError(error, "result_stability_declaration");
			if (candidate.stabilityCriterion == StackCriterion_ContactIslandsStabilization && value == kContactIslands10cmMarginPolicy)
				candidate.stabilityCriterion = StackCriterion_ContactIslandsStabilization10cm;
			candidate.stabilityPresence = PresenceStatus_Present;
		}
		if (ResultUnsigned(manifest, "repeat_count", 1, &candidate.repeatCount, error) != ArenaStatus_Ok ||
		    ParseHost(manifest, &candidate.host, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		OrderedJson::const_iterator engines = manifest.find("engines");
		if (engines == manifest.end() || !engines->is_array() || engines->empty() || engines->size() > kEngineCapacity)
			return ResultError(error, "result_engines_type");
		for (const OrderedJson& entry : *engines)
		{
			std::string_view engineId;
			if (!entry.is_object() || RequiredString(entry, "id", location, &engineId, error) != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
			const EngineRecord* engine = ResultEngine(*catalog, engineId);
			if (engine == nullptr)
				return ResultErrorParts(error, {"result_engine=", engineId});
			const std::uint32_t engineIndex = static_cast<std::uint32_t>(engine - catalog->engines.data());
			for (std::uint32_t prior = 0; prior < candidate.engineCount; ++prior)
				if (candidate.engines[prior].engineIndex == engineIndex)
					return ResultErrorParts(error, {"duplicate_manifest_engine=", engineId});
			ResultEngineSnapshot& snapshot = candidate.engines[candidate.engineCount++];
			snapshot.engineIndex = engineIndex;
			if (RequiredString(entry, "release_artifact_manifest", location, &value, error) != ArenaStatus_Ok ||
			    RelativePath(value) != ArenaStatus_Ok ||
			    StoreResultText(&candidate, &snapshot.artifactManifestPath, value) != ArenaStatus_Ok ||
			    RequiredString(entry, "source_version", location, &value, error) != ArenaStatus_Ok ||
			    StoreResultText(&candidate, &snapshot.sourceVersion, value) != ArenaStatus_Ok ||
			    RequiredString(entry, "toolchain_id", location, &value, error) != ArenaStatus_Ok ||
			    StoreResultText(&candidate, &snapshot.toolchainId, value) != ArenaStatus_Ok ||
			    RequiredString(entry, "timing_scope", location, &value, error) != ArenaStatus_Ok ||
			    value != (candidate.measurementMode == ResultMeasurementMode_PhysicalQuality
			                  ? "absent"
							  : selectedCase->fixtureKind == CaseFixtureKind_RayTracing ? "ordinary_coherent_primary_wall_sum_inline"
				              : (ResultCaseTextView(catalog, &candidate, selectedCase->workUnitId) == "cycle"
			                         ? "timed_phase_wall_sum_inline"
									 : "work_unit_wall_sum_inline")) ||
			    StoreResultText(&candidate, &snapshot.timingScope, value) != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
			OrderedJson::const_iterator reportVersion = entry.find("report_version");
			if (reportVersion != entry.end() &&
			    (RequiredString(entry, "report_version", location, &value, error) != ArenaStatus_Ok ||
			     StoreResultText(&candidate, &snapshot.reportVersion, value) != ArenaStatus_Ok))
				return ArenaStatus_InvalidResult;
		}
		if (benchmarkMode == "visualized_release")
		{
			if (RequiredString(manifest, "visual_shared_renderer_artifact_manifest", location, &value, error) !=
			        ArenaStatus_Ok ||
			    RelativePath(value) != ArenaStatus_Ok ||
			    StoreResultText(&candidate, &candidate.visualRendererManifestPath, value) != ArenaStatus_Ok)
				return ResultError(error, "result_visual_renderer_manifest");
		}
		if (ValidateTiming(manifest, benchmarkMode, &candidate, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		if (manifest.contains("execution_failures"))
		{
			const OrderedJson& failures = manifest.at("execution_failures");
			if (candidate.schemaVersion != 8 || !failures.is_array() ||
			    failures.size() > static_cast<std::size_t>(candidate.engineCount) * candidate.threadCount * candidate.repeatCount)
				return ResultError(error, "execution_failure_cardinality");
			candidate.executionFailures.reserve(failures.size());
			for (const OrderedJson& entry : failures)
			{
				ExecutionFailure failure = {};
				std::string_view engineId, stage, outcome, reason, detail;
				if (!entry.is_object() || entry.size() != 8 ||
				    RequiredString(entry, "engine_id", location, &engineId, error) != ArenaStatus_Ok ||
				    RequiredString(entry, "stage", location, &stage, error) != ArenaStatus_Ok ||
				    RequiredString(entry, "outcome", location, &outcome, error) != ArenaStatus_Ok ||
				    RequiredString(entry, "reason", location, &reason, error) != ArenaStatus_Ok ||
				    RequiredString(entry, "detail", location, &detail, error) != ArenaStatus_Ok ||
				    ResultUnsigned(entry, "thread_count", 1, &failure.threadCount, error) != ArenaStatus_Ok ||
				    ResultUnsigned(entry, "repeat_index", 0, &failure.repeatIndex, error) != ArenaStatus_Ok ||
				    failure.repeatIndex >= candidate.repeatCount || detail.size() >= failure.detail.size() ||
				    (stage != "preflight" && stage != "benchmark") || (outcome != "failed" && outcome != "not_run") ||
				    (outcome == "not_run" && stage == "benchmark" && reason != "previous_repeat_failed"))
					return ResultError(error, "execution_failure_identity");
				const EngineRecord* engine = ResultEngine(*catalog, engineId);
				if (engine == nullptr)
					return ResultError(error, "execution_failure_engine");
				failure.engineIndex = static_cast<std::uint32_t>(engine - catalog->engines.data());
				std::uint32_t selected = 0;
				for (std::uint32_t index = 0; index < candidate.engineCount; ++index)
					selected += candidate.engines[index].engineIndex == failure.engineIndex;
				if (selected != 1 || std::find(candidate.threadCounts.begin(), candidate.threadCounts.begin() + candidate.threadCount,
				                              failure.threadCount) == candidate.threadCounts.begin() + candidate.threadCount ||
				    FindExecutionFailure(candidate.executionFailures, failure.engineIndex, failure.threadCount, failure.repeatIndex) != nullptr)
					return ResultError(error, "execution_failure_duplicate_or_outside_matrix");
				std::uint32_t reasonIndex = 0;
				while (reasonIndex <= ExecutionFailureReason_PreviousRepeatFailed &&
				       reason != ExecutionFailureReasonName(static_cast<ExecutionFailureReason>(reasonIndex)))
					++reasonIndex;
				if (reasonIndex > ExecutionFailureReason_PreviousRepeatFailed || !entry.contains("exit_code") ||
				    (reason == "previous_repeat_failed" && (stage != "benchmark" || outcome != "not_run" || !entry.at("exit_code").is_null())))
					return ResultError(error, "execution_failure_reason");
				failure.reason = static_cast<ExecutionFailureReason>(reasonIndex);
				failure.stage = stage == "preflight" ? ExecutionStage_Preflight : ExecutionStage_Benchmark;
				failure.outcome = outcome == "failed" ? ExecutionOutcome_Failed : ExecutionOutcome_NotRun;
				if (!entry.at("exit_code").is_null())
				{
					const OrderedJson& code = entry.at("exit_code");
					if (!code.is_number_integer() || code.get<std::int64_t>() < INT32_MIN || code.get<std::int64_t>() > INT32_MAX)
						return ResultError(error, "execution_failure_exit_code");
					failure.exitCode = code.get<std::int32_t>();
					failure.exitCodePresence = PresenceStatus_Present;
				}
				std::copy(detail.begin(), detail.end(), failure.detail.begin());
				candidate.executionFailures.push_back(failure);
			}
		}
		*resultManifest = candidate;
		return ArenaStatus_Ok;
	}
	catch (const std::exception& exception)
	{
		*resultManifest = {};
		return ResultErrorParts(error, {"result_json_parse error=", exception.what()});
	}
}
const ExecutionFailure* FindExecutionFailure(std::span<const ExecutionFailure> failures, std::uint32_t engineIndex,
                                              std::uint32_t threadCount, std::uint32_t repeatIndex)
{
	for (const ExecutionFailure& failure : failures)
		if (failure.engineIndex == engineIndex && failure.threadCount == threadCount && failure.repeatIndex == repeatIndex)
			return &failure;
	return nullptr;
}

const char* ExecutionFailureReasonName(ExecutionFailureReason reason)
{
	constexpr std::array<const char*, 5> names = {"process_exit", "process_timeout", "invalid_output", "recording_failed", "previous_repeat_failed"};
	return names[reason];
}

const EffectiveRunConfiguration* ResultConfiguration(const ResultManifestRecord* manifest)
{
	return manifest->schemaVersion >= 7 ? &manifest->configuration : nullptr;
}

const CaseRecord& ResultCaseDefinition(const CaseRecord& authored, const ResultManifestRecord* manifest)
{
	return manifest->schemaVersion >= 7 || manifest->descriptiveCasePresence == PresenceStatus_Present ? manifest->configuration.benchmarkCase : authored;
}

std::string_view ResultCaseTextView(const Catalog* catalog, const ResultManifestRecord* manifest, CatalogText text)
{
	return manifest->schemaVersion >= 7 || manifest->descriptiveCasePresence == PresenceStatus_Present ? RunConfigurationTextView(&manifest->configuration, text)
	                                    : CatalogTextView(catalog, text);
}

ResultCaseMetadata ProjectResultCaseMetadata(const Catalog* catalog, const CaseRecord& record,
                                             ResultMeasurementMode mode, const EffectiveRunConfiguration* configuration)
{
	ResultCaseMetadata result = {record,
	                             CaseConfigurationTextView(catalog, configuration, record.primaryMetricId),
	                             CaseConfigurationTextView(catalog, configuration, record.primaryMetricUnit),
	                             CaseConfigurationTextView(catalog, configuration, record.primaryMetricLabel),
	                             CaseConfigurationTextView(catalog, configuration, record.primaryMetricNote),
	                             record.primaryMetricDirection};
	if (record.fixtureKind == CaseFixtureKind_RagdollStairTumble && mode == ResultMeasurementMode_Timed)
	{
		result.primaryMetricId = "median_ms_per_step";
		result.primaryMetricUnit = "ms";
		result.primaryMetricLabel = "Physics ms/step";
		result.primaryMetricNote = "Historical timing only; physical quality was not recorded";
		result.direction = PrimaryMetricDirection_LowerIsBetter;
		result.record.observationCount = 0;
		result.record.resultGroupCount = 0;
	}
	return result;
}
} // namespace physics_arena
