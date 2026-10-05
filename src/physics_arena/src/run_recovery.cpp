#include "physics_arena/ray_tracing_images.h"
#include "physics_arena/ray_tracing_results.h"
#include "physics_arena/ray_tracing_corpus.h"
#include "physics_arena/run_recovery.h"
#include "physics_arena/stack_stability.h"

#include "physics_arena/csv_io.h"
#include "physics_arena/report_pipeline.h"
#include "result_pipeline_internal.h"
#include "run_internal.h"
#include "physics_arena/replay.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <memory>
#include <array>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <string_view>

namespace physics_arena
{
ArenaStatus ConsumeRawRow(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error);
ArenaStatus ConsumeObservationRow(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error);

namespace
{
enum RecoveryFileState
{
	RecoveryFileState_Missing = 0,
	RecoveryFileState_Present = 1,
	RecoveryFileState_Invalid = 2,
};

enum RecoveryMatchStatus
{
	RecoveryMatchStatus_Different = 0,
	RecoveryMatchStatus_Equal = 1,
};

constexpr std::array<std::string_view, 3> kTimingColumns = {"step_index", "physics_step_ms", "render_frame_ms"};
constexpr std::array<const wchar_t*, 7> kFinalArtifactNames = {
    L"normalized.csv",  L"summary.csv",     L"observations.csv", L"summary.svg",
    L"step-timing.csv", L"step-timing.svg", L"report.md"};

struct TimingValidationContext
{
	std::array<std::int32_t, kTimingColumns.size()> headerIndexes;
	double physicsTotal;
	std::uint32_t expectedRowCount;
	std::uint32_t rowCount;
	PresenceStatus headerMapped;
};

template <std::size_t Capacity>
ArenaStatus CopyText(std::array<char, Capacity>* destination, std::uint32_t* size, std::string_view value)
{
	if (value.empty() || value.size() >= destination->size())
		return ArenaStatus_InvalidResult;
	std::fill(destination->begin(), destination->end(), '\0');
	std::copy(value.begin(), value.end(), destination->begin());
	*size = static_cast<std::uint32_t>(value.size());
	return ArenaStatus_Ok;
}

ArenaStatus RecoveryError(StatusRecord* error, ArenaStatus status, std::string_view detail)
{
	*error = {};
	CopyText(&error->component, &error->componentSize, "run_recovery");
	CopyText(&error->status, &error->statusSize, ArenaStatusText(status));
	if (CopyText(&error->detail, &error->detailSize, detail) != ArenaStatus_Ok)
		CopyText(&error->detail, &error->detailSize, "recovery_detail_capacity");
	error->code = status;
	return status;
}

RecoveryFileState FileState(const wchar_t* path)
{
	const DWORD attributes = GetFileAttributesW(path);
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND
		           ? RecoveryFileState_Missing
				   : RecoveryFileState_Invalid;
	return (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 ? RecoveryFileState_Present
	                                                                                     : RecoveryFileState_Invalid;
}

RecoveryFileState DirectoryState(const wchar_t* path)
{
	const DWORD attributes = GetFileAttributesW(path);
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND
		           ? RecoveryFileState_Missing
				   : RecoveryFileState_Invalid;
	return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0
	           ? RecoveryFileState_Present
			   : RecoveryFileState_Invalid;
}

ArenaStatus ChildPath(const wchar_t* parent, std::wstring_view child, std::array<wchar_t, kRunPathCapacity>* output,
                      StatusRecord* error)
{
	const std::size_t parentSize = std::wcslen(parent);
	const std::size_t separatorSize =
	    parentSize != 0 && parent[parentSize - 1] != L'\\' && parent[parentSize - 1] != L'/' ? 1 : 0;
	if (parentSize + separatorSize + child.size() >= output->size())
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_path_capacity");
	std::copy(parent, parent + parentSize, output->begin());
	std::size_t size = parentSize;
	if (separatorSize != 0)
		(*output)[size++] = L'\\';
	std::copy(child.begin(), child.end(), output->begin() + size);
	(*output)[size + child.size()] = L'\0';
	return ArenaStatus_Ok;
}

ArenaStatus Utf8ChildPath(const wchar_t* parent, std::string_view child, std::array<wchar_t, kRunPathCapacity>* output,
                          StatusRecord* error)
{
	std::array<wchar_t, kIdentifierCapacity> wide = {};
	if (child.empty() || child.size() > static_cast<std::size_t>(INT_MAX))
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_identifier");
	const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, child.data(), static_cast<int>(child.size()),
	                                     wide.data(), static_cast<int>(wide.size() - 1));
	if (size <= 0)
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_identifier");
	wide[static_cast<std::size_t>(size)] = L'\0';
	return ChildPath(parent, std::wstring_view(wide.data(), static_cast<std::size_t>(size)), output, error);
}

RecoveryMatchStatus TextEqual(const HostRecord* left, HostText leftText, const HostRecord* right, HostText rightText)
{
	return HostTextView(left, leftText) == HostTextView(right, rightText) ? RecoveryMatchStatus_Equal
	                                                                      : RecoveryMatchStatus_Different;
}

RecoveryMatchStatus HostIdentityEqual(const HostRecord* left, const HostRecord* right)
{
	if (left->physicalCoreCount != right->physicalCoreCount || left->logicalThreadCount != right->logicalThreadCount ||
	    left->performanceCoreCount != right->performanceCoreCount ||
	    left->efficiencyCoreCount != right->efficiencyCoreCount || left->maxClockMhz != right->maxClockMhz ||
	    left->totalMemoryGb != right->totalMemoryGb ||
	    left->configuredMemoryClockMhz != right->configuredMemoryClockMhz ||
	    left->memoryModuleCount != right->memoryModuleCount ||
	    TextEqual(left, left->cpuModel, right, right->cpuModel) != RecoveryMatchStatus_Equal ||
	    TextEqual(left, left->cpuTopology, right, right->cpuTopology) != RecoveryMatchStatus_Equal ||
	    TextEqual(left, left->memoryType, right, right->memoryType) != RecoveryMatchStatus_Equal ||
	    TextEqual(left, left->osName, right, right->osName) != RecoveryMatchStatus_Equal ||
	    TextEqual(left, left->osVersion, right, right->osVersion) != RecoveryMatchStatus_Equal ||
	    TextEqual(left, left->osBuild, right, right->osBuild) != RecoveryMatchStatus_Equal ||
	    TextEqual(left, left->osArchitecture, right, right->osArchitecture) != RecoveryMatchStatus_Equal ||
	    TextEqual(left, left->motherboardModel, right, right->motherboardModel) != RecoveryMatchStatus_Equal ||
	    TextEqual(left, left->biosVersion, right, right->biosVersion) != RecoveryMatchStatus_Equal ||
	    TextEqual(left, left->biosDate, right, right->biosDate) != RecoveryMatchStatus_Equal)
		return RecoveryMatchStatus_Different;
	for (std::uint32_t index = 0; index < left->memoryModuleCount; ++index)
	{
		const MemoryModuleRecord& leftModule = left->memoryModules[index];
		const MemoryModuleRecord& rightModule = right->memoryModules[index];
		if (leftModule.capacityGb != rightModule.capacityGb ||
		    leftModule.configuredClockMhz != rightModule.configuredClockMhz ||
		    leftModule.speedMhz != rightModule.speedMhz ||
		    TextEqual(left, leftModule.type, right, rightModule.type) != RecoveryMatchStatus_Equal ||
		    TextEqual(left, leftModule.manufacturer, right, rightModule.manufacturer) != RecoveryMatchStatus_Equal ||
		    TextEqual(left, leftModule.partNumber, right, rightModule.partNumber) != RecoveryMatchStatus_Equal ||
		    TextEqual(left, leftModule.slot, right, rightModule.slot) != RecoveryMatchStatus_Equal)
			return RecoveryMatchStatus_Different;
	}
	return RecoveryMatchStatus_Equal;
}

RecoveryMatchStatus ReleaseTextEqual(const ResultManifestRecord* manifest, CatalogText manifestText,
                                     const ReleaseCatalog* releaseCatalog, CatalogText releaseText)
{
	return ResultTextView(manifest, manifestText) == ReleaseTextView(releaseCatalog, releaseText)
	           ? RecoveryMatchStatus_Equal
			   : RecoveryMatchStatus_Different;
}

struct SavedStabilizationRead
{
	CaseExecutionToggle value;
	PresenceStatus presence;
};

ArenaStatus ReadSavedStabilizationRow(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error)
{
	SavedStabilizationRead* saved = static_cast<SavedStabilizationRead*>(opaque);
	std::uint32_t engineColumn = header->fieldCount, settingsColumn = header->fieldCount;
	for (std::uint32_t index = 0; index < header->fieldCount; ++index)
	{
		const std::string_view name = CsvHeaderTextView(header, header->fields[index]);
		if (name == "engine_id") engineColumn = index;
		if (name == "physics_settings") settingsColumn = index;
	}
	if (engineColumn >= row->fieldCount || settingsColumn >= row->fieldCount)
		return RecoveryError(error, ArenaStatus_InvalidResult, "saved_solver_stabilization_columns");
	if (CsvRowTextView(row, row->fields[engineColumn]) != "unity_physics") return ArenaStatus_Ok;
	const std::string_view settings = CsvRowTextView(row, row->fields[settingsColumn]);
	PresenceStatus present = PresenceStatus_Absent;
	CaseExecutionToggle value = {};
	std::size_t offset = 0;
	while (offset < settings.size())
	{
		const std::size_t end = settings.find(';', offset);
		std::string_view field = settings.substr(offset, end == std::string_view::npos ? end : end - offset);
		while (!field.empty() && field.front() == ' ') field.remove_prefix(1);
		while (!field.empty() && field.back() == ' ') field.remove_suffix(1);
		if (field == "stabilization=on" || field == "stabilization=off")
		{
			if (present == PresenceStatus_Present)
				return RecoveryError(error, ArenaStatus_InvalidResult, "saved_solver_stabilization_ambiguous");
			present = PresenceStatus_Present;
			value = field == "stabilization=on" ? CaseExecutionToggle_Enabled : CaseExecutionToggle_Disabled;
		}
		if (end == std::string_view::npos) break;
		offset = end + 1;
	}
	if (present == PresenceStatus_Absent || (saved->presence == PresenceStatus_Present && saved->value != value))
		return RecoveryError(error, ArenaStatus_InvalidResult, "saved_solver_stabilization_unavailable_or_ambiguous");
	saved->presence = present;
	saved->value = value;
	return ArenaStatus_Ok;
}

ArenaStatus ReconstructRequest(const Catalog* catalog, const ReleaseCatalog* releaseCatalog, const HostRecord* liveHost,
                               const ResultManifestRecord* manifest, const RunPathRecord* paths, PreparedRunRequest* request, StatusRecord* error)
{
	*request = {};
	if (manifest->schemaVersion < 7 || manifest->timingRenderSeries != TimingRenderSeries_Absent ||
	    manifest->renderResolutionPresence != PresenceStatus_Absent ||
	    ResultTextView(manifest, manifest->hostRoute) != ReleaseTextView(releaseCatalog, releaseCatalog->hostRoute) ||
	    HostIdentityEqual(&manifest->host, liveHost) != RecoveryMatchStatus_Equal)
		return RecoveryError(error, ArenaStatus_InvalidResult, "manifest_mode_or_host_drift");
	std::uint32_t caseIndex = 0;
	while (caseIndex < catalog->caseCount &&
	       CatalogTextView(catalog, catalog->cases[caseIndex].id) != ResultTextView(manifest, manifest->caseId))
		++caseIndex;
	if (caseIndex == catalog->caseCount || manifest->engineCount == 0 ||
	    manifest->engineCount > request->engineIndexes.size() || manifest->threadCount == 0 ||
	    manifest->threadCount > request->threadCounts.size() || manifest->repeatCount == 0 ||
	    manifest->repeatCount > kRunRepeatCapacity)
		return RecoveryError(error, ArenaStatus_InvalidResult, "manifest_matrix_contract");
	if (manifest->configuration.execution.fixtureKind == CaseFixtureKind_RagdollStairTumble &&
	    manifest->measurementMode != ResultMeasurementMode_PhysicalQuality)
	{
		return RecoveryError(error, ArenaStatus_InvalidResult, "historical_ragdoll_timing_cannot_resume_as_quality");
	}
	request->caseIndex = caseIndex;
	request->configuration = manifest->configuration;
	request->recordingMode = manifest->recordingMode;
	request->verificationMode = manifest->verificationMode;
	request->recordingThreads = manifest->recordingThreads;
	request->recordingVersion = manifest->recordingVersion;
	request->recordingKind = manifest->recordingKind;
	request->engineCount = manifest->engineCount;
	request->threadCount = manifest->threadCount;
	request->requestedThreadCount = manifest->threadCount;
	request->repeatCount = manifest->repeatCount;
	request->hostLogicalThreadCount = manifest->host.logicalThreadCount;
	request->threadSelectionMode = ThreadSelectionMode_Explicit;
	for (std::uint32_t thread = 0; thread < manifest->threadCount; ++thread)
	{
		request->threadCounts[thread] = manifest->threadCounts[thread];
		request->requestedThreadCounts[thread] = manifest->threadCounts[thread];
	}
	for (std::uint32_t engine = 0; engine < manifest->engineCount; ++engine)
	{
		const ResultEngineSnapshot& snapshot = manifest->engines[engine];
		if (snapshot.engineIndex >= catalog->engineCount ||
		    releaseCatalog->engineArtifactAvailability[snapshot.engineIndex] != PresenceStatus_Present)
			return RecoveryError(error, ArenaStatus_InvalidResult, "manifest_engine_release_missing");
		if (CaseRouteSupported(catalog, catalog->engines[snapshot.engineIndex], caseIndex) == 0)
			return RecoveryError(error, ArenaStatus_InvalidResult, "engine_route_preflight");
		const std::uint32_t artifactIndex = releaseCatalog->engineArtifactIndexes[snapshot.engineIndex];
		if (artifactIndex >= releaseCatalog->artifactCount)
			return RecoveryError(error, ArenaStatus_InvalidResult, "manifest_artifact_range");
		const ReleaseArtifactRecord& artifact = releaseCatalog->artifacts[artifactIndex];
		if (artifact.engineIndex != snapshot.engineIndex ||
		    ReleaseTextEqual(manifest, snapshot.artifactManifestPath, releaseCatalog, artifact.manifestPath) !=
		        RecoveryMatchStatus_Equal ||
		    ReleaseTextEqual(manifest, snapshot.sourceVersion, releaseCatalog, artifact.sourceVersion) !=
		        RecoveryMatchStatus_Equal ||
		    ReleaseTextEqual(manifest, snapshot.toolchainId, releaseCatalog, artifact.toolchainId) !=
		        RecoveryMatchStatus_Equal ||
		    ReleaseTextEqual(manifest, snapshot.reportVersion, releaseCatalog, artifact.reportVersion) !=
		        RecoveryMatchStatus_Equal)
			return RecoveryError(error, ArenaStatus_InvalidResult, "manifest_release_provenance_drift");
		EngineRunSettings& profile = request->configuration.selectedEngineSettings[engine];
		if (profile.solverStabilizationPresence == PresenceStatus_Absent &&
		    (catalog->engines[snapshot.engineIndex].settings.solverStabilizationFixtures & (1u << request->configuration.execution.fixtureKind)) != 0)
		{
			SavedStabilizationRead saved = {};
			CsvHeader header = {};
			CsvReadRecord read = {};
			const std::filesystem::path normalized = std::filesystem::path(paths->resultDirectory.data()) / "normalized.csv";
			if (ReadCsvFile(normalized.c_str(), &header, ReadSavedStabilizationRow, &saved, &read, error) != ArenaStatus_Ok ||
			    saved.presence != PresenceStatus_Present)
				return RecoveryError(error, ArenaStatus_InvalidResult, "setting=solver_stabilization unavailable engine=unity_physics");
			profile.solverStabilization = saved.value;
			profile.solverStabilizationPresence = PresenceStatus_Present;
		}
		if (AdmitEngineRunSettings(catalog, snapshot.engineIndex, ResolveEngineCaseExecution(&request->configuration, engine),
		                           request->configuration.selectedEngineSettings[engine], error) != ArenaStatus_Ok)
			return error->code;
		request->engineIndexes[engine] = snapshot.engineIndex;
		request->artifactIndexes[engine] = artifactIndex;
	}
	const std::uint64_t unitCount = static_cast<std::uint64_t>(request->engineCount) * request->threadCount;
	const std::uint64_t processCount = unitCount * request->repeatCount;
	if (unitCount > UINT32_MAX || processCount > UINT32_MAX || processCount > kRecoveryTupleCapacity)
		return RecoveryError(error, ArenaStatus_InvalidResult, "manifest_matrix_capacity");
	request->totalUnitCount = static_cast<std::uint32_t>(unitCount);
	request->totalProcessCount = static_cast<std::uint32_t>(processCount);
	return ArenaStatus_Ok;
}

ArenaStatus ValidateHeader(const CsvHeader* header, const std::string_view* columns, std::size_t columnCount,
                           std::string_view detail, StatusRecord* error)
{
	if (header->fieldCount != columnCount)
		return RecoveryError(error, ArenaStatus_InvalidResult, detail);
	for (std::uint32_t index = 0; index < header->fieldCount; ++index)
		if (CsvHeaderTextView(header, header->fields[index]) != columns[index])
			return RecoveryError(error, ArenaStatus_InvalidResult, detail);
	return ArenaStatus_Ok;
}

ArenaStatus ParseRecoveryUnsigned(std::string_view value, std::uint32_t* output)
{
	if (value.empty())
		return ArenaStatus_InvalidResult;
	const std::from_chars_result result = std::from_chars(value.data(), value.data() + value.size(), *output);
	return result.ec == std::errc() && result.ptr == value.data() + value.size() ? ArenaStatus_Ok
	                                                                             : ArenaStatus_InvalidResult;
}

ArenaStatus ParseRecoveryFinite(std::string_view value, double* output)
{
	if (value.empty())
		return ArenaStatus_InvalidResult;
	const std::from_chars_result result =
	    std::from_chars(value.data(), value.data() + value.size(), *output, std::chars_format::general);
	return result.ec == std::errc() && result.ptr == value.data() + value.size() && std::isfinite(*output) &&
	               *output > 0.0
	           ? ArenaStatus_Ok
			   : ArenaStatus_InvalidResult;
}

ArenaStatus ValidateTimingRow(const CsvHeader* header, const CsvRow* row, void* opaque, StatusRecord* error)
{
	TimingValidationContext* context = static_cast<TimingValidationContext*>(opaque);
	if (context->headerMapped != PresenceStatus_Present)
	{
		context->headerIndexes.fill(-1);
		if (header->fieldCount != kTimingColumns.size())
			return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_timing_header");
		for (std::uint32_t field = 0; field < header->fieldCount; ++field)
		{
			const std::string_view name = CsvHeaderTextView(header, header->fields[field]);
			std::uint32_t expected = 0;
			while (expected < kTimingColumns.size() && kTimingColumns[expected] != name)
				++expected;
			if (expected == kTimingColumns.size() || context->headerIndexes[expected] >= 0)
				return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_timing_header");
			context->headerIndexes[expected] = static_cast<std::int32_t>(field);
		}
		context->headerMapped = PresenceStatus_Present;
	}
	if (context->rowCount >= context->expectedRowCount)
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_timing_row_overflow");
	const std::string_view stepText =
	    CsvRowTextView(row, row->fields[static_cast<std::size_t>(context->headerIndexes[0])]);
	const std::string_view physicsText =
	    CsvRowTextView(row, row->fields[static_cast<std::size_t>(context->headerIndexes[1])]);
	const std::string_view renderText =
	    CsvRowTextView(row, row->fields[static_cast<std::size_t>(context->headerIndexes[2])]);
	std::uint32_t stepIndex = 0;
	double physics = 0.0;
	if (ParseRecoveryUnsigned(stepText, &stepIndex) != ArenaStatus_Ok || stepIndex != context->rowCount + 1 ||
	    ParseRecoveryFinite(physicsText, &physics) != ArenaStatus_Ok || !renderText.empty())
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_timing_row");
	context->physicsTotal += physics;
	context->rowCount += 1;
	return ArenaStatus_Ok;
}

ArenaStatus UnitPaths(const Catalog* catalog, const PreparedRunRequest* request, const RunPathRecord* paths,
                      std::uint32_t selectedEngineIndex, std::uint32_t selectedThreadIndex,
                      std::array<wchar_t, kRunPathCapacity>* engineDirectory,
                      std::array<wchar_t, kRunPathCapacity>* threadDirectory,
                      std::array<wchar_t, kRunPathCapacity>* rawPath,
                      std::array<wchar_t, kRunPathCapacity>* observationPath, StatusRecord* error)
{
	const std::string_view engineId =
	    CatalogTextView(catalog, catalog->engines[request->engineIndexes[selectedEngineIndex]].id);
	std::array<wchar_t, 32> threadName = {};
	std::array<wchar_t, 128> rawName = {};
	std::array<wchar_t, 128> observationName = {};
	const int threadSize =
	    std::swprintf(threadName.data(), threadName.size(), L"t%u", request->threadCounts[selectedThreadIndex]);
	const int rawSize =
	    std::swprintf(rawName.data(), rawName.size(), L"%.*S_t%u_raw.csv", static_cast<int>(engineId.size()),
		              engineId.data(), request->threadCounts[selectedThreadIndex]);
	const int observationSize =
	    std::swprintf(observationName.data(), observationName.size(), L"%.*S_t%u_observations.csv",
		              static_cast<int>(engineId.size()), engineId.data(), request->threadCounts[selectedThreadIndex]);
	if (threadSize <= 0 || rawSize <= 0 || observationSize <= 0 ||
	    Utf8ChildPath(paths->rawDirectory.data(), engineId, engineDirectory, error) != ArenaStatus_Ok ||
	    ChildPath(engineDirectory->data(), threadName.data(), threadDirectory, error) != ArenaStatus_Ok ||
	    ChildPath(threadDirectory->data(), rawName.data(), rawPath, error) != ArenaStatus_Ok ||
	    ChildPath(threadDirectory->data(), observationName.data(), observationPath, error) != ArenaStatus_Ok)
		return error->code != ArenaStatus_Ok ? error->code
		                                     : RecoveryError(error, ArenaStatus_InvalidResult, "recovery_unit_path");
	return ArenaStatus_Ok;
}

ArenaStatus TimingPath(const Catalog* catalog, const PreparedRunRequest* request, const wchar_t* threadDirectory,
                       std::uint32_t selectedEngineIndex, std::uint32_t selectedThreadIndex, std::uint32_t repeatIndex,
                       std::array<wchar_t, kRunPathCapacity>* output, StatusRecord* error)
{
	const std::string_view engineId =
	    CatalogTextView(catalog, catalog->engines[request->engineIndexes[selectedEngineIndex]].id);
	std::array<wchar_t, 160> name = {};
	const int size =
	    std::swprintf(name.data(), name.size(), L"%.*S_t%u_r%u_step-timing.csv", static_cast<int>(engineId.size()),
		              engineId.data(), request->threadCounts[selectedThreadIndex], repeatIndex);
	return size > 0 ? ChildPath(threadDirectory, name.data(), output, error)
	                : RecoveryError(error, ArenaStatus_InvalidResult, "recovery_timing_path");
}

ArenaStatus ValidateRawFile(const wchar_t* repositoryRoot, const Catalog* catalog, const ReleaseCatalog* releaseCatalog,
                            const PreparedRunRequest* request, const RunPathRecord* paths,
                            std::uint32_t selectedEngineIndex, std::uint32_t selectedThreadIndex, const wchar_t* path,
                            RawContext* context, std::span<const ExecutionFailure> failures, StatusRecord* error)
{
	CsvWriter writer = {};
	if (OpenCsvWriter(L"NUL", &writer, error) != ArenaStatus_Ok)
		return error->code;
	*context = {};
	context->failures = failures;
	context->repositoryRoot = repositoryRoot;
	context->catalog = catalog;
	context->releaseCatalog = releaseCatalog;
	context->request = request;
	context->paths = paths;
	const CaseRecord observationCase = RunObservationCase(request->configuration.benchmarkCase, request->verificationMode);
	context->benchmarkCase = &observationCase;
	context->configuration = &request->configuration;
	context->engine = &catalog->engines[request->engineIndexes[selectedEngineIndex]];
	context->artifact = &releaseCatalog->artifacts[request->artifactIndexes[selectedEngineIndex]];
	context->normalizedWriter = &writer;
	context->engineIndex = request->engineIndexes[selectedEngineIndex];
	context->threadCount = request->threadCounts[selectedThreadIndex];
	constexpr std::string_view rawRelative = "raw/recovery.csv";
	std::copy(rawRelative.begin(), rawRelative.end(), context->rawRelativePath.begin());
	context->rawRelativePathSize = static_cast<std::uint32_t>(rawRelative.size());
	CsvHeader header = {};
	CsvReadRecord read = {};
	const ArenaStatus status = ReadCsvFile(path, &header, ConsumeRawRow, context, &read, error);
	DestroyCsvWriter(&writer);
	context->benchmarkCase = nullptr;
	if (status != ArenaStatus_Ok)
		return status;
	return ValidateHeader(&header, kRawColumns.data(), kRawColumns.size(), "recovery_raw_header", error);
}

ArenaStatus ValidateObservationFile(const Catalog* catalog, const PreparedRunRequest* request,
                                    const RunPathRecord* paths, std::uint32_t selectedEngineIndex,
                                    std::uint32_t selectedThreadIndex, std::uint32_t expectedRows, const wchar_t* path,
                                    ObservationUnitContext* context, std::span<const ExecutionFailure> failures, StatusRecord* error)
{
	CsvWriter writer = {};
	if (OpenCsvWriter(L"NUL", &writer, error) != ArenaStatus_Ok)
		return error->code;
	*context = {};
	context->failures = failures;
	context->repeatCount = request->repeatCount;
	context->engineIndex = request->engineIndexes[selectedEngineIndex];
	context->catalog = catalog;
	const CaseRecord observationCase = RunObservationCase(request->configuration.benchmarkCase, request->verificationMode);
	context->benchmarkCase = &observationCase;
	context->configuration = &request->configuration;
	context->paths = paths;
	context->rootWriter = &writer;
	context->engineId = CatalogTextView(catalog, catalog->engines[request->engineIndexes[selectedEngineIndex]].id);
	context->benchmarkMode = RecordingForThread(request->recordingThreads, request->threadCounts[selectedThreadIndex]) == RecordingMode_On ? "recorded_api" : "headless_api";
	context->threadCount = request->threadCounts[selectedThreadIndex];
	context->expectedRowCount = expectedRows;
	CsvHeader header = {};
	CsvReadRecord read = {};
	const ArenaStatus status = ReadCsvFile(path, &header, ConsumeObservationRow, context, &read, error);
	DestroyCsvWriter(&writer);
	context->benchmarkCase = nullptr;
	if (status != ArenaStatus_Ok)
		return status;
	return ValidateHeader(&header, kObservationSidecarColumns.data(), kObservationSidecarColumns.size(),
	                      "recovery_observation_header", error);
}

} // namespace

ArenaStatus ValidateRunTimingFile(const wchar_t* path, std::uint32_t expectedRows, double expectedTotal,
                               StatusRecord* error)
{
	TimingValidationContext context = {};
	context.expectedRowCount = expectedRows;
	CsvHeader header = {};
	CsvReadRecord read = {};
	if (ReadCsvFile(path, &header, ValidateTimingRow, &context, &read, error) != ArenaStatus_Ok)
		return error->code;
	const double tolerance = std::max(0.000001, (expectedRows + 1) * 0.0000000005);
	if (context.headerMapped != PresenceStatus_Present || context.rowCount != expectedRows ||
	    std::abs(context.physicsTotal - expectedTotal) > tolerance)
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_timing_total_or_cardinality");
	return ArenaStatus_Ok;
}

namespace
{

ArenaStatus CountTimingFiles(const wchar_t* threadDirectory, std::uint32_t* count, StatusRecord* error)
{
	*count = 0;
	std::array<wchar_t, kRunPathCapacity> pattern = {};
	if (ChildPath(threadDirectory, L"*", &pattern, error) != ArenaStatus_Ok)
		return error->code;
	WIN32_FIND_DATAW data = {};
	HANDLE search = FindFirstFileW(pattern.data(), &data);
	if (search == INVALID_HANDLE_VALUE)
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_thread_directory");
	ArenaStatus status = ArenaStatus_Ok;
	do
	{
		const std::wstring_view name(data.cFileName);
		if (name == L"." || name == L"..")
			continue;
		if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
		{
			status = RecoveryError(error, ArenaStatus_InvalidResult, "recovery_reparse_artifact");
			break;
		}
		if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && name.ends_with(L"_step-timing.csv"))
			*count += 1;
	} while (FindNextFileW(search, &data) != 0);
	FindClose(search);
	return status;
}

ArenaStatus ValidateRecoveryStabilityIdentity(const Catalog* catalog, const PreparedRunRequest& request,
                                            const RunPathRecord* paths, StackCriterion expectedCriterion,
                                            const std::vector<StackStabilityResult>& stabilityResults,
                                            StatusRecord* error)
{
	for (const StackStabilityResult& tuple : stabilityResults)
	{
		if (tuple.criterion != expectedCriterion)
			return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_stability_criterion_disagreement");
		std::uint32_t engine = 0, thread = 0;
		while (engine < request.engineCount && CatalogTextView(catalog, catalog->engines[request.engineIndexes[engine]].id) != tuple.engineId) ++engine;
		while (thread < request.threadCount && request.threadCounts[thread] != tuple.threadCount) ++thread;
		if (engine == request.engineCount || thread == request.threadCount || tuple.repeatIndex >= request.repeatCount ||
		    tuple.runId != std::string_view(paths->runId.data(), paths->runIdSize))
			return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_stability_result_identity");
		if (ValidateStackShapeStabilityEvidence(ResolveEngineCaseExecution(&request.configuration, engine), tuple, error) != ArenaStatus_Ok)
			return error->code;
	}
	return ArenaStatus_Ok;
}

ArenaStatus ValidateAbsentTupleArtifacts(const PreparedRunRequest& request, const RunPathRecord* paths,
                                        const wchar_t* rawPath, std::string_view engineId, std::uint32_t threadCount,
                                        std::uint32_t repeat, const std::vector<StackStabilityResult>& stabilityResults,
                                        StatusRecord* error)
{
	const std::filesystem::path replay = request.recordingKind == RecordingKind_NativeRayHits
	    ? RayImageTuplePath(paths->resultDirectory.data(), engineId, threadCount, repeat)
	    : ReplayTuplePath(paths->resultDirectory.data(), engineId, threadCount, repeat);
	const std::filesystem::path spool = replay.parent_path() / ".pending" / replay.filename();
	if (FileState(replay.c_str()) != RecoveryFileState_Missing ||
	    FileState((replay.wstring() + L".partial").c_str()) != RecoveryFileState_Missing ||
	    FileState(spool.c_str()) != RecoveryFileState_Missing ||
	    FileState((spool.wstring() + L".partial").c_str()) != RecoveryFileState_Missing ||
	    FileState(benchmark_stack::TracePath(rawPath, repeat).c_str()) != RecoveryFileState_Missing)
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_unstarted_capture_conflict");
	for (const StackStabilityResult& stability : stabilityResults)
		if (stability.engineId == engineId && stability.threadCount == threadCount && stability.repeatIndex == repeat)
			return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_unstarted_stability_conflict");
	return ArenaStatus_Ok;
}

ArenaStatus InspectUnit(const wchar_t* repositoryRoot, const Catalog* catalog, const ReleaseCatalog* releaseCatalog,
                        const RunPathRecord* paths, RecoveryInventory* inventory, PresenceStatus stabilityRequired,
                        const std::vector<StackStabilityResult>& stabilityResults, std::uint32_t selectedEngineIndex,
                        std::uint32_t selectedThreadIndex, std::uint32_t schemaVersion, StatusRecord* error)
{
	std::array<wchar_t, kRunPathCapacity> engineDirectory = {};
	std::array<wchar_t, kRunPathCapacity> threadDirectory = {};
	std::array<wchar_t, kRunPathCapacity> rawPath = {};
	std::array<wchar_t, kRunPathCapacity> observationPath = {};
	if (UnitPaths(catalog, &inventory->request, paths, selectedEngineIndex, selectedThreadIndex, &engineDirectory,
	              &threadDirectory, &rawPath, &observationPath, error) != ArenaStatus_Ok)
		return error->code;
	const RecoveryFileState rawState = FileState(rawPath.data());
	const RecoveryFileState observationState = FileState(observationPath.data());
	const RecoveryFileState threadState = DirectoryState(threadDirectory.data());
	if (rawState == RecoveryFileState_Invalid || observationState == RecoveryFileState_Invalid ||
	    threadState == RecoveryFileState_Invalid ||
	    (threadState == RecoveryFileState_Missing &&
	     (rawState != RecoveryFileState_Missing || observationState != RecoveryFileState_Missing)))
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_unit_artifact_state");
	RawContext raw = {};
	if (rawState == RecoveryFileState_Present &&
	    ValidateRawFile(repositoryRoot, catalog, releaseCatalog, &inventory->request, paths, selectedEngineIndex,
	                    selectedThreadIndex, rawPath.data(), &raw, inventory->executionFailures, error) != ArenaStatus_Ok)
		return error->code;
	const EffectiveRunConfiguration& configuration = inventory->request.configuration;
	const CaseRecord benchmarkCase = RunObservationCase(configuration.benchmarkCase, inventory->request.verificationMode);
	std::uint32_t rowsPerRepeat = 0;
	for (std::uint32_t index = 0; index < benchmarkCase.observationCount; ++index)
		rowsPerRepeat += configuration.observations[index].sampleIndexCount;
	const std::uint64_t expectedObservationRows =
	    static_cast<std::uint64_t>(inventory->request.repeatCount) * rowsPerRepeat;
	if (expectedObservationRows > UINT32_MAX)
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_observation_capacity");
	ObservationUnitContext observations = {};
	if (observationState == RecoveryFileState_Present &&
	    ValidateObservationFile(catalog, &inventory->request, paths, selectedEngineIndex, selectedThreadIndex,
	                            static_cast<std::uint32_t>(expectedObservationRows), observationPath.data(),
	                            &observations, inventory->executionFailures, error) != ArenaStatus_Ok)
		return error->code;
	if (inventory->executionFailures.empty() && rowsPerRepeat != 0 && observations.rowCount % rowsPerRepeat != 0)
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_observation_partial_repeat");
	std::uint32_t presentTimingCount = 0;
	std::uint32_t completedInUnit = 0;
	for (std::uint32_t repeat = 0; repeat < inventory->request.repeatCount; ++repeat)
	{
		std::array<wchar_t, kRunPathCapacity> timingPath = {};
		if (TimingPath(catalog, &inventory->request, threadDirectory.data(), selectedEngineIndex, selectedThreadIndex,
		               repeat, &timingPath, error) != ArenaStatus_Ok)
			return error->code;
		const RecoveryFileState timingState = FileState(timingPath.data());
		const ExecutionFailure* terminal = FindExecutionFailure(inventory->executionFailures,
		    inventory->request.engineIndexes[selectedEngineIndex], inventory->request.threadCounts[selectedThreadIndex], repeat);
		if (terminal != nullptr)
		{
			if (terminal->outcome == ExecutionOutcome_Failed)
				inventory->completedFailures.push_back({{terminal->engineIndex, terminal->threadCount, repeat},
				    RepeatFailureCause_Execution, terminal->detail.data()});
			if (timingState == RecoveryFileState_Invalid ||
			    (terminal->outcome == ExecutionOutcome_NotRun &&
			     (raw.accumulator.seen[repeat] == PresenceStatus_Present || observations.repeatRows[repeat] != 0 || timingState != RecoveryFileState_Missing)))
				return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_terminal_artifact_conflict");
			if (timingState == RecoveryFileState_Present)
				++presentTimingCount;
			if (terminal->outcome == ExecutionOutcome_NotRun)
			{
				const std::string_view engineId = CatalogTextView(catalog, catalog->engines[terminal->engineIndex].id);
				if (ValidateAbsentTupleArtifacts(inventory->request, paths, rawPath.data(), engineId,
				    terminal->threadCount, repeat, stabilityResults, error) != ArenaStatus_Ok)
					return error->code;
				if (benchmarkCase.fixtureKind == CaseFixtureKind_RayTracing)
				{
					const std::string prefix = std::string(engineId) + "_t" + std::to_string(terminal->threadCount) + "_r" + std::to_string(repeat);
					for (const char* suffix : {"_ray-tracing.csv", "_ray-capabilities.csv", "_ray-process.csv"})
						if (FileState((std::filesystem::path(threadDirectory.data()) / (prefix + suffix)).c_str()) != RecoveryFileState_Missing)
							return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_skipped_ray_conflict");
				}
			}
			++inventory->terminalRepeatCount;
			++completedInUnit;
			continue;
		}

		const std::string_view engineId =
		    CatalogTextView(catalog, catalog->engines[inventory->request.engineIndexes[selectedEngineIndex]].id);
		const std::filesystem::path replayPath = inventory->request.recordingKind == RecordingKind_NativeRayHits
		    ? RayImageTuplePath(paths->resultDirectory.data(), engineId, inventory->request.threadCounts[selectedThreadIndex], repeat)
		    : ReplayTuplePath(paths->resultDirectory.data(), engineId, inventory->request.threadCounts[selectedThreadIndex], repeat);
		const RecoveryFileState replayState = FileState(replayPath.c_str());
		const RecoveryFileState partialState = FileState((replayPath.wstring() + L".partial").c_str());
		const int captureRequired = RecordingForThread(inventory->request.recordingThreads,
		    inventory->request.threadCounts[selectedThreadIndex]) == RecordingMode_On &&
		                                    inventory->finalArtifactState != RecoveryFinalArtifactState_Present
		                                ? 1
		                                : 0;
		const std::filesystem::path spoolPath = replayPath.parent_path() / ".pending" / replayPath.filename();
		const RecoveryFileState spoolState = FileState(spoolPath.c_str());
		if (captureRequired != 0 && (inventory->request.recordingKind == RecordingKind_NativeRayHits || inventory->request.recordingVersion == kCompressedReplayVersion))
		{
			const std::filesystem::path spoolPartialPath = spoolPath.wstring() + L".partial";
			if (FileState(spoolPartialPath.c_str()) != RecoveryFileState_Missing)
				return RecoveryError(error, ArenaStatus_InvalidResult,
				                     "recovery_recording_invalid_or_partial file=" + spoolPartialPath.generic_string());
		}
		if (captureRequired != 0 &&
		    (replayState == RecoveryFileState_Invalid || spoolState == RecoveryFileState_Invalid ||
		     (partialState != RecoveryFileState_Missing && spoolState != RecoveryFileState_Present)))
		{
			const std::string detail = "recovery_recording_invalid_or_partial file=replays/" +
			                           replayPath.filename().string() +
			                           (partialState != RecoveryFileState_Missing ? ".partial" : "");
			return RecoveryError(error, ArenaStatus_InvalidResult, detail);
		}
		if (captureRequired != 0 && replayState == RecoveryFileState_Present &&
		    ValidateRunRecording(catalog, &inventory->request, paths, selectedEngineIndex, selectedThreadIndex, repeat,
		                         error) != ArenaStatus_Ok)
			return error->code;
		if (benchmarkCase.fixtureKind == CaseFixtureKind_RagdollStairTumble && timingState != RecoveryFileState_Missing)
			return RecoveryError(error, ArenaStatus_InvalidResult, "untimed_recovery_has_timing");
		if (timingState == RecoveryFileState_Invalid)
			return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_timing_artifact_state");
		const PresenceStatus rawPresent =
		    raw.accumulator.seen[repeat];
		if (benchmarkCase.fixtureKind == CaseFixtureKind_RayTracing)
		{
			const std::string prefix = std::string(engineId)+"_t"+std::to_string(inventory->request.threadCounts[selectedThreadIndex])+"_r"+std::to_string(repeat);
			const std::filesystem::path directory(threadDirectory.data());
			const std::filesystem::path detail = directory / (prefix+"_ray-tracing.csv");
			const std::filesystem::path probes = directory / (prefix+"_ray-capabilities.csv");
			const std::filesystem::path memory = directory / (prefix+"_ray-process.csv");
			if (rawPresent == PresenceStatus_Present)
			{
				std::vector<RayResultRow> rows; std::vector<RayProbeRow> probeRows; std::vector<RayProcessMemory> memoryRows;
				if (ReadRayTracingResults(detail, &rows, error) != ArenaStatus_Ok ||
				    ValidateRayTracingTuple(rows, engineId, inventory->request.threadCounts[selectedThreadIndex], repeat, inventory->request.configuration.execution, error) != ArenaStatus_Ok ||
				    ValidateRaySecondaryWorkload(rows, std::filesystem::path(paths->resultDirectory.data()) / "ray-corpus", inventory->request.configuration.execution.rayTracing.viewCount, error) != ArenaStatus_Ok ||
				    ReadRayCapabilities(probes, &probeRows, error) != ArenaStatus_Ok ||
				    ReadRayProcessMemory(memory, &memoryRows, error) != ArenaStatus_Ok ||
				    ValidateRayAuxiliaryTuple(probeRows, memoryRows, engineId, inventory->request.threadCounts[selectedThreadIndex], repeat, error) != ArenaStatus_Ok)
					return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_ray_details_incomplete");
				for (const RayResultRow& row : rows)
					if (row.errors != 0 || row.capability == RayCapability_Failed)
						raw.accumulator.repeatFailures[repeat] = RepeatFailureCause_RayNumerical;
			}
			else if (FileState(detail.c_str()) != RecoveryFileState_Missing || FileState(probes.c_str()) != RecoveryFileState_Missing || FileState(memory.c_str()) != RecoveryFileState_Missing)
				return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_partial_ray_tuple_requires_inspection");
		}
		if (stabilityRequired == PresenceStatus_Present && rawPresent == PresenceStatus_Present)
		{
			const StackStabilityResult* stability = nullptr;
			for (const StackStabilityResult& candidate : stabilityResults)
				if (candidate.engineId == engineId && candidate.threadCount == inventory->request.threadCounts[selectedThreadIndex] &&
				    candidate.repeatIndex == repeat && candidate.runId == std::string_view(paths->runId.data(), paths->runIdSize))
					stability = &candidate;
			if (stability == nullptr || stability->coverage != StackCoverage_Complete)
				return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_stability_capture_incomplete");
		}
		const PresenceStatus observationPresent =
		    rowsPerRepeat == 0
		        ? (rawPresent == PresenceStatus_Present && observationState == RecoveryFileState_Present
		               ? PresenceStatus_Present
		               : PresenceStatus_Absent)
		        : (observations.repeatRows[repeat] == rowsPerRepeat ? PresenceStatus_Present : PresenceStatus_Absent);
		if (timingState == RecoveryFileState_Present)
		{
			presentTimingCount += 1;
			const double expectedTotal =
			    rawPresent == PresenceStatus_Present
			        ? raw.accumulator.meanMilliseconds[repeat] * benchmarkCase.measuredWorkUnitCount
			        : 0.0;
			if (rawPresent != PresenceStatus_Present)
				return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_timing_without_raw");
			if (ValidateRunTimingFile(timingPath.data(), benchmarkCase.measuredWorkUnitCount, expectedTotal, error) !=
			        ArenaStatus_Ok)
				return error->code;
		}
		const PresenceStatus timingPresent =
		    timingState == RecoveryFileState_Present ? PresenceStatus_Present : PresenceStatus_Absent;
		if (captureRequired != 0 && rawPresent == PresenceStatus_Present && replayState == RecoveryFileState_Missing &&
		    spoolState == RecoveryFileState_Present &&
		    (inventory->request.recordingKind == RecordingKind_NativeRayHits || inventory->request.recordingVersion == kCompressedReplayVersion) &&
		    observationPresent == PresenceStatus_Present &&
		    (benchmarkCase.fixtureKind == CaseFixtureKind_RagdollStairTumble ||
		     timingPresent == PresenceStatus_Present))
		{
			if (ValidateRunRecording(catalog, &inventory->request, paths, selectedEngineIndex, selectedThreadIndex,
			                         repeat, error, RunRecordingLocation_Spool) != ArenaStatus_Ok)
				return error->code;
			inventory->missingTuples[inventory->missingRepeatCount++] = {selectedEngineIndex, selectedThreadIndex,
			                                                             repeat, PresenceStatus_Present, PresenceStatus_Absent};
			continue;
		}
		if (captureRequired != 0 && rawPresent == PresenceStatus_Present && replayState == RecoveryFileState_Missing)
		{
			const std::string detail = "recovery_recording_missing file=replays/" + replayPath.filename().string();
			return RecoveryError(error, ArenaStatus_InvalidResult, detail);
		}
		if (rawPresent == PresenceStatus_Present && observationPresent == PresenceStatus_Present &&
		    (captureRequired == 0 || replayState == RecoveryFileState_Present) &&
		    (benchmarkCase.fixtureKind == CaseFixtureKind_RagdollStairTumble ||
		     timingPresent == PresenceStatus_Present))
		{
			CompletedRepeatEvidence completed = {{inventory->request.engineIndexes[selectedEngineIndex],
			    inventory->request.threadCounts[selectedThreadIndex], repeat}, raw.accumulator.repeatFailures[repeat], {}};
			if (completed.cause == RepeatFailureCause_None)
				completed.cause = observations.repeatFailures[repeat];
			for (const StackStabilityResult& stability : stabilityResults)
				if (stability.engineId == engineId && stability.threadCount == completed.tuple.threadCount &&
				    stability.repeatIndex == repeat && StackQualificationAssessment(stability, CurrentStackCriterion(inventory->request.configuration.execution.fixtureKind)) == StackAssessment_Fail)
				{
					completed.cause = RepeatFailureCause_StackAssessment;
					completed.detail = stability.reason;
				}
			if (completed.cause != RepeatFailureCause_None)
				inventory->completedFailures.push_back(completed);
			inventory->completedRepeatCount += 1;
			completedInUnit += 1;
			continue;
		}
		const PresenceStatus observationMissing =
		    rowsPerRepeat == 0
		        ? PresenceStatus_Present
		        : (observations.repeatRows[repeat] == 0 ? PresenceStatus_Present : PresenceStatus_Absent);
		if (rawPresent == PresenceStatus_Absent && timingPresent == PresenceStatus_Absent &&
		    (captureRequired == 0 ||
		     (replayState == RecoveryFileState_Missing && spoolState == RecoveryFileState_Missing)) &&
		    observationMissing == PresenceStatus_Present)
		{
			if (schemaVersion == 8 && ValidateAbsentTupleArtifacts(inventory->request, paths, rawPath.data(),
			    engineId, inventory->request.threadCounts[selectedThreadIndex], repeat, stabilityResults, error) != ArenaStatus_Ok)
				return error->code;
			if (inventory->missingRepeatCount >= inventory->missingTuples.size())
				return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_tuple_capacity");
			inventory->missingTuples[inventory->missingRepeatCount++] = {selectedEngineIndex, selectedThreadIndex,
			                                                             repeat, PresenceStatus_Absent, PresenceStatus_Absent};
			continue;
		}
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_mixed_partial_repeat");
	}
	if (threadState == RecoveryFileState_Present)
	{
		std::uint32_t countedTimingFiles = 0;
		if (CountTimingFiles(threadDirectory.data(), &countedTimingFiles, error) != ArenaStatus_Ok)
			return error->code;
		if (countedTimingFiles != presentTimingCount)
			return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_unexpected_timing_fragment");
	}
	if (raw.accumulator.rowCount > inventory->request.repeatCount ||
	    (rowsPerRepeat != 0 && observations.rowCount / rowsPerRepeat > inventory->request.repeatCount))
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_repeat_cardinality");
	if (completedInUnit == inventory->request.repeatCount)
		inventory->completedUnitCount += 1;
	return ArenaStatus_Ok;
}

ArenaStatus ClassifyFinalArtifacts(const RunPathRecord* paths, const PreparedRunRequest& request,
                                  RecoveryFinalArtifactState* finalArtifactState, StatusRecord* error)
{
	const int quality =
	    request.configuration.benchmarkCase.fixtureKind == CaseFixtureKind_RagdollStairTumble ? 1 : 0;
	const std::size_t expectedArtifactCount = kFinalArtifactNames.size() - (quality != 0 ? 2 : 0);
	std::uint32_t presentCount = 0;
	for (const wchar_t* name : kFinalArtifactNames)
	{
		std::array<wchar_t, kRunPathCapacity> path = {};
		if (ChildPath(paths->resultDirectory.data(), name, &path, error) != ArenaStatus_Ok)
			return error->code;
		const RecoveryFileState state = FileState(path.data());
		if (state == RecoveryFileState_Invalid)
			return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_final_artifact_state");
		if (quality != 0 && (std::wcscmp(name, L"step-timing.csv") == 0 || std::wcscmp(name, L"step-timing.svg") == 0))
		{
			if (state != RecoveryFileState_Missing)
				return RecoveryError(error, ArenaStatus_InvalidResult, "untimed_recovery_final_timing");
			continue;
		}
		if (state == RecoveryFileState_Present)
			presentCount += 1;
	}
	if (presentCount == 0)
	{
		*finalArtifactState = RecoveryFinalArtifactState_Absent;
		return ArenaStatus_Ok;
	}
	if (presentCount != expectedArtifactCount)
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_partial_final_artifacts");
	*finalArtifactState = RecoveryFinalArtifactState_Present;
	return ArenaStatus_Ok;
}

ArenaStatus InspectFinalArtifacts(const wchar_t* repositoryRoot, const Catalog* catalog, const RunPathRecord* paths,
                                  RecoveryWorkspace* workspace, RecoveryInventory* inventory, StatusRecord* error)
{
	if (ClassifyFinalArtifacts(paths, inventory->request, &inventory->finalArtifactState, error) != ArenaStatus_Ok)
		return error->code;
	if (inventory->finalArtifactState == RecoveryFinalArtifactState_Absent)
		return ArenaStatus_Ok;
	if (LoadResultPresentation(repositoryRoot, paths->resultDirectory.data(), catalog,
	                           &workspace->finalization.manifest, &workspace->finalization.model,
	                           error) != ArenaStatus_Ok)
		return error->code;
	if (workspace->finalization.model.summaryRowCount != inventory->request.totalUnitCount)
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_final_cardinality");
	inventory->normalizedRowCount = 0;
	for (std::uint32_t row = 0; row < workspace->finalization.model.summaryRowCount; ++row)
		inventory->normalizedRowCount += workspace->finalization.model.summaryRows[row].repeatCount;
	inventory->summaryRowCount = workspace->finalization.model.summaryRowCount;
	inventory->timingRowCount = workspace->finalization.model.timing.rowCount;
	inventory->observationRowCount = workspace->finalization.model.observations.rowCount;
	return ArenaStatus_Ok;
}

ArenaStatus EnsureUnitDirectories(const Catalog* catalog, const PreparedRunRequest* request, const RunPathRecord* paths,
                                  const RecoveryTuple& tuple, StatusRecord* error)
{
	std::array<wchar_t, kRunPathCapacity> engineDirectory = {};
	std::array<wchar_t, kRunPathCapacity> threadDirectory = {};
	std::array<wchar_t, kRunPathCapacity> rawPath = {};
	std::array<wchar_t, kRunPathCapacity> observationPath = {};
	if (UnitPaths(catalog, request, paths, tuple.selectedEngineIndex, tuple.selectedThreadIndex, &engineDirectory,
	              &threadDirectory, &rawPath, &observationPath, error) != ArenaStatus_Ok)
		return error->code;
	for (const wchar_t* directory : {engineDirectory.data(), threadDirectory.data()})
	{
		const RecoveryFileState state = DirectoryState(directory);
		if (state == RecoveryFileState_Invalid)
			return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_directory_state");
		if (state == RecoveryFileState_Missing && CreateDirectoryW(directory, nullptr) == 0)
			return RecoveryError(error, ArenaStatus_RunFailed, "recovery_directory_create");
	}
	if (request->recordingMode == RecordingMode_On)
	{
		std::error_code filesystemError;
		const std::filesystem::path replays = std::filesystem::path(paths->resultDirectory.data()) /
		    (request->recordingKind == RecordingKind_NativeRayHits ? L"ray-images" : L"replays");
		std::filesystem::create_directories(
		    (request->recordingKind == RecordingKind_NativeRayHits || request->recordingVersion == kCompressedReplayVersion) ? replays / L".pending" : replays, filesystemError);
		if (filesystemError)
			return RecoveryError(error, ArenaStatus_RunFailed, "recovery_recording_directory_create");
	}
	return ArenaStatus_Ok;
}

ArenaStatus EmitRecoveryEvent(InvocationContext* invocation, const char* status, const char* detail,
                              StatusRecord* error)
{
	InvocationEvent event = {};
	return AppendInvocationEvent(invocation, "result_recovery", status, detail, &event, error);
}

RecoveryMatchStatus TupleEqual(const RecoveryTuple& left, const RecoveryTuple& right)
{
	return left.selectedEngineIndex == right.selectedEngineIndex &&
	               left.selectedThreadIndex == right.selectedThreadIndex && left.repeatIndex == right.repeatIndex
	           ? RecoveryMatchStatus_Equal
			   : RecoveryMatchStatus_Different;
}
}

