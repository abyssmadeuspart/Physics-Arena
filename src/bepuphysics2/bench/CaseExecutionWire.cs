using System.Buffers.Binary;
using System.Runtime.CompilerServices;
using System.Text;

namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public enum CaseFixtureKind : ushort
{
    Unknown = 0,
    OpenContainerFallingPile = 1,
    BoxContactIslands = 2,
    SpatialQueryTrace = 3,
    RagdollStairTumble = 4,
    LargePyramid = 5,
    PyramidWall = 6
}

public enum CaseExecutionShape : byte
{
    Unknown = 0,
    Box = 1,
    Sphere = 2,
    Capsule = 3,
    ConvexHull = 4
}

public enum CaseShapePreset : byte
{
    Authored = 0,
    Sphere = 1,
    Capsule = 2,
    ConvexHull = 3
}

public enum CaseExecutionAxis : byte
{
    Y = 0,
    X = 1,
    Z = 2
}

public struct CaseExecutionGeometry
{
    public CaseExecutionVector3 HalfExtents;
    public float Radius;
    public float HalfSegment;
    public CaseExecutionShape Shape;
    public CaseExecutionAxis Axis;
}

public enum CaseExecutionToggle : byte
{
    Disabled = 0,
    Enabled = 1
}

public enum CaseDecodeStatus : byte
{
    Invalid = 0,
    Valid = 1
}

public enum CaseTextMatch : byte
{
    Different = 0,
    Equal = 1
}

[InlineArray(CaseExecutionWire.TextCapacity)]
public struct CaseTextBytes
{
    public byte value;
}

[InlineArray(3)]
public struct CaseUInt3
{
    public uint value;
}

[InlineArray(2)]
public struct CaseUInt2
{
    public uint value;
}

[InlineArray(2)]
public struct CaseFloat2
{
    public float value;
}

[InlineArray(CaseExecutionWire.StaticBoxCapacity)]
public struct CaseExecutionBoxes
{
    public CaseExecutionBox value;
}

[InlineArray(24)]
public struct CaseExecutionHullPoints
{
    public CaseExecutionVector3 Value;
}

[InlineArray(CaseExecutionWire.YawCapacity)]
public struct CaseExecutionYaws
{
    public float value;
}

[InlineArray(CaseExecutionWire.RagdollPartCapacity)]
public struct CaseExecutionRagdollParts
{
    public CaseExecutionRagdollPart value;
}

[InlineArray(CaseExecutionWire.RagdollLinkCapacity)]
public struct CaseExecutionRagdollLinks
{
    public CaseExecutionRagdollLink value;
}

public struct CaseText
{
    public CaseTextBytes Bytes;
    public ushort Length;

}

public struct CaseExecutionVector3
{
    public float X;
    public float Y;
    public float Z;
}

public struct CaseExecutionBox
{
    public CaseExecutionVector3 Center;
    public CaseExecutionVector3 HalfExtents;
}

public struct CaseExecutionOpenContainer
{
    public CaseUInt3 DynamicGrid;
    public CaseExecutionVector3 DynamicHalfExtents;
    public CaseExecutionVector3 DynamicSpacing;
    public float DynamicInitialY;
    public float Density;
    public CaseExecutionBoxes StaticBoxes;
    public ushort StaticBoxCount;
}

public struct CaseExecutionContactIslands
{
    public CaseUInt2 IslandGrid;
    public CaseFloat2 IslandSpacing;
    public CaseUInt3 BodyGrid;
    public CaseExecutionVector3 BodyHalfExtents;
    public CaseExecutionVector3 BodySpacing;
    public float BodyInitialY;
    public CaseExecutionVector3 FloorHalfExtents;
    public float Density;
}

public struct CaseExecutionSpatialQuery
{
    public CaseUInt3 StaticGrid;
    public CaseExecutionVector3 StaticHalfExtents;
    public CaseExecutionVector3 StaticSpacing;
    public CaseExecutionVector3 StaticBaseCenter;
    public uint RayCount;
    public uint SphereCastCount;
    public uint OverlapCount;
    public float QueryDistance;
    public float SphereCastRadius;
    public CaseExecutionVector3 OverlapHalfExtents;
    public float MissOffset;
    public uint DebugSamplesPerFamily;
}

