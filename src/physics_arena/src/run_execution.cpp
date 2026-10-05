#include "physics_arena/ray_tracing_images.h"
#include "physics_arena/ray_tracing_results.h"
#include "physics_arena/ray_tracing_corpus.h"
#include "physics_arena/ray_tracing_preflight.h"
#include "physics_arena/ray_tracing_corpus_cache.h"
#include "run_internal.h"
#include "stack_verification_channel.h"
#include "result_pipeline_internal.h"
#include "physics_arena/replay.h"
#include "physics_arena/stack_stability.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <climits>
#include <cstdio>
#include <cwchar>
#include <string_view>

namespace physics_arena
{
ArenaStatus ConsumeRawRow(const CsvHeader*, const CsvRow*, void*, StatusRecord*);
ArenaStatus ConsumeObservationRow(const CsvHeader*, const CsvRow*, void*, StatusRecord*);
ArenaStatus EmitRunEvent(InvocationContext* invocation, EventTransport* transport, const char* component,
                         const char* status, const char* detail, StatusRecord* error)
{
	InvocationEvent event = {};
	if (AppendInvocationEvent(invocation, component, status, detail, &event, error) != ArenaStatus_Ok)
		return error->code;
	if (PushEvent(transport, &event) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_InvalidResult, "event_transport_capacity");
	return ArenaStatus_Ok;
}

ArenaStatus Utf8ToWide(std::string_view value, std::array<wchar_t, kRunPathCapacity>* output)
{
	if (value.empty() || value.size() > static_cast<std::size_t>(INT_MAX))
		return ArenaStatus_InvalidResult;
	const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
	                                        output->data(), static_cast<int>(output->size() - 1));
	if (written <= 0)
		return ArenaStatus_InvalidResult;
	(*output)[written] = L'\0';
	return ArenaStatus_Ok;
}

