using System.Globalization;

namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public enum RecordingMode
{
    Off,
    On
}

public enum VerificationMode : byte
{
    On,
    Off
}

public struct BepuRunnerArgs
{
    public BepuCaseRegistration CaseRegistration;
    public CaseExecutionSpec CaseExecution;
    public string OutputPath;
    public string StackStream;
    public string StepTimingOutputPath;
    public string RecordingPath;
    public RecordingMode RecordingMode;
    public VerificationMode VerificationMode;
    public int WorkerCount;
    public int StepCount;
    public int WarmupSteps;
    public int RepeatIndex;
}

public static class BepuRunnerArgsParser
{
    public static int Parse(string[] args, out BepuRunnerArgs runnerArgs)
    {
        runnerArgs = new BepuRunnerArgs
        {
            OutputPath = "polygon_results.csv",
            WorkerCount = 1
        };
        int caseContractCount = 0;
        int verificationCount = 0;

        for (int index = 0; index < args.Length; ++index)
        {
            string arg = args[index];
            if (arg.StartsWith("--case-contract=", StringComparison.Ordinal))
            {
                ++caseContractCount;
                if (caseContractCount != 1 ||
                    CaseExecutionWire.DecodeHex(arg["--case-contract=".Length..], out runnerArgs.CaseExecution) != 0)
                {
                    return 2;
                }
            }
            else if (arg.StartsWith("--stack-stream=", StringComparison.Ordinal))
            {
                string endpoint = arg.Substring("--stack-stream=".Length);
                if (runnerArgs.StackStream != null || !endpoint.StartsWith(@"\\.\pipe\", StringComparison.Ordinal) || endpoint.Length <= 9) return 2;
                runnerArgs.StackStream = endpoint;
            }
            else if (arg.StartsWith("--verify=", StringComparison.Ordinal))
            {
                string value = arg["--verify=".Length..];
                if (++verificationCount != 1 || (value != "on" && value != "off")) return 2;
                runnerArgs.VerificationMode = value == "on" ? VerificationMode.On : VerificationMode.Off;
            }
            else if (arg.StartsWith("--thread-count=", StringComparison.Ordinal))
            {
                int parseStatus = ParseNumber("thread-count", arg["--thread-count=".Length..], 1, out runnerArgs.WorkerCount);
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
            else if (arg.StartsWith("--recording-output=", StringComparison.Ordinal) &&
                arg.Length > "--recording-output=".Length && runnerArgs.RecordingPath == null)
            {
                runnerArgs.RecordingPath = arg["--recording-output=".Length..];
                runnerArgs.RecordingMode = RecordingMode.On;
            }
            else if (arg.StartsWith("--step-timing-output=", StringComparison.Ordinal) &&
                arg.Length > "--step-timing-output=".Length)
            {
                runnerArgs.StepTimingOutputPath = arg["--step-timing-output=".Length..];
            }
            else
            {
                Console.Error.WriteLine($"invalid_argument value={arg}");
                return 2;
            }
        }

        if (caseContractCount != 1) return 2;
        if (runnerArgs.CaseExecution.MeasuredWorkUnitCount > int.MaxValue ||
            runnerArgs.CaseExecution.WarmupWorkUnitCount > int.MaxValue) return 2;
            CaseFixtureKind fixture = runnerArgs.CaseExecution.FixtureKind;
            int requiresStream = runnerArgs.VerificationMode == VerificationMode.On &&
                (fixture == CaseFixtureKind.OpenContainerFallingPile || fixture == CaseFixtureKind.BoxContactIslands ||
                 fixture == CaseFixtureKind.LargePyramid || fixture == CaseFixtureKind.PyramidWall) ? 1 : 0;
            if (requiresStream != 0 ? runnerArgs.StackStream == null : runnerArgs.StackStream != null) return 2;
        runnerArgs.StepCount = (int)runnerArgs.CaseExecution.MeasuredWorkUnitCount;
        runnerArgs.WarmupSteps = (int)runnerArgs.CaseExecution.WarmupWorkUnitCount;
        return BepuCaseRegistry.Resolve(runnerArgs.CaseExecution.FixtureKind, out runnerArgs.CaseRegistration);
    }

    public static int ParseNumber(string name, string value, int minimum, out int parsed)
    {
        if (!int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out parsed) ||
            parsed < minimum || parsed > 1_000_000)
        {
            Console.Error.WriteLine($"invalid_argument name={name} value={value}");
            return 2;
        }

        return 0;
    }
}
