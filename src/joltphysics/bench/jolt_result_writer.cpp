#include "jolt_result_writer.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

namespace jolt_benchmark
{
constexpr const char* kCsvHeader =
    "raw_schema_version,repeat_index,fixture_semantic,fixture_revision,physics_settings,body_count,shape_count,query_count,constraint_count,invalid_transform_count,case_status,metric_status,effective_thread_count,effective_worker_count,actual_taskgraph_worker_count,completed_work_unit_count,workload_elapsed_ms,render_elapsed_ms,present_wait_ms,visual_validation_status,proof_path\n";
constexpr const char* kObservationCsvHeader = "repeat_index,metric_id,phase_id,sample_index,value\n";

int WriteJoltTiming(const JoltRunRequest& request, const std::chrono::steady_clock::duration::rep* durations,
                    int completedWorkUnitCount);

int WriteJoltTiming(const JoltRunRequest& request, const std::chrono::steady_clock::duration::rep* durations,
                    int completedWorkUnitCount)
{
	if (request.stepTimingOutputPath == nullptr)
		return 0;
	if (durations == nullptr || completedWorkUnitCount != request.stepCount)
		return 2;
	std::array<char, 4096> temporary = {};
	const int pathSize = std::snprintf(temporary.data(), temporary.size(), "%s.tmp", request.stepTimingOutputPath);
	if (pathSize <= 0 || static_cast<std::size_t>(pathSize) >= temporary.size())
		return 2;
	std::ofstream file(temporary.data(), std::ios::binary | std::ios::trunc);
	if (!file)
		return 2;
	file << "step_index,physics_step_ms,render_frame_ms\n" << std::fixed << std::setprecision(9);
	for (int step = 0; step < request.stepCount; ++step)
		file << step + 1 << ','
		     << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::duration(durations[step])).count()
		     << ",\n";
	file.flush();
	const int valid = file.good() ? 1 : 0;
	file.close();
	if (valid == 0 || MoveFileExA(temporary.data(), request.stepTimingOutputPath,
	                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
	{
		DeleteFileA(temporary.data());
		return 2;
	}
	return 0;
}

int WriteObservationSidecar(const JoltRunRequest& request, const JoltResult& result)
{
	if (request.outputPath == nullptr || (result.observationCount != 0 && result.observations == nullptr) ||
	    result.observationCount > 200)
		return 2;
	std::string observationPath(request.outputPath);
	constexpr std::string_view rawSuffix = "_raw.csv";
	constexpr std::string_view observationSuffix = "_observations.csv";
	if (observationPath.size() < rawSuffix.size() ||
	    observationPath.compare(observationPath.size() - rawSuffix.size(), rawSuffix.size(), rawSuffix) != 0)
		return 2;
	observationPath.replace(observationPath.size() - rawSuffix.size(), rawSuffix.size(), observationSuffix.data(),
	                        observationSuffix.size());
	std::ifstream existing(observationPath, std::ios::binary);
	std::string existingBytes;
	if (existing.good())
	{
		existingBytes.assign(std::istreambuf_iterator<char>(existing), std::istreambuf_iterator<char>());
		if (existingBytes.compare(0, std::strlen(kObservationCsvHeader), kObservationCsvHeader) != 0)
			return 2;
	}
	else
		existingBytes = kObservationCsvHeader;
	existing.close();
	const std::string temporaryPath = observationPath + ".tmp";
	std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
	if (!file)
		return 2;
	file << existingBytes << std::setprecision(17);
	for (std::uint32_t index = 0; index < result.observationCount; ++index)
	{
		const JoltObservationRow& row = result.observations[index];
		if (row.metricId == nullptr || row.phaseId == nullptr)
			return 2;
		file << request.repeatIndex << ',' << row.metricId << ',' << row.phaseId << ',' << row.sampleIndex << ',';
		if (row.valueType == JoltObservationValueType_Uint64)
			file << row.valueBits;
		else if (row.valueType == JoltObservationValueType_Float64)
		{
			double value = 0.0;
			std::memcpy(&value, &row.valueBits, sizeof(value));
			if (!std::isfinite(value))
				return 2;
			file << value;
		}
		else
			return 2;
		file << '\n';
	}
	file.flush();
	const int valid = file.good() ? 1 : 0;
	file.close();
	if (valid == 0 || MoveFileExA(temporaryPath.c_str(), observationPath.c_str(),
	                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
	{
		DeleteFileA(temporaryPath.c_str());
		return 2;
	}
	return 0;
}

int WriteJoltResult(const JoltRunRequest& request, const JoltResult& result)
{
	if (WriteObservationSidecar(request, result) != 0)
		return 2;
	std::ifstream existing(request.outputPath);
	const int writeHeader = existing.good() ? 0 : 1;
	existing.close();
	std::ofstream file(request.outputPath, std::ios::app);
	if (!file)
		return 2;
	if (writeHeader != 0)
		file << kCsvHeader;
	file << std::fixed << std::setprecision(9) << "3," << request.repeatIndex << ',' << result.fixtureSemantic << ','
	     << result.fixtureRevision << ',' << result.physicsSettings << ',' << result.bodyCount << ','
	     << result.shapeCount << ',' << result.queryCount << ',' << result.constraintCount << ','
	     << result.invalidTransformCount << ',' << result.caseStatus << ',' << result.metricStatus << ','
	     << result.effectiveThreadCount << ',' << result.effectiveWorkerCount << ",," << result.completedWorkUnitCount
	     << ',';
	if (request.caseExecution.fixtureKind != CaseFixtureKind_RagdollStairTumble)
		file << result.workloadElapsedMs;
	file << ",,,,\n";
	file.flush();
	if (!file.good() || std::strcmp(result.metricStatus, "ok") != 0)
	{
		std::cerr << "invalid_result reason=result case_status=" << result.caseStatus
		          << " metric_status=" << result.metricStatus << " invalid=" << result.invalidTransformCount << '\n';
		return 2;
	}
	return request.caseExecution.fixtureKind == CaseFixtureKind_RagdollStairTumble
	           ? 0
			   : WriteJoltTiming(request, result.durations, result.completedWorkUnitCount);
}
}
