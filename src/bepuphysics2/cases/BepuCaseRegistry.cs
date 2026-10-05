using System.Numerics;
using BepuPhysics;
using BepuPhysics.Collidables;
using BepuUtilities.Memory;

namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public struct BepuResolvedShape
{
    public TypedIndex Index;
    public Vector3 Center;
    public float Volume;
}

public delegate int BepuBuildVisualSceneCallback(
    BepuCaseView state,
    BepuVisualGeometry[] geometries,

    ref BepuVisualMeshStorage meshes,
    BepuVisualInstance[] instances,
    out int geometryCount,
    out int instanceCount);
public delegate int BepuBuildVisualDebugPrimitivesCallback(
    BepuCaseView state,
    BepuVisualDebugPrimitive[] primitives);

public struct BepuCaseRegistration
{
    public BepuCaseDescriptor Descriptor;
    public BepuBuildVisualSceneCallback BuildVisualScene;
    public Func<BepuCaseView, BepuVisualStableTransform[], int> SampleVisualTransforms;
    public BepuBuildVisualDebugPrimitivesCallback BuildVisualDebugPrimitives;
    public Func<BepuRunnerArgs, int> RunHeadless;
}

public static class BepuCaseRegistry
{
    public const string EngineId = "bepuphysics2";

    public static Quaternion ShapeRotation(CaseExecutionAxis axis)
    {
        return axis switch
        {
            CaseExecutionAxis.X => Quaternion.CreateFromAxisAngle(Vector3.UnitZ, -MathF.PI * 0.5f),
            CaseExecutionAxis.Z => Quaternion.CreateFromAxisAngle(Vector3.UnitX, MathF.PI * 0.5f),
            _ => Quaternion.Identity
        };
    }

    public static BepuResolvedShape AddResolvedShape(Simulation simulation, BufferPool pool,
        in CaseExecutionSpec execution, in CaseExecutionGeometry geometry)
    {
        BepuResolvedShape result = default;
        switch (geometry.Shape)
        {
            case CaseExecutionShape.Box:
                result.Index = simulation.Shapes.Add(new Box(geometry.HalfExtents.X * 2f,
                    geometry.HalfExtents.Y * 2f, geometry.HalfExtents.Z * 2f));
                result.Volume = 8f * geometry.HalfExtents.X * geometry.HalfExtents.Y * geometry.HalfExtents.Z;
                break;
            case CaseExecutionShape.Sphere:
                result.Index = simulation.Shapes.Add(new Sphere(geometry.Radius));
                result.Volume = (4f / 3f) * MathF.PI * geometry.Radius * geometry.Radius * geometry.Radius;
                break;
            case CaseExecutionShape.Capsule:
                result.Index = simulation.Shapes.Add(new Capsule(geometry.Radius, geometry.HalfSegment * 2f));
                result.Volume = MathF.PI * geometry.Radius * geometry.Radius *
                    (2f * geometry.HalfSegment + (4f / 3f) * geometry.Radius);
                break;
            case CaseExecutionShape.ConvexHull:
                Span<Vector3> points = stackalloc Vector3[24];
                for (int index = 0; index < points.Length; ++index)
                {
                    CaseExecutionVector3 point = execution.HullPoints[index];
                    points[index] = new Vector3(point.X * geometry.HalfExtents.X,
                        point.Y * geometry.HalfExtents.Y, point.Z * geometry.HalfExtents.Z);
                }
                if (!ConvexHullHelper.CreateShape(points, pool, out result.Center, out ConvexHull hull))
                    throw new InvalidOperationException("Resolved convex hull construction failed.");
                ConvexHull.ConvexHullTriangleSource triangles = new(in hull);
                MeshInertiaHelper.ComputeClosedCenterOfMass(ref triangles, out result.Volume, out Vector3 _);
                if (!float.IsFinite(result.Volume) || result.Volume <= 0f)
                {
                    hull.Dispose(pool);
                    throw new InvalidOperationException("Resolved convex hull has invalid volume.");
                }
                result.Index = simulation.Shapes.Add(hull);
                break;
            default:
                throw new InvalidOperationException("Unsupported resolved shape.");
        }
        return result;
    }

    public static void OffsetHullVisualGeometry(Simulation simulation, TypedIndex index,
        in CaseExecutionGeometry geometry, ref BepuVisualMeshStorage meshes, in BepuVisualGeometry visual)
    {
        if (geometry.Shape != CaseExecutionShape.ConvexHull) return;
        simulation.Shapes.GetShape<ConvexHull>(index.Index).ComputeBounds(Quaternion.Identity,
            out Vector3 minimum, out Vector3 maximum);
        Vector3 offset = (minimum + maximum) * 0.5f;
        for (uint vertex = visual.VertexOffset; vertex < visual.VertexOffset + visual.VertexCount; ++vertex)
        {
            meshes.Vertices[vertex * 3] += offset.X;
            meshes.Vertices[vertex * 3 + 1] += offset.Y;
            meshes.Vertices[vertex * 3 + 2] += offset.Z;
        }
    }

