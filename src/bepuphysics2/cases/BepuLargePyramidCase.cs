using BepuPhysics;
using BepuPhysics.Collidables;
using BepuPhysics.CollisionDetection;
using BepuPhysics.Constraints;
using BepuUtilities;
using BepuUtilities.Memory;
using System.Diagnostics;
using System.Numerics;

namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public static class BepuLargePyramidCase
{
    public static BepuCaseRegistration Registration()
    {
        return new BepuCaseRegistration
        {
            Descriptor = new BepuCaseDescriptor { EngineId = BepuCaseRegistry.EngineId },
            BuildVisualScene = BuildVisualScene,
            SampleVisualTransforms = SampleVisualTransforms,
            RunHeadless = RunHeadless
        };
    }

    public static int RequestedWorkerCount(int threadCount)
    {
        return threadCount;
    }

    public static Simulation CreateSimulation(
        BufferPool bufferPool,
        int workerCount,
        in CaseExecutionSpec execution,
        out ThreadDispatcher threadDispatcher,
        out BodyHandle[] dynamicBodies)
    {
        CaseExecutionLargePyramid fixture = execution.LargePyramid;
        Simulation simulation = Simulation.Create(
            bufferPool,
            new PolygonNarrowPhaseCallbacks(new SpringSettings(30, 1), 2f, execution.Friction),
            new PolygonPoseIntegratorCallbacks(
                new Vector3(execution.Gravity.X, execution.Gravity.Y, execution.Gravity.Z), 0, 0),
            new SolveDescription((int)execution.VelocityIterations, (int)execution.Substeps));
        simulation.Deterministic = true;
        dynamicBodies = new BodyHandle[execution.DynamicBodyCount];
        BepuResolvedShape shape = BepuCaseRegistry.AddResolvedShape(simulation, bufferPool,
            in execution, in execution.SelectedGeometry);
        BodyInertia inertia = BepuCaseRegistry.ShapeInertia(simulation, shape.Index, fixture.BoxDensity * shape.Volume);
        Quaternion shapeRotation = BepuCaseRegistry.ShapeRotation(execution.SelectedGeometry.Axis);
        BodyActivityDescription activity = execution.SleepMode == CaseExecutionToggle.Enabled
            ? new BodyActivityDescription(0.01f) : new BodyActivityDescription(-1f);
        int bodyIndex = 0;
        for (int layer = 0; layer < fixture.RowCount; ++layer)
        {
            int layerSide = (int)fixture.RowCount - layer;
            for (int depth = 0; depth < layerSide; ++depth)
            {
                for (int column = 0; column < layerSide; ++column)
                {
                    Vector3 position = new(
                        fixture.BaseCenter.X +
                            (column - 0.5f * (layerSide - 1)) * fixture.BoxSpacing.X,
                        fixture.BaseCenter.Y + layer * fixture.BoxSpacing.Y,
                        fixture.BaseCenter.Z +
                            (depth - 0.5f * (layerSide - 1)) * fixture.BoxSpacing.Z);
                    BodyDescription description = BodyDescription.CreateDynamic(
                        new RigidPose(position + Vector3.Transform(shape.Center, shapeRotation), shapeRotation),
                        inertia, shape.Index, activity);
                    if (execution.ContinuousCollisionMode == CaseExecutionToggle.Enabled)
                        description.Collidable.Continuity = ContinuousDetection.Continuous(1e-3f, 1e-3f);
                    dynamicBodies[bodyIndex++] = simulation.Bodies.Add(description);
                }
            }
        }
        Sphere projectile = new(fixture.ProjectileRadius);
        TypedIndex projectileShape = simulation.Shapes.Add(projectile);
        float projectileVolume = 4f / 3f * MathF.PI * fixture.ProjectileRadius *
            fixture.ProjectileRadius * fixture.ProjectileRadius;
        BodyInertia projectileInertia = projectile.ComputeInertia(
            fixture.ProjectileDensity * projectileVolume);
        for (int index = 0; index < (int)fixture.ProjectileCount; ++index)
        {
            BodyDescription projectileDescription = BodyDescription.CreateDynamic(
                new Vector3(
                    fixture.ProjectileInitialCenter.X + index * fixture.ProjectileCenterSpacing.X,
                    fixture.ProjectileInitialCenter.Y + index * fixture.ProjectileCenterSpacing.Y,
                    fixture.ProjectileInitialCenter.Z + index * fixture.ProjectileCenterSpacing.Z),
                projectileInertia, projectileShape, activity);
            if (execution.ContinuousCollisionMode == CaseExecutionToggle.Enabled)
                projectileDescription.Collidable.Continuity = ContinuousDetection.Continuous(1e-3f, 1e-3f);
            dynamicBodies[bodyIndex++] = simulation.Bodies.Add(projectileDescription);
        }

        Box floor = new(fixture.FloorHalfExtents.X * 2f,
            fixture.FloorHalfExtents.Y * 2f, fixture.FloorHalfExtents.Z * 2f);
        TypedIndex floorShape = simulation.Shapes.Add(floor);
        simulation.Statics.Add(new StaticDescription(
            new Vector3(0f, -fixture.FloorHalfExtents.Y, 0f), floorShape));
        if (bodyIndex != execution.DynamicBodyCount)
            throw new InvalidOperationException($"Unexpected dynamic body count: {bodyIndex}");
        threadDispatcher = workerCount > 0 ? new ThreadDispatcher(workerCount) : null;
        return simulation;
    }

    public static void StepSimulation(
        Simulation simulation, ThreadDispatcher dispatcher, uint timestepHz, int stepCount)
    {
        float timestep = 1f / timestepHz;
        for (int step = 0; step < stepCount; ++step)
        {
            if (dispatcher == null) simulation.Timestep(timestep);
            else simulation.Timestep(timestep, dispatcher);
        }
    }

    public static int StepSimulationTimed(
        Simulation simulation,
        ThreadDispatcher dispatcher,
        in CaseExecutionSpec execution,
        BodyHandle[] dynamicBodies,
        int firstStep,
        long[] durations,
        int durationOffset,
        int stepCount)
    {
        float timestep = 1f / execution.TimestepHz;
        CaseExecutionLargePyramid fixture = execution.LargePyramid;
        for (int step = 0; step < stepCount; ++step)
        {
            if (firstStep + step == (int)fixture.ProjectileLaunchAfterWorkUnits)
            {
                int firstProjectile = dynamicBodies.Length - (int)fixture.ProjectileCount;
                for (int index = firstProjectile; index < dynamicBodies.Length; ++index)
                {
                    BodyReference projectile = simulation.Bodies.GetBodyReference(dynamicBodies[index]);
                    projectile.Awake = true;
                    projectile.Velocity.Linear = new Vector3(fixture.ProjectileLaunchVelocity.X,
                        fixture.ProjectileLaunchVelocity.Y, fixture.ProjectileLaunchVelocity.Z);
                }
            }
            long start = Stopwatch.GetTimestamp();
            if (dispatcher == null) simulation.Timestep(timestep);
            else simulation.Timestep(timestep, dispatcher);
            durations[durationOffset + step] = Stopwatch.GetTimestamp() - start;
        }
        return 0;
    }

    public static int SampleTransforms(
        Simulation simulation, BodyHandle[] bodies, BepuVisualStableTransform[] transforms)
    {
        if (bodies == null || transforms == null || transforms.Length < bodies.Length) return 2;
        for (int index = 0; index < bodies.Length; ++index)
        {
            RigidPose pose = simulation.Bodies.GetBodyReference(bodies[index]).Pose;
            transforms[index] = new BepuVisualStableTransform
            {
                StableSlot = (uint)index,
                Transform = new BepuVisualTransform
                {
                    PositionX = pose.Position.X,
                    PositionY = pose.Position.Y,
                    PositionZ = pose.Position.Z,
                    RotationX = pose.Orientation.X,
                    RotationY = pose.Orientation.Y,
                    RotationZ = pose.Orientation.Z,
                    RotationW = pose.Orientation.W
                }
            };
        }
        return 0;
    }

    public static ulong CountSimulationInvalidTransforms(Simulation simulation, BodyHandle[] bodies)
    {
        ulong count = 0;
        for (int index = 0; index < bodies.Length; ++index)
        {
            RigidPose pose = simulation.Bodies.GetBodyReference(bodies[index]).Pose;
            if (!float.IsFinite(pose.Position.X) || !float.IsFinite(pose.Position.Y) ||
                !float.IsFinite(pose.Position.Z) || !float.IsFinite(pose.Orientation.X) ||
                !float.IsFinite(pose.Orientation.Y) || !float.IsFinite(pose.Orientation.Z) ||
                !float.IsFinite(pose.Orientation.W)) count += 1;
        }
        return count;
    }

    public static int SampleVisualTransforms(
        BepuCaseView state, BepuVisualStableTransform[] transforms)
    {
        return SampleTransforms(state.Simulation, state.DynamicBodies, transforms);
    }

    public static int BuildVisualScene(
        BepuCaseView state,
        BepuVisualGeometry[] geometries,

        ref BepuVisualMeshStorage meshes,
        BepuVisualInstance[] instances,
        out int geometryCount,
        out int instanceCount)
    {
        geometryCount = 0;
        instanceCount = 0;
        CaseExecutionSpec execution = state.CaseExecution;
        CaseExecutionLargePyramid fixture = execution.LargePyramid;
        if (geometries == null || geometries.Length < 3 || instances == null ||
            instances.Length < execution.VisualInstanceCount) return 2;
        if (BepuCaseRegistry.BuildResolvedVisualGeometry(in execution, in execution.SelectedGeometry,
            ref meshes, out geometries[0]) != 0) return 2;
        BepuCaseRegistry.OffsetHullVisualGeometry(state.Simulation,
            state.Simulation.Bodies.GetBodyReference(new BodyHandle(0)).Collidable.Shape,
            in execution.SelectedGeometry, ref meshes, in geometries[0]);
        geometries[1] = new BepuVisualGeometry { Kind = 1,
            ParameterX = fixture.ProjectileRadius };
        geometries[2] = new BepuVisualGeometry { Kind = 2,
            ParameterX = fixture.FloorHalfExtents.X, ParameterY = fixture.FloorHalfExtents.Y,
            ParameterZ = fixture.FloorHalfExtents.Z };
        int firstProjectile = state.DynamicBodies.Length - (int)fixture.ProjectileCount;
        for (int index = 0; index < state.DynamicBodies.Length; ++index)
        {
            RigidPose pose = state.Simulation.Bodies.GetBodyReference(state.DynamicBodies[index]).Pose;
            instances[index] = new BepuVisualInstance
            {
                GeometryIndex = index >= firstProjectile ? 1u : 0u,
                StableSlot = (uint)index,
                TransformSlot = (uint)index,
                InitialTransform = new BepuVisualTransform
                {
                    PositionX = pose.Position.X,
                    PositionY = pose.Position.Y,
                    PositionZ = pose.Position.Z,
                    RotationX = pose.Orientation.X,
                    RotationY = pose.Orientation.Y,
                    RotationZ = pose.Orientation.Z,
                    RotationW = pose.Orientation.W
                }
            };
        }
        int floorSlot = state.DynamicBodies.Length;
        instances[floorSlot] = new BepuVisualInstance
        {
            GeometryIndex = 2,
            StableSlot = (uint)floorSlot,
            TransformSlot = uint.MaxValue,
            InitialTransform = new BepuVisualTransform
            {
                PositionY = -fixture.FloorHalfExtents.Y,
                RotationW = 1f
            }
        };
        geometryCount = 3;
        instanceCount = (int)execution.VisualInstanceCount;
        return 0;
    }

    public static string VisualPhysicsSettings(in CaseExecutionSpec execution, int threadCount)
    {
        return string.Create(System.Globalization.CultureInfo.InvariantCulture,
            $"velocity_iterations={execution.VelocityIterations}; substeps={execution.Substeps}; linear_damping=0; angular_damping=0; sleep={(execution.SleepMode == CaseExecutionToggle.Enabled ? "enabled" : "disabled")}; ccd={(execution.ContinuousCollisionMode == CaseExecutionToggle.Enabled ? "enabled" : "disabled")}; deterministic=yes; worker_count={threadCount}");
    }

    public static int RunHeadless(BepuRunnerArgs runnerArgs)
    {
        BufferPool bufferPool = new();
        Simulation simulation = default;
        ThreadDispatcher dispatcher = null;
        BepuRecordingWriter recording = default;
        StackCapture capture = default;
        try
        {
            int workerCount = RequestedWorkerCount(runnerArgs.WorkerCount);
            simulation = CreateSimulation(bufferPool, workerCount, in runnerArgs.CaseExecution,
                out dispatcher, out BodyHandle[] bodies);
            long[] durations = new long[runnerArgs.StepCount];
            BepuCaseView recordingView = new()
            {
                CaseExecution = runnerArgs.CaseExecution, Simulation = simulation, DynamicBodies = bodies
            };
            if (runnerArgs.VerificationMode == VerificationMode.On && StackStateCapture.Open(in runnerArgs, out capture) != 0) return 2;
            for (int ordinal = 0; ordinal <= runnerArgs.WarmupSteps; ++ordinal)
            {
                if (ordinal != 0) StepSimulation(simulation, dispatcher, runnerArgs.CaseExecution.TimestepHz, 1);
                if (runnerArgs.VerificationMode == VerificationMode.On && StackStateCapture.Append(ref capture, in runnerArgs.CaseRegistration, recordingView,
                    ordinal == 0 ? StackCapturePhase.Construction : StackCapturePhase.Warmup, 0, (uint)ordinal) != 0) return 2;
            }
            if (runnerArgs.RecordingMode == RecordingMode.On && BepuRecording.Begin(in runnerArgs, in recordingView, out recording) != 0) return 2;
            for (int step = 0; step < runnerArgs.StepCount; ++step)
            {
                StepSimulationTimed(simulation, dispatcher, in runnerArgs.CaseExecution,
                    bodies, step, durations, step, 1);
                if (runnerArgs.VerificationMode == VerificationMode.On && StackStateCapture.Append(ref capture, in runnerArgs.CaseRegistration, recordingView,
                    StackCapturePhase.Measured, 0, (uint)step + 1) != 0) return 2;
                if (runnerArgs.RecordingMode == RecordingMode.On && BepuRecording.Append(ref recording, in runnerArgs.CaseRegistration, recordingView, (ulong)(step + 1)) != 0) return 2;
            }
            if (runnerArgs.VerificationMode == VerificationMode.On && StackStateCapture.Close(ref capture) != 0) return 2;
            if (runnerArgs.RecordingMode == RecordingMode.On && BepuRecording.Complete(ref recording) != 0) return 2;

            double elapsedMilliseconds = 0.0;
            for (int index = 0; index < durations.Length; ++index)
                elapsedMilliseconds += durations[index] * 1000.0 / Stopwatch.Frequency;
            ulong invalidCount = CountSimulationInvalidTransforms(simulation, bodies);
            BepuBenchmarkResult result = new()
            {
                PhysicsSettings = VisualPhysicsSettings(in runnerArgs.CaseExecution, workerCount),
                BodyCount = (int)runnerArgs.CaseExecution.BodyCount,
                ShapeCount = (int)runnerArgs.CaseExecution.ShapeCount,
                QueryCount = 0,
                ConstraintCount = 0,
                InvalidTransformCount = invalidCount,
                EffectiveThreadCount = runnerArgs.WorkerCount,
                EffectiveWorkerCount = workerCount,
                CompletedWorkUnitCount = runnerArgs.StepCount,
                WorkloadElapsedMilliseconds = elapsedMilliseconds,
                CaseValidity = invalidCount == 0 && bodies.Length == runnerArgs.CaseExecution.DynamicBodyCount
                    ? BepuResultValidity.Valid : BepuResultValidity.Invalid,
                MetricValidity = elapsedMilliseconds > 0.0 && double.IsFinite(elapsedMilliseconds)
                    ? BepuResultValidity.Valid : BepuResultValidity.Invalid,
                Observations = Array.Empty<BepuObservationRow>()
            };
            if (BepuResultWriter.WriteResult(runnerArgs, in result) != 0) return 2;
            return BepuResultWriter.WriteStepTiming(runnerArgs, durations);
        }
        finally
        {
            StackStateCapture.Abort(ref capture);
            if (runnerArgs.RecordingMode == RecordingMode.On) BepuRecording.Abort(ref recording);
            simulation?.Dispose();
            dispatcher?.Dispose();
            bufferPool.Clear();
        }
    }
}