ArenaStatus InspectRunRecovery(const wchar_t* repositoryRoot, const Catalog* catalog,
                               const ReleaseCatalog* releaseCatalog, const HostRecord* liveHost,
                               const RunPathRecord* paths, RecoveryWorkspace* workspace, RecoveryInventory* inventory,
                               StatusRecord* error)
{
	if (workspace != nullptr)
		{
			std::destroy_at(workspace);
			std::construct_at(workspace);
		}
	if (inventory != nullptr)
		{
			std::destroy_at(inventory);
			std::construct_at(inventory);
		}
	if (error != nullptr)
		*error = {};
	if (repositoryRoot == nullptr || catalog == nullptr || releaseCatalog == nullptr || liveHost == nullptr ||
	    paths == nullptr || workspace == nullptr || inventory == nullptr || error == nullptr ||
	    DirectoryState(paths->resultDirectory.data()) != RecoveryFileState_Present ||
	    DirectoryState(paths->rawDirectory.data()) != RecoveryFileState_Present ||
	    FileState(paths->manifestPath.data()) != RecoveryFileState_Present)
		return error != nullptr
		           ? RecoveryError(error, ArenaStatus_InvalidArgument, "recovery_inspect_arguments_or_paths")
				   : ArenaStatus_InvalidArgument;
	if (LoadResultManifest(paths->manifestPath.data(), catalog, &workspace->manifest, error) != ArenaStatus_Ok)
		return error->code;
	if (ResultTextView(&workspace->manifest, workspace->manifest.runId) !=
	    std::string_view(paths->runId.data(), paths->runIdSize))
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_run_id_mismatch");
	if (workspace->manifest.schemaVersion < 7)
	{
		if (LoadResultPresentation(repositoryRoot, paths->resultDirectory.data(), catalog,
		                           &workspace->finalization.manifest, &workspace->finalization.model,
		                           error) != ArenaStatus_Ok)
			return error->code;
		const ResultViewModel& model = workspace->finalization.model;
		inventory->request.caseIndex = model.caseIndex;
		inventory->request.engineCount = model.engineCount;
		inventory->request.threadCount = model.threadCount;
		inventory->request.repeatCount = model.repeatCount;
		inventory->request.totalUnitCount = model.engineCount * model.threadCount;
		inventory->request.totalProcessCount = inventory->request.totalUnitCount * model.repeatCount;
		inventory->completedRepeatCount = inventory->request.totalProcessCount;
		inventory->completedUnitCount = inventory->request.totalUnitCount;
		inventory->normalizedRowCount = inventory->completedRepeatCount;
		inventory->summaryRowCount = model.summaryRowCount;
		inventory->timingRowCount = model.timing.rowCount;
		inventory->observationRowCount = model.observations.rowCount;
		inventory->finalArtifactState = RecoveryFinalArtifactState_Present;
		return ArenaStatus_Ok;
	}
	inventory->executionFailures = workspace->manifest.executionFailures;
	if (ReconstructRequest(catalog, releaseCatalog, liveHost, &workspace->manifest, paths, &inventory->request, error) !=
	    ArenaStatus_Ok)
		return error->code;
	if (inventory->request.configuration.execution.fixtureKind == CaseFixtureKind_RayTracing &&
	    AdmitHeavyRayCorpus(std::filesystem::path(paths->resultDirectory.data()) / "ray-corpus", inventory->request.configuration.execution.rayTracing) != benchmark_ray::Status_Ok)
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_ray_corpus_incomplete_or_invalid");
	if (InspectFinalArtifacts(repositoryRoot, catalog, paths, workspace, inventory, error) != ArenaStatus_Ok)
		return error->code;
	std::vector<StackStabilityResult> stabilityResults;
	const std::filesystem::path stabilityPath = std::filesystem::path(paths->resultDirectory.data()) / "stability.csv";
	if (workspace->manifest.stabilityPresence == PresenceStatus_Present && FileState(stabilityPath.c_str()) == RecoveryFileState_Present &&
	    LoadStackStability(stabilityPath, &stabilityResults, error) != ArenaStatus_Ok)
		return error->code;
	if (ValidateRecoveryStabilityIdentity(catalog, inventory->request, paths, workspace->manifest.stabilityCriterion, stabilityResults, error) != ArenaStatus_Ok)
		return error->code;
	for (std::uint32_t engine = 0; engine < inventory->request.engineCount; ++engine)
		for (std::uint32_t thread = 0; thread < inventory->request.threadCount; ++thread)
			if (InspectUnit(repositoryRoot, catalog, releaseCatalog, paths, inventory, workspace->manifest.stabilityPresence,
			    stabilityResults, engine, thread, workspace->manifest.schemaVersion, error) !=
			    ArenaStatus_Ok)
				return error->code;
	if (workspace->manifest.schemaVersion == 8 && workspace->manifest.stabilityPresence == PresenceStatus_Present &&
	    workspace->manifest.stabilityCriterion != StackCriterion_LegacyMovement)
	{
		std::array<PresenceStatus, kEngineCapacity> suppressing = {};
		for (const CompletedRepeatEvidence& completed : inventory->completedFailures)
			suppressing[completed.tuple.engineIndex] = PresenceStatus_Present;
		for (const StackStabilityResult& stability : stabilityResults)
			if (StackQualificationAssessment(stability, CurrentStackCriterion(inventory->request.configuration.execution.fixtureKind)) == StackAssessment_Unassessed)
				for (std::uint32_t engine = 0; engine < inventory->request.engineCount; ++engine)
					if (CatalogTextView(catalog, catalog->engines[inventory->request.engineIndexes[engine]].id) == stability.engineId)
						suppressing[inventory->request.engineIndexes[engine]] = PresenceStatus_Present;
		for (const ExecutionFailure& terminal : inventory->executionFailures)
		{
			if (terminal.reason != ExecutionFailureReason_PreviousRepeatFailed || suppressing[terminal.engineIndex] == PresenceStatus_Present)
				continue;
			std::uint32_t engine = 0, thread = 0;
			while (inventory->request.engineIndexes[engine] != terminal.engineIndex) ++engine;
			while (inventory->request.threadCounts[thread] != terminal.threadCount) ++thread;
			inventory->missingTuples[inventory->missingRepeatCount++] = {engine, thread, terminal.repeatIndex,
			    PresenceStatus_Absent, PresenceStatus_Present};
			--inventory->terminalRepeatCount;
		}
	}
	if (workspace->manifest.schemaVersion == 8)
	{
		for (const CompletedRepeatEvidence& completed : inventory->completedFailures)
		{
			std::uint32_t causalThread = 0;
			while (inventory->request.threadCounts[causalThread] != completed.tuple.threadCount)
				++causalThread;
			std::vector<RunRepeatTuple> remaining;
			remaining.reserve(inventory->missingRepeatCount);
			for (std::uint32_t index = 0; index < inventory->missingRepeatCount; ++index)
			{
				const RecoveryTuple& tuple = inventory->missingTuples[index];
				if (tuple.pendingCompression == PresenceStatus_Absent &&
				    (tuple.selectedThreadIndex > causalThread ||
				     (tuple.selectedThreadIndex == causalThread && tuple.repeatIndex > completed.tuple.repeatIndex)))
					remaining.push_back({inventory->request.engineIndexes[tuple.selectedEngineIndex],
					    inventory->request.threadCounts[tuple.selectedThreadIndex], tuple.repeatIndex});
			}
			SuppressRemainingRepeatTuples(catalog, completed, remaining, &inventory->executionFailures);
		}
		std::uint32_t retainedCount = 0;
		for (std::uint32_t index = 0; index < inventory->missingRepeatCount; ++index)
		{
			const RecoveryTuple& tuple = inventory->missingTuples[index];
			if (tuple.obsoleteSuppression == PresenceStatus_Absent &&
			    FindExecutionFailure(inventory->executionFailures, inventory->request.engineIndexes[tuple.selectedEngineIndex],
			    inventory->request.threadCounts[tuple.selectedThreadIndex], tuple.repeatIndex) != nullptr)
				++inventory->terminalRepeatCount;
			else
				inventory->missingTuples[retainedCount++] = tuple;
		}
		inventory->missingRepeatCount = retainedCount;
		inventory->completedUnitCount = 0;
		for (std::uint32_t engine = 0; engine < inventory->request.engineCount; ++engine)
			for (std::uint32_t thread = 0; thread < inventory->request.threadCount; ++thread)
			{
				std::uint32_t missing = 0;
				for (std::uint32_t index = 0; index < retainedCount; ++index)
					missing += inventory->missingTuples[index].selectedEngineIndex == engine && inventory->missingTuples[index].selectedThreadIndex == thread;
				if (missing == 0)
					++inventory->completedUnitCount;
			}
	}
	// std::sort requires a boolean ordering predicate at this library boundary
	std::sort(inventory->missingTuples.begin(), inventory->missingTuples.begin() + inventory->missingRepeatCount,
	    [](const RecoveryTuple& left, const RecoveryTuple& right)
	    {
		    return std::array<std::uint32_t, 3>{left.selectedEngineIndex, left.selectedThreadIndex, left.repeatIndex} <
		        std::array<std::uint32_t, 3>{right.selectedEngineIndex, right.selectedThreadIndex, right.repeatIndex};
	    });
	if (inventory->completedRepeatCount + inventory->terminalRepeatCount + inventory->missingRepeatCount != inventory->request.totalProcessCount)
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_inventory_cardinality");
	return InspectFinalArtifacts(repositoryRoot, catalog, paths, workspace, inventory, error);
}

