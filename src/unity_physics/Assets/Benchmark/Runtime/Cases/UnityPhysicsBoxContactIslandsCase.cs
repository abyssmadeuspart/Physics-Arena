using System;
using System.Diagnostics;
using Unity.Collections;
using Unity.Entities;
using Unity.Jobs;
using Unity.Jobs.LowLevel.Unsafe;
using Unity.Mathematics;
using Unity.Physics;
using PhysicsBoxCollider = Unity.Physics.BoxCollider;
using PhysicsCollider = Unity.Physics.Collider;

namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
    public struct UnityPhysicsContactIslandsState
    {
        public CaseExecutionSpec CaseExecution;
        public PhysicsWorld World;
        public Simulation Simulation;
        public BlobAssetReference<PhysicsCollider> DynamicCollider;
        public NativeArray<BlobAssetReference<PhysicsCollider>> StaticColliders;
        public NativeReference<int> StaticBodiesChanged;
    }

    public static partial class UnityPhysicsBenchmarkRunner
    {
        public static UnityPhysicsCaseDescriptor UnityPhysicsBoxContactIslandsDescriptor()
        {
            return new UnityPhysicsCaseDescriptor
            {
                EngineId = "unity_physics"
            };
        }

        public static UnityPhysicsCaseRegistration UnityPhysicsBoxContactIslandsRegistration()
        {
            return new UnityPhysicsCaseRegistration
            {
                Descriptor = UnityPhysicsBoxContactIslandsDescriptor(),
                RunHeadless = RunUnityPhysicsContactIslands,
                BuildVisualScene = BuildUnityPhysicsContactIslandsVisualScene
            };
        }

        public static float UnityPhysicsContactIslandsOrigin(float spacing, int coordinate, int count)
        {
            return spacing * (coordinate - 0.5f * (count - 1));
        }

        public static int CreateUnityPhysicsContactIslandsState(
            in CaseExecutionSpec caseExecution, out UnityPhysicsContactIslandsState state)
        {
            state = default;
            state.CaseExecution = caseExecution;
            state.World = new PhysicsWorld(
                (int)caseExecution.StaticBodyCount,
                (int)caseExecution.DynamicBodyCount,
                0);
            CaseExecutionContactIslands fixture = caseExecution.ContactIslands;
            state.DynamicCollider = CreateUnityPhysicsResolvedCollider(in caseExecution,
                in caseExecution.SelectedGeometry, FixtureFilter(),
                FixtureMaterial(caseExecution.Friction, caseExecution.Restitution));
            // UnityPhysicsContactIslandsState owns this array and disposes it in DisposeUnityPhysicsContactIslandsState
            state.StaticColliders = new NativeArray<BlobAssetReference<PhysicsCollider>>(
                (int)caseExecution.StaticBodyCount,
                Allocator.Persistent);
            int fixtureStatus = CreateUnityPhysicsContactIslandsFixture(ref state);
            if (fixtureStatus != 0)
            {
                return fixtureStatus;
            }
            state.World.UpdateIndexMaps();
            state.StaticBodiesChanged = new NativeReference<int>(Allocator.Persistent);
            state.StaticBodiesChanged.Value = 1;
            state.Simulation = Simulation.Create();
            return state.World.NumDynamicBodies == caseExecution.DynamicBodyCount &&
                state.World.NumStaticBodies == caseExecution.StaticBodyCount ? 0 : 2;
        }

        public static BlobAssetReference<PhysicsCollider> CreateContactIslandsBoxCollider(
            float3 size, float friction, float restitution)
        {
            Material material = Material.Default;
            material.Friction = friction;
            material.Restitution = restitution;
            material.CollisionResponse = CollisionResponsePolicy.Collide;
            return PhysicsBoxCollider.Create(new BoxGeometry
            {
                Center = float3.zero,
                Orientation = quaternion.identity,
                Size = size,
                BevelRadius = 0.0f
            }, new CollisionFilter
            {
                BelongsTo = 0xffffffffu,
                CollidesWith = 0xffffffffu,
                GroupIndex = 0
            }, material);
        }

        public static int CreateUnityPhysicsContactIslandsFixture(ref UnityPhysicsContactIslandsState state)
        {
            CaseExecutionSpec execution = state.CaseExecution;
            CaseExecutionContactIslands fixture = execution.ContactIslands;
            NativeArray<RigidBody> staticBodies = state.World.StaticBodies;
            for (int groupZ = 0; groupZ < fixture.IslandGrid.y; ++groupZ)
            {
                for (int groupX = 0; groupX < fixture.IslandGrid.x; ++groupX)
                {
                    int staticIndex = groupZ * (int)fixture.IslandGrid.x + groupX;
                    state.StaticColliders[staticIndex] = CreateContactIslandsBoxCollider(
                        fixture.FloorHalfExtents * 2.0f,
                        execution.Friction, execution.Restitution);
                    staticBodies[staticIndex] = new RigidBody
                    {
                        WorldFromBody = new RigidTransform(
                            quaternion.identity,
                            new float3(
                                UnityPhysicsContactIslandsOrigin(
                                    fixture.IslandSpacing.x, groupX, (int)fixture.IslandGrid.x),
                                -fixture.FloorHalfExtents.y,
                                UnityPhysicsContactIslandsOrigin(
                                    fixture.IslandSpacing.y, groupZ, (int)fixture.IslandGrid.y))),
                        Collider = state.StaticColliders[staticIndex],
                        Entity = Entity.Null,
                        CustomTags = 0,
                        Scale = 1.0f
                    };
                }
            }

            MassProperties massProperties = state.DynamicCollider.Value.MassProperties;
            NativeArray<RigidBody> dynamicBodies = state.World.DynamicBodies;
            NativeArray<MotionData> motionDatas = state.World.MotionDatas;
            NativeArray<MotionVelocity> motionVelocities = state.World.MotionVelocities;
            float mass = fixture.Density * massProperties.Volume;
            int bodySlot = 0;
            for (int groupZ = 0; groupZ < fixture.IslandGrid.y; ++groupZ)
            {
                for (int groupX = 0; groupX < fixture.IslandGrid.x; ++groupX)
                {
                    float originX = UnityPhysicsContactIslandsOrigin(
                        fixture.IslandSpacing.x, groupX, (int)fixture.IslandGrid.x);
                    float originZ = UnityPhysicsContactIslandsOrigin(
                        fixture.IslandSpacing.y, groupZ, (int)fixture.IslandGrid.y);
                    for (int y = 0; y < fixture.BodyGrid.y; ++y)
                    {
                        for (int z = 0; z < fixture.BodyGrid.z; ++z)
                        {
                            for (int x = 0; x < fixture.BodyGrid.x; ++x)
                            {
                                float3 position = new float3(
                                    originX + (x - 0.5f * (fixture.BodyGrid.x - 1)) * fixture.BodySpacing.x,
                                    fixture.BodyInitialY + y * fixture.BodySpacing.y,
                                    originZ + (z - 0.5f * (fixture.BodyGrid.z - 1)) * fixture.BodySpacing.z);
                                quaternion rotation = UnityPhysicsShapeRotation(state.CaseExecution.SelectedGeometry.Axis);
                                dynamicBodies[bodySlot] = new RigidBody
                                {
                                    WorldFromBody = new RigidTransform(rotation, position),
                                    Collider = state.DynamicCollider,
                                    Entity = Entity.Null,
                                    CustomTags = 0,
                                    Scale = 1.0f
                                };
                                motionDatas[bodySlot] = new MotionData
                                {
                                    WorldFromMotion = new RigidTransform(
                                        math.mul(rotation, massProperties.MassDistribution.Transform.rot),
                                        math.rotate(rotation, massProperties.MassDistribution.Transform.pos) + position),
                                    BodyFromMotion = massProperties.MassDistribution.Transform,
                                    LinearDamping = 0.0f,
                                    AngularDamping = 0.0f
                                };
                                motionVelocities[bodySlot] = new MotionVelocity
                                {
                                    LinearVelocity = float3.zero,
                                    AngularVelocity = float3.zero,
                                    InverseInertia = math.rcp(massProperties.MassDistribution.InertiaTensor * mass),
                                    InverseMass = 1.0f / mass,
                                    AngularExpansionFactor = massProperties.AngularExpansionFactor,
                                    GravityFactor = 1.0f
                                };
                                bodySlot += 1;
                            }
                        }
                    }
                }
            }
            if (bodySlot != execution.DynamicBodyCount)
            {
                return 2;
            }
            return 0;
        }

        public static void StepUnityPhysicsContactIslands(
            ref UnityPhysicsContactIslandsState state,
            int multiThreaded, CaseExecutionToggle solverStabilization)
        {
            CaseExecutionSpec execution = state.CaseExecution;
            PhysicsStep physicsStep = PhysicsStep.Default;
            physicsStep.Gravity = execution.Gravity;
            physicsStep.SynchronizeCollisionWorld = 1;
            physicsStep.SolverStabilizationHeuristicSettings.EnableSolverStabilization = solverStabilization == CaseExecutionToggle.Enabled;
            SimulationStepInput input = new SimulationStepInput
            {
                World = state.World,
                TimeStep = 1.0f / execution.TimestepHz,
                Gravity = physicsStep.Gravity,
                EnableGyroscopicTorque = physicsStep.EnableGyroscopicTorque,
                NumSubsteps = (int)execution.Substeps,
                NumSolverIterations = (int)execution.SolverIterations,
                DirectSolverSettings = physicsStep.DirectSolverSettings,
                MaxDynamicDepenetrationVelocity = physicsStep.MaxDynamicDepenetrationVelocity,
                MaxStaticDepenetrationVelocity = physicsStep.MaxStaticDepenetrationVelocity,
                SynchronizeCollisionWorld = true,
                SolverStabilizationHeuristicSettings = physicsStep.SolverStabilizationHeuristicSettings,
                HaveStaticBodiesChanged = state.StaticBodiesChanged.AsReadOnly()
            };
            JobHandle buildBroadphaseHandle = state.World.CollisionWorld.ScheduleBuildBroadphaseJobs(
                ref state.World,
                1.0f / execution.TimestepHz,
                physicsStep.Gravity,
                state.StaticBodiesChanged.AsReadOnly(),
                default,
                multiThreaded != 0);
            JobHandle inputDeps = JobHandle.CombineDependencies(state.Simulation.FinalJobHandle, buildBroadphaseHandle);
            SimulationJobHandles broadphaseHandles = state.Simulation.ScheduleBroadphaseJobs(input, inputDeps, multiThreaded != 0);
            SimulationJobHandles narrowphaseHandles = state.Simulation.ScheduleNarrowphaseJobs(input, broadphaseHandles.FinalExecutionHandle, multiThreaded != 0);
            SimulationJobHandles jacobianHandles = state.Simulation.ScheduleCreateJacobiansJobs(input, narrowphaseHandles.FinalExecutionHandle, multiThreaded != 0);
            SimulationJobHandles handles = state.Simulation.ScheduleSolveAndIntegrateJobs(input, jacobianHandles.FinalExecutionHandle, multiThreaded != 0);
            // complete the blocking physics work unit before the timer boundary ends
            JobHandle.CombineDependencies(handles.FinalExecutionHandle, handles.FinalDisposeHandle).Complete();
            state.StaticBodiesChanged.Value = 0;
        }

        public static int SampleUnityPhysicsContactIslandsTransforms(
            ref UnityPhysicsContactIslandsState state,
            UnityPhysicsVisualStableTransform[] transforms)
        {
            if (transforms == null || transforms.Length < state.CaseExecution.DynamicBodyCount)
            {
                return 2;
            }
            for (int slot = 0; slot < state.CaseExecution.DynamicBodyCount; ++slot)
            {
                MotionData motionData = state.World.MotionDatas[slot];
                RigidTransform worldFromBody = math.mul(motionData.WorldFromMotion, math.inverse(motionData.BodyFromMotion));
                transforms[slot] = new UnityPhysicsVisualStableTransform
                {
                    StableSlot = (uint)slot,
                    Transform = new UnityPhysicsTransform
                    {
                        PositionX = worldFromBody.pos.x,
                        PositionY = worldFromBody.pos.y,
                        PositionZ = worldFromBody.pos.z,
                        RotationX = worldFromBody.rot.value.x,
                        RotationY = worldFromBody.rot.value.y,
                        RotationZ = worldFromBody.rot.value.z,
                        RotationW = worldFromBody.rot.value.w
                    }
                };
            }
            return 0;
        }

        public static int BuildUnityPhysicsContactIslandsVisualScene(
            ref UnityPhysicsCaseView state,
            UnityPhysicsVisualGeometry[] geometries,

            ref UnityPhysicsVisualMeshStorage meshes,
            UnityPhysicsVisualInstance[] instances,
            out int geometryCount,
            out int instanceCount)
        {
            geometryCount = 0;
            instanceCount = 0;
            CaseExecutionSpec execution = state.Execution;
            CaseExecutionContactIslands fixture = execution.ContactIslands;
            if (geometries == null || geometries.Length < 2 ||
                instances == null || instances.Length < execution.BodyCount)
            {
                return 2;
            }
            if (BuildResolvedVisualGeometry(in execution, in execution.SelectedGeometry,
            ref meshes, out geometries[0]) != 0) return 2;
            geometries[1] = new UnityPhysicsVisualGeometry
            {
                Kind = 2, ParameterX = fixture.FloorHalfExtents.x,
                ParameterY = fixture.FloorHalfExtents.y, ParameterZ = fixture.FloorHalfExtents.z
            };
            UnityPhysicsVisualStableTransform[] transforms =
                new UnityPhysicsVisualStableTransform[execution.DynamicBodyCount];
            if (SampleUnityPhysicsTransforms(ref state.World, transforms) != 0)
            {
                return 2;
            }
            for (int slot = 0; slot < execution.DynamicBodyCount; ++slot)
            {
                instances[slot] = new UnityPhysicsVisualInstance
                {
                    GeometryIndex = 0,
                    StableSlot = (uint)slot,
                    TransformSlot = (uint)slot,
                    InitialTransform = transforms[slot].Transform
                };
            }
            int index = 0;
            for (int groupZ = 0; groupZ < fixture.IslandGrid.y; ++groupZ)
            {
                for (int groupX = 0; groupX < fixture.IslandGrid.x; ++groupX)
                {
                    int instanceIndex = (int)execution.DynamicBodyCount + index;
                    instances[instanceIndex] = new UnityPhysicsVisualInstance
                    {
                        GeometryIndex = 1,
                        StableSlot = (uint)instanceIndex,
                        TransformSlot = uint.MaxValue,
                        InitialTransform = new UnityPhysicsTransform
                        {
                            PositionX = UnityPhysicsContactIslandsOrigin(
                                fixture.IslandSpacing.x, groupX, (int)fixture.IslandGrid.x),
                            PositionY = -fixture.FloorHalfExtents.y,
                            PositionZ = UnityPhysicsContactIslandsOrigin(
                                fixture.IslandSpacing.y, groupZ, (int)fixture.IslandGrid.y),
                            RotationW = 1.0f
                        }
                    };
                    index += 1;
                }
            }
            geometryCount = 2;
            instanceCount = (int)execution.BodyCount;
            return 0;
        }

        public static uint CountUnityPhysicsContactIslandsInvalidTransforms(
            ref UnityPhysicsContactIslandsState state)
        {
            uint invalidTransformCount = 0;
            for (int slot = 0; slot < state.CaseExecution.DynamicBodyCount; ++slot)
            {
                MotionData motionData = state.World.MotionDatas[slot];
                RigidTransform worldFromBody = math.mul(motionData.WorldFromMotion, math.inverse(motionData.BodyFromMotion));
                float3 position = worldFromBody.pos;
                if (!math.all(math.isfinite(position)) || !math.all(math.isfinite(worldFromBody.rot.value)))
                {
                    invalidTransformCount += 1;
                }
            }
            return invalidTransformCount;
        }

        public static void DisposeUnityPhysicsContactIslandsState(ref UnityPhysicsContactIslandsState state)
        {
            if (state.StaticBodiesChanged.IsCreated)
            {
                // UnityPhysicsContactIslandsState owns this native reference
                state.StaticBodiesChanged.Dispose();
            }
            // UnityPhysicsContactIslandsState owns the simulation
            state.Simulation.Dispose();
            // UnityPhysicsContactIslandsState owns the physics world
            state.World.Dispose();
            if (state.DynamicCollider.IsCreated)
            {
                // UnityPhysicsContactIslandsState created and owns this collider blob
                state.DynamicCollider.Dispose();
            }
            if (state.StaticColliders.IsCreated)
            {
                for (int index = 0; index < state.StaticColliders.Length; ++index)
                {
                    if (state.StaticColliders[index].IsCreated)
                    {
                        // UnityPhysicsContactIslandsState created and owns each static collider blob
                        state.StaticColliders[index].Dispose();
                    }
                }
                // UnityPhysicsContactIslandsState owns the collider reference array
                state.StaticColliders.Dispose();
            }
            state = default;
        }

        public static int WarmupUnityPhysicsContactIslandsVisual(
            ref UnityPhysicsContactIslandsState state, int workUnitCount, int multiThreaded, CaseExecutionToggle solverStabilization)
        {
            for (int workUnit = 0; workUnit < workUnitCount; ++workUnit)
            {
                StepUnityPhysicsContactIslands(ref state, multiThreaded, solverStabilization);
            }
            return 0;
        }

        public static string UnityPhysicsContactIslandsVisualSettings(
            in CaseExecutionSpec execution, int workerCount, CaseExecutionToggle solverStabilization)
        {
            uint islandCount = execution.ContactIslands.IslandGrid.x *
                execution.ContactIslands.IslandGrid.y;
            return FormattableString.Invariant(
                $"solver_iterations={execution.SolverIterations}; substeps={execution.Substeps}; solver_type=iterative; stabilization={(solverStabilization == CaseExecutionToggle.Enabled ? "on" : "off")}; synchronize_collision_world=on; sleep=not_applicable; worker_count={workerCount}; islands={islandCount}");
        }

        public static int RunUnityPhysicsContactIslands(RunnerArgs runnerArgs)
        {
            if (runnerArgs.CaseExecution.SleepMode != CaseExecutionToggle.Disabled ||
                runnerArgs.CaseExecution.ContinuousCollisionMode != CaseExecutionToggle.Disabled)
            {
                return 2;
            }
            int previousWorkerCount = JobsUtility.JobWorkerCount;
            UnityPhysicsContactIslandsState state = default;
            UnityPhysicsRecordingWriter recording = default;
            UnityPhysicsStackCapture stack = default;
            try
            {
                JobsUtility.JobWorkerCount = math.max(0, runnerArgs.ThreadCount - 1);
                int effectiveWorkerCount = JobsUtility.JobWorkerCount;
                int effectiveThreadCount = effectiveWorkerCount + 1;
                if (effectiveThreadCount != runnerArgs.ThreadCount ||
                    CreateUnityPhysicsContactIslandsState(in runnerArgs.CaseExecution, out state) != 0)
                {
                    return 2;
                }
                long[] rawWorkUnitDurations = new long[runnerArgs.StepCount];
                UnityPhysicsCaseView capture = new UnityPhysicsCaseView
                {
                    Execution = runnerArgs.CaseExecution,
                    World = state.World
                };
                if (runnerArgs.VerificationMode == VerificationMode.On && UnityPhysicsStackStateCapture.Open(in runnerArgs, out stack) != 0) return 2;
                if (runnerArgs.VerificationMode == VerificationMode.On && UnityPhysicsStackStateCapture.Append(ref stack, in runnerArgs.CaseRegistration, ref capture,
                    UnityPhysicsStackCapturePhase.Construction, 0, 0) != 0) return 2;
                for (int warmup = 0; warmup < runnerArgs.WarmupSteps; ++warmup)
                {
                    StepUnityPhysicsContactIslands(ref state, runnerArgs.ThreadCount > 1 ? 1 : 0, runnerArgs.SolverStabilization);
                    if (runnerArgs.VerificationMode == VerificationMode.On && UnityPhysicsStackStateCapture.Append(ref stack, in runnerArgs.CaseRegistration, ref capture,
                        UnityPhysicsStackCapturePhase.Warmup, 0, (uint)warmup + 1) != 0) return 2;
                }
                if (runnerArgs.RecordingMode == RecordingMode.On && UnityPhysicsRecording.Begin(in runnerArgs, ref capture, out recording) != 0) return 2;
                for (int workUnit = 0; workUnit < runnerArgs.StepCount; ++workUnit)
                {
                    long start = Stopwatch.GetTimestamp();
                    StepUnityPhysicsContactIslands(ref state, runnerArgs.ThreadCount > 1 ? 1 : 0, runnerArgs.SolverStabilization);
                    rawWorkUnitDurations[workUnit] = Stopwatch.GetTimestamp() - start;
                    if (runnerArgs.VerificationMode == VerificationMode.On && UnityPhysicsStackStateCapture.Append(ref stack, in runnerArgs.CaseRegistration, ref capture,
                        UnityPhysicsStackCapturePhase.Measured, 0, (uint)workUnit + 1) != 0) return 2;
                    if (runnerArgs.RecordingMode == RecordingMode.On && UnityPhysicsRecording.Append(ref recording, in runnerArgs.CaseRegistration,
                        ref capture, (ulong)workUnit + 1) != 0) return 2;
                }
                if (runnerArgs.VerificationMode == VerificationMode.On && UnityPhysicsStackStateCapture.Close(ref stack) != 0) return 2;
                if (runnerArgs.RecordingMode == RecordingMode.On && UnityPhysicsRecording.Complete(ref recording) != 0) return 2;
                double workloadElapsedMilliseconds = 0.0;
                for (int workUnit = 0; workUnit < rawWorkUnitDurations.Length; ++workUnit)
                {
                    workloadElapsedMilliseconds += rawWorkUnitDurations[workUnit] * 1000.0 / Stopwatch.Frequency;
                }
                uint invalidTransformCount = CountUnityPhysicsContactIslandsInvalidTransforms(ref state);
                UnityPhysicsResultValidity caseValidity =
                    state.World.NumDynamicBodies == runnerArgs.CaseExecution.DynamicBodyCount &&
                    state.World.NumStaticBodies == runnerArgs.CaseExecution.StaticBodyCount &&
                    invalidTransformCount == 0 ?
                    UnityPhysicsResultValidity.Valid : UnityPhysicsResultValidity.Invalid;
                UnityPhysicsResultValidity metricValidity =
                    runnerArgs.StepCount == runnerArgs.CaseExecution.MeasuredWorkUnitCount &&
                    workloadElapsedMilliseconds > 0.0 && !double.IsNaN(workloadElapsedMilliseconds) &&
                    !double.IsInfinity(workloadElapsedMilliseconds) ?
                    UnityPhysicsResultValidity.Valid : UnityPhysicsResultValidity.Invalid;
                UnityPhysicsBenchmarkResult result = new UnityPhysicsBenchmarkResult
                {
                    PhysicsSettings = UnityPhysicsContactIslandsVisualSettings(
                        in runnerArgs.CaseExecution, effectiveWorkerCount, runnerArgs.SolverStabilization),
                    BodyCount = (int)runnerArgs.CaseExecution.BodyCount,
                    ShapeCount = (int)runnerArgs.CaseExecution.ShapeCount,
                    QueryCount = (int)runnerArgs.CaseExecution.QueryCount,
                    ConstraintCount = (int)runnerArgs.CaseExecution.ConstraintCount,
                    InvalidTransformCount = invalidTransformCount,
                    EffectiveThreadCount = effectiveThreadCount,
                    EffectiveWorkerCount = effectiveWorkerCount,
                    CompletedWorkUnitCount = runnerArgs.StepCount,
                    WorkloadElapsedMilliseconds = workloadElapsedMilliseconds,
                    CaseValidity = caseValidity,
                    MetricValidity = metricValidity
                };
                int resultStatus = WriteResult(runnerArgs, in result);
                return resultStatus == 0 ? WriteStepTiming(runnerArgs, rawWorkUnitDurations) : resultStatus;
            }
            finally
            {
                UnityPhysicsStackStateCapture.Abort(ref stack);
                if (runnerArgs.RecordingMode == RecordingMode.On) UnityPhysicsRecording.Abort(ref recording);
                JobsUtility.JobWorkerCount = previousWorkerCount;
                DisposeUnityPhysicsContactIslandsState(ref state);
            }
        }
    }
}
