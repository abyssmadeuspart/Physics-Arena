using BepuPhysics;
using System.Globalization;

namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public struct BepuResultWriter
{
    public const string CsvHeader = "body_count,shape_count,invalid_transform_count,below_floor_count,out_of_bounds_count,case_status,metric_status,physics_elapsed_ms";

    public static int Write(BepuRunnerArgs runnerArgs, Simulation simulation, TimeSpan elapsed)
    {
        string directory = Path.GetDirectoryName(Path.GetFullPath(runnerArgs.OutputPath));
        if (!string.IsNullOrEmpty(directory))
        {
            Directory.CreateDirectory(directory);
        }

        int writeHeader = File.Exists(runnerArgs.OutputPath) ? 0 : 1;
        using StreamWriter writer = new(runnerArgs.OutputPath, append: true);
        if (writeHeader != 0)
        {
            writer.WriteLine(CsvHeader);
        }

        BepuCaseDescriptor caseDescriptor = runnerArgs.CaseDescriptor;
        BepuStabilityCounters counters = BepuCaseRegistry.CountStability(in caseDescriptor, simulation);
        if (counters.DynamicBodyCount != caseDescriptor.DynamicBodyCount)
        {
            Console.Error.WriteLine($"invalid_result dynamic_body_count={counters.DynamicBodyCount}");
            return 2;
        }

        double msPerStep = elapsed.TotalMilliseconds / runnerArgs.StepCount;
        double stepsPerSecond = runnerArgs.StepCount / elapsed.TotalSeconds;
        string caseStatus = BepuCaseRegistry.CaseStatus(in caseDescriptor, counters);
        string metricStatus = BepuCaseRegistry.MetricStatus(in caseDescriptor, caseStatus);
        if (elapsed.TotalMilliseconds <= 0.0 ||
            double.IsNaN(elapsed.TotalMilliseconds) ||
            double.IsInfinity(elapsed.TotalMilliseconds) ||
            msPerStep <= 0.0 ||
            double.IsNaN(msPerStep) ||
            double.IsInfinity(msPerStep) ||
            stepsPerSecond <= 0.0 ||
            double.IsNaN(stepsPerSecond) ||
            double.IsInfinity(stepsPerSecond))
        {
            Console.Error.WriteLine(string.Create(CultureInfo.InvariantCulture, $"invalid_result reason=metric elapsed_ms={elapsed.TotalMilliseconds:F9} step_count={runnerArgs.StepCount}"));
            return 2;
        }
        if (metricStatus != "ok")
        {
            Console.Error.WriteLine($"invalid_result reason=case_status status={caseStatus}");
            return 2;
        }

        writer.WriteLine(string.Create(CultureInfo.InvariantCulture,
            $"{caseDescriptor.BodyCount},{caseDescriptor.BodyCount},{counters.InvalidTransformCount},{counters.BelowFloorCount},{counters.OutOfBoundsCount},{caseStatus},{metricStatus},{elapsed.TotalMilliseconds:F9}"));
        return 0;
    }
}