public struct CaseExecutionRagdollPart
{
    public CaseExecutionShape Shape;
    public CaseExecutionVector3 Center;
    public CaseExecutionVector3 HalfExtents;
    public float Radius;
    public float HalfSegment;
    public CaseExecutionAxis Axis;
}

public struct CaseExecutionRagdollLink
{
    public ushort ParentPart;
    public ushort ChildPart;
    public CaseExecutionVector3 Anchor;
    public CaseExecutionVector3 ParentLocalAnchor;
    public CaseExecutionVector3 ChildLocalAnchor;
}

public struct CaseExecutionRagdoll
{
    public CaseUInt2 RagdollGrid;
    public float ColumnSpacing;
    public float RowSpacing;
    public float BaseHeightOffset;
    public float PitchDegrees;
    public CaseExecutionYaws YawPatternDegrees;
    public ushort YawPatternCount;
    public float TriggerRowSpeed;
    public float FollowerRowSpeed;
    public uint StairCount;
    public float StairRise;
    public float StairDepth;
    public float StairHalfWidth;
    public float StairHalfHeight;
    public float StairHalfDepth;
    public CaseExecutionBoxes ExtraStaticBoxes;
    public ushort ExtraStaticBoxCount;
    public CaseExecutionRagdollParts Parts;
    public ushort PartCount;
    public CaseExecutionRagdollLinks Links;
    public ushort LinkCount;
    public float LinearDamping;
    public float AngularDamping;
    public float PartMass;
    public CaseExecutionToggle LinkedCollisionMode;
}

public struct CaseExecutionPyramidWall
{
    public uint RowCount;
    public float HalfExtent;
    public float Density;
    public CaseExecutionVector3 FloorHalfExtents;
}

public struct CaseExecutionLargePyramid
{
    public uint RowCount;
    public CaseExecutionVector3 BoxHalfExtents;
    public CaseExecutionVector3 BoxSpacing;
    public CaseExecutionVector3 BaseCenter;
    public CaseExecutionVector3 FloorHalfExtents;
    public float BoxDensity;
    public uint ProjectileCount;
    public float ProjectileRadius;
    public float ProjectileDensity;
    public CaseExecutionVector3 ProjectileInitialCenter;
    public CaseExecutionVector3 ProjectileCenterSpacing;
    public CaseExecutionVector3 ProjectileLaunchVelocity;
    public uint ProjectileLaunchAfterWorkUnits;
}

public struct CaseExecutionCamera
{
    public CaseExecutionVector3 Direction;
    public CaseExecutionVector3 Up;
    public CaseExecutionVector3 Minimum;
    public CaseExecutionVector3 Maximum;
    public CaseExecutionVector3 Eye;
    public CaseExecutionVector3 Target;
    public CaseExecutionVector3 EyeOffset;
    public CaseExecutionVector3 TargetOffset;
    public float VerticalFovDegrees;
    public float ViewportFill;
    public float NearPlane;
    public float FarPlane;
    public uint StableSlot;
    public uint Mode;
}

public struct CaseExecutionSpec
{
    public CaseShapePreset ShapePreset;
    public CaseExecutionGeometry SelectedGeometry;
    public CaseExecutionHullPoints HullPoints;
    public CaseFixtureKind FixtureKind;
    public CaseText CaseId;
    public CaseText FixtureSemantic;
    public uint FixtureRevision;
    public uint DynamicBodyCount;
    public uint KinematicBodyCount;
    public uint StaticBodyCount;
    public uint BodyCount;
    public uint ShapeCount;
    public uint VisualInstanceCount;
    public uint MeshTriangleCount;
    public uint QueryCount;
    public uint ConstraintCount;
    public uint TimestepHz;
    public uint WarmupWorkUnitCount;
    public uint MeasuredWorkUnitCount;
    public uint VisualDebugPrimitiveCount;
    public byte TimestepPresent;
    public CaseExecutionVector3 Gravity;
    public float Friction;
    public float Restitution;
    public CaseExecutionCamera ReplayCamera;
    public uint SolverFields;
    public uint VelocityIterations;
    public uint PositionIterations;
    public uint ProjectionIterations;
    public uint SolverIterations;
    public uint Substeps;
    public uint CollisionSteps;
    public CaseExecutionToggle SleepMode;
    public CaseExecutionToggle ContinuousCollisionMode;
    public CaseExecutionOpenContainer OpenContainer;
    public CaseExecutionContactIslands ContactIslands;
    public CaseExecutionSpatialQuery SpatialQuery;
    public CaseExecutionRagdoll Ragdoll;
    public CaseExecutionLargePyramid LargePyramid;
    public CaseExecutionPyramidWall PyramidWall;

}

