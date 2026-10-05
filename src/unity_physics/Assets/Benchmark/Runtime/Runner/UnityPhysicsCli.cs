using System;
using System.Globalization;

namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
    public static partial class UnityPhysicsBenchmarkRunner
    {
        public static int ParseArgs(string[] args, out RunnerArgs runnerArgs)
        {
            runnerArgs = new RunnerArgs
            {
                OutputPath = "polygon_results.csv",
                ThreadCount = 1
            };
            int delimiterIndex = Array.IndexOf(args, "--");
            if (delimiterIndex < 0)
            {
                Console.Error.WriteLine("invalid_argument name=delimiter value=missing");
                return 2;
            }

            int caseContractCount = 0;
            int verificationCount = 0;
            int stabilizationCount = 0;
            int recordingCount = 0;
            for (int index = delimiterIndex + 1; index < args.Length; ++index)
            {
                string arg = args[index];
                if (arg.StartsWith("--case-contract=", StringComparison.Ordinal))
                {
                    ++caseContractCount;
                    if (caseContractCount != 1 || UnityPhysicsCaseExecutionWire.DecodeHex(
                            arg.Substring("--case-contract=".Length), out runnerArgs.CaseExecution) != 0)
                    {
                        return 2;
                    }
                }
                else if (arg.StartsWith("--contact-solver-stabilization=", StringComparison.Ordinal))
                {
                    string value = arg.Substring("--contact-solver-stabilization=".Length);
                    if (++stabilizationCount != 1 || (value != "enabled" && value != "disabled")) return 2;
                    runnerArgs.SolverStabilization = value == "enabled" ? CaseExecutionToggle.Enabled : CaseExecutionToggle.Disabled;
                }
                else if (arg.StartsWith("--stack-stream=", StringComparison.Ordinal))
                {
                    string endpoint = arg.Substring("--stack-stream=".Length);
                    if (runnerArgs.StackStream != null || !endpoint.StartsWith(@"\\.\pipe\", StringComparison.Ordinal) || endpoint.Length <= 9) return 2;
                    runnerArgs.StackStream = endpoint;
                }
                else if (arg.StartsWith("--verify=", StringComparison.Ordinal))
                {
                    string value = arg.Substring("--verify=".Length);
                    if (++verificationCount != 1 || (value != "on" && value != "off")) return 2;
                    runnerArgs.VerificationMode = value == "on" ? VerificationMode.On : VerificationMode.Off;
                }
                else if (arg.StartsWith("--thread-count=", StringComparison.Ordinal))
                {
                    if (!int.TryParse(arg.Substring("--thread-count=".Length), NumberStyles.Integer, CultureInfo.InvariantCulture, out runnerArgs.ThreadCount) ||
                        runnerArgs.ThreadCount < 1 || runnerArgs.ThreadCount > 1_000_000)
                    {
                        Console.Error.WriteLine("invalid_argument name=thread-count value=" + arg);
                        return 2;
                    }
                }
                else if (arg.StartsWith("--repeat-index=", StringComparison.Ordinal))
                {
                    if (!int.TryParse(arg.Substring("--repeat-index=".Length), NumberStyles.Integer, CultureInfo.InvariantCulture, out runnerArgs.RepeatIndex) ||
                        runnerArgs.RepeatIndex < 0 || runnerArgs.RepeatIndex > 1_000_000)
                    {
                        Console.Error.WriteLine("invalid_argument name=repeat-index value=" + arg);
                        return 2;
                    }
                }
                else if (arg.StartsWith("--recording-output=", StringComparison.Ordinal))
                {
                    if (++recordingCount != 1 || arg.Length == "--recording-output=".Length) return 2;
                    runnerArgs.RecordingPath = arg.Substring("--recording-output=".Length);
                    runnerArgs.RecordingMode = RecordingMode.On;
                }
                else if (arg.StartsWith("--output=", StringComparison.Ordinal))
                {
                    runnerArgs.OutputPath = arg.Substring("--output=".Length);
                }
                else if (arg.StartsWith("--step-timing-output=", StringComparison.Ordinal) &&
                    arg.Length > "--step-timing-output=".Length)
                {
                    runnerArgs.StepTimingOutputPath = arg.Substring("--step-timing-output=".Length);
                }
                else
                {
                    Console.Error.WriteLine("invalid_argument value=" + arg);
                    return 2;
                }
            }

            if (caseContractCount != 1 ||
                runnerArgs.CaseExecution.MeasuredWorkUnitCount > int.MaxValue ||
                runnerArgs.CaseExecution.WarmupWorkUnitCount > int.MaxValue ||
                ResolveUnityPhysicsCase(
                    runnerArgs.CaseExecution.FixtureKind,
                    out UnityPhysicsCaseRegistration registration) != 0)
            {
                return 2;
            }
            int query = runnerArgs.CaseExecution.FixtureKind == CaseFixtureKind.SpatialQueryTrace ? 1 : 0;
            if (stabilizationCount != (query != 0 ? 0 : 1)) return 2;
            runnerArgs.CaseRegistration = registration;
            CaseFixtureKind fixture = runnerArgs.CaseExecution.FixtureKind;
            int requiresStream = runnerArgs.VerificationMode == VerificationMode.On &&
                (fixture == CaseFixtureKind.OpenContainerFallingPile || fixture == CaseFixtureKind.BoxContactIslands ||
                 fixture == CaseFixtureKind.LargePyramid || fixture == CaseFixtureKind.PyramidWall) ? 1 : 0;
            if (requiresStream != 0 ? runnerArgs.StackStream == null : runnerArgs.StackStream != null) return 2;
            runnerArgs.StepCount = (int)runnerArgs.CaseExecution.MeasuredWorkUnitCount;
            runnerArgs.WarmupSteps = (int)runnerArgs.CaseExecution.WarmupWorkUnitCount;

            return 0;
        }
    }
}