ArenaStatus ResumeRunRecovery(const wchar_t* repositoryRoot, const Catalog* catalog,
                              const ReleaseCatalog* releaseCatalog, const HostRecord* liveHost,
                              const RunPathRecord* paths, RunExecutionControl* control, InvocationContext* invocation,
                              RecoveryWorkspace* workspace, RecoveryInventory* inventory,
                              RecoveryExecutionRecord* record, StatusRecord* error)
{
	if (inventory != nullptr)
		{
			std::destroy_at(inventory);
			std::construct_at(inventory);
		}
	if (record != nullptr)
		*record = {};
	if (error != nullptr)
		*error = {};
	if (repositoryRoot == nullptr || catalog == nullptr || releaseCatalog == nullptr || liveHost == nullptr ||
	    paths == nullptr || control == nullptr || invocation == nullptr || workspace == nullptr ||
	    inventory == nullptr || record == nullptr || error == nullptr ||
	    invocation->status != AvailabilityStatus_Available)
		return error != nullptr ? RecoveryError(error, ArenaStatus_InvalidArgument, "recovery_resume_arguments")
		                        : ArenaStatus_InvalidArgument;
	if (LoadResultManifest(paths->manifestPath.data(), catalog, &workspace->manifest, error) != ArenaStatus_Ok)
		return error->code;
	if (workspace->manifest.stabilityPresence == PresenceStatus_Present &&
	    workspace->manifest.stabilityCriterion == StackCriterion_LegacyMovement)
		return RecoveryError(error, ArenaStatus_InvalidResult,
		    "recovery_legacy_stability_resume_unsupported criterion=authored-box-rest-envelope current_criterion_reassessment_required");
	if (workspace->manifest.verificationMode == VerificationMode_On &&
	    workspace->manifest.configuration.execution.fixtureKind == CaseFixtureKind_BoxContactIslands)
	{
		if (workspace->manifest.stabilityPresence == PresenceStatus_Present &&
		    workspace->manifest.stabilityCriterion != StackCriterion_ContactIslandsShapePreservation)
			return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_contact_islands_noncurrent_policy_resume_unsupported current_policy_reassessment_required");
		const CaseExecutionSpec& execution = workspace->manifest.configuration.execution;
		if (execution.measuredWorkUnitCount <= execution.timestepHz)
			return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_contact_islands_insufficient_terminal_window requires_measured_steps=" + std::to_string(execution.timestepHz + 1));
	}
	if (workspace->manifest.verificationMode == VerificationMode_On &&
	    (workspace->manifest.configuration.execution.fixtureKind == CaseFixtureKind_LargePyramid ||
	     workspace->manifest.configuration.execution.fixtureKind == CaseFixtureKind_PyramidWall) &&
	    workspace->manifest.stabilityPresence == PresenceStatus_Present &&
	    workspace->manifest.stabilityCriterion != StackCriterion_PyramidUnforcedShape)
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_pyramid_noncurrent_policy_resume_unsupported current_policy_reassessment_required");
	if (workspace->manifest.schemaVersion < 7)
		return RecoveryError(error, ArenaStatus_InvalidResult, "historical_result_resume_unsupported schema=5_or_6");
	std::uint32_t regeneratedCount = 0;
	if (workspace->manifest.stabilityPresence == PresenceStatus_Present)
	{
		PreparedRunRequest frozen = {};
		if (ReconstructRequest(catalog, releaseCatalog, liveHost, &workspace->manifest, paths, &frozen, error) != ArenaStatus_Ok)
			return error->code;
		const std::filesystem::path table = std::filesystem::path(paths->resultDirectory.data()) / "stability.csv";
		std::vector<StackStabilityResult> committed;
		if (FileState(table.c_str()) == RecoveryFileState_Present && LoadStackStability(table, &committed, error) != ArenaStatus_Ok)
			return error->code;
		if (ValidateRecoveryStabilityIdentity(catalog, frozen, paths, workspace->manifest.stabilityCriterion, committed, error) != ArenaStatus_Ok)
			return error->code;
		const std::size_t savedAssessmentCount = committed.size();
		if (workspace->manifest.schemaVersion == 8)
		{
			inventory->request = frozen;
			inventory->executionFailures = workspace->manifest.executionFailures;
			if (ClassifyFinalArtifacts(paths, frozen, &inventory->finalArtifactState, error) != ArenaStatus_Ok)
				return error->code;
		}
		for (std::uint32_t engine = 0; engine < frozen.engineCount; ++engine)
		{
			const std::string_view engineId = CatalogTextView(catalog, catalog->engines[frozen.engineIndexes[engine]].id);
			for (std::uint32_t thread = 0; thread < frozen.threadCount; ++thread)
			{
				std::array<wchar_t, kRunPathCapacity> engineDirectory = {}, threadDirectory = {}, rawPath = {}, observationPath = {};
				if (UnitPaths(catalog, &frozen, paths, engine, thread, &engineDirectory, &threadDirectory,
				              &rawPath, &observationPath, error) != ArenaStatus_Ok)
					return error->code;
				for (std::uint32_t repeat = 0; repeat < frozen.repeatCount; ++repeat)
				{
					const StackStabilityResult* saved = nullptr;
					for (const StackStabilityResult& candidate : committed)
						if (candidate.engineId == engineId && candidate.threadCount == frozen.threadCounts[thread] && candidate.repeatIndex == repeat)
							saved = &candidate;
					if (saved != nullptr && saved->coverage == StackCoverage_Complete)
						continue;
					const std::filesystem::path trace = benchmark_stack::TracePath(rawPath.data(), repeat);
					if (FileState(trace.c_str()) != RecoveryFileState_Present)
						continue;
					StackStabilityResult derived = {};
					if (AnalyzeStackTrace(trace, ResolveEngineCaseExecution(&frozen.configuration, engine), std::string_view(paths->runId.data(), paths->runIdSize),
					    engineId, frozen.threadCounts[thread], repeat, &derived, error) != ArenaStatus_Ok)
						return error->code;
					if (derived.coverage == StackCoverage_Complete)
					{
						if (workspace->manifest.schemaVersion == 8)
							committed.push_back(std::move(derived));
						else
						{
							if (CommitStackStability(table, derived, error) != ArenaStatus_Ok)
								return error->code;
							++regeneratedCount;
						}
					}
				}
			}
		}
		if (workspace->manifest.schemaVersion == 8)
		{
			// admit measured outputs and required coverage across the matrix before publishing derived assessments
			for (std::uint32_t engine = 0; engine < frozen.engineCount; ++engine)
				for (std::uint32_t thread = 0; thread < frozen.threadCount; ++thread)
					if (InspectUnit(repositoryRoot, catalog, releaseCatalog, paths, inventory, PresenceStatus_Present,
					    committed, engine, thread, workspace->manifest.schemaVersion, error) != ArenaStatus_Ok)
						return error->code;
			for (std::size_t index = savedAssessmentCount; index < committed.size(); ++index)
			{
				if (CommitStackStability(table, committed[index], error) != ArenaStatus_Ok)
					return error->code;
				++regeneratedCount;
			}
		}
	}
	if (InspectRunRecovery(repositoryRoot, catalog, releaseCatalog, liveHost, paths, workspace, inventory, error) !=
	    ArenaStatus_Ok)
	{
		record->status = error->code;
		return error->code;
	}
	if (inventory->finalArtifactState == RecoveryFinalArtifactState_Present && inventory->missingRepeatCount == 0)
	{
		if (regeneratedCount != 0)
		{
			record->status = ArenaStatus_Ok;
			record->exitCode = 0;
			return ArenaStatus_Ok;
		}
		record->status = ArenaStatus_InvalidResult;
		return RecoveryError(error, ArenaStatus_InvalidResult, "recovery_finalized_result");
	}
	if (workspace->manifest.schemaVersion == 8 &&
	    PersistExecutionFailures(catalog, paths, inventory->executionFailures, error) != ArenaStatus_Ok)
		return error->code;
	while (inventory->missingRepeatCount != 0)
	{
		const RecoveryTuple tuple = inventory->missingTuples[0];
		const std::string_view engineId =
		    CatalogTextView(catalog, catalog->engines[inventory->request.engineIndexes[tuple.selectedEngineIndex]].id);
		std::array<char, kDetailCapacity> detail = {};
		const int detailSize = std::snprintf(
		    detail.data(), detail.size(), "engine=%.*s thread=%u repeat=%u", static_cast<int>(engineId.size()),
		    engineId.data(), inventory->request.threadCounts[tuple.selectedThreadIndex], tuple.repeatIndex);
		if (detailSize <= 0 ||
		    EnsureUnitDirectories(catalog, &inventory->request, paths, tuple, error) != ArenaStatus_Ok ||
		    EmitRecoveryEvent(invocation, "running", detail.data(), error) != ArenaStatus_Ok)
		{
			record->status = error->code != ArenaStatus_Ok ? error->code : ArenaStatus_InvalidResult;
			return record->status;
		}
		const std::uint32_t priorMissingCount = inventory->missingRepeatCount;
		ArenaStatus tupleStatus = ArenaStatus_Ok;
		if (control->cancellationRequested.load(std::memory_order_acquire) != 0)
			return RecoveryError(error, ArenaStatus_Interrupted, "recovery_cancelled_before_launch");
		if (inventory->finalArtifactState == RecoveryFinalArtifactState_Present)
		{
			// retire derived presentation only after the full saved result and exact missing tuple have been admitted
			for (const wchar_t* name : kFinalArtifactNames)
			{
				const std::filesystem::path artifact = std::filesystem::path(paths->resultDirectory.data()) / name;
				if (DeleteFileW(artifact.c_str()) == 0)
					return RecoveryError(error, ArenaStatus_RunFailed, "recovery_presentation_retirement file=" + artifact.generic_string());
			}
			inventory->finalArtifactState = RecoveryFinalArtifactState_Absent;
		}
		if (tuple.obsoleteSuppression == PresenceStatus_Present)
		{
			std::vector<ExecutionFailure>::iterator terminal = inventory->executionFailures.begin();
			while (terminal != inventory->executionFailures.end() &&
			       (terminal->engineIndex != inventory->request.engineIndexes[tuple.selectedEngineIndex] ||
			        terminal->threadCount != inventory->request.threadCounts[tuple.selectedThreadIndex] || terminal->repeatIndex != tuple.repeatIndex))
				++terminal;
			inventory->executionFailures.erase(terminal);
			if (PersistExecutionFailures(catalog, paths, inventory->executionFailures, error) != ArenaStatus_Ok)
				return error->code;
		}

		RunExecutionResult execution = {};
		RunProcessOutcome process = {};
		if (tuple.pendingCompression == PresenceStatus_Absent)
		{
			tupleStatus = RunOneProcess(repositoryRoot, catalog, releaseCatalog, &inventory->request, paths,
			    tuple.selectedEngineIndex, tuple.selectedThreadIndex, tuple.repeatIndex, control, invocation,
			    &execution, error, RayRunStage_Heavy, &process);
			record->startedProcessCount += execution.startedProcessCount;
			if (process.disposition == RunProcessDisposition_Completed)
			{
				std::array<wchar_t, kRunPathCapacity> directory = {};
				if (EnsureUnitDirectories(catalog, &inventory->request, paths, tuple.selectedEngineIndex, tuple.selectedThreadIndex,
				    &directory, error) != ArenaStatus_Ok)
					return error->code;
				RunProcessDisposition outputDisposition = RunProcessDisposition_HarnessFailure;
				tupleStatus = ValidateRawOutput(repositoryRoot, catalog, releaseCatalog, &inventory->request, paths,
				    tuple.selectedEngineIndex, tuple.selectedThreadIndex, tuple.repeatIndex, directory.data(),
				    inventory->executionFailures, &outputDisposition, &process.completed, error);
				if (tupleStatus != ArenaStatus_Ok)
				{
					process.disposition = outputDisposition;
					process.reason = ExecutionFailureReason_InvalidOutput;
				}
			}
			if (process.disposition == RunProcessDisposition_EngineFailure && workspace->manifest.schemaVersion == 8)
			{
				inventory->executionFailures.push_back({inventory->request.engineIndexes[tuple.selectedEngineIndex],
				    inventory->request.threadCounts[tuple.selectedThreadIndex], tuple.repeatIndex, ExecutionStage_Benchmark,
				    ExecutionOutcome_Failed, process.reason, process.exitCodePresence, process.exitCode, error->detail});
				if (PersistExecutionFailures(catalog, paths, inventory->executionFailures, error) != ArenaStatus_Ok)
					return error->code;
				tupleStatus = ArenaStatus_Ok;
				*error = {};
			}
		}
		if (tuple.pendingCompression == PresenceStatus_Present && tupleStatus == ArenaStatus_Ok && RecordingForThread(inventory->request.recordingThreads,
		    inventory->request.threadCounts[tuple.selectedThreadIndex]) == RecordingMode_On &&
		    (inventory->request.recordingKind == RecordingKind_NativeRayHits || inventory->request.recordingVersion == kCompressedReplayVersion))
			tupleStatus =
			    CompressRunRecording(catalog, &inventory->request, paths, tuple.selectedEngineIndex,
				                     tuple.selectedThreadIndex, tuple.repeatIndex, control, invocation, error);
		if (tupleStatus != ArenaStatus_Ok)
		{
			record->status = tupleStatus;
			return tupleStatus;
		}
		if (InspectRunRecovery(repositoryRoot, catalog, releaseCatalog, liveHost, paths, workspace, inventory, error) !=
		        ArenaStatus_Ok ||
		    inventory->missingRepeatCount >= priorMissingCount ||
		    (inventory->missingRepeatCount != 0 &&
		     TupleEqual(inventory->missingTuples[0], tuple) == RecoveryMatchStatus_Equal))
		{
			record->status = error->code != ArenaStatus_Ok ? error->code : ArenaStatus_InvalidResult;
			if (error->code == ArenaStatus_Ok)
				RecoveryError(error, ArenaStatus_InvalidResult, "recovery_repeat_not_completed");
			return record->status;
		}
		if (inventory->request.verificationMode == VerificationMode_Off && tuple.pendingCompression != PresenceStatus_Present &&
		    FindExecutionFailure(inventory->executionFailures, inventory->request.engineIndexes[tuple.selectedEngineIndex], inventory->request.threadCounts[tuple.selectedThreadIndex], tuple.repeatIndex) == nullptr)
			control->unverifiedRepeatCount.fetch_add(1, std::memory_order_relaxed);
		if (workspace->manifest.schemaVersion == 8 &&
		    PersistExecutionFailures(catalog, paths, inventory->executionFailures, error) != ArenaStatus_Ok)
			return error->code;
		if (tuple.pendingCompression == PresenceStatus_Present || process.disposition == RunProcessDisposition_Completed)
			record->completedRepeatCount += 1;
		if (EmitRecoveryEvent(invocation, "ok", detail.data(), error) != ArenaStatus_Ok)
		{
			record->status = error->code;
			return error->code;
		}
	}
	if (FinalizeFreshRunArtifacts(repositoryRoot, catalog, releaseCatalog, liveHost, &inventory->request, paths,
	                              &workspace->finalization, &record->finalization, error) != ArenaStatus_Ok)
	{
		record->status = error->code;
		return error->code;
	}
	if (InspectRunRecovery(repositoryRoot, catalog, releaseCatalog, liveHost, paths, workspace, inventory, error) !=
	        ArenaStatus_Ok ||
	    inventory->finalArtifactState != RecoveryFinalArtifactState_Present)
	{
		record->status = error->code != ArenaStatus_Ok ? error->code : ArenaStatus_InvalidResult;
		if (error->code == ArenaStatus_Ok)
			RecoveryError(error, ArenaStatus_InvalidResult, "recovery_finalization_incomplete");
		return record->status;
	}
	record->exitCode = 0;
	record->status = ArenaStatus_Ok;
	return ArenaStatus_Ok;
}
}
