using System.Diagnostics;
using System.Globalization;

namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public enum BepuResultValidity : byte
{
    Invalid = 0,
    Valid = 1
}

public enum BepuObservationValueType : byte
{
    Uint64 = 1,
    Float64 = 2
}

public struct BepuObservationRow
{
    public string MetricId;
    public string PhaseId;
    public uint SampleIndex;
    public BepuObservationValueType ValueType;
    public ulong ValueBits;
}

public struct BepuBenchmarkResult
{
    public string PhysicsSettings;
    public int BodyCount;
    public int ShapeCount;
    public int QueryCount;
    public int ConstraintCount;
    public ulong InvalidTransformCount;
    public int EffectiveThreadCount;
    public int EffectiveWorkerCount;
    public int CompletedWorkUnitCount;
    public double WorkloadElapsedMilliseconds;
    public BepuResultValidity CaseValidity;
    public BepuResultValidity MetricValidity;
    public BepuObservationRow[] Observations;
}

public static class BepuResultWriter
{
    public const string CsvHeader = "raw_schema_version,repeat_index,fixture_semantic,fixture_revision,physics_settings,body_count,shape_count,query_count,constraint_count,invalid_transform_count,case_status,metric_status,effective_thread_count,effective_worker_count,actual_taskgraph_worker_count,completed_work_unit_count,workload_elapsed_ms,render_elapsed_ms,present_wait_ms,visual_validation_status,proof_path";
    public const string ObservationCsvHeader = "repeat_index,metric_id,phase_id,sample_index,value\n";

    public static int WriteStepTiming(BepuRunnerArgs runnerArgs, long[] rawStepDurations)
    {
        if (runnerArgs.StepTimingOutputPath == null)
        {
            return 0;
        }
        if (rawStepDurations.Length != runnerArgs.StepCount)
        {
            return 2;
        }

        string outputPath = Path.GetFullPath(runnerArgs.StepTimingOutputPath);
        string directory = Path.GetDirectoryName(outputPath);
        if (!string.IsNullOrEmpty(directory))
        {
            Directory.CreateDirectory(directory);
        }
        string temporaryPath = outputPath + ".tmp";
        using (StreamWriter writer = new(temporaryPath, append: false))
        {
            writer.WriteLine("step_index,physics_step_ms,render_frame_ms");
            for (int step = 0; step < rawStepDurations.Length; ++step)
            {
                double milliseconds = rawStepDurations[step] * 1000.0 / Stopwatch.Frequency;
                writer.WriteLine(string.Create(CultureInfo.InvariantCulture, $"{step + 1},{milliseconds:F9},"));
            }
        }
        File.Move(temporaryPath, outputPath, true);
        return 0;
    }

