using BepuPhysics;
using BepuPhysics.Collidables;
using BepuPhysics.CollisionDetection;
using BepuPhysics.Constraints;
using BepuUtilities;
using BepuUtilities.Memory;
using System.Diagnostics;
using System.Numerics;
using System.Runtime.CompilerServices;

namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public struct PolygonPoseIntegratorCallbacks : IPoseIntegratorCallbacks
{
    public Vector3 Gravity;
    public float LinearDamping;
    public float AngularDamping;
    public readonly AngularIntegrationMode AngularIntegrationMode => AngularIntegrationMode.Nonconserving;
    public readonly bool AllowSubstepsForUnconstrainedBodies => false;
    public readonly bool IntegrateVelocityForKinematics => false;
    public Vector3Wide GravityWideDt;
    public Vector<float> LinearDampingDt;
    public Vector<float> AngularDampingDt;

    public PolygonPoseIntegratorCallbacks(Vector3 gravity, float linearDamping, float angularDamping)
    {
        Gravity = gravity;
        LinearDamping = linearDamping;
        AngularDamping = angularDamping;
        GravityWideDt = default;
        LinearDampingDt = default;
        AngularDampingDt = default;
    }

    public void Initialize(Simulation simulation)
    {
    }

    public void PrepareForIntegration(float dt)
    {
        LinearDampingDt = new Vector<float>(MathF.Pow(MathHelper.Clamp(1 - LinearDamping, 0, 1), dt));
        AngularDampingDt = new Vector<float>(MathF.Pow(MathHelper.Clamp(1 - AngularDamping, 0, 1), dt));
        GravityWideDt = Vector3Wide.Broadcast(Gravity * dt);
    }

    public void IntegrateVelocity(
        Vector<int> bodyIndices,
        Vector3Wide position,
        QuaternionWide orientation,
        BodyInertiaWide localInertia,
        Vector<int> integrationMask,
        int workerIndex,
        Vector<float> dt,
        ref BodyVelocityWide velocity)
    {
        velocity.Linear = (velocity.Linear + GravityWideDt) * LinearDampingDt;
        velocity.Angular *= AngularDampingDt;
    }
}

public unsafe struct PolygonNarrowPhaseCallbacks : INarrowPhaseCallbacks
{
    public SpringSettings ContactSpringiness;
    public float MaximumRecoveryVelocity;
    public float FrictionCoefficient;

    public PolygonNarrowPhaseCallbacks(SpringSettings contactSpringiness, float maximumRecoveryVelocity, float frictionCoefficient)
    {
        ContactSpringiness = contactSpringiness;
        MaximumRecoveryVelocity = maximumRecoveryVelocity;
        FrictionCoefficient = frictionCoefficient;
    }

