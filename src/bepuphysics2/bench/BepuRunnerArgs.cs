using System.Globalization;

namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public struct BepuRunnerArgs
{
    public BepuCaseDescriptor CaseDescriptor;
    public string OutputPath;
    public int WorkerCount;
    public int StepCount;
    public int WarmupSteps;
    public int RepeatIndex;
}

public struct BepuRunnerArgsParser
{
    public static BepuRunnerArgs Default()
    {
        return new BepuRunnerArgs
        {
            CaseDescriptor = BepuCaseRegistry.DefaultCase(),
            OutputPath = "polygon_results.csv",
            WorkerCount = 1,
            StepCount = 300,
            WarmupSteps = 0,
            RepeatIndex = 0
        };
    }

    public static int Parse(string[] args, ref BepuRunnerArgs runnerArgs)
    {
        string requestedCaseId = runnerArgs.CaseDescriptor.CaseId;

        for (int index = 0; index < args.Length; ++index)
        {
            string arg = args[index];
            if (arg.StartsWith("--case=", StringComparison.Ordinal))
            {
                requestedCaseId = arg["--case=".Length..];
            }
            else if (arg.StartsWith("--thread-count=", StringComparison.Ordinal))
            {
                int parseStatus = ParseNumber("thread-count", arg["--thread-count=".Length..], 1, out runnerArgs.WorkerCount);
                if (parseStatus != 0)
                {
                    return parseStatus;
                }
            }
            else if (arg.StartsWith("--step-count=", StringComparison.Ordinal))
            {
                int parseStatus = ParseNumber("step-count", arg["--step-count=".Length..], 1, out runnerArgs.StepCount);
                if (parseStatus != 0)
                {
                    return parseStatus;
                }
            }
            else if (arg.StartsWith("--warmup-steps=", StringComparison.Ordinal))
            {
                int parseStatus = ParseNumber("warmup-steps", arg["--warmup-steps=".Length..], 0, out runnerArgs.WarmupSteps);
                if (parseStatus != 0)
                {
                    return parseStatus;
                }
            }
            else if (arg.StartsWith("--repeat-index=", StringComparison.Ordinal))
            {
                int parseStatus = ParseNumber("repeat-index", arg["--repeat-index=".Length..], 0, out runnerArgs.RepeatIndex);
                if (parseStatus != 0)
                {
                    return parseStatus;
                }
            }
            else if (arg.StartsWith("--output=", StringComparison.Ordinal))
            {
                runnerArgs.OutputPath = arg["--output=".Length..];
            }
            else
            {
                Console.Error.WriteLine($"invalid_argument value={arg}");
                return 2;
            }
        }

        return BepuCaseRegistry.Resolve(requestedCaseId, out runnerArgs.CaseDescriptor);
    }

    public static int ParseNumber(string name, string value, int minimum, out int parsed)
    {
        if (!int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out parsed) || parsed < minimum)
        {
            Console.Error.WriteLine($"invalid_argument name={name} value={value}");
            return 2;
        }

        return 0;
    }
}