    public static int WriteResult(BepuRunnerArgs runnerArgs, in BepuBenchmarkResult result)
    {
        if (WriteObservationSidecar(runnerArgs, in result) != 0)
        {
            Console.Error.WriteLine(
                $"result_failed engine={runnerArgs.CaseRegistration.Descriptor.EngineId} case={CaseExecutionWire.TextValue(runnerArgs.CaseExecution.CaseId)} reason=observation_sidecar");
            return 2;
        }

        string directory = Path.GetDirectoryName(Path.GetFullPath(runnerArgs.OutputPath));
        if (!string.IsNullOrEmpty(directory))
        {
            Directory.CreateDirectory(directory);
        }
        bool writeHeader = !File.Exists(runnerArgs.OutputPath);
        string caseStatus = result.CaseValidity == BepuResultValidity.Valid ? "ok" : "invalid_result";
        string metricStatus = result.CaseValidity == BepuResultValidity.Valid &&
            result.MetricValidity == BepuResultValidity.Valid ? "ok" : "invalid_result";
        string elapsed = runnerArgs.CaseExecution.FixtureKind == CaseFixtureKind.RagdollStairTumble ?
            string.Empty : result.WorkloadElapsedMilliseconds.ToString("F9", CultureInfo.InvariantCulture);
        using (StreamWriter writer = new(runnerArgs.OutputPath, append: true))
        {
            if (writeHeader)
            {
                writer.WriteLine(CsvHeader);
            }
            writer.WriteLine(string.Create(CultureInfo.InvariantCulture,
                $"3,{runnerArgs.RepeatIndex},{CaseExecutionWire.TextValue(runnerArgs.CaseExecution.FixtureSemantic)},{runnerArgs.CaseExecution.FixtureRevision},{result.PhysicsSettings},{result.BodyCount},{result.ShapeCount},{result.QueryCount},{result.ConstraintCount},{result.InvalidTransformCount},{caseStatus},{metricStatus},{result.EffectiveThreadCount},{result.EffectiveWorkerCount},,{result.CompletedWorkUnitCount},{elapsed},,,,"));
        }
        if (metricStatus != "ok")
        {
            Console.Error.WriteLine(
                $"invalid_result engine={runnerArgs.CaseRegistration.Descriptor.EngineId} case={CaseExecutionWire.TextValue(runnerArgs.CaseExecution.CaseId)} case_status={caseStatus} metric_status={metricStatus} invalid={result.InvalidTransformCount}");
            return 2;
        }
        return 0;
    }

    public static int WriteObservationSidecar(
        BepuRunnerArgs runnerArgs, in BepuBenchmarkResult result)
    {
        const string rawSuffix = "_raw.csv";
        const string observationSuffix = "_observations.csv";
        string rawPath = runnerArgs.OutputPath;
        if (string.IsNullOrEmpty(rawPath) || !rawPath.EndsWith(rawSuffix, StringComparison.Ordinal))
        {
            return 2;
        }
        string observationPath = rawPath[..^rawSuffix.Length] + observationSuffix;
        string directory = Path.GetDirectoryName(Path.GetFullPath(observationPath));
        if (!string.IsNullOrEmpty(directory))
        {
            Directory.CreateDirectory(directory);
        }
        BepuObservationRow[] rows = result.Observations ?? Array.Empty<BepuObservationRow>();
        if (rows.Length > 200) return 2;
        string existing = File.Exists(observationPath) ?
            File.ReadAllText(observationPath) : ObservationCsvHeader;
        if (!existing.StartsWith(ObservationCsvHeader, StringComparison.Ordinal)) return 2;
        string[] values = new string[rows.Length];
        for (int index = 0; index < rows.Length; ++index)
        {
            BepuObservationRow row = rows[index];
            if (string.IsNullOrEmpty(row.MetricId) || string.IsNullOrEmpty(row.PhaseId) ||
                row.MetricId.IndexOfAny([',', '\r', '\n']) >= 0 ||
                row.PhaseId.IndexOfAny([',', '\r', '\n']) >= 0)
                return 2;
            if (row.ValueType == BepuObservationValueType.Uint64)
                values[index] = row.ValueBits.ToString(CultureInfo.InvariantCulture);
            else if (row.ValueType == BepuObservationValueType.Float64)
            {
                double decoded = BitConverter.Int64BitsToDouble(unchecked((long)row.ValueBits));
                if (!double.IsFinite(decoded)) return 2;
                values[index] = decoded.ToString("G17", CultureInfo.InvariantCulture);
            }
            else return 2;
        }
        string temporaryPath = observationPath + ".tmp";
        using (StreamWriter writer = new(temporaryPath, append: false))
        {
            writer.Write(existing);
            for (int index = 0; index < rows.Length; ++index)
            {
                BepuObservationRow row = rows[index];
                writer.WriteLine(string.Create(CultureInfo.InvariantCulture,
                    $"{runnerArgs.RepeatIndex},{row.MetricId},{row.PhaseId},{row.SampleIndex},{values[index]}"));
            }
        }
        File.Move(temporaryPath, observationPath, true);
        return 0;
    }
}
