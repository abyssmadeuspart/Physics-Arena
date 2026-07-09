using BepuPhysics;
using BepuPhysics.Collidables;
using BepuPhysics.CollisionDetection;
using BepuPhysics.Constraints;
using BepuUtilities;
using BepuUtilities.Memory;
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
        if (ContactSpringiness.AngularFrequency == 0 && ContactSpringiness.TwiceDampingRatio == 0)
        {
            ContactSpringiness = new SpringSettings(30, 1);
            MaximumRecoveryVelocity = 2f;
            FrictionCoefficient = 0.5f;
        }
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

public struct BepuTransform
{
    public float PositionX;
    public float PositionY;
    public float PositionZ;
    public float RotationX;
    public float RotationY;
    public float RotationZ;
    public float RotationW;
}

public struct BepuStaticBox
{
    public float PositionX;
    public float PositionY;
    public float PositionZ;
    public float HalfExtentX;
    public float HalfExtentY;
    public float HalfExtentZ;
}

public struct BepuStabilityCounters
{
    public int InvalidTransformCount;
    public int BelowFloorCount;
    public int OutOfBoundsCount;
    public int DynamicBodyCount;
}

public static class BepuBoxContainerPileCase
{
    public const string EngineId = "bepuphysics2";
    public const string CaseId = "box_container_pile_10k";
    public const string EngineRef = "f73164bb3c9ca733eb3329f1f6b1cea4e216ece7";
    public const string ToolchainId = "dotnet8_release_no_profiling";
    public const int XCount = 25;
    public const int YCount = 16;
    public const int ZCount = 25;
    public const int DynamicBodyCount = XCount * YCount * ZCount;
    public const int StaticBodyCount = 5;
    public const int BodyCount = DynamicBodyCount + StaticBodyCount;
    public const float HalfExtent = 0.5f;
    public const float TimestepDuration = 1f / 60f;
    public const float Spacing = 1.02f;
    public const float InitialY = 24.51f;
    public const float InnerHalfWidth = 15.5f;
    public const float CeilingY = 96f;
    public const string FixtureSemantic = "open_container_falling_pile";
    public const string FixtureVersion = "bepu_open_container_v1";

    public static Simulation CreateSimulation(BufferPool bufferPool, int workerCount, out ThreadDispatcher threadDispatcher)
    {
        Simulation simulation = Simulation.Create(
            bufferPool,
            new PolygonNarrowPhaseCallbacks(new SpringSettings(30, 1), 2f, 0.5f),
            new PolygonPoseIntegratorCallbacks(new Vector3(0, -10, 0), 0, 0),
            new SolveDescription(4, 1));
        simulation.Deterministic = true;

        TypedIndex floorShape = simulation.Shapes.Add(new Box(29f, 1f, 29f));
        TypedIndex sideWallShape = simulation.Shapes.Add(new Box(1f, 24.5f, 29f));
        TypedIndex frontWallShape = simulation.Shapes.Add(new Box(29f, 24.5f, 1f));
        simulation.Statics.Add(new StaticDescription(new Vector3(0, -0.5f, 0), floorShape));
        simulation.Statics.Add(new StaticDescription(new Vector3(-14.5f, 11.75f, 0), sideWallShape));
        simulation.Statics.Add(new StaticDescription(new Vector3(14.5f, 11.75f, 0), sideWallShape));
        simulation.Statics.Add(new StaticDescription(new Vector3(0, 11.75f, -14.5f), frontWallShape));
        simulation.Statics.Add(new StaticDescription(new Vector3(0, 11.75f, 14.5f), frontWallShape));
        Box box = new(1f, 1f, 1f);
        TypedIndex boxShapeIndex = simulation.Shapes.Add(box);
        BodyInertia boxInertia = box.ComputeInertia(1f);
        float originX = -0.5f * (XCount - 1) * Spacing;
        float originZ = -0.5f * (ZCount - 1) * Spacing;
        BodyActivityDescription activity = new(-1f);

        for (int y = 0; y < YCount; ++y)
        {
            for (int z = 0; z < ZCount; ++z)
            {
                for (int x = 0; x < XCount; ++x)
                {
                    Vector3 location = new(
                        originX + x * Spacing,
                        InitialY + y * Spacing,
                        originZ + z * Spacing);
                    simulation.Bodies.Add(BodyDescription.CreateDynamic(location, boxInertia, boxShapeIndex, activity));
                }
            }
        }

        if (simulation.Bodies.ActiveSet.Count != DynamicBodyCount)
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

    public static void StepSimulation(Simulation simulation, ThreadDispatcher threadDispatcher, int stepCount)
    {
        if (threadDispatcher == null)
        {
            for (int index = 0; index < stepCount; ++index)
            {
                simulation.Timestep(TimestepDuration);
            }
            return;
        }

        for (int index = 0; index < stepCount; ++index)
        {
            simulation.Timestep(TimestepDuration, threadDispatcher);
        }
    }

    public static void RunWarmup(BufferPool bufferPool, int workerCount, int stepCount)
    {
        Simulation warmupSimulation = default;
        ThreadDispatcher warmupDispatcher = null;
        try
        {
            warmupSimulation = CreateSimulation(bufferPool, workerCount, out warmupDispatcher);
            StepSimulation(warmupSimulation, warmupDispatcher, stepCount);
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

                if (position.Y < 0f)
                {
                    counters.BelowFloorCount += 1;
                }

                if (position.X < -InnerHalfWidth || position.X > InnerHalfWidth ||
                    position.Z < -InnerHalfWidth || position.Z > InnerHalfWidth ||
                    position.Y < 0f || position.Y > CeilingY)
                {
                    counters.OutOfBoundsCount += 1;
                }
            }
        }
        return counters;
    }

