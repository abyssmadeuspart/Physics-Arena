#include "physics_arena/stack_stability_reassessment.h"
#include "physics_arena/report_pipeline.h"
#include "physics_arena/replay.h"
#include "json_contracts_internal.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <fstream>
#include <memory>

namespace physics_arena
{
ArenaStatus ReassessmentError(StatusRecord* error, std::string_view detail)
{
	*error = {};
	error->code = ArenaStatus_InvalidResult;
	constexpr std::string_view component = "stack_stability_reassessment", status = "invalid_result";
	std::copy(component.begin(), component.end(), error->component.begin());
	error->componentSize = static_cast<std::uint32_t>(component.size());
	std::copy(status.begin(), status.end(), error->status.begin());
	error->statusSize = static_cast<std::uint32_t>(status.size());
	error->detailSize = static_cast<std::uint32_t>(std::min(detail.size(), error->detail.size() - 1));
	std::copy_n(detail.begin(), error->detailSize, error->detail.begin());
	return error->code;
}

ArenaStatus AdmitReassessmentTrace(const std::filesystem::path& trace)
{
	const DWORD attributes = GetFileAttributesW(trace.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND
		    ? ArenaStatus_ToolMissing : ArenaStatus_InvalidResult;
	return (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0
	    ? ArenaStatus_Ok : ArenaStatus_InvalidResult;
}

ArenaStatus ProposeStackReassessment(const std::filesystem::path& directory, const CaseExecutionSpec& execution,
                                    const Catalog* catalog, const ResultManifestRecord& manifest, const StackStabilityResult& saved,
                                    StackReassessmentTuple* tuple, StatusRecord* error)
{
	tuple->proposed = saved;
	std::uint32_t engine = 0;
	while (engine < manifest.engineCount && CatalogTextView(catalog, catalog->engines[manifest.engines[engine].engineIndex].id) != saved.engineId)
		++engine;
	const ExecutionFailure* terminal = FindExecutionFailure(manifest.executionFailures,
	    manifest.engines[engine].engineIndex, saved.threadCount, saved.repeatIndex);
	if (terminal != nullptr && terminal->outcome == ExecutionOutcome_Failed)
	{
		tuple->source = StackReassessmentSource_ExecutionFailure;
		tuple->proposed.criterion = CurrentStackCriterion(execution.fixtureKind);
		tuple->proposed.margin = execution.fixtureKind == CaseFixtureKind_BoxContactIslands ?
		    0.20 * std::min({execution.selectedGeometry.halfExtents.x, execution.selectedGeometry.halfExtents.y, execution.selectedGeometry.halfExtents.z}) :
		    tuple->proposed.criterion == StackCriterion_PyramidUnforcedShape ? 0.10 : 0;
		tuple->proposed.assessment = StackAssessment_Unassessed;
		tuple->proposed.firstRule = StackRule_None;
		return ArenaStatus_Ok;
	}
	const std::filesystem::path raw = directory / "raw" / saved.engineId / ("t" + std::to_string(saved.threadCount)) /
	    (saved.engineId + "_t" + std::to_string(saved.threadCount) + "_raw.csv");
	const std::filesystem::path trace = benchmark_stack::TracePath(raw, saved.repeatIndex);
	const ArenaStatus traceStatus = AdmitReassessmentTrace(trace);
	tuple->source = StackReassessmentSource_Unavailable;
	if (traceStatus == ArenaStatus_Ok)
	{
		if (AnalyzeStackTrace(trace, execution, saved.runId, saved.engineId, saved.threadCount, saved.repeatIndex,
		    &tuple->proposed, error) != ArenaStatus_Ok)
			return error->code;
		if (tuple->proposed.coverage != StackCoverage_Complete)
			return ReassessmentError(error, tuple->proposed.reason);
		tuple->source = StackReassessmentSource_RetainedTrace;
		return ArenaStatus_Ok;
	}
	if (traceStatus != ArenaStatus_ToolMissing)
		return ReassessmentError(error, "trace_invalid_or_reparse file=" + trace.generic_string());
	if (saved.criterion == CurrentStackCriterion(execution.fixtureKind) && saved.coverage == StackCoverage_Complete)
	{
		tuple->source = StackReassessmentSource_CurrentAssessment;
		return ArenaStatus_Ok;
	}
	if (execution.fixtureKind == CaseFixtureKind_OpenContainerFallingPile)
	{
		std::uint32_t thread = 0;
		while (thread < manifest.threadCount && manifest.threadCounts[thread] != saved.threadCount) ++thread;
		ReplayRecording recording = {};
		if (OpenResultReplay(catalog, &manifest, directory, engine, thread, saved.repeatIndex, &recording, error) != ArenaStatus_Ok)
			return error->code;
		const ArenaStatus status = AnalyzeContainerRecording(&recording, execution, saved.runId, saved.engineId, saved.threadCount,
		    saved.repeatIndex, &tuple->proposed, error);
		CloseReplay(&recording);
		if (status != ArenaStatus_Ok) return status;
		tuple->source = StackReassessmentSource_Recording;
		tuple->evidenceLimit = "initial_plus_all_measured_frames mandatory_capture_overhead=unavailable";
		return ArenaStatus_Ok;
	}
	if (execution.fixtureKind == CaseFixtureKind_BoxContactIslands)
	{
		tuple->proposed.criterion = StackCriterion_ContactIslandsShapePreservation;
		tuple->proposed.margin = 0.20 * std::min({execution.selectedGeometry.halfExtents.x, execution.selectedGeometry.halfExtents.y, execution.selectedGeometry.halfExtents.z});
		tuple->proposed.assessment = StackQualificationAssessment(saved, tuple->proposed.criterion) == StackAssessment_Fail ? StackAssessment_Fail : StackAssessment_Unassessed;
		if (tuple->proposed.assessment != StackAssessment_Fail) tuple->proposed.firstRule = StackRule_None;
		tuple->proposed.terminalMetricsPresence = PresenceStatus_Absent;
		tuple->proposed.reason = "terminal_pose_evidence_unavailable";
		std::uint32_t thread = 0;
		while (thread < manifest.threadCount && manifest.threadCounts[thread] != saved.threadCount) ++thread;
		const std::filesystem::path replayPath = ReplayTuplePath(directory, saved.engineId, saved.threadCount, saved.repeatIndex);
		const ArenaStatus replayState = AdmitReassessmentTrace(replayPath);
		if (replayState == ArenaStatus_ToolMissing)
		{
			tuple->evidenceLimit = "terminal_poses_unavailable stronger_pass_not_inferred";
			return ArenaStatus_Ok;
		}
		if (replayState != ArenaStatus_Ok) return ReassessmentError(error, "recording_invalid_or_reparse");
		ReplayRecording recording = {};
		if (OpenResultReplay(catalog, &manifest, directory, engine, thread, saved.repeatIndex, &recording, error) != ArenaStatus_Ok) return error->code;
		const ArenaStatus status = AnalyzeContactIslandsRecording(&recording, execution, saved.runId, saved.engineId, saved.threadCount, saved.repeatIndex, &tuple->proposed, error);
		CloseReplay(&recording);
		if (status != ArenaStatus_Ok) return status;
		tuple->proposed.captureElapsedMs = saved.captureElapsedMs;
		if (StackQualificationAssessment(saved, StackCriterion_ContactIslandsShapePreservation) == StackAssessment_Fail && tuple->proposed.assessment != StackAssessment_Fail)
		{
			tuple->proposed.assessment = StackAssessment_Fail;
			tuple->proposed.firstRule = saved.firstRule;
			tuple->proposed.firstBody = saved.firstBody;
			tuple->proposed.firstBreach = saved.firstBreach;
		}
		tuple->source = StackReassessmentSource_Recording;
		tuple->evidenceLimit = "initial_plus_all_measured_frames full_trajectory_safety=incomplete capture_overhead=retained_metadata stronger_pass_not_inferred";
		return ArenaStatus_Ok;
	}
	if (CertifyLegacyStackPass(execution, saved, &tuple->proposed, error) != ArenaStatus_Ok)
		return error->code;
	tuple->source = StackReassessmentSource_CertifiedLegacyPass;
	return ArenaStatus_Ok;
}

ArenaStatus ProjectObsoleteStackSkips(const std::filesystem::path& directory, const Catalog* catalog,
                                     const ResultManifestRecord& manifest, ResultViewModel* model,
                                     StackReassessmentRecord* record, StatusRecord* error)
{
	std::array<PresenceStatus, kEngineCapacity> failures = {};
	for (const ExecutionFailure& terminal : manifest.executionFailures)
		if (terminal.outcome == ExecutionOutcome_Failed)
			failures[terminal.engineIndex] = PresenceStatus_Present;
	model->stabilityResults.clear();
	model->stabilityResults.reserve(record->tuples.size());
	for (const StackReassessmentTuple& tuple : record->tuples)
		model->stabilityResults.push_back(tuple.proposed);
	for (std::uint32_t engine = 0; engine < manifest.engineCount; ++engine)
		for (std::uint32_t thread = 0; thread < manifest.threadCount; ++thread)
		{
			ResultRepeatProjection repeats = {};
			if (ProjectResultRepeats((directory / "normalized.csv").c_str(), catalog, &manifest, engine,
			    manifest.threadCounts[thread], &repeats, error, model) != ArenaStatus_Ok)
				return error->code;
			for (const ResultRepeatRow& repeat : std::span(repeats.rows).first(repeats.rowCount))
				if (repeat.measurement == PresenceStatus_Present && repeat.outcome != ObservationOutcome_Ok)
					failures[manifest.engines[engine].engineIndex] = PresenceStatus_Present;
		}
	for (const ExecutionFailure& terminal : manifest.executionFailures)
		if (terminal.reason == ExecutionFailureReason_PreviousRepeatFailed &&
		    failures[terminal.engineIndex] == PresenceStatus_Absent)
			record->obsoleteSkips.push_back(terminal);
	return ArenaStatus_Ok;
}

ArenaStatus PublishStackReassessment(const std::filesystem::path& directory, const Catalog* catalog,
                                    std::span<const StackStabilityResult> corrected, StatusRecord* error)
{
	const std::filesystem::path table = directory / "stability.csv", manifestPath = directory / "manifest.json";
	const std::filesystem::path tableCandidate = table.wstring() + L".partial";
	const std::filesystem::path manifestCandidate = manifestPath.wstring() + L".partial";
	OrderedJson document;
	if (LoadDocument(directory.c_str(), "manifest.json", &document, error) != ArenaStatus_Ok)
		return error->code;
	document["stability"]["criterion"] = StackCriterionName(corrected.front().criterion);
	document["stability"]["margin_policy"] = StackMarginPolicyName(corrected.front().criterion);
	if (WriteStackStability(tableCandidate, corrected, error) != ArenaStatus_Ok)
		return ReassessmentError(error, "candidate_write file=" + tableCandidate.generic_string() + " publication=none");
	{
		std::ofstream output(manifestCandidate, std::ios::binary | std::ios::trunc);
		output << document.dump(2) << '\n';
		output.close();
		if (!output)
			return ReassessmentError(error, "candidate_write file=" + manifestCandidate.generic_string() + " publication=none");
	}
	std::unique_ptr<ResultManifestRecord> manifest(new ResultManifestRecord{});
	std::vector<StackStabilityResult> admitted;
	if (LoadResultManifest(manifestCandidate.c_str(), catalog, manifest.get(), error) != ArenaStatus_Ok ||
	    LoadStackStability(tableCandidate, &admitted, error) != ArenaStatus_Ok)
		return error->code;
	for (const StackStabilityResult& tuple : admitted)
		if (tuple.criterion != manifest->stabilityCriterion)
			return ReassessmentError(error, "candidate_criterion_disagreement publication=none");
	HANDLE file = CreateFileW(manifestCandidate.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return ReassessmentError(error, "candidate_open file=" + manifestCandidate.generic_string() + " publication=none");
	const int flushed = FlushFileBuffers(file), closed = CloseHandle(file);
	if (flushed == 0 || closed == 0)
		return ReassessmentError(error, "candidate_flush file=" + manifestCandidate.generic_string() + " publication=none");
	if (MoveFileExW(tableCandidate.c_str(), table.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
		return ReassessmentError(error, "publication_failed file=" + table.generic_string() + " publication=none");
	if (MoveFileExW(manifestCandidate.c_str(), manifestPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
		return ReassessmentError(error, "publication_failed file=" + manifestPath.generic_string() + " publication=stability.csv_only");
	return ArenaStatus_Ok;
}

ArenaStatus ReassessSavedStackStability(const wchar_t* repositoryRoot, const wchar_t* resultDirectory,
                                      const Catalog* catalog, StackReassessmentMode mode,
                                      StackReassessmentRecord* record, StatusRecord* error)
{
	*record = {};
	std::unique_ptr<ResultManifestRecord> manifest(new ResultManifestRecord{});
	std::unique_ptr<ResultViewModel> model(new ResultViewModel{});
	if (LoadResultPresentation(repositoryRoot, resultDirectory, catalog, manifest.get(), model.get(), error) != ArenaStatus_Ok)
		return error->code;
	if (manifest->verificationMode == VerificationMode_Off)
	{
		if (mode == StackReassessmentMode_Apply)
			return ReassessmentError(error, "Verification Off: physical quality not checked publication=none");
		for (std::uint32_t engine = 0; engine < manifest->engineCount; ++engine)
			for (std::uint32_t thread = 0; thread < manifest->threadCount; ++thread)
				for (std::uint32_t repeat = 0; repeat < manifest->repeatCount; ++repeat)
				{
					StackReassessmentTuple tuple = {};
					tuple.source = StackReassessmentSource_Unavailable;
					tuple.evidenceLimit = "Verification Off: physical quality not checked";
					tuple.proposed.engineId = CatalogTextView(catalog, catalog->engines[manifest->engines[engine].engineIndex].id);
					tuple.proposed.threadCount = manifest->threadCounts[thread];
					tuple.proposed.repeatIndex = repeat;
					record->tuples.push_back(tuple);
				}
		return ArenaStatus_Ok;
	}
	const int container = manifest->configuration.execution.fixtureKind == CaseFixtureKind_OpenContainerFallingPile;
	if (manifest->schemaVersion != 8 || (manifest->stabilityPresence != PresenceStatus_Present && container == 0))
		return ReassessmentError(error, "required_schema8_stack_declaration_unavailable");
	if (container != 0 && manifest->stabilityPresence == PresenceStatus_Absent && mode != StackReassessmentMode_Inspect)
		return ReassessmentError(error, "historical_container_recording_inspect_only publication=none");
	std::vector<StackStabilityResult> savedResults = model->stabilityResults;
	if (container != 0 && manifest->stabilityPresence == PresenceStatus_Absent)
		for (std::uint32_t engine = 0; engine < manifest->engineCount; ++engine)
			for (std::uint32_t thread = 0; thread < manifest->threadCount; ++thread)
			{
				ResultRepeatProjection repeats = {};
				if (ProjectResultRepeats((std::filesystem::path(resultDirectory) / "normalized.csv").c_str(), catalog, manifest.get(), engine,
				    manifest->threadCounts[thread], &repeats, error, model.get()) != ArenaStatus_Ok)
					return error->code;
				for (const ResultRepeatRow& repeat : std::span(repeats.rows).first(repeats.rowCount))
					if (repeat.measurement == PresenceStatus_Present)
					{
						StackStabilityResult saved = {};
						saved.runId = ResultTextView(manifest.get(), manifest->runId);
						saved.engineId = CatalogTextView(catalog, catalog->engines[manifest->engines[engine].engineIndex].id);
						saved.threadCount = manifest->threadCounts[thread];
						saved.repeatIndex = repeat.repeatIndex;
						saved.criterion = StackCriterion_ContainerEscape;
						savedResults.push_back(std::move(saved));
					}
			}
	if (savedResults.empty()) return ReassessmentError(error, "measured_tuples_unavailable");
	record->tuples.reserve(savedResults.size());
	StatusRecord firstError = {};
	for (const StackStabilityResult& saved : savedResults)
	{
		StackReassessmentTuple tuple = {};
		StatusRecord tupleError = {};
		std::uint32_t engine = 0;
		while (engine < manifest->engineCount && CatalogTextView(catalog, catalog->engines[manifest->engines[engine].engineIndex].id) != saved.engineId)
			++engine;
		if (engine == manifest->engineCount)
			return ReassessmentError(error, "stability_engine_unavailable");
		if (ProposeStackReassessment(resultDirectory, ResolveEngineCaseExecution(&manifest->configuration, engine), catalog, *manifest, saved, &tuple, &tupleError) != ArenaStatus_Ok)
		{
			tuple.evidenceLimit.assign(tupleError.detail.data(), tupleError.detailSize);
			if (firstError.code == ArenaStatus_Ok)
				ReassessmentError(&firstError, "engine=" + saved.engineId + " thread=" + std::to_string(saved.threadCount) +
				    " repeat=" + std::to_string(saved.repeatIndex) + " evidence=" + tuple.evidenceLimit);
		}
		else
		{
			record->changedTupleCount += saved.criterion != tuple.proposed.criterion || saved.assessment != tuple.proposed.assessment ||
			    saved.reason != tuple.proposed.reason || saved.firstRule != tuple.proposed.firstRule;
			record->reassessedTupleCount += tuple.source == StackReassessmentSource_RetainedTrace;
		}
			record->tuples.push_back(std::move(tuple));
	}
	if (firstError.code != ArenaStatus_Ok)
	{
		*error = firstError;
		return error->code;
	}
	if (ProjectObsoleteStackSkips(resultDirectory, catalog, *manifest, model.get(), record, error) != ArenaStatus_Ok)
		return error->code;
	if (mode == StackReassessmentMode_Apply)
	{
		for (const StackReassessmentTuple& tuple : record->tuples)
			if (tuple.source == StackReassessmentSource_Recording)
				return ReassessmentError(error, "recording_only_evidence_inspect_only publication=none");
		return PublishStackReassessment(resultDirectory, catalog, model->stabilityResults, error);
	}
	return ArenaStatus_Ok;
}

const char* StackReassessmentSourceName(StackReassessmentSource source)
{
	constexpr std::array<const char*, 6> names = {"retained_trace", "certified_legacy_pass", "current_assessment", "execution_failure", "unavailable", "recording_measured_window"};
	return names[source];
}
}
