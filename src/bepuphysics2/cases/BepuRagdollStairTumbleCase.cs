using BepuPhysics;
using BepuPhysics.Collidables;
using BepuPhysics.CollisionDetection;
using BepuPhysics.Constraints;
using BepuUtilities;
using BepuUtilities.Memory;
using System.Numerics;

namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public struct RagdollCollisionMembership
{
    public int Group;
    public uint Part;
    public uint AllowedParts;
}

public unsafe struct RagdollNarrowPhaseCallbacks : INarrowPhaseCallbacks
{
    public CollidableProperty<RagdollCollisionMembership> Membership;
    public float Friction;

    public void Initialize(Simulation simulation)
    {
        Membership.Initialize(simulation);
    }

    public bool AllowContactGeneration(int workerIndex, CollidableReference a,
        CollidableReference b, ref float speculativeMargin)
    {
        return BepuRagdollStairTumbleCase.ContactEligibility(Membership, a, b) != 0;
    }

    public bool AllowContactGeneration(int workerIndex, CollidablePair pair,
        int childIndexA, int childIndexB)
    {
        return true;
    }

    public bool ConfigureContactManifold<TManifold>(int workerIndex, CollidablePair pair,
        ref TManifold manifold, out PairMaterialProperties pairMaterial)
        where TManifold : unmanaged, IContactManifold<TManifold>
    {
        pairMaterial.FrictionCoefficient = Friction;
        pairMaterial.MaximumRecoveryVelocity = 2f;
        pairMaterial.SpringSettings = new SpringSettings(30, 1);
        return true;
    }

    public bool ConfigureContactManifold(int workerIndex, CollidablePair pair,
        int childIndexA, int childIndexB, ref ConvexContactManifold manifold)
    {
        return true;
    }

    public void Dispose()
    {
        Membership.Dispose();
    }
}

public struct BepuRagdollQuality
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

public static class BepuRagdollStairTumbleCase
{
    public static int ContactEligibility(CollidableProperty<RagdollCollisionMembership> membership,
        CollidableReference a, CollidableReference b)
    {
        if (a.Mobility != CollidableMobility.Dynamic && b.Mobility != CollidableMobility.Dynamic)
        {
            return 0;
        }
        if (a.Mobility == CollidableMobility.Static || b.Mobility == CollidableMobility.Static)
        {
            return 1;
        }
        ref RagdollCollisionMembership first = ref membership[a.BodyHandle];
        ref RagdollCollisionMembership second = ref membership[b.BodyHandle];
        return first.Group != second.Group ||
            ((first.AllowedParts & second.Part) != 0 && (second.AllowedParts & first.Part) != 0) ? 1 : 0;
    }

    public static readonly string[] QualityIds =
    {
        "joint_anchor_gap_rms_m", "joint_anchor_gap_max_m", "worst_joint_id",
        "worst_joint_step", "joint_sample_count", "body_sample_count",
        "invalid_body_sample_count", "missing_body_sample_count",
        "first_invalid_body_id", "first_invalid_step"
    };

    public static int QualityPoseValid(in RigidPose pose)
    {
        return float.IsFinite(pose.Position.X) && float.IsFinite(pose.Position.Y) &&
            float.IsFinite(pose.Position.Z) && float.IsFinite(pose.Orientation.X) &&
            float.IsFinite(pose.Orientation.Y) && float.IsFinite(pose.Orientation.Z) &&
            float.IsFinite(pose.Orientation.W) && pose.Orientation != default(Quaternion) ? 1 : 0;
    }

    public static void QualityAnchor(in RigidPose pose, in Vector3 local,
        out double x, out double y, out double z)
    {
        Quaternion q = pose.Orientation;
        double tx = 2.0 * (q.Y * (double)local.Z - q.Z * (double)local.Y);
        double ty = 2.0 * (q.Z * (double)local.X - q.X * (double)local.Z);
        double tz = 2.0 * (q.X * (double)local.Y - q.Y * (double)local.X);
        x = pose.Position.X + (double)local.X + q.W * tx + q.Y * tz - q.Z * ty;
        y = pose.Position.Y + (double)local.Y + q.W * ty + q.Z * tx - q.X * tz;
        z = pose.Position.Z + (double)local.Z + q.W * tz + q.X * ty - q.Y * tx;
    }

