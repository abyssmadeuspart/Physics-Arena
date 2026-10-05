#include "run_internal.h"
#include "physics_arena/stack_stability.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <fstream>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <string_view>

namespace physics_arena
{
struct JsonWriter
{
	void* handle;
	std::array<char, 4096> bytes;
	std::uint32_t size;
	ArenaStatus status;
};

ArenaStatus FlushJson(JsonWriter* writer)
{
	if (writer->status != ArenaStatus_Ok || writer->size == 0)
		return writer->status;
	DWORD written = 0;
	if (WriteFile(static_cast<HANDLE>(writer->handle), writer->bytes.data(), writer->size, &written, nullptr) == 0 ||
	    written != writer->size)
		writer->status = ArenaStatus_RunFailed;
	writer->size = 0;
	return writer->status;
}

ArenaStatus WriteJsonBytes(JsonWriter* writer, std::string_view value)
{
	for (char character : value)
	{
		if (writer->size == writer->bytes.size() && FlushJson(writer) != ArenaStatus_Ok)
			return writer->status;
		writer->bytes[writer->size++] = character;
	}
	return writer->status;
}

ArenaStatus WriteJsonString(JsonWriter* writer, std::string_view value)
{
	if (WriteJsonBytes(writer, "\"") != ArenaStatus_Ok)
		return writer->status;
	constexpr char hexadecimal[] = "0123456789abcdef";
	for (const unsigned char character : value)
	{
		if (character == '"' || character == '\\')
		{
			const char escaped[2] = {'\\', static_cast<char>(character)};
			if (WriteJsonBytes(writer, std::string_view(escaped, 2)) != ArenaStatus_Ok)
				return writer->status;
		}
		else if (character == '\b' && WriteJsonBytes(writer, "\\b") != ArenaStatus_Ok)
			return writer->status;
		else if (character == '\f' && WriteJsonBytes(writer, "\\f") != ArenaStatus_Ok)
			return writer->status;
		else if (character == '\n' && WriteJsonBytes(writer, "\\n") != ArenaStatus_Ok)
			return writer->status;
		else if (character == '\r' && WriteJsonBytes(writer, "\\r") != ArenaStatus_Ok)
			return writer->status;
		else if (character == '\t' && WriteJsonBytes(writer, "\\t") != ArenaStatus_Ok)
			return writer->status;
		else if (character < 0x20)
		{
			const char escaped[6] = {'\\', 'u', '0', '0', hexadecimal[character >> 4], hexadecimal[character & 15]};
			if (WriteJsonBytes(writer, std::string_view(escaped, 6)) != ArenaStatus_Ok)
				return writer->status;
		}
		else if (WriteJsonBytes(writer, std::string_view(reinterpret_cast<const char*>(&character), 1)) !=
		         ArenaStatus_Ok)
			return writer->status;
	}
	return WriteJsonBytes(writer, "\"");
}

ArenaStatus WriteJsonUnsigned(JsonWriter* writer, std::uint32_t value)
{
	std::array<char, 32> text = {};
	const int written = std::snprintf(text.data(), text.size(), "%u", value);
	return written > 0 ? WriteJsonBytes(writer, std::string_view(text.data(), static_cast<std::size_t>(written)))
	                   : ArenaStatus_InvalidResult;
}

ArenaStatus WriteJsonThreadArray(JsonWriter* writer, const std::array<std::uint32_t, kThreadCountCapacity>& values,
                                 std::uint32_t count)
{
	if (WriteJsonBytes(writer, "[") != ArenaStatus_Ok)
		return writer->status;
	for (std::uint32_t index = 0; index < count; ++index)
	{
		if (index != 0 && WriteJsonBytes(writer, ", ") != ArenaStatus_Ok)
			return writer->status;
		if (WriteJsonUnsigned(writer, values[index]) != ArenaStatus_Ok)
			return writer->status;
	}
	return WriteJsonBytes(writer, "]");
}

const char* ThreadModeText(ThreadSelectionMode mode)
{
	if (mode == ThreadSelectionMode_Default)
		return "default";
	if (mode == ThreadSelectionMode_Explicit)
		return "explicit";
	return "max";
}

const char* HostSourceText(HostMetadataSource source)
{
	return source == HostMetadataSource_ProcessorTopology ? "windows_processor_topology_api" : "windows_cim";
}

ArenaStatus WriteHostTextField(JsonWriter* writer, const char* prefix, const HostRecord* host, HostText text,
                               const char* suffix)
{
	return WriteJsonBytes(writer, prefix) == ArenaStatus_Ok &&
	               WriteJsonString(writer, HostTextView(host, text)) == ArenaStatus_Ok &&
	               WriteJsonBytes(writer, suffix) == ArenaStatus_Ok
	           ? ArenaStatus_Ok
			   : writer->status;
}

ArenaStatus WriteRunManifest(const Catalog* catalog, const ReleaseCatalog* releaseCatalog, const HostRecord* host,
                             const PreparedRunRequest* request, const RunPathRecord* paths, StatusRecord* error)
{
	std::string configurationJson;
	if (WriteRunConfigurationSnapshot(catalog, &request->configuration,
	                                  {request->engineIndexes.data(), request->engineCount}, &configurationJson,
	                                  error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	HANDLE file = CreateFileW(paths->manifestPath.data(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
	                          FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return RunError(error, ArenaStatus_RunFailed, "manifest_create_failed");
	JsonWriter writer = {};
	writer.handle = file;
	writer.status = ArenaStatus_Ok;
	const CaseRecord& benchmarkCase = request->configuration.benchmarkCase;
	WriteJsonBytes(&writer, "{\n  \"schema_version\": 8,\n  \"run_id\": ");
	WriteJsonString(&writer, std::string_view(paths->runId.data(), paths->runIdSize));
	WriteJsonBytes(&writer, ",\n  \"host_route\": ");
	WriteJsonString(&writer, ReleaseTextView(releaseCatalog, releaseCatalog->hostRoute));
	WriteJsonBytes(&writer, ",\n  \"case_id\": ");
	WriteJsonString(&writer, RunConfigurationTextView(&request->configuration, benchmarkCase.id));
	WriteJsonBytes(&writer, ",\n  \"benchmark_mode\": ");
	WriteJsonString(&writer, request->recordingMode == RecordingMode_On ? "recorded_api" : "headless_api");
	WriteJsonBytes(&writer, ",\n  \"verification_mode\": ");
	WriteJsonString(&writer, request->verificationMode == VerificationMode_On ? "on" : "off");
	WriteJsonBytes(&writer, ",\n  \"recordings\": ");
	if (request->recordingMode == RecordingMode_Off)
		WriteJsonBytes(&writer, "null");
	else
	{
		if (request->recordingKind == RecordingKind_NativeRayHits)
			WriteJsonBytes(&writer, "{\"kind\": \"native_ray_hits\", \"format_version\": 1, "
			                        "\"path_pattern\": \"ray-images/{engine_id}_t{thread_count}_r{repeat_index}.rth\", "
			                        "\"coverage\": \"first_measured_occurrence_of_each_view\", \"thread_counts\": ");
		else
			WriteJsonBytes(&writer, "{\"format_version\": 2, "
			                        "\"path_pattern\": \"replays/{engine_id}_t{thread_count}_r{repeat_index}.bpr\", "
			                        "\"coverage\": \"initial_and_each_measured_work_unit\", \"thread_counts\": ");
		WriteJsonThreadArray(&writer, request->recordingThreads.counts, request->recordingThreads.count);
		WriteJsonBytes(&writer, "}");
	}
	WriteJsonBytes(&writer, ",\n  \"run_configuration\": ");
	WriteJsonBytes(&writer, configurationJson);
	WriteJsonBytes(&writer, ",\n  \"thread_counts\": ");
	WriteJsonThreadArray(&writer, request->threadCounts, request->threadCount);
	WriteJsonBytes(&writer, ",\n  \"thread_selection\": {\n    \"mode\": ");
	WriteJsonString(&writer, ThreadModeText(request->threadSelectionMode));
	WriteJsonBytes(&writer, ",\n    \"host_logical_threads\": ");
	WriteJsonUnsigned(&writer, request->hostLogicalThreadCount);
	WriteJsonBytes(&writer, ",\n    \"thread_counts\": ");
	WriteJsonThreadArray(&writer, request->threadCounts, request->threadCount);
	WriteJsonBytes(&writer, ",\n    \"requested_max\": ");
	WriteJsonUnsigned(&writer, request->requestedMaximumThreadCount);
	WriteJsonBytes(&writer, ",\n    \"effective_max\": ");
	WriteJsonUnsigned(&writer, request->effectiveMaximumThreadCount);
	WriteJsonBytes(&writer, ",\n    \"requested_thread_counts\": ");
	WriteJsonThreadArray(&writer, request->requestedThreadCounts, request->requestedThreadCount);
	WriteJsonBytes(&writer, ",\n    \"dropped_thread_counts\": ");
	WriteJsonThreadArray(&writer, request->droppedThreadCounts, request->droppedThreadCount);
	WriteJsonBytes(&writer, ",\n    \"generated_thread_counts\": ");
	WriteJsonThreadArray(&writer, request->generatedThreadCounts, request->generatedThreadCount);
	WriteJsonBytes(&writer, "\n  }");
	WriteJsonBytes(&writer, ",\n  \"work_unit_id\": ");
	WriteJsonString(&writer, RunConfigurationTextView(&request->configuration, benchmarkCase.workUnitId));
	WriteJsonBytes(&writer, ",\n  \"measured_work_unit_count\": ");
	WriteJsonUnsigned(&writer, benchmarkCase.measuredWorkUnitCount);
	WriteJsonBytes(&writer, ",\n  \"warmup_work_unit_count\": ");
	WriteJsonUnsigned(&writer, benchmarkCase.warmupWorkUnitCount);
	WriteJsonBytes(&writer, ",\n  \"repeat_count\": ");
	WriteJsonUnsigned(&writer, request->repeatCount);
	WriteJsonBytes(&writer, ",\n  \"run_slug_metadata\": {\n    \"run_slug\": ");
	WriteJsonString(&writer, std::string_view(paths->runId.data(), paths->runIdSize));
	WriteJsonBytes(&writer, ",\n    \"base_run_slug\": ");
	WriteJsonString(&writer, std::string_view(paths->baseRunId.data(), paths->baseRunIdSize));
	WriteJsonBytes(&writer, ",\n    \"timestamp\": ");
	WriteJsonString(&writer, std::string_view(paths->timestamp.data(), paths->timestampSize));
	WriteJsonBytes(
	    &writer,
	    ",\n    \"timestamp_source\": \"local_time\",\n    \"timestamp_format\": \"%Y-%m-%d_%H%M\",\n    \"thread_selection_token\": ");
	WriteJsonString(&writer, std::string_view(paths->threadSelectionToken.data(), paths->threadSelectionTokenSize));
	WriteJsonBytes(&writer, ",\n    \"collision_suffix\": ");
	WriteJsonString(&writer, std::string_view(paths->collisionSuffix.data(), paths->collisionSuffixSize));
	WriteJsonBytes(&writer, "\n  },\n  \"host\": {\n    \"cpu\": {\n      \"model\": ");
	WriteJsonString(&writer, HostTextView(host, host->cpuModel));
	WriteJsonBytes(&writer, ",\n      \"physical_cores\": ");
	WriteJsonUnsigned(&writer, host->physicalCoreCount);
	WriteJsonBytes(&writer, ",\n      \"logical_threads\": ");
	WriteJsonUnsigned(&writer, host->logicalThreadCount);
	WriteJsonBytes(&writer, ",\n      \"max_clock_mhz\": ");
	WriteJsonUnsigned(&writer, host->maxClockMhz);
	WriteJsonBytes(&writer, ",\n      \"current_clock_mhz\": ");
	WriteJsonUnsigned(&writer, host->currentClockMhz);
	if (host->performanceCoreCount != 0)
	{
		WriteJsonBytes(&writer, ",\n      \"p_core_count\": ");
		WriteJsonUnsigned(&writer, host->performanceCoreCount);
	}
	if (host->efficiencyCoreCount != 0)
	{
		WriteJsonBytes(&writer, ",\n      \"e_core_count\": ");
		WriteJsonUnsigned(&writer, host->efficiencyCoreCount);
	}
	if (host->cpuTopology.size != 0)
		WriteHostTextField(&writer, ",\n      \"topology\": ", host, host->cpuTopology, "");
	WriteJsonBytes(&writer, "\n    },\n    \"memory\": {\n      \"total_gb\": ");
	WriteJsonUnsigned(&writer, host->totalMemoryGb);
	WriteHostTextField(&writer, ",\n      \"type\": ", host, host->memoryType, "");
	WriteJsonBytes(&writer, ",\n      \"configured_clock_mhz\": ");
	WriteJsonUnsigned(&writer, host->configuredMemoryClockMhz);
	WriteJsonBytes(&writer, ",\n      \"modules\": [");
	for (std::uint32_t moduleIndex = 0; moduleIndex < host->memoryModuleCount; ++moduleIndex)
	{
		const MemoryModuleRecord& module = host->memoryModules[moduleIndex];
		WriteJsonBytes(&writer, moduleIndex == 0 ? "\n        {\n          \"capacity_gb\": "
		                                         : ",\n        {\n          \"capacity_gb\": ");
		WriteJsonUnsigned(&writer, module.capacityGb);
		WriteJsonBytes(&writer, ",\n          \"configured_clock_mhz\": ");
		WriteJsonUnsigned(&writer, module.configuredClockMhz);
		WriteJsonBytes(&writer, ",\n          \"speed_mhz\": ");
		WriteJsonUnsigned(&writer, module.speedMhz);
		WriteHostTextField(&writer, ",\n          \"type\": ", host, module.type, "");
		WriteHostTextField(&writer, ",\n          \"manufacturer\": ", host, module.manufacturer, "");
		WriteHostTextField(&writer, ",\n          \"part_number\": ", host, module.partNumber, "");
		WriteHostTextField(&writer, ",\n          \"slot\": ", host, module.slot, "\n        }");
	}
	WriteJsonBytes(&writer, "\n      ]\n    },\n    \"os\": {\n      \"name\": ");
	WriteJsonString(&writer, HostTextView(host, host->osName));
	WriteHostTextField(&writer, ",\n      \"version\": ", host, host->osVersion, "");
	WriteHostTextField(&writer, ",\n      \"build\": ", host, host->osBuild, "");
	WriteHostTextField(&writer, ",\n      \"architecture\": ", host, host->osArchitecture,
	                   "\n    },\n    \"motherboard\": {\n      \"model\": ");
	WriteJsonString(&writer, HostTextView(host, host->motherboardModel));
	WriteHostTextField(&writer, ",\n      \"bios_version\": ", host, host->biosVersion, "");
	WriteHostTextField(&writer, ",\n      \"bios_date\": ", host, host->biosDate,
	                   "\n    },\n    \"metadata_sources\": {\n");
	struct HostSourceEntry
	{
		const char* field;
		HostMetadataSource source;
	};
	const HostSourceEntry sources[] = {{"cpu.model", host->cpuModelSource},
	                                   {"cpu.physical_cores", host->cpuCountSource},
	                                   {"cpu.logical_threads", host->cpuCountSource},
	                                   {"cpu.max_clock_mhz", HostMetadataSource_WindowsCim},
	                                   {"cpu.current_clock_mhz", HostMetadataSource_WindowsCim},
	                                   {"memory.total_gb", host->memorySource},
	                                   {"memory.type", host->memorySource},
	                                   {"memory.configured_clock_mhz", host->memorySource},
	                                   {"memory.modules", host->memorySource},
	                                   {"os.name", host->operatingSystemSource},
	                                   {"os.version", host->operatingSystemSource},
	                                   {"os.build", host->operatingSystemSource},
	                                   {"os.architecture", host->operatingSystemSource},
	                                   {"motherboard.model", host->motherboardSource},
	                                   {"motherboard.bios_version", host->motherboardSource},
	                                   {"motherboard.bios_date", host->motherboardSource}};
	const std::uint32_t additionalSourceCount = (host->performanceCoreCount != 0 ? 1U : 0U) +
	                                            (host->efficiencyCoreCount != 0 ? 1U : 0U) +
	                                            (host->cpuTopology.size != 0 ? 1U : 0U);
	for (std::uint32_t sourceIndex = 0; sourceIndex < std::size(sources); ++sourceIndex)
	{
		WriteJsonBytes(&writer, "      ");
		WriteJsonString(&writer, sources[sourceIndex].field);
		WriteJsonBytes(&writer, ": ");
		WriteJsonString(&writer, HostSourceText(sources[sourceIndex].source));
		WriteJsonBytes(&writer, sourceIndex + 1 == std::size(sources) && additionalSourceCount == 0 ? "\n" : ",\n");
	}
	std::uint32_t optionalSourceIndex = 0;
	if (host->performanceCoreCount != 0)
	{
		WriteJsonBytes(&writer, "      \"cpu.p_core_count\": ");
		WriteJsonString(&writer, HostSourceText(host->cpuCountSource));
		optionalSourceIndex += 1;
		WriteJsonBytes(&writer, optionalSourceIndex == additionalSourceCount ? "\n" : ",\n");
	}
	if (host->efficiencyCoreCount != 0)
	{
		WriteJsonBytes(&writer, "      \"cpu.e_core_count\": ");
		WriteJsonString(&writer, HostSourceText(host->cpuCountSource));
		optionalSourceIndex += 1;
		WriteJsonBytes(&writer, optionalSourceIndex == additionalSourceCount ? "\n" : ",\n");
	}
	if (host->cpuTopology.size != 0)
	{
		WriteJsonBytes(&writer, "      \"cpu.topology\": ");
		WriteJsonString(&writer, HostSourceText(host->cpuCountSource));
		WriteJsonBytes(&writer, "\n");
	}
	WriteJsonBytes(&writer, "    }\n  }");
	WriteJsonBytes(&writer, benchmarkCase.fixtureKind == CaseFixtureKind_RagdollStairTumble
	                            ? ",\n  \"step_timing\": null"
								: benchmarkCase.fixtureKind == CaseFixtureKind_RayTracing
	                            ? ",\n  \"step_timing\": {\"schema_version\": 2, \"csv\": \"step-timing.csv\", "
	                              "\"svg\": \"step-timing.svg\", \"physics_series\": \"ordinary_coherent_primary_wall\", "
	                              "\"render_series\": \"absent\", \"collection\": \"inline_all_repeats\"}"
	                            : ",\n  \"step_timing\": {\"schema_version\": 2, \"csv\": \"step-timing.csv\", "
								  "\"svg\": \"step-timing.svg\", \"physics_series\": \"per_work_unit_wall\", "
								  "\"render_series\": \"absent\", \"collection\": \"inline_all_repeats\"}");
	WriteJsonBytes(&writer, ",\n  \"observations\": {\"schema_version\": 1, \"csv\": \"observations.csv\"}");
	if (request->verificationMode == VerificationMode_On &&
	    benchmark_stack::TargetFixture(request->configuration.execution.fixtureKind) != 0)
	{
		WriteJsonBytes(&writer, ",\n  \"stability\": {\"csv\": \"stability.csv\", \"criterion\": ");
		WriteJsonString(&writer, StackCriterionName(CurrentStackCriterion(request->configuration.execution.fixtureKind)));
		WriteJsonBytes(&writer, ", \"margin_policy\": ");
		WriteJsonString(&writer, StackMarginPolicyName(CurrentStackCriterion(request->configuration.execution.fixtureKind)));
		WriteJsonBytes(&writer, "}");
	}
	WriteJsonBytes(&writer, ",\n  \"engines\": [");
	for (std::uint32_t selectionIndex = 0; selectionIndex < request->engineCount; ++selectionIndex)
	{
		const EngineRecord& engine = catalog->engines[request->engineIndexes[selectionIndex]];
		const ReleaseArtifactRecord& artifact = releaseCatalog->artifacts[request->artifactIndexes[selectionIndex]];
		WriteJsonBytes(&writer, selectionIndex == 0 ? "\n    {\n      \"id\": " : ",\n    {\n      \"id\": ");
		WriteJsonString(&writer, CatalogTextView(catalog, engine.id));
		WriteJsonBytes(&writer, ",\n      \"release_artifact_manifest\": ");
		WriteJsonString(&writer, ReleaseTextView(releaseCatalog, artifact.manifestPath));
		WriteJsonBytes(&writer, ",\n      \"source_version\": ");
		WriteJsonString(&writer, ReleaseTextView(releaseCatalog, artifact.sourceVersion));
		WriteJsonBytes(&writer, ",\n      \"toolchain_id\": ");
		WriteJsonString(&writer, ReleaseTextView(releaseCatalog, artifact.toolchainId));
		if (artifact.reportVersion.size != 0)
		{
			WriteJsonBytes(&writer, ",\n      \"report_version\": ");
			WriteJsonString(&writer, ReleaseTextView(releaseCatalog, artifact.reportVersion));
		}
		WriteJsonBytes(&writer, ",\n      \"timing_scope\": ");
		WriteJsonString(&writer,
		                benchmarkCase.fixtureKind == CaseFixtureKind_RagdollStairTumble ? std::string_view("absent")
		                : benchmarkCase.fixtureKind == CaseFixtureKind_RayTracing ? std::string_view("ordinary_coherent_primary_wall_sum_inline")
		                : RunConfigurationTextView(&request->configuration, benchmarkCase.workUnitId) == "cycle"
		                    ? std::string_view("timed_phase_wall_sum_inline")
							: std::string_view("work_unit_wall_sum_inline"));
		WriteJsonBytes(&writer, "\n    }");
	}
	WriteJsonBytes(&writer, "\n  ]\n}\n");
	const ArenaStatus writeStatus = FlushJson(&writer);
	CloseHandle(file);
	if (writeStatus != ArenaStatus_Ok)
	{
		DeleteFileW(paths->manifestPath.data());
		return RunError(error, ArenaStatus_RunFailed, "manifest_write_failed");
	}
	return ArenaStatus_Ok;
}

ArenaStatus PersistExecutionFailures(const Catalog* catalog, const RunPathRecord* paths,
                                      std::span<const ExecutionFailure> failures, StatusRecord* error)
{
	using Json = nlohmann::ordered_json;
	const std::filesystem::path path(paths->manifestPath.data());
	const std::filesystem::path temporary = path.wstring() + L".partial";
	{
		std::ifstream input(path);
		Json manifest = Json::parse(input, nullptr, false);
		if (!manifest.is_object())
			return RunError(error, ArenaStatus_RunFailed, "execution_manifest_read_failed");
		input.close();
		Json entries = Json::array();
		for (const ExecutionFailure& failure : failures)
		{
			Json entry = {{"engine_id", CatalogTextView(catalog, catalog->engines[failure.engineIndex].id)},
			              {"thread_count", failure.threadCount}, {"repeat_index", failure.repeatIndex},
			              {"stage", failure.stage == ExecutionStage_Preflight ? "preflight" : "benchmark"},
			              {"outcome", failure.outcome == ExecutionOutcome_NotRun ? "not_run" : "failed"},
			              {"reason", ExecutionFailureReasonName(failure.reason)},
			              {"exit_code", nullptr}, {"detail", failure.detail.data()}};
			if (failure.exitCodePresence == PresenceStatus_Present)
				entry["exit_code"] = failure.exitCode;
			entries.push_back(std::move(entry));
		}
		if (manifest.value("execution_failures", Json::array()) == entries)
			return ArenaStatus_Ok;
		manifest["execution_failures"] = std::move(entries);
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		output << manifest.dump(2) << '\n';
		output.close();
		if (!output || MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
			return RunError(error, ArenaStatus_RunFailed, "execution_manifest_write_failed");
	}
	return ArenaStatus_Ok;
}

}
