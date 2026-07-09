#include "Output/BenchmarkPolygonChaosOutput.h"

#include "Containers/StringConv.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Char.h"

#include <cmath>
#include <cstdio>

namespace BenchmarkPolygonChaos
{

    constexpr const char* kCsvHeader =
        "body_count,shape_count,invalid_transform_count,below_floor_count,out_of_bounds_count,"
        "case_status,metric_status,actual_taskgraph_worker_count,effective_worker_count,"
        "completed_step_count,physics_elapsed_ms\n";

    int WriteProbeCsv(const TCHAR* OutputPath)
    {
        if (OutputPath[0] == TEXT('\0'))
        {
            return 2;
        }

        FILE* File = std::fopen(TCHAR_TO_UTF8(OutputPath), "wb");
        if (File == nullptr)
        {
            return 2;
        }

        std::fprintf(File, "component,status,detail\n");
        std::fprintf(File,
                     "unreal_chaos_probe,ok,engine=%s ref=%s case=%s\n",
                     kEngineId,
                     kEngineRef,
                     kCaseId);
        std::fprintf(File,
                     "thread_worker_policy,ok,requested=thread_count_minus_one "
                     "actual=taskgraph_get_num_worker_threads "
                     "effective=actual_for_threaded_zero_for_single\n");
        std::fclose(File);
        FPlatformMisc::RequestExit(false);
        return 0;
    }

    const char* CaseStatus(const ThreadRuntimeState& RuntimeState,
                           int InvalidTransformCount,
                           int BelowFloorCount)
    {
        if (RuntimeState.WorkerStatus != ThreadWorkerStatus::Ok)
        {
            return ThreadWorkerStatusText(RuntimeState.WorkerStatus);
        }

        return InvalidTransformCount == 0 && BelowFloorCount == 0 ? "ok"
                                                                  : "invalid_stability_counters";
    }

    int WriteResult(const RunnerArgs& Args,
                    const ThreadRuntimeState& RuntimeState,
                    const ChaosCaseState& State)
    {
        FILE* Existing = std::fopen(TCHAR_TO_UTF8(Args.OutputPath), "r");
        const int WriteHeader = Existing == nullptr ? 1 : 0;
        if (Existing != nullptr)
        {
            std::fclose(Existing);
        }

        FILE* File = std::fopen(TCHAR_TO_UTF8(Args.OutputPath), "a");
        if (File == nullptr)
        {
            std::fprintf(stderr,
                         "result_failed reason=open_output path=%s\n",
                         TCHAR_TO_UTF8(Args.OutputPath));
            return 2;
        }

        if (WriteHeader != 0)
        {
            std::fprintf(File, "%s", kCsvHeader);
        }

        int InvalidTransformCount = 0;
        int BelowFloorCount = 0;
        int OutOfBoundsCount = 0;
        for (Chaos::FPBDRigidParticleHandle* Particle : State.DynamicBodies)
        {
            const Chaos::FVec3 Position = Particle->GetX();
            const Chaos::FRotation3 Rotation = Particle->GetR();
            const double PX = Position.X;
            const double PY = Position.Y;
            const double PZ = Position.Z;
            const double RX = Rotation.X;
            const double RY = Rotation.Y;
            const double RZ = Rotation.Z;
            const double RW = Rotation.W;

            if (!std::isfinite(PX) || !std::isfinite(PY) || !std::isfinite(PZ) ||
                !std::isfinite(RX) || !std::isfinite(RY) || !std::isfinite(RZ) ||
                !std::isfinite(RW))
            {
                InvalidTransformCount += 1;
            }

            if (PY < 0.0)
            {
                BelowFloorCount += 1;
            }

            if (PX < -kOpenContainerLateralEscape || PX > kOpenContainerLateralEscape ||
                PZ < -kOpenContainerLateralEscape || PZ > kOpenContainerLateralEscape || PY < 0.0 ||
                PY > kOpenContainerMaxY)
            {
                OutOfBoundsCount += 1;
            }
        }

        const double MsPerStep = State.PhysicsElapsedMs / static_cast<double>(Args.StepCount);
        const double StepsPerSecond = 1000.0 / MsPerStep;
        const char* Status = CaseStatus(RuntimeState, InvalidTransformCount, BelowFloorCount);
        const char* MetricStatus =
            FCStringAnsi::Strcmp(Status, "ok") == 0 ? "ok" : "invalid_result";
        if (State.PhysicsElapsedMs <= 0.0 || std::isfinite(State.PhysicsElapsedMs) == 0 ||
            MsPerStep <= 0.0 || std::isfinite(MsPerStep) == 0 || StepsPerSecond <= 0.0 ||
            std::isfinite(StepsPerSecond) == 0 || State.CompletedStepCount != Args.StepCount)
        {
            std::fprintf(stderr,
                         "invalid_result reason=metric elapsed_ms=%.9f step_count=%d "
                         "completed_step_count=%d\n",
                         State.PhysicsElapsedMs,
                         Args.StepCount,
                         State.CompletedStepCount);
            std::fclose(File);
            return 2;
        }
        std::fprintf(File,
                     "%d,%d,%d,%d,%d,%s,%s,%d,%d,%d,%.9f\n",
                     kBodyCount,
                     kBodyCount,
                     InvalidTransformCount,
                     BelowFloorCount,
                     OutOfBoundsCount,
                     Status,
                     MetricStatus,
                     RuntimeState.ActualTaskGraphWorkerCount,
                     RuntimeState.EffectiveChaosWorkerCount,
                     State.CompletedStepCount,
                     State.PhysicsElapsedMs);
        std::fclose(File);
        return FCStringAnsi::Strcmp(MetricStatus, "ok") == 0 ? 0 : 2;
    }

} // namespace BenchmarkPolygonChaos
