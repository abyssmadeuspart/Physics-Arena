using BepuPhysics;
using BepuPhysics.Collidables;
using BepuPhysics.Constraints;
using BepuPhysics.Trees;
using BepuUtilities;
using BepuUtilities.Memory;
using System.Diagnostics;
using System.Numerics;
using System.Runtime.CompilerServices;

namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public struct BepuSpatialQuery
{
    public Vector3 OriginOrCenter;
    public Vector3 Direction;
}

public struct BepuRayInput
{
    public Vector3 Origin;
    public Vector3 Direction;
}

public struct BepuSphereCastInput
{
    public RigidPose Pose;
    public BodyVelocity Velocity;
}

public struct BepuOverlapInput
{
    public Vector3 Minimum;
    public Vector3 Maximum;
}

public struct BepuSpatialQueryState
{
    public CaseExecutionSpec CaseExecution;
    public BufferPool BufferPool;
    public Simulation Simulation;
    public Sphere Sphere;
    public BepuRayInput[] RayInputs;
    public BepuSphereCastInput[] SphereCastInputs;
    public BepuOverlapInput[] OverlapInputs;
    public byte[] DebugHits;
    public float[] DebugHitDistances;
    public long[] RawBatchDurations;
    public ThreadDispatcher ThreadDispatcher;
    public Action<int> RayWorker;
    public Action<int> SphereCastWorker;
    public Action<int> OverlapWorker;
    public ulong[] LaneHitCounts;
    public long RayElapsed;
    public long SphereCastElapsed;
    public long OverlapElapsed;
    public double WorkloadElapsedMilliseconds;
    public double LatestBatchElapsedMilliseconds;
    public ulong RayHitCount;
    public ulong SphereCastHitCount;
    public ulong OverlapHitCount;
    public int ThreadCount;
    public int CompletedBatchCount;
}

public struct BepuClosestRayHitHandler : IRayHitHandler
{
    public byte Hit;
    public float Distance;

    public readonly bool AllowTest(CollidableReference collidable) => true;

    public readonly bool AllowTest(CollidableReference collidable, int childIndex) => true;

    public void OnRayHit(
        in RayData ray,
        ref float maximumT,
        float t,
        Vector3 normal,
        CollidableReference collidable,
        int childIndex)
    {
        Hit = 1;
        Distance = t;
        maximumT = t;
    }
}

public struct BepuClosestSweepHitHandler : ISweepHitHandler
{
    public byte Hit;
    public float Distance;

    public readonly bool AllowTest(CollidableReference collidable) => true;

    public readonly bool AllowTest(CollidableReference collidable, int childIndex) => true;

    public void OnHit(
        ref float maximumT,
        float t,
        Vector3 hitLocation,
        Vector3 hitNormal,
        CollidableReference collidable)
    {
        Hit = 1;
        Distance = t;
        maximumT = t;
    }

    public void OnHitAtZeroT(ref float maximumT, CollidableReference collidable)
    {
        Hit = 1;
        Distance = 0f;
        maximumT = 0f;
    }
}

public struct BepuAnyOverlapEnumerator : IBreakableForEach<CollidableReference>
{
    public byte Hit;

    public bool LoopBody(CollidableReference collidable)
    {
        Hit = 1;
        return false;
    }
}

public enum BepuSpatialQueryBatchPhase : byte
{
    Warmup,
    Measured
}

