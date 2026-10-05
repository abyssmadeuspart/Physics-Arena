#include "nvidia_physx34_result_writer.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <string>

namespace nvidia_physx34_benchmark
{
constexpr const char* kCsvHeader =
    "raw_schema_version,repeat_index,fixture_semantic,fixture_revision,physics_settings,body_count,shape_count,query_count,constraint_count,invalid_transform_count,case_status,metric_status,effective_thread_count,effective_worker_count,actual_taskgraph_worker_count,completed_work_unit_count,workload_elapsed_ms,render_elapsed_ms,present_wait_ms,visual_validation_status,proof_path\n";
constexpr const char* kObservationCsvHeader = "repeat_index,metric_id,phase_id,sample_index,value\n";

int WritePhysXTiming(const PhysXRunRequest& request, const std::chrono::steady_clock::duration::rep* durations,
                     int completedWorkUnitCount);

int WritePhysXTiming(const PhysXRunRequest& request, const std::chrono::steady_clock::duration::rep* durations,
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

int WriteObservationSidecar(const PhysXRunRequest& request, const PhysXResult& result)
{
	if (request.outputPath == nullptr || (result.observationCount != 0 && result.observations == nullptr) ||
	    result.observationCount > 200)
		return 2;
	std::string observationPath(request.outputPath);
	constexpr const char* rawSuffix = "_raw.csv";
	constexpr const char* observationSuffix = "_observations.csv";
	if (observationPath.size() < std::strlen(rawSuffix) ||
	    observationPath.compare(observationPath.size() - std::strlen(rawSuffix), std::strlen(rawSuffix), rawSuffix) !=
	        0)
		return 2;
	observationPath.replace(observationPath.size() - std::strlen(rawSuffix), std::strlen(rawSuffix), observationSuffix);
	std::string existingBytes;
	FILE* existing = std::fopen(observationPath.c_str(), "rb");
	if (existing != nullptr)
	{
		std::array<char, 4096> chunk = {};
		for (std::size_t count = std::fread(chunk.data(), 1, chunk.size(), existing); count != 0;
		     count = std::fread(chunk.data(), 1, chunk.size(), existing))
			existingBytes.append(chunk.data(), count);
		const int readStatus = std::ferror(existing);
		std::fclose(existing);
		if (readStatus != 0 || existingBytes.compare(0, std::strlen(kObservationCsvHeader), kObservationCsvHeader) != 0)
			return 2;
	}
	else
		existingBytes = kObservationCsvHeader;
	const std::string temporaryPath = observationPath + ".tmp";
	FILE* file = std::fopen(temporaryPath.c_str(), "wb");
	if (file == nullptr)
		return 2;
	int status = std::fwrite(existingBytes.data(), 1, existingBytes.size(), file) == existingBytes.size() ? 0 : -1;
	for (std::uint32_t index = 0; status == 0 && index < result.observationCount; ++index)
	{
		const PhysXObservationRow& row = result.observations[index];
		if (row.metricId == nullptr || row.phaseId == nullptr)
			status = -1;
		else if (row.valueType == PhysXObservationValueType_Uint64)
			status = std::fprintf(file, "%d,%s,%s,%u,%llu\n", request.repeatIndex, row.metricId, row.phaseId,
			                      row.sampleIndex, static_cast<unsigned long long>(row.valueBits)) >= 0
			             ? 0
			             : -1;
		else if (row.valueType == PhysXObservationValueType_Float64)
		{
			double value = 0.0;
			std::memcpy(&value, &row.valueBits, sizeof(value));
			status = std::isfinite(value) != 0 && std::fprintf(file, "%d,%s,%s,%u,%.17g\n", request.repeatIndex,
			                                                   row.metricId, row.phaseId, row.sampleIndex, value) >= 0
			             ? 0
			             : -1;
		}
		else
			status = -1;
	}
	const int flushStatus = std::fflush(file);
	const int closeStatus = std::fclose(file);
	if (status != 0 || flushStatus != 0 || closeStatus != 0 ||
	    MoveFileExA(temporaryPath.c_str(), observationPath.c_str(),
	                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
	{
		DeleteFileA(temporaryPath.c_str());
		return 2;
	}
	return 0;
}

int WritePhysXResult(const PhysXRunRequest& request, const PhysXResult& result)
{
	if (WriteObservationSidecar(request, result) != 0)
		return 2;
	FILE* existing = std::fopen(request.outputPath, "r");
	const int writeHeader = existing == nullptr ? 1 : 0;
	if (existing != nullptr)
		std::fclose(existing);
	FILE* file = std::fopen(request.outputPath, "a");
	if (file == nullptr)
		return 2;
	if (writeHeader != 0)
		std::fprintf(file, "%s", kCsvHeader);
	std::array<char, 64> duration = {};
	if (request.caseExecution.fixtureKind != CaseFixtureKind_RagdollStairTumble)
		std::snprintf(duration.data(), duration.size(), "%.9f", result.workloadElapsedMs);
	const int writeStatus = std::fprintf(
	    file, "3,%d,%s,%u,%s,%d,%d,%d,%d,%llu,%s,%s,%d,%d,,%d,%s,,,,\n", request.repeatIndex, result.fixtureSemantic,
	    result.fixtureRevision, result.physicsSettings, result.bodyCount, result.shapeCount, result.queryCount,
	    result.constraintCount, static_cast<unsigned long long>(result.invalidTransformCount), result.caseStatus,
	    result.metricStatus, result.effectiveThreadCount, result.effectiveWorkerCount, result.completedWorkUnitCount,
	    duration.data());
	const int closeStatus = std::fclose(file);
	if (writeStatus < 0 || closeStatus != 0 || std::strcmp(result.metricStatus, "ok") != 0)
	{
		std::fprintf(stderr, "invalid_result reason=result case_status=%s metric_status=%s invalid=%llu\n",
		             result.caseStatus, result.metricStatus,
		             static_cast<unsigned long long>(result.invalidTransformCount));
		return 2;
	}
	return request.caseExecution.fixtureKind == CaseFixtureKind_RagdollStairTumble
	           ? 0
			   : WritePhysXTiming(request, result.durations, result.completedWorkUnitCount);
}
}
