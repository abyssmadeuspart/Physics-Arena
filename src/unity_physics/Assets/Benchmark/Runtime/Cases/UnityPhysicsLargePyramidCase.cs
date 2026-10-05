using System;
using System.Diagnostics;
using Unity.Collections;
using Unity.Entities;
using Unity.Jobs.LowLevel.Unsafe;
using Unity.Mathematics;
using Unity.Physics;
using PhysicsBoxCollider = Unity.Physics.BoxCollider;
using PhysicsCollider = Unity.Physics.Collider;
using PhysicsSphereCollider = Unity.Physics.SphereCollider;

namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
    public struct UnityPhysicsLargePyramidState
    {
        public CaseExecutionLargePyramid Fixture;
        public CaseExecutionGeometry Geometry;
        public FixedList512Bytes<float3> HullPoints;
        public float3 Gravity;
        public float Friction;
        public float Restitution;
        public uint TimestepHz;
        public uint SolverIterations;
        public uint Substeps;
        public int DynamicBodyCount;
        public int BodyCount;
        public int CompletedStepCount;
        public PhysicsWorld World;
        public BlobAssetReference<PhysicsCollider> BoxCollider;
        public BlobAssetReference<PhysicsCollider> ProjectileCollider;
        public BlobAssetReference<PhysicsCollider> FloorCollider;
        public NativeReference<int> StaticBodiesChanged;
        public Simulation Simulation;
    }

    public static partial class UnityPhysicsBenchmarkRunner
    {
        public static UnityPhysicsCaseRegistration UnityPhysicsLargePyramidRegistration()
        {
            return new UnityPhysicsCaseRegistration
            {
                Descriptor = new UnityPhysicsCaseDescriptor { EngineId = EngineId },
                RunHeadless = RunUnityPhysicsLargePyramid,
                BuildVisualScene = BuildUnityPhysicsLargePyramidVisualScene
            };
        }

        public static BlobAssetReference<PhysicsCollider> CreateLargePyramidBoxCollider(
            float3 halfExtents, float friction, float restitution)
        {
            return PhysicsBoxCollider.Create(new BoxGeometry
            {
                Center = float3.zero,
                Orientation = quaternion.identity,
                Size = halfExtents * 2f,
                BevelRadius = 0f
            }, FixtureFilter(), FixtureMaterial(friction, restitution));
        }

        public static BlobAssetReference<PhysicsCollider> CreateLargePyramidSphereCollider(
            float radius, float friction, float restitution)
        {
            return PhysicsSphereCollider.Create(new SphereGeometry
            {
                Center = float3.zero,
                Radius = radius
            }, FixtureFilter(), FixtureMaterial(friction, restitution));
        }

        public static void SetLargePyramidDynamicBody(
            ref PhysicsWorld world,
            int bodyIndex,
            BlobAssetReference<PhysicsCollider> collider,
            float mass,
            float3 position, quaternion rotation)
        {
            MassProperties massProperties = collider.Value.MassProperties;
            NativeArray<RigidBody> dynamicBodies = world.DynamicBodies;
            NativeArray<MotionData> motionDatas = world.MotionDatas;
            NativeArray<MotionVelocity> motionVelocities = world.MotionVelocities;
            dynamicBodies[bodyIndex] = new RigidBody
            {
                WorldFromBody = new RigidTransform(rotation, position),
                Collider = collider,
                Entity = Entity.Null,
                CustomTags = 0,
                Scale = 1f
            };
            motionDatas[bodyIndex] = new MotionData
            {
                WorldFromMotion = new RigidTransform(
                    math.mul(rotation, massProperties.MassDistribution.Transform.rot),
                    math.rotate(rotation, massProperties.MassDistribution.Transform.pos) + position),
                BodyFromMotion = massProperties.MassDistribution.Transform,
                LinearDamping = 0f,
                AngularDamping = 0f
            };
            motionVelocities[bodyIndex] = new MotionVelocity
            {
                LinearVelocity = float3.zero,
                AngularVelocity = float3.zero,
                InverseInertia = math.rcp(massProperties.MassDistribution.InertiaTensor * mass),
                InverseMass = math.rcp(mass),
                AngularExpansionFactor = massProperties.AngularExpansionFactor,
                GravityFactor = 1f
            };
        }

        public static int CreateUnityPhysicsLargePyramidState(
            in CaseExecutionSpec execution, ref UnityPhysicsLargePyramidState state)
        {
            state.Fixture = execution.LargePyramid;
            state.Geometry = execution.SelectedGeometry;
            state.HullPoints = execution.HullPoints;
            state.Gravity = execution.Gravity;
            state.Friction = execution.Friction;
            state.Restitution = execution.Restitution;
            state.TimestepHz = execution.TimestepHz;
            state.SolverIterations = execution.SolverIterations;
            state.Substeps = execution.Substeps;
            state.DynamicBodyCount = (int)execution.DynamicBodyCount;
            state.BodyCount = (int)execution.BodyCount;
            state.CompletedStepCount = 0;
            state.World = new PhysicsWorld((int)execution.StaticBodyCount,
                state.DynamicBodyCount, 0);
            state.BoxCollider = CreateUnityPhysicsResolvedCollider(in execution,
                in execution.SelectedGeometry, FixtureFilter(), FixtureMaterial(execution.Friction, execution.Restitution));
            state.ProjectileCollider = CreateLargePyramidSphereCollider(
                state.Fixture.ProjectileRadius, execution.Friction, execution.Restitution);
            state.FloorCollider = CreateLargePyramidBoxCollider(
                state.Fixture.FloorHalfExtents, execution.Friction, execution.Restitution);
            float boxMass = state.Fixture.BoxDensity * state.BoxCollider.Value.MassProperties.Volume;
            int bodyIndex = 0;
            for (int layer = 0; layer < state.Fixture.RowCount; ++layer)
            {
                int layerSide = (int)state.Fixture.RowCount - layer;
                for (int depth = 0; depth < layerSide; ++depth)
                {
                    for (int column = 0; column < layerSide; ++column)
                    {
                        float3 position = new float3(
                            state.Fixture.BaseCenter.x +
                                (column - 0.5f * (layerSide - 1)) * state.Fixture.BoxSpacing.x,
                            state.Fixture.BaseCenter.y + layer * state.Fixture.BoxSpacing.y,
                            state.Fixture.BaseCenter.z +
                                (depth - 0.5f * (layerSide - 1)) * state.Fixture.BoxSpacing.z);
                        SetLargePyramidDynamicBody(
                            ref state.World, bodyIndex++, state.BoxCollider, boxMass, position,
                            UnityPhysicsShapeRotation(execution.SelectedGeometry.Axis));
                    }
                }
            }
            float radius = state.Fixture.ProjectileRadius;
            float projectileMass = state.Fixture.ProjectileDensity * 4f / 3f * math.PI *
                radius * radius * radius;
            for (int projectileIndex = 0;
                projectileIndex < (int)state.Fixture.ProjectileCount;
                ++projectileIndex)
            {
                float3 position = state.Fixture.ProjectileInitialCenter +
                    projectileIndex * state.Fixture.ProjectileCenterSpacing;
                SetLargePyramidDynamicBody(ref state.World, bodyIndex++,
                    state.ProjectileCollider, projectileMass, position, quaternion.identity);
            }
            NativeArray<RigidBody> staticBodies = state.World.StaticBodies;
            staticBodies[0] = new RigidBody
            {
                WorldFromBody = new RigidTransform(quaternion.identity,
                    new float3(0f, -state.Fixture.FloorHalfExtents.y, 0f)),
                Collider = state.FloorCollider,
                Entity = Entity.Null,
                CustomTags = 0,
                Scale = 1f
            };
            if (bodyIndex != state.DynamicBodyCount) return 2;
            state.World.UpdateIndexMaps();
            state.StaticBodiesChanged = new NativeReference<int>(Allocator.Persistent);
            state.StaticBodiesChanged.Value = 1;
            state.Simulation = Simulation.Create();
            return 0;
        }

        public static void LaunchUnityPhysicsLargePyramidProjectiles(
            ref UnityPhysicsLargePyramidState state)
        {
            int firstProjectileIndex = state.DynamicBodyCount - (int)state.Fixture.ProjectileCount;
            NativeArray<MotionVelocity> motionVelocities = state.World.MotionVelocities;
            for (int projectileIndex = firstProjectileIndex;
                projectileIndex < state.DynamicBodyCount;
                ++projectileIndex)
            {
                MotionVelocity velocity = motionVelocities[projectileIndex];
                velocity.LinearVelocity = state.Fixture.ProjectileLaunchVelocity;
                motionVelocities[projectileIndex] = velocity;
            }
        }

        public static UnityPhysicsTransform LargePyramidTransform(
            ref PhysicsWorld world, int index)
        {
            MotionData motionData = world.MotionDatas[index];
            RigidTransform worldFromBody = math.mul(
                motionData.WorldFromMotion, math.inverse(motionData.BodyFromMotion));
            return new UnityPhysicsTransform
            {
                PositionX = worldFromBody.pos.x,
                PositionY = worldFromBody.pos.y,
                PositionZ = worldFromBody.pos.z,
                RotationX = worldFromBody.rot.value.x,
                RotationY = worldFromBody.rot.value.y,
                RotationZ = worldFromBody.rot.value.z,
                RotationW = worldFromBody.rot.value.w
            };
        }

        public static int BuildUnityPhysicsLargePyramidVisualScene(
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
            if (geometries == null || geometries.Length < 3 || instances == null ||
                instances.Length < (int)execution.BodyCount) return 2;
            if (BuildResolvedVisualGeometry(in execution, in execution.SelectedGeometry,
            ref meshes, out geometries[0]) != 0) return 2;
            geometries[1] = new UnityPhysicsVisualGeometry { Kind = 1,
                ParameterX = execution.LargePyramid.ProjectileRadius };
            geometries[2] = new UnityPhysicsVisualGeometry { Kind = 2,
                ParameterX = execution.LargePyramid.FloorHalfExtents.x,
                ParameterY = execution.LargePyramid.FloorHalfExtents.y,
                ParameterZ = execution.LargePyramid.FloorHalfExtents.z };
            int firstProjectileIndex = (int)execution.DynamicBodyCount -
                (int)execution.LargePyramid.ProjectileCount;
            for (int index = 0; index < (int)execution.DynamicBodyCount; ++index)
            {
                instances[index] = new UnityPhysicsVisualInstance
                {
                    GeometryIndex = index >= firstProjectileIndex ? 1u : 0u,
                    StableSlot = (uint)index,
                    TransformSlot = (uint)index,
                    InitialTransform = LargePyramidTransform(ref state.World, index)
                };
            }
            int floorSlot = (int)execution.DynamicBodyCount;
            instances[floorSlot] = new UnityPhysicsVisualInstance
            {
                GeometryIndex = 2,
                StableSlot = (uint)floorSlot,
                TransformSlot = uint.MaxValue,
                InitialTransform = new UnityPhysicsTransform
                {
                    PositionY = -execution.LargePyramid.FloorHalfExtents.y,
                    RotationW = 1f
                }
            };
            geometryCount = 3;
            instanceCount = (int)execution.BodyCount;
            return 0;
        }

        public static int CountUnityPhysicsLargePyramidInvalidTransforms(ref PhysicsWorld world)
        {
            int count = 0;
            for (int index = 0; index < world.NumDynamicBodies; ++index)
            {
                UnityPhysicsTransform transform = LargePyramidTransform(ref world, index);
                if (!math.all(math.isfinite(new float3(
                    transform.PositionX, transform.PositionY, transform.PositionZ))) ||
                    !math.all(math.isfinite(new float4(transform.RotationX, transform.RotationY,
                        transform.RotationZ, transform.RotationW)))) count += 1;
            }
            return count;
        }

        public static string UnityPhysicsLargePyramidVisualSettings(
            in CaseExecutionSpec execution, int workerCount, CaseExecutionToggle solverStabilization)
        {
            return FormattableString.Invariant(
                $"solver_iterations={execution.SolverIterations}; substeps={execution.Substeps}; solver_type=iterative; stabilization={(solverStabilization == CaseExecutionToggle.Enabled ? "on" : "off")}; linear_damping=0; angular_damping=0; synchronize_collision_world=on; sleep=not_applicable; worker_count={workerCount}");
        }

        public static void DisposeUnityPhysicsLargePyramidState(
            ref UnityPhysicsLargePyramidState state)
        {
            if (state.StaticBodiesChanged.IsCreated)
            {
                // large-pyramid case owns this native change flag
                state.StaticBodiesChanged.Dispose();
            }
            // large-pyramid case owns the direct simulation
            state.Simulation.Dispose();
            // large-pyramid case owns the direct physics world
            state.World.Dispose();
            if (state.BoxCollider.IsCreated)
            {
                // large-pyramid case created and owns the box collider blob
                state.BoxCollider.Dispose();
            }
            if (state.ProjectileCollider.IsCreated)
            {
                // large-pyramid case created and owns the projectile collider blob
                state.ProjectileCollider.Dispose();
            }
            if (state.FloorCollider.IsCreated)
            {
                // large-pyramid case created and owns the floor collider blob
                state.FloorCollider.Dispose();
            }
        }

        public static int RunUnityPhysicsLargePyramid(RunnerArgs runnerArgs)
        {
            int previousWorkerCount = JobsUtility.JobWorkerCount;
            UnityPhysicsLargePyramidState state = default;
            UnityPhysicsRecordingWriter recording = default;
            UnityPhysicsStackCapture stack = default;
            try
            {
                int requestedWorkerCount = math.max(0, runnerArgs.ThreadCount - 1);
                JobsUtility.JobWorkerCount = requestedWorkerCount;
                int effectiveWorkerCount = JobsUtility.JobWorkerCount;
                int effectiveThreadCount = effectiveWorkerCount + 1;
                if (CreateUnityPhysicsLargePyramidState(
                    in runnerArgs.CaseExecution, ref state) != 0) return 2;
                long[] rawStepDurations = new long[runnerArgs.StepCount];
                UnityPhysicsCaseView capture = new UnityPhysicsCaseView
                {
                    Execution = runnerArgs.CaseExecution,
                    World = state.World
                };
                if (runnerArgs.VerificationMode == VerificationMode.On && UnityPhysicsStackStateCapture.Open(in runnerArgs, out stack) != 0) return 2;
                if (runnerArgs.VerificationMode == VerificationMode.On && UnityPhysicsStackStateCapture.Append(ref stack, in runnerArgs.CaseRegistration, ref capture,
                    UnityPhysicsStackCapturePhase.Construction, 0, 0) != 0) return 2;
                for (int step = 0; step < runnerArgs.WarmupSteps; ++step)
                {
                    if (step == (int)state.Fixture.ProjectileLaunchAfterWorkUnits)
                        LaunchUnityPhysicsLargePyramidProjectiles(ref state);
                    StepWorld(ref state.World, ref state.Simulation, state.StaticBodiesChanged,
                        state.Gravity, 1f / state.TimestepHz,
                        state.SolverIterations, state.Substeps, runnerArgs.ThreadCount > 1 ? 1 : 0, runnerArgs.SolverStabilization);
                    state.StaticBodiesChanged.Value = 0;
                    if (runnerArgs.VerificationMode == VerificationMode.On && UnityPhysicsStackStateCapture.Append(ref stack, in runnerArgs.CaseRegistration, ref capture,
                        UnityPhysicsStackCapturePhase.Warmup, 0, (uint)step + 1) != 0) return 2;
                }
                if (runnerArgs.RecordingMode == RecordingMode.On && UnityPhysicsRecording.Begin(in runnerArgs, ref capture, out recording) != 0) return 2;
                for (int step = 0; step < runnerArgs.StepCount; ++step)
                {
                    if (runnerArgs.WarmupSteps + state.CompletedStepCount ==
                        (int)state.Fixture.ProjectileLaunchAfterWorkUnits)
                    {
                        LaunchUnityPhysicsLargePyramidProjectiles(ref state);
                    }
                    long start = Stopwatch.GetTimestamp();
                    StepWorld(ref state.World, ref state.Simulation, state.StaticBodiesChanged,
                        state.Gravity, 1f / state.TimestepHz,
                        state.SolverIterations, state.Substeps, runnerArgs.ThreadCount > 1 ? 1 : 0, runnerArgs.SolverStabilization);
                    rawStepDurations[step] = Stopwatch.GetTimestamp() - start;
                    state.CompletedStepCount += 1;
                    if (runnerArgs.VerificationMode == VerificationMode.On && UnityPhysicsStackStateCapture.Append(ref stack, in runnerArgs.CaseRegistration, ref capture,
                        UnityPhysicsStackCapturePhase.Measured, 0, (uint)step + 1) != 0) return 2;
                    if (runnerArgs.RecordingMode == RecordingMode.On && UnityPhysicsRecording.Append(ref recording, in runnerArgs.CaseRegistration,
                        ref capture, (ulong)step + 1) != 0) return 2;
                }
                if (runnerArgs.VerificationMode == VerificationMode.On && UnityPhysicsStackStateCapture.Close(ref stack) != 0) return 2;
                if (runnerArgs.RecordingMode == RecordingMode.On && UnityPhysicsRecording.Complete(ref recording) != 0) return 2;
                double elapsedMilliseconds = 0.0;
                for (int step = 0; step < rawStepDurations.Length; ++step)
                    elapsedMilliseconds += rawStepDurations[step] * 1000.0 / Stopwatch.Frequency;
                int invalidCount = CountUnityPhysicsLargePyramidInvalidTransforms(ref state.World);
                UnityPhysicsBenchmarkResult result = new UnityPhysicsBenchmarkResult
                {
                    PhysicsSettings = UnityPhysicsLargePyramidVisualSettings(
                        in runnerArgs.CaseExecution, effectiveWorkerCount, runnerArgs.SolverStabilization),
                    BodyCount = state.BodyCount,
                    ShapeCount = (int)runnerArgs.CaseExecution.ShapeCount,
                    QueryCount = 0,
                    ConstraintCount = 0,
                    InvalidTransformCount = (uint)invalidCount,
                    EffectiveThreadCount = effectiveThreadCount,
                    EffectiveWorkerCount = effectiveWorkerCount,
                    CompletedWorkUnitCount = state.CompletedStepCount,
                    WorkloadElapsedMilliseconds = elapsedMilliseconds,
                    CaseValidity = invalidCount == 0 ?
                        UnityPhysicsResultValidity.Valid : UnityPhysicsResultValidity.Invalid,
                    MetricValidity = elapsedMilliseconds > 0.0 && double.IsFinite(elapsedMilliseconds) ?
                        UnityPhysicsResultValidity.Valid : UnityPhysicsResultValidity.Invalid
                };
                int status = WriteResult(runnerArgs, in result);
                return status == 0 ? WriteStepTiming(runnerArgs, rawStepDurations) : status;
            }
            finally
            {
                UnityPhysicsStackStateCapture.Abort(ref stack);
                if (runnerArgs.RecordingMode == RecordingMode.On) UnityPhysicsRecording.Abort(ref recording);
                JobsUtility.JobWorkerCount = previousWorkerCount;
                DisposeUnityPhysicsLargePyramidState(ref state);
            }
        }
    }
}