    public static BodyInertia ShapeInertia(Simulation simulation, TypedIndex shape, float mass)
    {
        return ((IConvexShapeBatch)simulation.Shapes[shape.Type]).ComputeInertia(shape.Index, mass);
    }

    public static int BuildResolvedVisualGeometry(in CaseExecutionSpec execution,
        in CaseExecutionGeometry geometry, ref BepuVisualMeshStorage meshes, out BepuVisualGeometry visual)
    {
        visual = default;
        if (geometry.Shape == CaseExecutionShape.Box)
        {
            visual.Kind = 2;
            visual.ParameterX = geometry.HalfExtents.X;
            visual.ParameterY = geometry.HalfExtents.Y;
            visual.ParameterZ = geometry.HalfExtents.Z;
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
                CaseExecutionVector3 point = execution.HullPoints[index];
                AppendVisualVertex(ref meshes, point.X * geometry.HalfExtents.X,
                    point.Y * geometry.HalfExtents.Y, point.Z * geometry.HalfExtents.Z);
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
                        CaseExecutionVector3 point = execution.HullPoints[index];
                        components[0] = point.X;
                        components[1] = point.Y;
                        components[2] = point.Z;
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
                    uint a = Math.Min(corners[corner], corners[(corner + 1) % count]);
                    uint b = Math.Max(corners[corner], corners[(corner + 1) % count]);
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

    public static void AppendVisualVertex(ref BepuVisualMeshStorage meshes, float x, float y, float z)
    {
        int offset = meshes.VertexCount++ * 3;
        meshes.Vertices[offset] = x;
        meshes.Vertices[offset + 1] = y;
        meshes.Vertices[offset + 2] = z;
    }

    public static void AppendVisualTriangle(ref BepuVisualMeshStorage meshes, uint a, uint b, uint c)
    {
        meshes.Indices[meshes.IndexCount++] = a;
        meshes.Indices[meshes.IndexCount++] = b;
        meshes.Indices[meshes.IndexCount++] = c;
    }

    public static void AppendVisualEdge(ref BepuVisualMeshStorage meshes, uint a, uint b)
    {
        int offset = meshes.EdgeCount++ * 2;
        meshes.Edges[offset] = a;
        meshes.Edges[offset + 1] = b;
    }

    public static readonly BepuCaseRegistration[] Registrations =
    {
        BepuBoxContainerPileCase.Registration(),
        BepuBoxContactIslandsCase.Registration(),
        BepuSpatialQueryTraceCase.Registration(),
        BepuRagdollStairTumbleCase.Registration(),
        BepuLargePyramidCase.Registration(),
        BepuPyramidWallCase.Registration()
    };

    public static int BuildVisualScene(
        in BepuCaseRegistration registration,
        BepuCaseView state,
        BepuVisualGeometry[] geometries,

        ref BepuVisualMeshStorage meshes,
        BepuVisualInstance[] instances,
        out int geometryCount,
        out int instanceCount)
    {
        CaseExecutionSpec execution = state.CaseExecution;
        int capsuleCount = execution.SelectedGeometry.Shape == CaseExecutionShape.Capsule ? 1 : 0;
        int hullCount = execution.SelectedGeometry.Shape == CaseExecutionShape.ConvexHull ? 1 : 0;
        if (execution.FixtureKind == CaseFixtureKind.RagdollStairTumble)
        {
            capsuleCount = 0;
            hullCount = 0;
            CaseExecutionRagdoll fixture = execution.Ragdoll;
            uint nextGeometry = 0;
            for (int partIndex = 0; partIndex < fixture.PartCount; ++partIndex)
            {
                if (BepuRagdollStairTumbleCase.GeometryIndex(in fixture, partIndex) != nextGeometry) continue;
                ++nextGeometry;
                CaseExecutionShape shape = fixture.Parts[partIndex].Shape;
                if (shape == CaseExecutionShape.Capsule) ++capsuleCount;
                else if (shape == CaseExecutionShape.ConvexHull) ++hullCount;
            }
        }
        meshes = new BepuVisualMeshStorage
        {
            Vertices = new float[(capsuleCount * 130 + hullCount * 24) * 3],
            Indices = new uint[capsuleCount * 768 + hullCount * 132],
            Edges = new uint[(capsuleCount * 272 + hullCount * 36) * 2]
        };
        return registration.BuildVisualScene(
            state, geometries, ref meshes, instances, out geometryCount, out instanceCount);
    }

    public static int BuildVisualDebugPrimitives(
        in BepuCaseRegistration registration,
        BepuCaseView state,
        BepuVisualDebugPrimitive[] primitives)
    {
        if (primitives == null) return 2;
        if (primitives.Length == 0) return 0;
        return registration.BuildVisualDebugPrimitives != null ?
            registration.BuildVisualDebugPrimitives(state, primitives) : 2;
    }

    public static int RunHeadless(BepuRunnerArgs runnerArgs)
    {
        return runnerArgs.CaseRegistration.RunHeadless(runnerArgs);
    }

    public static int Resolve(CaseFixtureKind fixtureKind, out BepuCaseRegistration registration)
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
        registration = index >= 0 ? Registrations[index] : default;
        return index >= 0 ? 0 : 2;
    }
}
