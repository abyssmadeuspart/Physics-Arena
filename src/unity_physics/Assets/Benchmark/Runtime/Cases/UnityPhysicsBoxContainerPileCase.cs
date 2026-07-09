using System;
using Unity.Collections;
using Unity.Entities;
using Unity.Jobs;
using Unity.Mathematics;
using Unity.Physics;
using PhysicsBoxCollider = Unity.Physics.BoxCollider;
using PhysicsCollider = Unity.Physics.Collider;

namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
    public static partial class UnityPhysicsBenchmarkRunner
    {
        private const int PileXCount = 25;
        private const int PileYCount = 16;
        private const int PileZCount = 25;
        private const int DynamicBodyCount = PileXCount * PileYCount * PileZCount;
        private const int StaticBodyCount = 5;
        private const int BodyCount = DynamicBodyCount + StaticBodyCount;
        private const float HalfExtent = 0.5f;
        private const float Spacing = 1.02f;
        private const float InitialY = 24.51f;
        private const float Timestep = 1.0f / 60.0f;
        private const string EngineId = "unity_physics";
        private const string CaseId = "box_container_pile_10k";
        private const float InnerHalfWidth = 15.5f;
        private const float CeilingY = 96.0f;
        private const string FixtureSemantic = "open_container_falling_pile";
        private const string FixtureVersion = "unity_physics_open_container_v1";

        public static UnityPhysicsCaseDescriptor UnityPhysicsBoxContainerPileDescriptor()
        {
            return new UnityPhysicsCaseDescriptor(
                EngineId,
                CaseId,
                FixtureSemantic,
                FixtureVersion,
                DynamicBodyCount,
                StaticBodyCount,
                BodyCount,
                HalfExtent);
        }

        public static int ResolveUnityPhysicsCase(string caseId, out UnityPhysicsCaseDescriptor descriptor)
        {
            if (caseId == CaseId)
            {
                descriptor = UnityPhysicsBoxContainerPileDescriptor();
                return 0;
            }

            descriptor = default;
            Console.Error.WriteLine("invalid_argument name=case value=" + caseId);
            return 2;
        }

        private static BlobAssetReference<PhysicsCollider> CreateBoxCollider(float3 size)
        {
            return PhysicsBoxCollider.Create(new BoxGeometry
            {
                Center = float3.zero,
                Orientation = quaternion.identity,
                Size = size,
                BevelRadius = 0.0f
            }, FixtureFilter(), FixtureMaterial());
        }

        private static CollisionFilter FixtureFilter()
        {
            return new CollisionFilter
            {
                BelongsTo = 0xffffffffu,
                CollidesWith = 0xffffffffu,
                GroupIndex = 0
            };
        }

        private static Unity.Physics.Material FixtureMaterial()
        {
            Unity.Physics.Material material = Unity.Physics.Material.Default;
            material.Friction = 0.5f;
            material.Restitution = 0.0f;
            material.CollisionResponse = CollisionResponsePolicy.Collide;
            return material;
        }

        private static void CreateFixture(ref PhysicsWorld world, BlobAssetReference<PhysicsCollider> dynamicCollider, NativeArray<BlobAssetReference<PhysicsCollider>> staticColliders)
        {
            AddStaticBox(ref world, staticColliders, 0, new float3(0.0f, -0.5f, 0.0f), new float3(29.0f, 1.0f, 29.0f));
            AddStaticBox(ref world, staticColliders, 1, new float3(-14.5f, 11.75f, 0.0f), new float3(1.0f, 24.5f, 29.0f));
            AddStaticBox(ref world, staticColliders, 2, new float3(14.5f, 11.75f, 0.0f), new float3(1.0f, 24.5f, 29.0f));
            AddStaticBox(ref world, staticColliders, 3, new float3(0.0f, 11.75f, -14.5f), new float3(29.0f, 24.5f, 1.0f));
            AddStaticBox(ref world, staticColliders, 4, new float3(0.0f, 11.75f, 14.5f), new float3(29.0f, 24.5f, 1.0f));

            MassProperties massProperties = dynamicCollider.Value.MassProperties;
            NativeArray<RigidBody> dynamicBodies = world.DynamicBodies;
            NativeArray<MotionData> motionDatas = world.MotionDatas;
            NativeArray<MotionVelocity> motionVelocities = world.MotionVelocities;
            int bodyIndex = 0;
            for (int y = 0; y < PileYCount; ++y)
            {
                for (int z = 0; z < PileZCount; ++z)
                {
                    for (int x = 0; x < PileXCount; ++x)
                    {
                        float3 position = new float3(
                            (x - (PileXCount - 1) * 0.5f) * Spacing,
                            InitialY + y * Spacing,
                            (z - (PileZCount - 1) * 0.5f) * Spacing);
                        quaternion rotation = quaternion.identity;
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
                            InverseInertia = math.rcp(massProperties.MassDistribution.InertiaTensor),
                            InverseMass = 1.0f,
                            AngularExpansionFactor = massProperties.AngularExpansionFactor,
                            GravityFactor = 1.0f
                        };
                        bodyIndex += 1;
                    }
                }
            }
        }

        public static int SampleUnityPhysicsTransforms(ref PhysicsWorld world, UnityPhysicsTransform[] transforms)
        {
            if (transforms.Length < DynamicBodyCount)
            {
                return 2;
            }

            for (int index = 0; index < world.NumDynamicBodies; ++index)
            {
                MotionData motionData = world.MotionDatas[index];
                RigidTransform worldFromBody = math.mul(motionData.WorldFromMotion, math.inverse(motionData.BodyFromMotion));
                transforms[index] = new UnityPhysicsTransform
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

            return world.NumDynamicBodies == DynamicBodyCount ? 0 : 2;
        }

        public static int CopyUnityPhysicsStaticBoxes(UnityPhysicsStaticBox[] boxes)
        {
            if (boxes.Length < StaticBodyCount)
            {
                return 2;
            }

            boxes[0] = new UnityPhysicsStaticBox { PositionX = 0.0f, PositionY = -0.5f, PositionZ = 0.0f, HalfExtentX = 14.5f, HalfExtentY = 0.5f, HalfExtentZ = 14.5f };
            boxes[1] = new UnityPhysicsStaticBox { PositionX = -14.5f, PositionY = 11.75f, PositionZ = 0.0f, HalfExtentX = 0.5f, HalfExtentY = 12.25f, HalfExtentZ = 14.5f };
            boxes[2] = new UnityPhysicsStaticBox { PositionX = 14.5f, PositionY = 11.75f, PositionZ = 0.0f, HalfExtentX = 0.5f, HalfExtentY = 12.25f, HalfExtentZ = 14.5f };
            boxes[3] = new UnityPhysicsStaticBox { PositionX = 0.0f, PositionY = 11.75f, PositionZ = -14.5f, HalfExtentX = 14.5f, HalfExtentY = 12.25f, HalfExtentZ = 0.5f };
            boxes[4] = new UnityPhysicsStaticBox { PositionX = 0.0f, PositionY = 11.75f, PositionZ = 14.5f, HalfExtentX = 14.5f, HalfExtentY = 12.25f, HalfExtentZ = 0.5f };
            return 0;
        }

        private static void AddStaticBox(ref PhysicsWorld world, NativeArray<BlobAssetReference<PhysicsCollider>> colliders, int index, float3 position, float3 size)
        {
            colliders[index] = CreateBoxCollider(size);
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

        private static void StepWorld(ref PhysicsWorld world, ref Simulation simulation, NativeReference<int> staticBodiesChanged, bool multiThreaded)
        {
            PhysicsStep physicsStep = PhysicsStep.Default;
            physicsStep.Gravity = new float3(0.0f, -10.0f, 0.0f);
            physicsStep.DirectSolverSettings = Solver.DirectSolverSettings.Default;
            physicsStep.SolverStabilizationHeuristicSettings = Solver.StabilizationHeuristicSettings.Default;
            physicsStep.SynchronizeCollisionWorld = 1;

            SimulationStepInput input = new SimulationStepInput
            {
                World = world,
                TimeStep = Timestep,
                Gravity = physicsStep.Gravity,
                EnableGyroscopicTorque = physicsStep.EnableGyroscopicTorque,
                NumSubsteps = physicsStep.SubstepCount,
                NumSolverIterations = physicsStep.SolverIterationCount,
                DirectSolverSettings = physicsStep.DirectSolverSettings,
                MaxDynamicDepenetrationVelocity = physicsStep.MaxDynamicDepenetrationVelocity,
                MaxStaticDepenetrationVelocity = physicsStep.MaxStaticDepenetrationVelocity,
                SynchronizeCollisionWorld = physicsStep.SynchronizeCollisionWorld > 0,
                SolverStabilizationHeuristicSettings = physicsStep.SolverStabilizationHeuristicSettings,
                HaveStaticBodiesChanged = staticBodiesChanged.AsReadOnly()
            };

            JobHandle buildBroadphaseHandle = world.CollisionWorld.ScheduleBuildBroadphaseJobs(
                ref world,
                Timestep,
                physicsStep.Gravity,
                staticBodiesChanged.AsReadOnly(),
                default,
                multiThreaded);
            JobHandle inputDeps = JobHandle.CombineDependencies(simulation.FinalJobHandle, buildBroadphaseHandle);
            SimulationJobHandles broadphaseHandles = simulation.ScheduleBroadphaseJobs(input, inputDeps, multiThreaded);
            SimulationJobHandles narrowphaseHandles = simulation.ScheduleNarrowphaseJobs(input, broadphaseHandles.FinalExecutionHandle, multiThreaded);
            SimulationJobHandles jacobianHandles = simulation.ScheduleCreateJacobiansJobs(input, narrowphaseHandles.FinalExecutionHandle, multiThreaded);
            SimulationJobHandles handles = simulation.ScheduleSolveAndIntegrateJobs(input, jacobianHandles.FinalExecutionHandle, multiThreaded);
            JobHandle.CombineDependencies(handles.FinalExecutionHandle, handles.FinalDisposeHandle).Complete();
        }

        private static void RunWarmup(int warmupSteps, bool multiThreaded)
        {
            PhysicsWorld world = default;
            BlobAssetReference<PhysicsCollider> dynamicCollider = default;
            NativeArray<BlobAssetReference<PhysicsCollider>> staticColliders = default;
            NativeReference<int> staticBodiesChanged = default;
            Simulation simulation = default;
            try
            {
                world = new PhysicsWorld(StaticBodyCount, DynamicBodyCount, 0);
                dynamicCollider = CreateBoxCollider(new float3(1.0f, 1.0f, 1.0f));
                staticColliders = new NativeArray<BlobAssetReference<PhysicsCollider>>(StaticBodyCount, Allocator.Persistent);
                CreateFixture(ref world, dynamicCollider, staticColliders);
                world.UpdateIndexMaps();
                staticBodiesChanged = new NativeReference<int>(Allocator.Persistent);
                staticBodiesChanged.Value = 1;
                simulation = Simulation.Create();
                for (int step = 0; step < warmupSteps; ++step)
                {
                    StepWorld(ref world, ref simulation, staticBodiesChanged, multiThreaded);
                    staticBodiesChanged.Value = 0;
                }
            }
            finally
            {
                DisposeCaseResources(ref world, ref simulation, ref dynamicCollider, ref staticColliders, ref staticBodiesChanged);
            }
        }

        private static void DisposeCaseResources(
            ref PhysicsWorld world,
            ref Simulation simulation,
            ref BlobAssetReference<PhysicsCollider> dynamicCollider,
            ref NativeArray<BlobAssetReference<PhysicsCollider>> staticColliders,
            ref NativeReference<int> staticBodiesChanged)
        {
            if (staticBodiesChanged.IsCreated)
            {
                staticBodiesChanged.Dispose();
            }
            simulation.Dispose();
            world.Dispose();
            if (dynamicCollider.IsCreated)
            {
                dynamicCollider.Dispose();
            }
            if (staticColliders.IsCreated)
            {
                for (int index = 0; index < staticColliders.Length; ++index)
                {
                    if (staticColliders[index].IsCreated)
                    {
                        staticColliders[index].Dispose();
                    }
                }
                staticColliders.Dispose();
            }
        }
    }
}