ArenaStatus OpenProcessLog(const InvocationContext* invocation, const Catalog* catalog, const EngineRecord& engine,
                           std::uint32_t threadCount, std::uint32_t repeatIndex, const wchar_t* extension,
                           HANDLE* handle, StatusRecord* error)
{
	std::array<wchar_t, kRunPathCapacity> invocationDirectory = {};
	if (Utf8ToWide(std::string_view(invocation->directoryPath.data.data(), invocation->directoryPath.size),
	               &invocationDirectory) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_InvalidResult, "invocation_directory_path");
	std::array<wchar_t, kRunPathCapacity> processDirectory = {};
	if (BuildRunChildPath(invocationDirectory.data(), L"process", &processDirectory) != ArenaStatus_Ok ||
	    EnsureDirectory(processDirectory.data()) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_RunFailed, "process_log_directory");
	std::array<wchar_t, kRunPathCapacity> engineId = {};
	std::uint32_t engineIdSize = 0;
	if (AppendRunUtf8AsWide(&engineId, &engineIdSize, CatalogTextView(catalog, engine.id)) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_InvalidResult, "process_log_engine_id");
	std::array<wchar_t, kRunPathCapacity> fileName = {};
	const int written = std::swprintf(fileName.data(), fileName.size(), L"%ls_t%u_r%u.%ls.log", engineId.data(),
	                                  threadCount, repeatIndex, extension);
	std::array<wchar_t, kRunPathCapacity> path = {};
	if (written <= 0 || BuildRunChildPath(processDirectory.data(), fileName.data(), &path) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_InvalidResult, "process_log_path");
	*handle = CreateFileW(path.data(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
	                      nullptr);
	return *handle != INVALID_HANDLE_VALUE ? ArenaStatus_Ok
	                                       : RunError(error, ArenaStatus_RunFailed, "process_log_create_failed");
}

ArenaStatus WriteProcessStream(const InvocationContext* invocation, const Catalog* catalog, const EngineRecord& engine,
                               std::uint32_t threadCount, std::uint32_t repeatIndex, const wchar_t* extension,
                               const char* bytes, std::uint32_t size, HANDLE* handle, StatusRecord* error)
{
	if (size == 0)
		return ArenaStatus_Ok;
	if (*handle == nullptr && OpenProcessLog(invocation, catalog, engine, threadCount, repeatIndex, extension, handle,
	                                         error) != ArenaStatus_Ok)
		return error->code;
	DWORD written = 0;
	return WriteFile(*handle, bytes, size, &written, nullptr) != 0 && written == size
	           ? ArenaStatus_Ok
			   : RunError(error, ArenaStatus_RunFailed, "process_log_write_failed");
}

ArenaStatus ValidateRunRecording(const Catalog* catalog, const PreparedRunRequest* request, const RunPathRecord* paths,
                                 std::uint32_t selectedEngineIndex, std::uint32_t selectedThreadIndex,
                                 std::uint32_t repeatIndex, StatusRecord* error, RunRecordingLocation location)
{
	const CaseExecutionSpec execution = ResolveEngineCaseExecution(&request->configuration, selectedEngineIndex);
	const std::string_view engineId =
	    CatalogTextView(catalog, catalog->engines[request->engineIndexes[selectedEngineIndex]].id);
	if (request->recordingKind == RecordingKind_NativeRayHits)
	{
		std::filesystem::path path = RayImageTuplePath(paths->resultDirectory.data(), engineId, request->threadCounts[selectedThreadIndex], repeatIndex);
		if (location == RunRecordingLocation_Spool) path = path.parent_path() / ".pending" / path.filename();
		RayImageArchive archive = {};
		ArenaStatus status = OpenRayImages(path, engineId, request->threadCounts[selectedThreadIndex], repeatIndex, &archive, error);
		if (status == ArenaStatus_Ok) status = ValidateRayImages(&archive, error);
		const int dimensionsMatch = archive.width == execution.rayTracing.width && archive.height == execution.rayTracing.height;
		CloseRayImages(&archive);
		if (status != ArenaStatus_Ok) return status;
		return dimensionsMatch != 0 ? ArenaStatus_Ok : RunError(error, ArenaStatus_InvalidResult, "ray_image_resolution");
	}
	std::array<char, benchmark_visual::kVisualBridgeTextCapacity> engineText = {};
	std::copy(engineId.begin(), engineId.end(), engineText.begin());
	benchmark_visual::VisualScene expected = {};
	benchmark_replay::SetRecordingSceneIdentity(execution, engineText.data(),
	                                            request->threadCounts[selectedThreadIndex], repeatIndex, &expected);
	ReplayRecording recording = {};
	std::filesystem::path path = ReplayTuplePath(paths->resultDirectory.data(), engineId,
	                                             request->threadCounts[selectedThreadIndex], repeatIndex);
	if (location == RunRecordingLocation_Spool)
		path = path.parent_path() / ".pending" / path.filename();
	ArenaStatus status = OpenReplay(path, &expected.identity, &recording, error);
	if (status != ArenaStatus_Ok)
		return status;
	const benchmark_replay::WorkKind workKind = execution.fixtureKind == CaseFixtureKind_SpatialQueryTrace
	                                                ? benchmark_replay::WorkKind_QueryBatch
	                                                : benchmark_replay::WorkKind_Dynamics;
	if (recording.formatVersion != (location == RunRecordingLocation_Spool ? 1U : request->recordingVersion) ||
	    recording.scene.instanceCount != execution.visualInstanceCount ||
	    recording.scene.dynamicTransformCount != execution.dynamicBodyCount ||
	    recording.scene.debugPrimitiveCount != execution.visualDebugPrimitiveCount || recording.workKind != workKind ||
	    recording.timestep != (workKind == benchmark_replay::WorkKind_QueryBatch ? 0 : 1.0 / execution.timestepHz))
		status = RunError(error, ArenaStatus_InvalidResult, "recording_effective_configuration_mismatch");
	if (status == ArenaStatus_Ok)
		status = SeekReplay(&recording, execution.measuredWorkUnitCount, error);
	CloseReplay(&recording);
	return status;
}

void RemoveFailedRecordingFile(const std::filesystem::path& path, StatusRecord* error)
{
	std::error_code cleanup;
	std::filesystem::remove(path, cleanup);
	if (cleanup)
	{
		const std::string detail(error->detail.data(), error->detailSize);
		RunError(error, error->code,
		         detail + "; cleanup_failed file=" + path.generic_string() +
		             " system_error=" + std::to_string(cleanup.value()));
	}
}

void CleanupFailedRunRecording(const Catalog* catalog, const PreparedRunRequest* request, const RunPathRecord* paths,
                               std::uint32_t selectedEngineIndex, std::uint32_t selectedThreadIndex,
                               std::uint32_t repeatIndex, StatusRecord* error)
{
	const std::string_view engineId = CatalogTextView(catalog, catalog->engines[request->engineIndexes[selectedEngineIndex]].id);
	const std::filesystem::path finalPath = request->recordingKind == RecordingKind_NativeRayHits
	    ? RayImageTuplePath(paths->resultDirectory.data(), engineId, request->threadCounts[selectedThreadIndex], repeatIndex)
	    : ReplayTuplePath(paths->resultDirectory.data(), engineId, request->threadCounts[selectedThreadIndex], repeatIndex);
	const RunRecordingLocation location = request->recordingKind == RecordingKind_NativeRayHits || request->recordingVersion == kCompressedReplayVersion
	                                          ? RunRecordingLocation_Spool
	                                          : RunRecordingLocation_Published;
	const std::filesystem::path spool = location == RunRecordingLocation_Spool
	                                        ? finalPath.parent_path() / ".pending" / finalPath.filename()
	                                        : finalPath;
	StatusRecord captureError = {};
	if (ValidateRunRecording(catalog, request, paths, selectedEngineIndex, selectedThreadIndex, repeatIndex,
	                         &captureError, location) != ArenaStatus_Ok)
		RemoveFailedRecordingFile(spool, error);
	RemoveFailedRecordingFile(spool.wstring() + L".partial", error);
}

ArenaStatus CompressRunRecording(const Catalog* catalog, const PreparedRunRequest* request, const RunPathRecord* paths,
                                 std::uint32_t selectedEngineIndex, std::uint32_t selectedThreadIndex,
                                 std::uint32_t repeatIndex, RunExecutionControl* control, InvocationContext* invocation,
                                 StatusRecord* error)
{

	const std::string_view engineId =
	    CatalogTextView(catalog, catalog->engines[request->engineIndexes[selectedEngineIndex]].id);
	const std::uint32_t threadCount = request->threadCounts[selectedThreadIndex];
	if (request->recordingKind == RecordingKind_NativeRayHits)
	{
		const std::filesystem::path finalPath = RayImageTuplePath(paths->resultDirectory.data(), engineId, threadCount, repeatIndex);
		const std::filesystem::path spool = finalPath.parent_path() / ".pending" / finalPath.filename();
		control->compressingReplay.store(1, std::memory_order_release);
		const ArenaStatus status = CompressRayImages(spool, finalPath, engineId, threadCount, repeatIndex, &control->cancellationRequested, error);
		control->compressingReplay.store(0, std::memory_order_release);
		return status;
	}
	if (request->recordingVersion == kCompressedReplayVersion)
	{
		const std::filesystem::path finalPath =
		    ReplayTuplePath(paths->resultDirectory.data(), engineId, threadCount, repeatIndex);
		const std::filesystem::path spoolPath = finalPath.parent_path() / ".pending" / finalPath.filename();
		if (ValidateRunRecording(catalog, request, paths, selectedEngineIndex, selectedThreadIndex, repeatIndex, error,
		                         RunRecordingLocation_Spool) != ArenaStatus_Ok)
		{
			RemoveFailedRecordingFile(spoolPath, error);
			RemoveFailedRecordingFile(spoolPath.wstring() + L".partial", error);
			return error->code;
		}
		std::array<char, benchmark_visual::kVisualBridgeTextCapacity> engineText = {};
		std::copy(engineId.begin(), engineId.end(), engineText.begin());
		benchmark_visual::VisualScene expected = {};
		benchmark_replay::SetRecordingSceneIdentity(request->configuration.execution, engineText.data(), threadCount,
		                                            repeatIndex, &expected);
		ReplayCompressionResult sizes = {};
		control->compressingReplay.store(1, std::memory_order_release);
		const ArenaStatus finalStatus = FinalizeReplayCompression(spoolPath, finalPath, &expected.identity,
		                                                          &control->cancellationRequested, &sizes, error);
		control->compressingReplay.store(0, std::memory_order_release);
		InvocationEvent compressionEvent = {};
		StatusRecord logError = {};
		const std::string detail = "raw_bytes=" + std::to_string(sizes.rawBytes) +
		                           " compressed_bytes=" + std::to_string(sizes.compressedBytes) +
		                           " temporary_bytes=" + std::to_string(sizes.retainedTemporaryBytes) + " file=" +
		                           (sizes.retainedTemporaryBytes != 0 ? spoolPath : finalPath).generic_string();
		if (AppendInvocationEvent(invocation, "recording_compression",
		                          finalStatus != ArenaStatus_Ok       ? "failed"
		                          : sizes.retainedTemporaryBytes != 0 ? "cleanup_required"
		                                                              : "ok",
		                          detail.c_str(), &compressionEvent, &logError) != ArenaStatus_Ok &&
		    finalStatus == ArenaStatus_Ok)
		{
			*error = logError;
			return logError.code;
		}
		if (finalStatus != ArenaStatus_Ok)
			return finalStatus;
	}
	return ArenaStatus_Ok;
}

ArenaStatus RunOneProcess(const wchar_t* repositoryRoot, const Catalog* catalog, const ReleaseCatalog* releaseCatalog,
                          const PreparedRunRequest* request, const RunPathRecord* paths,
                          std::uint32_t selectedEngineIndex, std::uint32_t selectedThreadIndex,
                          std::uint32_t repeatIndex, RunExecutionControl* control, InvocationContext* invocation,
                          RunExecutionResult* result, StatusRecord* error, RayRunStage rayStage, RunProcessOutcome* outcome)
{
	RunProcessOutcome localOutcome = {};
	if (outcome == nullptr)
		outcome = &localOutcome;
	*outcome = {};
	outcome->disposition = RunProcessDisposition_HarnessFailure;
	StackVerificationChannel channel = {};
	const CaseExecutionSpec execution = ResolveEngineCaseExecution(&request->configuration, selectedEngineIndex);
	const std::string_view engineId = CatalogTextView(catalog, catalog->engines[request->engineIndexes[selectedEngineIndex]].id);
	const int verifiedStack = rayStage == RayRunStage_Heavy && request->verificationMode == VerificationMode_On && benchmark_stack::TargetFixture(execution.fixtureKind) != 0;
	if (verifiedStack != 0 && OpenStackVerificationChannel(execution, std::string_view(paths->runId.data(), paths->runIdSize),
	    engineId, request->threadCounts[selectedThreadIndex], repeatIndex, &channel, error) != ArenaStatus_Ok) return error->code;
	ProcessSpec spec = {};
	if (RenderRunProcessSpec(repositoryRoot, paths->resultDirectory.data(), catalog, releaseCatalog, request,
	                         selectedEngineIndex, selectedThreadIndex, repeatIndex, channel.endpoint, &spec, error, rayStage) != ArenaStatus_Ok)
	{
		CloseStackVerificationChannel(&channel);
		return error->code;
	}
	ProcessRecord process = {};
	if (StartProcess(&spec, &process, error) != ArenaStatus_Ok)
	{
		CloseStackVerificationChannel(&channel);
		return error->code;
	}
	result->startedProcessCount += 1;
	if (rayStage == RayRunStage_Heavy)
		control->startedProcessCount.store(result->startedProcessCount, std::memory_order_relaxed);
	const EngineRecord& engine = catalog->engines[request->engineIndexes[selectedEngineIndex]];
	const std::uint32_t threadCount = request->threadCounts[selectedThreadIndex];
	HANDLE stdoutLog = nullptr;
	outcome->completed.tuple = {request->engineIndexes[selectedEngineIndex], threadCount, repeatIndex};
	HANDLE stderrLog = nullptr;
	ArenaStatus finalStatus = ArenaStatus_Ok;
	for (;;)
	{
		if (control->cancellationRequested.load(std::memory_order_acquire) != 0 &&
		    process.cancellationRequested == PresenceStatus_Absent)
		{
			if (RequestProcessCancellation(&process, error) != ArenaStatus_Ok)
			{
				finalStatus = error->code;
				break;
			}
		}
		ProcessPollRecord poll = {};
		if (PollProcess(&process, &poll, error) != ArenaStatus_Ok)
		{
			finalStatus = error->code;
			break;
		}
		if (WriteProcessStream(invocation, catalog, engine, threadCount, repeatIndex,
		                       rayStage == RayRunStage_Preflight ? L"preflight.stdout" : L"stdout", poll.output.data(),
		                       poll.outputSize, &stdoutLog, error) != ArenaStatus_Ok ||
		    WriteProcessStream(invocation, catalog, engine, threadCount, repeatIndex,
		                       rayStage == RayRunStage_Preflight ? L"preflight.stderr" : L"stderr",
		                       poll.errorOutput.data(), poll.errorOutputSize, &stderrLog, error) != ArenaStatus_Ok)
		{
			finalStatus = error->code;
			break;
		}
		if (poll.state == ProcessState_Exited)
		{
			result->exitCode = poll.exitCode;
			outcome->exitCodePresence = PresenceStatus_Present;
			outcome->exitCode = poll.exitCode;
			if (poll.cleanupStatus != ProcessCleanupStatus_Empty)
				finalStatus = RunError(error, ArenaStatus_RunFailed, "process_cleanup_failed");
			else if (control->cancellationRequested.load(std::memory_order_acquire) != 0)
			{
				outcome->disposition = RunProcessDisposition_Interrupted;
				finalStatus = RunError(error, ArenaStatus_Interrupted, "process_interrupted");
			}
			else if (poll.exitCode != 0)
			{
				outcome->disposition = RunProcessDisposition_EngineFailure;
				outcome->reason = process.termination == ProcessTermination_Timeout
				                      ? ExecutionFailureReason_ProcessTimeout : ExecutionFailureReason_ProcessExit;
				std::array<char, 64> detail = {};
				const int written = std::snprintf(detail.data(), detail.size(), "process_exit_code=%d", poll.exitCode);
				finalStatus = RunError(error, ArenaStatus_RunFailed,
				                       written > 0 && written < static_cast<int>(detail.size())
				                           ? std::string_view(detail.data(), static_cast<std::size_t>(written))
				                           : std::string_view("process_exit_code"));
			}
			else
				outcome->disposition = RunProcessDisposition_Completed;
			break;
		}
		Sleep(10);
	}
	if (stdoutLog != nullptr)
		CloseHandle(stdoutLog);
	if (stderrLog != nullptr)
		CloseHandle(stderrLog);
	if (request->configuration.execution.fixtureKind == CaseFixtureKind_RayTracing && process.processHandle != nullptr)
	{
		StatusRecord memoryError = {};
		const ArenaStatus memoryStatus = WriteRayProcessMemory(spec.workingDirectory.data(), CatalogTextView(catalog, engine.id), threadCount, repeatIndex, process.processHandle, &memoryError);
		if (memoryStatus != ArenaStatus_Ok)
		{
			finalStatus = memoryStatus;
			*error = memoryError;
			outcome->disposition = RunProcessDisposition_HarnessFailure;
		}
	}
	const ProcessCleanupStatus cleanup = DestroyProcess(&process);
	CloseStackVerificationChannel(&channel);
	if (cleanup != ProcessCleanupStatus_Empty)
	{
		outcome->disposition = RunProcessDisposition_HarnessFailure;
		return RunError(error, ArenaStatus_RunFailed, "process_cleanup_failed");
	}
	if (verifiedStack != 0)
	{
		const StackStabilityResult& stability = channel.assessment.result;
		StatusRecord stabilityError = {};
		if ((finalStatus != ArenaStatus_Interrupted || channel.assessment.frames != 0) &&
		    CommitStackStability(std::filesystem::path(paths->resultDirectory.data()) / "stability.csv", stability, &stabilityError) != ArenaStatus_Ok)
		{
			*error = stabilityError;
			outcome->disposition = RunProcessDisposition_HarnessFailure;
			return error->code;
		}

		if (finalStatus == ArenaStatus_Ok && stability.coverage != StackCoverage_Complete)
		{
			outcome->disposition = RunProcessDisposition_HarnessFailure;
			return RunError(error, ArenaStatus_InvalidResult, "stability_capture_incomplete " + stability.reason);
		}
		if (StackQualificationAssessment(stability, CurrentStackCriterion(request->configuration.execution.fixtureKind)) == StackAssessment_Fail)
		{
			outcome->completed.cause = RepeatFailureCause_StackAssessment;
			outcome->completed.detail = stability.reason;
		}
	}
	if (RecordingForThread(request->recordingThreads, request->threadCounts[selectedThreadIndex]) == RecordingMode_Off)
		return finalStatus;
	if (finalStatus != ArenaStatus_Ok)
	{
		if (request->recordingKind != RecordingKind_NativeRayHits)
			CleanupFailedRunRecording(catalog, request, paths, selectedEngineIndex, selectedThreadIndex, repeatIndex, error);
		return finalStatus;
	}
	if (request->recordingKind == RecordingKind_NativeRayHits || request->recordingVersion == kCompressedReplayVersion)
		finalStatus = CompressRunRecording(catalog, request, paths, selectedEngineIndex, selectedThreadIndex,
		                                   repeatIndex, control, invocation, error);
	if (finalStatus == ArenaStatus_Ok)
		finalStatus = ValidateRunRecording(catalog, request, paths, selectedEngineIndex, selectedThreadIndex, repeatIndex, error);
	if (finalStatus != ArenaStatus_Ok)
	{
		outcome->reason = ExecutionFailureReason_Recording;
		outcome->disposition = finalStatus == ArenaStatus_Interrupted ? RunProcessDisposition_Interrupted
		                       : finalStatus == ArenaStatus_InvalidResult ? RunProcessDisposition_EngineFailure
		                                                                  : RunProcessDisposition_HarnessFailure;
	}
	return finalStatus;
}

ArenaStatus EnsureUnitDirectories(const Catalog* catalog, const PreparedRunRequest* request, const RunPathRecord* paths,
                                  std::uint32_t selectedEngineIndex, std::uint32_t selectedThreadIndex,
                                  std::array<wchar_t, kRunPathCapacity>* threadDirectory, StatusRecord* error)
{
	const EngineRecord& engine = catalog->engines[request->engineIndexes[selectedEngineIndex]];
	std::array<wchar_t, kRunPathCapacity> engineDirectory = {};
	if (BuildRunChildPathUtf8(paths->rawDirectory.data(), CatalogTextView(catalog, engine.id), &engineDirectory) !=
	        ArenaStatus_Ok ||
	    EnsureDirectory(engineDirectory.data()) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_RunFailed, "engine_raw_directory");
	std::array<wchar_t, 32> threadName = {};
	std::swprintf(threadName.data(), threadName.size(), L"t%u", request->threadCounts[selectedThreadIndex]);
	if (BuildRunChildPath(engineDirectory.data(), threadName.data(), threadDirectory) != ArenaStatus_Ok ||
	    EnsureDirectory(threadDirectory->data()) != ArenaStatus_Ok)
		return RunError(error, ArenaStatus_RunFailed, "thread_raw_directory");
	return ArenaStatus_Ok;
}

ArenaStatus ValidateRawOutput(const wchar_t* repositoryRoot, const Catalog* catalog, const ReleaseCatalog* release,
                              const PreparedRunRequest* request, const RunPathRecord* paths,
                              std::uint32_t selectedEngineIndex, std::uint32_t selectedThreadIndex, std::uint32_t repeat,
                              const wchar_t* threadDirectory, std::span<const ExecutionFailure> failures,
                              RunProcessDisposition* disposition, CompletedRepeatEvidence* completed, StatusRecord* error)
{
	*disposition = RunProcessDisposition_HarnessFailure;
	const std::uint32_t engineIndex = request->engineIndexes[selectedEngineIndex];
	const std::uint32_t threads = request->threadCounts[selectedThreadIndex];
	const std::string engine(CatalogTextView(catalog, catalog->engines[engineIndex].id));
	const std::string prefix = engine + "_t" + std::to_string(threads);
	const std::filesystem::path directory(threadDirectory);
	if (!std::filesystem::exists(directory / (prefix + "_raw.csv")) ||
	    !std::filesystem::exists(directory / (prefix + "_observations.csv")))
	{
		*disposition = RunProcessDisposition_EngineFailure;
		return RunError(error, ArenaStatus_InvalidResult, "tuple_output_file_missing");
	}
	CsvWriter sink = {};
	if (OpenCsvWriter(L"NUL", &sink, error) != ArenaStatus_Ok)
		return error->code;
	RawContext raw = {};
	raw.repositoryRoot = repositoryRoot;
	raw.catalog = catalog;
	raw.releaseCatalog = release;
	raw.request = request;
	raw.paths = paths;
	raw.configuration = &request->configuration;
	const CaseRecord observationCase = RunObservationCase(request->configuration.benchmarkCase, request->verificationMode);
	raw.benchmarkCase = &observationCase;
	raw.engine = &catalog->engines[engineIndex];
	raw.artifact = &release->artifacts[request->artifactIndexes[selectedEngineIndex]];
	raw.engineIndex = engineIndex;
	raw.threadCount = threads;
	raw.failures = failures;
	raw.normalizedWriter = &sink;
	CsvHeader header = {};
	CsvReadRecord read = {};
	ArenaStatus status = ReadCsvFile((directory / (prefix + "_raw.csv")).c_str(), &header, ConsumeRawRow, &raw, &read, error);
	if (raw.rejectedRepeat == repeat)
		*disposition = RunProcessDisposition_EngineFailure;
	if (status == ArenaStatus_Ok && raw.accumulator.seen[repeat] != PresenceStatus_Present)
	{
		*disposition = RunProcessDisposition_EngineFailure;
		status = RunError(error, ArenaStatus_InvalidResult, "raw_output_repeat_missing");
	}
	ObservationUnitContext observations = {};
	observations.catalog = catalog;
	observations.configuration = &request->configuration;
	observations.benchmarkCase = raw.benchmarkCase;
	observations.paths = paths;
	observations.engineIndex = engineIndex;
	observations.engineId = engine;
	observations.threadCount = threads;
	observations.repeatCount = request->repeatCount;
	observations.benchmarkMode = RecordingForThread(request->recordingThreads, threads) == RecordingMode_On ? "recorded_api" : "headless_api";
	observations.failures = failures;
	observations.rootWriter = &sink;
	std::uint32_t rowsPerRepeat = 0;
	for (std::uint32_t index = 0; index < raw.benchmarkCase->observationCount; ++index)
		rowsPerRepeat += request->configuration.observations[index].sampleIndexCount;
	observations.expectedRowCount = rowsPerRepeat * request->repeatCount;
	if (status == ArenaStatus_Ok)
	{
		status = ReadCsvFile((directory / (prefix + "_observations.csv")).c_str(), &header, ConsumeObservationRow, &observations, &read, error);
		if (status == ArenaStatus_Ok)
		{
			if (header.fieldCount != kObservationSidecarColumns.size())
				status = RunError(error, ArenaStatus_InvalidResult, "observation_sidecar_header");
			for (std::uint32_t column = 0; status == ArenaStatus_Ok && column < header.fieldCount; ++column)
				if (CsvHeaderTextView(&header, header.fields[column]) != kObservationSidecarColumns[column])
					status = RunError(error, ArenaStatus_InvalidResult, "observation_sidecar_header");
		}
		if (observations.rejectedRepeat == repeat)
			*disposition = RunProcessDisposition_EngineFailure;
	}
	DestroyCsvWriter(&sink);
	if (status == ArenaStatus_Ok && observations.repeatRows[repeat] != rowsPerRepeat)
	{
		*disposition = RunProcessDisposition_EngineFailure;
		status = RunError(error, ArenaStatus_InvalidResult, "observation_output_repeat_incomplete");
	}
	if (status == ArenaStatus_Ok && completed->cause == RepeatFailureCause_None)
		completed->cause = raw.accumulator.repeatFailures[repeat] != RepeatFailureCause_None
		    ? raw.accumulator.repeatFailures[repeat] : observations.repeatFailures[repeat];
	if (status == ArenaStatus_Ok && request->verificationMode == VerificationMode_Off &&
	    raw.benchmarkCase->fixtureKind != CaseFixtureKind_RagdollStairTumble)
	{
		*disposition = RunProcessDisposition_EngineFailure;
		const std::filesystem::path timing = directory / (prefix + "_r" + std::to_string(repeat) + "_step-timing.csv");
		status = ValidateRunTimingFile(timing.c_str(), request->configuration.execution.measuredWorkUnitCount,
		    raw.accumulator.workloadElapsedMilliseconds[repeat], error);
	}
	if (status != ArenaStatus_Ok)
		return status;
	if (raw.benchmarkCase->fixtureKind == CaseFixtureKind_RayTracing)
	{
		const std::string tuple = prefix + "_r" + std::to_string(repeat);
		*disposition = RunProcessDisposition_EngineFailure;
		std::vector<RayResultRow> rows;
		std::vector<RayProbeRow> probes;
		std::vector<RayProcessMemory> memory;
		if (ReadRayTracingResults(directory / (tuple + "_ray-tracing.csv"), &rows, error) != ArenaStatus_Ok ||
		    ValidateRayTracingTuple(rows, engine, threads, repeat, request->configuration.execution, error) != ArenaStatus_Ok ||
		    ValidateRaySecondaryWorkload(rows, std::filesystem::path(paths->resultDirectory.data()) / "ray-corpus", request->configuration.execution.rayTracing.viewCount, error) != ArenaStatus_Ok ||
		    ReadRayCapabilities(directory / (tuple + "_ray-capabilities.csv"), &probes, error) != ArenaStatus_Ok ||
		    ReadRayProcessMemory(directory / (tuple + "_ray-process.csv"), &memory, error) != ArenaStatus_Ok ||
		    ValidateRayAuxiliaryTuple(probes, memory, engine, threads, repeat, error) != ArenaStatus_Ok)
			return error->code;
		for (const RayResultRow& row : rows)
			if (row.errors != 0 || row.capability == RayCapability_Failed)
				completed->cause = RepeatFailureCause_RayNumerical;
	}
	if (request->verificationMode == VerificationMode_Off && completed->cause != RepeatFailureCause_None)
	{
		*disposition = RunProcessDisposition_EngineFailure;
		return RunError(error, ArenaStatus_InvalidResult, std::string("verification_off_integrity ") + RepeatFailureCauseName(completed->cause));
	}
	return ArenaStatus_Ok;
}

ArenaStatus WritePreparedRunManifest(const Catalog* catalog, const ReleaseCatalog* releaseCatalog,
                                     const HostRecord* host, const PreparedRunRequest* request,
                                     const RunPathRecord* paths, StatusRecord* error)
{
	if (catalog == nullptr || releaseCatalog == nullptr || host == nullptr || request == nullptr || paths == nullptr ||
	    error == nullptr)
		return error != nullptr ? RunError(error, ArenaStatus_InvalidArgument, "manifest_arguments")
		                        : ArenaStatus_InvalidArgument;
	return WriteRunManifest(catalog, releaseCatalog, host, request, paths, error);
}

ArenaStatus ExecuteHeadlessRequest(const wchar_t* repositoryRoot, const Catalog* catalog,
                                   const ReleaseCatalog* releaseCatalog, const HostRecord* host,
                                   const PreparedRunRequest* request, const RunPathRecord* paths,
                                   RunExecutionControl* control, InvocationContext* invocation,
                                   EventTransport* transport, RunExecutionResult* result, StatusRecord* error)
{
	if (result != nullptr)
		*result = {};
	if (error != nullptr)
		*error = {};
	if (repositoryRoot == nullptr || catalog == nullptr || releaseCatalog == nullptr || host == nullptr ||
	    request == nullptr || paths == nullptr || control == nullptr || invocation == nullptr || transport == nullptr ||
	    result == nullptr || error == nullptr)
		return error != nullptr ? RunError(error, ArenaStatus_InvalidArgument, "invalid_execute_arguments")
		                        : ArenaStatus_InvalidArgument;
	if (request->totalUnitCount == 0 || request->totalProcessCount == 0 ||
	    invocation->status != AvailabilityStatus_Available)
		return RunError(error, ArenaStatus_InvalidArgument, "unprepared_or_inactive_execution");
	if (control->cancellationRequested.load(std::memory_order_acquire) != 0)
	{
		result->status = ArenaStatus_Interrupted;
		result->exitCode = 130;
		return RunError(error, ArenaStatus_Interrupted, "run_cancelled_before_launch");
	}
	if (WriteRunManifest(catalog, releaseCatalog, host, request, paths, error) != ArenaStatus_Ok)
	{
		result->status = error->code;
		return error->code;
	}
	if (EmitRunEvent(invocation, transport, "run_manifest", "ok", "manifest.json", error) != ArenaStatus_Ok)
	{
		result->status = error->code;
		return error->code;
	}
	if (request->configuration.execution.fixtureKind == CaseFixtureKind_RayTracing)
	{
		if (RunRayPreflight(repositoryRoot, catalog, releaseCatalog, request,
		                    std::filesystem::path(paths->resultDirectory.data()) / "preflight", control, invocation,
		                    transport, error, &result->failures, paths) != ArenaStatus_Ok)
		{
			result->status = error->code;
			result->exitCode = error->code == ArenaStatus_Interrupted ? 130 : 2;
			return error->code;
		}
		if (EmitRunEvent(invocation, transport, "ray_corpus", "running", "geometry_and_independent_reference", error) != ArenaStatus_Ok)
			return error->code;
		struct CorpusProgressContext
		{
			InvocationContext* invocation;
			EventTransport* transport;
			StatusRecord* error;
		};
		CorpusProgressContext progressContext = {invocation, transport, error};
		const RayCorpusProgress progress = [](void* state, std::uint32_t view, benchmark_ray::Phase phase) -> int
		{
			CorpusProgressContext* context = static_cast<CorpusProgressContext*>(state);
			std::array<char, 128> detail = {};
			std::snprintf(detail.data(), detail.size(), "view=%u phase=%s geometry_and_reference_outside_timing", view, benchmark_ray::PhaseName(phase));
			return EmitRunEvent(context->invocation, context->transport, "ray_corpus", "running", detail.data(), context->error) != ArenaStatus_Ok;
		};
		RayCorpusAcquisition acquisition = RayCorpusAcquisition_Generated;
		std::filesystem::path source;
		const benchmark_ray::Status corpusStatus = AcquireHeavyRayCorpus(repositoryRoot,
		    std::filesystem::path(paths->resultDirectory.data()) / "ray-corpus", request->configuration.execution.rayTracing, &control->cancellationRequested,
		    progress, &progressContext, &acquisition, &source);
		if (corpusStatus != benchmark_ray::Status_Ok)
			return RunError(error, corpusStatus == benchmark_ray::Status_Interrupted ? ArenaStatus_Interrupted : ArenaStatus_RunFailed, "ray_corpus_preparation_failed_or_cancelled");
		const std::string detail = std::string(acquisition == RayCorpusAcquisition_Reused ? "reused=" : "generated=") + source.generic_string();
		if (EmitRunEvent(invocation, transport, "ray_corpus", "ok", detail.c_str(), error) != ArenaStatus_Ok)
			return error->code;
	}
	if (paths->requiredCaptureBytes != 0)
	{
		const std::string detail = "required_bytes=" + std::to_string(paths->requiredCaptureBytes);
		if (EmitRunEvent(invocation, transport, "capture_space", "ok", detail.c_str(), error) != ArenaStatus_Ok)
			return error->code;
	}
	for (std::uint32_t engineSelection = 0; engineSelection < request->engineCount; ++engineSelection)
	{
		const EngineRecord& engine = catalog->engines[request->engineIndexes[engineSelection]];
		for (std::uint32_t threadSelection = 0; threadSelection < request->threadCount; ++threadSelection)
		{
			if (control->cancellationRequested.load(std::memory_order_acquire) != 0)
			{
				result->status = ArenaStatus_Interrupted;
				result->exitCode = 130;
				RunError(error, ArenaStatus_Interrupted, "run_cancelled_between_units");
				const StatusRecord retainedError = *error;
				StatusRecord eventError = {};
				EmitRunEvent(invocation, transport, "run_gate", "interrupted", "partial_result_retained", &eventError);
				*error = retainedError;
				return ArenaStatus_Interrupted;
			}
			std::array<wchar_t, kRunPathCapacity> threadDirectory = {};
			if (EnsureUnitDirectories(catalog, request, paths, engineSelection, threadSelection, &threadDirectory,
			                          error) != ArenaStatus_Ok)
			{
				result->failedUnitCount += 1;
				control->failedUnitCount.store(result->failedUnitCount, std::memory_order_relaxed);
				result->status = error->code;
				return error->code;
			}
			std::array<char, kDetailCapacity> detail = {};
			const std::string_view engineId = CatalogTextView(catalog, engine.id);
			std::snprintf(detail.data(), detail.size(), "unit=%u/%u engine=%.*s thread=%u",
			              result->completedUnitCount + result->failedUnitCount + 1, request->totalUnitCount,
			              static_cast<int>(engineId.size()), engineId.data(), request->threadCounts[threadSelection]);
			if (EmitRunEvent(invocation, transport, "run_progress", "running", detail.data(), error) != ArenaStatus_Ok)
			{
				result->failedUnitCount += 1;
				control->failedUnitCount.store(result->failedUnitCount, std::memory_order_relaxed);
				result->status = error->code;
				return error->code;
			}
			std::uint32_t failedRepeats = 0;
			for (std::uint32_t repeatIndex = 0; repeatIndex < request->repeatCount; ++repeatIndex)
			{
				if (FindExecutionFailure(result->failures, request->engineIndexes[engineSelection],
				                         request->threadCounts[threadSelection], repeatIndex) != nullptr)
				{
					++failedRepeats;
					continue;
				}
				if (control->cancellationRequested.load(std::memory_order_acquire) != 0)
				{
					result->status = ArenaStatus_Interrupted;
					result->exitCode = 130;
					return RunError(error, ArenaStatus_Interrupted, "run_cancelled_between_repeats");
				}
				RunProcessOutcome process = {};
				control->currentEngineIndex.store(request->engineIndexes[engineSelection], std::memory_order_relaxed);
				control->currentThreadCount.store(request->threadCounts[threadSelection], std::memory_order_relaxed);
				control->currentRepeatIndex.store(repeatIndex, std::memory_order_relaxed);
				ArenaStatus processStatus = RunOneProcess(repositoryRoot, catalog, releaseCatalog, request, paths,
				                                          engineSelection, threadSelection, repeatIndex, control,
				                                          invocation, result, error, RayRunStage_Heavy, &process);
				RunProcessDisposition outputDisposition = RunProcessDisposition_HarnessFailure;
				if (process.disposition == RunProcessDisposition_Completed &&
				    ValidateRawOutput(repositoryRoot, catalog, releaseCatalog, request, paths, engineSelection, threadSelection, repeatIndex, threadDirectory.data(), result->failures, &outputDisposition, &process.completed, error) != ArenaStatus_Ok)
				{
					process.disposition = outputDisposition;
					process.reason = ExecutionFailureReason_InvalidOutput;
					processStatus = error->code;
				}
				if (process.disposition == RunProcessDisposition_EngineFailure)
				{
					++failedRepeats;
					const ExecutionFailure failure = {request->engineIndexes[engineSelection], request->threadCounts[threadSelection],
					                                  repeatIndex, ExecutionStage_Benchmark, ExecutionOutcome_Failed,
					                                  process.reason, process.exitCodePresence, process.exitCode, error->detail};
					result->failures.push_back(failure);
					if (EmitRunEvent(invocation, transport, "engine_outcome", "failed", failure.detail.data(), error) != ArenaStatus_Ok)
						return error->code;
					process.completed.cause = RepeatFailureCause_Execution;
					process.completed.detail = failure.detail.data();
					++result->executionFailedRepeatCount;
					*error = {};
				}
				else if (processStatus != ArenaStatus_Ok)
				{
					result->status = processStatus;
					return processStatus;
				}
				else if (process.completed.cause != RepeatFailureCause_None)
				{
					++failedRepeats;
					++result->qualityFailedRepeatCount;
				}
				else if (request->verificationMode == VerificationMode_Off)
					++result->unverifiedRepeatCount;
				else
					++result->passedRepeatCount;
				if (process.completed.cause != RepeatFailureCause_None)
				{
					std::vector<RunRepeatTuple> remaining;
					remaining.reserve((request->threadCount - threadSelection) * request->repeatCount);
					for (std::uint32_t thread = threadSelection; thread < request->threadCount; ++thread)
						for (std::uint32_t repeat = thread == threadSelection ? repeatIndex + 1 : 0; repeat < request->repeatCount; ++repeat)
							remaining.push_back({request->engineIndexes[engineSelection], request->threadCounts[thread], repeat});
					const std::size_t priorCount = result->failures.size();
					SuppressRemainingRepeatTuples(catalog, process.completed, remaining, &result->failures);
					result->skippedRepeatCount += static_cast<std::uint32_t>(result->failures.size() - priorCount);
					if (PersistExecutionFailures(catalog, paths, result->failures, error) != ArenaStatus_Ok)
						return error->code;
					std::snprintf(detail.data(), detail.size(), "engine=%.*s thread=%u repeat=%u reason=%s skipped=%u",
					    static_cast<int>(engineId.size()), engineId.data(), request->threadCounts[threadSelection], repeatIndex,
					    RepeatFailureCauseName(process.completed.cause), result->skippedRepeatCount);
					if (EmitRunEvent(invocation, transport, "repeat_outcome", "failed", detail.data(), error) != ArenaStatus_Ok)
						return error->code;
				}
				control->passedRepeatCount.store(result->passedRepeatCount, std::memory_order_relaxed);
				control->unverifiedRepeatCount.store(result->unverifiedRepeatCount, std::memory_order_relaxed);
				control->qualityFailedRepeatCount.store(result->qualityFailedRepeatCount, std::memory_order_relaxed);
				control->executionFailedRepeatCount.store(result->executionFailedRepeatCount, std::memory_order_relaxed);
				control->skippedRepeatCount.store(result->skippedRepeatCount, std::memory_order_relaxed);
			}
			if (failedRepeats != 0)
				++result->failedUnitCount;
			else
				++result->completedUnitCount;
			control->failedUnitCount.store(result->failedUnitCount, std::memory_order_relaxed);
			control->completedUnitCount.store(result->completedUnitCount, std::memory_order_relaxed);
			std::snprintf(detail.data(), detail.size(), "unit=%u/%u engine=%.*s thread=%u raw=%s",
			              result->completedUnitCount + result->failedUnitCount, request->totalUnitCount, static_cast<int>(engineId.size()),
			              engineId.data(), request->threadCounts[threadSelection], failedRepeats == 0 ? "valid" : "partial");
			if (EmitRunEvent(invocation, transport, "run_progress", "ok", detail.data(), error) != ArenaStatus_Ok)
			{
				result->status = error->code;
				return error->code;
			}
		}
	}
	if (result->completedUnitCount + result->failedUnitCount != request->totalUnitCount)
	{
		result->status = ArenaStatus_InvalidResult;
		return RunError(error, ArenaStatus_InvalidResult, "run_matrix_incomplete");
	}
	result->exitCode = 0;
	result->status = ArenaStatus_Ok;
	if (EmitRunEvent(invocation, transport, "run_gate", "ok", result->failures.empty() && result->qualityFailedRepeatCount == 0 ? "raw_matrix_complete" : "completed_with_failures", error) != ArenaStatus_Ok)
	{
		result->status = error->code;
		return error->code;
	}
	return ArenaStatus_Ok;
}
}
