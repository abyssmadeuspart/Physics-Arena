using System;
using Unity.Collections;
using Unity.Entities;
using Unity.Jobs;
using Unity.Jobs.LowLevel.Unsafe;
using Unity.Mathematics;
using Unity.Physics;
using Unity.Physics.Systems;
using PhysicsBoxCollider = Unity.Physics.BoxCollider;
using PhysicsCollider = Unity.Physics.Collider;
using PhysicsMTransform = Unity.Physics.Math.MTransform;

namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
    public struct UnityPhysicsRagdollQuality
    {
        public double SquaredGapSum;
        public double MaximumGap;
        public ulong JointSamples;
        public ulong BodySamples;
        public ulong InvalidBodySamples;
        public ulong MissingBodySamples;
        public ulong WorstJoint;
        public ulong WorstStep;
        public ulong FirstInvalidBody;
        public ulong FirstInvalidStep;
        public uint CompletedSteps;
    }

    public struct UnityPhysicsRagdollState
    {
        public UnityPhysicsRagdollQuality Quality;
        public PhysicsWorld World;
        public NativeArray<BlobAssetReference<PhysicsCollider>> Colliders;
        public NativeReference<int> StaticBodiesChanged;
        public Simulation Simulation;
        public CaseExecutionSpec Execution;
    }

    public static partial class UnityPhysicsBenchmarkRunner
    {
        public static readonly string[] RagdollQualityIds =
        {
            "joint_anchor_gap_rms_m", "joint_anchor_gap_max_m", "worst_joint_id",
            "worst_joint_step", "joint_sample_count", "body_sample_count",
            "invalid_body_sample_count", "missing_body_sample_count",
            "first_invalid_body_id", "first_invalid_step"
        };

        public static int RagdollQualityPoseValid(in RigidTransform pose)
        {
            return math.all(math.isfinite(pose.pos)) && math.all(math.isfinite(pose.rot.value)) &&
                math.any(pose.rot.value != 0f) ? 1 : 0;
        }

        public static void RagdollQualityAnchor(in RigidTransform pose, in float3 local,
            out double x, out double y, out double z)
        {
            double4 q = pose.rot.value;
            double3 v = local;
            double3 t = 2.0 * math.cross(q.xyz, v);
            double3 world = (double3)pose.pos + v + q.w * t + math.cross(q.xyz, t);
            x = world.x;
            y = world.y;
            z = world.z;
        }

        public static RigidTransform RagdollQualityPose(ref UnityPhysicsRagdollState state, int body)
        {
            MotionData motion = state.World.MotionDatas[body];
            return math.mul(motion.WorldFromMotion, math.inverse(motion.BodyFromMotion));
        }

        public static void ObserveRagdollQuality(ref UnityPhysicsRagdollState state)
        {
            ref UnityPhysicsRagdollQuality quality = ref state.Quality;
            CaseExecutionSpec execution = state.Execution;
            ++quality.CompletedSteps;
            for (int index = 0; index < execution.DynamicBodyCount; ++index)
            {
                if (index < state.World.NumDynamicBodies)
                {
                    RigidTransform pose = RagdollQualityPose(ref state, index);
                    ++quality.BodySamples;
                    if (RagdollQualityPoseValid(in pose) != 0) continue;
                    ++quality.InvalidBodySamples;
                }
                else ++quality.MissingBodySamples;
                if (quality.FirstInvalidStep == 0)
                {
                    quality.FirstInvalidBody = (ulong)index;
                    quality.FirstInvalidStep = quality.CompletedSteps;
                }
            }
            CaseExecutionRagdoll fixture = execution.Ragdoll;
            for (int ragdoll = 0; ragdoll < fixture.RagdollGrid.x * fixture.RagdollGrid.y; ++ragdoll)
            {
                for (int joint = 0; joint < fixture.Links.Length; ++joint)
                {
                    CaseExecutionRagdollLink link = fixture.Links[joint];
                    int parentIndex = ragdoll * fixture.Parts.Length + link.ParentPart;
                    int childIndex = ragdoll * fixture.Parts.Length + link.ChildPart;
                    if (parentIndex >= state.World.NumDynamicBodies || childIndex >= state.World.NumDynamicBodies) continue;
                    RigidTransform parent = RagdollQualityPose(ref state, parentIndex);
                    RigidTransform child = RagdollQualityPose(ref state, childIndex);
                    if (RagdollQualityPoseValid(in parent) == 0 || RagdollQualityPoseValid(in child) == 0) continue;
                    RagdollQualityAnchor(in parent, in link.ParentLocalAnchor, out double ax, out double ay, out double az);
                    RagdollQualityAnchor(in child, in link.ChildLocalAnchor, out double bx, out double by, out double bz);
                    double square = (ax - bx) * (ax - bx) + (ay - by) * (ay - by) + (az - bz) * (az - bz);
                    double gap = math.sqrt(square);
                    quality.SquaredGapSum += square;
                    ++quality.JointSamples;
                    if (quality.WorstStep == 0 || gap > quality.MaximumGap)
                    {
                        quality.MaximumGap = gap;
                        quality.WorstJoint = (ulong)(ragdoll * fixture.Links.Length + joint);
                        quality.WorstStep = quality.CompletedSteps;
                    }
                }
            }
        }

        public static ulong RagdollQualityValue(in UnityPhysicsRagdollQuality quality, int ordinal)
        {
            return ordinal switch
            {
                0 => math.asulong(math.sqrt(quality.SquaredGapSum / quality.JointSamples)),
                1 => math.asulong(quality.MaximumGap),
                2 => quality.WorstJoint,
                3 => quality.WorstStep,
                4 => quality.JointSamples,
                5 => quality.BodySamples,
                6 => quality.InvalidBodySamples,
                7 => quality.MissingBodySamples,
                8 => quality.FirstInvalidBody,
                9 => quality.FirstInvalidStep,
                _ => throw new ArgumentOutOfRangeException(nameof(ordinal))
            };
        }

        public static UnityPhysicsCaseDescriptor UnityPhysicsRagdollStairTumbleDescriptor()
        {
            return new UnityPhysicsCaseDescriptor
            {
                EngineId = "unity_physics"
            };
        }

        public static UnityPhysicsCaseRegistration UnityPhysicsRagdollStairTumbleRegistration()
        {
            return new UnityPhysicsCaseRegistration
            {
                Descriptor = UnityPhysicsRagdollStairTumbleDescriptor(),
                RunHeadless = RunUnityPhysicsRagdollStairTumble,
                BuildVisualScene = BuildUnityPhysicsRagdollVisualScene,
                BuildVisualDebugPrimitives = BuildUnityPhysicsRagdollVisualDebugPrimitives
            };
        }

        public static CollisionFilter UnityPhysicsRagdollFilter()
        {
            return new CollisionFilter
            {
                BelongsTo = 0xffffffffu,
                CollidesWith = 0xffffffffu,
                GroupIndex = 0
            };
        }

        public static Material UnityPhysicsRagdollMaterial(
            in CaseExecutionSpec execution)
        {
            Material material = Material.Default;
            material.Friction = execution.Friction;
            material.Restitution = execution.Restitution;
            material.CollisionResponse = CollisionResponsePolicy.Collide;
            return material;
        }

        public static BlobAssetReference<PhysicsCollider> CreateUnityPhysicsRagdollBoxCollider(
            float3 halfExtents, in CaseExecutionSpec execution)
        {
            return PhysicsBoxCollider.Create(new BoxGeometry
            {
                Center = float3.zero,
                Orientation = quaternion.identity,
                Size = halfExtents * 2.0f,
                BevelRadius = 0.0f
            }, UnityPhysicsRagdollFilter(), UnityPhysicsRagdollMaterial(in execution));
        }

        public static void CreateUnityPhysicsRagdollColliders(
            in CaseExecutionSpec execution,
            ref NativeArray<BlobAssetReference<PhysicsCollider>> colliders)
        {
            CaseExecutionRagdoll fixture = execution.Ragdoll;
            for (int partIndex = 0; partIndex < fixture.Parts.Length; ++partIndex)
            {
                CaseExecutionRagdollPart part = fixture.Parts[partIndex];
                int geometryIndex = (int)UnityPhysicsRagdollGeometryIndex(in fixture, partIndex);
                if (colliders[geometryIndex].IsCreated) continue;
                CaseExecutionGeometry geometry = UnityPhysicsCaseExecutionWire.PartGeometry(in part);
                colliders[geometryIndex] = CreateUnityPhysicsResolvedCollider(in execution,
                    in geometry, UnityPhysicsRagdollFilter(), UnityPhysicsRagdollMaterial(in execution));
            }
            int stairColliderIndex = fixture.Parts.Length;
            colliders[stairColliderIndex] = CreateUnityPhysicsRagdollBoxCollider(
                new float3(
                    fixture.StairHalfWidth,
                    fixture.StairHalfHeight,
                    fixture.StairHalfDepth),
                in execution);
            for (int index = 0; index < fixture.ExtraStaticBoxes.Length; ++index)
            {
                colliders[stairColliderIndex + 1 + index] =
                    CreateUnityPhysicsRagdollBoxCollider(
                        fixture.ExtraStaticBoxes[index].HalfExtents,
                        in execution);
            }
        }

        public static quaternion UnityPhysicsRagdollRotation(
            in CaseExecutionRagdoll fixture, int ragdollIndex)
        {
            float yaw = math.radians(fixture.YawPatternDegrees[
                ragdollIndex % fixture.YawPatternDegrees.Length]);
            return math.mul(
                quaternion.AxisAngle(new float3(0.0f, 1.0f, 0.0f), yaw),
                quaternion.AxisAngle(
                    new float3(1.0f, 0.0f, 0.0f),
                    math.radians(fixture.PitchDegrees)));
        }

        public static float3 UnityPhysicsRagdollBase(
            in CaseExecutionRagdoll fixture, int row, int column)
        {
            return new float3(
                (column - ((int)fixture.RagdollGrid.y - 1) * 0.5f) *
                    fixture.ColumnSpacing,
                ((int)fixture.StairCount - 1 - row) * fixture.StairRise +
                    fixture.BaseHeightOffset,
                (row - ((int)fixture.StairCount - 1) * 0.5f) * fixture.RowSpacing);
        }

        public static int CreateUnityPhysicsRagdollJointPrototype(out Joint prototype)
        {
            prototype = default;
            using (World conversionWorld = new World("Ragdoll joint conversion"))
            {
                SystemHandle buildPhysicsWorld =
                    conversionWorld.GetOrCreateSystem<BuildPhysicsWorld>();
                EntityArchetype jointArchetype = conversionWorld.EntityManager.CreateArchetype(
                    typeof(PhysicsJoint), typeof(PhysicsConstrainedBodyPair), typeof(PhysicsWorldIndex));
                Entity jointEntity = conversionWorld.EntityManager.CreateEntity(jointArchetype);
                conversionWorld.EntityManager.SetComponentData(
                    jointEntity, PhysicsJoint.CreateBallAndSocket(float3.zero, float3.zero));
                conversionWorld.EntityManager.SetComponentData(
                    jointEntity,
                    new PhysicsConstrainedBodyPair(Entity.Null, Entity.Null, false));
                conversionWorld.EntityManager.SetSharedComponent(
                    jointEntity, PhysicsWorldIndex.Default);

                buildPhysicsWorld.Update(conversionWorld.Unmanaged);
                JobHandle dependency =
                    conversionWorld.Unmanaged.ResolveSystemStateRef(buildPhysicsWorld).Dependency;
                dependency.Complete();
                PhysicsWorldSingleton singleton =
                    conversionWorld.EntityManager
                        .CreateEntityQuery(typeof(PhysicsWorldSingleton))
                        .GetSingleton<PhysicsWorldSingleton>();
                if (singleton.PhysicsWorld.NumJoints != 1)
                {
                    return 2;
                }
                prototype = singleton.PhysicsWorld.Joints[0];
            }
            return 0;
        }

        public static void AddUnityPhysicsRagdollStaticBodies(
            ref PhysicsWorld world,
            NativeArray<BlobAssetReference<PhysicsCollider>> colliders,
            in CaseExecutionSpec execution)
        {
            CaseExecutionRagdoll fixture = execution.Ragdoll;
            NativeArray<RigidBody> staticBodies = world.StaticBodies;
            int stairCount = (int)fixture.StairCount;
            int stairColliderIndex = fixture.Parts.Length;
            for (int row = 0; row < stairCount; ++row)
            {
                staticBodies[row] = new RigidBody
                {
                    WorldFromBody = new RigidTransform(
                        quaternion.identity,
                        new float3(
                            0.0f,
                            (stairCount - 1 - row) * fixture.StairRise -
                                fixture.StairHalfHeight,
                            (row - (stairCount - 1) * 0.5f) *
                                fixture.StairDepth)),
                    Collider = colliders[stairColliderIndex],
                    Entity = Entity.Null,
                    CustomTags = 0,
                    Scale = 1.0f
                };
            }
            for (int index = 0; index < fixture.ExtraStaticBoxes.Length; ++index)
            {
                CaseExecutionBox box = fixture.ExtraStaticBoxes[index];
                staticBodies[stairCount + index] = new RigidBody
                {
                    WorldFromBody = new RigidTransform(
                        quaternion.identity, box.Center),
                    Collider = colliders[stairColliderIndex + 1 + index],
                    Entity = Entity.Null,
                    CustomTags = 0,
                    Scale = 1.0f
                };
            }
        }

        public static void AddUnityPhysicsRagdollDynamicBodies(
            ref PhysicsWorld world,
            NativeArray<BlobAssetReference<PhysicsCollider>> colliders,
            in CaseExecutionSpec execution)
        {
            CaseExecutionRagdoll fixture = execution.Ragdoll;
            NativeArray<RigidBody> dynamicBodies = world.DynamicBodies;
            NativeArray<MotionData> motionDatas = world.MotionDatas;
            NativeArray<MotionVelocity> motionVelocities = world.MotionVelocities;
            for (int row = 0; row < fixture.RagdollGrid.x; ++row)
            {
                for (int column = 0; column < fixture.RagdollGrid.y; ++column)
                {
                    int ragdollIndex = row * (int)fixture.RagdollGrid.y + column;
                    quaternion rotation = UnityPhysicsRagdollRotation(
                        in fixture, ragdollIndex);
                    float3 ragdollBase = UnityPhysicsRagdollBase(
                        in fixture, row, column);
                    for (int partIndex = 0; partIndex < fixture.Parts.Length; ++partIndex)
                    {
                        CaseExecutionRagdollPart part = fixture.Parts[partIndex];
                        int bodyIndex =
                            ragdollIndex * fixture.Parts.Length + partIndex;
                        BlobAssetReference<PhysicsCollider> collider =
                            colliders[(int)UnityPhysicsRagdollGeometryIndex(in fixture, partIndex)];
                        MassProperties massProperties = collider.Value.MassProperties;
                        float3 position = ragdollBase +
                            math.rotate(rotation, part.Center);
                        quaternion bodyRotation = math.mul(rotation, UnityPhysicsShapeRotation(part.Axis));
                        dynamicBodies[bodyIndex] = new RigidBody
                        {
                            WorldFromBody = new RigidTransform(bodyRotation, position),
                            Collider = collider,
                            Entity = Entity.Null,
                            CustomTags = 0,
                            Scale = 1.0f
                        };
                        motionDatas[bodyIndex] = new MotionData
                        {
                            WorldFromMotion = new RigidTransform(
                                math.mul(bodyRotation, massProperties.MassDistribution.Transform.rot),
                                math.rotate(
                                    bodyRotation,
                                    massProperties.MassDistribution.Transform.pos) + position),
                            BodyFromMotion = massProperties.MassDistribution.Transform,
                            LinearDamping = fixture.LinearDamping,
                            AngularDamping = fixture.AngularDamping
                        };
                        motionVelocities[bodyIndex] = new MotionVelocity
                        {
                            LinearVelocity = new float3(
                                0.0f,
                                0.0f,
                                row == 0 ?
                                    fixture.TriggerRowSpeed :
                                    fixture.FollowerRowSpeed),
                            AngularVelocity = float3.zero,
                            InverseInertia =
                                math.rcp(
                                    massProperties.MassDistribution.InertiaTensor *
                                    fixture.PartMass),
                            InverseMass = math.rcp(fixture.PartMass),
                            AngularExpansionFactor = massProperties.AngularExpansionFactor,
                            GravityFactor = 1.0f
                        };
                    }
                }
            }
        }

        public static void AddUnityPhysicsRagdollJoints(
            ref PhysicsWorld world,
            Joint prototype,
            in CaseExecutionSpec execution)
        {
            CaseExecutionRagdoll fixture = execution.Ragdoll;
            NativeArray<Joint> joints = world.Joints;
            int jointIndex = 0;
            int ragdollCount = (int)(fixture.RagdollGrid.x * fixture.RagdollGrid.y);
            for (int ragdollIndex = 0; ragdollIndex < ragdollCount; ++ragdollIndex)
            {
                for (int linkIndex = 0; linkIndex < fixture.Links.Length; ++linkIndex)
                {
                    CaseExecutionRagdollLink link = fixture.Links[linkIndex];
                    Joint joint = prototype;
                    joint.BodyPair = new BodyIndexPair
                    {
                        BodyIndexA = ragdollIndex * fixture.Parts.Length +
                            link.ParentPart,
                        BodyIndexB = ragdollIndex * fixture.Parts.Length +
                            link.ChildPart
                    };
                    joint.AFromJoint = new PhysicsMTransform(
                        quaternion.identity,
                        link.ParentLocalAnchor);
                    joint.BFromJoint = new PhysicsMTransform(
                        quaternion.identity,
                        link.ChildLocalAnchor);
                    joint.Entity = Entity.Null;
                    joint.EnableCollision =
                        fixture.LinkedCollisionMode == CaseExecutionToggle.Enabled;
                    joints[jointIndex++] = joint;
                }
            }
        }

        public static int CreateUnityPhysicsRagdollState(
            in CaseExecutionSpec execution,
            out UnityPhysicsRagdollState state)
        {
            state = default;
            if (execution.FixtureKind != CaseFixtureKind.RagdollStairTumble ||
                execution.SleepMode != CaseExecutionToggle.Disabled ||
                execution.ContinuousCollisionMode != CaseExecutionToggle.Disabled)
            {
                return 2;
            }
            if (CreateUnityPhysicsRagdollJointPrototype(out Joint prototype) != 0)
            {
                return 2;
            }
            try
            {
                CaseExecutionRagdoll fixture = execution.Ragdoll;
                state.Execution = execution;
                state.World = new PhysicsWorld(
                    (int)execution.StaticBodyCount,
                    (int)execution.DynamicBodyCount,
                    (int)execution.ConstraintCount);
                state.Colliders =
                    new NativeArray<BlobAssetReference<PhysicsCollider>>(
                        fixture.Parts.Length + 1 + fixture.ExtraStaticBoxes.Length,
                        Allocator.Persistent);
                CreateUnityPhysicsRagdollColliders(in execution, ref state.Colliders);
                AddUnityPhysicsRagdollStaticBodies(
                    ref state.World, state.Colliders, in execution);
                AddUnityPhysicsRagdollDynamicBodies(
                    ref state.World, state.Colliders, in execution);
                AddUnityPhysicsRagdollJoints(
                    ref state.World, prototype, in execution);
                state.World.UpdateIndexMaps();
                state.StaticBodiesChanged =
                    new NativeReference<int>(Allocator.Persistent);
                state.StaticBodiesChanged.Value = 1;
                state.Simulation = Simulation.Create();
                if (state.World.NumBodies == execution.BodyCount &&
                    state.World.NumJoints == execution.ConstraintCount)
                {
                    return 0;
                }
                DisposeUnityPhysicsRagdollState(ref state);
                return 2;
            }
            catch
            {
                DisposeUnityPhysicsRagdollState(ref state);
                throw;
            }
        }

        public static void StepUnityPhysicsRagdoll(
            ref UnityPhysicsRagdollState state, int multiThreaded, CaseExecutionToggle solverStabilization)
        {
            PhysicsStep physicsStep = PhysicsStep.Default;
            physicsStep.Gravity = state.Execution.Gravity;
            physicsStep.SynchronizeCollisionWorld = 1;
            physicsStep.SolverStabilizationHeuristicSettings.EnableSolverStabilization = solverStabilization == CaseExecutionToggle.Enabled;
            float timestep = 1.0f / state.Execution.TimestepHz;
            SimulationStepInput input = new SimulationStepInput
            {
                World = state.World,
                TimeStep = timestep,
                Gravity = physicsStep.Gravity,
                EnableGyroscopicTorque = physicsStep.EnableGyroscopicTorque,
                NumSubsteps = (int)state.Execution.Substeps,
                NumSolverIterations = (int)state.Execution.SolverIterations,
                DirectSolverSettings = physicsStep.DirectSolverSettings,
                MaxDynamicDepenetrationVelocity = physicsStep.MaxDynamicDepenetrationVelocity,
                MaxStaticDepenetrationVelocity = physicsStep.MaxStaticDepenetrationVelocity,
                SynchronizeCollisionWorld = true,
                SolverStabilizationHeuristicSettings =
                    physicsStep.SolverStabilizationHeuristicSettings,
                HaveStaticBodiesChanged = state.StaticBodiesChanged.AsReadOnly()
            };
            JobHandle buildBroadphaseHandle =
                state.World.CollisionWorld.ScheduleBuildBroadphaseJobs(
                    ref state.World,
                    timestep,
                    physicsStep.Gravity,
                    state.StaticBodiesChanged.AsReadOnly(),
                    default,
                    multiThreaded != 0);
            JobHandle inputDependencies = JobHandle.CombineDependencies(
                state.Simulation.FinalJobHandle, buildBroadphaseHandle);
            SimulationJobHandles broadphaseHandles =
                state.Simulation.ScheduleBroadphaseJobs(
                    input, inputDependencies, multiThreaded != 0);
            SimulationJobHandles narrowphaseHandles =
                state.Simulation.ScheduleNarrowphaseJobs(
                    input, broadphaseHandles.FinalExecutionHandle, multiThreaded != 0);
            SimulationJobHandles jacobianHandles =
                state.Simulation.ScheduleCreateJacobiansJobs(
                    input, narrowphaseHandles.FinalExecutionHandle, multiThreaded != 0);
            SimulationJobHandles solveHandles =
                state.Simulation.ScheduleSolveAndIntegrateJobs(
                    input, jacobianHandles.FinalExecutionHandle, multiThreaded != 0);
            JobHandle.CombineDependencies(
                solveHandles.FinalExecutionHandle,
                solveHandles.FinalDisposeHandle).Complete();
            state.StaticBodiesChanged.Value = 0;
        }

        public static int RunUnityPhysicsRagdollWarmup(
            ref UnityPhysicsRagdollState state,
            int warmupSteps,
            int multiThreaded, CaseExecutionToggle solverStabilization)
        {
            if (warmupSteps != state.Execution.WarmupWorkUnitCount)
            {
                return 2;
            }
            for (int step = 0; step < warmupSteps; ++step)
            {
                StepUnityPhysicsRagdoll(ref state, multiThreaded, solverStabilization);
            }
            return 0;
        }

        public static int SampleUnityPhysicsRagdollTransforms(
            ref UnityPhysicsRagdollState state,
            UnityPhysicsVisualStableTransform[] transforms)
        {
            if (transforms == null ||
                transforms.Length < state.Execution.DynamicBodyCount)
            {
                return 2;
            }
            for (int index = 0; index < state.Execution.DynamicBodyCount; ++index)
            {
                MotionData motionData = state.World.MotionDatas[index];
                RigidTransform transform = math.mul(
                    motionData.WorldFromMotion,
                    math.inverse(motionData.BodyFromMotion));
                transforms[index] = new UnityPhysicsVisualStableTransform
                {
                    StableSlot = (uint)index,
                    Transform = new UnityPhysicsTransform
                    {
                        PositionX = transform.pos.x,
                        PositionY = transform.pos.y,
                        PositionZ = transform.pos.z,
                        RotationX = transform.rot.value.x,
                        RotationY = transform.rot.value.y,
                        RotationZ = transform.rot.value.z,
                        RotationW = transform.rot.value.w
                    }
                };
            }
            return 0;
        }

        public static int CountUnityPhysicsRagdollInvalidTransforms(
            ref UnityPhysicsRagdollState state)
        {
            int invalidCount = 0;
            for (int index = 0; index < state.Execution.DynamicBodyCount; ++index)
            {
                if (index >= state.World.NumDynamicBodies)
                {
                    ++invalidCount;
                    continue;
                }
                MotionData motionData = state.World.MotionDatas[index];
                RigidTransform transform = math.mul(
                    motionData.WorldFromMotion,
                    math.inverse(motionData.BodyFromMotion));
                if (!math.all(math.isfinite(transform.pos)) ||
                    !math.all(math.isfinite(transform.rot.value)))
                {
                    ++invalidCount;
                }
            }
            return invalidCount;
        }

        public static void DisposeUnityPhysicsRagdollState(
            ref UnityPhysicsRagdollState state)
        {
            if (state.StaticBodiesChanged.IsCreated)
            {
                // BLOB_DISPOSE_OWNER: ragdoll case owns this native change flag
                state.StaticBodiesChanged.Dispose();
            }
            // BLOB_DISPOSE_OWNER: ragdoll case owns the direct simulation
            state.Simulation.Dispose();
            // BLOB_DISPOSE_OWNER: ragdoll case owns the direct physics world
            state.World.Dispose();
            if (state.Colliders.IsCreated)
            {
                for (int index = state.Colliders.Length - 1; index >= 0; --index)
                {
                    if (state.Colliders[index].IsCreated)
                    {
                        // BLOB_DISPOSE_OWNER: ragdoll case created every collider blob
                        state.Colliders[index].Dispose();
                    }
                }
                // BLOB_DISPOSE_OWNER: ragdoll case owns the collider reference array
                state.Colliders.Dispose();
            }
            state = default;
        }

        public static int UnityPhysicsRagdollSameGeometry(
            in CaseExecutionRagdollPart left,
            in CaseExecutionRagdollPart right)
        {
            if (left.Shape != right.Shape || left.Axis != right.Axis || left.HalfSegment != right.HalfSegment)
            {
                return 0;
            }
            return left.Shape == CaseExecutionShape.Sphere || left.Shape == CaseExecutionShape.Capsule ?
                (left.Radius == right.Radius ? 1 : 0) :
                (math.all(left.HalfExtents == right.HalfExtents) ? 1 : 0);
        }

        public static uint UnityPhysicsRagdollGeometryIndex(
            in CaseExecutionRagdoll fixture,
            int partIndex)
        {
            uint geometryIndex = 0;
            for (int sourceIndex = 0;
                sourceIndex < fixture.Parts.Length;
                ++sourceIndex)
            {
                CaseExecutionRagdollPart sourcePart = fixture.Parts[sourceIndex];
                int duplicate = 0;
                for (int previousIndex = 0;
                    previousIndex < sourceIndex;
                    ++previousIndex)
                {
                    CaseExecutionRagdollPart previousPart =
                        fixture.Parts[previousIndex];
                    if (UnityPhysicsRagdollSameGeometry(
                            in sourcePart, in previousPart) != 0)
                    {
                        duplicate = 1;
                        break;
                    }
                }
                if (duplicate != 0)
                {
                    continue;
                }
                CaseExecutionRagdollPart requestedPart = fixture.Parts[partIndex];
                if (UnityPhysicsRagdollSameGeometry(
                        in requestedPart, in sourcePart) != 0)
                {
                    return geometryIndex;
                }
                ++geometryIndex;
            }
            throw new ArgumentOutOfRangeException(nameof(partIndex));
        }

        public static int UnityPhysicsRagdollPartGeometryCount(
            in CaseExecutionRagdoll fixture)
        {
            int geometryCount = 0;
            for (int partIndex = 0; partIndex < fixture.Parts.Length; ++partIndex)
            {
                if (UnityPhysicsRagdollGeometryIndex(in fixture, partIndex) ==
                    geometryCount)
                {
                    ++geometryCount;
                }
            }
            return geometryCount;
        }

        public static int BuildUnityPhysicsRagdollVisualScene(
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
            CaseExecutionRagdoll fixture = execution.Ragdoll;
            int partGeometryCount = UnityPhysicsRagdollPartGeometryCount(in fixture);
            int requiredGeometryCount = partGeometryCount + 2;
            if (fixture.ExtraStaticBoxes.Length == 0 ||
                geometries.Length < requiredGeometryCount ||
                instances.Length < execution.VisualInstanceCount)
            {
                return 2;
            }
            int nextGeometry = 0;
            for (int partIndex = 0; partIndex < fixture.Parts.Length; ++partIndex)
            {
                uint geometryIndex = UnityPhysicsRagdollGeometryIndex(
                    in fixture, partIndex);
                if (geometryIndex != nextGeometry)
                {
                    continue;
                }
                CaseExecutionRagdollPart part = fixture.Parts[partIndex];
                CaseExecutionGeometry geometry = UnityPhysicsCaseExecutionWire.PartGeometry(in part);
                if (BuildResolvedVisualGeometry(in execution, in geometry, ref meshes,
                    out geometries[nextGeometry++]) != 0) return 2;
            }
            uint stairGeometryIndex = (uint)nextGeometry;
            geometries[nextGeometry++] = new UnityPhysicsVisualGeometry
            {
                Kind = 2u,
                ParameterX = fixture.StairHalfWidth,
                ParameterY = fixture.StairHalfHeight,
                ParameterZ = fixture.StairHalfDepth
            };
            uint runoutGeometryIndex = (uint)nextGeometry;
            CaseExecutionBox runout = fixture.ExtraStaticBoxes[0];
            geometries[nextGeometry++] = new UnityPhysicsVisualGeometry
            {
                Kind = 2u,
                ParameterX = runout.HalfExtents.x,
                ParameterY = runout.HalfExtents.y,
                ParameterZ = runout.HalfExtents.z
            };

            for (int index = 0; index < execution.DynamicBodyCount; ++index)
            {
                MotionData motionData = state.World.MotionDatas[index];
                RigidTransform transform = math.mul(
                    motionData.WorldFromMotion,
                    math.inverse(motionData.BodyFromMotion));
                instances[index] = new UnityPhysicsVisualInstance
                {
                    GeometryIndex = UnityPhysicsRagdollGeometryIndex(
                        in fixture, index % fixture.Parts.Length),
                    StableSlot = (uint)index,
                    TransformSlot = (uint)index,
                    InitialTransform = new UnityPhysicsTransform
                    {
                        PositionX = transform.pos.x,
                        PositionY = transform.pos.y,
                        PositionZ = transform.pos.z,
                        RotationX = transform.rot.value.x,
                        RotationY = transform.rot.value.y,
                        RotationZ = transform.rot.value.z,
                        RotationW = transform.rot.value.w
                    }
                };
            }
            int stairCount = (int)fixture.StairCount;
            for (int row = 0; row < stairCount; ++row)
            {
                int stableSlot = (int)execution.DynamicBodyCount + row;
                instances[stableSlot] = new UnityPhysicsVisualInstance
                {
                    GeometryIndex = stairGeometryIndex,
                    StableSlot = (uint)stableSlot,
                    TransformSlot = uint.MaxValue,
                    InitialTransform = new UnityPhysicsTransform
                    {
                        PositionY =
                            (stairCount - 1 - row) * fixture.StairRise -
                            fixture.StairHalfHeight,
                        PositionZ =
                            (row - (stairCount - 1) * 0.5f) * fixture.StairDepth,
                        RotationW = 1.0f
                    }
                };
            }
            int runoutSlot = (int)execution.VisualInstanceCount - 1;
            instances[runoutSlot] =
                new UnityPhysicsVisualInstance
                {
                    GeometryIndex = runoutGeometryIndex,
                    StableSlot = (uint)runoutSlot,
                    TransformSlot = uint.MaxValue,
                    InitialTransform = new UnityPhysicsTransform
                    {
                        PositionX = runout.Center.x,
                        PositionY = runout.Center.y,
                        PositionZ = runout.Center.z,
                        RotationW = 1.0f
                    }
                };
            geometryCount = nextGeometry;
            instanceCount = (int)execution.VisualInstanceCount;
            return 0;
        }

        public static int BuildUnityPhysicsRagdollVisualDebugPrimitives(
            ref UnityPhysicsCaseView state,
            UnityPhysicsVisualDebugPrimitive[] primitives)
        {
            if (primitives == null)
            {
                return 2;
            }
            CaseExecutionSpec execution = state.Execution;
            CaseExecutionRagdoll fixture = execution.Ragdoll;
            int debugCount = (int)execution.VisualDebugPrimitiveCount;
            int firstDebugBox = fixture.ExtraStaticBoxes.Length - debugCount;
            if (debugCount > fixture.ExtraStaticBoxes.Length ||
                primitives.Length < debugCount)
            {
                return 2;
            }
            for (int index = 0; index < debugCount; ++index)
            {
                CaseExecutionBox box =
                    fixture.ExtraStaticBoxes[firstDebugBox + index];
                primitives[index] = new UnityPhysicsVisualDebugPrimitive
                {
                    Kind = 2u,
                    MaterialIndex = 6u,
                    OriginOrCenterX = box.Center.x,
                    OriginOrCenterY = box.Center.y,
                    OriginOrCenterZ = box.Center.z,
                    EndOrHalfExtentsX = box.HalfExtents.x,
                    EndOrHalfExtentsY = box.HalfExtents.y,
                    EndOrHalfExtentsZ = box.HalfExtents.z
                };
            }
            return 0;
        }

        public static string UnityPhysicsRagdollVisualSettings(
            in CaseExecutionSpec execution, int workerCount, CaseExecutionToggle solverStabilization)
        {
            return FormattableString.Invariant(
                $"solver_iterations={execution.SolverIterations}; substeps={execution.Substeps}; solver_type=iterative; stabilization={(solverStabilization == CaseExecutionToggle.Enabled ? "on" : "off")}; synchronize_collision_world=on; sleep=not_applicable; linked_collision={(execution.Ragdoll.LinkedCollisionMode == CaseExecutionToggle.Enabled ? "enabled" : "disabled")}; worker_count={workerCount}");
        }

        public static int RunUnityPhysicsRagdollStairTumble(RunnerArgs runnerArgs)
        {
            if (runnerArgs.WarmupSteps !=
                    runnerArgs.CaseExecution.WarmupWorkUnitCount ||
                runnerArgs.StepCount !=
                    runnerArgs.CaseExecution.MeasuredWorkUnitCount ||
                runnerArgs.CaseExecution.FixtureKind !=
                    CaseFixtureKind.RagdollStairTumble ||
                runnerArgs.CaseExecution.SleepMode != CaseExecutionToggle.Disabled ||
                runnerArgs.CaseExecution.ContinuousCollisionMode !=
                    CaseExecutionToggle.Disabled)
            {
                return 2;
            }
            int previousWorkerCount = JobsUtility.JobWorkerCount;
            UnityPhysicsRagdollState state = default;
            UnityPhysicsRecordingWriter recording = default;
            try
            {
                int requestedWorkerCount = math.max(0, runnerArgs.ThreadCount - 1);
                JobsUtility.JobWorkerCount = requestedWorkerCount;
                int effectiveWorkerCount = JobsUtility.JobWorkerCount;
                int effectiveThreadCount = effectiveWorkerCount + 1;
                if (effectiveThreadCount != runnerArgs.ThreadCount ||
                    CreateUnityPhysicsRagdollState(
                        in runnerArgs.CaseExecution, out state) != 0 ||
                    RunUnityPhysicsRagdollWarmup(
                        ref state,
                        runnerArgs.WarmupSteps,
                        runnerArgs.ThreadCount > 1 ? 1 : 0, runnerArgs.SolverStabilization) != 0)
                {
                    return 2;
                }
                if (runnerArgs.WarmupSteps != 0)
                {
                    DisposeUnityPhysicsRagdollState(ref state);
                    if (CreateUnityPhysicsRagdollState(
                        in runnerArgs.CaseExecution, out state) != 0)
                    {
                        return 2;
                    }
                }
                UnityPhysicsCaseView capture = new UnityPhysicsCaseView
                {
                    Execution = runnerArgs.CaseExecution,
                    World = state.World
                };
                if (runnerArgs.RecordingMode == RecordingMode.On && UnityPhysicsRecording.Begin(in runnerArgs, ref capture, out recording) != 0) return 2;
                for (int step = 0; step < runnerArgs.StepCount; ++step)
                {
                    StepUnityPhysicsRagdoll(ref state, runnerArgs.ThreadCount > 1 ? 1 : 0, runnerArgs.SolverStabilization);
                    if (runnerArgs.VerificationMode == VerificationMode.On) ObserveRagdollQuality(ref state);
                    if (runnerArgs.RecordingMode == RecordingMode.On && UnityPhysicsRecording.Append(ref recording, in runnerArgs.CaseRegistration,
                        ref capture, (ulong)step + 1) != 0) return 2;
                }
                if (runnerArgs.RecordingMode == RecordingMode.On && UnityPhysicsRecording.Complete(ref recording) != 0) return 2;
                if (runnerArgs.VerificationMode == VerificationMode.On && state.Quality.JointSamples == 0) return 2;
                UnityPhysicsObservationRow[] observations = runnerArgs.VerificationMode == VerificationMode.On ? new UnityPhysicsObservationRow[10] : Array.Empty<UnityPhysicsObservationRow>();
                for (int index = 0; index < observations.Length; ++index)
                {
                    observations[index] = new UnityPhysicsObservationRow
                    {
                        MetricId = RagdollQualityIds[index], PhaseId = "final", SampleIndex = state.Quality.CompletedSteps,
                        ValueType = index < 2 ? UnityPhysicsObservationValueType.Float64 : UnityPhysicsObservationValueType.Uint64,
                        ValueBits = RagdollQualityValue(in state.Quality, index)
                    };
                }
                int invalidTransformCount =
                    CountUnityPhysicsRagdollInvalidTransforms(ref state);
                UnityPhysicsBenchmarkResult result = new UnityPhysicsBenchmarkResult
                {
                    PhysicsSettings =
                        UnityPhysicsRagdollVisualSettings(
                            in runnerArgs.CaseExecution, effectiveWorkerCount, runnerArgs.SolverStabilization),
                    BodyCount = (int)runnerArgs.CaseExecution.BodyCount,
                    ShapeCount = (int)runnerArgs.CaseExecution.ShapeCount,
                    QueryCount = (int)runnerArgs.CaseExecution.QueryCount,
                    ConstraintCount =
                        (int)runnerArgs.CaseExecution.ConstraintCount,
                    InvalidTransformCount = (uint)invalidTransformCount,
                    EffectiveThreadCount = effectiveThreadCount,
                    EffectiveWorkerCount = effectiveWorkerCount,
                    CompletedWorkUnitCount = runnerArgs.StepCount,
                    WorkloadElapsedMilliseconds = double.NaN,
                    CaseValidity = UnityPhysicsResultValidity.Valid,
                    MetricValidity = runnerArgs.StepCount == runnerArgs.CaseExecution.MeasuredWorkUnitCount ?
                        UnityPhysicsResultValidity.Valid : UnityPhysicsResultValidity.Invalid,
                    Observations = observations
                };
                int status = WriteResult(runnerArgs, in result);
                return status;
            }
            finally
            {
                if (runnerArgs.RecordingMode == RecordingMode.On) UnityPhysicsRecording.Abort(ref recording);
                JobsUtility.JobWorkerCount = previousWorkerCount;
                DisposeUnityPhysicsRagdollState(ref state);
            }
        }
    }
}