    public static int SampleTransforms(Simulation simulation, BepuTransform[] transforms)
    {
        if (transforms.Length < DynamicBodyCount)
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
                transforms[transformIndex] = new BepuTransform
                {
                    PositionX = pose.Position.X,
                    PositionY = pose.Position.Y,
                    PositionZ = pose.Position.Z,
                    RotationX = pose.Orientation.X,
                    RotationY = pose.Orientation.Y,
                    RotationZ = pose.Orientation.Z,
                    RotationW = pose.Orientation.W
                };
                transformIndex += 1;
            }
        }
        return transformIndex == DynamicBodyCount ? 0 : 2;
    }

    public static int CopyStaticBoxes(BepuStaticBox[] boxes)
    {
        if (boxes.Length < StaticBodyCount)
        {
            return 2;
        }
        boxes[0] = new BepuStaticBox { PositionX = 0f, PositionY = -0.5f, PositionZ = 0f, HalfExtentX = 14.5f, HalfExtentY = 0.5f, HalfExtentZ = 14.5f };
        boxes[1] = new BepuStaticBox { PositionX = -14.5f, PositionY = 11.75f, PositionZ = 0f, HalfExtentX = 0.5f, HalfExtentY = 12.25f, HalfExtentZ = 14.5f };
        boxes[2] = new BepuStaticBox { PositionX = 14.5f, PositionY = 11.75f, PositionZ = 0f, HalfExtentX = 0.5f, HalfExtentY = 12.25f, HalfExtentZ = 14.5f };
        boxes[3] = new BepuStaticBox { PositionX = 0f, PositionY = 11.75f, PositionZ = -14.5f, HalfExtentX = 14.5f, HalfExtentY = 12.25f, HalfExtentZ = 0.5f };
        boxes[4] = new BepuStaticBox { PositionX = 0f, PositionY = 11.75f, PositionZ = 14.5f, HalfExtentX = 14.5f, HalfExtentY = 12.25f, HalfExtentZ = 0.5f };
        return 0;
    }

    public static string CaseStatus(BepuStabilityCounters counters)
    {
        return counters.InvalidTransformCount == 0 && counters.BelowFloorCount == 0 ? "ok" : "invalid_stability_counters";
    }

    public static string MetricStatus(string caseStatus)
    {
        return caseStatus == "ok" ? "ok" : "invalid_result";
    }
}
