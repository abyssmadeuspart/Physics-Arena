using System;
using System.Globalization;

namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
    public static partial class UnityPhysicsBenchmarkRunner
    {
        private static RunnerArgs DefaultRunnerArgs()
        {
            UnityPhysicsCaseDescriptor defaultDescriptor = UnityPhysicsBoxContainerPileDescriptor();
            return new RunnerArgs
            {
                CaseId = defaultDescriptor.CaseId,
                OutputPath = "polygon_results.csv",
                CaseDescriptor = defaultDescriptor,
                ThreadCount = 1,
                StepCount = 300,
                WarmupSteps = 0,
                RepeatIndex = 0
            };
        }

        private static int ParseArgs(string[] args, ref RunnerArgs runnerArgs)
        {
            int delimiterIndex = Array.IndexOf(args, "--");
            if (delimiterIndex < 0)
            {
                Console.Error.WriteLine("invalid_argument name=delimiter value=missing");
                return 2;
            }

            for (int index = delimiterIndex + 1; index < args.Length; ++index)
            {
                string arg = args[index];
                if (arg.StartsWith("--case=", StringComparison.Ordinal))
                {
                    runnerArgs.CaseId = arg.Substring("--case=".Length);
                }
                else if (arg.StartsWith("--thread-count=", StringComparison.Ordinal))
                {
                    if (!int.TryParse(arg.Substring("--thread-count=".Length), NumberStyles.Integer, CultureInfo.InvariantCulture, out runnerArgs.ThreadCount) ||
                        runnerArgs.ThreadCount < 1)
                    {
                        Console.Error.WriteLine("invalid_argument name=thread-count value=" + arg);
                        return 2;
                    }
                }
                else if (arg.StartsWith("--step-count=", StringComparison.Ordinal))
                {
                    if (!int.TryParse(arg.Substring("--step-count=".Length), NumberStyles.Integer, CultureInfo.InvariantCulture, out runnerArgs.StepCount) ||
                        runnerArgs.StepCount < 1)
                    {
                        Console.Error.WriteLine("invalid_argument name=step-count value=" + arg);
                        return 2;
                    }
                }
                else if (arg.StartsWith("--warmup-steps=", StringComparison.Ordinal))
                {
                    if (!int.TryParse(arg.Substring("--warmup-steps=".Length), NumberStyles.Integer, CultureInfo.InvariantCulture, out runnerArgs.WarmupSteps) ||
                        runnerArgs.WarmupSteps < 0)
                    {
                        Console.Error.WriteLine("invalid_argument name=warmup-steps value=" + arg);
                        return 2;
                    }
                }
                else if (arg.StartsWith("--repeat-index=", StringComparison.Ordinal))
                {
                    if (!int.TryParse(arg.Substring("--repeat-index=".Length), NumberStyles.Integer, CultureInfo.InvariantCulture, out runnerArgs.RepeatIndex) ||
                        runnerArgs.RepeatIndex < 0)
                    {
                        Console.Error.WriteLine("invalid_argument name=repeat-index value=" + arg);
                        return 2;
                    }
                }
                else if (arg.StartsWith("--output=", StringComparison.Ordinal))
                {
                    runnerArgs.OutputPath = arg.Substring("--output=".Length);
                }
                else
                {
                    Console.Error.WriteLine("invalid_argument value=" + arg);
                    return 2;
                }
            }

            if (ResolveUnityPhysicsCase(runnerArgs.CaseId, out UnityPhysicsCaseDescriptor descriptor) != 0)
            {
                return 2;
            }
            runnerArgs.CaseDescriptor = descriptor;

            return 0;
        }
    }
}