    public void Initialize(Simulation simulation)
    {

    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool AllowContactGeneration(int workerIndex, CollidableReference a, CollidableReference b, ref float speculativeMargin)
    {
        return a.Mobility == CollidableMobility.Dynamic || b.Mobility == CollidableMobility.Dynamic;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool AllowContactGeneration(int workerIndex, CollidablePair pair, int childIndexA, int childIndexB)
    {
        return true;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool ConfigureContactManifold<TManifold>(
        int workerIndex,
        CollidablePair pair,
        ref TManifold manifold,
        out PairMaterialProperties pairMaterial)
        where TManifold : unmanaged, IContactManifold<TManifold>
    {
        pairMaterial.FrictionCoefficient = FrictionCoefficient;
        pairMaterial.MaximumRecoveryVelocity = MaximumRecoveryVelocity;
        pairMaterial.SpringSettings = ContactSpringiness;
        return true;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool ConfigureContactManifold(int workerIndex, CollidablePair pair, int childIndexA, int childIndexB, ref ConvexContactManifold manifold)
    {
        return true;
    }

    public void Dispose()
    {
    }
}

public struct BepuStabilityCounters
{
    public int InvalidTransformCount;
    public int DynamicBodyCount;
}

public static class BepuBoxContainerPileCase
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
        return SampleTransforms(state.Simulation, state.CaseExecution.DynamicBodyCount, transforms);
    }

    public static Simulation CreateSimulation(BufferPool bufferPool, int workerCount,
        in CaseExecutionSpec execution, out ThreadDispatcher threadDispatcher)
    {
        CaseExecutionOpenContainer fixture = execution.OpenContainer;
        Simulation simulation = Simulation.Create(
            bufferPool,
            new PolygonNarrowPhaseCallbacks(new SpringSettings(30, 1), 2f, execution.Friction),
            new PolygonPoseIntegratorCallbacks(
                new Vector3(execution.Gravity.X, execution.Gravity.Y, execution.Gravity.Z), 0, 0),
            new SolveDescription((int)execution.VelocityIterations, (int)execution.Substeps));
        simulation.Deterministic = true;

        for (int index = 0; index < fixture.StaticBoxCount; ++index)
        {
            CaseExecutionBox staticBox = fixture.StaticBoxes[index];
            TypedIndex staticShape = simulation.Shapes.Add(new Box(
                staticBox.HalfExtents.X * 2f, staticBox.HalfExtents.Y * 2f,
                staticBox.HalfExtents.Z * 2f));
            simulation.Statics.Add(new StaticDescription(new Vector3(
                staticBox.Center.X, staticBox.Center.Y, staticBox.Center.Z), staticShape));
        }
        BepuResolvedShape shape = BepuCaseRegistry.AddResolvedShape(simulation, bufferPool,
            in execution, in execution.SelectedGeometry);
        BodyInertia inertia = BepuCaseRegistry.ShapeInertia(simulation, shape.Index, fixture.Density * shape.Volume);
        Quaternion shapeRotation = BepuCaseRegistry.ShapeRotation(execution.SelectedGeometry.Axis);
        float originX = -0.5f * (fixture.DynamicGrid[0] - 1) * fixture.DynamicSpacing.X;
        float originZ = -0.5f * (fixture.DynamicGrid[2] - 1) * fixture.DynamicSpacing.Z;
        BodyActivityDescription activity = execution.SleepMode == CaseExecutionToggle.Enabled
            ? new BodyActivityDescription(0.01f) : new BodyActivityDescription(-1f);

        for (int y = 0; y < fixture.DynamicGrid[1]; ++y)
        {
            for (int z = 0; z < fixture.DynamicGrid[2]; ++z)
            {
                for (int x = 0; x < fixture.DynamicGrid[0]; ++x)
                {
                    Vector3 location = new(
                        originX + x * fixture.DynamicSpacing.X,
                        fixture.DynamicInitialY + y * fixture.DynamicSpacing.Y,
                        originZ + z * fixture.DynamicSpacing.Z);
                    BodyDescription description = BodyDescription.CreateDynamic(
                        new RigidPose(location + Vector3.Transform(shape.Center, shapeRotation), shapeRotation),
                        inertia, shape.Index, activity);
                    if (execution.ContinuousCollisionMode == CaseExecutionToggle.Enabled)
                        description.Collidable.Continuity = ContinuousDetection.Continuous(1e-3f, 1e-3f);
                    simulation.Bodies.Add(description);
                }
            }
        }

        if (simulation.Bodies.ActiveSet.Count != execution.DynamicBodyCount)
        {
            throw new InvalidOperationException($"Unexpected dynamic body count: {simulation.Bodies.ActiveSet.Count}");
        }

        threadDispatcher = workerCount > 0 ? new ThreadDispatcher(workerCount) : null;
        return simulation;
    }

    public static int RequestedWorkerCount(int threadCount)
    {
        return threadCount;
    }

    public static void StepSimulation(Simulation simulation, ThreadDispatcher threadDispatcher,
        uint timestepHz, int stepCount)
    {
        float timestepDuration = 1f / timestepHz;
        if (threadDispatcher == null)
        {
            for (int index = 0; index < stepCount; ++index)
            {
                simulation.Timestep(timestepDuration);
            }
            return;
        }

        for (int index = 0; index < stepCount; ++index)
        {
            simulation.Timestep(timestepDuration, threadDispatcher);
        }
    }

    public static int RunWarmup(BufferPool bufferPool, int workerCount,
        in BepuRunnerArgs runnerArgs, ref StackCapture capture)
    {
        Simulation warmupSimulation = default;
        ThreadDispatcher warmupDispatcher = null;
        try
        {
            CaseExecutionSpec execution = runnerArgs.CaseExecution;
            warmupSimulation = CreateSimulation(bufferPool, workerCount, in execution, out warmupDispatcher);
            BepuCaseView view = new() { CaseExecution = execution, Simulation = warmupSimulation, DynamicBodies = null };
            for (int step = 0; step <= runnerArgs.WarmupSteps; ++step)
            {
                if (step != 0) StepSimulation(warmupSimulation, warmupDispatcher, execution.TimestepHz, 1);
                if (runnerArgs.VerificationMode == VerificationMode.On && StackStateCapture.Append(ref capture, in runnerArgs.CaseRegistration, view,
                    step == 0 ? StackCapturePhase.Construction : StackCapturePhase.Warmup, 0, (uint)step) != 0) return 2;
            }
            return 0;
        }
        finally
        {
            warmupSimulation?.Dispose();
            warmupDispatcher?.Dispose();
        }
    }

    public static BepuStabilityCounters CountStability(Simulation simulation)
    {
        BepuStabilityCounters counters = default;
        ref Buffer<BodySet> sets = ref simulation.Bodies.Sets;
        for (int setIndex = 0; setIndex < sets.Length; ++setIndex)
        {
            ref BodySet set = ref sets[setIndex];
            if (!set.Allocated)
            {
                continue;
            }

            for (int bodyIndex = 0; bodyIndex < set.Count; ++bodyIndex)
            {
                ref RigidPose pose = ref set.DynamicsState[bodyIndex].Motion.Pose;
                Vector3 position = pose.Position;
                Quaternion orientation = pose.Orientation;
                counters.DynamicBodyCount += 1;

                if (!float.IsFinite(position.X) || !float.IsFinite(position.Y) || !float.IsFinite(position.Z) ||
                    !float.IsFinite(orientation.X) || !float.IsFinite(orientation.Y) || !float.IsFinite(orientation.Z) ||
                    !float.IsFinite(orientation.W))
                {
                    counters.InvalidTransformCount += 1;
                }
            }
        }
        return counters;
    }

    public static int SampleTransforms(
        Simulation simulation, uint expectedDynamicBodyCount, BepuVisualStableTransform[] transforms)
    {
        if (transforms.Length < expectedDynamicBodyCount)
        {
            return 2;
        }
        int transformIndex = 0;
        ref Buffer<BodySet> sets = ref simulation.Bodies.Sets;
        for (int setIndex = 0; setIndex < sets.Length; ++setIndex)
        {
            ref BodySet set = ref sets[setIndex];
            if (!set.Allocated)
            {
                continue;
            }
            for (int bodyIndex = 0; bodyIndex < set.Count; ++bodyIndex)
            {
                ref RigidPose pose = ref set.DynamicsState[bodyIndex].Motion.Pose;
                int stableSlot = set.IndexToHandle[bodyIndex].Value;
                if ((uint)stableSlot >= expectedDynamicBodyCount) return 2;
                transforms[stableSlot] = new BepuVisualStableTransform
                {
                    StableSlot = (uint)stableSlot,
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
                transformIndex += 1;
            }
        }
        return transformIndex == expectedDynamicBodyCount ? 0 : 2;
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
        CaseExecutionOpenContainer fixture = execution.OpenContainer;
        if (geometries == null || geometries.Length < 1 + fixture.StaticBoxCount ||
            instances == null || instances.Length < execution.BodyCount)
        {
            return 2;
        }
        if (BepuCaseRegistry.BuildResolvedVisualGeometry(in execution, in execution.SelectedGeometry,
            ref meshes, out geometries[0]) != 0) return 2;
        BepuCaseRegistry.OffsetHullVisualGeometry(state.Simulation,
            state.Simulation.Bodies.GetBodyReference(new BodyHandle(0)).Collidable.Shape,
            in execution.SelectedGeometry, ref meshes, in geometries[0]);
        for (int index = 0; index < fixture.StaticBoxCount; ++index)
        {
            CaseExecutionVector3 halfExtents = fixture.StaticBoxes[index].HalfExtents;
            geometries[index + 1] = new BepuVisualGeometry { Kind = 2,
                ParameterX = halfExtents.X, ParameterY = halfExtents.Y, ParameterZ = halfExtents.Z };
        }
        BepuVisualStableTransform[] transforms = new BepuVisualStableTransform[execution.DynamicBodyCount];
        if (SampleTransforms(state.Simulation, execution.DynamicBodyCount, transforms) != 0) return 2;
        for (int index = 0; index < execution.DynamicBodyCount; ++index)
        {
            instances[index] = new BepuVisualInstance
            {
                GeometryIndex = 0,
                StableSlot = (uint)index,
                TransformSlot = (uint)index,
                InitialTransform = transforms[index].Transform
            };
        }
        for (int index = 0; index < fixture.StaticBoxCount; ++index)
        {
            CaseExecutionBox staticBox = fixture.StaticBoxes[index];
            int stableSlot = (int)execution.DynamicBodyCount + index;
            instances[stableSlot] = new BepuVisualInstance
            {
                GeometryIndex = (uint)(index + 1),
                StableSlot = (uint)stableSlot,
                TransformSlot = uint.MaxValue,
                InitialTransform = new BepuVisualTransform
                {
                    PositionX = staticBox.Center.X,
                    PositionY = staticBox.Center.Y,
                    PositionZ = staticBox.Center.Z,
                    RotationW = 1f
                }
            };
        }
        geometryCount = 1 + fixture.StaticBoxCount;
        instanceCount = (int)execution.BodyCount;
        return 0;
    }

    public static string VisualPhysicsSettings(
        in CaseExecutionSpec execution, int threadCount)
    {
        return string.Create(
            System.Globalization.CultureInfo.InvariantCulture,
            $"velocity_iterations={execution.VelocityIterations}; substeps={execution.Substeps}; linear_damping=0; angular_damping=0; sleep={(execution.SleepMode == CaseExecutionToggle.Enabled ? "enabled" : "disabled")}; ccd={(execution.ContinuousCollisionMode == CaseExecutionToggle.Enabled ? "enabled" : "disabled")}; deterministic=yes; worker_count={threadCount}");
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
            int requestedWorkerCount = RequestedWorkerCount(runnerArgs.WorkerCount);
            long[] rawStepDurations = new long[runnerArgs.StepCount];
            if (runnerArgs.VerificationMode == VerificationMode.On && StackStateCapture.Open(in runnerArgs, out capture) != 0) return 2;
            if (runnerArgs.WarmupSteps > 0)
            {
                if (RunWarmup(bufferPool, requestedWorkerCount, in runnerArgs, ref capture) != 0) return 2;
                bufferPool.Clear();
            }

            simulation = CreateSimulation(bufferPool, requestedWorkerCount,
                in runnerArgs.CaseExecution, out threadDispatcher);

            BepuCaseView recordingView = new()
            {
                CaseExecution = runnerArgs.CaseExecution, Simulation = simulation, DynamicBodies = null
            };
            uint segment = runnerArgs.WarmupSteps > 0 ? 1u : 0u;
            if (runnerArgs.VerificationMode == VerificationMode.On && StackStateCapture.Append(ref capture, in runnerArgs.CaseRegistration, recordingView,
                StackCapturePhase.Construction, segment, 0) != 0) return 2;
            if (runnerArgs.RecordingMode == RecordingMode.On && BepuRecording.Begin(in runnerArgs, in recordingView, out recording) != 0) return 2;
            float timestepDuration = 1f / runnerArgs.CaseExecution.TimestepHz;
            for (int step = 0; step < runnerArgs.StepCount; ++step)
            {
                long start = Stopwatch.GetTimestamp();
                if (threadDispatcher == null) simulation.Timestep(timestepDuration);
                else simulation.Timestep(timestepDuration, threadDispatcher);
                rawStepDurations[step] = Stopwatch.GetTimestamp() - start;
                if (runnerArgs.VerificationMode == VerificationMode.On && StackStateCapture.Append(ref capture, in runnerArgs.CaseRegistration, recordingView,
                    StackCapturePhase.Measured, segment, (uint)step + 1) != 0) return 2;
                if (runnerArgs.RecordingMode == RecordingMode.On && BepuRecording.Append(ref recording, in runnerArgs.CaseRegistration, recordingView, (ulong)(step + 1)) != 0) return 2;
            }

            if (runnerArgs.VerificationMode == VerificationMode.On && StackStateCapture.Close(ref capture) != 0) return 2;
            if (runnerArgs.RecordingMode == RecordingMode.On && BepuRecording.Complete(ref recording) != 0) return 2;
            double elapsedMilliseconds = 0.0;
            for (int step = 0; step < rawStepDurations.Length; ++step)
            {
                elapsedMilliseconds += rawStepDurations[step] * 1000.0 / Stopwatch.Frequency;
            }
            BepuStabilityCounters counters = CountStability(simulation);
            double msPerStep = elapsedMilliseconds / runnerArgs.StepCount;
            bool metricValid = elapsedMilliseconds > 0.0 && double.IsFinite(elapsedMilliseconds) &&
                msPerStep > 0.0 && double.IsFinite(msPerStep) &&
                counters.DynamicBodyCount == runnerArgs.CaseExecution.DynamicBodyCount;
            BepuResultValidity caseValidity = counters.InvalidTransformCount == 0 &&
                counters.DynamicBodyCount == runnerArgs.CaseExecution.DynamicBodyCount ?
                BepuResultValidity.Valid : BepuResultValidity.Invalid;
            BepuBenchmarkResult result = new()
            {
                PhysicsSettings = VisualPhysicsSettings(
                    in runnerArgs.CaseExecution, runnerArgs.WorkerCount),
                BodyCount = (int)runnerArgs.CaseExecution.BodyCount,
                ShapeCount = (int)runnerArgs.CaseExecution.ShapeCount,
                QueryCount = (int)runnerArgs.CaseExecution.QueryCount,
                ConstraintCount = (int)runnerArgs.CaseExecution.ConstraintCount,
				InvalidTransformCount = (ulong)counters.InvalidTransformCount,
                EffectiveThreadCount = runnerArgs.WorkerCount,
                EffectiveWorkerCount = runnerArgs.WorkerCount,
                CompletedWorkUnitCount = runnerArgs.StepCount,
                WorkloadElapsedMilliseconds = elapsedMilliseconds,
                CaseValidity = caseValidity,
                MetricValidity = metricValid ? BepuResultValidity.Valid : BepuResultValidity.Invalid
            };
            int resultStatus = BepuResultWriter.WriteResult(runnerArgs, in result);
            return resultStatus == 0 ? BepuResultWriter.WriteStepTiming(runnerArgs, rawStepDurations) : resultStatus;
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
