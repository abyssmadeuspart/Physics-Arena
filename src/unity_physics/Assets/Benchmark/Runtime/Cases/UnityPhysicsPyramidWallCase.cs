using System;
using System.Diagnostics;
using Unity.Collections;
using Unity.Entities;
using Unity.Jobs.LowLevel.Unsafe;
using Unity.Mathematics;
using Unity.Physics;
using PhysicsBoxCollider = Unity.Physics.BoxCollider;
using PhysicsCollider = Unity.Physics.Collider;
using PhysicsMaterial = Unity.Physics.Material;

namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
    public struct UnityPhysicsWallBodyInput
    {
        public MotionData Motion;
        public MotionVelocity Velocity;
    }

    public struct UnityPhysicsWallObservation
    {
        public double Height;
        public double LateralRms;
        public double TranslationalEnergy;
        public double RotationalEnergy;
        public double PotentialEnergy;
        public double Penetration;
        public double ElapsedMs;
        public ulong EscapedBodies;
        public ulong InvalidBodies;
    }


    public struct UnityPhysicsPyramidWallState
    {
        public CaseExecutionPyramidWall Fixture;
        public float3 Gravity;
        public uint TimestepHz;
        public uint SolverIterations;
        public uint Substeps;
        public int DynamicBodyCount;
        public int BodyCount;
        public int CompletedStepCount;
        public double InitialPotentialEnergy;
        public PhysicsWorld World;
        public BlobAssetReference<PhysicsCollider> BoxCollider;
        public BlobAssetReference<PhysicsCollider> FloorCollider;
        public NativeReference<int> StaticBodiesChanged;
        public Simulation Simulation;
    }

    public static partial class UnityPhysicsBenchmarkRunner
    {
        public static float3 PyramidWallPosition(in CaseExecutionPyramidWall wall, uint index)
        {
            uint row = 0;
            uint width = wall.RowCount;
            while (index >= width)
            {
                index -= width;
                --width;
                ++row;
            }
            float h = wall.HalfExtent;
            return new float3((row + 1) * h + 2f * index * h - h * wall.RowCount, (2 * row + 1) * h, 0f);
        }

        public static uint PyramidWallObservationStep(uint measured, uint ordinal)
        {
            if (measured < 4 || ordinal == 0) return ordinal + 1;
            if (ordinal == 3) return measured;
            return math.max(ordinal + 1, (measured + 1) / (ordinal == 1 ? 4u : 2u));
        }

        public static int PyramidWallObservationIndex(uint completed, uint measured)
        {
            for (uint index = 0; index < math.min(4u, measured); ++index)
                if (completed == PyramidWallObservationStep(measured, index)) return (int)index;
            return -1;
        }

        public static void AccumulatePyramidWall(in CaseExecutionSpec execution, uint index, float3 p, quaternion q,
            float3 linear, float3 angular, double mass, double rotationalEnergy, int sleepMatches, ref UnityPhysicsWallObservation sample)
        {
            if (!float.IsFinite(p.x) || !float.IsFinite(p.y) || !float.IsFinite(p.z) ||
                !float.IsFinite(q.value.x) || !float.IsFinite(q.value.y) || !float.IsFinite(q.value.z) || !float.IsFinite(q.value.w) ||
                !float.IsFinite(linear.x) || !float.IsFinite(linear.y) || !float.IsFinite(linear.z) ||
                !float.IsFinite(angular.x) || !float.IsFinite(angular.y) || !float.IsFinite(angular.z) ||
                !double.IsFinite(mass) || !double.IsFinite(rotationalEnergy))
            {
                ++sample.InvalidBodies;
                return;
            }
            if (mass <= 0 || sleepMatches == 0) ++sample.InvalidBodies;
            float3 initial = PyramidWallPosition(in execution.PyramidWall, index);
            double dx = (double)p.x - initial.x;
            double dz = (double)p.z - initial.z;
            sample.Height += p.y;
            sample.LateralRms += dx * dx + dz * dz;
            sample.TranslationalEnergy += 0.5 * mass * ((double)linear.x * linear.x + (double)linear.y * linear.y + (double)linear.z * linear.z);
            sample.RotationalEnergy += rotationalEnergy;
            sample.PotentialEnergy -= mass * ((double)execution.Gravity.x * p.x + (double)execution.Gravity.y * p.y + (double)execution.Gravity.z * p.z);
            double x = q.value.x, y = q.value.y, z = q.value.z, w = q.value.w;
            double h = execution.PyramidWall.HalfExtent;
            double sx = h * (math.abs(1 - 2 * (y*y + z*z)) + math.abs(2 * (x*y-z*w)) + math.abs(2 * (x*z+y*w)));
            double sy = h * (math.abs(2 * (x*y+z*w)) + math.abs(1 - 2 * (x*x+z*z)) + math.abs(2 * (y*z-x*w)));
            double sz = h * (math.abs(2 * (x*z-y*w)) + math.abs(2 * (y*z+x*w)) + math.abs(1 - 2 * (x*x+y*y)));
            sample.Penetration = math.max(sample.Penetration, sy - p.y);
            if (math.abs(p.x) + sx > execution.PyramidWall.FloorHalfExtents.x ||
                math.abs(p.z) + sz > execution.PyramidWall.FloorHalfExtents.z) ++sample.EscapedBodies;
        }

        public static UnityPhysicsObservationRow[] PyramidWallObservationRows(uint measured, UnityPhysicsWallObservation[] samples, double initialEnergy)
        {
            string[] ids = { "centre_of_mass_height", "lateral_rms", "translational_energy", "rotational_energy",
                "potential_energy", "floor_penetration", "escaped_body_count", "invalid_body_count", "observation_elapsed_ms" };
            uint count = math.min(4u, measured);
            UnityPhysicsObservationRow[] rows = new UnityPhysicsObservationRow[9 * count + 1];
            for (uint field = 0; field < 9; ++field)
            {
                for (uint ordinal = 0; ordinal < count; ++ordinal)
                {
                    UnityPhysicsWallObservation sample = samples[ordinal];
                    double value = field switch
                    {
                        0 => sample.Height, 1 => sample.LateralRms, 2 => sample.TranslationalEnergy,
                        3 => sample.RotationalEnergy, 4 => sample.PotentialEnergy, 5 => sample.Penetration,
                        _ => sample.ElapsedMs
                    };
                    rows[(field < 8 ? field * count : 8 * count + 1) + ordinal] = new UnityPhysicsObservationRow
                    {
                        MetricId = ids[field], PhaseId = "observation", SampleIndex = PyramidWallObservationStep(measured, ordinal),
                        ValueType = field == 6 || field == 7 ? UnityPhysicsObservationValueType.Uint64 : UnityPhysicsObservationValueType.Float64,
                        ValueBits = field == 6 ? sample.EscapedBodies : field == 7 ? sample.InvalidBodies : (ulong)BitConverter.DoubleToInt64Bits(value)
                    };
                }
            }
            rows[8 * count] = new UnityPhysicsObservationRow
            {
                MetricId = "initial_potential_energy", PhaseId = "construction", SampleIndex = 0,
                ValueType = UnityPhysicsObservationValueType.Float64, ValueBits = (ulong)BitConverter.DoubleToInt64Bits(initialEnergy)
            };
            return rows;
        }


        public static UnityPhysicsCaseRegistration UnityPhysicsPyramidWallRegistration()
        {
            return new UnityPhysicsCaseRegistration
            {
                Descriptor = new UnityPhysicsCaseDescriptor { EngineId = EngineId },
                RunHeadless = RunUnityPhysicsPyramidWall,
                BuildVisualScene = BuildUnityPhysicsPyramidWallVisualScene
            };
        }

        public static BlobAssetReference<PhysicsCollider> CreatePyramidWallBoxCollider(
            float3 halfExtents, float friction, float restitution)
        {
            PhysicsMaterial material = FixtureMaterial(friction, restitution);
            material.FrictionCombinePolicy = PhysicsMaterial.CombinePolicy.ArithmeticMean;
            material.RestitutionCombinePolicy = PhysicsMaterial.CombinePolicy.ArithmeticMean;
            return PhysicsBoxCollider.Create(new BoxGeometry
            {
                Center = float3.zero,
                Orientation = quaternion.identity,
                Size = halfExtents * 2f,
                BevelRadius = 0f
            }, FixtureFilter(), material);
        }

        public static void SetPyramidWallDynamicBody(
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

        public static int CreateUnityPhysicsPyramidWallState(
            in CaseExecutionSpec execution, ref UnityPhysicsPyramidWallState state, VerificationMode verificationMode = VerificationMode.On)
        {
            state.Fixture = execution.PyramidWall;
            state.Gravity = execution.Gravity;
            state.TimestepHz = execution.TimestepHz;
            state.SolverIterations = execution.SolverIterations;
            state.Substeps = execution.Substeps;
            state.DynamicBodyCount = (int)execution.DynamicBodyCount;
            state.BodyCount = (int)execution.BodyCount;
            state.CompletedStepCount = 0;
            state.World = new PhysicsWorld((int)execution.StaticBodyCount,
                state.DynamicBodyCount, 0);
            state.BoxCollider = CreatePyramidWallBoxCollider(new float3(state.Fixture.HalfExtent), execution.Friction, execution.Restitution);
            state.FloorCollider = CreatePyramidWallBoxCollider(
                state.Fixture.FloorHalfExtents, execution.Friction, execution.Restitution);
            float boxMass = state.Fixture.Density * state.BoxCollider.Value.MassProperties.Volume;
            float expectedMass = 8f * state.Fixture.HalfExtent * state.Fixture.HalfExtent * state.Fixture.HalfExtent * state.Fixture.Density;
            float expectedInertia = (2f / 3f) * expectedMass * state.Fixture.HalfExtent * state.Fixture.HalfExtent;
            for (int index = 0; index < state.DynamicBodyCount; ++index)
            {
                float3 position = PyramidWallPosition(in state.Fixture, (uint)index);
                SetPyramidWallDynamicBody(ref state.World, index, state.BoxCollider, boxMass, position, quaternion.identity);
                MotionData motion = state.World.MotionDatas[index];
                MotionVelocity velocity = state.World.MotionVelocities[index];
                RigidTransform native = math.mul(motion.WorldFromMotion, math.inverse(motion.BodyFromMotion));
                double mass = 1.0 / velocity.InverseMass;
                float3 inertia = math.rcp(velocity.InverseInertia);
                if (math.any(native.pos != position) || math.any(native.rot.value != quaternion.identity.value) ||
                    math.any(velocity.LinearVelocity != float3.zero) || math.any(velocity.AngularVelocity != float3.zero) ||
                    !double.IsFinite(mass) || mass <= 0 || !math.all(math.isfinite(inertia)) ||
                    math.abs(mass - expectedMass) > 1e-5 * expectedMass ||
                    math.any(math.abs(inertia - expectedInertia) > 1e-5f * expectedInertia) ||
                    motion.LinearDamping != 0f || motion.AngularDamping != 0f || velocity.GravityFactor != 1f) return 2;
                if (verificationMode == VerificationMode.On) state.InitialPotentialEnergy -= mass * ((double)execution.Gravity.x * native.pos.x +
                    (double)execution.Gravity.y * native.pos.y + (double)execution.Gravity.z * native.pos.z);
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
            if (state.World.NumDynamicBodies != execution.DynamicBodyCount || state.World.NumStaticBodies != 1 ||
                state.World.NumBodies != execution.BodyCount || state.World.NumBodies != execution.ShapeCount ||
                math.any(staticBodies[0].WorldFromBody.pos != new float3(0f, -state.Fixture.FloorHalfExtents.y, 0f)) ||
                math.any(staticBodies[0].WorldFromBody.rot.value != quaternion.identity.value) ||
                state.BoxCollider.Value.Type != ColliderType.Box || state.FloorCollider.Value.Type != ColliderType.Box ||
                state.BoxCollider.Value.GetFriction() != execution.Friction || state.FloorCollider.Value.GetFriction() != execution.Friction ||
                state.BoxCollider.Value.GetRestitution() != execution.Restitution || state.FloorCollider.Value.GetRestitution() != execution.Restitution ||
                execution.SleepMode != CaseExecutionToggle.Disabled || execution.ContinuousCollisionMode != CaseExecutionToggle.Disabled) return 2;
            state.World.UpdateIndexMaps();
            state.StaticBodiesChanged = new NativeReference<int>(Allocator.Persistent);
            state.StaticBodiesChanged.Value = 1;
            state.Simulation = Simulation.Create();
            return 0;
        }

        public static UnityPhysicsTransform PyramidWallTransform(
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

        public static int BuildUnityPhysicsPyramidWallVisualScene(
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
            if (geometries == null || geometries.Length < 2 || instances == null ||
                instances.Length < (int)execution.BodyCount) return 2;
            if (BuildResolvedVisualGeometry(in execution, in execution.SelectedGeometry,
            ref meshes, out geometries[0]) != 0) return 2;
            geometries[1] = new UnityPhysicsVisualGeometry { Kind = 2,
                ParameterX = execution.PyramidWall.FloorHalfExtents.x,
                ParameterY = execution.PyramidWall.FloorHalfExtents.y,
                ParameterZ = execution.PyramidWall.FloorHalfExtents.z };
            for (int index = 0; index < (int)execution.DynamicBodyCount; ++index)
            {
                instances[index] = new UnityPhysicsVisualInstance
                {
                    GeometryIndex = 0u,
                    StableSlot = (uint)index,
                    TransformSlot = (uint)index,
                    InitialTransform = PyramidWallTransform(ref state.World, index)
                };
            }
            int floorSlot = (int)execution.DynamicBodyCount;
            instances[floorSlot] = new UnityPhysicsVisualInstance
            {
                GeometryIndex = 1,
                StableSlot = (uint)floorSlot,
                TransformSlot = uint.MaxValue,
                InitialTransform = new UnityPhysicsTransform
                {
                    PositionY = -execution.PyramidWall.FloorHalfExtents.y,
                    RotationW = 1f
                }
            };
            geometryCount = 2;
            instanceCount = (int)execution.BodyCount;
            return 0;
        }

        public static double CaptureUnityPhysicsPyramidWall(ref PhysicsWorld world, UnityPhysicsWallBodyInput[] inputs)
        {
            long start = Stopwatch.GetTimestamp();
            for (int index = 0; index < inputs.Length; ++index)
            {
                inputs[index] = new UnityPhysicsWallBodyInput
                {
                    Motion = world.MotionDatas[index], Velocity = world.MotionVelocities[index]
                };
            }
            return (Stopwatch.GetTimestamp() - start) * 1000.0 / Stopwatch.Frequency;
        }

        public static UnityPhysicsWallObservation ReduceUnityPhysicsPyramidWall(UnityPhysicsWallBodyInput[] inputs, in CaseExecutionSpec execution, double extractionMs)
        {
            long start = Stopwatch.GetTimestamp();
            UnityPhysicsWallObservation sample = default;
            for (int index = 0; index < inputs.Length; ++index)
            {
                MotionData motion = inputs[index].Motion;
                MotionVelocity velocity = inputs[index].Velocity;
                RigidTransform pose = math.mul(motion.WorldFromMotion, math.inverse(motion.BodyFromMotion));
                float3 local = velocity.AngularVelocity;
                double energy = 0.5 * ((double)local.x * local.x / velocity.InverseInertia.x +
                    (double)local.y * local.y / velocity.InverseInertia.y + (double)local.z * local.z / velocity.InverseInertia.z);
                float3 angular = math.rotate(motion.WorldFromMotion.rot, local);
                AccumulatePyramidWall(in execution, (uint)index, pose.pos, pose.rot, velocity.LinearVelocity, angular,
                    1.0 / velocity.InverseMass, energy, 1, ref sample);
            }
            sample.Height /= inputs.Length;
            sample.LateralRms = math.sqrt(sample.LateralRms / inputs.Length);
            sample.ElapsedMs = extractionMs + (Stopwatch.GetTimestamp() - start) * 1000.0 / Stopwatch.Frequency;
            return sample;
        }

        public static string UnityPhysicsPyramidWallVisualSettings(
            in CaseExecutionSpec execution, int workerCount, CaseExecutionToggle solverStabilization)
        {
            Solver.StabilizationHeuristicSettings stabilization = Solver.StabilizationHeuristicSettings.Default;
            return FormattableString.Invariant(
                $"solver_iterations={execution.SolverIterations}; substeps={execution.Substeps}; solver_type=iterative; stabilization={(solverStabilization == CaseExecutionToggle.Enabled ? "on" : "off")}; stab_friction={(stabilization.EnableFrictionVelocities ? "on" : "off")}; stab_clip={stabilization.VelocityClippingFactor}; stab_inertia={stabilization.InertiaScalingFactor}; linear_damping=0; angular_damping=0; synchronize_collision_world=on; sleep=not_applicable; worker_count={workerCount}");
        }

        public static void DisposeUnityPhysicsPyramidWallState(
            ref UnityPhysicsPyramidWallState state)
        {
            if (state.StaticBodiesChanged.IsCreated)
            {
                state.StaticBodiesChanged.Dispose();
            }
            state.Simulation.Dispose();
            state.World.Dispose();
            if (state.BoxCollider.IsCreated)
            {
                state.BoxCollider.Dispose();
            }
            if (state.FloorCollider.IsCreated)
            {
                state.FloorCollider.Dispose();
            }
        }

        public static int RunUnityPhysicsPyramidWall(RunnerArgs runnerArgs)
        {
            int previousWorkerCount = JobsUtility.JobWorkerCount;
            UnityPhysicsPyramidWallState state = default;
            UnityPhysicsRecordingWriter recording = default;
            UnityPhysicsStackCapture stack = default;
            try
            {
                int requestedWorkerCount = math.max(0, runnerArgs.ThreadCount - 1);
                JobsUtility.JobWorkerCount = requestedWorkerCount;
                int effectiveWorkerCount = JobsUtility.JobWorkerCount;
                int effectiveThreadCount = effectiveWorkerCount + 1;
                if (CreateUnityPhysicsPyramidWallState(
                    in runnerArgs.CaseExecution, ref state, runnerArgs.VerificationMode) != 0) return 2;
                long[] rawStepDurations = new long[runnerArgs.StepCount];
                int sampleCount = runnerArgs.VerificationMode == VerificationMode.On ? math.min(4, runnerArgs.StepCount) : 0;
                UnityPhysicsWallObservation[] samples = new UnityPhysicsWallObservation[sampleCount];
                UnityPhysicsWallBodyInput[][] inputs = new UnityPhysicsWallBodyInput[sampleCount][];
                for (int ordinal = 0; ordinal < sampleCount; ++ordinal)
                    inputs[ordinal] = new UnityPhysicsWallBodyInput[state.World.NumDynamicBodies];
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
                    StepWorld(ref state.World, ref state.Simulation, state.StaticBodiesChanged,
                        state.Gravity, 1f / state.TimestepHz, state.SolverIterations, state.Substeps,
                        runnerArgs.ThreadCount > 1 ? 1 : 0, runnerArgs.SolverStabilization);
                    if (runnerArgs.VerificationMode == VerificationMode.On && UnityPhysicsStackStateCapture.Append(ref stack, in runnerArgs.CaseRegistration, ref capture,
                        UnityPhysicsStackCapturePhase.Warmup, 0, (uint)step + 1) != 0) return 2;
                }
                if (runnerArgs.RecordingMode == RecordingMode.On && UnityPhysicsRecording.Begin(in runnerArgs, ref capture, out recording) != 0) return 2;
                for (int step = 0; step < runnerArgs.StepCount; ++step)
                {
                    long start = Stopwatch.GetTimestamp();
                    StepWorld(ref state.World, ref state.Simulation, state.StaticBodiesChanged,
                        state.Gravity, 1f / state.TimestepHz, state.SolverIterations, state.Substeps,
                        runnerArgs.ThreadCount > 1 ? 1 : 0, runnerArgs.SolverStabilization);
                    rawStepDurations[step] = Stopwatch.GetTimestamp() - start;
                    state.CompletedStepCount += 1;
                    int ordinal = PyramidWallObservationIndex((uint)state.CompletedStepCount, (uint)runnerArgs.StepCount);
                    if (runnerArgs.VerificationMode == VerificationMode.On && ordinal >= 0) samples[ordinal].ElapsedMs = CaptureUnityPhysicsPyramidWall(ref state.World, inputs[ordinal]);
                    if (runnerArgs.VerificationMode == VerificationMode.On && UnityPhysicsStackStateCapture.Append(ref stack, in runnerArgs.CaseRegistration, ref capture,
                        UnityPhysicsStackCapturePhase.Measured, 0, (uint)step + 1) != 0) return 2;
                    if (runnerArgs.RecordingMode == RecordingMode.On && UnityPhysicsRecording.Append(ref recording, in runnerArgs.CaseRegistration,
                        ref capture, (ulong)step + 1) != 0) return 2;
                }
                if (runnerArgs.VerificationMode == VerificationMode.On && UnityPhysicsStackStateCapture.Close(ref stack) != 0) return 2;
                if (runnerArgs.RecordingMode == RecordingMode.On && UnityPhysicsRecording.Complete(ref recording) != 0) return 2;
                for (int ordinal = 0; ordinal < sampleCount; ++ordinal)
                    samples[ordinal] = ReduceUnityPhysicsPyramidWall(inputs[ordinal], in runnerArgs.CaseExecution, samples[ordinal].ElapsedMs);
                double elapsedMilliseconds = 0.0;
                for (int step = 0; step < rawStepDurations.Length; ++step)
                    elapsedMilliseconds += rawStepDurations[step] * 1000.0 / Stopwatch.Frequency;
                ulong invalidCount = runnerArgs.VerificationMode == VerificationMode.Off ? (ulong)CountUnityPhysicsContainerPileInvalidTransforms(ref state.World) : 0;
                for (int index = 0; index < samples.Length; ++index) invalidCount += samples[index].InvalidBodies;
                UnityPhysicsBenchmarkResult result = new UnityPhysicsBenchmarkResult
                {
                    PhysicsSettings = UnityPhysicsPyramidWallVisualSettings(
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
                        UnityPhysicsResultValidity.Valid : UnityPhysicsResultValidity.Invalid,
                    Observations = runnerArgs.VerificationMode == VerificationMode.On ? PyramidWallObservationRows((uint)runnerArgs.StepCount, samples, state.InitialPotentialEnergy) : Array.Empty<UnityPhysicsObservationRow>()
                };
                int status = WriteResult(runnerArgs, in result);
                return status == 0 ? WriteStepTiming(runnerArgs, rawStepDurations) : status;
            }
            finally
            {
                UnityPhysicsStackStateCapture.Abort(ref stack);
                if (runnerArgs.RecordingMode == RecordingMode.On) UnityPhysicsRecording.Abort(ref recording);
                JobsUtility.JobWorkerCount = previousWorkerCount;
                DisposeUnityPhysicsPyramidWallState(ref state);
            }
        }
    }
}
