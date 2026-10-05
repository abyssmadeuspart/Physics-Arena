using Unity.Collections;
using Unity.Physics;

namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
    public struct UnityPhysicsCaseDescriptor
    {
        public string EngineId;
    }

    public struct UnityPhysicsCaseView
    {
        public CaseExecutionSpec Execution;
        public PhysicsWorld World;
        public NativeArray<RaycastInput> RayInputs;
        public NativeArray<ColliderCastInput> SphereCastInputs;
        public NativeArray<OverlapAabbInput> OverlapInputs;
        public NativeArray<byte> DebugHits;
        public NativeArray<float> DebugHitDistances;
    }

    public struct UnityPhysicsTransform
    {
        public float PositionX;
        public float PositionY;
        public float PositionZ;
        public float RotationX;
        public float RotationY;
        public float RotationZ;
        public float RotationW;
    }

    public struct UnityPhysicsVisualGeometry
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

    public struct UnityPhysicsVisualMeshStorage
    {
        public float[] Vertices;
        public uint[] Indices;
        public uint[] Edges;
        public int VertexCount;
        public int IndexCount;
        public int EdgeCount;
    }

    public struct UnityPhysicsVisualInstance
    {
        public uint GeometryIndex;
        public uint StableSlot;
        public uint TransformSlot;
        public UnityPhysicsTransform InitialTransform;
    }

    public struct UnityPhysicsVisualStableTransform
    {
        public uint StableSlot;
        public UnityPhysicsTransform Transform;
    }

    public struct UnityPhysicsVisualDebugPrimitive
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

}
