using System.Buffers.Binary;
using System.Diagnostics;
using System.IO.Pipes;
using System.Text;

namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public enum StackCapturePhase : uint
{
    Construction,
    Warmup,
    Measured
}

public enum StackCaptureReply : uint
{
    Accepted = 0,
    Rejected = 1
}

public struct StackCapture
{
    public NamedPipeClientStream Pipe;
    public byte[] ReplyBuffer;
    public byte[] Buffer;
    public BepuVisualStableTransform[] Transforms;
    public uint FrameCount;
    public double ElapsedMs;
}

public static class StackStateCapture
{
    public static int Open(in BepuRunnerArgs args, out StackCapture capture)
    {
        capture = default;
        CaseExecutionSpec execution = args.CaseExecution;
        capture.Transforms = new BepuVisualStableTransform[execution.DynamicBodyCount];
        capture.Buffer = new byte[checked(12 + (int)execution.DynamicBodyCount * 32)];
        byte[] header = new byte[236];
        Encoding.ASCII.GetBytes("BPSTACK", header);
        int offset = 8;
        WriteText(header, ref offset, CaseExecutionWire.TextValue(execution.CaseId));
        WriteText(header, ref offset, CaseExecutionWire.TextValue(execution.FixtureSemantic));
        WriteText(header, ref offset, args.CaseRegistration.Descriptor.EngineId);
        WriteU32(header, ref offset, (uint)execution.FixtureKind);
        WriteU32(header, ref offset, execution.FixtureRevision);
        WriteU32(header, ref offset, execution.DynamicBodyCount);
        WriteU32(header, ref offset, execution.DynamicBodyCount -
            (execution.FixtureKind == CaseFixtureKind.LargePyramid ? execution.LargePyramid.ProjectileCount : 0));
        WriteU32(header, ref offset, (uint)args.WorkerCount);
        WriteU32(header, ref offset, (uint)args.RepeatIndex);
        WriteU32(header, ref offset, (uint)args.WarmupSteps);
        WriteU32(header, ref offset, (uint)args.StepCount);
        WriteU32(header, ref offset, execution.TimestepHz);
        try
        {
            capture.Pipe = new NamedPipeClientStream(".", args.StackStream.Substring(9), PipeDirection.InOut, PipeOptions.None);
            capture.ReplyBuffer = new byte[4];
            capture.Pipe.Connect();
            return Exchange(ref capture, header);
        }
        catch (IOException)
        {
            Abort(ref capture);
            return 2;
        }
    }

    public static int Append(ref StackCapture capture, in BepuCaseRegistration registration,
        BepuCaseView state, StackCapturePhase phase, uint segment, uint step)
    {
        long start = Stopwatch.GetTimestamp();
        if (registration.SampleVisualTransforms(state, capture.Transforms) != 0) return 2;
        int offset = 0;
        byte[] buffer = capture.Buffer;
        WriteU32(buffer, ref offset, (uint)phase);
        WriteU32(buffer, ref offset, segment);
        WriteU32(buffer, ref offset, step);
        for (int index = 0; index < capture.Transforms.Length; ++index)
        {
            BepuVisualStableTransform pose = capture.Transforms[index];
            WriteU32(buffer, ref offset, pose.StableSlot);
            WriteFloat(buffer, ref offset, pose.Transform.PositionX);
            WriteFloat(buffer, ref offset, pose.Transform.PositionY);
            WriteFloat(buffer, ref offset, pose.Transform.PositionZ);
            WriteFloat(buffer, ref offset, pose.Transform.RotationX);
            WriteFloat(buffer, ref offset, pose.Transform.RotationY);
            WriteFloat(buffer, ref offset, pose.Transform.RotationZ);
            WriteFloat(buffer, ref offset, pose.Transform.RotationW);
        }
        try
        {
            if (Exchange(ref capture, buffer) != 0) return 2;
            ++capture.FrameCount;
            capture.ElapsedMs += (Stopwatch.GetTimestamp() - start) * 1000.0 / Stopwatch.Frequency;
            return 0;
        }
        catch (IOException)
        {
            Abort(ref capture);
            return 2;
        }
    }

    public static int Close(ref StackCapture capture)
    {
        try
        {
            byte[] footer = new byte[16];
            int offset = 0;
            WriteU32(footer, ref offset, 3);
            WriteU32(footer, ref offset, capture.FrameCount);
            BinaryPrimitives.WriteInt64LittleEndian(footer.AsSpan(8), BitConverter.DoubleToInt64Bits(capture.ElapsedMs));
            int status = Exchange(ref capture, footer);
            capture.Pipe.Dispose();
            capture.Pipe = null;
            return status;
        }
        catch (IOException)
        {
            Abort(ref capture);
            return 2;
        }
    }

    public static void Abort(ref StackCapture capture)
    {
        capture.Pipe?.Dispose();
        capture.Pipe = null;
    }

    public static int Exchange(ref StackCapture capture, byte[] bytes)
    {
        capture.Pipe.Write(bytes, 0, bytes.Length);
        int offset = 0;
        while (offset < capture.ReplyBuffer.Length)
        {
            int received = capture.Pipe.Read(capture.ReplyBuffer, offset, capture.ReplyBuffer.Length - offset);
            if (received == 0) return 2;
            offset += received;
        }
        return BinaryPrimitives.ReadUInt32LittleEndian(capture.ReplyBuffer) == (uint)StackCaptureReply.Accepted ? 0 : 2;
    }

    public static void WriteText(byte[] buffer, ref int offset, string value)
    {
        Encoding.ASCII.GetBytes(value, buffer.AsSpan(offset, 64));
        offset += 64;
    }

    public static void WriteU32(byte[] buffer, ref int offset, uint value)
    {
        BinaryPrimitives.WriteUInt32LittleEndian(buffer.AsSpan(offset, 4), value);
        offset += 4;
    }

    public static void WriteFloat(byte[] buffer, ref int offset, float value)
    {
        BinaryPrimitives.WriteInt32LittleEndian(buffer.AsSpan(offset, 4), BitConverter.SingleToInt32Bits(value));
        offset += 4;
    }
}
