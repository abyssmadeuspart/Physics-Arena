using BepuPhysics;
using BepuUtilities;
using BepuUtilities.Memory;
using System.Diagnostics;

namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public enum BepuRunnerMode
{
    Headless = 0,
    SharedVisual = 1
}

public struct BepuPolygonRunner
{
    public static int Run(string[] args)
    {
        if (SelectRunnerMode(args) == BepuRunnerMode.SharedVisual)
        {
            return BepuSharedVisualMode.Run(args);
        }

        BepuRunnerArgs runnerArgs = BepuRunnerArgsParser.Default();
        int parseStatus = BepuRunnerArgsParser.Parse(args, ref runnerArgs);
        if (parseStatus != 0)
        {
            return parseStatus;
        }

        BufferPool bufferPool = new();
        Simulation simulation = default;
        ThreadDispatcher threadDispatcher = null;
        try
        {
            BepuCaseDescriptor caseDescriptor = runnerArgs.CaseDescriptor;
            int requestedWorkerCount = BepuCaseRegistry.RequestedWorkerCount(in caseDescriptor, runnerArgs.WorkerCount);
            if (runnerArgs.WarmupSteps > 0)
            {
                BepuCaseRegistry.RunWarmup(in caseDescriptor, bufferPool, requestedWorkerCount, runnerArgs.WarmupSteps);
            }

            simulation = BepuCaseRegistry.CreateSimulation(in caseDescriptor, bufferPool, requestedWorkerCount, out threadDispatcher);
            Stopwatch stopwatch = Stopwatch.StartNew();
            BepuCaseRegistry.StepSimulation(in caseDescriptor, simulation, threadDispatcher, runnerArgs.StepCount);
            stopwatch.Stop();
            return BepuResultWriter.Write(runnerArgs, simulation, stopwatch.Elapsed);
        }
        finally
        {
            simulation?.Dispose();
            threadDispatcher?.Dispose();
            bufferPool.Clear();
        }
    }

    public static BepuRunnerMode SelectRunnerMode(string[] args)
    {
        for (int index = 0; index < args.Length; ++index)
        {
            if (args[index].StartsWith("--producer-mode=", StringComparison.Ordinal))
            {
                return BepuRunnerMode.SharedVisual;
            }
        }

        return BepuRunnerMode.Headless;
    }
}
