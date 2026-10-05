using System;
using System.IO;
using System.Buffers.Binary;
using Unity.Mathematics;
using System.Globalization;
using System.Text;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Bas3D.BenchmarkPolygon.UnityPhysics
{

public enum UnityPhysicsRecordingState : byte
{
    Closed,
    Open,
    Failed,
    Complete
}

public struct UnityPhysicsRecordingWriter
{
    public FileStream File;
    public string FinalPath;
    public string PartialPath;
    public byte[] FrameBuffer;
    public UnityPhysicsVisualStableTransform[] Transforms;
    public UnityPhysicsVisualDebugPrimitive[] Debug;
    public ulong ExpectedFrames;
    public ulong WrittenFrames;
    public ulong FileBytes;
    public UnityPhysicsRecordingState State;
}

public static class UnityPhysicsRecording
{
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern int CloseHandle(IntPtr handle);

    public static int Begin(in RunnerArgs args, ref UnityPhysicsCaseView state, out UnityPhysicsRecordingWriter writer)
    {
        writer = default;
        CaseExecutionSpec execution = args.CaseExecution;
        if (execution.VisualInstanceCount == 0 || execution.VisualInstanceCount > 16291 ||
            execution.DynamicBodyCount > 16290 || execution.VisualDebugPrimitiveCount > 768 ||
            args.StepCount <= 0 || args.StepCount > 100000 || Path.GetExtension(args.RecordingPath) != ".bpr") return 2;
        writer.FinalPath = args.RecordingPath;
        writer.PartialPath = args.RecordingPath + ".partial";
        if (File.Exists(writer.FinalPath)) return 2;
        UnityPhysicsVisualGeometry[] geometries = new UnityPhysicsVisualGeometry[64];
        UnityPhysicsVisualInstance[] instances = new UnityPhysicsVisualInstance[execution.VisualInstanceCount];
        UnityPhysicsVisualMeshStorage meshes = UnityPhysicsBenchmarkRunner.CreateVisualMeshStorage(in execution);
        if (UnityPhysicsBenchmarkRunner.BuildUnityPhysicsVisualScene(args.CaseRegistration, ref state, geometries, ref meshes, instances,
            out int geometryCount, out int instanceCount) != 0 || geometryCount <= 0 || geometryCount > 64 ||
            instanceCount <= 0 || instanceCount > instances.Length) return 2;
        ulong sceneBytes = 120ul + (ulong)geometryCount * 40 + (ulong)instanceCount * 44 +
            (ulong)meshes.VertexCount * 12 + (ulong)meshes.IndexCount * 4 + (ulong)meshes.EdgeCount * 8;
        ulong framesOffset = 384 + sceneBytes;
        ulong frameStride = 8ul + (ulong)execution.DynamicBodyCount * 32 + (ulong)execution.VisualDebugPrimitiveCount * 40;
        writer.ExpectedFrames = (ulong)args.StepCount + 1;
        ulong trailerOffset = checked(framesOffset + frameStride * writer.ExpectedFrames);
        writer.FileBytes = checked(trailerOffset + 24);
        writer.FrameBuffer = new byte[checked((int)frameStride)];
        writer.Transforms = new UnityPhysicsVisualStableTransform[execution.DynamicBodyCount];
        writer.Debug = new UnityPhysicsVisualDebugPrimitive[execution.VisualDebugPrimitiveCount];
        byte[] payload = new byte[checked((int)framesOffset)];
        Encoding.ASCII.GetBytes("BPREPLAY", 0, 8, payload, 0);
        int offset = 8;
        WriteUInt32(payload, ref offset, 1); WriteUInt32(payload, ref offset, 384);
        WriteText(payload, ref offset, execution.CaseId.ToString());
        WriteText(payload, ref offset, args.CaseRegistration.Descriptor.EngineId);
        WriteText(payload, ref offset, execution.FixtureSemantic.ToString());
        WriteText(payload, ref offset, execution.FixtureRevision.ToString(CultureInfo.InvariantCulture));
        WriteUInt32(payload, ref offset, (uint)args.ThreadCount); WriteUInt32(payload, ref offset, (uint)args.RepeatIndex);
        WriteUInt32(payload, ref offset, (uint)args.StepCount); WriteUInt32(payload, ref offset, (uint)args.WarmupSteps);
        WriteUInt32(payload, ref offset, execution.BodyCount); WriteUInt32(payload, ref offset, execution.ShapeCount);
        uint kind = execution.FixtureKind == CaseFixtureKind.SpatialQueryTrace ? 2u : 1u;
        WriteUInt32(payload, ref offset, kind);
        WriteUInt64(payload, ref offset, (ulong)BitConverter.DoubleToInt64Bits(kind == 2 ? 0 : 1.0 / execution.TimestepHz));
        WriteUInt32(payload, ref offset, (uint)geometryCount); WriteUInt32(payload, ref offset, (uint)instanceCount);
        WriteUInt32(payload, ref offset, execution.DynamicBodyCount); WriteUInt32(payload, ref offset, execution.VisualDebugPrimitiveCount);
        WriteUInt32(payload, ref offset, (uint)meshes.VertexCount); WriteUInt32(payload, ref offset, (uint)meshes.IndexCount);
        WriteUInt32(payload, ref offset, (uint)meshes.EdgeCount);
        WriteUInt64(payload, ref offset, 384); WriteUInt64(payload, ref offset, sceneBytes);
        WriteUInt64(payload, ref offset, framesOffset); WriteUInt64(payload, ref offset, frameStride);
        WriteUInt64(payload, ref offset, writer.ExpectedFrames); WriteUInt64(payload, ref offset, trailerOffset);
        WriteVector(payload, ref offset, execution.ReplayCamera.Direction);
        WriteVector(payload, ref offset, execution.ReplayCamera.Up);
        WriteVector(payload, ref offset, execution.ReplayCamera.Minimum);
        WriteVector(payload, ref offset, execution.ReplayCamera.Maximum);
        WriteVector(payload, ref offset, execution.ReplayCamera.Eye);
        WriteVector(payload, ref offset, execution.ReplayCamera.Target);
        WriteVector(payload, ref offset, execution.ReplayCamera.EyeOffset);
        WriteVector(payload, ref offset, execution.ReplayCamera.TargetOffset);
        WriteFloat(payload, ref offset, execution.ReplayCamera.VerticalFovDegrees);
        WriteFloat(payload, ref offset, execution.ReplayCamera.ViewportFill);
        WriteFloat(payload, ref offset, execution.ReplayCamera.NearPlane);
        WriteFloat(payload, ref offset, execution.ReplayCamera.FarPlane);
        WriteUInt32(payload, ref offset, execution.ReplayCamera.StableSlot); WriteUInt32(payload, ref offset, execution.ReplayCamera.Mode);
        for (int index = 0; index < geometryCount; ++index)
        {
            WriteUInt32(payload, ref offset, geometries[index].Kind);
            WriteFloat(payload, ref offset, geometries[index].ParameterX);
            WriteFloat(payload, ref offset, geometries[index].ParameterY);
            WriteFloat(payload, ref offset, geometries[index].ParameterZ);
            WriteUInt32(payload, ref offset, geometries[index].VertexOffset);
            WriteUInt32(payload, ref offset, geometries[index].VertexCount);
            WriteUInt32(payload, ref offset, geometries[index].IndexOffset);
            WriteUInt32(payload, ref offset, geometries[index].IndexCount);
            WriteUInt32(payload, ref offset, geometries[index].EdgeOffset);
            WriteUInt32(payload, ref offset, geometries[index].EdgeCount);
        }
        for (int index = 0; index < instanceCount; ++index)
        {
            UnityPhysicsVisualInstance instance = instances[index];
            WriteUInt32(payload, ref offset, instance.GeometryIndex);
            WriteUInt32(payload, ref offset, instance.StableSlot);
            WriteUInt32(payload, ref offset, 0);
            WriteUInt32(payload, ref offset, instance.TransformSlot);
            WriteVisualTransform(payload, ref offset, instance.InitialTransform);
        }
        for (int index = 0; index < meshes.VertexCount * 3; ++index)
            WriteFloat(payload, ref offset, meshes.Vertices[index]);
        for (int index = 0; index < meshes.IndexCount; ++index)
            WriteUInt32(payload, ref offset, meshes.Indices[index]);
        for (int index = 0; index < meshes.EdgeCount * 2; ++index)
            WriteUInt32(payload, ref offset, meshes.Edges[index]);
        if (offset != payload.Length) return 2;
        try
        {
            writer.File = new FileStream(writer.PartialPath, FileMode.CreateNew, FileAccess.Write, FileShare.Read,
                65536, FileOptions.SequentialScan);
            writer.File.Write(payload, 0, payload.Length);
            writer.State = UnityPhysicsRecordingState.Open;
            return Append(ref writer, in args.CaseRegistration, ref state, 0);
        }
        catch (IOException error)
        {
            return Fail(ref writer, error);
        }
        catch (UnauthorizedAccessException error)
        {
            return Fail(ref writer, error);
        }
    }

    public static int Append(ref UnityPhysicsRecordingWriter writer, in UnityPhysicsCaseRegistration registration,
        ref UnityPhysicsCaseView state, ulong ordinal)
    {
        if (writer.State != UnityPhysicsRecordingState.Open || ordinal != writer.WrittenFrames || ordinal >= writer.ExpectedFrames)
        {
            writer.State = UnityPhysicsRecordingState.Failed;
            return 2;
        }
        writer.State = UnityPhysicsRecordingState.Failed;
        if (UnityPhysicsBenchmarkRunner.SampleUnityPhysicsTransforms(ref state.World, writer.Transforms) != 0 ||
            UnityPhysicsBenchmarkRunner.BuildUnityPhysicsVisualDebugPrimitives(registration, ref state, writer.Debug) != 0) return 2;
        int offset = 0;
        byte[] payload = writer.FrameBuffer;
        WriteUInt64(payload, ref offset, ordinal);
        for (int index = 0; index < writer.Transforms.Length; ++index)
        {
            WriteUInt32(payload, ref offset, writer.Transforms[index].StableSlot);
            WriteVisualTransform(payload, ref offset, writer.Transforms[index].Transform);
        }
        for (int index = 0; index < writer.Debug.Length; ++index)
        {
            UnityPhysicsVisualDebugPrimitive value = writer.Debug[index];
            WriteUInt32(payload, ref offset, value.Kind); WriteUInt32(payload, ref offset, value.MaterialIndex);
            WriteFloat(payload, ref offset, value.OriginOrCenterX); WriteFloat(payload, ref offset, value.OriginOrCenterY);
            WriteFloat(payload, ref offset, value.OriginOrCenterZ); WriteFloat(payload, ref offset, value.EndOrHalfExtentsX);
            WriteFloat(payload, ref offset, value.EndOrHalfExtentsY); WriteFloat(payload, ref offset, value.EndOrHalfExtentsZ);
            WriteFloat(payload, ref offset, value.Radius); WriteUInt32(payload, ref offset, value.Reserved);
        }
        try
        {
            writer.File.Write(payload, 0, payload.Length);
            ++writer.WrittenFrames;
            writer.State = UnityPhysicsRecordingState.Open;
            return 0;
        }
        catch (IOException error)
        {
            return Fail(ref writer, error);
        }
    }

    public static int Complete(ref UnityPhysicsRecordingWriter writer)
    {
        if (writer.State != UnityPhysicsRecordingState.Open || writer.WrittenFrames != writer.ExpectedFrames) return 2;
        writer.State = UnityPhysicsRecordingState.Failed;
        try
        {
            byte[] trailer = new byte[24];
            Encoding.ASCII.GetBytes("BPRDONE1", 0, 8, trailer, 0);
            int offset = 8;
            WriteUInt64(trailer, ref offset, writer.WrittenFrames); WriteUInt64(trailer, ref offset, writer.FileBytes);
            writer.File.Write(trailer, 0, trailer.Length);
            writer.File.Flush();
            SafeFileHandle fileHandle = writer.File.SafeFileHandle;
            IntPtr nativeHandle = fileHandle.DangerousGetHandle();
            fileHandle.SetHandleAsInvalid();
            int closeStatus = 0;
            try
            {
                writer.File.Dispose();
            }
            finally
            {
                writer.File = null;
                closeStatus = CloseHandle(nativeHandle);
            }
            if (closeStatus == 0) throw new IOException("Recording file close failed", Marshal.GetLastWin32Error());
            File.Move(writer.PartialPath, writer.FinalPath);
            writer.State = UnityPhysicsRecordingState.Complete;
            return 0;
        }
        catch (IOException error)
        {
            return Fail(ref writer, error);
        }
        catch (UnauthorizedAccessException error)
        {
            return Fail(ref writer, error);
        }
    }

    public static void Abort(ref UnityPhysicsRecordingWriter writer)
    {
        if (writer.File == null) return;
        try
        {
            writer.File.Dispose();
        }
        catch (IOException error)
        {
            Console.Error.WriteLine($"run_failed reason=recording_close path={writer.PartialPath} error={error.Message}");
        }
        writer.File = null;
        writer.State = UnityPhysicsRecordingState.Failed;
    }

    public static int Fail(ref UnityPhysicsRecordingWriter writer, Exception error)
    {
        Console.Error.WriteLine($"run_failed reason=recording_io path={writer.PartialPath} error={error.Message}");
        Abort(ref writer);
        writer.State = UnityPhysicsRecordingState.Failed;
        return 2;
    }

    public static void WriteUInt32(byte[] output, ref int offset, uint value)
    {
        BinaryPrimitives.WriteUInt32LittleEndian(output.AsSpan(offset, 4), value);
        offset += 4;
    }

    public static void WriteUInt64(byte[] output, ref int offset, ulong value)
    {
        BinaryPrimitives.WriteUInt64LittleEndian(output.AsSpan(offset, 8), value);
        offset += 8;
    }

    public static void WriteFloat(byte[] output, ref int offset, float value)
    {
        WriteUInt32(output, ref offset, (uint)BitConverter.SingleToInt32Bits(value));
    }

    public static void WriteText(byte[] output, ref int offset, string value)
    {
        if (value.Length == 0 || Encoding.UTF8.GetByteCount(value) >= 64) throw new InvalidDataException("Recording identity exceeds its fixed field");
        Encoding.UTF8.GetBytes(value, 0, value.Length, output, offset);
        offset += 64;
    }

    public static void WriteVector(byte[] output, ref int offset, float3 value)
    {
        WriteFloat(output, ref offset, value.x); WriteFloat(output, ref offset, value.y); WriteFloat(output, ref offset, value.z);
    }

    public static void WriteVisualTransform(byte[] output, ref int offset, in UnityPhysicsTransform value)
    {
        WriteFloat(output, ref offset, value.PositionX); WriteFloat(output, ref offset, value.PositionY);
        WriteFloat(output, ref offset, value.PositionZ); WriteFloat(output, ref offset, value.RotationX);
        WriteFloat(output, ref offset, value.RotationY); WriteFloat(output, ref offset, value.RotationZ);
        WriteFloat(output, ref offset, value.RotationW);
    }
}
}
