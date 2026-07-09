using BepuPhysics;
using BepuUtilities;
using BepuUtilities.Memory;
using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Text;

namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public struct BepuSharedVisualCli
{
    public string SharedVisualMode;
    public string ConnectHost;
    public string RunToken;
    public string EngineId;
    public string CaseId;
    public int ConnectPort;
    public int ThreadCount;
    public int RepeatIndex;
    public int StepCount;
    public int WarmupSteps;
}

public struct BepuVisualRunConfig
{
    public int ThreadCount;
    public int RepeatIndex;
    public int StepCount;
    public int WarmupSteps;
}

public struct VisualFrame
{
    public ushort FrameType;
    public uint Sequence;
    public byte[] Payload;
}

public struct BepuSharedVisualMode
{
    public const uint Magic = 0x42565354u;
    public const ushort Version = 1;
    public const int MaxPayloadBytes = 2 * 1024 * 1024;
    public const int TextCapacity = 64;
    public const uint StatusOk = 0;
    public const uint StatusVisualFailed = 5;
    public const ushort FrameHello = 1;
    public const ushort FrameStart = 2;
    public const ushort FrameWarmup = 3;
    public const ushort FrameStep = 4;
    public const ushort FrameShutdown = 5;
    public const ushort FrameHelloAck = 6;
    public const ushort FrameStarted = 7;
    public const ushort FrameSnapshot = 8;
    public const ushort FrameCompleted = 9;
    public const int SnapshotHeaderBytes = 360;
    public const int TransformBytes = 28;
    public const int StaticBoxBytes = 24;

    public static int Run(string[] args)
    {
        int parseStatus = ParseArgs(args, out BepuSharedVisualCli cli);
        if (parseStatus != 0)
        {
            return parseStatus;
        }

        TcpClient client = null;
        NetworkStream stream = null;
        BufferPool bufferPool = new();
        Simulation simulation = default;
        ThreadDispatcher threadDispatcher = null;
        try
        {
            int caseStatus = BepuCaseRegistry.Resolve(cli.CaseId, out BepuCaseDescriptor caseDescriptor);
            if (caseStatus != 0)
            {
                return caseStatus;
            }

            client = new TcpClient();
            client.Connect(IPAddress.Loopback, cli.ConnectPort);
            stream = client.GetStream();

            VisualFrame hello = ReceiveFrame(stream, FrameHello, 1);
            if (DecodeHello(cli, hello.Payload) != 0 || SendHelloAck(stream, cli, in caseDescriptor, 1) != 0)
            {
                return 2;
            }

            VisualFrame start = ReceiveFrame(stream, FrameStart, 2);
            if (DecodeStart(cli, start.Payload, out BepuVisualRunConfig config) != 0)
            {
                return 2;
            }

            int requestedWorkerCount = BepuCaseRegistry.RequestedWorkerCount(in caseDescriptor, config.ThreadCount);
            simulation = BepuCaseRegistry.CreateSimulation(in caseDescriptor, bufferPool, requestedWorkerCount, out threadDispatcher);
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
            int warmupStatus = RunWarmup(in caseDescriptor, requestedWorkerCount, warmupSteps);
            if (SendCompleted(stream, FrameCompleted, 3, warmupStatus == 0 ? StatusOk : StatusVisualFailed, 0, 0.0) != 0 || warmupStatus != 0)
            {
                return 2;
            }

            int completedStepCount = 0;
            double physicsElapsedMs = 0.0;
            BepuTransform[] transforms = new BepuTransform[caseDescriptor.DynamicBodyCount];
            BepuStaticBox[] boxes = new BepuStaticBox[caseDescriptor.StaticBodyCount];
            byte[] snapshotPayload = new byte[SnapshotHeaderBytes + caseDescriptor.DynamicBodyCount * TransformBytes + caseDescriptor.StaticBodyCount * StaticBoxBytes];
            for (uint sequence = 4;; ++sequence)
            {
                VisualFrame frame = ReceiveAnyFrame(stream, sequence);
                if (frame.FrameType == FrameShutdown)
                {
                    if (SendCompleted(stream, FrameCompleted, sequence, StatusOk, completedStepCount, physicsElapsedMs) != 0)
                    {
                        Console.Error.WriteLine($"run_failed reason=send_shutdown_completed sequence={sequence}");
                        return 2;
                    }
                    return 0;
                }
                if (frame.FrameType != FrameStep)
                {
                    Console.Error.WriteLine($"run_failed reason=unexpected_frame frame_type={frame.FrameType}");
                    return 2;
                }
                int stepCount = DecodeStep(frame.Payload);
                if (stepCount < 0)
                {
                    return 2;
                }
                Stopwatch stopwatch = Stopwatch.StartNew();
                BepuCaseRegistry.StepSimulation(in caseDescriptor, simulation, threadDispatcher, stepCount);
                stopwatch.Stop();
                physicsElapsedMs += stopwatch.Elapsed.TotalMilliseconds;
                completedStepCount += stepCount;
                BuildSnapshotPayload(in caseDescriptor, config, simulation, completedStepCount, physicsElapsedMs, transforms, boxes, snapshotPayload);
                if (SendFrame(stream, FrameSnapshot, sequence, snapshotPayload) != 0)
                {
                    return 2;
                }
            }
        }
        finally
        {
            simulation?.Dispose();
            threadDispatcher?.Dispose();
            stream?.Dispose();
            client?.Dispose();
            bufferPool.Clear();
        }
    }