    public static void ObserveQuality(Simulation simulation, BodyHandle[] bodies, ConstraintHandle[] joints,
        in CaseExecutionSpec execution, ref BepuRagdollQuality quality)
    {
        ++quality.CompletedSteps;
        for (int index = 0; index < execution.DynamicBodyCount; ++index)
        {
            if (simulation.Bodies.BodyExists(bodies[index]))
            {
                RigidPose pose = simulation.Bodies.GetBodyReference(bodies[index]).Pose;
                ++quality.BodySamples;
                if (QualityPoseValid(in pose) != 0) continue;
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
        for (int ragdoll = 0; ragdoll < fixture.RagdollGrid[0] * fixture.RagdollGrid[1]; ++ragdoll)
        {
            for (int joint = 0; joint < fixture.LinkCount; ++joint)
            {
                CaseExecutionRagdollLink link = fixture.Links[joint];
                if (!simulation.Bodies.BodyExists(bodies[ragdoll * fixture.PartCount + link.ParentPart]) ||
                    !simulation.Bodies.BodyExists(bodies[ragdoll * fixture.PartCount + link.ChildPart])) continue;
                RigidPose parent = simulation.Bodies.GetBodyReference(
                    bodies[ragdoll * fixture.PartCount + link.ParentPart]).Pose;
                RigidPose child = simulation.Bodies.GetBodyReference(
                    bodies[ragdoll * fixture.PartCount + link.ChildPart]).Pose;
                if (QualityPoseValid(in parent) == 0 || QualityPoseValid(in child) == 0) continue;
                simulation.Solver.GetDescription(joints[ragdoll * fixture.LinkCount + joint], out BallSocket constraint);
                QualityAnchor(in parent, in constraint.LocalOffsetA, out double ax, out double ay, out double az);
                QualityAnchor(in child, in constraint.LocalOffsetB, out double bx, out double by, out double bz);
                double square = (ax - bx) * (ax - bx) + (ay - by) * (ay - by) + (az - bz) * (az - bz);
                double gap = Math.Sqrt(square);
                quality.SquaredGapSum += square;
                ++quality.JointSamples;
                if (quality.WorstStep == 0 || gap > quality.MaximumGap)
                {
                    quality.MaximumGap = gap;
                    quality.WorstJoint = (ulong)(ragdoll * fixture.LinkCount + joint);
                    quality.WorstStep = quality.CompletedSteps;
                }
            }
        }
    }

    public static ulong QualityValue(in BepuRagdollQuality quality, int ordinal)
    {
        return ordinal switch
        {
            0 => BitConverter.DoubleToUInt64Bits(Math.Sqrt(quality.SquaredGapSum / quality.JointSamples)),
            1 => BitConverter.DoubleToUInt64Bits(quality.MaximumGap),
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

    public static Quaternion Rotation(in CaseExecutionRagdoll fixture, int ragdollIndex)
    {
        float yawRadians = fixture.YawPatternDegrees[ragdollIndex % fixture.YawPatternCount] *
            MathF.PI / 180f;
        float pitchRadians = fixture.PitchDegrees * MathF.PI / 180f;
        return Quaternion.Normalize(
            Quaternion.Multiply(
                Quaternion.CreateFromAxisAngle(Vector3.UnitY, yawRadians),
                Quaternion.CreateFromAxisAngle(Vector3.UnitX, pitchRadians)));
    }

    public static Vector3 Base(in CaseExecutionRagdoll fixture, int row, int column)
    {
        return new Vector3(
            (column - 0.5f * (fixture.RagdollGrid[1] - 1)) * fixture.ColumnSpacing,
            (fixture.StairCount - 1 - row) * fixture.StairRise + fixture.BaseHeightOffset,
            (row - 0.5f * (fixture.StairCount - 1)) * fixture.RowSpacing);
    }

    public static Simulation CreateSimulation(
        BufferPool bufferPool,
        int workerCount,
        in CaseExecutionSpec execution,
        BodyHandle[] dynamicBodies,
        ConstraintHandle[] jointHandles,
        out ThreadDispatcher threadDispatcher)
    {
        threadDispatcher = null;
        CaseExecutionRagdoll fixture = execution.Ragdoll;
        CollidableProperty<RagdollCollisionMembership> membership = new(bufferPool);
        Simulation simulation = Simulation.Create(
            bufferPool,
            new RagdollNarrowPhaseCallbacks { Membership = membership, Friction = execution.Friction },
            new PolygonPoseIntegratorCallbacks(
                new Vector3(execution.Gravity.X, execution.Gravity.Y, execution.Gravity.Z),
                fixture.LinearDamping, fixture.AngularDamping),
            new SolveDescription((int)execution.VelocityIterations, (int)execution.Substeps));
        int bodyIndex = 0;
        int jointIndex = 0;
        try
        {
            simulation.Deterministic = true;

            TypedIndex stepShape = simulation.Shapes.Add(new Box(
                fixture.StairHalfWidth * 2f,
                fixture.StairHalfHeight * 2f,
                fixture.StairHalfDepth * 2f));
            for (int row = 0; row < fixture.StairCount; ++row)
            {
                simulation.Statics.Add(new StaticDescription(
                    new Vector3(
                        0f,
                        (fixture.StairCount - 1 - row) * fixture.StairRise -
                            fixture.StairHalfHeight,
                        (row - 0.5f * (fixture.StairCount - 1)) * fixture.StairDepth),
                    stepShape));
            }
            for (int index = 0; index < fixture.ExtraStaticBoxCount; ++index)
            {
                CaseExecutionBox staticBox = fixture.ExtraStaticBoxes[index];
                TypedIndex shape = simulation.Shapes.Add(new Box(
                    staticBox.HalfExtents.X * 2f,
                    staticBox.HalfExtents.Y * 2f,
                    staticBox.HalfExtents.Z * 2f));
                simulation.Statics.Add(new StaticDescription(
                    new Vector3(staticBox.Center.X, staticBox.Center.Y, staticBox.Center.Z),
                    shape));
            }

            BepuResolvedShape[] partShapes = new BepuResolvedShape[fixture.PartCount];
            BodyInertia[] partInertias = new BodyInertia[fixture.PartCount];
            for (int partIndex = 0; partIndex < fixture.PartCount; ++partIndex)
            {
                CaseExecutionRagdollPart part = fixture.Parts[partIndex];
                int sharedIndex = -1;
                for (int previous = 0; previous < partIndex; ++previous)
                {
                    CaseExecutionRagdollPart previousPart = fixture.Parts[previous];
                    if (SameGeometry(in part, in previousPart) == GeometryMatch.Equal)
                    {
                        sharedIndex = previous;
                        break;
                    }
                }
                if (sharedIndex >= 0)
                {
                    partShapes[partIndex] = partShapes[sharedIndex];
                    partInertias[partIndex] = partInertias[sharedIndex];
                    continue;
                }
                CaseExecutionGeometry geometry = CaseExecutionWire.PartGeometry(in part);
                partShapes[partIndex] = BepuCaseRegistry.AddResolvedShape(simulation, bufferPool,
                    in execution, in geometry);
                partInertias[partIndex] = BepuCaseRegistry.ShapeInertia(simulation,
                    partShapes[partIndex].Index, fixture.PartMass);
            }

            BodyActivityDescription activity = execution.SleepMode == CaseExecutionToggle.Enabled
                ? new BodyActivityDescription(0.01f) : new BodyActivityDescription(-1f);
            uint[] allowedParts = new uint[fixture.PartCount];
            Array.Fill(allowedParts, uint.MaxValue);
            if (fixture.LinkedCollisionMode == CaseExecutionToggle.Disabled)
            {
                for (int linkIndex = 0; linkIndex < fixture.LinkCount; ++linkIndex)
                {
                    CaseExecutionRagdollLink link = fixture.Links[linkIndex];
                    allowedParts[link.ParentPart] &= ~(1u << link.ChildPart);
                    allowedParts[link.ChildPart] &= ~(1u << link.ParentPart);
                }
            }
            for (int row = 0; row < fixture.RagdollGrid[0]; ++row)
            {
                for (int column = 0; column < fixture.RagdollGrid[1]; ++column)
                {
                    int ragdollIndex = row * (int)fixture.RagdollGrid[1] + column;
                    Quaternion rotation = Rotation(in fixture, ragdollIndex);
                    Vector3 translation = Base(in fixture, row, column);
                    for (int partIndex = 0; partIndex < fixture.PartCount; ++partIndex)
                    {
                        CaseExecutionRagdollPart part = fixture.Parts[partIndex];
                        Vector3 localCenter =
                            new(part.Center.X, part.Center.Y, part.Center.Z);
                        Quaternion bodyRotation = rotation * BepuCaseRegistry.ShapeRotation(part.Axis);
                        RigidPose pose = new(
                            translation + Vector3.Transform(localCenter, rotation) +
                                Vector3.Transform(partShapes[partIndex].Center, bodyRotation),
                            bodyRotation);
                        BodyVelocity velocity =
                            new(
                                new Vector3(
                                    0f,
                                    0f,
                                    row == 0 ? fixture.TriggerRowSpeed : fixture.FollowerRowSpeed),
                                Vector3.Zero);
                        BodyDescription description = BodyDescription.CreateDynamic(
                            pose, velocity, partInertias[partIndex], partShapes[partIndex].Index, activity);
                        if (execution.ContinuousCollisionMode == CaseExecutionToggle.Enabled)
                            description.Collidable.Continuity =
                                ContinuousDetection.Continuous(1e-3f, 1e-3f);
                        BodyHandle handle = simulation.Bodies.Add(description);
                        dynamicBodies[bodyIndex++] = handle;
                        membership.Allocate(handle) = new RagdollCollisionMembership
                        {
                            Group = ragdollIndex,
                            Part = 1u << partIndex,
                            AllowedParts = allowedParts[partIndex]
                        };
                    }
                }
            }

            int ragdollCount = (int)(fixture.RagdollGrid[0] * fixture.RagdollGrid[1]);
            for (int ragdollIndex = 0; ragdollIndex < ragdollCount; ++ragdollIndex)
            {
                for (int linkIndex = 0; linkIndex < fixture.LinkCount; ++linkIndex)
                {
                    CaseExecutionRagdollLink link = fixture.Links[linkIndex];
                    BallSocket ballSocket = new()
                    {
                        LocalOffsetA = new Vector3(
                            link.ParentLocalAnchor.X,
                            link.ParentLocalAnchor.Y,
                            link.ParentLocalAnchor.Z) - partShapes[link.ParentPart].Center,
                        LocalOffsetB = new Vector3(
                            link.ChildLocalAnchor.X,
                            link.ChildLocalAnchor.Y,
                            link.ChildLocalAnchor.Z) - partShapes[link.ChildPart].Center,
                        SpringSettings = new SpringSettings(30, 1)
                    };
                    jointHandles[jointIndex++] = simulation.Solver.Add(
                        dynamicBodies[ragdollIndex * fixture.PartCount + link.ParentPart],
                        dynamicBodies[ragdollIndex * fixture.PartCount + link.ChildPart],
                        ballSocket);
                }
            }

            int nativeConstraintCount = simulation.Solver.CountConstraints();
            if (simulation.Statics.Count != execution.StaticBodyCount ||
                simulation.Bodies.ActiveSet.Count != execution.DynamicBodyCount ||
                nativeConstraintCount != execution.ConstraintCount)
            {
                throw new InvalidOperationException(
                    $"Unexpected native counts: {simulation.Statics.Count}/" +
                    $"{simulation.Bodies.ActiveSet.Count}/{nativeConstraintCount}");
            }

            threadDispatcher =
                workerCount > 0 ? new ThreadDispatcher(workerCount) : null;
            return simulation;
        }
        catch
        {
            ReleaseCaseStorage(
                simulation,
                dynamicBodies,
                bodyIndex,
                jointHandles,
                jointIndex);
            simulation.Dispose();
            throw;
        }
    }

    public static void StepSimulation(
        Simulation simulation, ThreadDispatcher threadDispatcher, uint timestepHz, int stepCount)
    {
        float timestepDuration = 1f / timestepHz;
        if (threadDispatcher == null)
        {
            for (int step = 0; step < stepCount; ++step)
            {
                simulation.Timestep(timestepDuration);
            }
            return;
        }
        for (int step = 0; step < stepCount; ++step)
        {
            simulation.Timestep(timestepDuration, threadDispatcher);
        }
    }

    public static void ReleaseCaseStorage(
        Simulation simulation,
        BodyHandle[] dynamicBodies,
        int createdDynamicBodyCount,
        ConstraintHandle[] jointHandles,
        int createdJointCount)
    {
        if (simulation == null) return;
        if (jointHandles != null)
        {
            for (int index = createdJointCount - 1; index >= 0; --index)
            {
                simulation.Solver.Remove(jointHandles[index]);
            }
        }
        if (dynamicBodies != null)
        {
            for (int index = createdDynamicBodyCount - 1; index >= 0; --index)
            {
                simulation.Bodies.Remove(dynamicBodies[index]);
            }
        }
    }

    public static void RunWarmup(int workerCount, in CaseExecutionSpec execution, int stepCount)
    {
        BufferPool pool = new();
        Simulation simulation = default;
        ThreadDispatcher dispatcher = null;
        BodyHandle[] bodies = new BodyHandle[execution.DynamicBodyCount];
        ConstraintHandle[] joints = new ConstraintHandle[execution.ConstraintCount];
        try
        {
            simulation = CreateSimulation(
                pool, workerCount, in execution, bodies, joints, out dispatcher);
            StepSimulation(simulation, dispatcher, execution.TimestepHz, stepCount);
        }
        finally
        {
            ReleaseCaseStorage(
                simulation, bodies, (int)execution.DynamicBodyCount,
                joints, (int)execution.ConstraintCount);
            simulation?.Dispose();
            dispatcher?.Dispose();
            pool.Clear();
        }
    }

    public static int SampleVisualTransforms(
        BepuCaseView state, BepuVisualStableTransform[] transforms)
    {
        int dynamicBodyCount = (int)state.CaseExecution.DynamicBodyCount;
        if (transforms == null || transforms.Length < dynamicBodyCount) return 2;
        for (int index = 0; index < dynamicBodyCount; ++index)
        {
            RigidPose pose = state.Simulation.Bodies.GetBodyReference(
                state.DynamicBodies[index]).Pose;
            transforms[index] = new BepuVisualStableTransform
            {
                StableSlot = (uint)index,
                Transform = Transform(pose)
            };
        }
        return 0;
    }

    public static BepuVisualTransform Transform(RigidPose pose)
    {
        return new BepuVisualTransform
        {
            PositionX = pose.Position.X,
            PositionY = pose.Position.Y,
            PositionZ = pose.Position.Z,
            RotationX = pose.Orientation.X,
            RotationY = pose.Orientation.Y,
            RotationZ = pose.Orientation.Z,
            RotationW = pose.Orientation.W
        };
    }

    public enum GeometryMatch : byte
    {
        Different,
        Equal
    }

    public static GeometryMatch SameGeometry(
        in CaseExecutionRagdollPart left, in CaseExecutionRagdollPart right)
    {
        return left.Shape == right.Shape && left.Radius == right.Radius &&
            left.HalfSegment == right.HalfSegment && left.Axis == right.Axis &&
            left.HalfExtents.X == right.HalfExtents.X &&
            left.HalfExtents.Y == right.HalfExtents.Y &&
            left.HalfExtents.Z == right.HalfExtents.Z ? GeometryMatch.Equal : GeometryMatch.Different;
    }

    public static uint GeometryIndex(in CaseExecutionRagdoll fixture, int partIndex)
    {
        uint geometryIndex = 0;
        for (int index = 0; index < partIndex; ++index)
        {
            CaseExecutionRagdollPart current = fixture.Parts[index];
            CaseExecutionRagdollPart target = fixture.Parts[partIndex];
            if (SameGeometry(in current, in target) == GeometryMatch.Equal)
                return GeometryIndex(in fixture, index);
            int previousMatches = 0;
            for (int prior = 0; prior < index; ++prior)
            {
                CaseExecutionRagdollPart previous = fixture.Parts[prior];
                if (SameGeometry(in previous, in current) == GeometryMatch.Equal) ++previousMatches;
            }
            if (previousMatches == 0) ++geometryIndex;
        }
        return geometryIndex;
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
        CaseExecutionRagdoll fixture = execution.Ragdoll;
        if (geometries == null || instances == null) return 2;
        uint partGeometryCount = 0;
        for (int partIndex = 0; partIndex < fixture.PartCount; ++partIndex)
        {
            uint index = GeometryIndex(in fixture, partIndex);
            if (index < partGeometryCount) continue;
            if (index != partGeometryCount) return 2;
            ++partGeometryCount;
        }
        int totalGeometryCount = (int)partGeometryCount + 2;
        if (geometries.Length < totalGeometryCount ||
            instances.Length < execution.VisualInstanceCount) return 2;
        for (int partIndex = 0; partIndex < fixture.PartCount; ++partIndex)
        {
            CaseExecutionRagdollPart part = fixture.Parts[partIndex];
            int previousMatches = 0;
            for (int prior = 0; prior < partIndex; ++prior)
            {
                CaseExecutionRagdollPart previous = fixture.Parts[prior];
                if (SameGeometry(in previous, in part) == GeometryMatch.Equal) ++previousMatches;
            }
            if (previousMatches != 0) continue;
            uint index = GeometryIndex(in fixture, partIndex);
            CaseExecutionGeometry geometry = CaseExecutionWire.PartGeometry(in part);
            if (BepuCaseRegistry.BuildResolvedVisualGeometry(in execution, in geometry, ref meshes,
                out geometries[index]) != 0) return 2;
            BepuCaseRegistry.OffsetHullVisualGeometry(state.Simulation,
                state.Simulation.Bodies.GetBodyReference(state.DynamicBodies[partIndex]).Collidable.Shape,
                in geometry, ref meshes, in geometries[index]);
        }
        geometries[partGeometryCount] = new BepuVisualGeometry
        {
            Kind = 2,
            ParameterX = fixture.StairHalfWidth,
            ParameterY = fixture.StairHalfHeight,
            ParameterZ = fixture.StairHalfDepth
        };
        CaseExecutionBox floor = fixture.ExtraStaticBoxes[0];
        geometries[partGeometryCount + 1] = new BepuVisualGeometry
        {
            Kind = 2,
            ParameterX = floor.HalfExtents.X,
            ParameterY = floor.HalfExtents.Y,
            ParameterZ = floor.HalfExtents.Z
        };
        for (int index = 0; index < execution.DynamicBodyCount; ++index)
        {
            RigidPose pose = state.Simulation.Bodies.GetBodyReference(
                state.DynamicBodies[index]).Pose;
            instances[index] = new BepuVisualInstance
            {
                GeometryIndex = GeometryIndex(in fixture, index % fixture.PartCount),
                StableSlot = (uint)index,
                TransformSlot = (uint)index,
                InitialTransform = Transform(pose)
            };
        }
        for (int row = 0; row < fixture.StairCount; ++row)
        {
            int stableSlot = (int)execution.DynamicBodyCount + row;
            instances[stableSlot] = new BepuVisualInstance
            {
                GeometryIndex = partGeometryCount,
                StableSlot = (uint)stableSlot,
                TransformSlot = uint.MaxValue,
                InitialTransform = new BepuVisualTransform
                {
                    PositionY =
                        (fixture.StairCount - 1 - row) * fixture.StairRise -
                            fixture.StairHalfHeight,
                    PositionZ = (row - 0.5f * (fixture.StairCount - 1)) * fixture.StairDepth,
                    RotationW = 1f
                }
            };
        }
        int floorSlot = (int)execution.VisualInstanceCount - 1;
        instances[floorSlot] = new BepuVisualInstance
        {
            GeometryIndex = partGeometryCount + 1,
            StableSlot = (uint)floorSlot,
            TransformSlot = uint.MaxValue,
            InitialTransform = new BepuVisualTransform
            {
                PositionX = floor.Center.X,
                PositionY = floor.Center.Y,
                PositionZ = floor.Center.Z,
                RotationW = 1f
            }
        };
        geometryCount = totalGeometryCount;
        instanceCount = (int)execution.VisualInstanceCount;
        return 0;
    }

    public static int BuildVisualDebugPrimitives(
        BepuCaseView state, BepuVisualDebugPrimitive[] primitives)
    {
        CaseExecutionSpec execution = state.CaseExecution;
        CaseExecutionRagdoll fixture = execution.Ragdoll;
        if (state.Simulation == null || primitives == null ||
            primitives.Length < execution.VisualDebugPrimitiveCount ||
            execution.VisualDebugPrimitiveCount > fixture.ExtraStaticBoxCount) return 2;
        int first = fixture.ExtraStaticBoxCount - (int)execution.VisualDebugPrimitiveCount;
        for (int index = 0; index < execution.VisualDebugPrimitiveCount; ++index)
        {
            CaseExecutionBox box = fixture.ExtraStaticBoxes[first + index];
            primitives[index] = new BepuVisualDebugPrimitive
            {
                Kind = 2,
                MaterialIndex = 6,
                OriginOrCenterX = box.Center.X,
                OriginOrCenterY = box.Center.Y,
                OriginOrCenterZ = box.Center.Z,
                EndOrHalfExtentsX = box.HalfExtents.X,
                EndOrHalfExtentsY = box.HalfExtents.Y,
                EndOrHalfExtentsZ = box.HalfExtents.Z
            };
        }
        return 0;
    }

    public static string VisualPhysicsSettings(
        in CaseExecutionSpec execution, int threadCount)
    {
        return string.Create(
            System.Globalization.CultureInfo.InvariantCulture,
            $"velocity_iterations={execution.VelocityIterations}; substeps={execution.Substeps}; sleep={(execution.SleepMode == CaseExecutionToggle.Enabled ? "enabled" : "disabled")}; ccd={(execution.ContinuousCollisionMode == CaseExecutionToggle.Enabled ? "enabled" : "disabled")}; linked_collision={(execution.Ragdoll.LinkedCollisionMode == CaseExecutionToggle.Enabled ? "enabled" : "disabled")}; deterministic=yes; worker_count={threadCount}");
    }

    public static int RunHeadless(BepuRunnerArgs runnerArgs)
    {
        CaseExecutionSpec execution = runnerArgs.CaseExecution;
        if (runnerArgs.WarmupSteps != execution.WarmupWorkUnitCount ||
            runnerArgs.StepCount != execution.MeasuredWorkUnitCount) return 2;
        int workerCount = runnerArgs.WorkerCount;
        RunWarmup(workerCount, in execution, runnerArgs.WarmupSteps);
        BufferPool pool = new();
        Simulation simulation = default;
        ThreadDispatcher dispatcher = null;
        BodyHandle[] bodies = new BodyHandle[execution.DynamicBodyCount];
        ConstraintHandle[] joints = new ConstraintHandle[execution.ConstraintCount];
        BepuRagdollQuality quality = default;
        BepuRecordingWriter recording = default;
        try
        {
            simulation = CreateSimulation(
                pool, workerCount, in execution, bodies, joints, out dispatcher);
            BepuCaseView recordingView = new()
            {
                CaseExecution = runnerArgs.CaseExecution, Simulation = simulation, DynamicBodies = bodies
            };
            if (runnerArgs.RecordingMode == RecordingMode.On && BepuRecording.Begin(in runnerArgs, in recordingView, out recording) != 0) return 2;
            float timestepDuration = 1f / execution.TimestepHz;
            for (int step = 0; step < runnerArgs.StepCount; ++step)
            {
                if (dispatcher == null) simulation.Timestep(timestepDuration);
                else simulation.Timestep(timestepDuration, dispatcher);
                if (runnerArgs.VerificationMode == VerificationMode.On) ObserveQuality(simulation, bodies, joints, in execution, ref quality);
                if (runnerArgs.RecordingMode == RecordingMode.On && BepuRecording.Append(ref recording, in runnerArgs.CaseRegistration, recordingView, (ulong)(step + 1)) != 0) return 2;
            }

            if (runnerArgs.RecordingMode == RecordingMode.On && BepuRecording.Complete(ref recording) != 0) return 2;
            if (runnerArgs.VerificationMode == VerificationMode.On && quality.JointSamples == 0) return 2;
            BepuObservationRow[] observations = runnerArgs.VerificationMode == VerificationMode.On ? new BepuObservationRow[10] : Array.Empty<BepuObservationRow>();
            for (int index = 0; index < observations.Length; ++index)
            {
                observations[index] = new BepuObservationRow
                {
                    MetricId = QualityIds[index], PhaseId = "final", SampleIndex = quality.CompletedSteps,
                    ValueType = index < 2 ? BepuObservationValueType.Float64 : BepuObservationValueType.Uint64,
                    ValueBits = QualityValue(in quality, index)
                };
            }

            ulong invalidCount = 0;
            for (int index = 0; index < execution.DynamicBodyCount; ++index)
            {
                if (!simulation.Bodies.BodyExists(bodies[index]))
                {
                    ++invalidCount;
                    continue;
                }
                RigidPose pose = simulation.Bodies.GetBodyReference(bodies[index]).Pose;
                if (!float.IsFinite(pose.Position.X) ||
                    !float.IsFinite(pose.Position.Y) ||
                    !float.IsFinite(pose.Position.Z) ||
                    !float.IsFinite(pose.Orientation.X) ||
                    !float.IsFinite(pose.Orientation.Y) ||
                    !float.IsFinite(pose.Orientation.Z) ||
                    !float.IsFinite(pose.Orientation.W))
                {
                    invalidCount += 1;
                }
            }

            BepuResultValidity caseValidity = BepuResultValidity.Valid;
            BepuResultValidity metricValidity =
                runnerArgs.StepCount == execution.MeasuredWorkUnitCount ?
                BepuResultValidity.Valid : BepuResultValidity.Invalid;
            BepuBenchmarkResult result = new()
            {
                PhysicsSettings = VisualPhysicsSettings(
                    in runnerArgs.CaseExecution, runnerArgs.WorkerCount),
                BodyCount = (int)execution.BodyCount,
                ShapeCount = (int)execution.ShapeCount,
                QueryCount = (int)execution.QueryCount,
                ConstraintCount = (int)execution.ConstraintCount,
                InvalidTransformCount = invalidCount,
                EffectiveThreadCount = runnerArgs.WorkerCount,
                EffectiveWorkerCount = runnerArgs.WorkerCount,
                CompletedWorkUnitCount = runnerArgs.StepCount,
                WorkloadElapsedMilliseconds = double.NaN,
                CaseValidity = caseValidity,
                MetricValidity = metricValidity,
                Observations = observations
            };
            int resultStatus = BepuResultWriter.WriteResult(runnerArgs, in result);
            return resultStatus;
        }
        finally
        {
            if (runnerArgs.RecordingMode == RecordingMode.On) BepuRecording.Abort(ref recording);
            ReleaseCaseStorage(
                simulation, bodies, (int)execution.DynamicBodyCount,
                joints, (int)execution.ConstraintCount);
            simulation?.Dispose();
            dispatcher?.Dispose();
            pool.Clear();
        }
    }
}