public static class CaseExecutionWire
{
    public const int PayloadCapacity = 2048;
    public const int HexCapacity = PayloadCapacity * 2;
    public const int StaticBoxCapacity = 16;
    public const int RagdollPartCapacity = 32;
    public const int RagdollLinkCapacity = 32;
    public const int YawCapacity = 16;
    public const int TextCapacity = 64;

    public static int DecodeHex(string hex, out CaseExecutionSpec spec)
    {
        spec = default;
        if (string.IsNullOrEmpty(hex) || hex.Length > HexCapacity || (hex.Length & 1) != 0) return 2;
        Span<byte> bytes = stackalloc byte[PayloadCapacity];
        int byteCount = hex.Length / 2;
        for (int index = 0; index < byteCount; ++index)
        {
            int high = Nibble(hex[index * 2]);
            int low = Nibble(hex[index * 2 + 1]);
            if (high < 0 || low < 0) return 2;
            bytes[index] = (byte)((high << 4) | low);
        }
        return Decode(bytes[..byteCount], out spec);
    }

    public static int Decode(ReadOnlySpan<byte> bytes, out CaseExecutionSpec spec)
    {
        spec = default;
        if (bytes.Length < 10 || bytes.Length > PayloadCapacity) return 2;
        Reader reader = new Reader { Bytes = bytes, Status = CaseDecodeStatus.Valid };
        if (U8(ref reader) != (byte)'P' || U8(ref reader) != (byte)'A' || U8(ref reader) != (byte)'C' ||
            U8(ref reader) != (byte)'X') return 2;
        spec.FixtureKind = U16(ref reader) switch
        {
            1 => CaseFixtureKind.OpenContainerFallingPile,
            2 => CaseFixtureKind.BoxContactIslands,
            3 => CaseFixtureKind.SpatialQueryTrace,
            4 => CaseFixtureKind.RagdollStairTumble,
            5 => CaseFixtureKind.LargePyramid,
            6 => CaseFixtureKind.PyramidWall,
            _ => CaseFixtureKind.Unknown
        };
        if (spec.FixtureKind == CaseFixtureKind.Unknown || U32(ref reader) != bytes.Length) return 2;
        ReadText(ref reader, ref spec.CaseId);
        ReadText(ref reader, ref spec.FixtureSemantic);
        spec.FixtureRevision = U32(ref reader);
        spec.DynamicBodyCount = U32(ref reader);
        spec.KinematicBodyCount = U32(ref reader);
        spec.StaticBodyCount = U32(ref reader);
        spec.BodyCount = U32(ref reader);
        spec.ShapeCount = U32(ref reader);
        spec.VisualInstanceCount = U32(ref reader);
        spec.MeshTriangleCount = U32(ref reader);
        spec.QueryCount = U32(ref reader);
        spec.ConstraintCount = U32(ref reader);
        spec.TimestepPresent = U8(ref reader);
        spec.TimestepHz = U32(ref reader);
        spec.WarmupWorkUnitCount = U32(ref reader);
        spec.MeasuredWorkUnitCount = U32(ref reader);
        spec.VisualDebugPrimitiveCount = U32(ref reader);
        spec.Gravity = Vector3(ref reader);
        spec.SleepMode = Toggle(U8(ref reader), ref reader);
        spec.ContinuousCollisionMode = Toggle(U8(ref reader), ref reader);
        spec.Friction = Float(ref reader);
        spec.Restitution = Float(ref reader);
        spec.SolverFields = U32(ref reader);
        spec.VelocityIterations = U32(ref reader);
        spec.PositionIterations = U32(ref reader);
        spec.ProjectionIterations = U32(ref reader);
        spec.SolverIterations = U32(ref reader);
        spec.Substeps = U32(ref reader);
        spec.CollisionSteps = U32(ref reader);
        spec.ReplayCamera.Direction = Vector3(ref reader);
        spec.ReplayCamera.Up = Vector3(ref reader);
        spec.ReplayCamera.Minimum = Vector3(ref reader);
        spec.ReplayCamera.Maximum = Vector3(ref reader);
        spec.ReplayCamera.Eye = Vector3(ref reader);
        spec.ReplayCamera.Target = Vector3(ref reader);
        spec.ReplayCamera.EyeOffset = Vector3(ref reader);
        spec.ReplayCamera.TargetOffset = Vector3(ref reader);
        spec.ReplayCamera.VerticalFovDegrees = Float(ref reader);
        spec.ReplayCamera.ViewportFill = Float(ref reader);
        spec.ReplayCamera.NearPlane = Float(ref reader);
        spec.ReplayCamera.FarPlane = Float(ref reader);
        spec.ReplayCamera.StableSlot = U32(ref reader);
        spec.ReplayCamera.Mode = U32(ref reader);
        if ((spec.SolverFields & ~63u) != 0 ||
            (spec.FixtureKind == CaseFixtureKind.SpatialQueryTrace && spec.SolverFields != 0)) return 2;
        if (((spec.SolverFields & 1u) == 0 && spec.VelocityIterations != 0) ||
            ((spec.SolverFields & 2u) == 0 && spec.PositionIterations != 0) ||
            ((spec.SolverFields & 4u) == 0 && spec.ProjectionIterations != 0) ||
            ((spec.SolverFields & 8u) == 0 && spec.SolverIterations != 0) ||
            ((spec.SolverFields & 16u) == 0 && spec.Substeps != 0) ||
            ((spec.SolverFields & 32u) == 0 && spec.CollisionSteps != 0)) return 2;
        if (spec.FixtureKind != CaseFixtureKind.SpatialQueryTrace &&
            (spec.SolverFields != 17u || spec.VelocityIterations == 0 || spec.VelocityIterations > int.MaxValue ||
             spec.Substeps == 0 || spec.Substeps > int.MaxValue)) return 2;
        spec.ShapePreset = (CaseShapePreset)U8(ref reader);
        if (spec.ShapePreset > CaseShapePreset.ConvexHull) return 2;
        if (spec.ShapePreset == CaseShapePreset.ConvexHull)
            for (int index = 0; index < 24; ++index) spec.HullPoints[index] = Vector3(ref reader);
        if (spec.FixtureKind != CaseFixtureKind.RagdollStairTumble)
            spec.SelectedGeometry = ReadGeometry(ref reader);

        switch (spec.FixtureKind)
        {
            case CaseFixtureKind.OpenContainerFallingPile:
                for (int index = 0; index < 3; ++index) spec.OpenContainer.DynamicGrid[index] = U32(ref reader);
                spec.OpenContainer.DynamicHalfExtents = Vector3(ref reader);
                spec.OpenContainer.DynamicSpacing = Vector3(ref reader);
                spec.OpenContainer.DynamicInitialY = Float(ref reader);
                spec.OpenContainer.Density = Float(ref reader);
                spec.OpenContainer.StaticBoxCount = U16(ref reader);
                if (spec.OpenContainer.StaticBoxCount > StaticBoxCapacity) reader.Status = CaseDecodeStatus.Invalid;
                for (int index = 0; reader.Status == CaseDecodeStatus.Valid && index < spec.OpenContainer.StaticBoxCount; ++index)
                    spec.OpenContainer.StaticBoxes[index] = Box(ref reader);
                break;
            case CaseFixtureKind.BoxContactIslands:
                for (int index = 0; index < 2; ++index) spec.ContactIslands.IslandGrid[index] = U32(ref reader);
                for (int index = 0; index < 2; ++index) spec.ContactIslands.IslandSpacing[index] = Float(ref reader);
                for (int index = 0; index < 3; ++index) spec.ContactIslands.BodyGrid[index] = U32(ref reader);
                spec.ContactIslands.BodyHalfExtents = Vector3(ref reader);
                spec.ContactIslands.BodySpacing = Vector3(ref reader);
                spec.ContactIslands.BodyInitialY = Float(ref reader);
                spec.ContactIslands.FloorHalfExtents = Vector3(ref reader);
                spec.ContactIslands.Density = Float(ref reader);
                break;
            case CaseFixtureKind.SpatialQueryTrace:
                for (int index = 0; index < 3; ++index) spec.SpatialQuery.StaticGrid[index] = U32(ref reader);
                spec.SpatialQuery.StaticHalfExtents = Vector3(ref reader);
                spec.SpatialQuery.StaticSpacing = Vector3(ref reader);
                spec.SpatialQuery.StaticBaseCenter = Vector3(ref reader);
                spec.SpatialQuery.RayCount = U32(ref reader);
                spec.SpatialQuery.SphereCastCount = U32(ref reader);
                spec.SpatialQuery.OverlapCount = U32(ref reader);
                spec.SpatialQuery.QueryDistance = Float(ref reader);
                spec.SpatialQuery.SphereCastRadius = Float(ref reader);
                spec.SpatialQuery.OverlapHalfExtents = Vector3(ref reader);
                spec.SpatialQuery.MissOffset = Float(ref reader);
                spec.SpatialQuery.DebugSamplesPerFamily = U32(ref reader);
                break;
            case CaseFixtureKind.RagdollStairTumble:
                spec.Ragdoll.LinkedCollisionMode = Toggle(U8(ref reader), ref reader);
                spec.Ragdoll.LinearDamping = Float(ref reader);
                spec.Ragdoll.AngularDamping = Float(ref reader);
                spec.Ragdoll.PartMass = Float(ref reader);
                for (int index = 0; index < 2; ++index) spec.Ragdoll.RagdollGrid[index] = U32(ref reader);
                spec.Ragdoll.ColumnSpacing = Float(ref reader);
                spec.Ragdoll.RowSpacing = Float(ref reader);
                spec.Ragdoll.BaseHeightOffset = Float(ref reader);
                spec.Ragdoll.PitchDegrees = Float(ref reader);
                spec.Ragdoll.YawPatternCount = U16(ref reader);
                if (spec.Ragdoll.YawPatternCount == 0 || spec.Ragdoll.YawPatternCount > YawCapacity) reader.Status = CaseDecodeStatus.Invalid;
                for (int index = 0; reader.Status == CaseDecodeStatus.Valid && index < spec.Ragdoll.YawPatternCount; ++index)
                    spec.Ragdoll.YawPatternDegrees[index] = Float(ref reader);
                spec.Ragdoll.TriggerRowSpeed = Float(ref reader);
                spec.Ragdoll.FollowerRowSpeed = Float(ref reader);
                spec.Ragdoll.StairCount = U32(ref reader);
                spec.Ragdoll.StairRise = Float(ref reader);
                spec.Ragdoll.StairDepth = Float(ref reader);
                spec.Ragdoll.StairHalfWidth = Float(ref reader);
                spec.Ragdoll.StairHalfHeight = Float(ref reader);
                spec.Ragdoll.StairHalfDepth = Float(ref reader);
                spec.Ragdoll.ExtraStaticBoxCount = U16(ref reader);
                if (spec.Ragdoll.ExtraStaticBoxCount > StaticBoxCapacity) reader.Status = CaseDecodeStatus.Invalid;
                for (int index = 0; reader.Status == CaseDecodeStatus.Valid && index < spec.Ragdoll.ExtraStaticBoxCount; ++index)
                    spec.Ragdoll.ExtraStaticBoxes[index] = Box(ref reader);
                spec.Ragdoll.PartCount = U16(ref reader);
                if (spec.Ragdoll.PartCount == 0 || spec.Ragdoll.PartCount > RagdollPartCapacity) reader.Status = CaseDecodeStatus.Invalid;
                for (int index = 0; reader.Status == CaseDecodeStatus.Valid && index < spec.Ragdoll.PartCount; ++index)
                {
                    ref CaseExecutionRagdollPart part = ref spec.Ragdoll.Parts[index];
                    part.Center = Vector3(ref reader);
                    CaseExecutionGeometry geometry = ReadGeometry(ref reader);
                    part.Shape = geometry.Shape;
                    part.HalfExtents = geometry.HalfExtents;
                    part.Radius = geometry.Radius;
                    part.HalfSegment = geometry.HalfSegment;
                    part.Axis = geometry.Axis;
                }
                spec.Ragdoll.LinkCount = U16(ref reader);
                if (spec.Ragdoll.LinkCount > RagdollLinkCapacity) reader.Status = CaseDecodeStatus.Invalid;
                for (int index = 0; reader.Status == CaseDecodeStatus.Valid && index < spec.Ragdoll.LinkCount; ++index)
                {
                    ref CaseExecutionRagdollLink link = ref spec.Ragdoll.Links[index];
                    link.ParentPart = U16(ref reader);
                    link.ChildPart = U16(ref reader);
                    link.Anchor = Vector3(ref reader);
                    link.ParentLocalAnchor = Vector3(ref reader);
                    link.ChildLocalAnchor = Vector3(ref reader);
                    if (link.ParentPart >= spec.Ragdoll.PartCount || link.ChildPart >= spec.Ragdoll.PartCount)
                        reader.Status = CaseDecodeStatus.Invalid;
                }
                break;
            case CaseFixtureKind.PyramidWall:
                spec.PyramidWall.RowCount = U32(ref reader);
                spec.PyramidWall.HalfExtent = Float(ref reader);
                spec.PyramidWall.Density = Float(ref reader);
                spec.PyramidWall.FloorHalfExtents = Vector3(ref reader);
                CaseExecutionPyramidWall wall = spec.PyramidWall;
                ulong count = (ulong)wall.RowCount * (wall.RowCount + 1UL) / 2;
                if (wall.RowCount == 0 || wall.RowCount > 180 || count > 16290 ||
                    wall.HalfExtent <= 0 || wall.Density <= 0 ||
                    wall.FloorHalfExtents.X <= 0 || wall.FloorHalfExtents.Y <= 0 || wall.FloorHalfExtents.Z <= 0 ||
                    spec.DynamicBodyCount != count || spec.StaticBodyCount != 1 || spec.KinematicBodyCount != 0 ||
                    spec.ShapeCount != count + 1 || spec.VisualInstanceCount != count + 1 || spec.MeshTriangleCount != 0 ||
                    spec.QueryCount != 0 || spec.ConstraintCount != 0 || spec.TimestepHz == 0 ||
                    spec.ShapePreset != CaseShapePreset.Authored ||
                    spec.SelectedGeometry.Shape != CaseExecutionShape.Box ||
                    spec.SelectedGeometry.HalfExtents.X != wall.HalfExtent ||
                    spec.SelectedGeometry.HalfExtents.Y != wall.HalfExtent ||
                    spec.SelectedGeometry.HalfExtents.Z != wall.HalfExtent) return 2;
                break;
            case CaseFixtureKind.LargePyramid:
                spec.LargePyramid.RowCount = U32(ref reader);
                spec.LargePyramid.BoxHalfExtents = Vector3(ref reader);
                spec.LargePyramid.BoxSpacing = Vector3(ref reader);
                spec.LargePyramid.BaseCenter = Vector3(ref reader);
                spec.LargePyramid.FloorHalfExtents = Vector3(ref reader);
                spec.LargePyramid.BoxDensity = Float(ref reader);
                spec.LargePyramid.ProjectileCount = U32(ref reader);
                spec.LargePyramid.ProjectileRadius = Float(ref reader);
                spec.LargePyramid.ProjectileDensity = Float(ref reader);
                spec.LargePyramid.ProjectileInitialCenter = Vector3(ref reader);
                spec.LargePyramid.ProjectileCenterSpacing = Vector3(ref reader);
                spec.LargePyramid.ProjectileLaunchVelocity = Vector3(ref reader);
                spec.LargePyramid.ProjectileLaunchAfterWorkUnits = U32(ref reader);
                break;
        }

        CaseExecutionShape expectedShape = spec.ShapePreset switch
        {
            CaseShapePreset.Authored => CaseExecutionShape.Box,
            CaseShapePreset.Sphere => CaseExecutionShape.Sphere,
            CaseShapePreset.Capsule => CaseExecutionShape.Capsule,
            _ => CaseExecutionShape.ConvexHull
        };
        if (spec.FixtureKind == CaseFixtureKind.RagdollStairTumble)
        {
            for (int index = 0; index < spec.Ragdoll.PartCount; ++index)
            {
                CaseExecutionShape shape = spec.Ragdoll.Parts[index].Shape;
                if (shape != CaseExecutionShape.Sphere && shape != expectedShape) return 2;
            }
        }
        else if (spec.SelectedGeometry.Shape != expectedShape) return 2;

        ReadOnlySpan<byte> expectedSemantic = (spec.FixtureKind, spec.ShapePreset) switch
        {
            (CaseFixtureKind.OpenContainerFallingPile, CaseShapePreset.Authored) => "open_container_falling_pile"u8,
            (CaseFixtureKind.OpenContainerFallingPile, CaseShapePreset.Sphere) => "open_container_falling_pile_sphere"u8,
            (CaseFixtureKind.OpenContainerFallingPile, CaseShapePreset.Capsule) => "open_container_falling_pile_capsule"u8,
            (CaseFixtureKind.OpenContainerFallingPile, CaseShapePreset.ConvexHull) => "open_container_falling_pile_convex_hull"u8,
            (CaseFixtureKind.BoxContactIslands, CaseShapePreset.Authored) => "box_contact_islands_10k"u8,
            (CaseFixtureKind.BoxContactIslands, CaseShapePreset.Sphere) => "box_contact_islands_10k_sphere"u8,
            (CaseFixtureKind.BoxContactIslands, CaseShapePreset.Capsule) => "box_contact_islands_10k_capsule"u8,
            (CaseFixtureKind.BoxContactIslands, CaseShapePreset.ConvexHull) => "box_contact_islands_10k_convex_hull"u8,
            (CaseFixtureKind.SpatialQueryTrace, CaseShapePreset.Authored) => "spatial_query_trace"u8,
            (CaseFixtureKind.SpatialQueryTrace, CaseShapePreset.Sphere) => "spatial_query_trace_sphere"u8,
            (CaseFixtureKind.SpatialQueryTrace, CaseShapePreset.Capsule) => "spatial_query_trace_capsule"u8,
            (CaseFixtureKind.SpatialQueryTrace, CaseShapePreset.ConvexHull) => "spatial_query_trace_convex_hull"u8,
            (CaseFixtureKind.RagdollStairTumble, CaseShapePreset.Authored) => "ragdoll_stair_tumble"u8,
            (CaseFixtureKind.RagdollStairTumble, CaseShapePreset.Sphere) => "ragdoll_stair_tumble_sphere"u8,
            (CaseFixtureKind.RagdollStairTumble, CaseShapePreset.Capsule) => "ragdoll_stair_tumble_capsule"u8,
            (CaseFixtureKind.RagdollStairTumble, CaseShapePreset.ConvexHull) => "ragdoll_stair_tumble_convex_hull"u8,
            (CaseFixtureKind.PyramidWall, CaseShapePreset.Authored) => "pyramid_wall"u8,
            (CaseFixtureKind.LargePyramid, CaseShapePreset.Authored) => "large_pyramid"u8,
            (CaseFixtureKind.LargePyramid, CaseShapePreset.Sphere) => "large_pyramid_sphere"u8,
            (CaseFixtureKind.LargePyramid, CaseShapePreset.Capsule) => "large_pyramid_capsule"u8,
            (CaseFixtureKind.LargePyramid, CaseShapePreset.ConvexHull) => "large_pyramid_convex_hull"u8,
            _ => default
        };
        if (reader.Status != CaseDecodeStatus.Valid || reader.Offset != bytes.Length || MatchText(spec.FixtureSemantic, expectedSemantic) != CaseTextMatch.Equal ||
            spec.FixtureRevision == 0 || spec.MeasuredWorkUnitCount == 0 ||
            spec.BodyCount != spec.DynamicBodyCount + spec.KinematicBodyCount + spec.StaticBodyCount ||
            spec.VisualInstanceCount == 0 || spec.VisualInstanceCount > spec.BodyCount ||
            spec.TimestepPresent > 1 || (spec.TimestepPresent == 0) != (spec.TimestepHz == 0)) return 2;
        return 0;
    }