public static class BepuSpatialQueryTraceCase
{
    public static BepuCaseRegistration Registration()
    {
        return new BepuCaseRegistration
        {
            Descriptor = Descriptor(),
            BuildVisualScene = BuildVisualScene,
            SampleVisualTransforms = SampleVisualTransforms,
            BuildVisualDebugPrimitives = BuildVisualDebugPrimitives,
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
        return transforms != null && transforms.Length == 0 ? 0 : 2;
    }

    public static ulong CountInvalidTransforms(BepuCaseView state) => 0;

    public static string VisualPhysicsSettings(
        in CaseExecutionSpec execution, int threadCount)
    {
        CaseExecutionSpatialQuery fixture = execution.SpatialQuery;
        return string.Create(
            System.Globalization.CultureInfo.InvariantCulture,
            $"query_world=static_only; worker_count={threadCount}; rays={fixture.RayCount}; sphere_casts={fixture.SphereCastCount}; overlaps={fixture.OverlapCount}");
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
        if (state.CaseExecution.FixtureKind != CaseFixtureKind.SpatialQueryTrace || geometries == null ||
            instances == null)
        {
            return 2;
        }
        ref BepuSpatialQueryState value =
            ref state.QueryState;
        CaseExecutionSpec execution = value.CaseExecution;
        CaseExecutionSpatialQuery fixture = execution.SpatialQuery;
        if (geometries.Length < 1 || instances.Length < (int)execution.StaticBodyCount)
        {
            return 2;
        }
        if (BepuCaseRegistry.BuildResolvedVisualGeometry(in execution, in execution.SelectedGeometry,
            ref meshes, out geometries[0]) != 0) return 2;
        Quaternion rotation = BepuCaseRegistry.ShapeRotation(execution.SelectedGeometry.Axis);
        for (uint iy = 0; iy < fixture.StaticGrid[1]; ++iy)
        {
            for (uint iz = 0; iz < fixture.StaticGrid[2]; ++iz)
            {
                for (uint ix = 0; ix < fixture.StaticGrid[0]; ++ix)
                {
                    int slot = (int)((iy * fixture.StaticGrid[2] + iz) *
                        fixture.StaticGrid[0] + ix);
                    instances[slot] = new BepuVisualInstance
                    {
                        GeometryIndex = 0,
                        StableSlot = (uint)slot,
                        TransformSlot = uint.MaxValue,
                        InitialTransform = new BepuVisualTransform
                        {
                            PositionX = CenteredGridCoordinate(fixture.StaticBaseCenter.X,
                                fixture.StaticSpacing.X, ix, fixture.StaticGrid[0]),
                            PositionY = UncenteredGridCoordinate(fixture.StaticBaseCenter.Y,
                                fixture.StaticSpacing.Y, iy),
                            PositionZ = CenteredGridCoordinate(fixture.StaticBaseCenter.Z,
                                fixture.StaticSpacing.Z, iz, fixture.StaticGrid[2]),
                            RotationX = rotation.X, RotationY = rotation.Y,
                            RotationZ = rotation.Z, RotationW = rotation.W
                        }
                    };
                }
            }
        }
        geometryCount = 1;
        instanceCount = (int)execution.StaticBodyCount;
        return 0;
    }

    public static int BuildVisualDebugPrimitives(
        BepuCaseView state, BepuVisualDebugPrimitive[] primitives)
    {
        if (state.CaseExecution.FixtureKind != CaseFixtureKind.SpatialQueryTrace || primitives == null)
        {
            return 2;
        }
        ref BepuSpatialQueryState value =
            ref state.QueryState;
        CaseExecutionSpec execution = value.CaseExecution;
        CaseExecutionSpatialQuery fixture = execution.SpatialQuery;
        int samples = (int)fixture.DebugSamplesPerFamily;
        if (primitives.Length < (int)execution.VisualDebugPrimitiveCount)
        {
            return 2;
        }
        for (int local = 0; local < samples; ++local)
        {
            for (int family = 0; family < 3; ++family)
            {
                int primitiveIndex = family * samples + local;
                BepuVisualDebugPrimitive primitive = new()
                {
                    Kind = (uint)family,
                    MaterialIndex = value.DebugHits[primitiveIndex] != 0 ? 6u : 7u
                };
                if (family == 0)
                {
                    BepuRayInput input = value.RayInputs[local];
                    primitive.OriginOrCenterX = input.Origin.X;
                    primitive.OriginOrCenterY = input.Origin.Y;
                    primitive.OriginOrCenterZ = input.Origin.Z;
                    Vector3 end = input.Origin +
                        input.Direction * value.DebugHitDistances[primitiveIndex];
                    primitive.EndOrHalfExtentsX = end.X;
                    primitive.EndOrHalfExtentsY = end.Y;
                    primitive.EndOrHalfExtentsZ = end.Z;
                }
                else if (family == 1)
                {
                    BepuSphereCastInput input = value.SphereCastInputs[local];
                    Vector3 origin = input.Pose.Position;
                    primitive.OriginOrCenterX = origin.X;
                    primitive.OriginOrCenterY = origin.Y;
                    primitive.OriginOrCenterZ = origin.Z;
                    Vector3 end = origin +
                        input.Velocity.Linear * value.DebugHitDistances[primitiveIndex];
                    primitive.EndOrHalfExtentsX = end.X;
                    primitive.EndOrHalfExtentsY = end.Y;
                    primitive.EndOrHalfExtentsZ = end.Z;
                    primitive.Radius = fixture.SphereCastRadius;
                }
                else
                {
                    BepuOverlapInput input = value.OverlapInputs[local];
                    Vector3 center = (input.Minimum + input.Maximum) * 0.5f;
                    primitive.OriginOrCenterX = center.X;
                    primitive.OriginOrCenterY = center.Y;
                    primitive.OriginOrCenterZ = center.Z;
                    primitive.EndOrHalfExtentsX = fixture.OverlapHalfExtents.X;
                    primitive.EndOrHalfExtentsY = fixture.OverlapHalfExtents.Y;
                    primitive.EndOrHalfExtentsZ = fixture.OverlapHalfExtents.Z;
                }
                primitives[primitiveIndex] = primitive;
            }
        }
        return 0;
    }

    public static int RunHeadless(BepuRunnerArgs runnerArgs)
    {
        if (runnerArgs.StepCount != runnerArgs.CaseExecution.MeasuredWorkUnitCount ||
            runnerArgs.WarmupSteps != runnerArgs.CaseExecution.WarmupWorkUnitCount)
        {
            Console.Error.WriteLine(
                $"invalid_argument engine={runnerArgs.CaseRegistration.Descriptor.EngineId} " +
                $"case={CaseExecutionWire.TextValue(runnerArgs.CaseExecution.CaseId)} reason=work_schedule");
            return 2;
        }
        if (CreateState(in runnerArgs.CaseExecution, runnerArgs.WorkerCount, runnerArgs.RecordingMode,
            out BepuSpatialQueryState state) != 0)
        {
            return 2;
        }
        BepuRecordingWriter recording = default;
        try
        {
            int status = WarmupState(ref state, runnerArgs.WarmupSteps);
            if (status != 0) return status;
            BepuCaseView recordingView = new()
            {
                CaseExecution = runnerArgs.CaseExecution, Simulation = state.Simulation, DynamicBodies = null, QueryState = state
            };
            if (runnerArgs.RecordingMode == RecordingMode.On && BepuRecording.Begin(in runnerArgs, in recordingView, out recording) != 0) return 2;
            for (int batch = 0; batch < runnerArgs.StepCount; ++batch)
            {
                status = StepState(ref state, 1);
                if (status != 0) return status;
                if (runnerArgs.RecordingMode == RecordingMode.On && BepuRecording.Append(ref recording, in runnerArgs.CaseRegistration, recordingView, (ulong)(batch + 1)) != 0) return 2;
            }
            if (runnerArgs.RecordingMode == RecordingMode.On && BepuRecording.Complete(ref recording) != 0) return 2;

            if (status != 0)
            {
                return status;
            }
            BepuResultValidity validity =
                state.Simulation.Statics.Count == runnerArgs.CaseExecution.StaticBodyCount &&
                state.CompletedBatchCount == runnerArgs.StepCount &&
                state.WorkloadElapsedMilliseconds > 0.0 &&
                double.IsFinite(state.WorkloadElapsedMilliseconds) &&
                state.RayElapsed > 0 && state.SphereCastElapsed > 0 &&
                state.OverlapElapsed > 0 ?
                    BepuResultValidity.Valid :
                    BepuResultValidity.Invalid;
            BepuBenchmarkResult result = new()
            {
                PhysicsSettings = VisualPhysicsSettings(
                    in runnerArgs.CaseExecution, runnerArgs.WorkerCount),
                BodyCount = (int)runnerArgs.CaseExecution.BodyCount,
                ShapeCount = (int)runnerArgs.CaseExecution.ShapeCount,
                QueryCount = (int)runnerArgs.CaseExecution.QueryCount,
                ConstraintCount = (int)runnerArgs.CaseExecution.ConstraintCount,
                InvalidTransformCount = 0,
                EffectiveThreadCount = runnerArgs.WorkerCount,
                EffectiveWorkerCount = runnerArgs.WorkerCount,
                CompletedWorkUnitCount = state.CompletedBatchCount,
                WorkloadElapsedMilliseconds = state.WorkloadElapsedMilliseconds,
                CaseValidity = validity,
                MetricValidity = validity,
                Observations = BuildHeadlessObservations(state)
            };
            int resultStatus = BepuResultWriter.WriteResult(runnerArgs, in result);
            return resultStatus == 0 ?
                BepuResultWriter.WriteStepTiming(runnerArgs, state.RawBatchDurations) :
                resultStatus;
        }
        finally
        {
            if (runnerArgs.RecordingMode == RecordingMode.On) BepuRecording.Abort(ref recording);
            DestroyState(ref state);
        }
    }

    public static int CreateState(
        in CaseExecutionSpec execution, int threadCount, RecordingMode recordingMode, out BepuSpatialQueryState state)
    {
        CaseExecutionSpatialQuery fixture = execution.SpatialQuery;
        if (threadCount <= 0 || fixture.RayCount < threadCount ||
            fixture.SphereCastCount < threadCount || fixture.OverlapCount < threadCount)
        {
            state = default;
            return 2;
        }
        state = default;
        try
        {
            state = new BepuSpatialQueryState
            {
                CaseExecution = execution,
                BufferPool = new BufferPool(),
                Sphere = new Sphere(fixture.SphereCastRadius),
                RayInputs = new BepuRayInput[fixture.RayCount],
                SphereCastInputs = new BepuSphereCastInput[fixture.SphereCastCount],
                OverlapInputs = new BepuOverlapInput[fixture.OverlapCount],
                DebugHits = recordingMode == RecordingMode.On ? new byte[execution.VisualDebugPrimitiveCount] : null,
                DebugHitDistances = recordingMode == RecordingMode.On ? new float[execution.VisualDebugPrimitiveCount] : null,
                RawBatchDurations = new long[execution.MeasuredWorkUnitCount],
                ThreadDispatcher = new ThreadDispatcher(threadCount),
                LaneHitCounts = new ulong[threadCount],
                ThreadCount = threadCount
            };
            for (int index = 0; index < fixture.RayCount; ++index)
            {
                BepuSpatialQuery query = GenerateQuery(in execution, index);
                state.RayInputs[index] = new BepuRayInput
                {
                    Origin = query.OriginOrCenter,
                    Direction = query.Direction
                };
            }
            for (int index = 0; index < fixture.SphereCastCount; ++index)
            {
                BepuSpatialQuery query = GenerateQuery(in execution,
                    (int)fixture.RayCount + index);
                state.SphereCastInputs[index] = new BepuSphereCastInput
                {
                    Pose = new RigidPose(query.OriginOrCenter),
                    Velocity = new BodyVelocity { Linear = query.Direction }
                };
            }
            Vector3 halfExtent = new(fixture.OverlapHalfExtents.X,
                fixture.OverlapHalfExtents.Y, fixture.OverlapHalfExtents.Z);
            for (int index = 0; index < fixture.OverlapCount; ++index)
            {
                BepuSpatialQuery query =
                    GenerateQuery(in execution,
                        (int)(fixture.RayCount + fixture.SphereCastCount) + index);
                state.OverlapInputs[index] = new BepuOverlapInput
                {
                    Minimum = query.OriginOrCenter - halfExtent,
                    Maximum = query.OriginOrCenter + halfExtent
                };
            }
            state.Simulation = Simulation.Create(
                state.BufferPool,
                new PolygonNarrowPhaseCallbacks(new SpringSettings(30, 1), 2f,
                    execution.Friction),
                new PolygonPoseIntegratorCallbacks(new Vector3(
                    execution.Gravity.X, execution.Gravity.Y, execution.Gravity.Z), 0, 0),
                new SolveDescription(1, 1));
            BepuResolvedShape shape = BepuCaseRegistry.AddResolvedShape(state.Simulation, state.BufferPool,
                in execution, in execution.SelectedGeometry);
            Quaternion shapeRotation = BepuCaseRegistry.ShapeRotation(execution.SelectedGeometry.Axis);
            for (uint iy = 0; iy < fixture.StaticGrid[1]; ++iy)
            {
                for (uint iz = 0; iz < fixture.StaticGrid[2]; ++iz)
                {
                    for (uint ix = 0; ix < fixture.StaticGrid[0]; ++ix)
                    {
                        state.Simulation.Statics.Add(new StaticDescription(
                            new Vector3(
                                CenteredGridCoordinate(fixture.StaticBaseCenter.X,
                                    fixture.StaticSpacing.X, ix, fixture.StaticGrid[0]),
                                UncenteredGridCoordinate(fixture.StaticBaseCenter.Y,
                                    fixture.StaticSpacing.Y, iy),
                                CenteredGridCoordinate(fixture.StaticBaseCenter.Z,
                                    fixture.StaticSpacing.Z, iz, fixture.StaticGrid[2])) +
                                Vector3.Transform(shape.Center, shapeRotation),
                            shapeRotation, shape.Index));
                    }
                }
            }
            if (state.Simulation.Statics.Count != execution.StaticBodyCount)
            {
                DestroyState(ref state);
                return 2;
            }
            state.Simulation.BroadPhase.Update2();
            CreateQueryWorkers(ref state);
            if (recordingMode == RecordingMode.On) CaptureDebugSamples(ref state);
            return 0;
        }
        catch
        {
            DestroyState(ref state);
            return 2;
        }
    }

    public static void DestroyState(ref BepuSpatialQueryState state)
    {
        state.Simulation?.Dispose();
        state.ThreadDispatcher?.Dispose();
        state.BufferPool?.Clear();
        state = default;
    }

    public static void CreateQueryWorkers(ref BepuSpatialQueryState state)
    {
        Simulation simulation = state.Simulation;
        ThreadDispatcher dispatcher = state.ThreadDispatcher;
        Sphere sphere = state.Sphere;
        BepuRayInput[] rayInputs = state.RayInputs;
        BepuSphereCastInput[] sphereCastInputs = state.SphereCastInputs;
        BepuOverlapInput[] overlapInputs = state.OverlapInputs;
        ulong[] laneHitCounts = state.LaneHitCounts;
        int threadCount = state.ThreadCount;
        float queryDistance = state.CaseExecution.SpatialQuery.QueryDistance;
        state.RayWorker = workerIndex =>
        {
            int start = rayInputs.Length * workerIndex / threadCount;
            int end = rayInputs.Length * (workerIndex + 1) / threadCount;
            BufferPool workerPool = dispatcher.WorkerPools[workerIndex];
            ulong hitCount = 0;
            for (int index = start; index < end; ++index)
            {
                BepuRayInput input = rayInputs[index];
                BepuClosestRayHitHandler handler = new() { Distance = queryDistance };
                simulation.RayCast(input.Origin, input.Direction, queryDistance,
                    workerPool, ref handler);
                hitCount += handler.Hit;
            }
            laneHitCounts[workerIndex] = hitCount;
        };
        state.SphereCastWorker = workerIndex =>
        {
            int start = sphereCastInputs.Length * workerIndex / threadCount;
            int end = sphereCastInputs.Length * (workerIndex + 1) / threadCount;
            BufferPool workerPool = dispatcher.WorkerPools[workerIndex];
            ulong hitCount = 0;
            for (int index = start; index < end; ++index)
            {
                BepuSphereCastInput input = sphereCastInputs[index];
                BepuClosestSweepHitHandler handler = new() { Distance = queryDistance };
                simulation.Sweep(sphere, input.Pose, input.Velocity, queryDistance,
                    workerPool, ref handler);
                hitCount += handler.Hit;
            }
            laneHitCounts[workerIndex] = hitCount;
        };
        state.OverlapWorker = workerIndex =>
        {
            int start = overlapInputs.Length * workerIndex / threadCount;
            int end = overlapInputs.Length * (workerIndex + 1) / threadCount;
            BufferPool workerPool = dispatcher.WorkerPools[workerIndex];
            ulong hitCount = 0;
            for (int index = start; index < end; ++index)
            {
                BepuOverlapInput input = overlapInputs[index];
                BepuAnyOverlapEnumerator enumerator = default;
                simulation.BroadPhase.GetOverlaps(
                    input.Minimum, input.Maximum, workerPool, ref enumerator);
                hitCount += enumerator.Hit;
            }
            laneHitCounts[workerIndex] = hitCount;
        };
    }

    public static int CheckQueryBatch(in BepuSpatialQueryState state, string phase, int batch)
    {
        CaseExecutionSpatialQuery fixture = state.CaseExecution.SpatialQuery;
        for (int family = 0; family < 3; ++family)
        {
            uint count = family == 0 ? fixture.RayCount :
                (family == 1 ? fixture.SphereCastCount : fixture.OverlapCount);
            ulong expected = count / 2 + count % 2;
            ulong actual = family == 0 ? state.RayHitCount :
                (family == 1 ? state.SphereCastHitCount : state.OverlapHitCount);
            if (actual != expected)
            {
                string familyName = family == 0 ? "ray" : (family == 1 ? "sphere_cast" : "overlap");
                Console.Error.WriteLine($"run_failed reason=query_batch engine=bepuphysics2 phase={phase} batch={batch} family={familyName} expected={expected} actual={actual}");
                return 2;
            }
        }
        return 0;
    }

    public static int WarmupState(ref BepuSpatialQueryState state, int batchCount)
    {
        if (batchCount != state.CaseExecution.WarmupWorkUnitCount)
        {
            return 2;
        }
        for (int batch = 0; batch < batchCount; ++batch)
        {
            ExecuteBatch(
                ref state, BepuSpatialQueryBatchPhase.Warmup, out _);
            if (CheckQueryBatch(in state, "warmup", batch) != 0) return 2;
        }
        return 0;
    }

    public static int StepState(ref BepuSpatialQueryState state, int batchCount)
    {
        if (batchCount < 0 ||
            batchCount > state.RawBatchDurations.Length - state.CompletedBatchCount)
        {
            return 2;
        }
        for (int batch = 0; batch < batchCount; ++batch)
        {
            int slot = state.CompletedBatchCount + batch;
            ExecuteBatch(
                ref state,
                BepuSpatialQueryBatchPhase.Measured,
                out long batchDuration);
            if (CheckQueryBatch(in state, "measured", slot) != 0) return 2;
            state.RawBatchDurations[slot] = batchDuration;
            state.LatestBatchElapsedMilliseconds =
                batchDuration * 1000.0 / Stopwatch.Frequency;
            state.WorkloadElapsedMilliseconds += state.LatestBatchElapsedMilliseconds;
        }
        state.CompletedBatchCount += batchCount;
        return 0;
    }

    public static void CaptureDebugSamples(ref BepuSpatialQueryState state)
    {
        CaseExecutionSpatialQuery fixture = state.CaseExecution.SpatialQuery;
        int samples = (int)fixture.DebugSamplesPerFamily;
        for (int index = 0; index < samples; ++index)
        {
            BepuRayInput input = state.RayInputs[index];
            BepuClosestRayHitHandler handler = new() { Distance = fixture.QueryDistance };
            state.Simulation.RayCast(
                input.Origin,
                input.Direction,
                fixture.QueryDistance,
                state.BufferPool,
                ref handler);
            state.DebugHits[index] = handler.Hit;
            state.DebugHitDistances[index] = handler.Distance;
        }
        for (int index = 0; index < samples; ++index)
        {
            BepuSphereCastInput input = state.SphereCastInputs[index];
            BepuClosestSweepHitHandler handler = new() { Distance = fixture.QueryDistance };
            state.Simulation.Sweep(
                state.Sphere,
                input.Pose,
                input.Velocity,
                fixture.QueryDistance,
                state.BufferPool,
                ref handler);
            int debugIndex = samples + index;
            state.DebugHits[debugIndex] = handler.Hit;
            state.DebugHitDistances[debugIndex] = handler.Distance;
        }
        for (int index = 0; index < samples; ++index)
        {
            BepuOverlapInput input = state.OverlapInputs[index];
            BepuAnyOverlapEnumerator enumerator = default;
            state.Simulation.BroadPhase.GetOverlaps(
                input.Minimum,
                input.Maximum,
                state.BufferPool,
                ref enumerator);
            int debugIndex = 2 * samples + index;
            state.DebugHits[debugIndex] = enumerator.Hit;
            state.DebugHitDistances[debugIndex] = fixture.QueryDistance;
        }
    }

    public static unsafe void ExecuteBatch(
        ref BepuSpatialQueryState state,
        BepuSpatialQueryBatchPhase phase,
        out long batchDuration)
    {
        CaseExecutionSpatialQuery fixture = state.CaseExecution.SpatialQuery;
        long batchStart = Stopwatch.GetTimestamp();
        long rayStart = Stopwatch.GetTimestamp();
        ulong rayHitCount = 0;
        if (state.ThreadCount == 1)
        {
            for (int index = 0; index < fixture.RayCount; ++index)
            {
                BepuRayInput input = state.RayInputs[index];
                BepuClosestRayHitHandler handler = new() { Distance = fixture.QueryDistance };
                state.Simulation.RayCast(
                    input.Origin,
                    input.Direction,
                    fixture.QueryDistance,
                    state.BufferPool,
                    ref handler);
                rayHitCount += handler.Hit;
            }
        }
        else
        {
            state.ThreadDispatcher.DispatchWorkers(state.RayWorker, state.ThreadCount);
            rayHitCount = ReduceLaneHitCounts(in state);
        }
        state.RayHitCount = rayHitCount;
        long rayEnd = Stopwatch.GetTimestamp();
        long castStart = Stopwatch.GetTimestamp();
        ulong sphereCastHitCount = 0;
        if (state.ThreadCount == 1)
        {
            for (int index = 0; index < fixture.SphereCastCount; ++index)
            {
                BepuSphereCastInput input = state.SphereCastInputs[index];
                BepuClosestSweepHitHandler handler = new() { Distance = fixture.QueryDistance };
                state.Simulation.Sweep(
                    state.Sphere,
                    input.Pose,
                    input.Velocity,
                    fixture.QueryDistance,
                    state.BufferPool,
                    ref handler);
                sphereCastHitCount += handler.Hit;
            }
        }
        else
        {
            state.ThreadDispatcher.DispatchWorkers(state.SphereCastWorker, state.ThreadCount);
            sphereCastHitCount = ReduceLaneHitCounts(in state);
        }
        state.SphereCastHitCount = sphereCastHitCount;
        long castEnd = Stopwatch.GetTimestamp();
        long overlapStart = Stopwatch.GetTimestamp();
        ulong overlapHitCount = 0;
        if (state.ThreadCount == 1)
        {
            for (int index = 0; index < fixture.OverlapCount; ++index)
            {
                BepuOverlapInput input = state.OverlapInputs[index];
                BepuAnyOverlapEnumerator enumerator = default;
                state.Simulation.BroadPhase.GetOverlaps(
                    input.Minimum,
                    input.Maximum,
                    state.BufferPool,
                    ref enumerator);
                overlapHitCount += enumerator.Hit;
            }
        }
        else
        {
            state.ThreadDispatcher.DispatchWorkers(state.OverlapWorker, state.ThreadCount);
            overlapHitCount = ReduceLaneHitCounts(in state);
        }
        state.OverlapHitCount = overlapHitCount;
        long overlapEnd = Stopwatch.GetTimestamp();
        batchDuration = Stopwatch.GetTimestamp() - batchStart;
        if (phase == BepuSpatialQueryBatchPhase.Measured)
        {
            state.RayElapsed += rayEnd - rayStart;
            state.SphereCastElapsed += castEnd - castStart;
            state.OverlapElapsed += overlapEnd - overlapStart;
        }
    }

    public static ulong ReduceLaneHitCounts(in BepuSpatialQueryState state)
    {
        ulong hitCount = 0;
        for (int workerIndex = 0; workerIndex < state.ThreadCount; ++workerIndex)
        {
            hitCount += state.LaneHitCounts[workerIndex];
        }
        return hitCount;
    }

    public static BepuObservationRow[] BuildHeadlessObservations(
        in BepuSpatialQueryState state)
    {
        CaseExecutionSpatialQuery fixture = state.CaseExecution.SpatialQuery;
        return
        [
            FloatObservation("ray_queries_per_second",
                QueryRate((int)fixture.RayCount, state.CompletedBatchCount, state.RayElapsed)),
            FloatObservation("sphere_cast_queries_per_second",
                QueryRate((int)fixture.SphereCastCount, state.CompletedBatchCount, state.SphereCastElapsed)),
            FloatObservation("overlap_queries_per_second",
                QueryRate((int)fixture.OverlapCount, state.CompletedBatchCount, state.OverlapElapsed)),
            UIntObservation("ray_hit_count", state.RayHitCount),
            UIntObservation("sphere_cast_hit_count", state.SphereCastHitCount),
            UIntObservation("overlap_hit_count", state.OverlapHitCount)
        ];
    }

    public static BepuObservationRow FloatObservation(string metricId, double value)
    {
        return new BepuObservationRow
        {
            MetricId = metricId,
            PhaseId = "final",
            SampleIndex = 0,
            ValueType = BepuObservationValueType.Float64,
            ValueBits = DoubleBits(value)
        };
    }

    public static BepuObservationRow UIntObservation(string metricId, ulong value)
    {
        return new BepuObservationRow
        {
            MetricId = metricId,
            PhaseId = "final",
            SampleIndex = 0,
            ValueType = BepuObservationValueType.Uint64,
            ValueBits = value
        };
    }

    public static double QueryRate(int queryCount, int batchCount, long elapsedTicks)
    {
        return elapsedTicks > 0 ?
            queryCount * (double)batchCount * Stopwatch.Frequency / elapsedTicks : 0.0;
    }

    public static ulong DoubleBits(double value)
    {
        return unchecked((ulong)BitConverter.DoubleToInt64Bits(value));
    }

    public static float CenteredGridCoordinate(
        float baseCenter, float spacing, uint coordinate, uint count)
    {
        return baseCenter + spacing * (coordinate - 0.5f * (count - 1));
    }

    public static float UncenteredGridCoordinate(
        float baseCenter, float spacing, uint coordinate)
    {
        return baseCenter + spacing * coordinate;
    }

    public static BepuSpatialQuery GenerateQuery(in CaseExecutionSpec execution, int index)
    {
        CaseExecutionSpatialQuery fixture = execution.SpatialQuery;
        int familyIndex = index;
        if (index >= fixture.RayCount + fixture.SphereCastCount)
        {
            familyIndex -= (int)(fixture.RayCount + fixture.SphereCastCount);
        }
        else if (index >= fixture.RayCount)
        {
            familyIndex -= (int)fixture.RayCount;
        }
        int sample = familyIndex / 2;
        int intendedHit = (familyIndex & 1) == 0 ? 1 : 0;
        uint slot = (uint)sample % execution.StaticBodyCount;
        uint ix = slot % fixture.StaticGrid[0];
        uint iz = (slot / fixture.StaticGrid[0]) % fixture.StaticGrid[2];
        uint iy = slot / (fixture.StaticGrid[0] * fixture.StaticGrid[2]);
        Vector3 center = new(
            CenteredGridCoordinate(fixture.StaticBaseCenter.X, fixture.StaticSpacing.X,
                ix, fixture.StaticGrid[0]),
            UncenteredGridCoordinate(fixture.StaticBaseCenter.Y, fixture.StaticSpacing.Y, iy),
            CenteredGridCoordinate(fixture.StaticBaseCenter.Z, fixture.StaticSpacing.Z,
                iz, fixture.StaticGrid[2]));
        Vector3 sceneMinimum = new(
            CenteredGridCoordinate(fixture.StaticBaseCenter.X, fixture.StaticSpacing.X,
                0, fixture.StaticGrid[0]) - fixture.StaticHalfExtents.X,
            fixture.StaticBaseCenter.Y - fixture.StaticHalfExtents.Y,
            CenteredGridCoordinate(fixture.StaticBaseCenter.Z, fixture.StaticSpacing.Z,
                0, fixture.StaticGrid[2]) - fixture.StaticHalfExtents.Z);
        Vector3 sceneMaximum = new(
            CenteredGridCoordinate(fixture.StaticBaseCenter.X, fixture.StaticSpacing.X,
                fixture.StaticGrid[0] - 1, fixture.StaticGrid[0]) + fixture.StaticHalfExtents.X,
            UncenteredGridCoordinate(fixture.StaticBaseCenter.Y, fixture.StaticSpacing.Y,
                fixture.StaticGrid[1] - 1) + fixture.StaticHalfExtents.Y,
            CenteredGridCoordinate(fixture.StaticBaseCenter.Z, fixture.StaticSpacing.Z,
                fixture.StaticGrid[2] - 1, fixture.StaticGrid[2]) + fixture.StaticHalfExtents.Z);
        int face = sample % 6;
        int axis = face / 2;
        if (index >= fixture.RayCount + fixture.SphereCastCount)
        {
            Vector3 overlapCenter = center;
            if (intendedHit == 0)
            {
                overlapCenter = SetComponent(
                    overlapCenter, axis, GetComponent(sceneMaximum, axis) + fixture.MissOffset);
            }
            return new BepuSpatialQuery
            {
                OriginOrCenter = overlapCenter
            };
        }
        int faceSign = (face & 1) != 0 ? 1 : -1;
        int firstTransverse = axis == 0 ? 1 : 0;
        Vector3 origin = default;
        Vector3 direction = default;
        for (int component = 0; component < 3; ++component)
        {
            if (component == axis)
            {
                origin = SetComponent(origin, component,
                    faceSign > 0 ?
                        GetComponent(sceneMaximum, component) + 5f :
                        GetComponent(sceneMinimum, component) - 5f);
                direction = SetComponent(direction, component, faceSign > 0 ? -1f : 1f);
            }
            else
            {
                origin = SetComponent(
                    origin,
                    component,
                    intendedHit == 0 && component == firstTransverse ?
                        GetComponent(sceneMaximum, component) + fixture.MissOffset :
                        GetComponent(center, component));
            }
        }
        return new BepuSpatialQuery { OriginOrCenter = origin, Direction = direction };
    }

    public static float GetComponent(Vector3 value, int component)
    {
        return component == 0 ? value.X : component == 1 ? value.Y : value.Z;
    }

    public static Vector3 SetComponent(
        Vector3 value, int component, float componentValue)
    {
        if (component == 0) value.X = componentValue;
        else if (component == 1) value.Y = componentValue;
        else value.Z = componentValue;
        return value;
    }
}
