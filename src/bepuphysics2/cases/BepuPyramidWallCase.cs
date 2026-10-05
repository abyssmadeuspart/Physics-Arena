using BepuPhysics;
using BepuPhysics.Collidables;
using BepuPhysics.CollisionDetection;
using BepuPhysics.Constraints;
using BepuUtilities;
using BepuUtilities.Memory;
using System.Diagnostics;
using System.Numerics;

namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public struct WallObservation
{
    public double Height;
    public double LateralRms;
    public double TranslationalEnergy;
    public double RotationalEnergy;
    public double PotentialEnergy;
    public double Penetration;
    public double ElapsedMs;
    public ulong EscapedBodies;
    public ulong InvalidBodies;
}

public struct BepuWallBodyInput
{
    public RigidPose Pose;
    public Vector3 Linear;
    public Vector3 Angular;
    public Symmetric3x3 InverseInertia;
    public float InverseMass;
    public float SleepThreshold;
    public uint Awake;
}

public static class BepuPyramidWallCase
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

    public static Vector3 Position(in CaseExecutionPyramidWall wall, uint index)
    {
        uint row = 0;
        uint width = wall.RowCount;
        while (index >= width)
        {
            index -= width;
            --width;
            ++row;
        }
        float h = wall.HalfExtent;
        return new Vector3((row + 1) * h + 2f * index * h - h * wall.RowCount, (2 * row + 1) * h, 0f);
    }

    public static uint ObservationStep(uint measured, uint ordinal)
    {
        if (measured < 4 || ordinal == 0) return ordinal + 1;
        if (ordinal == 3) return measured;
        return Math.Max(ordinal + 1, (measured + 1) / (ordinal == 1 ? 4u : 2u));
    }

    public static int ObservationIndex(uint completed, uint measured)
    {
        for (uint index = 0; index < Math.Min(4u, measured); ++index)
            if (completed == ObservationStep(measured, index)) return (int)index;
        return -1;
    }

    public static void Accumulate(in CaseExecutionSpec execution, uint index, Vector3 p, Quaternion q,
        Vector3 linear, Vector3 angular, double mass, double rotationalEnergy, int sleepMatches, ref WallObservation sample)
    {
        if (!float.IsFinite(p.X) || !float.IsFinite(p.Y) || !float.IsFinite(p.Z) ||
            !float.IsFinite(q.X) || !float.IsFinite(q.Y) || !float.IsFinite(q.Z) || !float.IsFinite(q.W) ||
            !float.IsFinite(linear.X) || !float.IsFinite(linear.Y) || !float.IsFinite(linear.Z) ||
            !float.IsFinite(angular.X) || !float.IsFinite(angular.Y) || !float.IsFinite(angular.Z) ||
            !double.IsFinite(mass) || !double.IsFinite(rotationalEnergy))
        {
            ++sample.InvalidBodies;
            return;
        }
        if (mass <= 0 || sleepMatches == 0) ++sample.InvalidBodies;
        Vector3 initial = Position(in execution.PyramidWall, index);
        double dx = (double)p.X - initial.X;
        double dz = (double)p.Z - initial.Z;
        sample.Height += p.Y;
        sample.LateralRms += dx * dx + dz * dz;
        sample.TranslationalEnergy += 0.5 * mass * ((double)linear.X * linear.X + (double)linear.Y * linear.Y + (double)linear.Z * linear.Z);
        sample.RotationalEnergy += rotationalEnergy;
        sample.PotentialEnergy -= mass * ((double)execution.Gravity.X * p.X + (double)execution.Gravity.Y * p.Y + (double)execution.Gravity.Z * p.Z);
        double x = q.X, y = q.Y, z = q.Z, w = q.W;
        double h = execution.PyramidWall.HalfExtent;
        double sx = h * (Math.Abs(1 - 2 * (y*y + z*z)) + Math.Abs(2 * (x*y-z*w)) + Math.Abs(2 * (x*z+y*w)));
        double sy = h * (Math.Abs(2 * (x*y+z*w)) + Math.Abs(1 - 2 * (x*x+z*z)) + Math.Abs(2 * (y*z-x*w)));
        double sz = h * (Math.Abs(2 * (x*z-y*w)) + Math.Abs(2 * (y*z+x*w)) + Math.Abs(1 - 2 * (x*x+y*y)));
        sample.Penetration = Math.Max(sample.Penetration, sy - p.Y);
        if (Math.Abs(p.X) + sx > execution.PyramidWall.FloorHalfExtents.X ||
            Math.Abs(p.Z) + sz > execution.PyramidWall.FloorHalfExtents.Z) ++sample.EscapedBodies;
    }

    public static BepuObservationRow[] ObservationRows(uint measured, WallObservation[] samples, double initialEnergy)
    {
        string[] ids = { "centre_of_mass_height", "lateral_rms", "translational_energy", "rotational_energy",
            "potential_energy", "floor_penetration", "escaped_body_count", "invalid_body_count", "observation_elapsed_ms" };
        uint count = Math.Min(4u, measured);
        BepuObservationRow[] rows = new BepuObservationRow[9 * count + 1];
        for (uint field = 0; field < 9; ++field)
        {
            for (uint ordinal = 0; ordinal < count; ++ordinal)
            {
                WallObservation sample = samples[ordinal];
                double value = field switch
                {
                    0 => sample.Height, 1 => sample.LateralRms, 2 => sample.TranslationalEnergy,
                    3 => sample.RotationalEnergy, 4 => sample.PotentialEnergy, 5 => sample.Penetration,
                    _ => sample.ElapsedMs
                };
                rows[(field < 8 ? field * count : 8 * count + 1) + ordinal] = new BepuObservationRow
                {
                    MetricId = ids[field], PhaseId = "observation", SampleIndex = ObservationStep(measured, ordinal),
                    ValueType = field == 6 || field == 7 ? BepuObservationValueType.Uint64 : BepuObservationValueType.Float64,
                    ValueBits = field == 6 ? sample.EscapedBodies : field == 7 ? sample.InvalidBodies : (ulong)BitConverter.DoubleToInt64Bits(value)
                };
            }
        }
        rows[8 * count] = new BepuObservationRow
        {
            MetricId = "initial_potential_energy", PhaseId = "construction", SampleIndex = 0,
            ValueType = BepuObservationValueType.Float64, ValueBits = (ulong)BitConverter.DoubleToInt64Bits(initialEnergy)
        };
        return rows;
    }

    public static int RequestedWorkerCount(int threadCount)
    {
        return threadCount;
    }

    public static int CreateSimulation(
        BufferPool bufferPool,
        int workerCount,
        in CaseExecutionSpec execution,
        out ThreadDispatcher threadDispatcher,
        out BodyHandle[] dynamicBodies,
        out Simulation simulation, out double initialEnergy, VerificationMode verificationMode = VerificationMode.On)
    {
        CaseExecutionPyramidWall fixture = execution.PyramidWall;
        initialEnergy = 0.0;
        threadDispatcher = null;
        simulation = Simulation.Create(
            bufferPool,
            new PolygonNarrowPhaseCallbacks(new SpringSettings(30, 1), 2f, execution.Friction),
            new PolygonPoseIntegratorCallbacks(
                new Vector3(execution.Gravity.X, execution.Gravity.Y, execution.Gravity.Z), 0, 0),
            new SolveDescription((int)execution.VelocityIterations, (int)execution.Substeps));
        simulation.Deterministic = true;
        PolygonNarrowPhaseCallbacks material = ((BepuPhysics.CollisionDetection.NarrowPhase<PolygonNarrowPhaseCallbacks>)simulation.NarrowPhase).Callbacks;
        PolygonPoseIntegratorCallbacks integration = ((PoseIntegrator<PolygonPoseIntegratorCallbacks>)simulation.PoseIntegrator).Callbacks;
        dynamicBodies = new BodyHandle[execution.DynamicBodyCount];
        if (material.FrictionCoefficient != execution.Friction || integration.LinearDamping != 0f ||
            integration.AngularDamping != 0f) return 2;
        BepuResolvedShape shape = BepuCaseRegistry.AddResolvedShape(simulation, bufferPool,
            in execution, in execution.SelectedGeometry);
        BodyInertia inertia = BepuCaseRegistry.ShapeInertia(simulation, shape.Index, fixture.Density * shape.Volume);
        Quaternion shapeRotation = Quaternion.Identity;
        BodyActivityDescription activity = execution.SleepMode == CaseExecutionToggle.Enabled
            ? new BodyActivityDescription(0.01f) : new BodyActivityDescription(-1f);
        float expectedMass = 8f * fixture.HalfExtent * fixture.HalfExtent * fixture.HalfExtent * fixture.Density;
        float expectedInertia = (2f / 3f) * expectedMass * fixture.HalfExtent * fixture.HalfExtent;
        for (uint index = 0; index < execution.DynamicBodyCount; ++index)
        {
            Vector3 position = Position(in fixture, index);
            BodyDescription description = BodyDescription.CreateDynamic(new RigidPose(position, shapeRotation), inertia, shape.Index, activity);
            description.Collidable.Continuity = execution.ContinuousCollisionMode == CaseExecutionToggle.Enabled
                ? ContinuousDetection.Continuous(1e-3f, 1e-3f) : ContinuousDetection.Passive;
            dynamicBodies[index] = simulation.Bodies.Add(description);
            BodyReference body = simulation.Bodies.GetBodyReference(dynamicBodies[index]);
            BodyInertia nativeInertia = body.LocalInertia;
            double mass = 1.0 / nativeInertia.InverseMass;
            if (body.Pose.Position != position || body.Pose.Orientation != Quaternion.Identity ||
                body.Velocity.Linear != Vector3.Zero || body.Velocity.Angular != Vector3.Zero ||
                !double.IsFinite(mass) || mass <= 0 || Math.Abs(mass - expectedMass) > 1e-5 * expectedMass ||
                !float.IsFinite(nativeInertia.InverseInertiaTensor.XX) || !float.IsFinite(nativeInertia.InverseInertiaTensor.YY) ||
                !float.IsFinite(nativeInertia.InverseInertiaTensor.ZZ) || body.Collidable.Shape != shape.Index ||
                Math.Abs(1.0 / nativeInertia.InverseInertiaTensor.XX - expectedInertia) > 1e-5 * expectedInertia ||
                Math.Abs(1.0 / nativeInertia.InverseInertiaTensor.YY - expectedInertia) > 1e-5 * expectedInertia ||
                Math.Abs(1.0 / nativeInertia.InverseInertiaTensor.ZZ - expectedInertia) > 1e-5 * expectedInertia ||
                nativeInertia.InverseInertiaTensor.YX != 0f || nativeInertia.InverseInertiaTensor.ZX != 0f || nativeInertia.InverseInertiaTensor.ZY != 0f ||
                (body.Activity.SleepThreshold >= 0f) != (execution.SleepMode == CaseExecutionToggle.Enabled) || !body.Awake ||
                body.Collidable.Continuity.Mode != description.Collidable.Continuity.Mode) return 2;
            Vector3 nativePosition = body.Pose.Position;
            if (verificationMode == VerificationMode.On) initialEnergy -= mass * ((double)execution.Gravity.X * nativePosition.X +
                (double)execution.Gravity.Y * nativePosition.Y + (double)execution.Gravity.Z * nativePosition.Z);
        }

        Box floor = new(fixture.FloorHalfExtents.X * 2f,
            fixture.FloorHalfExtents.Y * 2f, fixture.FloorHalfExtents.Z * 2f);
        TypedIndex floorShape = simulation.Shapes.Add(floor);
        StaticHandle floorHandle = simulation.Statics.Add(new StaticDescription(
            new Vector3(0f, -fixture.FloorHalfExtents.Y, 0f), floorShape));
        StaticReference nativeFloor = simulation.Statics.GetStaticReference(floorHandle);
        if (nativeFloor.Pose.Position != new Vector3(0f, -fixture.FloorHalfExtents.Y, 0f) ||
            nativeFloor.Pose.Orientation != Quaternion.Identity || nativeFloor.Shape != floorShape ||
            simulation.Bodies.ActiveSet.Count != execution.DynamicBodyCount || simulation.Statics.Count != 1 ||
            simulation.Bodies.ActiveSet.Count + simulation.Statics.Count != execution.ShapeCount) return 2;
        threadDispatcher = workerCount > 0 ? new ThreadDispatcher(workerCount) : null;
        return 0;
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

    public static double CaptureObservation(Simulation simulation, BodyHandle[] bodies, BepuWallBodyInput[] inputs)
    {
        long start = Stopwatch.GetTimestamp();
        for (int index = 0; index < bodies.Length; ++index)
        {
            BodyReference body = simulation.Bodies.GetBodyReference(bodies[index]);
            inputs[index] = new BepuWallBodyInput
            {
                Pose = body.Pose, Linear = body.Velocity.Linear, Angular = body.Velocity.Angular,
                InverseInertia = body.LocalInertia.InverseInertiaTensor, InverseMass = body.LocalInertia.InverseMass,
                SleepThreshold = body.Activity.SleepThreshold, Awake = body.Awake ? 1u : 0u
            };
        }
        return (Stopwatch.GetTimestamp() - start) * 1000.0 / Stopwatch.Frequency;
    }

    public static WallObservation ReduceObservation(BepuWallBodyInput[] inputs, in CaseExecutionSpec execution, double extractionMs)
    {
        long start = Stopwatch.GetTimestamp();
        WallObservation sample = default;
        for (uint index = 0; index < inputs.Length; ++index)
        {
            BepuWallBodyInput body = inputs[index];
            RigidPose pose = body.Pose;
            Vector3 linear = body.Linear;
            Vector3 angular = body.Angular;
            Vector3 local = Vector3.Transform(angular, Quaternion.Conjugate(pose.Orientation));
            Symmetric3x3.Invert(body.InverseInertia, out Symmetric3x3 inertia);
            Symmetric3x3.TransformWithoutOverlap(local, inertia, out Vector3 momentum);
            double energy = 0.5 * ((double)local.X * momentum.X + (double)local.Y * momentum.Y + (double)local.Z * momentum.Z);
            int sleepMatches = (body.SleepThreshold >= 0f) == (execution.SleepMode == CaseExecutionToggle.Enabled) &&
                (execution.SleepMode == CaseExecutionToggle.Enabled || body.Awake != 0) ? 1 : 0;
            Accumulate(in execution, index, pose.Position, pose.Orientation, linear, angular,
                1.0 / body.InverseMass, energy, sleepMatches, ref sample);
        }
        sample.Height /= inputs.Length;
        sample.LateralRms = Math.Sqrt(sample.LateralRms / inputs.Length);
        sample.ElapsedMs = extractionMs + (Stopwatch.GetTimestamp() - start) * 1000.0 / Stopwatch.Frequency;
        return sample;
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
        CaseExecutionPyramidWall fixture = execution.PyramidWall;
        if (geometries == null || geometries.Length < 2 || instances == null ||
            instances.Length < execution.VisualInstanceCount) return 2;
        if (BepuCaseRegistry.BuildResolvedVisualGeometry(in execution, in execution.SelectedGeometry,
            ref meshes, out geometries[0]) != 0) return 2;
        geometries[1] = new BepuVisualGeometry { Kind = 2,
            ParameterX = fixture.FloorHalfExtents.X, ParameterY = fixture.FloorHalfExtents.Y,
            ParameterZ = fixture.FloorHalfExtents.Z };
        for (int index = 0; index < state.DynamicBodies.Length; ++index)
        {
            RigidPose pose = state.Simulation.Bodies.GetBodyReference(state.DynamicBodies[index]).Pose;
            instances[index] = new BepuVisualInstance
            {
                GeometryIndex = 0u,
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
            GeometryIndex = 1,
            StableSlot = (uint)floorSlot,
            TransformSlot = uint.MaxValue,
            InitialTransform = new BepuVisualTransform
            {
                PositionY = -fixture.FloorHalfExtents.Y,
                RotationW = 1f
            }
        };
        geometryCount = 2;
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
            if (CreateSimulation(bufferPool, workerCount, in runnerArgs.CaseExecution,
                out dispatcher, out BodyHandle[] bodies, out simulation, out double initialEnergy, runnerArgs.VerificationMode) != 0) return 2;
            long[] durations = new long[runnerArgs.StepCount];
            int sampleCount = runnerArgs.VerificationMode == VerificationMode.On ? Math.Min(4, runnerArgs.StepCount) : 0;
            WallObservation[] samples = new WallObservation[sampleCount];
            BepuWallBodyInput[][] inputs = new BepuWallBodyInput[sampleCount][];
            for (int ordinal = 0; ordinal < sampleCount; ++ordinal)
                inputs[ordinal] = new BepuWallBodyInput[bodies.Length];
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
                long start = Stopwatch.GetTimestamp();
                StepSimulation(simulation, dispatcher, runnerArgs.CaseExecution.TimestepHz, 1);
                durations[step] = Stopwatch.GetTimestamp() - start;
                int observation = ObservationIndex((uint)step + 1, (uint)runnerArgs.StepCount);
                if (runnerArgs.VerificationMode == VerificationMode.On && observation >= 0) samples[observation].ElapsedMs = CaptureObservation(simulation, bodies, inputs[observation]);
                if (runnerArgs.VerificationMode == VerificationMode.On && StackStateCapture.Append(ref capture, in runnerArgs.CaseRegistration, recordingView,
                    StackCapturePhase.Measured, 0, (uint)step + 1) != 0) return 2;
                if (runnerArgs.RecordingMode == RecordingMode.On && BepuRecording.Append(ref recording, in runnerArgs.CaseRegistration, recordingView, (ulong)(step + 1)) != 0) return 2;
            }
            if (runnerArgs.VerificationMode == VerificationMode.On && StackStateCapture.Close(ref capture) != 0) return 2;
            if (runnerArgs.RecordingMode == RecordingMode.On && BepuRecording.Complete(ref recording) != 0) return 2;

            for (int ordinal = 0; ordinal < sampleCount; ++ordinal)
                samples[ordinal] = ReduceObservation(inputs[ordinal], in runnerArgs.CaseExecution, samples[ordinal].ElapsedMs);

            double elapsedMilliseconds = 0.0;
            for (int index = 0; index < durations.Length; ++index)
                elapsedMilliseconds += durations[index] * 1000.0 / Stopwatch.Frequency;
            ulong invalidCount = runnerArgs.VerificationMode == VerificationMode.Off ? (ulong)BepuBoxContactIslandsCase.CountInvalidTransforms(simulation, bodies) : 0;
            for (int index = 0; index < samples.Length; ++index) invalidCount += samples[index].InvalidBodies;
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
                Observations = runnerArgs.VerificationMode == VerificationMode.On ? ObservationRows((uint)runnerArgs.StepCount, samples, initialEnergy) : Array.Empty<BepuObservationRow>()
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