    public static int RunWarmup(in BepuCaseDescriptor caseDescriptor, int requestedWorkerCount, int warmupSteps)
    {
        BufferPool warmupPool = new();
        try
        {
            BepuCaseRegistry.RunWarmup(in caseDescriptor, warmupPool, requestedWorkerCount, warmupSteps);
            return 0;
        }
        finally
        {
            warmupPool.Clear();
        }
    }

    public static int ParseArgs(string[] args, out BepuSharedVisualCli cli)
    {
        cli = new BepuSharedVisualCli
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
                cli.SharedVisualMode = arg["--producer-mode=".Length..];
            }
            else if (arg.StartsWith("--connect-host=", StringComparison.Ordinal))
            {
                cli.ConnectHost = arg["--connect-host=".Length..];
            }
            else if (arg.StartsWith("--connect-port=", StringComparison.Ordinal))
            {
                if (ParseNumber("connect-port", arg["--connect-port=".Length..], 1, out cli.ConnectPort) != 0)
                {
                    return 2;
                }
            }
            else if (arg.StartsWith("--run-token=", StringComparison.Ordinal))
            {
                cli.RunToken = arg["--run-token=".Length..];
            }
            else if (arg.StartsWith("--engine=", StringComparison.Ordinal))
            {
                cli.EngineId = arg["--engine=".Length..];
            }
            else if (arg.StartsWith("--case=", StringComparison.Ordinal))
            {
                cli.CaseId = arg["--case=".Length..];
            }
            else if (arg.StartsWith("--thread-count=", StringComparison.Ordinal))
            {
                if (ParseNumber("thread-count", arg["--thread-count=".Length..], 1, out cli.ThreadCount) != 0)
                {
                    return 2;
                }
            }
            else if (arg.StartsWith("--repeat-index=", StringComparison.Ordinal))
            {
                if (ParseNumber("repeat-index", arg["--repeat-index=".Length..], 0, out cli.RepeatIndex) != 0)
                {
                    return 2;
                }
            }
            else if (arg.StartsWith("--step-count=", StringComparison.Ordinal))
            {
                if (ParseNumber("step-count", arg["--step-count=".Length..], 1, out cli.StepCount) != 0)
                {
                    return 2;
                }
            }
            else if (arg.StartsWith("--warmup-steps=", StringComparison.Ordinal))
            {
                if (ParseNumber("warmup-steps", arg["--warmup-steps=".Length..], 0, out cli.WarmupSteps) != 0)
                {
                    return 2;
                }
            }
            else
            {
                Console.Error.WriteLine($"invalid_argument value={arg}");
                return 2;
            }
        }

        if (cli.SharedVisualMode != "shared-visual" ||
            cli.ConnectHost != "127.0.0.1" ||
            cli.EngineId != BepuCaseRegistry.DefaultCase().EngineId ||
            string.IsNullOrWhiteSpace(cli.CaseId) ||
            string.IsNullOrWhiteSpace(cli.RunToken) ||
            cli.ConnectPort <= 0)
        {
            Console.Error.WriteLine("invalid_argument reason=shared_visual_mode_contract");
            return 2;
        }
        return 0;
    }

    public static int ParseNumber(string name, string value, int minimum, out int parsed)
    {
        if (!int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out parsed) || parsed < minimum || parsed > 1_000_000)
        {
            Console.Error.WriteLine($"invalid_argument name={name} value={value}");
            return 2;
        }
        return 0;
    }

    public static VisualFrame ReceiveFrame(NetworkStream stream, ushort expectedFrameType, uint expectedSequence)
    {
        VisualFrame frame = ReceiveAnyFrame(stream, expectedSequence);
        if (frame.FrameType != expectedFrameType)
        {
            throw new InvalidOperationException("unexpected frame");
        }
        return frame;
    }

    public static VisualFrame ReceiveAnyFrame(NetworkStream stream, uint expectedSequence)
    {
        byte[] header = new byte[20];
        stream.ReadExactly(header);
        uint magic = ReadUInt32(header, 0);
        ushort version = ReadUInt16(header, 4);
        ushort frameType = ReadUInt16(header, 6);
        uint sequence = ReadUInt32(header, 8);
        uint payloadBytes = ReadUInt32(header, 12);
        uint crc = ReadUInt32(header, 16);
        if (magic != Magic || version != Version || sequence != expectedSequence || payloadBytes > MaxPayloadBytes || crc != HeaderCrc(frameType, sequence, payloadBytes))
        {
            throw new InvalidOperationException("invalid frame header");
        }
        byte[] payload = new byte[payloadBytes];
        if (payloadBytes > 0)
        {
            stream.ReadExactly(payload);
        }
        return new VisualFrame { FrameType = frameType, Sequence = sequence, Payload = payload };
    }

    public static int SendFrame(NetworkStream stream, ushort frameType, uint sequence, byte[] payload)
    {
        byte[] header = new byte[20];
        int offset = 0;
        WriteUInt32(header, ref offset, Magic);
        WriteUInt16(header, ref offset, Version);
        WriteUInt16(header, ref offset, frameType);
        WriteUInt32(header, ref offset, sequence);
        WriteUInt32(header, ref offset, (uint)payload.Length);
        WriteUInt32(header, ref offset, HeaderCrc(frameType, sequence, (uint)payload.Length));
        stream.Write(header);
        if (payload.Length > 0)
        {
            stream.Write(payload);
        }
        return 0;
    }

    public static int SendHelloAck(NetworkStream stream, BepuSharedVisualCli cli, in BepuCaseDescriptor caseDescriptor, uint sequence)
    {
        byte[] payload = new byte[208];
        int offset = 0;
        WriteText(payload, ref offset, caseDescriptor.EngineId);
        WriteText(payload, ref offset, caseDescriptor.CaseId);
        WriteText(payload, ref offset, "BepuPhysics2.PolygonRunner");
        WriteUInt32(payload, ref offset, (uint)Environment.ProcessId);
        WriteUInt32(payload, ref offset, 0);
        WriteUInt32(payload, ref offset, HashText(cli.RunToken));
        WriteUInt32(payload, ref offset, StatusOk);
        return SendFrame(stream, FrameHelloAck, sequence, payload);
    }

    public static int SendCompleted(NetworkStream stream, ushort frameType, uint sequence, uint status, int completedStepCount, double physicsElapsedMs)
    {
        byte[] payload = new byte[16];
        int offset = 0;
        WriteUInt32(payload, ref offset, status);
        WriteUInt32(payload, ref offset, (uint)completedStepCount);
        WriteDouble(payload, ref offset, physicsElapsedMs);
        return SendFrame(stream, frameType, sequence, payload);
    }

    public static int DecodeHello(BepuSharedVisualCli cli, byte[] payload)
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

    public static int DecodeStart(BepuSharedVisualCli cli, byte[] payload, out BepuVisualRunConfig config)
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

    public static int DecodeStep(byte[] payload)
    {
        return payload.Length == 4 ? (int)ReadUInt32(payload, 0) : -1;
    }

    public static void BuildSnapshotPayload(in BepuCaseDescriptor caseDescriptor, BepuVisualRunConfig config, Simulation simulation, int completedStepCount, double physicsElapsedMs, BepuTransform[] transforms, BepuStaticBox[] boxes, byte[] payload)
    {
        if (BepuCaseRegistry.SampleTransforms(in caseDescriptor, simulation, transforms) != 0 ||
            BepuCaseRegistry.CopyStaticBoxes(in caseDescriptor, boxes) != 0)
        {
            throw new InvalidOperationException("snapshot sample failed");
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
            WriteFloat(payload, ref offset, boxes[index].PositionX);
            WriteFloat(payload, ref offset, boxes[index].PositionY);
            WriteFloat(payload, ref offset, boxes[index].PositionZ);
            WriteFloat(payload, ref offset, boxes[index].HalfExtentX);
            WriteFloat(payload, ref offset, boxes[index].HalfExtentY);
            WriteFloat(payload, ref offset, boxes[index].HalfExtentZ);
        }
    }

    public static uint HeaderCrc(ushort frameType, uint sequence, uint payloadBytes)
    {
        return Magic ^ ((uint)Version << 16) ^ frameType ^ sequence ^ payloadBytes ^ 0x9e3779b9u;
    }

    public static uint HashText(string value)
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

    public static string ReadText(byte[] payload, int offset)
    {
        int end = offset;
        int limit = offset + TextCapacity;
        while (end < limit && payload[end] != 0)
        {
            end += 1;
        }
        return Encoding.UTF8.GetString(payload, offset, end - offset);
    }

    public static ushort ReadUInt16(byte[] payload, int offset)
    {
        return BinaryPrimitives.ReadUInt16LittleEndian(payload.AsSpan(offset, 2));
    }

    public static uint ReadUInt32(byte[] payload, int offset)
    {
        return BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(offset, 4));
    }

    public static void WriteText(byte[] payload, ref int offset, string value)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(value);
        int count = Math.Min(bytes.Length, TextCapacity - 1);
        bytes.AsSpan(0, count).CopyTo(payload.AsSpan(offset, count));
        offset += TextCapacity;
    }

    public static void WriteUInt16(byte[] payload, ref int offset, ushort value)
    {
        BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(offset, 2), value);
        offset += 2;
    }

    public static void WriteUInt32(byte[] payload, ref int offset, uint value)
    {
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(offset, 4), value);
        offset += 4;
    }

    public static void WriteFloat(byte[] payload, ref int offset, float value)
    {
        BinaryPrimitives.WriteInt32LittleEndian(payload.AsSpan(offset, 4), BitConverter.SingleToInt32Bits(value));
        offset += 4;
    }

    public static void WriteDouble(byte[] payload, ref int offset, double value)
    {
        BinaryPrimitives.WriteInt64LittleEndian(payload.AsSpan(offset, 8), BitConverter.DoubleToInt64Bits(value));
        offset += 8;
    }
}
