using BepuPhysics;
using BepuPhysics.Collidables;
using BepuPhysics.Constraints;
using BepuUtilities;
using BepuUtilities.Memory;
using System.Diagnostics;
using System.Numerics;

namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public static class BepuBoxContactIslandsCase
{
    public static BepuCaseRegistration Registration()
    {
        return new BepuCaseRegistration
        {
            Descriptor = Descriptor(),
            BuildVisualScene = BuildVisualScene,
            SampleVisualTransforms = SampleVisualTransforms,
            RunHeadless = RunHeadless
        };
    }

    public static BepuCaseDescriptor Descriptor()
    {
        return new BepuCaseDescriptor
        {
            EngineId = BepuCaseRegistry.EngineId
        };
    }

    public static int SampleVisualTransforms(
        BepuCaseView state, BepuVisualStableTransform[] transforms)
    {
        return SampleTransforms(state.Simulation, state.DynamicBodies, transforms);
    }

    public static float IslandOrigin(float spacing, int coordinate, int count)
    {
        return spacing * (coordinate - 0.5f * (count - 1));
    }

    public static int CreateSimulation(
        BufferPool bufferPool,
        int workerCount,
        in CaseExecutionSpec execution,
        out Simulation simulation,
        out ThreadDispatcher threadDispatcher,
        out BodyHandle[] dynamicBodies)
    {
        CaseExecutionContactIslands fixture = execution.ContactIslands;
        threadDispatcher = null;
        dynamicBodies = null;
        simulation = Simulation.Create(
            bufferPool,
            new PolygonNarrowPhaseCallbacks(new SpringSettings(30, 1), 2f, execution.Friction),
            new PolygonPoseIntegratorCallbacks(
                new Vector3(execution.Gravity.X, execution.Gravity.Y, execution.Gravity.Z), 0, 0),
            new SolveDescription((int)execution.VelocityIterations, (int)execution.Substeps));
        simulation.Deterministic = true;
        TypedIndex floorShape = simulation.Shapes.Add(new Box(
            fixture.FloorHalfExtents.X * 2f, fixture.FloorHalfExtents.Y * 2f,
            fixture.FloorHalfExtents.Z * 2f));
        for (int groupZ = 0; groupZ < fixture.IslandGrid[1]; ++groupZ)
        {
            for (int groupX = 0; groupX < fixture.IslandGrid[0]; ++groupX)
            {
                simulation.Statics.Add(new StaticDescription(
                    new Vector3(
                        IslandOrigin(fixture.IslandSpacing[0], groupX, (int)fixture.IslandGrid[0]),
                        -fixture.FloorHalfExtents.Y,
                        IslandOrigin(fixture.IslandSpacing[1], groupZ, (int)fixture.IslandGrid[1])),
                    floorShape));
            }
        }
        BepuResolvedShape shape = BepuCaseRegistry.AddResolvedShape(simulation, bufferPool,
            in execution, in execution.SelectedGeometry);
        BodyInertia inertia = BepuCaseRegistry.ShapeInertia(simulation, shape.Index, fixture.Density * shape.Volume);
        Quaternion shapeRotation = BepuCaseRegistry.ShapeRotation(execution.SelectedGeometry.Axis);
        BodyActivityDescription activity = execution.SleepMode == CaseExecutionToggle.Enabled
            ? new BodyActivityDescription(0.01f) : new BodyActivityDescription(-1f);
        dynamicBodies = new BodyHandle[execution.DynamicBodyCount];
        int slot = 0;
        for (int groupZ = 0; groupZ < fixture.IslandGrid[1]; ++groupZ)
        {
            for (int groupX = 0; groupX < fixture.IslandGrid[0]; ++groupX)
            {
                float originX = IslandOrigin(
                    fixture.IslandSpacing[0], groupX, (int)fixture.IslandGrid[0]);
                float originZ = IslandOrigin(
                    fixture.IslandSpacing[1], groupZ, (int)fixture.IslandGrid[1]);
                for (int y = 0; y < fixture.BodyGrid[1]; ++y)
                {
                    for (int z = 0; z < fixture.BodyGrid[2]; ++z)
                    {
                        for (int x = 0; x < fixture.BodyGrid[0]; ++x)
                        {
                            Vector3 position = new(
                                originX + (x - 0.5f * (fixture.BodyGrid[0] - 1)) * fixture.BodySpacing.X,
                                fixture.BodyInitialY + y * fixture.BodySpacing.Y,
                                originZ + (z - 0.5f * (fixture.BodyGrid[2] - 1)) * fixture.BodySpacing.Z);
                            BodyDescription description = BodyDescription.CreateDynamic(
                                new RigidPose(position + Vector3.Transform(shape.Center, shapeRotation), shapeRotation),
                                inertia, shape.Index, activity);
                            if (execution.ContinuousCollisionMode == CaseExecutionToggle.Enabled)
                                description.Collidable.Continuity = ContinuousDetection.Continuous(1e-3f, 1e-3f);
                            dynamicBodies[slot++] = simulation.Bodies.Add(description);
                        }
                    }
                }
            }
        }
        if (slot != execution.DynamicBodyCount)
        {
            return 2;
        }
        threadDispatcher = workerCount > 0 ? new ThreadDispatcher(workerCount) : null;
        return 0;
    }

    public static void StepSimulation(Simulation simulation, ThreadDispatcher dispatcher,
        uint timestepHz, int workUnitCount)
    {
        float timestepDuration = 1f / timestepHz;
        if (dispatcher == null)
        {
            for (int workUnit = 0; workUnit < workUnitCount; ++workUnit)
            {
                simulation.Timestep(timestepDuration);
            }
            return;
        }
        for (int workUnit = 0; workUnit < workUnitCount; ++workUnit)
        {
            simulation.Timestep(timestepDuration, dispatcher);
        }
    }

    public static int SampleTransforms(
        Simulation simulation,
        BodyHandle[] bodies,
        BepuVisualStableTransform[] transforms)
    {
        if (bodies == null || transforms == null || transforms.Length < bodies.Length) return 2;
        for (int slot = 0; slot < bodies.Length; ++slot)
        {
            BodyReference body = simulation.Bodies.GetBodyReference(bodies[slot]);
            RigidPose pose = body.Pose;
            transforms[slot] = new BepuVisualStableTransform
            {
                StableSlot = (uint)slot,
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
        CaseExecutionContactIslands fixture = execution.ContactIslands;
        if (geometries == null || geometries.Length < 2 ||
            instances == null || instances.Length < execution.BodyCount) return 2;
        if (BepuCaseRegistry.BuildResolvedVisualGeometry(in execution, in execution.SelectedGeometry,
            ref meshes, out geometries[0]) != 0) return 2;
        BepuCaseRegistry.OffsetHullVisualGeometry(state.Simulation,
            state.Simulation.Bodies.GetBodyReference(new BodyHandle(0)).Collidable.Shape,
            in execution.SelectedGeometry, ref meshes, in geometries[0]);
        geometries[1] = new BepuVisualGeometry
        {
            Kind = 2, ParameterX = fixture.FloorHalfExtents.X,
            ParameterY = fixture.FloorHalfExtents.Y, ParameterZ = fixture.FloorHalfExtents.Z
        };
        BepuVisualStableTransform[] transforms =
            new BepuVisualStableTransform[execution.DynamicBodyCount];
        if (SampleTransforms(state.Simulation, state.DynamicBodies, transforms) != 0) return 2;
        for (int slot = 0; slot < execution.DynamicBodyCount; ++slot)
        {
            instances[slot] = new BepuVisualInstance
            {
                GeometryIndex = 0,
                StableSlot = (uint)slot,
                TransformSlot = (uint)slot,
                InitialTransform = transforms[slot].Transform
            };
        }
        int index = 0;
        for (int groupZ = 0; groupZ < fixture.IslandGrid[1]; ++groupZ)
        {
            for (int groupX = 0; groupX < fixture.IslandGrid[0]; ++groupX)
            {
                instances[execution.DynamicBodyCount + index] = new BepuVisualInstance
                {
                    GeometryIndex = 1,
                    StableSlot = execution.DynamicBodyCount + (uint)index,
                    TransformSlot = uint.MaxValue,
                    InitialTransform = new BepuVisualTransform
                    {
                        PositionX = IslandOrigin(
                            fixture.IslandSpacing[0], groupX, (int)fixture.IslandGrid[0]),
                        PositionY = -fixture.FloorHalfExtents.Y,
                        PositionZ = IslandOrigin(
                            fixture.IslandSpacing[1], groupZ, (int)fixture.IslandGrid[1]),
                        RotationW = 1f
                    }
                };
                index += 1;
            }
        }
        geometryCount = 2;
        instanceCount = (int)execution.BodyCount;
        return 0;
    }

    public static ulong CountInvalidTransforms(Simulation simulation, BodyHandle[] bodies)
    {
        ulong invalidTransformCount = 0;
        for (int slot = 0; slot < bodies.Length; ++slot)
        {
            RigidPose pose = simulation.Bodies.GetBodyReference(bodies[slot]).Pose;
            Vector3 position = pose.Position;
            Quaternion rotation = pose.Orientation;
            if (!float.IsFinite(position.X) || !float.IsFinite(position.Y) || !float.IsFinite(position.Z) ||
                !float.IsFinite(rotation.X) || !float.IsFinite(rotation.Y) || !float.IsFinite(rotation.Z) ||
                !float.IsFinite(rotation.W)) invalidTransformCount += 1;
        }
        return invalidTransformCount;
    }

    public static string VisualPhysicsSettings(
        in CaseExecutionSpec execution, int threadCount)
    {
        uint islandCount = execution.ContactIslands.IslandGrid[0] *
            execution.ContactIslands.IslandGrid[1];
        return string.Create(
            System.Globalization.CultureInfo.InvariantCulture,
            $"velocity_iterations={execution.VelocityIterations}; substeps={execution.Substeps}; linear_damping=0; angular_damping=0; sleep={(execution.SleepMode == CaseExecutionToggle.Enabled ? "enabled" : "disabled")}; ccd={(execution.ContinuousCollisionMode == CaseExecutionToggle.Enabled ? "enabled" : "disabled")}; deterministic=yes; worker_count={threadCount}; islands={islandCount}");
    }

    public static int RunHeadless(BepuRunnerArgs runnerArgs)
    {
        BufferPool bufferPool = new();
        Simulation simulation = default;
        ThreadDispatcher threadDispatcher = null;
        BepuRecordingWriter recording = default;
        StackCapture capture = default;
        try
        {
            int createStatus = CreateSimulation(
                bufferPool,
                runnerArgs.WorkerCount,
                in runnerArgs.CaseExecution,
                out simulation,
                out threadDispatcher,
                out BodyHandle[] dynamicBodies);
            if (createStatus != 0)
            {
                return createStatus;
            }
            BepuCaseView recordingView = new()
            {
                CaseExecution = runnerArgs.CaseExecution, Simulation = simulation, DynamicBodies = dynamicBodies
            };
            if (runnerArgs.VerificationMode == VerificationMode.On && StackStateCapture.Open(in runnerArgs, out capture) != 0) return 2;
            for (int ordinal = 0; ordinal <= runnerArgs.WarmupSteps; ++ordinal)
            {
                if (ordinal != 0) StepSimulation(simulation, threadDispatcher, runnerArgs.CaseExecution.TimestepHz, 1);
                if (runnerArgs.VerificationMode == VerificationMode.On && StackStateCapture.Append(ref capture, in runnerArgs.CaseRegistration, recordingView,
                    ordinal == 0 ? StackCapturePhase.Construction : StackCapturePhase.Warmup, 0, (uint)ordinal) != 0) return 2;
            }
            if (runnerArgs.RecordingMode == RecordingMode.On && BepuRecording.Begin(in runnerArgs, in recordingView, out recording) != 0) return 2;
            long[] rawWorkUnitDurations = new long[runnerArgs.StepCount];
            for (int workUnit = 0; workUnit < rawWorkUnitDurations.Length; ++workUnit)
            {
                long start = Stopwatch.GetTimestamp();
                StepSimulation(simulation, threadDispatcher,
                    runnerArgs.CaseExecution.TimestepHz, 1);
                rawWorkUnitDurations[workUnit] = Stopwatch.GetTimestamp() - start;
                if (runnerArgs.VerificationMode == VerificationMode.On && StackStateCapture.Append(ref capture, in runnerArgs.CaseRegistration, recordingView,
                    StackCapturePhase.Measured, 0, (uint)workUnit + 1) != 0) return 2;
                if (runnerArgs.RecordingMode == RecordingMode.On && BepuRecording.Append(ref recording, in runnerArgs.CaseRegistration, recordingView, (ulong)(workUnit + 1)) != 0) return 2;
            }

            if (runnerArgs.VerificationMode == VerificationMode.On && StackStateCapture.Close(ref capture) != 0) return 2;
            if (runnerArgs.RecordingMode == RecordingMode.On && BepuRecording.Complete(ref recording) != 0) return 2;
            double workloadElapsedMilliseconds = 0.0;
            for (int workUnit = 0; workUnit < rawWorkUnitDurations.Length; ++workUnit)
            {
                workloadElapsedMilliseconds += rawWorkUnitDurations[workUnit] * 1000.0 / Stopwatch.Frequency;
            }
            ulong invalidTransformCount = CountInvalidTransforms(simulation, dynamicBodies);
            BepuResultValidity caseValidity = dynamicBodies.Length == runnerArgs.CaseExecution.DynamicBodyCount &&
                invalidTransformCount == 0 ?
                BepuResultValidity.Valid : BepuResultValidity.Invalid;
            BepuResultValidity metricValidity =
                runnerArgs.StepCount == runnerArgs.CaseExecution.MeasuredWorkUnitCount &&
                workloadElapsedMilliseconds > 0.0 && double.IsFinite(workloadElapsedMilliseconds) ?
                BepuResultValidity.Valid : BepuResultValidity.Invalid;
            BepuBenchmarkResult result = new()
            {
                PhysicsSettings = VisualPhysicsSettings(
                    in runnerArgs.CaseExecution, runnerArgs.WorkerCount),
                BodyCount = (int)runnerArgs.CaseExecution.BodyCount,
                ShapeCount = (int)runnerArgs.CaseExecution.ShapeCount,
                QueryCount = (int)runnerArgs.CaseExecution.QueryCount,
                ConstraintCount = (int)runnerArgs.CaseExecution.ConstraintCount,
                InvalidTransformCount = invalidTransformCount,
                EffectiveThreadCount = runnerArgs.WorkerCount,
                EffectiveWorkerCount = runnerArgs.WorkerCount,
                CompletedWorkUnitCount = runnerArgs.StepCount,
                WorkloadElapsedMilliseconds = workloadElapsedMilliseconds,
                CaseValidity = caseValidity,
                MetricValidity = metricValidity
            };
            int resultStatus = BepuResultWriter.WriteResult(runnerArgs, in result);
            return resultStatus == 0 ? BepuResultWriter.WriteStepTiming(runnerArgs, rawWorkUnitDurations) : resultStatus;
        }
        finally
        {
            StackStateCapture.Abort(ref capture);
            if (runnerArgs.RecordingMode == RecordingMode.On) BepuRecording.Abort(ref recording);
            simulation?.Dispose();
            threadDispatcher?.Dispose();
            bufferPool.Clear();
        }
    }
}
