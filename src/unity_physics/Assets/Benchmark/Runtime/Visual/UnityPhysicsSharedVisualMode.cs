using System;
using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Unity.Collections;
using Unity.Entities;
using Unity.Jobs.LowLevel.Unsafe;
using Unity.Mathematics;
using Unity.Physics;
using PhysicsCollider = Unity.Physics.Collider;

namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
    public static partial class UnityPhysicsBenchmarkRunner
    {
        private const uint VisualMagic = 0x42565354u;
        private const ushort VisualVersion = 1;
        private const int MaxPayloadBytes = 2 * 1024 * 1024;
        private const int TextCapacity = 64;
        private const uint StatusOk = 0;
        private const uint StatusVisualFailed = 5;
        private const ushort FrameHello = 1;
        private const ushort FrameStart = 2;
        private const ushort FrameWarmup = 3;
        private const ushort FrameStep = 4;
        private const ushort FrameShutdown = 5;
        private const ushort FrameHelloAck = 6;
        private const ushort FrameStarted = 7;
        private const ushort FrameSnapshot = 8;
        private const ushort FrameCompleted = 9;
        private const int SnapshotHeaderBytes = 360;
        private const int TransformBytes = 28;
        private const int StaticBoxBytes = 24;

        private static int RunSharedVisualMode(string[] args)
        {
            int parseStatus = ParseSharedVisualArgs(args, out SharedVisualCli cli);
            if (parseStatus != 0)
            {
                return parseStatus;
            }

            int previousWorkerCount = JobsUtility.JobWorkerCount;
            PhysicsWorld world = default;
            BlobAssetReference<PhysicsCollider> dynamicCollider = default;
            NativeArray<BlobAssetReference<PhysicsCollider>> staticColliders = default;
            NativeReference<int> staticBodiesChanged = default;
            Simulation simulation = default;
            TcpClient client = null;
            NetworkStream stream = null;

            try
            {
                int caseStatus = ResolveUnityPhysicsCase(cli.CaseId, out UnityPhysicsCaseDescriptor caseDescriptor);
                if (caseStatus != 0)
                {
                    return caseStatus;
                }

                int requestedWorkerCount = math.max(0, cli.ThreadCount - 1);
                JobsUtility.JobWorkerCount = requestedWorkerCount;
                client = new TcpClient();
                client.Connect(IPAddress.Loopback, cli.ConnectPort);
                stream = client.GetStream();

                VisualFrame hello = ReceiveFrame(stream, FrameHello, 1);
                if (DecodeHello(cli, hello.Payload) != 0 || SendHelloAck(stream, cli, caseDescriptor, 1) != 0)
                {
                    return 2;
                }

                VisualFrame start = ReceiveFrame(stream, FrameStart, 2);
                if (DecodeStart(cli, start.Payload, out VisualRunConfig config) != 0)
                {
                    return 2;
                }

                world = new PhysicsWorld(caseDescriptor.StaticBodyCount, caseDescriptor.DynamicBodyCount, 0);
                dynamicCollider = CreateBoxCollider(new float3(1.0f, 1.0f, 1.0f));
                staticColliders = new NativeArray<BlobAssetReference<PhysicsCollider>>(caseDescriptor.StaticBodyCount, Allocator.Persistent);
                CreateFixture(ref world, dynamicCollider, staticColliders);
                world.UpdateIndexMaps();
                staticBodiesChanged = new NativeReference<int>(Allocator.Persistent);
                staticBodiesChanged.Value = 1;
                simulation = Simulation.Create();

                if (SendCompleted(stream, FrameStarted, 2, StatusOk, 0, 0.0) != 0)
                {
                    return 2;
                }

                VisualFrame warmup = ReceiveFrame(stream, FrameWarmup, 3);
                int warmupSteps = DecodeStep(warmup.Payload);
                if (warmupSteps < 0)
                {
                    return 2;
                }
                int warmupStatus = RunVisualWarmup(warmupSteps, cli.ThreadCount > 1);
                if (SendCompleted(stream, FrameCompleted, 3, warmupStatus == 0 ? StatusOk : StatusVisualFailed, 0, 0.0) != 0 || warmupStatus != 0)
                {
                    return 2;
                }

                int completedStepCount = 0;
                double physicsElapsedMs = 0.0;
                UnityPhysicsTransform[] transforms = new UnityPhysicsTransform[caseDescriptor.DynamicBodyCount];
                UnityPhysicsStaticBox[] boxes = new UnityPhysicsStaticBox[caseDescriptor.StaticBodyCount];
                byte[] payload = new byte[SnapshotHeaderBytes + caseDescriptor.DynamicBodyCount * TransformBytes + caseDescriptor.StaticBodyCount * StaticBoxBytes];
                for (uint sequence = 4;; ++sequence)
                {
                    VisualFrame frame = ReceiveAnyFrame(stream, sequence);
                    if (frame.FrameType == FrameShutdown)
                    {
                        if (SendCompleted(stream, FrameCompleted, sequence, StatusOk, completedStepCount, physicsElapsedMs) != 0)
                        {
                            Console.Error.WriteLine("run_failed reason=send_shutdown_completed sequence=" + sequence);
                            return 2;
                        }
                        return 0;
                    }
                    if (frame.FrameType != FrameStep)
                    {
                        Console.Error.WriteLine("run_failed reason=unexpected_frame frame_type=" + frame.FrameType);
                        return 2;
                    }
                    int stepCount = DecodeStep(frame.Payload);
                    if (stepCount < 0)
                    {
                        return 2;
                    }
                    Stopwatch stopwatch = Stopwatch.StartNew();
                    for (int step = 0; step < stepCount; ++step)
                    {
                        StepWorld(ref world, ref simulation, staticBodiesChanged, cli.ThreadCount > 1);
                        staticBodiesChanged.Value = 0;
                    }
                    stopwatch.Stop();
                    physicsElapsedMs += stopwatch.Elapsed.TotalMilliseconds;
                    completedStepCount += stepCount;
                    BuildSnapshotPayload(caseDescriptor, config, ref world, completedStepCount, physicsElapsedMs, transforms, boxes, payload);
                    if (SendFrame(stream, FrameSnapshot, sequence, payload) != 0)
                    {
                        return 2;
                    }
                }
            }
            finally
            {
                JobsUtility.JobWorkerCount = previousWorkerCount;
                stream?.Dispose();
                client?.Dispose();
                DisposeCaseResources(ref world, ref simulation, ref dynamicCollider, ref staticColliders, ref staticBodiesChanged);
            }
        }

        private static int RunVisualWarmup(int warmupSteps, bool multiThreaded)
        {
            RunWarmup(warmupSteps, multiThreaded);
            return 0;
        }

        private static int ParseSharedVisualArgs(string[] args, out SharedVisualCli cli)
        {
            cli = new SharedVisualCli
            {
                SharedVisualMode = string.Empty,
                ConnectHost = "127.0.0.1",
                RunToken = string.Empty,
                EngineId = string.Empty,
                CaseId = string.Empty,
                ConnectPort = 0,
                ThreadCount = 1,
                RepeatIndex = 0,
                StepCount = 300,
                WarmupSteps = 0
            };

            for (int index = 0; index < args.Length; ++index)
            {
                string arg = args[index];
                if (arg.StartsWith("--producer-mode=", StringComparison.Ordinal))
                {
                    cli.SharedVisualMode = arg.Substring("--producer-mode=".Length);
                }
                else if (arg.StartsWith("--connect-host=", StringComparison.Ordinal))
                {
                    cli.ConnectHost = arg.Substring("--connect-host=".Length);
                }
                else if (arg.StartsWith("--connect-port=", StringComparison.Ordinal))
                {
                    if (!int.TryParse(arg.Substring("--connect-port=".Length), NumberStyles.Integer, CultureInfo.InvariantCulture, out cli.ConnectPort) || cli.ConnectPort < 1)
                    {
                        Console.Error.WriteLine("invalid_argument name=connect-port value=" + arg);
                        return 2;
                    }
                }
                else if (arg.StartsWith("--run-token=", StringComparison.Ordinal))
                {
                    cli.RunToken = arg.Substring("--run-token=".Length);
                }
                else if (arg.StartsWith("--engine=", StringComparison.Ordinal))
                {
                    cli.EngineId = arg.Substring("--engine=".Length);
                }
                else if (arg.StartsWith("--case=", StringComparison.Ordinal))
                {
                    cli.CaseId = arg.Substring("--case=".Length);
                }
                else if (arg.StartsWith("--thread-count=", StringComparison.Ordinal))
                {
                    if (!int.TryParse(arg.Substring("--thread-count=".Length), NumberStyles.Integer, CultureInfo.InvariantCulture, out cli.ThreadCount) || cli.ThreadCount < 1)
                    {
                        Console.Error.WriteLine("invalid_argument name=thread-count value=" + arg);
                        return 2;
                    }
                }
                else if (arg.StartsWith("--repeat-index=", StringComparison.Ordinal))
                {
                    if (!int.TryParse(arg.Substring("--repeat-index=".Length), NumberStyles.Integer, CultureInfo.InvariantCulture, out cli.RepeatIndex) || cli.RepeatIndex < 0)
                    {
                        Console.Error.WriteLine("invalid_argument name=repeat-index value=" + arg);
                        return 2;
                    }
                }
                else if (arg.StartsWith("--step-count=", StringComparison.Ordinal))
                {
                    if (!int.TryParse(arg.Substring("--step-count=".Length), NumberStyles.Integer, CultureInfo.InvariantCulture, out cli.StepCount) || cli.StepCount < 1)
                    {
                        Console.Error.WriteLine("invalid_argument name=step-count value=" + arg);
                        return 2;
                    }
                }
                else if (arg.StartsWith("--warmup-steps=", StringComparison.Ordinal))
                {
                    if (!int.TryParse(arg.Substring("--warmup-steps=".Length), NumberStyles.Integer, CultureInfo.InvariantCulture, out cli.WarmupSteps) || cli.WarmupSteps < 0)
                    {
                        Console.Error.WriteLine("invalid_argument name=warmup-steps value=" + arg);
                        return 2;
                    }
                }
            }

            if (cli.SharedVisualMode != "shared-visual" || cli.ConnectHost != "127.0.0.1" || cli.EngineId != UnityPhysicsBoxContainerPileDescriptor().EngineId || string.IsNullOrWhiteSpace(cli.CaseId) || string.IsNullOrWhiteSpace(cli.RunToken) || cli.ConnectPort <= 0)
            {
                Console.Error.WriteLine("invalid_argument reason=shared_visual_mode_contract");
                return 2;
            }
            return 0;
        }

        private static VisualFrame ReceiveFrame(NetworkStream stream, ushort expectedFrameType, uint expectedSequence)
        {
            VisualFrame frame = ReceiveAnyFrame(stream, expectedSequence);
            if (frame.FrameType != expectedFrameType)
            {
                throw new InvalidOperationException("Unexpected visual frame.");
            }
            return frame;
        }

        private static VisualFrame ReceiveAnyFrame(NetworkStream stream, uint expectedSequence)
        {
            byte[] header = new byte[20];
            ReadExact(stream, header, header.Length);
            uint magic = ReadUInt32(header, 0);
            ushort version = ReadUInt16(header, 4);
            ushort frameType = ReadUInt16(header, 6);
            uint sequence = ReadUInt32(header, 8);
            uint payloadBytes = ReadUInt32(header, 12);
            uint crc = ReadUInt32(header, 16);
            if (magic != VisualMagic || version != VisualVersion || sequence != expectedSequence || payloadBytes > MaxPayloadBytes || crc != HeaderCrc(frameType, sequence, payloadBytes))
            {
                throw new InvalidOperationException("Invalid visual frame header.");
            }
            byte[] payload = new byte[payloadBytes];
            if (payloadBytes > 0)
            {
                ReadExact(stream, payload, payload.Length);
            }
            return new VisualFrame { FrameType = frameType, Sequence = sequence, Payload = payload };
        }

        private static void ReadExact(NetworkStream stream, byte[] buffer, int byteCount)
        {
            int offset = 0;
            while (offset < byteCount)
            {
                int read = stream.Read(buffer, offset, byteCount - offset);
                if (read <= 0)
                {
                    throw new EndOfStreamException("Visual transport closed.");
                }
                offset += read;
            }
        }

        private static int SendFrame(NetworkStream stream, ushort frameType, uint sequence, byte[] payload)
        {
            byte[] header = new byte[20];
            int offset = 0;
            WriteUInt32(header, ref offset, VisualMagic);
            WriteUInt16(header, ref offset, VisualVersion);
            WriteUInt16(header, ref offset, frameType);
            WriteUInt32(header, ref offset, sequence);
            WriteUInt32(header, ref offset, (uint)payload.Length);
            WriteUInt32(header, ref offset, HeaderCrc(frameType, sequence, (uint)payload.Length));
            stream.Write(header, 0, header.Length);
            if (payload.Length > 0)
            {
                stream.Write(payload, 0, payload.Length);
            }
            return 0;
        }

        private static int SendHelloAck(NetworkStream stream, SharedVisualCli cli, UnityPhysicsCaseDescriptor caseDescriptor, uint sequence)
        {
            byte[] payload = new byte[208];
            int offset = 0;
            WriteText(payload, ref offset, caseDescriptor.EngineId);
            WriteText(payload, ref offset, caseDescriptor.CaseId);
            WriteText(payload, ref offset, "unity_physics_visual_producer_player");
            WriteUInt32(payload, ref offset, 0);
            WriteUInt32(payload, ref offset, 0);
            WriteUInt32(payload, ref offset, HashText(cli.RunToken));
            WriteUInt32(payload, ref offset, StatusOk);
            return SendFrame(stream, FrameHelloAck, sequence, payload);
        }

        private static int SendCompleted(NetworkStream stream, ushort frameType, uint sequence, uint status, int completedStepCount, double physicsElapsedMs)
        {
            byte[] payload = new byte[16];
            int offset = 0;
            WriteUInt32(payload, ref offset, status);
            WriteUInt32(payload, ref offset, (uint)completedStepCount);
            WriteDouble(payload, ref offset, physicsElapsedMs);
            return SendFrame(stream, frameType, sequence, payload);
        }

        private static int DecodeHello(SharedVisualCli cli, byte[] payload)
        {
            if (payload.Length != TextCapacity * 3)
            {
                return 2;
            }
            if (ReadText(payload, 0) != cli.RunToken || ReadText(payload, 64) != cli.EngineId || ReadText(payload, 128) != cli.CaseId)
            {
                return 2;
            }
            return 0;
        }

        private static int DecodeStart(SharedVisualCli cli, byte[] payload, out VisualRunConfig config)
        {
            config = default;
            if (payload.Length != 156)
            {
                return 2;
            }
            if (ReadText(payload, 0) != cli.CaseId || ReadText(payload, 64) != cli.EngineId)
            {
                return 2;
            }
            config.ThreadCount = (int)ReadUInt32(payload, 128);
            config.RepeatIndex = (int)ReadUInt32(payload, 132);
            config.StepCount = (int)ReadUInt32(payload, 136);
            config.WarmupSteps = (int)ReadUInt32(payload, 140);
            return 0;
        }

        private static int DecodeStep(byte[] payload)
        {
            return payload.Length == 4 ? (int)ReadUInt32(payload, 0) : -1;
        }

        private static void BuildSnapshotPayload(UnityPhysicsCaseDescriptor caseDescriptor, VisualRunConfig config, ref PhysicsWorld world, int completedStepCount, double physicsElapsedMs, UnityPhysicsTransform[] transforms, UnityPhysicsStaticBox[] boxes, byte[] payload)
        {
            if (SampleUnityPhysicsTransforms(ref world, transforms) != 0 || CopyUnityPhysicsStaticBoxes(boxes) != 0)
            {
                throw new InvalidOperationException("Snapshot sample failed.");
            }

            int offset = 0;
            WriteText(payload, ref offset, caseDescriptor.CaseId);
            WriteText(payload, ref offset, caseDescriptor.EngineId);
            WriteText(payload, ref offset, caseDescriptor.FixtureSemantic);
            WriteText(payload, ref offset, caseDescriptor.FixtureVersion);
            WriteUInt32(payload, ref offset, (uint)config.ThreadCount);
            WriteUInt32(payload, ref offset, (uint)config.RepeatIndex);
            WriteUInt32(payload, ref offset, (uint)config.StepCount);
            WriteUInt32(payload, ref offset, (uint)config.WarmupSteps);
            WriteUInt32(payload, ref offset, (uint)caseDescriptor.BodyCount);
            WriteUInt32(payload, ref offset, (uint)caseDescriptor.BodyCount);
            WriteUInt32(payload, ref offset, (uint)caseDescriptor.StaticBodyCount);
            WriteUInt32(payload, ref offset, 0);
            WriteDouble(payload, ref offset, physicsElapsedMs);
            WriteDouble(payload, ref offset, 0.0);
            WriteDouble(payload, ref offset, 0.0);
            WriteDouble(payload, ref offset, 0.0);
            WriteDouble(payload, ref offset, 0.0);
            WriteUInt32(payload, ref offset, (uint)caseDescriptor.DynamicBodyCount);
            WriteUInt32(payload, ref offset, (uint)caseDescriptor.StaticBodyCount);
            WriteUInt32(payload, ref offset, (uint)completedStepCount);
            WriteUInt32(payload, ref offset, StatusOk);
            WriteUInt32(payload, ref offset, 0);
            WriteFloat(payload, ref offset, caseDescriptor.DynamicHalfExtent);
            WriteFloat(payload, ref offset, caseDescriptor.DynamicHalfExtent);
            WriteFloat(payload, ref offset, caseDescriptor.DynamicHalfExtent);

            for (int index = 0; index < caseDescriptor.DynamicBodyCount; ++index)
            {
                WriteFloat(payload, ref offset, transforms[index].PositionX);
                WriteFloat(payload, ref offset, transforms[index].PositionY);
                WriteFloat(payload, ref offset, transforms[index].PositionZ);
                WriteFloat(payload, ref offset, transforms[index].RotationX);
                WriteFloat(payload, ref offset, transforms[index].RotationY);
                WriteFloat(payload, ref offset, transforms[index].RotationZ);
                WriteFloat(payload, ref offset, transforms[index].RotationW);
            }

            for (int index = 0; index < caseDescriptor.StaticBodyCount; ++index)
            {
                WriteStaticBox(payload, ref offset, boxes[index]);
            }
        }

        private static void WriteStaticBox(byte[] payload, ref int offset, UnityPhysicsStaticBox box)
        {
            WriteFloat(payload, ref offset, box.PositionX);
            WriteFloat(payload, ref offset, box.PositionY);
            WriteFloat(payload, ref offset, box.PositionZ);
            WriteFloat(payload, ref offset, box.HalfExtentX);
            WriteFloat(payload, ref offset, box.HalfExtentY);
            WriteFloat(payload, ref offset, box.HalfExtentZ);
        }

        private static uint HeaderCrc(ushort frameType, uint sequence, uint payloadBytes)
        {
            return VisualMagic ^ ((uint)VisualVersion << 16) ^ frameType ^ sequence ^ payloadBytes ^ 0x9e3779b9u;
        }

        private static uint HashText(string value)
        {
            uint hash = 2166136261u;
            byte[] bytes = Encoding.UTF8.GetBytes(value);
            for (int index = 0; index < bytes.Length; ++index)
            {
                hash ^= bytes[index];
                hash *= 16777619u;
            }
            return hash;
        }

        private static string ReadText(byte[] payload, int offset)
        {
            int end = offset;
            int limit = offset + TextCapacity;
            while (end < limit && payload[end] != 0)
            {
                end += 1;
            }
            return Encoding.UTF8.GetString(payload, offset, end - offset);
        }

        private static ushort ReadUInt16(byte[] payload, int offset)
        {
            return BinaryPrimitives.ReadUInt16LittleEndian(payload.AsSpan(offset, 2));
        }

        private static uint ReadUInt32(byte[] payload, int offset)
        {
            return BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(offset, 4));
        }

        private static void WriteText(byte[] payload, ref int offset, string value)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(value);
            int count = math.min(bytes.Length, TextCapacity - 1);
            Array.Copy(bytes, 0, payload, offset, count);
            offset += TextCapacity;
        }

        private static void WriteUInt16(byte[] payload, ref int offset, ushort value)
        {
            BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(offset, 2), value);
            offset += 2;
        }

        private static void WriteUInt32(byte[] payload, ref int offset, uint value)
        {
            BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(offset, 4), value);
            offset += 4;
        }

        private static void WriteFloat(byte[] payload, ref int offset, float value)
        {
            BinaryPrimitives.WriteInt32LittleEndian(payload.AsSpan(offset, 4), BitConverter.SingleToInt32Bits(value));
            offset += 4;
        }

        private static void WriteDouble(byte[] payload, ref int offset, double value)
        {
            BinaryPrimitives.WriteInt64LittleEndian(payload.AsSpan(offset, 8), BitConverter.DoubleToInt64Bits(value));
            offset += 8;
        }
    }
}
