using System;
using System.IO;
using Unity.Mathematics;
using Unity.Physics;

namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
    public static partial class UnityPhysicsBenchmarkRunner
    {
        private const string CsvHeader = "body_count,shape_count,invalid_transform_count,below_floor_count,out_of_bounds_count,case_status,metric_status,effective_thread_count,effective_worker_count,physics_elapsed_ms";

        private static int WriteResult(RunnerArgs args, in UnityPhysicsCaseDescriptor descriptor, ref PhysicsWorld world, double elapsedMilliseconds, int effectiveWorkerCount, int effectiveThreadCount)
        {
            string directory = Path.GetDirectoryName(Path.GetFullPath(args.OutputPath));
            if (!string.IsNullOrEmpty(directory))
            {
                Directory.CreateDirectory(directory);
            }

            bool writeHeader = !File.Exists(args.OutputPath);
            using StreamWriter writer = new StreamWriter(args.OutputPath, true);
            if (writeHeader)
            {
                writer.WriteLine(CsvHeader);
            }

            int invalidTransformCount = 0;
            int belowFloorCount = 0;
            int outOfBoundsCount = 0;
            for (int index = 0; index < world.NumDynamicBodies; ++index)
            {
                MotionData motionData = world.MotionDatas[index];
                RigidTransform worldFromBody = math.mul(motionData.WorldFromMotion, math.inverse(motionData.BodyFromMotion));
                float3 position = worldFromBody.pos;
                quaternion rotation = worldFromBody.rot;

                if (!math.all(math.isfinite(position)) || !math.all(math.isfinite(rotation.value)))
                {
                    invalidTransformCount += 1;
                }
                if (position.y < 0.0f)
                {
                    belowFloorCount += 1;
                }
                if (position.x < -InnerHalfWidth || position.x > InnerHalfWidth ||
                    position.z < -InnerHalfWidth || position.z > InnerHalfWidth ||
                    position.y < 0.0f || position.y > CeilingY)
                {
                    outOfBoundsCount += 1;
                }
            }

            double msPerStep = elapsedMilliseconds / args.StepCount;
            double stepsPerSecond = msPerStep > 0.0 ? 1000.0 / msPerStep : 0.0;
            string caseStatus = invalidTransformCount == 0 && belowFloorCount == 0 ? "ok" : "invalid_stability_counters";
            string metricStatus = caseStatus == "ok" ? "ok" : "invalid_result";
            if (elapsedMilliseconds <= 0.0 ||
                double.IsNaN(elapsedMilliseconds) ||
                double.IsInfinity(elapsedMilliseconds) ||
                msPerStep <= 0.0 ||
                double.IsNaN(msPerStep) ||
                double.IsInfinity(msPerStep) ||
                stepsPerSecond <= 0.0 ||
                double.IsNaN(stepsPerSecond) ||
                double.IsInfinity(stepsPerSecond))
            {
                Console.Error.WriteLine(FormattableString.Invariant($"invalid_result reason=metric elapsed_ms={elapsedMilliseconds:F9} step_count={args.StepCount}"));
                return 2;
            }
            if (metricStatus != "ok")
            {
                Console.Error.WriteLine("invalid_result reason=case_status status=" + caseStatus);
                return 2;
            }

            writer.WriteLine(FormattableString.Invariant(
                $"{descriptor.BodyCount},{descriptor.BodyCount},{invalidTransformCount},{belowFloorCount},{outOfBoundsCount},{caseStatus},{metricStatus},{effectiveThreadCount},{effectiveWorkerCount},{elapsedMilliseconds:F9}"));
            return 0;
        }
    }
}
