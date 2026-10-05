using BepuPhysics;
using BepuPhysics.Constraints;
using BepuUtilities;
using BepuUtilities.Memory;

namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public struct BepuCaseView
{
    public CaseExecutionSpec CaseExecution;
    public Simulation Simulation;
    public BodyHandle[] DynamicBodies;
    public BepuSpatialQueryState QueryState;
}

public struct BepuVisualTransform
{
    public float PositionX;
    public float PositionY;
    public float PositionZ;
    public float RotationX;
    public float RotationY;
    public float RotationZ;
    public float RotationW;
}

public struct BepuVisualGeometry
{
    public uint Kind;
    public float ParameterX;
    public float ParameterY;
    public float ParameterZ;
    public uint VertexOffset;
    public uint VertexCount;
    public uint IndexOffset;
    public uint IndexCount;
    public uint EdgeOffset;
    public uint EdgeCount;
}

public struct BepuVisualMeshStorage
{
    public float[] Vertices;
    public uint[] Indices;
    public uint[] Edges;
    public int VertexCount;
    public int IndexCount;
    public int EdgeCount;
}

public struct BepuVisualInstance
{
    public uint GeometryIndex;
    public uint StableSlot;
    public uint TransformSlot;
    public BepuVisualTransform InitialTransform;
}

public struct BepuVisualStableTransform
{
    public uint StableSlot;
    public BepuVisualTransform Transform;
}

public struct BepuVisualDebugPrimitive
{
    public uint Kind;
    public uint MaterialIndex;
    public float OriginOrCenterX;
    public float OriginOrCenterY;
    public float OriginOrCenterZ;
    public float EndOrHalfExtentsX;
    public float EndOrHalfExtentsY;
    public float EndOrHalfExtentsZ;
    public float Radius;
    public uint Reserved;
}

public struct BepuCaseDescriptor
{
    public string EngineId;
}