    public static CaseTextMatch MatchText(in CaseText text, ReadOnlySpan<byte> expected)
    {
        if (text.Length != expected.Length) return CaseTextMatch.Different;
        ReadOnlySpan<byte> actual = text.Bytes;
        return actual[..text.Length].SequenceEqual(expected) ? CaseTextMatch.Equal : CaseTextMatch.Different;
    }

    public static CaseExecutionGeometry ReadGeometry(ref Reader reader)
    {
        CaseExecutionGeometry geometry = default;
        geometry.Shape = (CaseExecutionShape)U8(ref reader);
        if (geometry.Shape == CaseExecutionShape.Box || geometry.Shape == CaseExecutionShape.ConvexHull)
        {
            geometry.HalfExtents = Vector3(ref reader);
            if (geometry.HalfExtents.X <= 0.0f || geometry.HalfExtents.Y <= 0.0f || geometry.HalfExtents.Z <= 0.0f)
                reader.Status = CaseDecodeStatus.Invalid;
        }
        else if (geometry.Shape == CaseExecutionShape.Sphere || geometry.Shape == CaseExecutionShape.Capsule)
        {
            geometry.Radius = Float(ref reader);
            if (geometry.Radius <= 0.0f) reader.Status = CaseDecodeStatus.Invalid;
            if (geometry.Shape == CaseExecutionShape.Capsule)
            {
                geometry.HalfSegment = Float(ref reader);
                geometry.Axis = (CaseExecutionAxis)U8(ref reader);
                if (geometry.HalfSegment <= 0.0f || geometry.Axis > CaseExecutionAxis.Z)
                    reader.Status = CaseDecodeStatus.Invalid;
            }
        }
        else reader.Status = CaseDecodeStatus.Invalid;
        return geometry;
    }

