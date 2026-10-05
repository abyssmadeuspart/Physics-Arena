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
    public static partial class UnityPhysicsBenchmarkRunner
    {
        public const string EngineId = "unity_physics";

        public static UnityPhysicsCaseDescriptor UnityPhysicsBoxContainerPileDescriptor()
        {
            return new UnityPhysicsCaseDescriptor
            {
                EngineId = EngineId
            };
        }

        public static UnityPhysicsCaseRegistration UnityPhysicsBoxContainerPileRegistration()
        {
            return new UnityPhysicsCaseRegistration
            {
                Descriptor = UnityPhysicsBoxContainerPileDescriptor(),
                RunHeadless = RunUnityPhysicsContainerPile,
                BuildVisualScene = BuildUnityPhysicsContainerVisualScene
            };
        }

        public static BlobAssetReference<PhysicsCollider> CreateBoxCollider(
            float3 size, float friction, float restitution)
        {
            return PhysicsBoxCollider.Create(new BoxGeometry
            {
                Center = float3.zero,
                Orientation = quaternion.identity,
                Size = size,
                BevelRadius = 0.0f
            }, FixtureFilter(), FixtureMaterial(friction, restitution));
        }

        public static CollisionFilter FixtureFilter()
        {
            return new CollisionFilter
            {
                BelongsTo = 0xffffffffu,
                CollidesWith = 0xffffffffu,
                GroupIndex = 0
            };
        }

        public static Material FixtureMaterial(float friction, float restitution)
        {
            Material material = Material.Default;
            material.Friction = friction;
            material.Restitution = restitution;
            material.CollisionResponse = CollisionResponsePolicy.Collide;
            return material;
        }

        public static void CreateFixture(ref PhysicsWorld world,
            BlobAssetReference<PhysicsCollider> dynamicCollider,
            NativeArray<BlobAssetReference<PhysicsCollider>> staticColliders,
            in CaseExecutionSpec execution)
        {
            CaseExecutionOpenContainer fixture = execution.OpenContainer;
            for (int index = 0; index < fixture.StaticBoxes.Length; ++index)
            {
                CaseExecutionBox box = fixture.StaticBoxes[index];
                AddStaticBox(ref world, staticColliders, index, box.Center,
                    box.HalfExtents * 2f, execution.Friction, execution.Restitution);
            }

            MassProperties massProperties = dynamicCollider.Value.MassProperties;
            NativeArray<RigidBody> dynamicBodies = world.DynamicBodies;
            NativeArray<MotionData> motionDatas = world.MotionDatas;
            NativeArray<MotionVelocity> motionVelocities = world.MotionVelocities;
            float mass = fixture.Density * massProperties.Volume;
            int bodyIndex = 0;
            for (int y = 0; y < fixture.DynamicGrid.y; ++y)
            {
                for (int z = 0; z < fixture.DynamicGrid.z; ++z)
                {
                    for (int x = 0; x < fixture.DynamicGrid.x; ++x)
                    {
                        float3 position = new float3(
                            (x - (fixture.DynamicGrid.x - 1) * 0.5f) * fixture.DynamicSpacing.x,
                            fixture.DynamicInitialY + y * fixture.DynamicSpacing.y,
                            (z - (fixture.DynamicGrid.z - 1) * 0.5f) * fixture.DynamicSpacing.z);
                        quaternion rotation = UnityPhysicsShapeRotation(execution.SelectedGeometry.Axis);
                        dynamicBodies[bodyIndex] = new RigidBody
                        {
                            WorldFromBody = new RigidTransform(rotation, position),
                            Collider = dynamicCollider,
                            Entity = Entity.Null,
                            CustomTags = 0,
                            Scale = 1.0f
                        };
                        motionDatas[bodyIndex] = new MotionData
                        {
                            WorldFromMotion = new RigidTransform(
                                math.mul(rotation, massProperties.MassDistribution.Transform.rot),
                                math.rotate(rotation, massProperties.MassDistribution.Transform.pos) + position),
                            BodyFromMotion = massProperties.MassDistribution.Transform,
                            LinearDamping = 0.0f,
                            AngularDamping = 0.0f
                        };
                        motionVelocities[bodyIndex] = new MotionVelocity
                        {
                            LinearVelocity = float3.zero,
                            AngularVelocity = float3.zero,
                            InverseInertia = math.rcp(massProperties.MassDistribution.InertiaTensor * mass),
                            InverseMass = math.rcp(mass),
                            AngularExpansionFactor = massProperties.AngularExpansionFactor,
                            GravityFactor = 1.0f
                        };
                        bodyIndex += 1;
                    }
                }
            }
        }

        public static int SampleUnityPhysicsTransforms(
            ref PhysicsWorld world, UnityPhysicsVisualStableTransform[] transforms)
        {
            if (transforms.Length < world.NumDynamicBodies)
            {
                return 2;
            }

            for (int index = 0; index < world.NumDynamicBodies; ++index)
            {
                MotionData motionData = world.MotionDatas[index];
                RigidTransform worldFromBody = math.mul(motionData.WorldFromMotion, math.inverse(motionData.BodyFromMotion));
                transforms[index] = new UnityPhysicsVisualStableTransform
                {
                    StableSlot = (uint)index,
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

        public static int BuildUnityPhysicsContainerVisualScene(
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
            CaseExecutionOpenContainer fixture = execution.OpenContainer;
            if (geometries == null || geometries.Length < 1 + fixture.StaticBoxes.Length ||
                instances == null || instances.Length < (int)execution.BodyCount)
            {
                return 2;
            }
            if (BuildResolvedVisualGeometry(in execution, in execution.SelectedGeometry,
            ref meshes, out geometries[0]) != 0) return 2;
            for (int index = 0; index < fixture.StaticBoxes.Length; ++index)
            {
                float3 halfExtents = fixture.StaticBoxes[index].HalfExtents;
                geometries[index + 1] = new UnityPhysicsVisualGeometry
                {
                    Kind = 2, ParameterX = halfExtents.x,
                    ParameterY = halfExtents.y, ParameterZ = halfExtents.z
                };
            }
            UnityPhysicsVisualStableTransform[] transforms =
                new UnityPhysicsVisualStableTransform[(int)execution.DynamicBodyCount];
            if (SampleUnityPhysicsTransforms(ref state.World, transforms) != 0)
            {
                return 2;
            }
            for (int slot = 0; slot < (int)execution.DynamicBodyCount; ++slot)
            {
                instances[slot] = new UnityPhysicsVisualInstance
                {
                    GeometryIndex = 0,
                    StableSlot = (uint)slot,
                    TransformSlot = (uint)slot,
                    InitialTransform = transforms[slot].Transform
                };
            }
            for (int index = 0; index < fixture.StaticBoxes.Length; ++index)
            {
                int stableSlot = (int)execution.DynamicBodyCount + index;
                instances[stableSlot] = CreateUnityPhysicsStaticVisualInstance(
                    (uint)(index + 1), stableSlot, fixture.StaticBoxes[index].Center);
            }
            geometryCount = 1 + fixture.StaticBoxes.Length;
            instanceCount = (int)execution.BodyCount;
            return 0;
        }

        public static UnityPhysicsVisualInstance CreateUnityPhysicsStaticVisualInstance(
            uint geometryIndex, int stableSlot, float3 position)
        {
            return new UnityPhysicsVisualInstance
            {
                GeometryIndex = geometryIndex,
                StableSlot = (uint)stableSlot,
                TransformSlot = uint.MaxValue,
                InitialTransform = new UnityPhysicsTransform
                {
                    PositionX = position.x,
                    PositionY = position.y,
                    PositionZ = position.z,
                    RotationW = 1.0f
                }
            };
        }

        public static void AddStaticBox(ref PhysicsWorld world,
            NativeArray<BlobAssetReference<PhysicsCollider>> colliders,
            int index, float3 position, float3 size, float friction, float restitution)
        {
            colliders[index] = CreateBoxCollider(size, friction, restitution);
            NativeArray<RigidBody> staticBodies = world.StaticBodies;
            staticBodies[index] = new RigidBody
            {
                WorldFromBody = new RigidTransform(quaternion.identity, position),
                Collider = colliders[index],
                Entity = Entity.Null,
                CustomTags = 0,
                Scale = 1.0f
            };
        }

        public static void StepWorld(ref PhysicsWorld world, ref Simulation simulation,
            NativeReference<int> staticBodiesChanged, float3 gravity,
            float timestep, uint solverIterations, uint substeps, int multiThreaded, CaseExecutionToggle solverStabilization)
        {
            PhysicsStep physicsStep = PhysicsStep.Default;
            physicsStep.Gravity = gravity;
            physicsStep.DirectSolverSettings = Solver.DirectSolverSettings.Default;
            physicsStep.SolverStabilizationHeuristicSettings = Solver.StabilizationHeuristicSettings.Default;
            physicsStep.SolverStabilizationHeuristicSettings.EnableSolverStabilization = solverStabilization == CaseExecutionToggle.Enabled;
            physicsStep.SynchronizeCollisionWorld = 1;

            SimulationStepInput input = new SimulationStepInput
            {
                World = world,
                TimeStep = timestep,
                Gravity = physicsStep.Gravity,
                EnableGyroscopicTorque = physicsStep.EnableGyroscopicTorque,
                NumSubsteps = (int)substeps,
                NumSolverIterations = (int)solverIterations,
                DirectSolverSettings = physicsStep.DirectSolverSettings,
                MaxDynamicDepenetrationVelocity = physicsStep.MaxDynamicDepenetrationVelocity,
                MaxStaticDepenetrationVelocity = physicsStep.MaxStaticDepenetrationVelocity,
                SynchronizeCollisionWorld = physicsStep.SynchronizeCollisionWorld > 0,
                SolverStabilizationHeuristicSettings = physicsStep.SolverStabilizationHeuristicSettings,
                HaveStaticBodiesChanged = staticBodiesChanged.AsReadOnly()
            };

            JobHandle buildBroadphaseHandle = world.CollisionWorld.ScheduleBuildBroadphaseJobs(
                ref world,
                timestep,
                physicsStep.Gravity,
                staticBodiesChanged.AsReadOnly(),
                default,
                multiThreaded != 0);
            JobHandle inputDeps = JobHandle.CombineDependencies(simulation.FinalJobHandle, buildBroadphaseHandle);
            SimulationJobHandles broadphaseHandles = simulation.ScheduleBroadphaseJobs(
                input, inputDeps, multiThreaded != 0);
            SimulationJobHandles narrowphaseHandles = simulation.ScheduleNarrowphaseJobs(
                input, broadphaseHandles.FinalExecutionHandle, multiThreaded != 0);
            SimulationJobHandles jacobianHandles = simulation.ScheduleCreateJacobiansJobs(
                input, narrowphaseHandles.FinalExecutionHandle, multiThreaded != 0);
            SimulationJobHandles handles = simulation.ScheduleSolveAndIntegrateJobs(
                input, jacobianHandles.FinalExecutionHandle, multiThreaded != 0);
            JobHandle.CombineDependencies(handles.FinalExecutionHandle, handles.FinalDisposeHandle).Complete();
            staticBodiesChanged.Value = 0;
        }

        public static int RunWarmup(in RunnerArgs runnerArgs, int multiThreaded,
            ref UnityPhysicsStackCapture stack)
        {
            CaseExecutionSpec execution = runnerArgs.CaseExecution;
            PhysicsWorld world = default;
            BlobAssetReference<PhysicsCollider> dynamicCollider = default;
            NativeArray<BlobAssetReference<PhysicsCollider>> staticColliders = default;
            NativeReference<int> staticBodiesChanged = default;
            Simulation simulation = default;
            try
            {
                world = new PhysicsWorld((int)execution.StaticBodyCount,
                    (int)execution.DynamicBodyCount, 0);
                dynamicCollider = CreateUnityPhysicsResolvedCollider(in execution,
                    in execution.SelectedGeometry, FixtureFilter(), FixtureMaterial(execution.Friction, execution.Restitution));
                staticColliders = new NativeArray<BlobAssetReference<PhysicsCollider>>(
                    (int)execution.StaticBodyCount, Allocator.Persistent);
                CreateFixture(ref world, dynamicCollider, staticColliders, in execution);
                world.UpdateIndexMaps();
                staticBodiesChanged = new NativeReference<int>(Allocator.Persistent);
                staticBodiesChanged.Value = 1;
                simulation = Simulation.Create();
                UnityPhysicsCaseView view = new() { Execution = execution, World = world };
                for (int step = 0; step <= runnerArgs.WarmupSteps; ++step)
                {
                    if (step != 0)
                    {
                        StepWorld(ref world, ref simulation, staticBodiesChanged,
                            execution.Gravity, 1f / execution.TimestepHz,
                            execution.SolverIterations, execution.Substeps, multiThreaded, runnerArgs.SolverStabilization);
                        staticBodiesChanged.Value = 0;
                    }
                    if (runnerArgs.VerificationMode == VerificationMode.On && UnityPhysicsStackStateCapture.Append(ref stack, in runnerArgs.CaseRegistration, ref view,
                        step == 0 ? UnityPhysicsStackCapturePhase.Construction : UnityPhysicsStackCapturePhase.Warmup, 0, (uint)step) != 0) return 2;
                }
                return 0;
            }
            finally
            {
                DisposeCaseResources(ref world, ref simulation, ref dynamicCollider, ref staticColliders, ref staticBodiesChanged);
            }
        }

        public static void DisposeCaseResources(
            ref PhysicsWorld world,
            ref Simulation simulation,
            ref BlobAssetReference<PhysicsCollider> dynamicCollider,
            ref NativeArray<BlobAssetReference<PhysicsCollider>> staticColliders,
            ref NativeReference<int> staticBodiesChanged)
        {
            if (staticBodiesChanged.IsCreated)
            {
                // container case owns this native change flag
                staticBodiesChanged.Dispose();
            }
            // container case owns the direct simulation
            simulation.Dispose();
            // container case owns the direct physics world
            world.Dispose();
            if (dynamicCollider.IsCreated)
            {
                // container case created and owns the dynamic collider blob
                dynamicCollider.Dispose();
            }
            if (staticColliders.IsCreated)
            {
                for (int index = 0; index < staticColliders.Length; ++index)
                {
                    if (staticColliders[index].IsCreated)
                    {
                        // container case created and owns each static collider blob
                        staticColliders[index].Dispose();
                    }
                }
                // container case owns the collider reference array
                staticColliders.Dispose();
            }
        }

        public static string UnityPhysicsContainerVisualSettings(
            in CaseExecutionSpec execution, int workerCount, CaseExecutionToggle solverStabilization)
        {
            return FormattableString.Invariant(
                $"solver_iterations={execution.SolverIterations}; substeps={execution.Substeps}; solver_type=iterative; stabilization={(solverStabilization == CaseExecutionToggle.Enabled ? "on" : "off")}; synchronize_collision_world=on; sleep=not_applicable; worker_count={workerCount}");
        }

        public static int CountUnityPhysicsContainerPileInvalidTransforms(ref PhysicsWorld world)
        {
            int invalidTransformCount = 0;
            for (int index = 0; index < world.NumDynamicBodies; ++index)
            {
                MotionData motionData = world.MotionDatas[index];
                RigidTransform worldFromBody = math.mul(
                    motionData.WorldFromMotion, math.inverse(motionData.BodyFromMotion));
                if (!math.all(math.isfinite(worldFromBody.pos)) ||
                    !math.all(math.isfinite(worldFromBody.rot.value)))
                {
                    invalidTransformCount += 1;
                }
            }
            return invalidTransformCount;
        }

        public static int RunUnityPhysicsContainerPile(RunnerArgs runnerArgs)
        {
            int previousWorkerCount = JobsUtility.JobWorkerCount;
            PhysicsWorld world = default;
            BlobAssetReference<PhysicsCollider> dynamicCollider = default;
            NativeArray<BlobAssetReference<PhysicsCollider>> staticColliders = default;
            NativeReference<int> staticBodiesChanged = default;
            Simulation simulation = default;

            UnityPhysicsRecordingWriter recording = default;
            UnityPhysicsStackCapture stack = default;
            try
            {
                int requestedWorkerCount = math.max(0, runnerArgs.ThreadCount - 1);
                JobsUtility.JobWorkerCount = requestedWorkerCount;
                int effectiveWorkerCount = JobsUtility.JobWorkerCount;
                int effectiveThreadCount = effectiveWorkerCount + 1;
                long[] rawStepDurations = new long[runnerArgs.StepCount];
                if (runnerArgs.VerificationMode == VerificationMode.On && UnityPhysicsStackStateCapture.Open(in runnerArgs, out stack) != 0) return 2;
                if (runnerArgs.WarmupSteps > 0)
                {
                    if (RunWarmup(in runnerArgs, runnerArgs.ThreadCount > 1 ? 1 : 0, ref stack) != 0) return 2;
                }

                CaseExecutionSpec execution = runnerArgs.CaseExecution;
                world = new PhysicsWorld((int)execution.StaticBodyCount,
                    (int)execution.DynamicBodyCount, 0);
                dynamicCollider = CreateUnityPhysicsResolvedCollider(in execution,
                    in execution.SelectedGeometry, FixtureFilter(), FixtureMaterial(execution.Friction, execution.Restitution));
                staticColliders = new NativeArray<BlobAssetReference<PhysicsCollider>>(
                    (int)execution.StaticBodyCount, Allocator.Persistent);
                CreateFixture(ref world, dynamicCollider, staticColliders, in execution);
                world.UpdateIndexMaps();
                staticBodiesChanged = new NativeReference<int>(Allocator.Persistent);
                staticBodiesChanged.Value = 1;
                simulation = Simulation.Create();

                UnityPhysicsCaseView capture = new UnityPhysicsCaseView
                {
                    Execution = runnerArgs.CaseExecution,
                    World = world
                };
                uint segment = runnerArgs.WarmupSteps > 0 ? 1u : 0u;
                if (runnerArgs.VerificationMode == VerificationMode.On && UnityPhysicsStackStateCapture.Append(ref stack, in runnerArgs.CaseRegistration, ref capture,
                    UnityPhysicsStackCapturePhase.Construction, segment, 0) != 0) return 2;
                if (runnerArgs.RecordingMode == RecordingMode.On && UnityPhysicsRecording.Begin(in runnerArgs, ref capture, out recording) != 0) return 2;
                for (int step = 0; step < runnerArgs.StepCount; ++step)
                {
                    long start = Stopwatch.GetTimestamp();
                    StepWorld(ref world, ref simulation, staticBodiesChanged,
                        execution.Gravity, 1f / execution.TimestepHz,
                        execution.SolverIterations, execution.Substeps, runnerArgs.ThreadCount > 1 ? 1 : 0, runnerArgs.SolverStabilization);
                    rawStepDurations[step] = Stopwatch.GetTimestamp() - start;
                    if (runnerArgs.VerificationMode == VerificationMode.On && UnityPhysicsStackStateCapture.Append(ref stack, in runnerArgs.CaseRegistration, ref capture,
                        UnityPhysicsStackCapturePhase.Measured, segment, (uint)step + 1) != 0) return 2;
                    if (runnerArgs.RecordingMode == RecordingMode.On && UnityPhysicsRecording.Append(ref recording, in runnerArgs.CaseRegistration,
                        ref capture, (ulong)step + 1) != 0) return 2;
                    staticBodiesChanged.Value = 0;
                }

                if (runnerArgs.VerificationMode == VerificationMode.On && UnityPhysicsStackStateCapture.Close(ref stack) != 0) return 2;
                if (runnerArgs.RecordingMode == RecordingMode.On && UnityPhysicsRecording.Complete(ref recording) != 0) return 2;
                double elapsedMilliseconds = 0.0;
                for (int step = 0; step < rawStepDurations.Length; ++step)
                {
                    elapsedMilliseconds += rawStepDurations[step] * 1000.0 / Stopwatch.Frequency;
                }
                int invalidTransformCount = CountUnityPhysicsContainerPileInvalidTransforms(ref world);
                double msPerStep = elapsedMilliseconds / runnerArgs.StepCount;
                int metricValid = elapsedMilliseconds > 0.0 &&
                    !double.IsNaN(elapsedMilliseconds) && !double.IsInfinity(elapsedMilliseconds) &&
                    msPerStep > 0.0 && !double.IsNaN(msPerStep) && !double.IsInfinity(msPerStep) ? 1 : 0;
                UnityPhysicsBenchmarkResult result = new UnityPhysicsBenchmarkResult
                {
                    PhysicsSettings = UnityPhysicsContainerVisualSettings(
                        in execution, effectiveWorkerCount, runnerArgs.SolverStabilization),
                    BodyCount = (int)execution.BodyCount,
                    ShapeCount = (int)execution.ShapeCount,
                    QueryCount = (int)execution.QueryCount,
                    ConstraintCount = (int)execution.ConstraintCount,
					InvalidTransformCount = (uint)invalidTransformCount,
                    EffectiveThreadCount = effectiveThreadCount,
                    EffectiveWorkerCount = effectiveWorkerCount,
                    CompletedWorkUnitCount = runnerArgs.StepCount,
                    WorkloadElapsedMilliseconds = elapsedMilliseconds,
                    CaseValidity = invalidTransformCount == 0 ?
                        UnityPhysicsResultValidity.Valid : UnityPhysicsResultValidity.Invalid,
                    MetricValidity = metricValid != 0 ?
                        UnityPhysicsResultValidity.Valid : UnityPhysicsResultValidity.Invalid
                };
                int resultStatus = WriteResult(runnerArgs, in result);
                return resultStatus == 0 ? WriteStepTiming(runnerArgs, rawStepDurations) : resultStatus;
            }
            finally
            {
                UnityPhysicsStackStateCapture.Abort(ref stack);
                if (runnerArgs.RecordingMode == RecordingMode.On) UnityPhysicsRecording.Abort(ref recording);
                JobsUtility.JobWorkerCount = previousWorkerCount;
                DisposeCaseResources(
                    ref world, ref simulation, ref dynamicCollider, ref staticColliders, ref staticBodiesChanged);
            }
        }
    }
}
