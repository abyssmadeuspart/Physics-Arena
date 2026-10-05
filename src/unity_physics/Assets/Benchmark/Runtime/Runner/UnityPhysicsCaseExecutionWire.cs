using System;
using Unity.Collections;
using Unity.Mathematics;

namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
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
        public float3 HalfExtents;
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

    public struct CaseExecutionBox
    {
        public float3 Center;
        public float3 HalfExtents;
    }

    public struct CaseExecutionOpenContainer
    {
        public uint3 DynamicGrid;
        public float3 DynamicHalfExtents;
        public float3 DynamicSpacing;
        public float DynamicInitialY;
        public float Density;
        public FixedList4096Bytes<CaseExecutionBox> StaticBoxes;
    }

    public struct CaseExecutionContactIslands
    {
        public uint2 IslandGrid;
        public float2 IslandSpacing;
        public uint3 BodyGrid;
        public float3 BodyHalfExtents;
        public float3 BodySpacing;
        public float BodyInitialY;
        public float3 FloorHalfExtents;
        public float Density;
    }

    public struct CaseExecutionSpatialQuery
    {
        public uint3 StaticGrid;
        public float3 StaticHalfExtents;
        public float3 StaticSpacing;
        public float3 StaticBaseCenter;
        public uint RayCount;
        public uint SphereCastCount;
        public uint OverlapCount;
        public float QueryDistance;
        public float SphereCastRadius;
        public float3 OverlapHalfExtents;
        public float MissOffset;
        public uint DebugSamplesPerFamily;
    }

    public struct CaseExecutionRagdollPart
    {
        public CaseExecutionShape Shape;
        public float3 Center;
        public float3 HalfExtents;
        public float Radius;
        public float HalfSegment;
        public CaseExecutionAxis Axis;
    }

    public struct CaseExecutionRagdollLink
    {
        public ushort ParentPart;
        public ushort ChildPart;
        public float3 Anchor;
        public float3 ParentLocalAnchor;
        public float3 ChildLocalAnchor;
    }

    public struct CaseExecutionRagdoll
    {
        public uint2 RagdollGrid;
        public float ColumnSpacing;
        public float RowSpacing;
        public float BaseHeightOffset;
        public float PitchDegrees;
        public FixedList4096Bytes<float> YawPatternDegrees;
        public float TriggerRowSpeed;
        public float FollowerRowSpeed;
        public uint StairCount;
        public float StairRise;
        public float StairDepth;
        public float StairHalfWidth;
        public float StairHalfHeight;
        public float StairHalfDepth;
        public FixedList4096Bytes<CaseExecutionBox> ExtraStaticBoxes;
        public FixedList4096Bytes<CaseExecutionRagdollPart> Parts;
        public FixedList4096Bytes<CaseExecutionRagdollLink> Links;
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
        public float3 FloorHalfExtents;
    }

    public struct CaseExecutionLargePyramid
    {
        public uint RowCount;
        public float3 BoxHalfExtents;
        public float3 BoxSpacing;
        public float3 BaseCenter;
        public float3 FloorHalfExtents;
        public float BoxDensity;
        public uint ProjectileCount;
        public float ProjectileRadius;
        public float ProjectileDensity;
        public float3 ProjectileInitialCenter;
        public float3 ProjectileCenterSpacing;
        public float3 ProjectileLaunchVelocity;
        public uint ProjectileLaunchAfterWorkUnits;
    }

    public struct CaseExecutionCamera
    {
        public float3 Direction;
        public float3 Up;
        public float3 Minimum;
        public float3 Maximum;
        public float3 Eye;
        public float3 Target;
        public float3 EyeOffset;
        public float3 TargetOffset;
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
        public FixedList512Bytes<float3> HullPoints;
        public FixedString64Bytes CaseId;
        public FixedString64Bytes FixtureSemantic;
        public CaseExecutionOpenContainer OpenContainer;
        public CaseExecutionContactIslands ContactIslands;
        public CaseExecutionSpatialQuery SpatialQuery;
        public CaseExecutionRagdoll Ragdoll;
        public CaseExecutionLargePyramid LargePyramid;
        public CaseExecutionPyramidWall PyramidWall;
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
        public CaseFixtureKind FixtureKind;
        public byte TimestepPresent;
        public float3 Gravity;
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
    }

    public static class UnityPhysicsCaseExecutionWire
    {
        public const int PayloadCapacity = 2048;
        public const int HexCapacity = PayloadCapacity * 2;
        public const int StaticBoxCapacity = 16;
        public const int RagdollPartCapacity = 32;
        public const int RagdollLinkCapacity = 32;
        public const int YawCapacity = 16;

        public static int DecodeHex(string hex, out CaseExecutionSpec spec)
        {
            spec = default;
            if (string.IsNullOrEmpty(hex) || hex.Length > HexCapacity || (hex.Length & 1) != 0)
            {
                return 2;
            }
            FixedList4096Bytes<byte> bytes = default;
            int byteCount = hex.Length / 2;
            if (byteCount > PayloadCapacity)
            {
                return 2;
            }
            bytes.Length = byteCount;
            for (int index = 0; index < byteCount; ++index)
            {
                int high = Nibble(hex[index * 2]);
                int low = Nibble(hex[index * 2 + 1]);
                if (high < 0 || low < 0)
                {
                    return 2;
                }
                bytes[index] = (byte)((high << 4) | low);
            }
            return Decode(in bytes, out spec);
        }

        public static int Decode(
            in FixedList4096Bytes<byte> bytes, out CaseExecutionSpec spec)
        {
            spec = default;
            if (bytes.Length < 10 || bytes.Length > PayloadCapacity)
            {
                return 2;
            }
            Reader reader = new Reader { Bytes = bytes, Status = 1 };
            if (ReadByte(ref reader) != (byte)'P' || ReadByte(ref reader) != (byte)'A' ||
                ReadByte(ref reader) != (byte)'C' || ReadByte(ref reader) != (byte)'X')
            {
                return 2;
            }
            spec.FixtureKind = (CaseFixtureKind)ReadUShort(ref reader);
            if (spec.FixtureKind < CaseFixtureKind.OpenContainerFallingPile ||
                spec.FixtureKind > CaseFixtureKind.PyramidWall ||
                ReadUInt(ref reader) != bytes.Length)
            {
                return 2;
            }
            ReadText(ref reader, ref spec.CaseId);
            ReadText(ref reader, ref spec.FixtureSemantic);
            spec.FixtureRevision = ReadUInt(ref reader);
            spec.DynamicBodyCount = ReadUInt(ref reader);
            spec.KinematicBodyCount = ReadUInt(ref reader);
            spec.StaticBodyCount = ReadUInt(ref reader);
            spec.BodyCount = ReadUInt(ref reader);
            spec.ShapeCount = ReadUInt(ref reader);
            spec.VisualInstanceCount = ReadUInt(ref reader);
            spec.MeshTriangleCount = ReadUInt(ref reader);
            spec.QueryCount = ReadUInt(ref reader);
            spec.ConstraintCount = ReadUInt(ref reader);
            spec.TimestepPresent = ReadByte(ref reader);
            spec.TimestepHz = ReadUInt(ref reader);
            spec.WarmupWorkUnitCount = ReadUInt(ref reader);
            spec.MeasuredWorkUnitCount = ReadUInt(ref reader);
            spec.VisualDebugPrimitiveCount = ReadUInt(ref reader);
            spec.Gravity = ReadFloat3(ref reader);
            spec.SleepMode = ReadToggle(ref reader);
            spec.ContinuousCollisionMode = ReadToggle(ref reader);
            spec.Friction = ReadFloat(ref reader);
            spec.Restitution = ReadFloat(ref reader);
            spec.SolverFields = ReadUInt(ref reader);
            spec.VelocityIterations = ReadUInt(ref reader);
            spec.PositionIterations = ReadUInt(ref reader);
            spec.ProjectionIterations = ReadUInt(ref reader);
            spec.SolverIterations = ReadUInt(ref reader);
            spec.Substeps = ReadUInt(ref reader);
            spec.CollisionSteps = ReadUInt(ref reader);
            spec.ReplayCamera.Direction = ReadFloat3(ref reader);
            spec.ReplayCamera.Up = ReadFloat3(ref reader);
            spec.ReplayCamera.Minimum = ReadFloat3(ref reader);
            spec.ReplayCamera.Maximum = ReadFloat3(ref reader);
            spec.ReplayCamera.Eye = ReadFloat3(ref reader);
            spec.ReplayCamera.Target = ReadFloat3(ref reader);
            spec.ReplayCamera.EyeOffset = ReadFloat3(ref reader);
            spec.ReplayCamera.TargetOffset = ReadFloat3(ref reader);
            spec.ReplayCamera.VerticalFovDegrees = ReadFloat(ref reader);
            spec.ReplayCamera.ViewportFill = ReadFloat(ref reader);
            spec.ReplayCamera.NearPlane = ReadFloat(ref reader);
            spec.ReplayCamera.FarPlane = ReadFloat(ref reader);
            spec.ReplayCamera.StableSlot = ReadUInt(ref reader);
            spec.ReplayCamera.Mode = ReadUInt(ref reader);
            if ((spec.SolverFields & ~63u) != 0 ||
                (spec.FixtureKind == CaseFixtureKind.SpatialQueryTrace && spec.SolverFields != 0)) return 2;
            if (((spec.SolverFields & 1u) == 0 && spec.VelocityIterations != 0) ||
                ((spec.SolverFields & 2u) == 0 && spec.PositionIterations != 0) ||
                ((spec.SolverFields & 4u) == 0 && spec.ProjectionIterations != 0) ||
                ((spec.SolverFields & 8u) == 0 && spec.SolverIterations != 0) ||
                ((spec.SolverFields & 16u) == 0 && spec.Substeps != 0) ||
                ((spec.SolverFields & 32u) == 0 && spec.CollisionSteps != 0)) return 2;
            if (spec.FixtureKind != CaseFixtureKind.SpatialQueryTrace &&
                (spec.SolverFields != 24u || spec.SolverIterations == 0 || spec.SolverIterations > int.MaxValue ||
                 spec.Substeps == 0 || spec.Substeps > int.MaxValue)) return 2;
            spec.ShapePreset = (CaseShapePreset)ReadByte(ref reader);
            if (spec.ShapePreset > CaseShapePreset.ConvexHull) return 2;
            if (spec.ShapePreset == CaseShapePreset.ConvexHull)
                for (int index = 0; index < 24; ++index) spec.HullPoints.Add(ReadFloat3(ref reader));
            if (spec.FixtureKind != CaseFixtureKind.RagdollStairTumble)
                spec.SelectedGeometry = ReadGeometry(ref reader);

            switch (spec.FixtureKind)
            {
                case CaseFixtureKind.OpenContainerFallingPile:
                    spec.OpenContainer.DynamicGrid = ReadUInt3(ref reader);
                    spec.OpenContainer.DynamicHalfExtents = ReadFloat3(ref reader);
                    spec.OpenContainer.DynamicSpacing = ReadFloat3(ref reader);
                    spec.OpenContainer.DynamicInitialY = ReadFloat(ref reader);
                    spec.OpenContainer.Density = ReadFloat(ref reader);
                    int staticBoxCount = ReadUShort(ref reader);
                    if (staticBoxCount > StaticBoxCapacity)
                    {
                        reader.Status = 0;
                    }
                    for (int index = 0; reader.Status != 0 && index < staticBoxCount; ++index)
                    {
                        spec.OpenContainer.StaticBoxes.Add(ReadBox(ref reader));
                    }
                    break;
                case CaseFixtureKind.BoxContactIslands:
                    spec.ContactIslands.IslandGrid = ReadUInt2(ref reader);
                    spec.ContactIslands.IslandSpacing = ReadFloat2(ref reader);
                    spec.ContactIslands.BodyGrid = ReadUInt3(ref reader);
                    spec.ContactIslands.BodyHalfExtents = ReadFloat3(ref reader);
                    spec.ContactIslands.BodySpacing = ReadFloat3(ref reader);
                    spec.ContactIslands.BodyInitialY = ReadFloat(ref reader);
                    spec.ContactIslands.FloorHalfExtents = ReadFloat3(ref reader);
                    spec.ContactIslands.Density = ReadFloat(ref reader);
                    break;
                case CaseFixtureKind.SpatialQueryTrace:
                    spec.SpatialQuery.StaticGrid = ReadUInt3(ref reader);
                    spec.SpatialQuery.StaticHalfExtents = ReadFloat3(ref reader);
                    spec.SpatialQuery.StaticSpacing = ReadFloat3(ref reader);
                    spec.SpatialQuery.StaticBaseCenter = ReadFloat3(ref reader);
                    spec.SpatialQuery.RayCount = ReadUInt(ref reader);
                    spec.SpatialQuery.SphereCastCount = ReadUInt(ref reader);
                    spec.SpatialQuery.OverlapCount = ReadUInt(ref reader);
                    spec.SpatialQuery.QueryDistance = ReadFloat(ref reader);
                    spec.SpatialQuery.SphereCastRadius = ReadFloat(ref reader);
                    spec.SpatialQuery.OverlapHalfExtents = ReadFloat3(ref reader);
                    spec.SpatialQuery.MissOffset = ReadFloat(ref reader);
                    spec.SpatialQuery.DebugSamplesPerFamily = ReadUInt(ref reader);
                    break;
                case CaseFixtureKind.RagdollStairTumble:
                    spec.Ragdoll.LinkedCollisionMode = ReadToggle(ref reader);
                    spec.Ragdoll.LinearDamping = ReadFloat(ref reader);
                    spec.Ragdoll.AngularDamping = ReadFloat(ref reader);
                    spec.Ragdoll.PartMass = ReadFloat(ref reader);
                    spec.Ragdoll.RagdollGrid = ReadUInt2(ref reader);
                    spec.Ragdoll.ColumnSpacing = ReadFloat(ref reader);
                    spec.Ragdoll.RowSpacing = ReadFloat(ref reader);
                    spec.Ragdoll.BaseHeightOffset = ReadFloat(ref reader);
                    spec.Ragdoll.PitchDegrees = ReadFloat(ref reader);
                    int yawCount = ReadUShort(ref reader);
                    if (yawCount == 0 || yawCount > YawCapacity)
                    {
                        reader.Status = 0;
                    }
                    for (int index = 0; reader.Status != 0 && index < yawCount; ++index)
                    {
                        spec.Ragdoll.YawPatternDegrees.Add(ReadFloat(ref reader));
                    }
                    spec.Ragdoll.TriggerRowSpeed = ReadFloat(ref reader);
                    spec.Ragdoll.FollowerRowSpeed = ReadFloat(ref reader);
                    spec.Ragdoll.StairCount = ReadUInt(ref reader);
                    spec.Ragdoll.StairRise = ReadFloat(ref reader);
                    spec.Ragdoll.StairDepth = ReadFloat(ref reader);
                    spec.Ragdoll.StairHalfWidth = ReadFloat(ref reader);
                    spec.Ragdoll.StairHalfHeight = ReadFloat(ref reader);
                    spec.Ragdoll.StairHalfDepth = ReadFloat(ref reader);
                    int extraBoxCount = ReadUShort(ref reader);
                    if (extraBoxCount > StaticBoxCapacity)
                    {
                        reader.Status = 0;
                    }
                    for (int index = 0; reader.Status != 0 && index < extraBoxCount; ++index)
                    {
                        spec.Ragdoll.ExtraStaticBoxes.Add(ReadBox(ref reader));
                    }
                    int partCount = ReadUShort(ref reader);
                    if (partCount == 0 || partCount > RagdollPartCapacity)
                    {
                        reader.Status = 0;
                    }
                    for (int index = 0; reader.Status != 0 && index < partCount; ++index)
                    {
                        CaseExecutionRagdollPart part = default;
                        part.Center = ReadFloat3(ref reader);
                        CaseExecutionGeometry geometry = ReadGeometry(ref reader);
                        part.Shape = geometry.Shape;
                        part.HalfExtents = geometry.HalfExtents;
                        part.Radius = geometry.Radius;
                        part.HalfSegment = geometry.HalfSegment;
                        part.Axis = geometry.Axis;
                        spec.Ragdoll.Parts.Add(part);
                    }
                    int linkCount = ReadUShort(ref reader);
                    if (linkCount > RagdollLinkCapacity)
                    {
                        reader.Status = 0;
                    }
                    for (int index = 0; reader.Status != 0 && index < linkCount; ++index)
                    {
                        CaseExecutionRagdollLink link = default;
                        link.ParentPart = ReadUShort(ref reader);
                        link.ChildPart = ReadUShort(ref reader);
                        link.Anchor = ReadFloat3(ref reader);
                        link.ParentLocalAnchor = ReadFloat3(ref reader);
                        link.ChildLocalAnchor = ReadFloat3(ref reader);
                        if (link.ParentPart >= partCount || link.ChildPart >= partCount)
                        {
                            reader.Status = 0;
                        }
                        spec.Ragdoll.Links.Add(link);
                    }
                    break;
                case CaseFixtureKind.PyramidWall:
                    spec.PyramidWall.RowCount = ReadUInt(ref reader);
                    spec.PyramidWall.HalfExtent = ReadFloat(ref reader);
                    spec.PyramidWall.Density = ReadFloat(ref reader);
                    spec.PyramidWall.FloorHalfExtents = ReadFloat3(ref reader);
                    CaseExecutionPyramidWall wall = spec.PyramidWall;
                    ulong count = (ulong)wall.RowCount * (wall.RowCount + 1UL) / 2;
                    if (wall.RowCount == 0 || wall.RowCount > 180 || count > 16290 ||
                        wall.HalfExtent <= 0 || wall.Density <= 0 ||
                        wall.FloorHalfExtents.x <= 0 || wall.FloorHalfExtents.y <= 0 || wall.FloorHalfExtents.z <= 0 ||
                        spec.DynamicBodyCount != count || spec.StaticBodyCount != 1 || spec.KinematicBodyCount != 0 ||
                        spec.ShapeCount != count + 1 || spec.VisualInstanceCount != count + 1 || spec.MeshTriangleCount != 0 ||
                        spec.QueryCount != 0 || spec.ConstraintCount != 0 || spec.TimestepHz == 0 ||
                        spec.ShapePreset != CaseShapePreset.Authored || spec.ContinuousCollisionMode != CaseExecutionToggle.Disabled ||
                        spec.SelectedGeometry.Shape != CaseExecutionShape.Box ||
                        spec.SelectedGeometry.HalfExtents.x != wall.HalfExtent ||
                        spec.SelectedGeometry.HalfExtents.y != wall.HalfExtent ||
                        spec.SelectedGeometry.HalfExtents.z != wall.HalfExtent) return 2;
                    break;
                case CaseFixtureKind.LargePyramid:
                    spec.LargePyramid.RowCount = ReadUInt(ref reader);
                    spec.LargePyramid.BoxHalfExtents = ReadFloat3(ref reader);
                    spec.LargePyramid.BoxSpacing = ReadFloat3(ref reader);
                    spec.LargePyramid.BaseCenter = ReadFloat3(ref reader);
                    spec.LargePyramid.FloorHalfExtents = ReadFloat3(ref reader);
                    spec.LargePyramid.BoxDensity = ReadFloat(ref reader);
                    spec.LargePyramid.ProjectileCount = ReadUInt(ref reader);
                    spec.LargePyramid.ProjectileRadius = ReadFloat(ref reader);
                    spec.LargePyramid.ProjectileDensity = ReadFloat(ref reader);
                    spec.LargePyramid.ProjectileInitialCenter = ReadFloat3(ref reader);
                    spec.LargePyramid.ProjectileCenterSpacing = ReadFloat3(ref reader);
                    spec.LargePyramid.ProjectileLaunchVelocity = ReadFloat3(ref reader);
                    spec.LargePyramid.ProjectileLaunchAfterWorkUnits = ReadUInt(ref reader);
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
                for (int index = 0; index < spec.Ragdoll.Parts.Length; ++index)
                {
                    CaseExecutionShape shape = spec.Ragdoll.Parts[index].Shape;
                    if (shape != CaseExecutionShape.Sphere && shape != expectedShape) return 2;
                }
            }
            else if (spec.SelectedGeometry.Shape != expectedShape) return 2;

            FixedString64Bytes expectedSemantic = (spec.FixtureKind, spec.ShapePreset) switch
            {
                (CaseFixtureKind.OpenContainerFallingPile, CaseShapePreset.Authored) => "open_container_falling_pile",
                (CaseFixtureKind.OpenContainerFallingPile, CaseShapePreset.Sphere) => "open_container_falling_pile_sphere",
                (CaseFixtureKind.OpenContainerFallingPile, CaseShapePreset.Capsule) => "open_container_falling_pile_capsule",
                (CaseFixtureKind.OpenContainerFallingPile, CaseShapePreset.ConvexHull) => "open_container_falling_pile_convex_hull",
                (CaseFixtureKind.BoxContactIslands, CaseShapePreset.Authored) => "box_contact_islands_10k",
                (CaseFixtureKind.BoxContactIslands, CaseShapePreset.Sphere) => "box_contact_islands_10k_sphere",
                (CaseFixtureKind.BoxContactIslands, CaseShapePreset.Capsule) => "box_contact_islands_10k_capsule",
                (CaseFixtureKind.BoxContactIslands, CaseShapePreset.ConvexHull) => "box_contact_islands_10k_convex_hull",
                (CaseFixtureKind.SpatialQueryTrace, CaseShapePreset.Authored) => "spatial_query_trace",
                (CaseFixtureKind.SpatialQueryTrace, CaseShapePreset.Sphere) => "spatial_query_trace_sphere",
                (CaseFixtureKind.SpatialQueryTrace, CaseShapePreset.Capsule) => "spatial_query_trace_capsule",
                (CaseFixtureKind.SpatialQueryTrace, CaseShapePreset.ConvexHull) => "spatial_query_trace_convex_hull",
                (CaseFixtureKind.RagdollStairTumble, CaseShapePreset.Authored) => "ragdoll_stair_tumble",
                (CaseFixtureKind.RagdollStairTumble, CaseShapePreset.Sphere) => "ragdoll_stair_tumble_sphere",
                (CaseFixtureKind.RagdollStairTumble, CaseShapePreset.Capsule) => "ragdoll_stair_tumble_capsule",
                (CaseFixtureKind.RagdollStairTumble, CaseShapePreset.ConvexHull) => "ragdoll_stair_tumble_convex_hull",
                (CaseFixtureKind.PyramidWall, CaseShapePreset.Authored) => "pyramid_wall",
                (CaseFixtureKind.LargePyramid, CaseShapePreset.Authored) => "large_pyramid",
                (CaseFixtureKind.LargePyramid, CaseShapePreset.Sphere) => "large_pyramid_sphere",
                (CaseFixtureKind.LargePyramid, CaseShapePreset.Capsule) => "large_pyramid_capsule",
                (CaseFixtureKind.LargePyramid, CaseShapePreset.ConvexHull) => "large_pyramid_convex_hull",
                _ => default
            };
            if (reader.Status == 0 || reader.Offset != bytes.Length ||
                !spec.FixtureSemantic.Equals(expectedSemantic) || spec.FixtureRevision == 0 ||
                spec.MeasuredWorkUnitCount == 0 ||
                spec.BodyCount != spec.DynamicBodyCount + spec.KinematicBodyCount + spec.StaticBodyCount ||
                spec.VisualInstanceCount == 0 || spec.VisualInstanceCount > spec.BodyCount ||
                spec.TimestepPresent > 1 || (spec.TimestepPresent == 0) != (spec.TimestepHz == 0))
            {
                return 2;
            }
            return 0;
        }

        public static CaseExecutionGeometry ReadGeometry(ref Reader reader)
        {
            CaseExecutionGeometry geometry = default;
            geometry.Shape = (CaseExecutionShape)ReadByte(ref reader);
            if (geometry.Shape == CaseExecutionShape.Box || geometry.Shape == CaseExecutionShape.ConvexHull)
            {
                geometry.HalfExtents = ReadFloat3(ref reader);
                if (geometry.HalfExtents.x <= 0.0f || geometry.HalfExtents.y <= 0.0f || geometry.HalfExtents.z <= 0.0f)
                    reader.Status = 0;
            }
            else if (geometry.Shape == CaseExecutionShape.Sphere || geometry.Shape == CaseExecutionShape.Capsule)
            {
                geometry.Radius = ReadFloat(ref reader);
                if (geometry.Radius <= 0.0f) reader.Status = 0;
                if (geometry.Shape == CaseExecutionShape.Capsule)
                {
                    geometry.HalfSegment = ReadFloat(ref reader);
                    geometry.Axis = (CaseExecutionAxis)ReadByte(ref reader);
                    if (geometry.HalfSegment <= 0.0f || geometry.Axis > CaseExecutionAxis.Z)
                        reader.Status = 0;
                }
            }
            else reader.Status = 0;
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

        public static byte ReadByte(ref Reader reader)
        {
            if (reader.Offset >= reader.Bytes.Length)
            {
                reader.Status = 0;
                return 0;
            }
            return reader.Bytes[reader.Offset++];
        }

        public static ushort ReadUShort(ref Reader reader)
        {
            ushort low = ReadByte(ref reader);
            ushort high = ReadByte(ref reader);
            return (ushort)(low | (high << 8));
        }

        public static uint ReadUInt(ref Reader reader)
        {
            uint low = ReadUShort(ref reader);
            uint high = ReadUShort(ref reader);
            return low | (high << 16);
        }

        public static float ReadFloat(ref Reader reader)
        {
            float value = BitConverter.Int32BitsToSingle((int)ReadUInt(ref reader));
            if (!math.isfinite(value))
            {
                reader.Status = 0;
            }
            return value;
        }

        public static uint2 ReadUInt2(ref Reader reader)
        {
            return new uint2(ReadUInt(ref reader), ReadUInt(ref reader));
        }

        public static uint3 ReadUInt3(ref Reader reader)
        {
            return new uint3(ReadUInt(ref reader), ReadUInt(ref reader), ReadUInt(ref reader));
        }

        public static float2 ReadFloat2(ref Reader reader)
        {
            return new float2(ReadFloat(ref reader), ReadFloat(ref reader));
        }

        public static float3 ReadFloat3(ref Reader reader)
        {
            return new float3(ReadFloat(ref reader), ReadFloat(ref reader), ReadFloat(ref reader));
        }

        public static CaseExecutionBox ReadBox(ref Reader reader)
        {
            return new CaseExecutionBox
            {
                Center = ReadFloat3(ref reader),
                HalfExtents = ReadFloat3(ref reader)
            };
        }

        public static CaseExecutionToggle ReadToggle(ref Reader reader)
        {
            byte value = ReadByte(ref reader);
            if (value > 1)
            {
                reader.Status = 0;
            }
            return (CaseExecutionToggle)value;
        }

        public static void ReadText(ref Reader reader, ref FixedString64Bytes text)
        {
            int length = ReadUShort(ref reader);
            if (length == 0 || length >= 64 || length > reader.Bytes.Length - reader.Offset)
            {
                reader.Status = 0;
                return;
            }
            for (int index = 0; index < length; ++index)
            {
                byte value = ReadByte(ref reader);
                if (value == 0 || value > 0x7f)
                {
                    reader.Status = 0;
                }
                text.Append((char)value);
            }
        }

        public struct Reader
        {
            public FixedList4096Bytes<byte> Bytes;
            public int Offset;
            public byte Status;
        }
    }
}