    public static CaseExecutionGeometry PartGeometry(in CaseExecutionRagdollPart part)
    {
        return new CaseExecutionGeometry
        {
            Shape = part.Shape, HalfExtents = part.HalfExtents, Radius = part.Radius,
            HalfSegment = part.HalfSegment, Axis = part.Axis
        };
    }

    public static int Nibble(char value)
    {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        return -1;
    }

    public static CaseExecutionToggle Toggle(byte value, ref Reader reader)
    {
        if (value <= 1) return (CaseExecutionToggle)value;
        reader.Status = CaseDecodeStatus.Invalid;
        return CaseExecutionToggle.Disabled;
    }

    public ref struct Reader
    {
        public ReadOnlySpan<byte> Bytes;
        public int Offset;
        public CaseDecodeStatus Status;
    }

    public static string TextValue(in CaseText text)
    {
        ReadOnlySpan<byte> bytes = text.Bytes;
        return Encoding.ASCII.GetString(bytes[..text.Length]);
    }

    public static byte U8(ref Reader reader)
    {
        if (reader.Offset >= reader.Bytes.Length)
        {
            reader.Status = CaseDecodeStatus.Invalid;
            return 0;
        }
        return reader.Bytes[reader.Offset++];
    }

    public static ushort U16(ref Reader reader)
    {
        if (reader.Bytes.Length - reader.Offset < 2)
        {
            reader.Status = CaseDecodeStatus.Invalid;
            reader.Offset = reader.Bytes.Length;
            return 0;
        }
        ushort value = BinaryPrimitives.ReadUInt16LittleEndian(reader.Bytes[reader.Offset..]);
        reader.Offset += 2;
        return value;
    }

