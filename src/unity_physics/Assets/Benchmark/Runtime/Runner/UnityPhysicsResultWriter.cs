using System;
using System.Diagnostics;
using System.IO;

namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
    public enum UnityPhysicsResultValidity : byte
    {
        Invalid = 0,
        Valid = 1
    }

    public enum UnityPhysicsObservationValueType : byte
    {
        Uint64 = 1,
        Float64 = 2
    }

    public struct UnityPhysicsObservationRow
    {
        public string MetricId;
        public string PhaseId;
        public uint SampleIndex;
        public UnityPhysicsObservationValueType ValueType;
        public ulong ValueBits;
    }

    public struct UnityPhysicsBenchmarkResult
    {
        public string PhysicsSettings;
        public int BodyCount;
        public int ShapeCount;
        public int QueryCount;
        public int ConstraintCount;
        public uint InvalidTransformCount;
        public int EffectiveThreadCount;
        public int EffectiveWorkerCount;
        public int CompletedWorkUnitCount;
        public double WorkloadElapsedMilliseconds;
        public UnityPhysicsResultValidity CaseValidity;
        public UnityPhysicsResultValidity MetricValidity;
        public UnityPhysicsObservationRow[] Observations;
    }

    public static partial class UnityPhysicsBenchmarkRunner
    {
        public const string CsvHeader = "raw_schema_version,repeat_index,fixture_semantic,fixture_revision,physics_settings,body_count,shape_count,query_count,constraint_count,invalid_transform_count,case_status,metric_status,effective_thread_count,effective_worker_count,actual_taskgraph_worker_count,completed_work_unit_count,workload_elapsed_ms,render_elapsed_ms,present_wait_ms,visual_validation_status,proof_path";
        public const string ObservationCsvHeader = "repeat_index,metric_id,phase_id,sample_index,value\n";

        public static int WriteStepTiming(RunnerArgs args, long[] rawStepDurations)
        {
            if (args.StepTimingOutputPath == null)
            {
                return 0;
            }
            if (rawStepDurations.Length != args.StepCount)
            {
                return 2;
            }

            string outputPath = Path.GetFullPath(args.StepTimingOutputPath);
            string directory = Path.GetDirectoryName(outputPath);
            if (!string.IsNullOrEmpty(directory))
            {
                Directory.CreateDirectory(directory);
            }
            string temporaryPath = outputPath + ".tmp";
            using (StreamWriter writer = new StreamWriter(temporaryPath, false))
            {
                writer.WriteLine("step_index,physics_step_ms,render_frame_ms");
                for (int step = 0; step < rawStepDurations.Length; ++step)
                {
                    double milliseconds = rawStepDurations[step] * 1000.0 / Stopwatch.Frequency;
                    writer.WriteLine(FormattableString.Invariant($"{step + 1},{milliseconds:F9},"));
                }
            }
            if (File.Exists(outputPath))
            {
                File.Replace(temporaryPath, outputPath, null);
            }
            else
            {
                File.Move(temporaryPath, outputPath);
            }
            return 0;
        }

        public static int WriteResult(RunnerArgs args, in UnityPhysicsBenchmarkResult result)
        {
            if (WriteUnityPhysicsObservationSidecar(args, in result) != 0)
            {
                return 2;
            }
            string directory = Path.GetDirectoryName(Path.GetFullPath(args.OutputPath));
            if (!string.IsNullOrEmpty(directory))
            {
                Directory.CreateDirectory(directory);
            }
            int writeHeader = File.Exists(args.OutputPath) ? 0 : 1;
            UnityPhysicsCaseDescriptor descriptor = args.CaseRegistration.Descriptor;
            string caseStatus = result.CaseValidity == UnityPhysicsResultValidity.Valid ?
                "ok" : "invalid_result";
            string metricStatus = result.CaseValidity == UnityPhysicsResultValidity.Valid &&
                result.MetricValidity == UnityPhysicsResultValidity.Valid ? "ok" : "invalid_result";
            string elapsed = args.CaseExecution.FixtureKind == CaseFixtureKind.RagdollStairTumble ?
                string.Empty : result.WorkloadElapsedMilliseconds.ToString("F9", System.Globalization.CultureInfo.InvariantCulture);
            using (StreamWriter writer = new StreamWriter(args.OutputPath, true))
            {
                if (writeHeader != 0)
                {
                    writer.WriteLine(CsvHeader);
                }
                writer.WriteLine(FormattableString.Invariant(
                    $"3,{args.RepeatIndex},{args.CaseExecution.FixtureSemantic},{args.CaseExecution.FixtureRevision},{result.PhysicsSettings},{result.BodyCount},{result.ShapeCount},{result.QueryCount},{result.ConstraintCount},{result.InvalidTransformCount},{caseStatus},{metricStatus},{result.EffectiveThreadCount},{result.EffectiveWorkerCount},,{result.CompletedWorkUnitCount},{elapsed},,,,"));
            }
            if (metricStatus != "ok")
            {
                Console.Error.WriteLine(
                    "invalid_result engine=" + descriptor.EngineId + " case=" + args.CaseExecution.CaseId +
                    " case_status=" + caseStatus + " metric_status=" + metricStatus +
                    " invalid=" + result.InvalidTransformCount);
                return 2;
            }
            return 0;
        }

        public static int WriteUnityPhysicsObservationSidecar(
            RunnerArgs args, in UnityPhysicsBenchmarkResult result)
        {
            const string rawSuffix = "_raw.csv";
            string rawPath = args.OutputPath;
            if (string.IsNullOrEmpty(rawPath) || !rawPath.EndsWith(rawSuffix, StringComparison.Ordinal))
            {
                return 2;
            }
            string observationPath = rawPath.Substring(0, rawPath.Length - rawSuffix.Length) + "_observations.csv";
            string directory = Path.GetDirectoryName(Path.GetFullPath(observationPath));
            if (!string.IsNullOrEmpty(directory))
            {
                Directory.CreateDirectory(directory);
            }
            string existing = File.Exists(observationPath) ?
                File.ReadAllText(observationPath) : ObservationCsvHeader;
            if (!existing.StartsWith(ObservationCsvHeader, StringComparison.Ordinal))
            {
                return 2;
            }
            UnityPhysicsObservationRow[] rows =
                result.Observations ?? Array.Empty<UnityPhysicsObservationRow>();
            if (rows.Length > 200)
            {
                return 2;
            }
            string[] values = new string[rows.Length];
            for (int index = 0; index < rows.Length; ++index)
            {
                UnityPhysicsObservationRow row = rows[index];
                if (string.IsNullOrEmpty(row.MetricId) || string.IsNullOrEmpty(row.PhaseId) ||
                    row.MetricId.IndexOfAny(new[] { ',', '\r', '\n' }) >= 0 ||
                    row.PhaseId.IndexOfAny(new[] { ',', '\r', '\n' }) >= 0)
                {
                    return 2;
                }
                if (row.ValueType == UnityPhysicsObservationValueType.Uint64)
                {
                    values[index] = row.ValueBits.ToString(
                        System.Globalization.CultureInfo.InvariantCulture);
                }
                else if (row.ValueType == UnityPhysicsObservationValueType.Float64)
                {
                    double value = BitConverter.Int64BitsToDouble(unchecked((long)row.ValueBits));
                    if (double.IsNaN(value) || double.IsInfinity(value))
                    {
                        return 2;
                    }
                    values[index] = value.ToString(
                        "G17", System.Globalization.CultureInfo.InvariantCulture);
                }
                else
                {
                    return 2;
                }
            }
            string temporaryPath = observationPath + ".tmp";
            using (StreamWriter writer = new StreamWriter(temporaryPath, false))
            {
                writer.Write(existing);
                for (int index = 0; index < rows.Length; ++index)
                {
                    UnityPhysicsObservationRow row = rows[index];
                    writer.WriteLine(FormattableString.Invariant(
                        $"{args.RepeatIndex},{row.MetricId},{row.PhaseId},{row.SampleIndex},{values[index]}"));
                }
            }
            if (File.Exists(observationPath))
            {
                File.Replace(temporaryPath, observationPath, null);
            }
            else
            {
                File.Move(temporaryPath, observationPath);
            }
            return 0;
        }
    }
}
