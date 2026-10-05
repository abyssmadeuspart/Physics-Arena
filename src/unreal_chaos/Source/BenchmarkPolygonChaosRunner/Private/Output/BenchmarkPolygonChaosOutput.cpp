#include "Output/BenchmarkPolygonChaosOutput.h"

#include "Containers/StringConv.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace BenchmarkPolygonChaos
{

constexpr const char* kCsvHeader =
    "raw_schema_version,repeat_index,fixture_semantic,fixture_revision,physics_settings,"
    "body_count,shape_count,query_count,constraint_count,invalid_transform_count,"
    "case_status,metric_status,effective_thread_count,"
    "effective_worker_count,actual_taskgraph_worker_count,completed_work_unit_count,"
    "workload_elapsed_ms,render_elapsed_ms,present_wait_ms,visual_validation_status,proof_path\n";
constexpr const char* kObservationCsvHeader = "repeat_index,metric_id,phase_id,sample_index,value\n";

int WriteProbeCsv(const TCHAR* OutputPath, const char* EngineId, const char* EngineRef, const char* CaseId)
{
	if (OutputPath[0] == TEXT('\0') || EngineId == nullptr || EngineRef == nullptr || CaseId == nullptr)
	{
		return 2;
	}

	FILE* File = std::fopen(TCHAR_TO_UTF8(OutputPath), "wb");
	if (File == nullptr)
	{
		return 2;
	}
	std::fprintf(File, "component,status,detail\n");
	std::fprintf(File, "unreal_chaos_probe,ok,engine=%s ref=%s case=%s\n", EngineId, EngineRef, CaseId);
	std::fprintf(File, "thread_worker_policy,ok,requested=thread_count "
	                   "effective=taskgraph_actual_for_threaded_game_thread_for_single\n");
	const int CloseStatus = std::fclose(File);
	FPlatformMisc::RequestExit(false);
	return CloseStatus == 0 ? 0 : 2;
}

int WriteChaosStepTiming(const RunnerArgs& Args, const TArray<uint64>* RawWorkUnitCycles, int CompletedWorkUnitCount)
{
	if (Args.StepTimingOutputPath[0] == TEXT('\0'))
	{
		return 0;
	}
	if (RawWorkUnitCycles == nullptr || CompletedWorkUnitCount != Args.StepCount ||
	    RawWorkUnitCycles->Num() != Args.StepCount)
	{
		return 2;
	}

	const FString TemporaryPath = FString(Args.StepTimingOutputPath) + TEXT(".tmp");
	FILE* File = std::fopen(TCHAR_TO_UTF8(*TemporaryPath), "wb");
	if (File == nullptr)
	{
		return 2;
	}
	int Status = std::fprintf(File, "step_index,physics_step_ms,render_frame_ms\n");
	for (int WorkUnit = 0; Status >= 0 && WorkUnit < CompletedWorkUnitCount; ++WorkUnit)
	{
		Status = std::fprintf(File, "%d,%.9f,\n", WorkUnit + 1,
		                      FPlatformTime::ToMilliseconds64((*RawWorkUnitCycles)[WorkUnit]));
	}
	const int FlushStatus = std::fflush(File);
	const int CloseStatus = std::fclose(File);
	if (Status < 0 || FlushStatus != 0 || CloseStatus != 0 ||
	    !IFileManager::Get().Move(Args.StepTimingOutputPath, *TemporaryPath, true, true, false, true))
	{
		IFileManager::Get().Delete(*TemporaryPath);
		return 2;
	}
	return 0;
}

int WriteObservationSidecar(const RunnerArgs& Args, const ChaosObservationRow* Observations, int ObservationCount)
{
	if (ObservationCount < 0 || ObservationCount > 200 || (ObservationCount > 0 && Observations == nullptr))
	{
		return 2;
	}
	const FString Raw(Args.OutputPath);
	constexpr const TCHAR* RawSuffix = TEXT("_raw.csv");
	if (!Raw.EndsWith(RawSuffix))
	{
		return 2;
	}
	const FString ObservationPath = Raw.LeftChop(FCString::Strlen(RawSuffix)) + TEXT("_observations.csv");
	FString ExistingText;
	if (IFileManager::Get().FileExists(*ObservationPath))
	{
		if (!FFileHelper::LoadFileToString(ExistingText, *ObservationPath))
		{
			return 2;
		}
	}
	else
	{
		ExistingText = UTF8_TO_TCHAR(kObservationCsvHeader);
	}
	if (!ExistingText.StartsWith(UTF8_TO_TCHAR(kObservationCsvHeader)))
	{
		return 2;
	}
	for (int Index = 0; Index < ObservationCount; ++Index)
	{
		const ChaosObservationRow& Row = Observations[Index];
		if (Row.MetricId == nullptr || Row.MetricId[0] == '\0' || Row.PhaseId == nullptr || Row.PhaseId[0] == '\0' ||
		    std::strpbrk(Row.MetricId, ",\r\n") != nullptr || std::strpbrk(Row.PhaseId, ",\r\n") != nullptr)
		{
			return 2;
		}
		if (Row.ValueType == ChaosObservationValueType::Float64)
		{
			double Value = 0.0;
			static_assert(sizeof(Value) == sizeof(Row.ValueBits));
			FMemory::Memcpy(&Value, &Row.ValueBits, sizeof(Value));
			if (!std::isfinite(Value))
			{
				return 2;
			}
		}
		else if (Row.ValueType != ChaosObservationValueType::Uint64)
		{
			return 2;
		}
	}
	const FString TemporaryPath = ObservationPath + TEXT(".tmp");
	FILE* File = std::fopen(TCHAR_TO_UTF8(*TemporaryPath), "wb");
	if (File == nullptr)
	{
		return 2;
	}
	FTCHARToUTF8 ExistingUtf8(*ExistingText);
	int Status = std::fprintf(File, "%s", ExistingUtf8.Get());
	for (int Index = 0; Status >= 0 && Index < ObservationCount; ++Index)
	{
		const ChaosObservationRow& Row = Observations[Index];
		if (Row.ValueType == ChaosObservationValueType::Uint64)
		{
			Status = std::fprintf(File, "%d,%s,%s,%u,%llu\n", Args.RepeatIndex, Row.MetricId, Row.PhaseId,
			                      Row.SampleIndex, static_cast<unsigned long long>(Row.ValueBits));
		}
		else
		{
			double Value = 0.0;
			FMemory::Memcpy(&Value, &Row.ValueBits, sizeof(Value));
			Status = std::fprintf(File, "%d,%s,%s,%u,%.17g\n", Args.RepeatIndex, Row.MetricId, Row.PhaseId,
			                      Row.SampleIndex, Value);
		}
	}
	const int FlushStatus = std::fflush(File);
	const int CloseStatus = std::fclose(File);
	if (Status < 0 || FlushStatus != 0 || CloseStatus != 0 ||
	    !IFileManager::Get().Move(*ObservationPath, *TemporaryPath, true, true, false, true))
	{
		IFileManager::Get().Delete(*TemporaryPath);
		return 2;
	}
	return 0;
}

int WriteChaosResult(const RunnerArgs& Args, const ChaosResult& Result)
{
	if (WriteObservationSidecar(Args, Result.Observations, Result.ObservationCount) != 0)
	{
		return 2;
	}
	FILE* Existing = std::fopen(TCHAR_TO_UTF8(Args.OutputPath), "r");
	const int WriteHeader = Existing == nullptr ? 1 : 0;
	if (Existing != nullptr)
	{
		std::fclose(Existing);
	}
	FILE* File = std::fopen(TCHAR_TO_UTF8(Args.OutputPath), "a");
	if (File == nullptr)
	{
		return 2;
	}
	if (WriteHeader != 0)
	{
		std::fprintf(File, "%s", kCsvHeader);
	}
	char Elapsed[64] = {};
	if (Args.CaseExecution.fixtureKind != CaseFixtureKind_RagdollStairTumble)
		std::snprintf(Elapsed, sizeof(Elapsed), "%.9f", Result.WorkloadElapsedMs);
	const int WriteStatus = std::fprintf(
	    File, "3,%d,%s,%u,%s,%d,%d,%d,%d,%llu,%s,%s,%d,%d,,%d,%s,,,,\n", Args.RepeatIndex, Result.FixtureSemantic,
	    Result.FixtureRevision, Result.PhysicsSettings, Result.BodyCount, Result.ShapeCount, Result.QueryCount,
	    Result.ConstraintCount, static_cast<unsigned long long>(Result.InvalidTransformCount), Result.CaseStatus,
	    Result.MetricStatus, Result.EffectiveThreadCount, Result.EffectiveWorkerCount, Result.CompletedWorkUnitCount,
	    Elapsed);
	const int CloseStatus = std::fclose(File);
	if (WriteStatus < 0 || CloseStatus != 0 || FCStringAnsi::Strcmp(Result.MetricStatus, "ok") != 0)
	{
		std::fprintf(stderr, "invalid_result case_status=%s metric_status=%s invalid=%llu\n", Result.CaseStatus,
		             Result.MetricStatus, static_cast<unsigned long long>(Result.InvalidTransformCount));
		return 2;
	}
	if (Args.CaseExecution.fixtureKind == CaseFixtureKind_RagdollStairTumble)
		return 0;
	return WriteChaosStepTiming(Args, Result.RawWorkUnitCycles, Result.CompletedWorkUnitCount);
}

} // namespace BenchmarkPolygonChaos