    public static uint U32(ref Reader reader)
    {
        if (reader.Bytes.Length - reader.Offset < 4)
        {
            reader.Status = CaseDecodeStatus.Invalid;
            reader.Offset = reader.Bytes.Length;
            return 0;
        }
        uint value = BinaryPrimitives.ReadUInt32LittleEndian(reader.Bytes[reader.Offset..]);
        reader.Offset += 4;
        return value;
    }

    public static float Float(ref Reader reader)
    {
        float value = BitConverter.UInt32BitsToSingle(U32(ref reader));
        if (!float.IsFinite(value)) reader.Status = CaseDecodeStatus.Invalid;
        return value;
    }

    public static CaseExecutionVector3 Vector3(ref Reader reader)
    {
        return new CaseExecutionVector3 { X = Float(ref reader), Y = Float(ref reader), Z = Float(ref reader) };
    }

    public static CaseExecutionBox Box(ref Reader reader)
    {
        return new CaseExecutionBox { Center = Vector3(ref reader), HalfExtents = Vector3(ref reader) };
    }

    public static void ReadText(ref Reader reader, ref CaseText text)
    {
        ushort length = U16(ref reader);
        if (length == 0 || length >= TextCapacity || length > reader.Bytes.Length - reader.Offset)
        {
            reader.Status = CaseDecodeStatus.Invalid;
            return;
        }
        text.Length = length;
        for (int index = 0; index < length; ++index)
        {
            byte value = U8(ref reader);
            if (value == 0 || value > 0x7f) reader.Status = CaseDecodeStatus.Invalid;
            text.Bytes[index] = value;
        }
    }
}
