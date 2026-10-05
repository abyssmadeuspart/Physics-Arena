using System;
using System.Diagnostics;
using Unity.Burst;
using Unity.Collections;
using Unity.Collections.LowLevel.Unsafe;
using Unity.Entities;
using Unity.Jobs;
using Unity.Jobs.LowLevel.Unsafe;
using Unity.Mathematics;
using Unity.Physics;
using PhysicsBoxCollider = Unity.Physics.BoxCollider;
using PhysicsCollider = Unity.Physics.Collider;
using PhysicsSphereCollider = Unity.Physics.SphereCollider;

namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
    public struct UnityPhysicsSpatialQuery
    {
        public float3 OriginOrCenter;
        public float3 Direction;
    }

    public enum UnityPhysicsSpatialQueryBatchPhase : byte
    {
        Warmup,
        Measured
    }

    [BurstCompile(OptimizeFor = OptimizeFor.Performance)]
    public struct UnityPhysicsSpatialRayJob : IJob
    {
        [ReadOnly] public PhysicsWorld World;
        [ReadOnly] public NativeArray<RaycastInput> Inputs;
        // each scheduled lane writes one unique HitCountIndex
        [NativeDisableParallelForRestriction]
        public NativeArray<ulong> HitCounts;
        public int StartIndex;
        public int EndIndex;
        public int HitCountIndex;

        public void Execute()
        {
            ulong hitCount = 0;
            for (int index = StartIndex; index < EndIndex; ++index)
            {
                byte hit = World.CastRay(
                    Inputs[index], out RaycastHit closestHit) ? (byte)1 : (byte)0;
                hitCount += hit;
            }
            HitCounts[HitCountIndex] = hitCount;
        }
    }

    [BurstCompile(OptimizeFor = OptimizeFor.Performance)]
    public struct UnityPhysicsSpatialSphereCastJob : IJob
    {
        [ReadOnly] public PhysicsWorld World;
        [ReadOnly] public NativeArray<ColliderCastInput> Inputs;
        // each scheduled lane writes one unique HitCountIndex
        [NativeDisableParallelForRestriction]
        public NativeArray<ulong> HitCounts;
        public int StartIndex;
        public int EndIndex;
        public int HitCountIndex;

        public void Execute()
        {
            ulong hitCount = 0;
            for (int index = StartIndex; index < EndIndex; ++index)
            {
                byte hit = World.CastCollider(
                    Inputs[index], out ColliderCastHit closestHit) ?
                    (byte)1 : (byte)0;
                hitCount += hit;
            }
            HitCounts[HitCountIndex] = hitCount;
        }
    }

    [BurstCompile(OptimizeFor = OptimizeFor.Performance)]
    public struct UnityPhysicsSpatialOverlapJob : IJob
    {
        [ReadOnly] public PhysicsWorld World;
        [ReadOnly] public NativeArray<OverlapAabbInput> Inputs;
        public NativeList<int> OverlapHits;
        // each scheduled lane writes one unique HitCountIndex
        [NativeDisableParallelForRestriction]
        public NativeArray<ulong> HitCounts;
        public int StartIndex;
        public int EndIndex;
        public int HitCountIndex;

        public void Execute()
        {
            ulong hitCount = 0;
            for (int index = StartIndex; index < EndIndex; ++index)
            {
                OverlapHits.Clear();
                byte hit = World.OverlapAabb(Inputs[index], ref OverlapHits) ?
                    (byte)1 : (byte)0;
                hitCount += hit;
            }
            HitCounts[HitCountIndex] = hitCount;
        }
    }

    [BurstCompile(OptimizeFor = OptimizeFor.Performance)]
    public struct UnityPhysicsSpatialDebugCaptureJob : IJob
    {
        [ReadOnly] public PhysicsWorld World;
        [ReadOnly] public NativeArray<RaycastInput> RayInputs;
        [ReadOnly] public NativeArray<ColliderCastInput> SphereCastInputs;
        [ReadOnly] public NativeArray<OverlapAabbInput> OverlapInputs;
        public NativeList<int> OverlapHits;
        public NativeArray<byte> DebugHits;
        public NativeArray<float> DebugHitDistances;
        public int DebugSamplesPerFamily;
        public float QueryDistance;

        public void Execute()
        {
            for (int index = 0; index < DebugSamplesPerFamily; ++index)
            {
                byte hit = World.CastRay(
                    RayInputs[index], out RaycastHit closestHit) ?
                    (byte)1 : (byte)0;
                DebugHits[index] = hit;
                DebugHitDistances[index] = hit != 0 ?
                    closestHit.Fraction *
                        QueryDistance : QueryDistance;
            }
            for (int index = 0; index < DebugSamplesPerFamily; ++index)
            {
                byte hit = World.CastCollider(
                    SphereCastInputs[index], out ColliderCastHit closestHit) ?
                    (byte)1 : (byte)0;
                int debugIndex = DebugSamplesPerFamily + index;
                DebugHits[debugIndex] = hit;
                DebugHitDistances[debugIndex] = hit != 0 ?
                    closestHit.Fraction *
                        QueryDistance : QueryDistance;
            }
            for (int index = 0; index < DebugSamplesPerFamily; ++index)
            {
                OverlapHits.Clear();
                byte hit = World.OverlapAabb(
                    OverlapInputs[index], ref OverlapHits) ? (byte)1 : (byte)0;
                int debugIndex = 2 * DebugSamplesPerFamily + index;
                DebugHits[debugIndex] = hit;
                DebugHitDistances[debugIndex] =
                    QueryDistance;
            }
        }
    }

    public struct UnityPhysicsSpatialQueryState
    {
        public CaseExecutionSpec CaseExecution;
        public PhysicsWorld World;
        public NativeArray<RaycastInput> RayInputs;
        public NativeArray<ColliderCastInput> SphereCastInputs;
        public NativeArray<OverlapAabbInput> OverlapInputs;
        public NativeArray<byte> DebugHits;
        public NativeArray<float> DebugHitDistances;
        public NativeArray<ulong> HitCounts;
        public UnsafeList<NativeList<int>> OverlapHitLists;
        public NativeArray<JobHandle> JobHandles;
        public UnityPhysicsSpatialRayJob RayJob;
        public UnityPhysicsSpatialSphereCastJob SphereCastJob;
        public UnityPhysicsSpatialOverlapJob OverlapJob;
        public NativeArray<long> RawBatchDurations;
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

    public static partial class UnityPhysicsBenchmarkRunner
    {
        public static UnityPhysicsCaseDescriptor UnityPhysicsSpatialQueryTraceDescriptor()
        {
            return new UnityPhysicsCaseDescriptor
            {
                EngineId = "unity_physics"
            };
        }

        public static UnityPhysicsCaseRegistration UnityPhysicsSpatialQueryTraceRegistration()
        {
            return new UnityPhysicsCaseRegistration
            {
                Descriptor = UnityPhysicsSpatialQueryTraceDescriptor(),
                RunHeadless = RunUnityPhysicsSpatialQueryTrace,
                BuildVisualScene = BuildUnityPhysicsSpatialQueryVisualScene,
                BuildVisualDebugPrimitives = BuildUnityPhysicsSpatialQueryVisualDebugPrimitives
            };
        }

        public static int BuildUnityPhysicsSpatialQueryVisualDebugPrimitives(
            ref UnityPhysicsCaseView state,
            UnityPhysicsVisualDebugPrimitive[] primitives)
        {
            if (primitives == null)
            {
                return 2;
            }
            CaseExecutionSpec execution = state.Execution;
            CaseExecutionSpatialQuery fixture = execution.SpatialQuery;
            int samples = (int)fixture.DebugSamplesPerFamily;
            if (primitives.Length < execution.VisualDebugPrimitiveCount)
            {
                return 2;
            }
            for (int local = 0; local < samples; ++local)
            {
                for (int family = 0; family < 3; ++family)
                {
                    int primitiveIndex = family * samples + local;
                    UnityPhysicsVisualDebugPrimitive primitive =
                        new UnityPhysicsVisualDebugPrimitive
                        {
                            Kind = (uint)family,
                            MaterialIndex =
                                state.DebugHits[primitiveIndex] != 0 ? 6u : 7u
                        };
                    if (family == 0)
                    {
                        RaycastInput input = state.RayInputs[local];
                        primitive.OriginOrCenterX = input.Start.x;
                        primitive.OriginOrCenterY = input.Start.y;
                        primitive.OriginOrCenterZ = input.Start.z;
                        float fraction =
                            state.DebugHitDistances[primitiveIndex] /
                            fixture.QueryDistance;
                        float3 end = math.lerp(input.Start, input.End, fraction);
                        primitive.EndOrHalfExtentsX = end.x;
                        primitive.EndOrHalfExtentsY = end.y;
                        primitive.EndOrHalfExtentsZ = end.z;
                    }
                    else if (family == 1)
                    {
                        ColliderCastInput input =
                            state.SphereCastInputs[local];
                        primitive.OriginOrCenterX = input.Start.x;
                        primitive.OriginOrCenterY = input.Start.y;
                        primitive.OriginOrCenterZ = input.Start.z;
                        float fraction =
                            state.DebugHitDistances[primitiveIndex] /
                            fixture.QueryDistance;
                        float3 end = math.lerp(input.Start, input.End, fraction);
                        primitive.EndOrHalfExtentsX = end.x;
                        primitive.EndOrHalfExtentsY = end.y;
                        primitive.EndOrHalfExtentsZ = end.z;
                        primitive.Radius = fixture.SphereCastRadius;
                    }
                    else
                    {
                        OverlapAabbInput input =
                            state.OverlapInputs[local];
                        float3 center = (input.Aabb.Min + input.Aabb.Max) * 0.5f;
                        primitive.OriginOrCenterX = center.x;
                        primitive.OriginOrCenterY = center.y;
                        primitive.OriginOrCenterZ = center.z;
                        primitive.EndOrHalfExtentsX =
                            fixture.OverlapHalfExtents.x;
                        primitive.EndOrHalfExtentsY =
                            fixture.OverlapHalfExtents.y;
                        primitive.EndOrHalfExtentsZ =
                            fixture.OverlapHalfExtents.z;
                    }
                    primitives[primitiveIndex] = primitive;
                }
            }
            return 0;
        }

        public static string UnityPhysicsSpatialQueryVisualSettings(
            in CaseExecutionSpec execution, int workerCount)
        {
            CaseExecutionSpatialQuery fixture = execution.SpatialQuery;
            return FormattableString.Invariant(
                $"query_world=static_only; worker_count={workerCount}; rays={fixture.RayCount}; sphere_casts={fixture.SphereCastCount}; overlaps={fixture.OverlapCount}");
        }

        public static int CreateUnityPhysicsSpatialQueryState(
            in CaseExecutionSpec caseExecution,
            int threadCount,
            RecordingMode recordingMode,
            BlobAssetReference<PhysicsCollider> worldCollider,
            BlobAssetReference<PhysicsCollider> sphereCollider,
            out UnityPhysicsSpatialQueryState state)
        {
            state = default;
            CaseExecutionSpatialQuery fixture = caseExecution.SpatialQuery;
            if (threadCount <= 0 || fixture.RayCount < threadCount ||
                fixture.SphereCastCount < threadCount ||
                fixture.OverlapCount < threadCount ||
                JobsUtility.JobWorkerCount != math.max(0, threadCount - 1))
            {
                return 2;
            }
            state.CaseExecution = caseExecution;
            state.ThreadCount = threadCount;
            state.World = new PhysicsWorld(
                (int)caseExecution.StaticBodyCount, 0, 0);
            state.RayInputs = new NativeArray<RaycastInput>(
                (int)fixture.RayCount,
                Allocator.Persistent,
                NativeArrayOptions.UninitializedMemory);
            state.SphereCastInputs = new NativeArray<ColliderCastInput>(
                (int)fixture.SphereCastCount,
                Allocator.Persistent,
                NativeArrayOptions.UninitializedMemory);
            state.OverlapInputs = new NativeArray<OverlapAabbInput>(
                (int)fixture.OverlapCount,
                Allocator.Persistent,
                NativeArrayOptions.UninitializedMemory);
            if (recordingMode == RecordingMode.On)
            {
                state.DebugHits = new NativeArray<byte>(
                    (int)caseExecution.VisualDebugPrimitiveCount,
                    Allocator.Persistent,
                    NativeArrayOptions.ClearMemory);
                state.DebugHitDistances = new NativeArray<float>(
                    (int)caseExecution.VisualDebugPrimitiveCount,
                    Allocator.Persistent,
                    NativeArrayOptions.UninitializedMemory);
                for (int index = 0;
                    index < caseExecution.VisualDebugPrimitiveCount;
                    ++index)
                {
                    state.DebugHitDistances[index] = fixture.QueryDistance;
                }
            }
            state.HitCounts = new NativeArray<ulong>(
                3 * threadCount,
                Allocator.Persistent,
                NativeArrayOptions.ClearMemory);
            state.OverlapHitLists = new UnsafeList<NativeList<int>>(
                threadCount, Allocator.Persistent);
            for (int laneIndex = 0; laneIndex < threadCount; ++laneIndex)
            {
                NativeList<int> laneHits = new NativeList<int>(1, Allocator.Persistent);
                state.OverlapHitLists.Add(in laneHits);
            }
            state.JobHandles = new NativeArray<JobHandle>(
                threadCount,
                Allocator.Persistent,
                NativeArrayOptions.ClearMemory);
            state.RawBatchDurations = new NativeArray<long>(
                (int)caseExecution.MeasuredWorkUnitCount,
                Allocator.Persistent,
                NativeArrayOptions.ClearMemory);
            CollisionFilter queryFilter = CollisionFilter.Default;
            for (int index = 0; index < fixture.RayCount; ++index)
            {
                UnityPhysicsSpatialQuery query =
                    GenerateUnityPhysicsSpatialQuery(in caseExecution, index);
                state.RayInputs[index] = new RaycastInput
                {
                    Start = query.OriginOrCenter,
                    End = query.OriginOrCenter +
                        query.Direction * fixture.QueryDistance,
                    Filter = queryFilter
                };
            }
            for (int index = 0;
                index < fixture.SphereCastCount;
                ++index)
            {
                UnityPhysicsSpatialQuery query =
                    GenerateUnityPhysicsSpatialQuery(in caseExecution,
                        (int)fixture.RayCount + index);
                state.SphereCastInputs[index] = new ColliderCastInput(
                    sphereCollider,
                    query.OriginOrCenter,
                    query.OriginOrCenter +
                        query.Direction * fixture.QueryDistance,
                    quaternion.identity);
            }
            float3 overlapHalfExtent = fixture.OverlapHalfExtents;
            for (int index = 0;
                index < fixture.OverlapCount;
                ++index)
            {
                UnityPhysicsSpatialQuery query =
                    GenerateUnityPhysicsSpatialQuery(in caseExecution,
                        (int)(fixture.RayCount + fixture.SphereCastCount) + index);
                state.OverlapInputs[index] = new OverlapAabbInput
                {
                    Aabb = new Aabb
                    {
                        Min = query.OriginOrCenter - overlapHalfExtent,
                        Max = query.OriginOrCenter + overlapHalfExtent
                    },
                    Filter = queryFilter
                };
            }
            NativeArray<RigidBody> staticBodies = state.World.StaticBodies;
            for (uint iy = 0; iy < fixture.StaticGrid.y; ++iy)
            {
                for (uint iz = 0; iz < fixture.StaticGrid.z; ++iz)
                {
                    for (uint ix = 0; ix < fixture.StaticGrid.x; ++ix)
                    {
                        int slot = (int)((iy * fixture.StaticGrid.z + iz) *
                            fixture.StaticGrid.x + ix);
                        staticBodies[slot] = new RigidBody
                        {
                            WorldFromBody = new RigidTransform(
                                UnityPhysicsShapeRotation(caseExecution.SelectedGeometry.Axis),
                                new float3(
                                    UnityPhysicsSpatialCenteredGridCoordinate(
                                        fixture.StaticBaseCenter.x, fixture.StaticSpacing.x,
                                        ix, fixture.StaticGrid.x),
                                    UnityPhysicsSpatialUncenteredGridCoordinate(
                                        fixture.StaticBaseCenter.y, fixture.StaticSpacing.y,
                                        iy),
                                    UnityPhysicsSpatialCenteredGridCoordinate(
                                        fixture.StaticBaseCenter.z, fixture.StaticSpacing.z,
                                        iz, fixture.StaticGrid.z))),
                            Collider = worldCollider,
                            Entity = Entity.Null,
                            CustomTags = 0,
                            Scale = 1.0f
                        };
                    }
                }
            }
            state.World.UpdateIndexMaps();
            state.World.CollisionWorld.BuildBroadphase(
                ref state.World, 1.0f / caseExecution.TimestepHz,
                caseExecution.Gravity, true);
            if (recordingMode == RecordingMode.On)
            {
                new UnityPhysicsSpatialDebugCaptureJob
                {
                    World = state.World,
                    RayInputs = state.RayInputs,
                    SphereCastInputs = state.SphereCastInputs,
                    OverlapInputs = state.OverlapInputs,
                    OverlapHits = state.OverlapHitLists[0],
                    DebugHits = state.DebugHits,
                    DebugHitDistances = state.DebugHitDistances,
                    DebugSamplesPerFamily = (int)fixture.DebugSamplesPerFamily,
                    QueryDistance = fixture.QueryDistance
                }.Run();
            }
            state.RayJob = new UnityPhysicsSpatialRayJob
            {
                World = state.World,
                Inputs = state.RayInputs,
                HitCounts = state.HitCounts
            };
            state.SphereCastJob = new UnityPhysicsSpatialSphereCastJob
            {
                World = state.World,
                Inputs = state.SphereCastInputs,
                HitCounts = state.HitCounts
            };
            state.OverlapJob = new UnityPhysicsSpatialOverlapJob
            {
                World = state.World,
                Inputs = state.OverlapInputs,
                HitCounts = state.HitCounts
            };
            return state.World.NumDynamicBodies == 0 &&
                state.World.NumStaticBodies == caseExecution.StaticBodyCount ?
                0 : 2;
        }

        public static int CheckQueryBatch(in UnityPhysicsSpatialQueryState state, string phase, int batch)
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
                    Console.Error.WriteLine($"run_failed reason=query_batch engine=unity_physics phase={phase} batch={batch} family={familyName} expected={expected} actual={actual}");
                    return 2;
                }
            }
            return 0;
        }

        public static int WarmupUnityPhysicsSpatialQuery(
            ref UnityPhysicsSpatialQueryState state, int batchCount)
        {
            if (batchCount != state.CaseExecution.WarmupWorkUnitCount)
            {
                return 2;
            }
            for (int batch = 0; batch < batchCount; ++batch)
            {
                ExecuteUnityPhysicsSpatialQueryBatch(
                    ref state, UnityPhysicsSpatialQueryBatchPhase.Warmup, out _);
                if (CheckQueryBatch(in state, "warmup", batch) != 0) return 2;
            }
            return 0;
        }

        public static int StepUnityPhysicsSpatialQuery(
            ref UnityPhysicsSpatialQueryState state,
            int batchCount,
            out long latestRawDuration)
        {
            latestRawDuration = 0;
            if (batchCount < 0 ||
                batchCount > state.RawBatchDurations.Length - state.CompletedBatchCount)
            {
                return 2;
            }
            for (int batch = 0; batch < batchCount; ++batch)
            {
                int slot = state.CompletedBatchCount + batch;
                ExecuteUnityPhysicsSpatialQueryBatch(
                    ref state,
                    UnityPhysicsSpatialQueryBatchPhase.Measured,
                    out latestRawDuration);
                if (CheckQueryBatch(in state, "measured", slot) != 0) return 2;
                state.RawBatchDurations[slot] = latestRawDuration;
                state.LatestBatchElapsedMilliseconds =
                    latestRawDuration * 1000.0 / Stopwatch.Frequency;
                state.WorkloadElapsedMilliseconds +=
                    state.LatestBatchElapsedMilliseconds;
            }
            state.CompletedBatchCount += batchCount;
            return 0;
        }

        public static void ExecuteUnityPhysicsSpatialQueryBatch(
            ref UnityPhysicsSpatialQueryState state,
            UnityPhysicsSpatialQueryBatchPhase phase,
            out long rawBatchDuration)
        {
            long batchStart = Stopwatch.GetTimestamp();
            long rayStart = Stopwatch.GetTimestamp();
            if (state.ThreadCount == 1)
            {
                UnityPhysicsSpatialRayJob job = state.RayJob;
                job.StartIndex = 0;
                job.EndIndex = state.RayInputs.Length;
                job.HitCountIndex = 0;
                // the one-thread baseline runs directly without scheduler overhead
                job.Run();
            }
            else
            {
                for (int laneIndex = 0; laneIndex < state.ThreadCount; ++laneIndex)
                {
                    UnityPhysicsSpatialRayJob job = state.RayJob;
                    job.StartIndex = state.RayInputs.Length * laneIndex / state.ThreadCount;
                    job.EndIndex = state.RayInputs.Length * (laneIndex + 1) / state.ThreadCount;
                    job.HitCountIndex = laneIndex;
                    state.JobHandles[laneIndex] = job.Schedule();
                }
                // all disjoint ray lanes complete before reduction and the next family
                JobHandle.CombineDependencies(state.JobHandles).Complete();
            }
            state.RayHitCount = ReduceUnityPhysicsSpatialQueryHitCounts(
                in state, 0);
            long rayEnd = Stopwatch.GetTimestamp();

            long castStart = Stopwatch.GetTimestamp();
            if (state.ThreadCount == 1)
            {
                UnityPhysicsSpatialSphereCastJob job = state.SphereCastJob;
                job.StartIndex = 0;
                job.EndIndex = state.SphereCastInputs.Length;
                job.HitCountIndex = state.ThreadCount;
                // the one-thread baseline runs directly without scheduler overhead
                job.Run();
            }
            else
            {
                for (int laneIndex = 0; laneIndex < state.ThreadCount; ++laneIndex)
                {
                    UnityPhysicsSpatialSphereCastJob job = state.SphereCastJob;
                    job.StartIndex = state.SphereCastInputs.Length * laneIndex /
                        state.ThreadCount;
                    job.EndIndex = state.SphereCastInputs.Length * (laneIndex + 1) /
                        state.ThreadCount;
                    job.HitCountIndex = state.ThreadCount + laneIndex;
                    state.JobHandles[laneIndex] = job.Schedule();
                }
                // all disjoint cast lanes complete before reduction and the next family
                JobHandle.CombineDependencies(state.JobHandles).Complete();
            }
            state.SphereCastHitCount = ReduceUnityPhysicsSpatialQueryHitCounts(
                in state, 1);
            long castEnd = Stopwatch.GetTimestamp();

            long overlapStart = Stopwatch.GetTimestamp();
            if (state.ThreadCount == 1)
            {
                UnityPhysicsSpatialOverlapJob job = state.OverlapJob;
                job.StartIndex = 0;
                job.EndIndex = state.OverlapInputs.Length;
                job.HitCountIndex = 2 * state.ThreadCount;
                job.OverlapHits = state.OverlapHitLists[0];
                // the one-thread baseline runs directly without scheduler overhead
                job.Run();
            }
            else
            {
                for (int laneIndex = 0; laneIndex < state.ThreadCount; ++laneIndex)
                {
                    UnityPhysicsSpatialOverlapJob job = state.OverlapJob;
                    job.StartIndex = state.OverlapInputs.Length * laneIndex /
                        state.ThreadCount;
                    job.EndIndex = state.OverlapInputs.Length * (laneIndex + 1) /
                        state.ThreadCount;
                    job.HitCountIndex = 2 * state.ThreadCount + laneIndex;
                    job.OverlapHits = state.OverlapHitLists[laneIndex];
                    state.JobHandles[laneIndex] = job.Schedule();
                }
                // all lane-local overlap collectors complete before reduction
                JobHandle.CombineDependencies(state.JobHandles).Complete();
            }
            state.OverlapHitCount = ReduceUnityPhysicsSpatialQueryHitCounts(
                in state, 2);
            long overlapEnd = Stopwatch.GetTimestamp();
            rawBatchDuration = Stopwatch.GetTimestamp() - batchStart;
            if (phase == UnityPhysicsSpatialQueryBatchPhase.Measured)
            {
                state.RayElapsed += rayEnd - rayStart;
                state.SphereCastElapsed += castEnd - castStart;
                state.OverlapElapsed += overlapEnd - overlapStart;
            }
        }

        public static ulong ReduceUnityPhysicsSpatialQueryHitCounts(
            in UnityPhysicsSpatialQueryState state, int familyIndex)
        {
            ulong hitCount = 0;
            int offset = familyIndex * state.ThreadCount;
            for (int laneIndex = 0; laneIndex < state.ThreadCount; ++laneIndex)
            {
                hitCount += state.HitCounts[offset + laneIndex];
            }
            return hitCount;
        }

        public static int BuildUnityPhysicsSpatialQueryVisualScene(
            ref UnityPhysicsCaseView state,
            UnityPhysicsVisualGeometry[] geometries,

            ref UnityPhysicsVisualMeshStorage meshes,
            UnityPhysicsVisualInstance[] instances,
            out int geometryCount,
            out int instanceCount)
        {
            geometryCount = 0;
            instanceCount = 0;
            if (geometries == null || instances == null)
            {
                return 2;
            }
            CaseExecutionSpec execution = state.Execution;
            CaseExecutionSpatialQuery fixture = execution.SpatialQuery;
            if (geometries.Length < 1 || instances.Length < execution.StaticBodyCount)
            {
                return 2;
            }
            if (BuildResolvedVisualGeometry(in execution, in execution.SelectedGeometry,
            ref meshes, out geometries[0]) != 0) return 2;
            quaternion rotation = UnityPhysicsShapeRotation(execution.SelectedGeometry.Axis);
            for (uint iy = 0; iy < fixture.StaticGrid.y; ++iy)
            {
                for (uint iz = 0; iz < fixture.StaticGrid.z; ++iz)
                {
                    for (uint ix = 0; ix < fixture.StaticGrid.x; ++ix)
                    {
                        int slot = (int)((iy * fixture.StaticGrid.z + iz) *
                            fixture.StaticGrid.x + ix);
                        instances[slot] = new UnityPhysicsVisualInstance
                        {
                            GeometryIndex = 0,
                            StableSlot = (uint)slot,
                            TransformSlot = uint.MaxValue,
                            InitialTransform = new UnityPhysicsTransform
                            {
                                PositionX = UnityPhysicsSpatialCenteredGridCoordinate(
                                    fixture.StaticBaseCenter.x, fixture.StaticSpacing.x,
                                    ix, fixture.StaticGrid.x),
                                PositionY = UnityPhysicsSpatialUncenteredGridCoordinate(
                                    fixture.StaticBaseCenter.y, fixture.StaticSpacing.y,
                                    iy),
                                PositionZ = UnityPhysicsSpatialCenteredGridCoordinate(
                                    fixture.StaticBaseCenter.z, fixture.StaticSpacing.z,
                                    iz, fixture.StaticGrid.z),
                                RotationX = rotation.value.x,
                                RotationY = rotation.value.y,
                                RotationZ = rotation.value.z,
                                RotationW = rotation.value.w
                            }
                        };
                    }
                }
            }
            geometryCount = 1;
            instanceCount = (int)execution.StaticBodyCount;
            return 0;
        }

        public static UnityPhysicsObservationRow[] BuildUnityPhysicsSpatialQueryObservations(
            ref UnityPhysicsSpatialQueryState state)
        {
            CaseExecutionSpatialQuery fixture = state.CaseExecution.SpatialQuery;
            return new[]
            {
                UnityPhysicsFloatObservation(
                    "ray_queries_per_second",
                    UnityPhysicsQueryRate(
                        (int)fixture.RayCount,
                        state.CompletedBatchCount,
                        state.RayElapsed)),
                UnityPhysicsFloatObservation(
                    "sphere_cast_queries_per_second",
                    UnityPhysicsQueryRate(
                        (int)fixture.SphereCastCount,
                        state.CompletedBatchCount,
                        state.SphereCastElapsed)),
                UnityPhysicsFloatObservation(
                    "overlap_queries_per_second",
                    UnityPhysicsQueryRate(
                        (int)fixture.OverlapCount,
                        state.CompletedBatchCount,
                        state.OverlapElapsed)),
                UnityPhysicsUintObservation("ray_hit_count", state.RayHitCount),
                UnityPhysicsUintObservation(
                    "sphere_cast_hit_count", state.SphereCastHitCount),
                UnityPhysicsUintObservation(
                    "overlap_hit_count", state.OverlapHitCount)
            };
        }

        public static UnityPhysicsObservationRow UnityPhysicsFloatObservation(
            string metricId, double value)
        {
            return new UnityPhysicsObservationRow
            {
                MetricId = metricId,
                PhaseId = "final",
                SampleIndex = 0,
                ValueType = UnityPhysicsObservationValueType.Float64,
                ValueBits = UnityPhysicsDoubleBits(value)
            };
        }

        public static UnityPhysicsObservationRow UnityPhysicsUintObservation(
            string metricId, ulong value)
        {
            return new UnityPhysicsObservationRow
            {
                MetricId = metricId,
                PhaseId = "final",
                SampleIndex = 0,
                ValueType = UnityPhysicsObservationValueType.Uint64,
                ValueBits = value
            };
        }

        public static int RunUnityPhysicsSpatialQueryTrace(RunnerArgs runnerArgs)
        {
            if (runnerArgs.StepCount != runnerArgs.CaseExecution.MeasuredWorkUnitCount ||
                runnerArgs.WarmupSteps != runnerArgs.CaseExecution.WarmupWorkUnitCount)
            {
                return 2;
            }
            int previousWorkerCount = JobsUtility.JobWorkerCount;
            UnityPhysicsSpatialQueryState state = default;
            UnityPhysicsRecordingWriter recording = default;
            try
            {
                int requestedWorkerCount = math.max(0, runnerArgs.ThreadCount - 1);
                JobsUtility.JobWorkerCount = requestedWorkerCount;
                int effectiveWorkerCount = JobsUtility.JobWorkerCount;
                int effectiveThreadCount = effectiveWorkerCount + 1;
                if (effectiveThreadCount != runnerArgs.ThreadCount)
                {
                    return 2;
                }
                CollisionFilter filter = new CollisionFilter
                {
                    BelongsTo = 0xffffffffu,
                    CollidesWith = 0xffffffffu,
                    GroupIndex = 0
                };
                Material material = Material.Default;
                material.Friction = runnerArgs.CaseExecution.Friction;
                material.Restitution = runnerArgs.CaseExecution.Restitution;
                material.CollisionResponse = CollisionResponsePolicy.Collide;
                using BlobAssetReference<PhysicsCollider> worldCollider = CreateUnityPhysicsResolvedCollider(in runnerArgs.CaseExecution,
                    in runnerArgs.CaseExecution.SelectedGeometry, filter, material);
                using BlobAssetReference<PhysicsCollider> sphereCollider = PhysicsSphereCollider.Create(
                    new SphereGeometry
                    {
                        Center = float3.zero,
                        Radius = runnerArgs.CaseExecution.SpatialQuery.SphereCastRadius
                    },
                    filter,
                    material);
                try
                {
                    if (CreateUnityPhysicsSpatialQueryState(
                            in runnerArgs.CaseExecution,
                            effectiveThreadCount, runnerArgs.RecordingMode, worldCollider, sphereCollider,
                            out state) != 0 ||
                        WarmupUnityPhysicsSpatialQuery(
                            ref state, runnerArgs.WarmupSteps) != 0)
                    {
                        return 2;
                    }
                    UnityPhysicsCaseView capture = new UnityPhysicsCaseView
                    {
                        Execution = runnerArgs.CaseExecution,
                        World = state.World,
                        RayInputs = state.RayInputs,
                        SphereCastInputs = state.SphereCastInputs,
                        OverlapInputs = state.OverlapInputs,
                        DebugHits = state.DebugHits,
                        DebugHitDistances = state.DebugHitDistances
                    };
                    if (runnerArgs.RecordingMode == RecordingMode.On && UnityPhysicsRecording.Begin(in runnerArgs, ref capture, out recording) != 0) return 2;
                    for (int batch = 0; batch < runnerArgs.StepCount; ++batch)
                    {
                        if (StepUnityPhysicsSpatialQuery(ref state, 1, out _) != 0 ||
                            (runnerArgs.RecordingMode == RecordingMode.On && UnityPhysicsRecording.Append(ref recording, in runnerArgs.CaseRegistration,
                                ref capture, (ulong)batch + 1) != 0)) return 2;
                    }
                    if (runnerArgs.RecordingMode == RecordingMode.On && UnityPhysicsRecording.Complete(ref recording) != 0) return 2;
                    UnityPhysicsResultValidity validity =
                        state.World.NumStaticBodies ==
                            runnerArgs.CaseExecution.StaticBodyCount &&
                        state.World.NumDynamicBodies == 0 &&
                        state.CompletedBatchCount ==
                            runnerArgs.CaseExecution.MeasuredWorkUnitCount &&
                        state.WorkloadElapsedMilliseconds > 0.0 &&
                        !double.IsNaN(state.WorkloadElapsedMilliseconds) &&
                        !double.IsInfinity(state.WorkloadElapsedMilliseconds) &&
                        state.RayElapsed > 0 &&
                        state.SphereCastElapsed > 0 &&
                        state.OverlapElapsed > 0 ?
                        UnityPhysicsResultValidity.Valid :
                        UnityPhysicsResultValidity.Invalid;
                    UnityPhysicsBenchmarkResult result =
                        new UnityPhysicsBenchmarkResult
                        {
                            PhysicsSettings =
                                UnityPhysicsSpatialQueryVisualSettings(
                                    in runnerArgs.CaseExecution, effectiveWorkerCount),
                            BodyCount = (int)runnerArgs.CaseExecution.BodyCount,
                            ShapeCount = (int)runnerArgs.CaseExecution.ShapeCount,
                            QueryCount = (int)runnerArgs.CaseExecution.QueryCount,
                            ConstraintCount = (int)runnerArgs.CaseExecution.ConstraintCount,
                            InvalidTransformCount = 0,
                            EffectiveThreadCount = effectiveThreadCount,
                            EffectiveWorkerCount = effectiveWorkerCount,
                            CompletedWorkUnitCount = state.CompletedBatchCount,
                            WorkloadElapsedMilliseconds =
                                state.WorkloadElapsedMilliseconds,
                            CaseValidity = validity,
                            MetricValidity = validity,
                            Observations =
                                BuildUnityPhysicsSpatialQueryObservations(ref state)
                        };
                    int resultStatus = WriteResult(runnerArgs, in result);
                    if (resultStatus != 0)
                    {
                        return resultStatus;
                    }
                    long[] rawDurations = state.RawBatchDurations.ToArray();
                    return WriteStepTiming(runnerArgs, rawDurations);
                }
                finally
                {
                    if (runnerArgs.RecordingMode == RecordingMode.On) UnityPhysicsRecording.Abort(ref recording);
                    DisposeUnityPhysicsSpatialQueryState(ref state);
                }
            }
            finally
            {
                JobsUtility.JobWorkerCount = previousWorkerCount;
            }
        }

        public static void DisposeUnityPhysicsSpatialQueryState(
            ref UnityPhysicsSpatialQueryState state)
        {
            if (state.JobHandles.IsCreated)
            {
                // every scheduled query lane completes before owned data is disposed
                JobHandle.CombineDependencies(state.JobHandles).Complete();
                state.JobHandles.Dispose();
            }
            state.World.Dispose();
            if (state.RayInputs.IsCreated)
            {
                state.RayInputs.Dispose();
            }
            if (state.SphereCastInputs.IsCreated)
            {
                state.SphereCastInputs.Dispose();
            }
            if (state.OverlapInputs.IsCreated)
            {
                state.OverlapInputs.Dispose();
            }
            if (state.DebugHits.IsCreated)
            {
                state.DebugHits.Dispose();
            }
            if (state.DebugHitDistances.IsCreated)
            {
                state.DebugHitDistances.Dispose();
            }
            if (state.HitCounts.IsCreated)
            {
                state.HitCounts.Dispose();
            }
            if (state.OverlapHitLists.IsCreated)
            {
                for (int laneIndex = 0; laneIndex < state.OverlapHitLists.Length; ++laneIndex)
                {
                    NativeList<int> laneHits = state.OverlapHitLists[laneIndex];
                    if (laneHits.IsCreated)
                    {
                        laneHits.Dispose();
                    }
                }
                state.OverlapHitLists.Dispose();
            }
            if (state.RawBatchDurations.IsCreated)
            {
                state.RawBatchDurations.Dispose();
            }
            state = default;
        }

        public static float UnityPhysicsSpatialCenteredGridCoordinate(
            float baseCenter, float spacing, uint coordinate, uint count)
        {
            return baseCenter + spacing * (coordinate - 0.5f * (count - 1));
        }

        public static float UnityPhysicsSpatialUncenteredGridCoordinate(
            float baseCenter, float spacing, uint coordinate)
        {
            return baseCenter + spacing * coordinate;
        }

        public static UnityPhysicsSpatialQuery GenerateUnityPhysicsSpatialQuery(
            in CaseExecutionSpec execution, int index)
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
            uint ix = slot % fixture.StaticGrid.x;
            uint iz = (slot / fixture.StaticGrid.x) % fixture.StaticGrid.z;
            uint iy = slot / (fixture.StaticGrid.x * fixture.StaticGrid.z);
            float3 center = new float3(
                UnityPhysicsSpatialCenteredGridCoordinate(fixture.StaticBaseCenter.x,
                    fixture.StaticSpacing.x, ix, fixture.StaticGrid.x),
                UnityPhysicsSpatialUncenteredGridCoordinate(fixture.StaticBaseCenter.y,
                    fixture.StaticSpacing.y, iy),
                UnityPhysicsSpatialCenteredGridCoordinate(fixture.StaticBaseCenter.z,
                    fixture.StaticSpacing.z, iz, fixture.StaticGrid.z));
            float3 sceneMinimum = new float3(
                UnityPhysicsSpatialCenteredGridCoordinate(fixture.StaticBaseCenter.x,
                    fixture.StaticSpacing.x, 0, fixture.StaticGrid.x) -
                    fixture.StaticHalfExtents.x,
                fixture.StaticBaseCenter.y - fixture.StaticHalfExtents.y,
                UnityPhysicsSpatialCenteredGridCoordinate(fixture.StaticBaseCenter.z,
                    fixture.StaticSpacing.z, 0, fixture.StaticGrid.z) -
                    fixture.StaticHalfExtents.z);
            float3 sceneMaximum = new float3(
                UnityPhysicsSpatialCenteredGridCoordinate(fixture.StaticBaseCenter.x,
                    fixture.StaticSpacing.x, fixture.StaticGrid.x - 1,
                    fixture.StaticGrid.x) + fixture.StaticHalfExtents.x,
                UnityPhysicsSpatialUncenteredGridCoordinate(fixture.StaticBaseCenter.y,
                    fixture.StaticSpacing.y, fixture.StaticGrid.y - 1) +
                    fixture.StaticHalfExtents.y,
                UnityPhysicsSpatialCenteredGridCoordinate(fixture.StaticBaseCenter.z,
                    fixture.StaticSpacing.z, fixture.StaticGrid.z - 1,
                    fixture.StaticGrid.z) + fixture.StaticHalfExtents.z);
            int face = sample % 6;
            int axis = face / 2;
            if (index >= fixture.RayCount + fixture.SphereCastCount)
            {
                float3 overlapCenter = center;
                if (intendedHit == 0)
                {
                    overlapCenter[axis] =
                        sceneMaximum[axis] + fixture.MissOffset;
                }
                return new UnityPhysicsSpatialQuery
                {
                    OriginOrCenter = overlapCenter
                };
            }
            int faceSign = (face & 1) * 2 - 1;
            int firstTransverse = axis == 0 ? 1 : 0;
            float3 origin = float3.zero;
            float3 direction = float3.zero;
            for (int component = 0; component < 3; ++component)
            {
                if (component == axis)
                {
                    origin[component] = faceSign > 0 ?
                        sceneMaximum[component] + 5.0f :
                        sceneMinimum[component] - 5.0f;
                    direction[component] = -faceSign;
                }
                else
                {
                    origin[component] =
                        intendedHit == 0 && component == firstTransverse ?
                            sceneMaximum[component] + fixture.MissOffset :
                            center[component];
                }
            }
            return new UnityPhysicsSpatialQuery
            {
                OriginOrCenter = origin,
                Direction = direction
            };
        }

        public static double UnityPhysicsQueryRate(
            int queryCount, int batchCount, long elapsedTicks)
        {
            return elapsedTicks > 0 ?
                queryCount * (double)batchCount * Stopwatch.Frequency /
                    elapsedTicks :
                0.0;
        }

        public static ulong UnityPhysicsDoubleBits(double value)
        {
            return unchecked((ulong)BitConverter.DoubleToInt64Bits(value));
        }
    }
}
