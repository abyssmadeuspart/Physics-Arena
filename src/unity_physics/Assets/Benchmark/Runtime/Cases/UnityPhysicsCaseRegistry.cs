using System;
using Unity.Collections;
using Unity.Entities;
using Unity.Mathematics;
using Unity.Physics;
using PhysicsCollider = Unity.Physics.Collider;

namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
    public delegate int UnityPhysicsBuildVisualSceneCallback(
        ref UnityPhysicsCaseView state,
        UnityPhysicsVisualGeometry[] geometries,

        ref UnityPhysicsVisualMeshStorage meshes,
        UnityPhysicsVisualInstance[] instances,
        out int geometryCount,
        out int instanceCount);
    public delegate int UnityPhysicsBuildVisualDebugPrimitivesCallback(
        ref UnityPhysicsCaseView state,
        UnityPhysicsVisualDebugPrimitive[] primitives);

    public struct UnityPhysicsCaseRegistration
    {
        public UnityPhysicsCaseDescriptor Descriptor;
        public Func<RunnerArgs, int> RunHeadless;
        public UnityPhysicsBuildVisualSceneCallback BuildVisualScene;
        public UnityPhysicsBuildVisualDebugPrimitivesCallback BuildVisualDebugPrimitives;
    }

    public static partial class UnityPhysicsBenchmarkRunner
    {
        public static quaternion UnityPhysicsShapeRotation(CaseExecutionAxis axis)
        {
            return axis switch
            {
                CaseExecutionAxis.X => quaternion.RotateZ(-math.PI * 0.5f),
                CaseExecutionAxis.Z => quaternion.RotateX(math.PI * 0.5f),
                _ => quaternion.identity
            };
        }

        public static BlobAssetReference<PhysicsCollider> CreateUnityPhysicsResolvedCollider(
            in CaseExecutionSpec execution, in CaseExecutionGeometry geometry,
            CollisionFilter filter, Material material)
        {
            switch (geometry.Shape)
            {
                case CaseExecutionShape.Box:
                    return BoxCollider.Create(new BoxGeometry
                    {
                        Center = float3.zero,
                        Orientation = quaternion.identity,
                        Size = geometry.HalfExtents * 2f,
                        BevelRadius = 0f
                    }, filter, material);
                case CaseExecutionShape.Sphere:
                    return SphereCollider.Create(new SphereGeometry
                    {
                        Center = float3.zero,
                        Radius = geometry.Radius
                    }, filter, material);
                case CaseExecutionShape.Capsule:
                    return CapsuleCollider.Create(new CapsuleGeometry
                    {
                        Vertex0 = new float3(0f, -geometry.HalfSegment, 0f),
                        Vertex1 = new float3(0f, geometry.HalfSegment, 0f),
                        Radius = geometry.Radius
                    }, filter, material);
                case CaseExecutionShape.ConvexHull:
                    NativeArray<float3> points = new NativeArray<float3>(24, Allocator.Temp);
                    try
                    {
                        for (int index = 0; index < points.Length; ++index)
                            points[index] = execution.HullPoints[index] * geometry.HalfExtents;
                        ConvexHullGenerationParameters parameters = new ConvexHullGenerationParameters
                        {
                            BevelRadius = 0f,
                            SimplificationTolerance = 0f,
                            MinimumAngle = 0f
                        };
                        return ConvexCollider.Create(points, parameters, filter, material);
                    }
                    finally
                    {
                        points.Dispose();
                    }
                default:
                    throw new InvalidOperationException("Unsupported resolved shape.");
            }
        }

        public static int BuildResolvedVisualGeometry(in CaseExecutionSpec execution,
            in CaseExecutionGeometry geometry, ref UnityPhysicsVisualMeshStorage meshes, out UnityPhysicsVisualGeometry visual)
        {
            visual = default;
            if (geometry.Shape == CaseExecutionShape.Box)
            {
                visual.Kind = 2;
                visual.ParameterX = geometry.HalfExtents.x;
                visual.ParameterY = geometry.HalfExtents.y;
                visual.ParameterZ = geometry.HalfExtents.z;
                return 0;
            }
            if (geometry.Shape == CaseExecutionShape.Sphere)
            {
                visual.Kind = 1;
                visual.ParameterX = geometry.Radius;
                return 0;
            }
            if (geometry.Shape != CaseExecutionShape.Capsule && geometry.Shape != CaseExecutionShape.ConvexHull) return 2;
            int vertexCount = geometry.Shape == CaseExecutionShape.Capsule ? 130 : 24;
            int indexCount = geometry.Shape == CaseExecutionShape.Capsule ? 768 : 132;
            int edgeCount = geometry.Shape == CaseExecutionShape.Capsule ? 272 : 36;
            if (meshes.VertexCount + vertexCount > 8192 || meshes.IndexCount + indexCount > 49152 ||
                meshes.EdgeCount + edgeCount > 24576) return 2;
            if (meshes.Vertices == null || (meshes.VertexCount + vertexCount) * 3 > meshes.Vertices.Length ||
                meshes.Indices == null || meshes.IndexCount + indexCount > meshes.Indices.Length ||
                meshes.Edges == null || (meshes.EdgeCount + edgeCount) * 2 > meshes.Edges.Length) return 2;
            visual.Kind = 3;
            visual.VertexOffset = (uint)meshes.VertexCount;
            visual.IndexOffset = (uint)meshes.IndexCount;
            visual.EdgeOffset = (uint)meshes.EdgeCount;
            if (geometry.Shape == CaseExecutionShape.Capsule)
            {
                AppendVisualVertex(ref meshes, 0f, geometry.HalfSegment + geometry.Radius, 0f);
                for (int ring = 0; ring < 8; ++ring)
                {
                    float latitude = (ring < 4 ? ring + 1 : ring) * MathF.PI / 8f;
                    float y = (ring < 4 ? geometry.HalfSegment : -geometry.HalfSegment) + geometry.Radius * MathF.Cos(latitude);
                    float radial = geometry.Radius * MathF.Sin(latitude);
                    for (int longitude = 0; longitude < 16; ++longitude)
                    {
                        float angle = longitude * MathF.PI / 8f;
                        AppendVisualVertex(ref meshes, radial * MathF.Cos(angle), y, radial * MathF.Sin(angle));
                    }
                }
                AppendVisualVertex(ref meshes, 0f, -geometry.HalfSegment - geometry.Radius, 0f);
                for (uint longitude = 0; longitude < 16; ++longitude)
                {
                    uint next = (longitude + 1) % 16;
                    AppendVisualTriangle(ref meshes, 0, 1 + next, 1 + longitude);
                    AppendVisualEdge(ref meshes, 0, 1 + longitude);
                    for (uint ring = 0; ring < 8; ++ring)
                    {
                        uint current = 1 + ring * 16;
                        AppendVisualEdge(ref meshes, current + longitude, current + next);
                        if (ring < 7)
                        {
                            uint lower = current + 16;
                            AppendVisualTriangle(ref meshes, current + longitude, current + next, lower + longitude);
                            AppendVisualTriangle(ref meshes, current + next, lower + next, lower + longitude);
                            AppendVisualEdge(ref meshes, current + longitude, lower + longitude);
                        }
                    }
                    AppendVisualTriangle(ref meshes, 113 + longitude, 113 + next, 129);
                    AppendVisualEdge(ref meshes, 113 + longitude, 129);
                }
            }
            else
            {
                for (int index = 0; index < 24; ++index)
                {
                    float3 point = execution.HullPoints[index];
                    AppendVisualVertex(ref meshes, point.x * geometry.HalfExtents.x,
                        point.y * geometry.HalfExtents.y, point.z * geometry.HalfExtents.z);
                }
                Span<uint> corners = stackalloc uint[8];
                Span<float> angles = stackalloc float[8];
                Span<float> components = stackalloc float[3];
                for (int face = 0; face < 14; ++face)
                {
                    int count = 0;
                    if (face < 6)
                    {
                        int axis = face / 2;
                        float sign = (face & 1) != 0 ? 1f : -1f;
                        for (int index = 0; index < 24; ++index)
                        {
                            float3 point = execution.HullPoints[index];
                            components[0] = point.x;
                            components[1] = point.y;
                            components[2] = point.z;
                            if (components[axis] != sign) continue;
                            if (count == 8) return 2;
                            float angle = sign * MathF.Atan2(components[(axis + 2) % 3], components[(axis + 1) % 3]);
                            int insertion = count++;
                            while (insertion != 0 && angles[insertion - 1] > angle)
                            {
                                corners[insertion] = corners[insertion - 1];
                                angles[insertion] = angles[insertion - 1];
                                --insertion;
                            }
                            corners[insertion] = (uint)index;
                            angles[insertion] = angle;
                        }
                        if (count != 8) return 2;
                    }
                    else
                    {
                        uint signs = (uint)(face - 6);
                        uint positiveCount = (signs & 1) + ((signs >> 1) & 1) + ((signs >> 2) & 1);
                        corners[0] = signs;
                        corners[1] = (positiveCount & 1) != 0 ? 8 + signs : 16 + signs;
                        corners[2] = (positiveCount & 1) != 0 ? 16 + signs : 8 + signs;
                        count = 3;
                    }
                    for (int corner = 1; corner < count - 1; ++corner)
                        AppendVisualTriangle(ref meshes, corners[0], corners[corner], corners[corner + 1]);
                    for (int corner = 0; corner < count; ++corner)
                    {
                        uint a = math.min(corners[corner], corners[(corner + 1) % count]);
                        uint b = math.max(corners[corner], corners[(corner + 1) % count]);
                        int existing = (int)visual.EdgeOffset;
                        while (existing < meshes.EdgeCount &&
                            (meshes.Edges[existing * 2] != a || meshes.Edges[existing * 2 + 1] != b)) ++existing;
                        if (existing == meshes.EdgeCount) AppendVisualEdge(ref meshes, a, b);
                    }
                }
            }
            visual.VertexCount = (uint)(meshes.VertexCount - visual.VertexOffset);
            visual.IndexCount = (uint)(meshes.IndexCount - visual.IndexOffset);
            visual.EdgeCount = (uint)(meshes.EdgeCount - visual.EdgeOffset);
            return visual.VertexCount == vertexCount && visual.IndexCount == indexCount &&
                visual.EdgeCount == edgeCount ? 0 : 2;
        }

        public static void AppendVisualVertex(ref UnityPhysicsVisualMeshStorage meshes, float x, float y, float z)
        {
            int offset = meshes.VertexCount++ * 3;
            meshes.Vertices[offset] = x;
            meshes.Vertices[offset + 1] = y;
            meshes.Vertices[offset + 2] = z;
        }

        public static void AppendVisualTriangle(ref UnityPhysicsVisualMeshStorage meshes, uint a, uint b, uint c)
        {
            meshes.Indices[meshes.IndexCount++] = a;
            meshes.Indices[meshes.IndexCount++] = b;
            meshes.Indices[meshes.IndexCount++] = c;
        }

        public static void AppendVisualEdge(ref UnityPhysicsVisualMeshStorage meshes, uint a, uint b)
        {
            int offset = meshes.EdgeCount++ * 2;
            meshes.Edges[offset] = a;
            meshes.Edges[offset + 1] = b;
        }

        public static readonly UnityPhysicsCaseRegistration[] CaseRegistrations =
        {
            UnityPhysicsBoxContainerPileRegistration(),
            UnityPhysicsBoxContactIslandsRegistration(),
            UnityPhysicsSpatialQueryTraceRegistration(),
            UnityPhysicsRagdollStairTumbleRegistration(),
            UnityPhysicsLargePyramidRegistration(),
            UnityPhysicsPyramidWallRegistration()
        };

        public static int ResolveUnityPhysicsCase(
            CaseFixtureKind fixtureKind, out UnityPhysicsCaseRegistration registration)
        {
            int index = fixtureKind switch
            {
                CaseFixtureKind.OpenContainerFallingPile => 0,
                CaseFixtureKind.BoxContactIslands => 1,
                CaseFixtureKind.SpatialQueryTrace => 2,
                CaseFixtureKind.RagdollStairTumble => 3,
                CaseFixtureKind.LargePyramid => 4,
                CaseFixtureKind.PyramidWall => 5,
                _ => -1
            };
            registration = index >= 0 ? CaseRegistrations[index] : default;
            return index >= 0 ? 0 : 2;
        }

        public static int RunRegisteredHeadless(RunnerArgs runnerArgs)
        {
            return runnerArgs.CaseRegistration.RunHeadless(runnerArgs);
        }

        public static int BuildUnityPhysicsVisualScene(
            UnityPhysicsCaseRegistration registration,
            ref UnityPhysicsCaseView state,
            UnityPhysicsVisualGeometry[] geometries,

            ref UnityPhysicsVisualMeshStorage meshes,
            UnityPhysicsVisualInstance[] instances,
            out int geometryCount,
            out int instanceCount)
        {
            return registration.BuildVisualScene(
                ref state, geometries, ref meshes, instances, out geometryCount, out instanceCount);
        }

        public static UnityPhysicsVisualMeshStorage CreateVisualMeshStorage(in CaseExecutionSpec execution)
        {
            int capsuleCount = execution.SelectedGeometry.Shape == CaseExecutionShape.Capsule ? 1 : 0;
            int hullCount = execution.SelectedGeometry.Shape == CaseExecutionShape.ConvexHull ? 1 : 0;
            if (execution.FixtureKind == CaseFixtureKind.RagdollStairTumble)
            {
                capsuleCount = 0;
                hullCount = 0;
                CaseExecutionRagdoll fixture = execution.Ragdoll;
                uint nextGeometry = 0;
                for (int partIndex = 0; partIndex < fixture.Parts.Length; ++partIndex)
                {
                    if (UnityPhysicsRagdollGeometryIndex(in fixture, partIndex) != nextGeometry) continue;
                    ++nextGeometry;
                    CaseExecutionShape shape = fixture.Parts[partIndex].Shape;
                    if (shape == CaseExecutionShape.Capsule) ++capsuleCount;
                    else if (shape == CaseExecutionShape.ConvexHull) ++hullCount;
                }
            }
            return new UnityPhysicsVisualMeshStorage
            {
                Vertices = new float[(capsuleCount * 130 + hullCount * 24) * 3],
                Indices = new uint[capsuleCount * 768 + hullCount * 132],
                Edges = new uint[(capsuleCount * 272 + hullCount * 36) * 2]
            };
        }

        public static int BuildUnityPhysicsVisualDebugPrimitives(
            UnityPhysicsCaseRegistration registration,
            ref UnityPhysicsCaseView state,
            UnityPhysicsVisualDebugPrimitive[] primitives)
        {
            if (primitives == null)
            {
                return 2;
            }
            if (primitives.Length == 0)
            {
                return 0;
            }
            return registration.BuildVisualDebugPrimitives != null ?
                registration.BuildVisualDebugPrimitives(ref state, primitives) : 2;
        }

    }
}
