using System;
using UnityEngine;

namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
    public static partial class UnityPhysicsBenchmarkRunner
    {
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterAssembliesLoaded)]
        public static void RunFromCommandLine()
        {
            int status = Run(Environment.GetCommandLineArgs());
            Application.Quit(status);
        }

        public static int Run(string[] args)
        {
            int parseStatus = ParseArgs(args, out RunnerArgs runnerArgs);
            if (parseStatus != 0)
            {
                return parseStatus;
            }
            return RunRegisteredHeadless(runnerArgs);
        }

    }
}
