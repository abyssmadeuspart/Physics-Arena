using System;
using System.Diagnostics;
using Unity.Collections;
using Unity.Entities;
using Unity.Jobs.LowLevel.Unsafe;
using Unity.Mathematics;
using Unity.Physics;
using UnityEngine;
using PhysicsCollider = Unity.Physics.Collider;

namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
    public enum UnityPhysicsRunnerMode
    {
        Headless = 0,
        SharedVisual = 1
    }

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
            if (SelectRunnerMode(args) == UnityPhysicsRunnerMode.SharedVisual)
            {
                return RunSharedVisualMode(args);
            }

            RunnerArgs runnerArgs = DefaultRunnerArgs();
            int parseStatus = ParseArgs(args, ref runnerArgs);
            if (parseStatus != 0)
            {
                return parseStatus;
            }

            int previousWorkerCount = JobsUtility.JobWorkerCount;
            PhysicsWorld world = default;
            BlobAssetReference<PhysicsCollider> dynamicCollider = default;
            NativeArray<BlobAssetReference<PhysicsCollider>> staticColliders = default;
            NativeReference<int> staticBodiesChanged = default;
            Simulation simulation = default;

            try
            {
                int requestedWorkerCount = math.max(0, runnerArgs.ThreadCount - 1);
                JobsUtility.JobWorkerCount = requestedWorkerCount;
                int effectiveWorkerCount = JobsUtility.JobWorkerCount;
                int effectiveThreadCount = effectiveWorkerCount + 1;

                if (runnerArgs.WarmupSteps > 0)
                {
                    RunWarmup(runnerArgs.WarmupSteps, runnerArgs.ThreadCount > 1);
                }

                UnityPhysicsCaseDescriptor caseDescriptor = runnerArgs.CaseDescriptor;
                world = new PhysicsWorld(caseDescriptor.StaticBodyCount, caseDescriptor.DynamicBodyCount, 0);
                dynamicCollider = CreateBoxCollider(new float3(1.0f, 1.0f, 1.0f));
                staticColliders = new NativeArray<BlobAssetReference<PhysicsCollider>>(caseDescriptor.StaticBodyCount, Allocator.Persistent);
                CreateFixture(ref world, dynamicCollider, staticColliders);
                world.UpdateIndexMaps();

                staticBodiesChanged = new NativeReference<int>(Allocator.Persistent);
                staticBodiesChanged.Value = 1;
                simulation = Simulation.Create();

                Stopwatch stopwatch = Stopwatch.StartNew();
                for (int step = 0; step < runnerArgs.StepCount; ++step)
                {
                    StepWorld(ref world, ref simulation, staticBodiesChanged, runnerArgs.ThreadCount > 1);
                    staticBodiesChanged.Value = 0;
                }
                stopwatch.Stop();

                return WriteResult(runnerArgs, in caseDescriptor, ref world, stopwatch.Elapsed.TotalMilliseconds, effectiveWorkerCount, effectiveThreadCount);
            }
            finally
            {
                JobsUtility.JobWorkerCount = previousWorkerCount;
                DisposeCaseResources(ref world, ref simulation, ref dynamicCollider, ref staticColliders, ref staticBodiesChanged);
            }
        }

        private static UnityPhysicsRunnerMode SelectRunnerMode(string[] args)
        {
            for (int index = 0; index < args.Length; ++index)
            {
                if (args[index] == "--producer-mode=shared-visual")
                {
                    return UnityPhysicsRunnerMode.SharedVisual;
                }
            }

            return UnityPhysicsRunnerMode.Headless;
        }
    }
}
