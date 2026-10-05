#include "result_pipeline_internal.h"
#include "physics_arena/result_view_model.h"

#include <cmath>

#include <cwchar>
#include <memory>
#include <new>

namespace physics_arena
{
ArenaStatus FinalizeRunResults(const wchar_t* repositoryRoot, const Catalog* catalog,
                               const ReleaseCatalog* releaseCatalog, const PreparedRunRequest* request,
                               const RunPathRecord* paths, ResultPipelineRecord* record, StatusRecord* error)
{
	if (record != nullptr)
		*record = {};
	if (error != nullptr)
		*error = {};
	if (repositoryRoot == nullptr || catalog == nullptr || releaseCatalog == nullptr || request == nullptr ||
	    paths == nullptr || record == nullptr || error == nullptr || request->caseIndex >= catalog->caseCount ||
	    request->repeatCount == 0 || request->repeatCount > kRunRepeatCapacity || request->totalUnitCount == 0 ||
	    request->totalUnitCount > kResultUnitCapacity)
		return error != nullptr ? PipelineError(error, ArenaStatus_InvalidArgument, "invalid_result_pipeline_arguments")
		                        : ArenaStatus_InvalidArgument;
	return FinalizeResults(repositoryRoot, catalog, releaseCatalog, request, paths, record, error);
}

ArenaStatus FinalizeHeadlessResults(const wchar_t* repositoryRoot, const Catalog* catalog,
                                    const ReleaseCatalog* releaseCatalog, const PreparedRunRequest* request,
                                    const RunPathRecord* paths, ResultPipelineRecord* record, StatusRecord* error)
{
	return FinalizeRunResults(repositoryRoot, catalog, releaseCatalog, request, paths, record, error);
}

ArenaStatus ValidateNormalizedCoverage(const NormalizedValidationContext& context, StatusRecord* error)
{
	const ResultManifestRecord& manifest = *context.manifest;
	for (std::uint32_t engine = 0; engine < manifest.engineCount; ++engine)
		for (std::uint32_t thread = 0; thread < manifest.threadCount; ++thread)
			for (std::uint32_t repeat = 0; repeat < manifest.repeatCount; ++repeat)
			{
				const ExecutionFailure* failure = FindExecutionFailure(manifest.executionFailures, manifest.engines[engine].engineIndex,
				                                                        manifest.threadCounts[thread], repeat);
				const PresenceStatus seen = context.seen[engine][thread][repeat];
				if ((seen == PresenceStatus_Absent && failure == nullptr) ||
				    (seen == PresenceStatus_Present && failure != nullptr && failure->outcome == ExecutionOutcome_NotRun))
					return PipelineError(error, ArenaStatus_InvalidResult, "normalized_terminal_coverage");
			}
	return ArenaStatus_Ok;
}

ArenaStatus ValidateNormalizedResults(const wchar_t* normalizedPath, const Catalog* catalog,
                                      const ResultManifestRecord* manifest, StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (normalizedPath == nullptr || catalog == nullptr || manifest == nullptr || error == nullptr ||
	    manifest->engineCount == 0 || manifest->threadCount == 0 || manifest->repeatCount == 0 ||
	    manifest->repeatCount > kRunRepeatCapacity)
		return error != nullptr ? PipelineError(error, ArenaStatus_InvalidArgument, "normalized_validation_argument")
		                        : ArenaStatus_InvalidArgument;
	NormalizedValidationContext context = {};
	context.catalog = catalog;
	context.manifest = manifest;
	CsvHeader header = {};
	CsvReadRecord record = {};
	if (ReadCsvFile(normalizedPath, &header, ValidateNormalizedRow, &context, &record, error) != ArenaStatus_Ok ||
	    ValidateNormalizedCoverage(context, error) != ArenaStatus_Ok)
		return error->code != ArenaStatus_Ok
		           ? error->code
				   : PipelineError(error, ArenaStatus_InvalidResult, "normalized_validation_cardinality");
	return ArenaStatus_Ok;
}

ArenaStatus ValidateResultAgreement(const wchar_t* normalizedPath, const Catalog* catalog,
                                    const ResultManifestRecord* manifest, const ResultViewModel* model,
                                    const TimingArtifactTotals* totals, StatusRecord* error)
{
	std::unique_ptr<NormalizedSummaryContext> context(new (std::nothrow) NormalizedSummaryContext{});
	if (context == nullptr)
		return PipelineError(error, ArenaStatus_RunFailed, "normalized_summary_workspace");
	for (std::uint32_t engine = 0; engine < manifest->engineCount; ++engine)
	{
		*context = {};
		context->validation.catalog = catalog;
		context->validation.manifest = manifest;
		context->targetEngineOrdinal = engine;
		CsvHeader header = {};
		CsvReadRecord record = {};
		if (ReadCsvFile(normalizedPath, &header, AccumulateNormalizedSummaryRow, context.get(), &record, error) !=
		        ArenaStatus_Ok ||
		    ValidateNormalizedCoverage(context->validation, error) != ArenaStatus_Ok)
			return error->code != ArenaStatus_Ok
			           ? error->code
					   : PipelineError(error, ArenaStatus_InvalidResult, "normalized_summary_cardinality");
		for (std::uint32_t thread = 0; thread < manifest->threadCount; ++thread)
		{
			const ResultAccumulator& accumulator = context->accumulators[thread];
			const ResultSummary expected =
			    SummarizeUnit(accumulator, manifest->engines[engine].engineIndex, manifest->threadCounts[thread]);
			std::uint32_t rowIndex = 0;
			while (rowIndex < model->summaryRowCount &&
			       (model->summaryRows[rowIndex].engineOrdinal != engine ||
			        model->summaryRows[rowIndex].threadCount != expected.threadCount))
				++rowIndex;
			if (rowIndex == model->summaryRowCount)
				return PipelineError(error, ArenaStatus_InvalidResult, "summary_normalized_identity");
			const ResultSummaryViewRow& actual = model->summaryRows[rowIndex];
			if (actual.repeatCount != expected.repeatCount)
				return PipelineError(error, ArenaStatus_InvalidResult, "summary_normalized_repeat_coverage");
			if (expected.repeatCount == 0)
				continue;
			if (CompareSerializedMetric(actual.minimumPrimaryValue, expected.minimumPrimaryValue) != ArenaStatus_Ok ||
			    CompareSerializedMetric(actual.medianPrimaryValue, expected.medianPrimaryValue) != ArenaStatus_Ok ||
			    CompareSerializedMetric(actual.maximumPrimaryValue, expected.maximumPrimaryValue) != ArenaStatus_Ok)
				return PipelineError(error, ArenaStatus_InvalidResult, "summary_normalized_disagreement");
			if (manifest->measurementMode == ResultMeasurementMode_PhysicalQuality)
				continue;
			const std::array<double, kTimingRepeatCapacity>& timing =
			    totals->physicsMilliseconds[engine * manifest->threadCount + thread];
			// same decimal accumulation bound as raw-fragment finalization
			const double tolerance = std::max(0.000001, (manifest->measuredWorkUnitCount + 1) * 0.0000000005);
			for (std::uint32_t repeat = 0; repeat < manifest->repeatCount; ++repeat)
			{
				PresenceStatus timingPresent = PresenceStatus_Absent;
				for (std::uint32_t index = 0; index < model->timing.sliceCount; ++index)
				{
					const TimingArtifactSlice& slice = model->timing.slices[index];
					if (slice.engineIndex == manifest->engines[engine].engineIndex && slice.threadCount == manifest->threadCounts[thread] &&
					    std::find(slice.repeatIndexes.begin(), slice.repeatIndexes.end(), repeat) != slice.repeatIndexes.end())
						timingPresent = PresenceStatus_Present;
				}
				if (timingPresent == PresenceStatus_Absent && FindExecutionFailure(manifest->executionFailures, manifest->engines[engine].engineIndex, manifest->threadCounts[thread], repeat) != nullptr)
					continue;
				if (accumulator.seen[repeat] == PresenceStatus_Present && (!std::isfinite(timing[repeat]) ||
				    std::abs(timing[repeat] - accumulator.workloadElapsedMilliseconds[repeat]) > tolerance))
					return PipelineError(error, ArenaStatus_InvalidResult, "timing_normalized_total_disagreement");
			}

		}
	}
	return ArenaStatus_Ok;
}

ArenaStatus RegenerateSummaryFromNormalized(const wchar_t* normalizedPath, const wchar_t* summaryPath,
                                            const Catalog* catalog, const ResultManifestRecord* manifest,
                                            StatusRecord* error)
{
	if (error != nullptr)
		*error = {};
	if (normalizedPath == nullptr || summaryPath == nullptr || catalog == nullptr || manifest == nullptr ||
	    error == nullptr || manifest->engineCount == 0 || manifest->threadCount == 0 || manifest->repeatCount == 0 ||
	    manifest->repeatCount > kRunRepeatCapacity)
		return error != nullptr ? PipelineError(error, ArenaStatus_InvalidArgument, "normalized_summary_argument")
		                        : ArenaStatus_InvalidArgument;
	std::uint32_t caseIndex = 0;
	while (caseIndex < catalog->caseCount &&
	       CatalogTextView(catalog, catalog->cases[caseIndex].id) != ResultTextView(manifest, manifest->caseId))
		++caseIndex;
	if (caseIndex == catalog->caseCount && ResultConfiguration(manifest) == nullptr)
		return PipelineError(error, ArenaStatus_InvalidResult, "normalized_summary_case");
	const EffectiveRunConfiguration* configuration = ResultConfiguration(manifest);
	const CaseRecord& benchmarkCase =
	    configuration != nullptr ? configuration->benchmarkCase : catalog->cases[caseIndex];
	std::unique_ptr<std::array<ResultSummary, kResultUnitCapacity>> summaries =
	    std::unique_ptr<std::array<ResultSummary, kResultUnitCapacity>>(
	        new (std::nothrow) std::array<ResultSummary, kResultUnitCapacity>{});
	std::unique_ptr<NormalizedSummaryContext> context =
	    std::unique_ptr<NormalizedSummaryContext>(new (std::nothrow) NormalizedSummaryContext{});
	if (summaries == nullptr || context == nullptr)
		return PipelineError(error, ArenaStatus_RunFailed, "normalized_summary_workspace");
	std::uint32_t summaryCount = 0;
	for (std::uint32_t engine = 0; engine < manifest->engineCount; ++engine)
	{
		*context = {};
		context->validation.catalog = catalog;
		context->validation.manifest = manifest;
		context->targetEngineOrdinal = engine;
		CsvHeader header = {};
		CsvReadRecord readRecord = {};
		if (ReadCsvFile(normalizedPath, &header, AccumulateNormalizedSummaryRow, context.get(), &readRecord, error) !=
		        ArenaStatus_Ok ||
		    ValidateNormalizedCoverage(context->validation, error) != ArenaStatus_Ok)
			return error->code != ArenaStatus_Ok
			           ? error->code
					   : PipelineError(error, ArenaStatus_InvalidResult, "normalized_summary_cardinality");
		for (std::uint32_t thread = 0; thread < manifest->threadCount; ++thread)
		{
			const ResultAccumulator& accumulator = context->accumulators[thread];
			ResultSummary& summary = (*summaries)[summaryCount++];
			summary = SummarizeUnit(accumulator, manifest->engines[engine].engineIndex, manifest->threadCounts[thread]);
			if (summary.repeatCount == 0)
			{
				summary.bodyCount = benchmarkCase.bodyCount;
				summary.shapeCount = benchmarkCase.shapeCount;
				summary.queryCount = benchmarkCase.queryCount;
				summary.constraintCount = benchmarkCase.constraintCount;
				constexpr std::string_view unavailable = "unavailable";
				std::copy(unavailable.begin(), unavailable.end(), summary.physicsSettings.begin());
				std::copy(unavailable.begin(), unavailable.end(), summary.buildSettings.begin());
				summary.physicsSettingsSize = summary.buildSettingsSize = static_cast<std::uint32_t>(unavailable.size());
			}
		}
	}
	std::sort(summaries->begin(), summaries->begin() + summaryCount,
	          [catalog, &benchmarkCase](const ResultSummary& left, const ResultSummary& right)
	          {
		          if (left.threadCount != right.threadCount)
			          return left.threadCount < right.threadCount;
		          if (left.medianPrimaryValue != right.medianPrimaryValue)
			          return benchmarkCase.primaryMetricDirection == PrimaryMetricDirection_HigherIsBetter
			                     ? left.medianPrimaryValue > right.medianPrimaryValue
								 : left.medianPrimaryValue < right.medianPrimaryValue;
		          return CatalogTextView(catalog, catalog->engines[left.engineIndex].id) <
				         CatalogTextView(catalog, catalog->engines[right.engineIndex].id);
	          });
	std::array<wchar_t, kRunPathCapacity> temporary = {};
	std::array<wchar_t, kRunPathCapacity> final = {};
	const std::size_t summaryLength = std::wcslen(summaryPath);
	if (summaryLength + 4 >= final.size())
		return PipelineError(error, ArenaStatus_InvalidResult, "normalized_summary_path_capacity");
	std::copy(summaryPath, summaryPath + summaryLength + 1, final.begin());
	std::copy(summaryPath, summaryPath + summaryLength, temporary.begin());
	std::copy_n(L".tmp", 5, temporary.begin() + summaryLength);
	DeleteOutput(temporary);
	CsvWriter writer = {};
	ArenaStatus status = OpenCsvWriter(temporary.data(), &writer, error);
	if (status == ArenaStatus_Ok)
		status = WriteHeader(&writer, kSummaryColumns, error);
	for (std::uint32_t index = 0; index < summaryCount && status == ArenaStatus_Ok; ++index)
		status = WriteResultSummaryRow(&writer, catalog, &benchmarkCase, ResultTextView(manifest, manifest->runId),
		                               ResultThreadBenchmarkMode(manifest, (*summaries)[index].threadCount), (*summaries)[index], error,
		                               manifest->measurementMode, configuration);
	if (status == ArenaStatus_Ok)
		status = FinishCsvWriter(&writer, error);
	else
		DestroyCsvWriter(&writer);
	if (status == ArenaStatus_Ok)
		status = RenameOutput(temporary, final, error);
	if (status != ArenaStatus_Ok)
		DeleteOutput(temporary);
	return status;
}
} // namespace physics_arena
