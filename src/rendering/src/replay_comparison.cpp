#include "benchmark_visual/native_replay_comparison.h"
#include "launcher_app_internal.h"
#include <algorithm>
#include <new>
#include <memory>

namespace benchmark_visual
{
using namespace physics_arena;

void ReleaseReplayPeer(NativeReplayPeer* peer)
{
	if (peer == nullptr)
		return;
	PhysicsSceneViewReleaseScene(peer->sceneResources);
	CloseReplay(&peer->recording);
	delete peer;
}

void ReleaseReplayComparison(PhysicsArenaApp* app)
{
	NativeReplayComparison* comparison = app->replay->comparison;
	if (comparison == nullptr)
		return;
	ReleaseReplayPeer(comparison->peer);
	delete comparison->scanManifest;
	delete comparison;
	app->replay->comparison = nullptr;
}

void CancelReplayComparisonPicker(PhysicsArenaApp* app)
{
	NativeReplayComparison& comparison = *app->replay->comparison;
	comparison.picker = ReplayComparisonPickerState_Closed;
	comparison.candidates.clear();
	delete comparison.scanManifest;
	comparison.scanManifest = nullptr;
}

void BeginReplayComparisonPicker(PhysicsArenaApp* app)
{
	if (ActiveAction(app->action) == PresenceStatus_Present || app->queue.state == NativeRunQueueState_Armed)
		return;
	PauseReplayView(app);
	if (app->replay->comparison == nullptr)
		app->replay->comparison = new (std::nothrow) NativeReplayComparison{};
	if (app->replay->comparison == nullptr)
	{
		ReplayError(&app->results.replayError, "comparison_storage_allocation_failed");
		return;
	}
	NativeReplayComparison& comparison = *app->replay->comparison;
	CancelReplayComparisonPicker(app);
	comparison.selectionError = {};
	comparison.scanManifest = new (std::nothrow) ResultManifestRecord{};
	if (comparison.scanManifest == nullptr)
	{
		ReplayError(&comparison.selectionError, "comparison_manifest_allocation_failed");
		comparison.picker = ReplayComparisonPickerState_Ready;
		return;
	}
	comparison.candidates.reserve(app->results.recordingRunCount);
	comparison.scanIndex = 0;
	comparison.runChoice = UINT32_MAX;
	comparison.tupleChoice = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
	comparison.picker = ReplayComparisonPickerState_Scanning;
}

void UpdateReplayComparisonPicker(PhysicsArenaApp* app)
{
	NativeReplayComparison& comparison = *app->replay->comparison;
	if (comparison.picker != ReplayComparisonPickerState_Scanning)
		return;
	if (comparison.scanIndex == app->results.recordingRunCount)
	{
		for (std::uint32_t run = 0; run < comparison.candidates.size(); ++run)
		{
			const NativeReplayComparisonCandidate& candidate = comparison.candidates[run];
			if (candidate.error.code != ArenaStatus_Ok) continue;
			std::array<wchar_t, kRunPathCapacity> directory = {};
			BuildRecordDirectory(app->model, app->workspace.finalization.indexes.records[candidate.runIndex], &directory);
			if (comparison.peer == nullptr || _wcsicmp(directory.data(), comparison.peer->directory.c_str()) != 0) continue;
			for (const NativeReplayComparisonChoice& choice : candidate.choices)
				if (choice.tuple == comparison.peer->tuple && choice.state == ReplayComparisonChoiceState_Ready)
				{
					comparison.runChoice = run;
					comparison.tupleChoice = choice.tuple;
					break;
				}
			if (comparison.tupleChoice[2] != UINT32_MAX) break;
		}
		delete comparison.scanManifest;
		comparison.scanManifest = nullptr;
		comparison.picker = ReplayComparisonPickerState_Ready;
		app->renderRequested = PresenceStatus_Present;
		return;
	}
	NativeReplayComparisonCandidate candidate = {};
	candidate.runIndex = app->results.recordingRunIndexes[comparison.scanIndex++];
	const ResultIndexRecord& index = app->workspace.finalization.indexes.records[candidate.runIndex];
	std::array<wchar_t, kRunPathCapacity> path = {};
	if (BuildRecordDirectory(app->model, index, &path) != ArenaStatus_Ok)
		ReplayError(&candidate.error, "comparison_result_directory_invalid");
	else if (LoadResultManifest((std::filesystem::path(path.data()) / "manifest.json").c_str(),
	                           &app->model.catalog, comparison.scanManifest, &candidate.error) == ArenaStatus_Ok)
	{
		const ResultManifestRecord& manifest = *comparison.scanManifest;
		if (CheckReplayComparisonConfiguration(app->workspace.finalization.manifest, manifest, &candidate.error) == ArenaStatus_Ok)
		{
			if (_wcsicmp(path.data(), app->results.selectedDirectory.data()) == 0)
				comparison.runChoice = static_cast<std::uint32_t>(comparison.candidates.size());
			for (const ReplayStorageFile& file : app->results.libraryStorage.files)
			{
				if (_wcsicmp(ReplayStorageRunDirectory(file).c_str(), path.data()) != 0 ||
				    file.engineOrdinal >= manifest.engineCount ||
				    file.threadOrdinal >= manifest.threadCount || file.repeatIndex >= manifest.repeatCount)
					continue;
				const std::array<std::uint32_t, 3> tuple = {file.engineOrdinal, file.threadOrdinal, file.repeatIndex};
				if (_wcsicmp(path.data(), app->results.selectedDirectory.data()) == 0 &&
				    tuple == app->replay->selection)
					continue;
				std::uint32_t existing = 0;
				while (existing < candidate.choices.size() && candidate.choices[existing].tuple != tuple) ++existing;
				if (existing < candidate.choices.size() && file.kind == ReplayStorageKind_Temporary) continue;
				const EngineRecord& engine = app->model.catalog.engines[manifest.engines[file.engineOrdinal].engineIndex];
				EngineProvenanceLabelProjection label = {};
				ProjectEngineProvenanceLabel(CatalogTextView(&app->model.catalog, engine.id), CatalogTextView(&app->model.catalog, engine.displayName),
				    ResultTextView(&manifest, manifest.engines[file.engineOrdinal].reportVersion), &label, &candidate.error);
				ReplayComparisonChoiceState state = ReplayComparisonChoiceState_Ready;
				std::string reason;
				if (file.kind == ReplayStorageKind_Temporary || file.bytes < benchmark_replay::kHeaderBytes + benchmark_replay::kTrailerBytes)
				{
					state = ReplayComparisonChoiceState_Unavailable;
					reason = "Recording incomplete or temporary";
				}
				else if (RecordingForThread(manifest.recordingThreads, manifest.threadCounts[file.threadOrdinal]) == RecordingMode_Off ||
				    FindExecutionFailure(manifest.executionFailures, manifest.engines[file.engineOrdinal].engineIndex, manifest.threadCounts[file.threadOrdinal], file.repeatIndex) != nullptr)
				{
					state = ReplayComparisonChoiceState_Unavailable;
					reason = "Tuple was not recorded or did not complete";
				}
				else if (_wcsicmp(path.data(), app->results.selectedDirectory.data()) == 0 && file.availability != ReplayAvailability_Available)
				{
					state = ReplayComparisonChoiceState_Unavailable;
					reason = "Recording unavailable or invalid";
				}
				NativeReplayComparisonChoice choice = {tuple, manifest.threadCounts[file.threadOrdinal], file.bytes, state,
				    std::move(reason), std::string(label.text.data(), label.size)};
				if (existing < candidate.choices.size()) candidate.choices[existing] = std::move(choice);
				else candidate.choices.push_back(std::move(choice));
			}
			if (candidate.choices.empty())
				ReplayError(&candidate.error, "comparison_has_no_distinct_recording");
		}
	}
	comparison.candidates.push_back(std::move(candidate));
	app->renderRequested = PresenceStatus_Present;
}

void SelectReplayComparisonRun(PhysicsArenaApp* app, std::uint32_t run)
{
	NativeReplayComparison& comparison = *app->replay->comparison;
	if (comparison.runChoice == run) return;
	comparison.runChoice = run;
	comparison.tupleChoice = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
	comparison.selectionError = {};
}

void CombineReplayCameraBounds(const ReplayCameraContext& primary, const ReplayCameraContext& peer,
                               ReplayCameraContext* combined)
{
	*combined = primary;
	combined->objectScale = (std::min)(primary.objectScale, peer.objectScale);
	VisualCameraVector* minima[] = {&combined->sceneMinimum, &combined->framing.minimum};
	const VisualCameraVector peerMinima[] = {peer.sceneMinimum, peer.framing.minimum};
	VisualCameraVector* maxima[] = {&combined->sceneMaximum, &combined->framing.maximum};
	const VisualCameraVector peerMaxima[] = {peer.sceneMaximum, peer.framing.maximum};
	for (int index = 0; index < 2; ++index)
	{
		*minima[index] = {(std::min)(minima[index]->x, peerMinima[index].x), (std::min)(minima[index]->y, peerMinima[index].y), (std::min)(minima[index]->z, peerMinima[index].z)};
		*maxima[index] = {(std::max)(maxima[index]->x, peerMaxima[index].x), (std::max)(maxima[index]->y, peerMaxima[index].y), (std::max)(maxima[index]->z, peerMaxima[index].z)};
	}
	combined->framing.policy.minimum = combined->framing.minimum;
	combined->framing.policy.maximum = combined->framing.maximum;
}

ObservationOutcome LoadReplayComparisonOutcome(const Catalog& catalog, const ResultManifestRecord& manifest,
                                               const std::filesystem::path& directory,
                                               std::array<std::uint32_t, 3> tuple, StatusRecord* error)
{
	ResultRepeatProjection repeats = {};
	const std::uint32_t threads = manifest.threadCounts[tuple[1]];
	if (ProjectResultRepeats((directory / "normalized.csv").c_str(), &catalog, &manifest, tuple[0], threads,
	    &repeats, error) != ArenaStatus_Ok)
		return ObservationOutcome_Unknown;
	ObservationOutcome outcome = repeats.rows[tuple[2]].outcome;
	const CaseRecord benchmarkCase = RunObservationCase(manifest.configuration.benchmarkCase, manifest.verificationMode);
	if (benchmarkCase.observationCount != 0)
	{
		std::unique_ptr<ObservationResultModel> observations(new (std::nothrow) ObservationResultModel{});
		if (observations == nullptr)
		{
			ReplayError(error, "comparison_observation_allocation_failed");
			return ObservationOutcome_Unknown;
		}
		ObservationDetailProjection detail = {};
		const ObservationDetailSelection selection = {manifest.engines[tuple[0]].engineIndex, threads, tuple[2]};
		if (LoadObservationResultModel((directory / "observations.csv").c_str(), &catalog, &benchmarkCase, &manifest,
		    observations.get(), error) != ArenaStatus_Ok || ProjectObservationDetail(observations.get(), &catalog,
		    &benchmarkCase, &selection, &detail, error, &manifest.configuration) != ArenaStatus_Ok)
			return ObservationOutcome_Unknown;
		for (const ObservationDetailRecord& row : std::span(detail.rows).first(detail.rowCount))
			if (row.outcome == ObservationOutcome_Failed) outcome = ObservationOutcome_Failed;
	}
	if (manifest.verificationMode == VerificationMode_On && benchmark_stack::TargetFixture(benchmarkCase.fixtureKind) != 0)
	{
		StackAssessment assessment = StackAssessment_Unassessed;
		if (manifest.stabilityPresence == PresenceStatus_Present)
		{
			std::vector<StackStabilityResult> stability;
			if (LoadStackStability(directory / "stability.csv", &stability, error) != ArenaStatus_Ok)
				return ObservationOutcome_Unknown;
			const std::string_view engine = CatalogTextView(&catalog, catalog.engines[manifest.engines[tuple[0]].engineIndex].id);
			for (const StackStabilityResult& row : stability)
				if (row.runId == ResultTextView(&manifest, manifest.runId) && row.engineId == engine &&
				    row.threadCount == threads && row.repeatIndex == tuple[2] && row.coverage == StackCoverage_Complete)
					assessment = row.assessment;
		}
		if (assessment == StackAssessment_Fail) outcome = ObservationOutcome_Failed;
		else if (assessment != StackAssessment_Pass && outcome != ObservationOutcome_Failed) outcome = ObservationOutcome_Unknown;
	}
	return outcome;
}

ArenaStatus OpenReplayComparisonPeer(PhysicsArenaApp* app)
{
	NativeReplayView& primary = *app->replay;
	NativeReplayComparison& comparison = *primary.comparison;
	PauseReplayView(app);
	if (comparison.runChoice >= comparison.candidates.size() || comparison.tupleChoice[2] == UINT32_MAX)
		return ReplayError(&comparison.selectionError, "comparison_choose_exact_recording");
	NativeReplayPeer* candidate = new (std::nothrow) NativeReplayPeer{};
	if (candidate == nullptr)
		return ReplayError(&comparison.selectionError, "comparison_peer_allocation_failed");
	std::array<wchar_t, kRunPathCapacity> directory = {};
	const ResultIndexRecord& index = app->workspace.finalization.indexes.records[comparison.candidates[comparison.runChoice].runIndex];
	ArenaStatus status = BuildRecordDirectory(app->model, index, &directory);
	candidate->directory = directory.data();
	candidate->tuple = comparison.tupleChoice;
	if (status == ArenaStatus_Ok)
		status = LoadResultManifest((candidate->directory / "manifest.json").c_str(), &app->model.catalog, &candidate->manifest, &comparison.selectionError);
	if (status == ArenaStatus_Ok)
		status = CheckReplayComparisonConfiguration(app->workspace.finalization.manifest, candidate->manifest, &comparison.selectionError);
	if (status == ArenaStatus_Ok && _wcsicmp(candidate->directory.c_str(), app->results.selectedDirectory.data()) == 0 && candidate->tuple == primary.selection)
		status = ReplayError(&comparison.selectionError, "comparison_identical_primary_tuple");
	if (status == ArenaStatus_Ok)
		status = OpenResultReplay(&app->model.catalog, &candidate->manifest, candidate->directory,
		                         candidate->tuple[0], candidate->tuple[1], candidate->tuple[2], &candidate->recording, &comparison.selectionError);
	if (status == ArenaStatus_Ok && FindExecutionFailure(candidate->manifest.executionFailures,
	    candidate->manifest.engines[candidate->tuple[0]].engineIndex, candidate->manifest.threadCounts[candidate->tuple[1]], candidate->tuple[2]) != nullptr)
		status = ReplayError(&comparison.selectionError, "comparison_tuple_did_not_complete");
	std::uint64_t commonFinal = 0;
	if (status == ArenaStatus_Ok)
		status = AdmitReplayComparison(app->workspace.finalization.manifest, primary.recording,
		                               candidate->manifest, candidate->recording, &commonFinal, &comparison.selectionError);
	ReplayCameraContext peerCamera = {}, combined = {};
	if (status == ArenaStatus_Ok &&
	    (PrepareReplayCamera(candidate->recording.scene, &candidate->manifest.configuration.execution, &peerCamera) != VisualCameraStatus_Ok ||
	     PhysicsSceneViewInstallScene(&candidate->recording.scene, &candidate->sceneResources) != RenderViewerStatus_Ok))
		status = ReplayError(&comparison.selectionError, "comparison_scene_install_failed");
	if (status == ArenaStatus_Ok)
	{
		status = SeekReplay(&primary.recording, 0, &comparison.selectionError);
		if (status != ArenaStatus_Ok && comparison.peer != nullptr)
		{
			comparison.pairPresence = PresenceStatus_Absent;
			comparison.failedPane = ReplayComparisonPane_Primary;
			comparison.error = comparison.selectionError;
			SeekReplayClock(&primary.clock, comparison.committedOrdinal, ReplayNow());
		}
	}
	if (status != ArenaStatus_Ok)
	{
		ReleaseReplayPeer(candidate);
		return status;
	}
	CombineReplayCameraBounds(primary.cameraContext, peerCamera, &combined);
	const ResultManifestRecord& manifest = candidate->manifest;
	const std::string_view run = ResultTextView(&manifest, manifest.runId);
	const EngineRecord& engineRecord = app->model.catalog.engines[manifest.engines[candidate->tuple[0]].engineIndex];
	EngineProvenanceLabelProjection engine = {};
	ProjectEngineProvenanceLabel(CatalogTextView(&app->model.catalog, engineRecord.id), CatalogTextView(&app->model.catalog, engineRecord.displayName),
	    ResultTextView(&manifest, manifest.engines[candidate->tuple[0]].reportVersion), &engine, &comparison.selectionError);
	NativeRunLabelProjection runLabel = {};
	ProjectNativeRunLabel(run, manifest.threadCount, manifest.repeatCount, &runLabel);
	candidate->engineLabel.assign(engine.text.data(), engine.size);
	candidate->runLabel.assign(runLabel.text.data(), runLabel.size);
	candidate->identity = candidate->runLabel + " | " + candidate->engineLabel + " | Threads " +
	    std::to_string(manifest.threadCounts[candidate->tuple[1]]) + " | Repeat " + std::to_string(candidate->tuple[2] + 1);
	candidate->outcome = LoadReplayComparisonOutcome(app->model.catalog, manifest, candidate->directory,
	    candidate->tuple, &candidate->outcomeError);
	comparison.primaryOutcome = LoadReplayComparisonOutcome(app->model.catalog, app->workspace.finalization.manifest,
	    app->results.selectedDirectory.data(), primary.selection, &comparison.primaryOutcomeError);
	ReleaseReplayPeer(comparison.peer);
	comparison.peer = candidate;
	comparison.cameraContext = combined;
	comparison.pairPresence = PresenceStatus_Present;
	comparison.committedOrdinal = 0;
	comparison.selectionError = {};
	comparison.error = {};
	primary.clock.finalOrdinal = commonFinal;
	SeekReplayClock(&primary.clock, 0, ReplayNow());
	app->results.replayError = {};
	CancelReplayComparisonPicker(app);
	app->renderRequested = PresenceStatus_Present;
	return ArenaStatus_Ok;
}

ArenaStatus ReadReplayComparisonOrdinal(PhysicsArenaApp* app, std::uint64_t ordinal)
{
	NativeReplayView& primary = *app->replay;
	NativeReplayComparison& comparison = *primary.comparison;
	comparison.pairPresence = PresenceStatus_Absent;
	comparison.failedPane = ReplayComparisonPane_Primary;
	ArenaStatus status = SeekReplay(&primary.recording, ordinal, &comparison.error);
	if (status == ArenaStatus_Ok)
	{
		comparison.failedPane = ReplayComparisonPane_Peer;
		status = SeekReplay(&comparison.peer->recording, ordinal, &comparison.error);
	}
	if (status == ArenaStatus_Ok)
	{
		comparison.committedOrdinal = ordinal;
		comparison.pairPresence = PresenceStatus_Present;
		comparison.error = {};
		comparison.selectionError = {};
		app->results.replayError = {};
	}
	else
		SeekReplayClock(&primary.clock, comparison.committedOrdinal, ReplayNow());
	return status;
}

void StopReplayComparison(PhysicsArenaApp* app)
{
	NativeReplayView& primary = *app->replay;
	NativeReplayComparison& comparison = *primary.comparison;
	const std::uint64_t ordinal = comparison.peer == nullptr ? primary.clock.ordinal : comparison.committedOrdinal;
	const ArenaStatus status = SeekReplay(&primary.recording, ordinal, &app->results.replayError);
	primary.clock.finalOrdinal = primary.recording.layout.frameCount - 1;
	SeekReplayClock(&primary.clock, ordinal, ReplayNow());
	if (status == ArenaStatus_Ok)
		app->results.replayError = {};
	ReleaseReplayComparison(app);
	app->ui.scene = {};
	app->renderRequested = PresenceStatus_Present;
}
}
