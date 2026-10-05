#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <initializer_list>

// the host and its private producers consume this current lockstep layout
constexpr std::uint32_t kCaseExecutionPayloadCapacity = 2048;
constexpr std::uint32_t kCaseExecutionHexCapacity = kCaseExecutionPayloadCapacity * 2;
constexpr std::uint32_t kCaseExecutionStaticBoxCapacity = 16;
constexpr std::uint32_t kCaseExecutionRagdollPartCapacity = 32;
constexpr std::uint32_t kCaseExecutionRagdollLinkCapacity = 32;
constexpr std::uint32_t kCaseExecutionYawCapacity = 16;
constexpr std::uint32_t kCaseExecutionTextCapacity = 64;
constexpr std::uint32_t kCaseExecutionHullPointCount = 24;

enum CaseExecutionDecodeStatus
{
	CaseExecutionDecodeStatus_Ok = 0,
	CaseExecutionDecodeStatus_Invalid = 1,
	CaseExecutionDecodeStatus_Capacity = 2,
};

enum CaseFixtureKind
{
	CaseFixtureKind_Unknown = 0,
	CaseFixtureKind_OpenContainerFallingPile = 1,
	CaseFixtureKind_BoxContactIslands = 2,
	CaseFixtureKind_SpatialQueryTrace = 3,
	CaseFixtureKind_RagdollStairTumble = 4,
	CaseFixtureKind_LargePyramid = 5,
	CaseFixtureKind_PyramidWall = 6,
	CaseFixtureKind_RayTracing = 7,
};

inline int CaseExecutionIsQuery(CaseFixtureKind kind)
{
	return kind == CaseFixtureKind_SpatialQueryTrace || kind == CaseFixtureKind_RayTracing;
}

enum CaseExecutionShape
{
	CaseExecutionShape_Unknown = 0,
	CaseExecutionShape_Box = 1,
	CaseExecutionShape_Sphere = 2,
	CaseExecutionShape_Capsule = 3,
	CaseExecutionShape_ConvexHull = 4,
};

enum CaseShapePreset
{
	CaseShapePreset_Authored = 0,
	CaseShapePreset_Sphere = 1,
	CaseShapePreset_Capsule = 2,
	CaseShapePreset_ConvexHull = 3,
};

enum CaseExecutionAxis
{
	CaseExecutionAxis_Y = 0,
	CaseExecutionAxis_X = 1,
	CaseExecutionAxis_Z = 2,
};

enum CaseExecutionToggle
{
	CaseExecutionToggle_Disabled = 0,
	CaseExecutionToggle_Enabled = 1,
};

enum CaseSolverField
{
	CaseSolverField_VelocityIterations = 0,
	CaseSolverField_PositionIterations = 1,
	CaseSolverField_ProjectionIterations = 2,
	CaseSolverField_SolverIterations = 3,
	CaseSolverField_Substeps = 4,
	CaseSolverField_CollisionSteps = 5,
	CaseSolverField_Count = 6,
};

struct CaseNativeSolver
{
	std::uint32_t values[CaseSolverField_Count];
	std::uint32_t supportedFields;
};

struct CaseExecutionVector3
{
	float x;
	float y;
	float z;
};

struct CaseExecutionCamera
{
	CaseExecutionVector3 direction;
	CaseExecutionVector3 up;
	CaseExecutionVector3 minimum;
	CaseExecutionVector3 maximum;
	CaseExecutionVector3 eye;
	CaseExecutionVector3 target;
	CaseExecutionVector3 eyeOffset;
	CaseExecutionVector3 targetOffset;
	float verticalFovDegrees;
	float viewportFill;
	float nearPlane;
	float farPlane;
	std::uint32_t stableSlot;
	std::uint32_t mode;
};

struct CaseExecutionBox
{
	CaseExecutionVector3 center;
	CaseExecutionVector3 halfExtents;
};

struct CaseExecutionGeometry
{
	CaseExecutionShape shape;
	CaseExecutionVector3 halfExtents;
	float radius;
	float halfSegment;
	CaseExecutionAxis axis;
};

struct CaseExecutionQuaternion
{
	float x;
	float y;
	float z;
	float w;
};

struct CaseExecutionOpenContainer
{
	std::uint32_t dynamicGrid[3];
	CaseExecutionVector3 dynamicHalfExtents;
	CaseExecutionVector3 dynamicSpacing;
	float dynamicInitialY;
	float density;
	CaseExecutionBox staticBoxes[kCaseExecutionStaticBoxCapacity];
	std::uint16_t staticBoxCount;
};

struct CaseExecutionContactIslands
{
	std::uint32_t islandGrid[2];
	float islandSpacing[2];
	std::uint32_t bodyGrid[3];
	CaseExecutionVector3 bodyHalfExtents;
	CaseExecutionVector3 bodySpacing;
	float bodyInitialY;
	CaseExecutionVector3 floorHalfExtents;
	float density;
};

struct CaseExecutionSpatialQuery
{
	std::uint32_t staticGrid[3];
	CaseExecutionVector3 staticHalfExtents;
	CaseExecutionVector3 staticSpacing;
	CaseExecutionVector3 staticBaseCenter;
	std::uint32_t rayCount;
	std::uint32_t sphereCastCount;
	std::uint32_t overlapCount;
	float queryDistance;
	float sphereCastRadius;
	CaseExecutionVector3 overlapHalfExtents;
	float missOffset;
	std::uint32_t debugSamplesPerFamily;
};

struct CaseExecutionRagdollPart
{
	CaseExecutionShape shape;
	CaseExecutionVector3 center;
	CaseExecutionVector3 halfExtents;
	float radius;
	float halfSegment;
	CaseExecutionAxis axis;
};

struct CaseExecutionRagdollLink
{
	std::uint16_t parentPart;
	std::uint16_t childPart;
	CaseExecutionVector3 anchor;
	CaseExecutionVector3 parentLocalAnchor;
	CaseExecutionVector3 childLocalAnchor;
};

struct CaseExecutionRagdoll
{
	std::uint32_t ragdollGrid[2];
	float columnSpacing;
	float rowSpacing;
	float baseHeightOffset;
	float pitchDegrees;
	float yawPatternDegrees[kCaseExecutionYawCapacity];
	std::uint16_t yawPatternCount;
	float triggerRowSpeed;
	float followerRowSpeed;
	std::uint32_t stairCount;
	float stairRise;
	float stairDepth;
	float stairHalfWidth;
	float stairHalfHeight;
	float stairHalfDepth;
	CaseExecutionBox extraStaticBoxes[kCaseExecutionStaticBoxCapacity];
	std::uint16_t extraStaticBoxCount;
	CaseExecutionRagdollPart parts[kCaseExecutionRagdollPartCapacity];
	std::uint16_t partCount;
	CaseExecutionRagdollLink links[kCaseExecutionRagdollLinkCapacity];
	std::uint16_t linkCount;
	float linearDamping;
	float angularDamping;
	float partMass;
	CaseExecutionToggle linkedCollisionMode;
};

struct CaseExecutionLargePyramid
{
	std::uint32_t rowCount;
	CaseExecutionVector3 boxHalfExtents;
	CaseExecutionVector3 boxSpacing;
	CaseExecutionVector3 baseCenter;
	CaseExecutionVector3 floorHalfExtents;
	float boxDensity;
	std::uint32_t projectileCount;
	float projectileRadius;
	float projectileDensity;
	CaseExecutionVector3 projectileInitialCenter;
	CaseExecutionVector3 projectileCenterSpacing;
	CaseExecutionVector3 projectileLaunchVelocity;
	std::uint32_t projectileLaunchAfterWorkUnits;
};

struct CaseExecutionPyramidWall
{
	std::uint32_t rowCount;
	float halfExtent;
	float density;
	CaseExecutionVector3 floorHalfExtents;
};

struct CaseExecutionRayTracing
{
	std::uint32_t recipeRevision;
	std::uint32_t width;
	std::uint32_t height;
	std::uint32_t viewCount;
	std::uint32_t primitiveCount;
	std::uint32_t meshCount;
	std::uint32_t trianglesPerMesh;
	std::uint32_t movingCount;
	std::uint32_t seedLow;
	std::uint32_t seedHigh;
};

struct CaseExecutionSpec
{
	CaseFixtureKind fixtureKind;
	CaseShapePreset shapePreset;
	CaseExecutionGeometry selectedGeometry;
	CaseExecutionVector3 hullPoints[kCaseExecutionHullPointCount];
	char caseId[kCaseExecutionTextCapacity];
	char fixtureSemantic[kCaseExecutionTextCapacity];
	std::uint32_t fixtureRevision;
	std::uint32_t dynamicBodyCount;
	std::uint32_t kinematicBodyCount;
	std::uint32_t staticBodyCount;
	std::uint32_t bodyCount;
	std::uint32_t shapeCount;
	std::uint32_t visualInstanceCount;
	std::uint32_t meshTriangleCount;
	std::uint32_t queryCount;
	std::uint32_t constraintCount;
	std::uint32_t timestepHz;
	std::uint32_t warmupWorkUnitCount;
	std::uint32_t measuredWorkUnitCount;
	std::uint32_t visualDebugPrimitiveCount;
	std::uint8_t timestepPresent;
	CaseExecutionVector3 gravity;
	float friction;
	float restitution;
	CaseExecutionToggle sleepMode;
	CaseExecutionToggle continuousCollisionMode;
	CaseNativeSolver nativeSolver;
	CaseExecutionCamera replayCamera;
	CaseExecutionOpenContainer openContainer;
	CaseExecutionContactIslands contactIslands;
	CaseExecutionSpatialQuery spatialQuery;
	CaseExecutionRagdoll ragdoll;
	CaseExecutionLargePyramid largePyramid;
	CaseExecutionPyramidWall pyramidWall;
	CaseExecutionRayTracing rayTracing;
};

inline CaseExecutionQuaternion CaseExecutionAxisRotation(CaseExecutionAxis axis)
{
	constexpr float halfSqrtTwo = 0.7071067811865475244f;
	if (axis == CaseExecutionAxis_X)
		return {0.0f, 0.0f, -halfSqrtTwo, halfSqrtTwo};
	if (axis == CaseExecutionAxis_Z)
		return {halfSqrtTwo, 0.0f, 0.0f, halfSqrtTwo};
	return {0.0f, 0.0f, 0.0f, 1.0f};
}

inline CaseExecutionVector3 CaseExecutionAxisLocalVector(CaseExecutionAxis axis, CaseExecutionVector3 value)
{
	if (axis == CaseExecutionAxis_X)
		return {-value.y, value.x, value.z};
	if (axis == CaseExecutionAxis_Z)
		return {value.x, value.z, -value.y};
	return value;
}

inline CaseExecutionGeometry CaseExecutionPartGeometry(const CaseExecutionRagdollPart& part)
{
	return {part.shape, part.halfExtents, part.radius, part.halfSegment, part.axis};
}

inline int CaseExecutionSameGeometry(const CaseExecutionGeometry& left, const CaseExecutionGeometry& right)
{
	return left.shape == right.shape && left.halfExtents.x == right.halfExtents.x &&
	       left.halfExtents.y == right.halfExtents.y && left.halfExtents.z == right.halfExtents.z &&
	       left.radius == right.radius && left.halfSegment == right.halfSegment && left.axis == right.axis;
}

namespace case_execution_wire_detail
{
struct Writer
{
	std::uint8_t* bytes;
	std::uint32_t capacity;
	std::uint32_t offset;
	int valid;
};

struct Reader
{
	const std::uint8_t* bytes;
	std::uint32_t size;
	std::uint32_t offset;
	int valid;
};

inline void WriteU8(Writer* writer, std::uint8_t value)
{
	if (writer->offset >= writer->capacity)
	{
		writer->valid = 0;
		return;
	}
	writer->bytes[writer->offset++] = value;
}

inline void WriteU16(Writer* writer, std::uint16_t value)
{
	WriteU8(writer, static_cast<std::uint8_t>(value));
	WriteU8(writer, static_cast<std::uint8_t>(value >> 8));
}

inline void WriteU32(Writer* writer, std::uint32_t value)
{
	WriteU16(writer, static_cast<std::uint16_t>(value));
	WriteU16(writer, static_cast<std::uint16_t>(value >> 16));
}

inline void WriteFloat(Writer* writer, float value)
{
	std::uint32_t bits = 0;
	static_assert(sizeof(bits) == sizeof(value));
	std::memcpy(&bits, &value, sizeof(bits));
	WriteU32(writer, bits);
}

inline void WriteVector3(Writer* writer, CaseExecutionVector3 value)
{
	WriteFloat(writer, value.x);
	WriteFloat(writer, value.y);
	WriteFloat(writer, value.z);
}

inline void WriteBox(Writer* writer, const CaseExecutionBox& value)
{
	WriteVector3(writer, value.center);
	WriteVector3(writer, value.halfExtents);
}

inline void WriteText(Writer* writer, const char* value)
{
	std::uint32_t size = 0;
	while (size < kCaseExecutionTextCapacity && value[size] != '\0')
		++size;
	if (size == 0 || size >= kCaseExecutionTextCapacity)
	{
		writer->valid = 0;
		return;
	}
	WriteU16(writer, static_cast<std::uint16_t>(size));
	for (std::uint32_t index = 0; index < size; ++index)
		WriteU8(writer, static_cast<std::uint8_t>(value[index]));
}

inline std::uint8_t ReadU8(Reader* reader)
{
	if (reader->offset >= reader->size)
	{
		reader->valid = 0;
		return 0;
	}
	return reader->bytes[reader->offset++];
}

inline std::uint16_t ReadU16(Reader* reader)
{
	const std::uint16_t low = ReadU8(reader);
	const std::uint16_t high = ReadU8(reader);
	return static_cast<std::uint16_t>(low | (high << 8));
}

inline std::uint32_t ReadU32(Reader* reader)
{
	const std::uint32_t low = ReadU16(reader);
	const std::uint32_t high = ReadU16(reader);
	return low | (high << 16);
}

inline float ReadFloat(Reader* reader)
{
	const std::uint32_t bits = ReadU32(reader);
	float value = 0.0f;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

inline CaseExecutionVector3 ReadVector3(Reader* reader)
{
	return {ReadFloat(reader), ReadFloat(reader), ReadFloat(reader)};
}

inline CaseExecutionBox ReadBox(Reader* reader)
{
	return {ReadVector3(reader), ReadVector3(reader)};
}

inline void ReadText(Reader* reader, char* output)
{
	const std::uint16_t size = ReadU16(reader);
	if (size == 0 || size >= kCaseExecutionTextCapacity || size > reader->size - reader->offset)
	{
		reader->valid = 0;
		return;
	}
	for (std::uint16_t index = 0; index < size; ++index)
	{
		const std::uint8_t value = ReadU8(reader);
		if (value == 0 || value > 0x7f)
			reader->valid = 0;
		output[index] = static_cast<char>(value);
	}
	output[size] = '\0';
}

inline int ValidKind(CaseFixtureKind kind)
{
	return kind >= CaseFixtureKind_OpenContainerFallingPile && kind <= CaseFixtureKind_RayTracing;
}

inline int ValidToggle(CaseExecutionToggle value)
{
	return value == CaseExecutionToggle_Disabled || value == CaseExecutionToggle_Enabled;
}

inline int ValidFloat(float value)
{
	return std::isfinite(value);
}

inline int ValidVector3(CaseExecutionVector3 value)
{
	return ValidFloat(value.x) && ValidFloat(value.y) && ValidFloat(value.z);
}

inline int ValidBox(const CaseExecutionBox& value)
{
	return ValidVector3(value.center) && ValidVector3(value.halfExtents);
}

inline const char* ExpectedSemantic(CaseFixtureKind kind, CaseShapePreset preset)
{
	if (!ValidKind(kind) || preset < CaseShapePreset_Authored || preset > CaseShapePreset_ConvexHull)
	{
		return nullptr;
	}
	if (kind == CaseFixtureKind_PyramidWall || kind == CaseFixtureKind_RayTracing)
	{
		if (preset != CaseShapePreset_Authored)
			return nullptr;
		return kind == CaseFixtureKind_PyramidWall ? "pyramid_wall" : "ray_tracing";
	}
	constexpr const char* semantics[5][4] = {
	    {"open_container_falling_pile", "open_container_falling_pile_sphere", "open_container_falling_pile_capsule",
		 "open_container_falling_pile_convex_hull"},
	    {"box_contact_islands_10k", "box_contact_islands_10k_sphere", "box_contact_islands_10k_capsule",
		 "box_contact_islands_10k_convex_hull"},
	    {"spatial_query_trace", "spatial_query_trace_sphere", "spatial_query_trace_capsule",
		 "spatial_query_trace_convex_hull"},
	    {"ragdoll_stair_tumble", "ragdoll_stair_tumble_sphere", "ragdoll_stair_tumble_capsule",
		 "ragdoll_stair_tumble_convex_hull"},
	    {"large_pyramid", "large_pyramid_sphere", "large_pyramid_capsule", "large_pyramid_convex_hull"},
	};
	return semantics[static_cast<std::uint32_t>(kind) - 1][static_cast<std::uint32_t>(preset)];
}

inline int ValidGeometry(const CaseExecutionGeometry& geometry)
{
	if (geometry.shape == CaseExecutionShape_Box || geometry.shape == CaseExecutionShape_ConvexHull)
	{
		return ValidVector3(geometry.halfExtents) && geometry.halfExtents.x > 0.0f && geometry.halfExtents.y > 0.0f &&
		       geometry.halfExtents.z > 0.0f && geometry.axis == CaseExecutionAxis_Y;
	}
	if (geometry.shape == CaseExecutionShape_Sphere)
		return ValidFloat(geometry.radius) && geometry.radius > 0.0f && geometry.axis == CaseExecutionAxis_Y;
	return geometry.shape == CaseExecutionShape_Capsule && ValidFloat(geometry.radius) && geometry.radius > 0.0f &&
	       ValidFloat(geometry.halfSegment) && geometry.halfSegment > 0.0f && geometry.axis >= CaseExecutionAxis_Y &&
	       geometry.axis <= CaseExecutionAxis_Z;
}

inline void WriteGeometry(Writer* writer, const CaseExecutionGeometry& geometry)
{
	WriteU8(writer, static_cast<std::uint8_t>(geometry.shape));
	if (geometry.shape == CaseExecutionShape_Box || geometry.shape == CaseExecutionShape_ConvexHull)
		WriteVector3(writer, geometry.halfExtents);
	else
	{
		WriteFloat(writer, geometry.radius);
		if (geometry.shape == CaseExecutionShape_Capsule)
		{
			WriteFloat(writer, geometry.halfSegment);
			WriteU8(writer, static_cast<std::uint8_t>(geometry.axis));
		}
	}
}

inline CaseExecutionGeometry ReadGeometry(Reader* reader)
{
	CaseExecutionGeometry geometry = {};
	geometry.shape = static_cast<CaseExecutionShape>(ReadU8(reader));
	if (geometry.shape == CaseExecutionShape_Box || geometry.shape == CaseExecutionShape_ConvexHull)
		geometry.halfExtents = ReadVector3(reader);
	else if (geometry.shape == CaseExecutionShape_Sphere || geometry.shape == CaseExecutionShape_Capsule)
	{
		geometry.radius = ReadFloat(reader);
		if (geometry.shape == CaseExecutionShape_Capsule)
		{
			geometry.halfSegment = ReadFloat(reader);
			geometry.axis = static_cast<CaseExecutionAxis>(ReadU8(reader));
		}
	}
	else
		reader->valid = 0;
	return geometry;
}

inline CaseExecutionShape PresetShape(CaseShapePreset preset)
{
	return preset == CaseShapePreset_Authored  ? CaseExecutionShape_Box
	       : preset == CaseShapePreset_Sphere  ? CaseExecutionShape_Sphere
	       : preset == CaseShapePreset_Capsule ? CaseExecutionShape_Capsule
	                                           : CaseExecutionShape_ConvexHull;
}

inline int ValidSpec(const CaseExecutionSpec& spec)
{
	if ((spec.nativeSolver.supportedFields & ~((1u << CaseSolverField_Count) - 1u)) != 0)
		return 0;
	for (std::uint32_t field = 0; field < CaseSolverField_Count; ++field)
		if ((spec.nativeSolver.supportedFields & (1u << field)) == 0 && spec.nativeSolver.values[field] != 0)
			return 0;
	if (CaseExecutionIsQuery(spec.fixtureKind) && spec.nativeSolver.supportedFields != 0)
		return 0;
	const char* semantic = ExpectedSemantic(spec.fixtureKind, spec.shapePreset);
	if (semantic == nullptr || std::strcmp(spec.fixtureSemantic, semantic) != 0 || spec.fixtureRevision == 0 ||
	    spec.measuredWorkUnitCount == 0 ||
	    static_cast<std::uint64_t>(spec.dynamicBodyCount) + spec.kinematicBodyCount + spec.staticBodyCount !=
	        spec.bodyCount ||
	    (spec.visualInstanceCount == 0 && spec.fixtureKind != CaseFixtureKind_RayTracing) ||
	    spec.visualInstanceCount > spec.bodyCount || spec.timestepPresent > 1 ||
	    (spec.timestepPresent == 0) != (spec.timestepHz == 0) || !ValidToggle(spec.sleepMode) ||
	    !ValidToggle(spec.continuousCollisionMode) || !ValidVector3(spec.gravity) || !ValidFloat(spec.friction) ||
	    !ValidFloat(spec.restitution))
		return 0;
	if (spec.fixtureKind != CaseFixtureKind_RagdollStairTumble && spec.fixtureKind != CaseFixtureKind_RayTracing &&
	    (!ValidGeometry(spec.selectedGeometry) || spec.selectedGeometry.shape != PresetShape(spec.shapePreset)))
		return 0;
	if (spec.shapePreset == CaseShapePreset_ConvexHull)
	{
		for (const CaseExecutionVector3 point : spec.hullPoints)
			if (!ValidVector3(point))
				return 0;
	}
	if (spec.fixtureKind == CaseFixtureKind_OpenContainerFallingPile)
	{
		if (spec.openContainer.staticBoxCount > kCaseExecutionStaticBoxCapacity ||
		    !ValidVector3(spec.openContainer.dynamicHalfExtents) || !ValidVector3(spec.openContainer.dynamicSpacing) ||
		    !ValidFloat(spec.openContainer.dynamicInitialY) || !ValidFloat(spec.openContainer.density))
			return 0;
		for (std::uint16_t index = 0; index < spec.openContainer.staticBoxCount; ++index)
			if (!ValidBox(spec.openContainer.staticBoxes[index]))
				return 0;
	}
	else if (spec.fixtureKind == CaseFixtureKind_BoxContactIslands)
	{
		if (!ValidFloat(spec.contactIslands.islandSpacing[0]) || !ValidFloat(spec.contactIslands.islandSpacing[1]) ||
		    !ValidVector3(spec.contactIslands.bodyHalfExtents) || !ValidVector3(spec.contactIslands.bodySpacing) ||
		    !ValidFloat(spec.contactIslands.bodyInitialY) || !ValidVector3(spec.contactIslands.floorHalfExtents) ||
		    !ValidFloat(spec.contactIslands.density))
			return 0;
	}
	else if (spec.fixtureKind == CaseFixtureKind_SpatialQueryTrace)
	{
		if (!ValidVector3(spec.spatialQuery.staticHalfExtents) || !ValidVector3(spec.spatialQuery.staticSpacing) ||
		    !ValidVector3(spec.spatialQuery.staticBaseCenter) || !ValidFloat(spec.spatialQuery.queryDistance) ||
		    !ValidFloat(spec.spatialQuery.sphereCastRadius) || !ValidVector3(spec.spatialQuery.overlapHalfExtents) ||
		    !ValidFloat(spec.spatialQuery.missOffset))
			return 0;
	}
	else if (spec.fixtureKind == CaseFixtureKind_RagdollStairTumble)
	{
		if (!ValidToggle(spec.ragdoll.linkedCollisionMode) || !ValidFloat(spec.ragdoll.linearDamping) ||
		    !ValidFloat(spec.ragdoll.angularDamping) || !ValidFloat(spec.ragdoll.partMass) ||
		    !ValidFloat(spec.ragdoll.columnSpacing) || !ValidFloat(spec.ragdoll.rowSpacing) ||
		    !ValidFloat(spec.ragdoll.baseHeightOffset) || !ValidFloat(spec.ragdoll.pitchDegrees) ||
		    !ValidFloat(spec.ragdoll.triggerRowSpeed) || !ValidFloat(spec.ragdoll.followerRowSpeed) ||
		    !ValidFloat(spec.ragdoll.stairRise) || !ValidFloat(spec.ragdoll.stairDepth) ||
		    !ValidFloat(spec.ragdoll.stairHalfWidth) || !ValidFloat(spec.ragdoll.stairHalfHeight) ||
		    !ValidFloat(spec.ragdoll.stairHalfDepth) || spec.ragdoll.yawPatternCount == 0 ||
		    spec.ragdoll.yawPatternCount > kCaseExecutionYawCapacity ||
		    spec.ragdoll.extraStaticBoxCount > kCaseExecutionStaticBoxCapacity || spec.ragdoll.partCount == 0 ||
		    spec.ragdoll.partCount > kCaseExecutionRagdollPartCapacity ||
		    spec.ragdoll.linkCount > kCaseExecutionRagdollLinkCapacity)
			return 0;
		for (std::uint16_t index = 0; index < spec.ragdoll.yawPatternCount; ++index)
			if (!ValidFloat(spec.ragdoll.yawPatternDegrees[index]))
				return 0;
		for (std::uint16_t index = 0; index < spec.ragdoll.extraStaticBoxCount; ++index)
			if (!ValidBox(spec.ragdoll.extraStaticBoxes[index]))
				return 0;
		for (std::uint16_t index = 0; index < spec.ragdoll.partCount; ++index)
		{
			const CaseExecutionRagdollPart& part = spec.ragdoll.parts[index];
			if (!ValidVector3(part.center) || !ValidGeometry(CaseExecutionPartGeometry(part)) ||
			    (part.shape != CaseExecutionShape_Sphere && part.shape != PresetShape(spec.shapePreset)))
				return 0;
		}
		for (std::uint16_t index = 0; index < spec.ragdoll.linkCount; ++index)
		{
			const CaseExecutionRagdollLink& link = spec.ragdoll.links[index];
			if (link.parentPart >= spec.ragdoll.partCount || link.childPart >= spec.ragdoll.partCount ||
			    !ValidVector3(link.anchor) || !ValidVector3(link.parentLocalAnchor) ||
			    !ValidVector3(link.childLocalAnchor))
				return 0;
		}
	}
	else if (spec.fixtureKind == CaseFixtureKind_PyramidWall)
	{
		const CaseExecutionPyramidWall& wall = spec.pyramidWall;
		const std::uint64_t count = static_cast<std::uint64_t>(wall.rowCount) * (wall.rowCount + 1ull) / 2;
		if (wall.rowCount == 0 || wall.rowCount > 180 || count > 16290 || !ValidFloat(wall.halfExtent) || wall.halfExtent <= 0 ||
		    !ValidFloat(wall.density) || wall.density <= 0 || !ValidVector3(wall.floorHalfExtents) ||
		    wall.floorHalfExtents.x <= 0 || wall.floorHalfExtents.y <= 0 || wall.floorHalfExtents.z <= 0 ||
		    spec.dynamicBodyCount != count || spec.staticBodyCount != 1 || spec.kinematicBodyCount != 0 ||
		    spec.shapeCount != count + 1 || spec.visualInstanceCount != count + 1 || spec.meshTriangleCount != 0 ||
		    spec.queryCount != 0 || spec.constraintCount != 0 || spec.timestepHz == 0)
			return 0;
	}
	else if (spec.fixtureKind == CaseFixtureKind_RayTracing)
	{
		const CaseExecutionRayTracing& ray = spec.rayTracing;
		if (ray.recipeRevision != 1 || ray.width == 0 || ray.width > 1920 || ray.height == 0 || ray.height > 1080 ||
		    ray.viewCount == 0 || ray.viewCount > 6 || ray.primitiveCount == 0 || ray.primitiveCount > 65536 ||
		    ray.meshCount > 1024 || ray.trianglesPerMesh == 0 || ray.trianglesPerMesh > 1024 ||
		    ray.movingCount > ray.primitiveCount ||
		    spec.dynamicBodyCount != 0 || spec.kinematicBodyCount != ray.movingCount ||
		    spec.staticBodyCount != ray.primitiveCount - ray.movingCount + ray.meshCount ||
		    spec.shapeCount != ray.primitiveCount + ray.meshCount || spec.visualInstanceCount != 0 ||
		    spec.visualDebugPrimitiveCount != 0 || spec.meshTriangleCount != ray.meshCount * ray.trianglesPerMesh ||
		    spec.queryCount != ray.width * ray.height || spec.constraintCount != 0 || spec.timestepPresent != 0 ||
		    spec.gravity.x != 0.0f || spec.gravity.y != 0.0f || spec.gravity.z != 0.0f ||
		    spec.sleepMode != CaseExecutionToggle_Disabled || spec.continuousCollisionMode != CaseExecutionToggle_Disabled ||
		    spec.friction != 0.0f || spec.restitution != 0.0f)
			return 0;
	}
	else
	{
		if (!ValidVector3(spec.largePyramid.boxHalfExtents) || !ValidVector3(spec.largePyramid.boxSpacing) ||
		    !ValidVector3(spec.largePyramid.baseCenter) || !ValidVector3(spec.largePyramid.floorHalfExtents) ||
		    !ValidFloat(spec.largePyramid.boxDensity) || spec.largePyramid.projectileCount == 0 ||
		    !ValidFloat(spec.largePyramid.projectileRadius) || !ValidFloat(spec.largePyramid.projectileDensity) ||
		    !ValidVector3(spec.largePyramid.projectileInitialCenter) ||
		    !ValidVector3(spec.largePyramid.projectileCenterSpacing) ||
		    !ValidVector3(spec.largePyramid.projectileLaunchVelocity))
			return 0;
	}
	return 1;
}
} // namespace case_execution_wire_detail

inline CaseExecutionDecodeStatus EncodeCaseExecution(const CaseExecutionSpec* spec, std::uint8_t* bytes,
                                                     std::uint32_t capacity, std::uint32_t* byteCount)
{
	using namespace case_execution_wire_detail;
	if (spec == nullptr || bytes == nullptr || byteCount == nullptr || capacity > kCaseExecutionPayloadCapacity ||
	    !ValidSpec(*spec))
		return CaseExecutionDecodeStatus_Invalid;
	Writer writer = {bytes, capacity, 0, 1};
	for (const std::uint8_t value : {'P', 'A', 'C', 'X'})
		WriteU8(&writer, value);
	WriteU16(&writer, static_cast<std::uint16_t>(spec->fixtureKind));
	const std::uint32_t sizeOffset = writer.offset;
	WriteU32(&writer, 0);
	WriteText(&writer, spec->caseId);
	WriteText(&writer, spec->fixtureSemantic);
	WriteU32(&writer, spec->fixtureRevision);
	for (const std::uint32_t value :
	     {spec->dynamicBodyCount, spec->kinematicBodyCount, spec->staticBodyCount, spec->bodyCount, spec->shapeCount,
	      spec->visualInstanceCount, spec->meshTriangleCount, spec->queryCount, spec->constraintCount})
		WriteU32(&writer, value);
	WriteU8(&writer, spec->timestepPresent);
	WriteU32(&writer, spec->timestepHz);
	WriteU32(&writer, spec->warmupWorkUnitCount);
	WriteU32(&writer, spec->measuredWorkUnitCount);
	WriteU32(&writer, spec->visualDebugPrimitiveCount);
	WriteVector3(&writer, spec->gravity);
	WriteU8(&writer, static_cast<std::uint8_t>(spec->sleepMode));
	WriteU8(&writer, static_cast<std::uint8_t>(spec->continuousCollisionMode));
	WriteFloat(&writer, spec->friction);
	WriteFloat(&writer, spec->restitution);
	WriteU32(&writer, spec->nativeSolver.supportedFields);
	for (const std::uint32_t value : spec->nativeSolver.values)
		WriteU32(&writer, value);
	WriteVector3(&writer, spec->replayCamera.direction);
	WriteVector3(&writer, spec->replayCamera.up);
	WriteVector3(&writer, spec->replayCamera.minimum);
	WriteVector3(&writer, spec->replayCamera.maximum);
	WriteVector3(&writer, spec->replayCamera.eye);
	WriteVector3(&writer, spec->replayCamera.target);
	WriteVector3(&writer, spec->replayCamera.eyeOffset);
	WriteVector3(&writer, spec->replayCamera.targetOffset);
	WriteFloat(&writer, spec->replayCamera.verticalFovDegrees);
	WriteFloat(&writer, spec->replayCamera.viewportFill);
	WriteFloat(&writer, spec->replayCamera.nearPlane);
	WriteFloat(&writer, spec->replayCamera.farPlane);
	WriteU32(&writer, spec->replayCamera.stableSlot);
	WriteU32(&writer, spec->replayCamera.mode);
	WriteU8(&writer, static_cast<std::uint8_t>(spec->shapePreset));
	if (spec->shapePreset == CaseShapePreset_ConvexHull)
		for (const CaseExecutionVector3 point : spec->hullPoints)
			WriteVector3(&writer, point);
	if (spec->fixtureKind != CaseFixtureKind_RagdollStairTumble && spec->fixtureKind != CaseFixtureKind_RayTracing)
		WriteGeometry(&writer, spec->selectedGeometry);
	if (spec->fixtureKind == CaseFixtureKind_OpenContainerFallingPile)
	{
		for (std::uint32_t value : spec->openContainer.dynamicGrid)
			WriteU32(&writer, value);
		WriteVector3(&writer, spec->openContainer.dynamicHalfExtents);
		WriteVector3(&writer, spec->openContainer.dynamicSpacing);
		WriteFloat(&writer, spec->openContainer.dynamicInitialY);
		WriteFloat(&writer, spec->openContainer.density);
		WriteU16(&writer, spec->openContainer.staticBoxCount);
		for (std::uint16_t index = 0; index < spec->openContainer.staticBoxCount; ++index)
			WriteBox(&writer, spec->openContainer.staticBoxes[index]);
	}
	else if (spec->fixtureKind == CaseFixtureKind_BoxContactIslands)
	{
		for (std::uint32_t value : spec->contactIslands.islandGrid)
			WriteU32(&writer, value);
		for (float value : spec->contactIslands.islandSpacing)
			WriteFloat(&writer, value);
		for (std::uint32_t value : spec->contactIslands.bodyGrid)
			WriteU32(&writer, value);
		WriteVector3(&writer, spec->contactIslands.bodyHalfExtents);
		WriteVector3(&writer, spec->contactIslands.bodySpacing);
		WriteFloat(&writer, spec->contactIslands.bodyInitialY);
		WriteVector3(&writer, spec->contactIslands.floorHalfExtents);
		WriteFloat(&writer, spec->contactIslands.density);
	}
	else if (spec->fixtureKind == CaseFixtureKind_SpatialQueryTrace)
	{
		for (std::uint32_t value : spec->spatialQuery.staticGrid)
			WriteU32(&writer, value);
		WriteVector3(&writer, spec->spatialQuery.staticHalfExtents);
		WriteVector3(&writer, spec->spatialQuery.staticSpacing);
		WriteVector3(&writer, spec->spatialQuery.staticBaseCenter);
		WriteU32(&writer, spec->spatialQuery.rayCount);
		WriteU32(&writer, spec->spatialQuery.sphereCastCount);
		WriteU32(&writer, spec->spatialQuery.overlapCount);
		WriteFloat(&writer, spec->spatialQuery.queryDistance);
		WriteFloat(&writer, spec->spatialQuery.sphereCastRadius);
		WriteVector3(&writer, spec->spatialQuery.overlapHalfExtents);
		WriteFloat(&writer, spec->spatialQuery.missOffset);
		WriteU32(&writer, spec->spatialQuery.debugSamplesPerFamily);
	}
	else if (spec->fixtureKind == CaseFixtureKind_RagdollStairTumble)
	{
		WriteU8(&writer, static_cast<std::uint8_t>(spec->ragdoll.linkedCollisionMode));
		WriteFloat(&writer, spec->ragdoll.linearDamping);
		WriteFloat(&writer, spec->ragdoll.angularDamping);
		WriteFloat(&writer, spec->ragdoll.partMass);
		for (std::uint32_t value : spec->ragdoll.ragdollGrid)
			WriteU32(&writer, value);
		WriteFloat(&writer, spec->ragdoll.columnSpacing);
		WriteFloat(&writer, spec->ragdoll.rowSpacing);
		WriteFloat(&writer, spec->ragdoll.baseHeightOffset);
		WriteFloat(&writer, spec->ragdoll.pitchDegrees);
		WriteU16(&writer, spec->ragdoll.yawPatternCount);
		for (std::uint16_t index = 0; index < spec->ragdoll.yawPatternCount; ++index)
			WriteFloat(&writer, spec->ragdoll.yawPatternDegrees[index]);
		WriteFloat(&writer, spec->ragdoll.triggerRowSpeed);
		WriteFloat(&writer, spec->ragdoll.followerRowSpeed);
		WriteU32(&writer, spec->ragdoll.stairCount);
		WriteFloat(&writer, spec->ragdoll.stairRise);
		WriteFloat(&writer, spec->ragdoll.stairDepth);
		WriteFloat(&writer, spec->ragdoll.stairHalfWidth);
		WriteFloat(&writer, spec->ragdoll.stairHalfHeight);
		WriteFloat(&writer, spec->ragdoll.stairHalfDepth);
		WriteU16(&writer, spec->ragdoll.extraStaticBoxCount);
		for (std::uint16_t index = 0; index < spec->ragdoll.extraStaticBoxCount; ++index)
			WriteBox(&writer, spec->ragdoll.extraStaticBoxes[index]);
		WriteU16(&writer, spec->ragdoll.partCount);
		for (std::uint16_t index = 0; index < spec->ragdoll.partCount; ++index)
		{
			const CaseExecutionRagdollPart& part = spec->ragdoll.parts[index];
			WriteVector3(&writer, part.center);
			WriteGeometry(&writer, CaseExecutionPartGeometry(part));
		}
		WriteU16(&writer, spec->ragdoll.linkCount);
		for (std::uint16_t index = 0; index < spec->ragdoll.linkCount; ++index)
		{
			const CaseExecutionRagdollLink& link = spec->ragdoll.links[index];
			WriteU16(&writer, link.parentPart);
			WriteU16(&writer, link.childPart);
			WriteVector3(&writer, link.anchor);
			WriteVector3(&writer, link.parentLocalAnchor);
			WriteVector3(&writer, link.childLocalAnchor);
		}
	}
	else if (spec->fixtureKind == CaseFixtureKind_PyramidWall)
	{
		WriteU32(&writer, spec->pyramidWall.rowCount);
		WriteFloat(&writer, spec->pyramidWall.halfExtent);
		WriteFloat(&writer, spec->pyramidWall.density);
		WriteVector3(&writer, spec->pyramidWall.floorHalfExtents);
	}
	else if (spec->fixtureKind == CaseFixtureKind_RayTracing)
	{
		const CaseExecutionRayTracing& ray = spec->rayTracing;
		for (std::uint32_t value : {ray.recipeRevision, ray.width, ray.height, ray.viewCount, ray.primitiveCount,
		                            ray.meshCount, ray.trianglesPerMesh, ray.movingCount, ray.seedLow, ray.seedHigh})
			WriteU32(&writer, value);
	}
	else
	{
		WriteU32(&writer, spec->largePyramid.rowCount);
		WriteVector3(&writer, spec->largePyramid.boxHalfExtents);
		WriteVector3(&writer, spec->largePyramid.boxSpacing);
		WriteVector3(&writer, spec->largePyramid.baseCenter);
		WriteVector3(&writer, spec->largePyramid.floorHalfExtents);
		WriteFloat(&writer, spec->largePyramid.boxDensity);
		WriteU32(&writer, spec->largePyramid.projectileCount);
		WriteFloat(&writer, spec->largePyramid.projectileRadius);
		WriteFloat(&writer, spec->largePyramid.projectileDensity);
		WriteVector3(&writer, spec->largePyramid.projectileInitialCenter);
		WriteVector3(&writer, spec->largePyramid.projectileCenterSpacing);
		WriteVector3(&writer, spec->largePyramid.projectileLaunchVelocity);
		WriteU32(&writer, spec->largePyramid.projectileLaunchAfterWorkUnits);
	}
	if (!writer.valid || writer.offset > kCaseExecutionPayloadCapacity)
		return CaseExecutionDecodeStatus_Capacity;
	const std::uint32_t total = writer.offset;
	bytes[sizeOffset] = static_cast<std::uint8_t>(total);
	bytes[sizeOffset + 1] = static_cast<std::uint8_t>(total >> 8);
	bytes[sizeOffset + 2] = static_cast<std::uint8_t>(total >> 16);
	bytes[sizeOffset + 3] = static_cast<std::uint8_t>(total >> 24);
	*byteCount = total;
	return CaseExecutionDecodeStatus_Ok;
}

inline CaseExecutionDecodeStatus DecodeCaseExecution(const std::uint8_t* bytes, std::uint32_t byteCount,
                                                     CaseExecutionSpec* spec)
{
	using namespace case_execution_wire_detail;
	if (bytes == nullptr || spec == nullptr || byteCount < 10 || byteCount > kCaseExecutionPayloadCapacity)
		return CaseExecutionDecodeStatus_Invalid;
	*spec = {};
	Reader reader = {bytes, byteCount, 0, 1};
	if (ReadU8(&reader) != 'P' || ReadU8(&reader) != 'A' || ReadU8(&reader) != 'C' || ReadU8(&reader) != 'X')
		return CaseExecutionDecodeStatus_Invalid;
	spec->fixtureKind = static_cast<CaseFixtureKind>(ReadU16(&reader));
	if (!ValidKind(spec->fixtureKind) || ReadU32(&reader) != byteCount)
		return CaseExecutionDecodeStatus_Invalid;
	ReadText(&reader, spec->caseId);
	ReadText(&reader, spec->fixtureSemantic);
	spec->fixtureRevision = ReadU32(&reader);
	std::uint32_t* counts[] = {&spec->dynamicBodyCount,  &spec->kinematicBodyCount, &spec->staticBodyCount,
	                           &spec->bodyCount,         &spec->shapeCount,         &spec->visualInstanceCount,
	                           &spec->meshTriangleCount, &spec->queryCount,         &spec->constraintCount};
	for (std::uint32_t* value : counts)
		*value = ReadU32(&reader);
	spec->timestepPresent = ReadU8(&reader);
	spec->timestepHz = ReadU32(&reader);
	spec->warmupWorkUnitCount = ReadU32(&reader);
	spec->measuredWorkUnitCount = ReadU32(&reader);
	spec->visualDebugPrimitiveCount = ReadU32(&reader);
	spec->gravity = ReadVector3(&reader);
	spec->sleepMode = static_cast<CaseExecutionToggle>(ReadU8(&reader));
	spec->continuousCollisionMode = static_cast<CaseExecutionToggle>(ReadU8(&reader));
	spec->friction = ReadFloat(&reader);
	spec->restitution = ReadFloat(&reader);
	spec->nativeSolver.supportedFields = ReadU32(&reader);
	for (std::uint32_t& value : spec->nativeSolver.values)
		value = ReadU32(&reader);
	spec->replayCamera.direction = ReadVector3(&reader);
	spec->replayCamera.up = ReadVector3(&reader);
	spec->replayCamera.minimum = ReadVector3(&reader);
	spec->replayCamera.maximum = ReadVector3(&reader);
	spec->replayCamera.eye = ReadVector3(&reader);
	spec->replayCamera.target = ReadVector3(&reader);
	spec->replayCamera.eyeOffset = ReadVector3(&reader);
	spec->replayCamera.targetOffset = ReadVector3(&reader);
	spec->replayCamera.verticalFovDegrees = ReadFloat(&reader);
	spec->replayCamera.viewportFill = ReadFloat(&reader);
	spec->replayCamera.nearPlane = ReadFloat(&reader);
	spec->replayCamera.farPlane = ReadFloat(&reader);
	spec->replayCamera.stableSlot = ReadU32(&reader);
	spec->replayCamera.mode = ReadU32(&reader);
	spec->shapePreset = static_cast<CaseShapePreset>(ReadU8(&reader));
	if (spec->shapePreset == CaseShapePreset_ConvexHull)
		for (CaseExecutionVector3& point : spec->hullPoints)
			point = ReadVector3(&reader);
	if (spec->fixtureKind != CaseFixtureKind_RagdollStairTumble && spec->fixtureKind != CaseFixtureKind_RayTracing)
		spec->selectedGeometry = ReadGeometry(&reader);
	if (spec->fixtureKind == CaseFixtureKind_OpenContainerFallingPile)
	{
		for (std::uint32_t& value : spec->openContainer.dynamicGrid)
			value = ReadU32(&reader);
		spec->openContainer.dynamicHalfExtents = ReadVector3(&reader);
		spec->openContainer.dynamicSpacing = ReadVector3(&reader);
		spec->openContainer.dynamicInitialY = ReadFloat(&reader);
		spec->openContainer.density = ReadFloat(&reader);
		spec->openContainer.staticBoxCount = ReadU16(&reader);
		if (spec->openContainer.staticBoxCount > kCaseExecutionStaticBoxCapacity)
			reader.valid = 0;
		for (std::uint16_t index = 0; reader.valid && index < spec->openContainer.staticBoxCount; ++index)
			spec->openContainer.staticBoxes[index] = ReadBox(&reader);
	}
	else if (spec->fixtureKind == CaseFixtureKind_BoxContactIslands)
	{
		for (std::uint32_t& value : spec->contactIslands.islandGrid)
			value = ReadU32(&reader);
		for (float& value : spec->contactIslands.islandSpacing)
			value = ReadFloat(&reader);
		for (std::uint32_t& value : spec->contactIslands.bodyGrid)
			value = ReadU32(&reader);
		spec->contactIslands.bodyHalfExtents = ReadVector3(&reader);
		spec->contactIslands.bodySpacing = ReadVector3(&reader);
		spec->contactIslands.bodyInitialY = ReadFloat(&reader);
		spec->contactIslands.floorHalfExtents = ReadVector3(&reader);
		spec->contactIslands.density = ReadFloat(&reader);
	}
	else if (spec->fixtureKind == CaseFixtureKind_SpatialQueryTrace)
	{
		for (std::uint32_t& value : spec->spatialQuery.staticGrid)
			value = ReadU32(&reader);
		spec->spatialQuery.staticHalfExtents = ReadVector3(&reader);
		spec->spatialQuery.staticSpacing = ReadVector3(&reader);
		spec->spatialQuery.staticBaseCenter = ReadVector3(&reader);
		spec->spatialQuery.rayCount = ReadU32(&reader);
		spec->spatialQuery.sphereCastCount = ReadU32(&reader);
		spec->spatialQuery.overlapCount = ReadU32(&reader);
		spec->spatialQuery.queryDistance = ReadFloat(&reader);
		spec->spatialQuery.sphereCastRadius = ReadFloat(&reader);
		spec->spatialQuery.overlapHalfExtents = ReadVector3(&reader);
		spec->spatialQuery.missOffset = ReadFloat(&reader);
		spec->spatialQuery.debugSamplesPerFamily = ReadU32(&reader);
	}
	else if (spec->fixtureKind == CaseFixtureKind_RagdollStairTumble)
	{
		spec->ragdoll.linkedCollisionMode = static_cast<CaseExecutionToggle>(ReadU8(&reader));
		spec->ragdoll.linearDamping = ReadFloat(&reader);
		spec->ragdoll.angularDamping = ReadFloat(&reader);
		spec->ragdoll.partMass = ReadFloat(&reader);
		for (std::uint32_t& value : spec->ragdoll.ragdollGrid)
			value = ReadU32(&reader);
		spec->ragdoll.columnSpacing = ReadFloat(&reader);
		spec->ragdoll.rowSpacing = ReadFloat(&reader);
		spec->ragdoll.baseHeightOffset = ReadFloat(&reader);
		spec->ragdoll.pitchDegrees = ReadFloat(&reader);
		spec->ragdoll.yawPatternCount = ReadU16(&reader);
		if (spec->ragdoll.yawPatternCount == 0 || spec->ragdoll.yawPatternCount > kCaseExecutionYawCapacity)
			reader.valid = 0;
		for (std::uint16_t index = 0; reader.valid && index < spec->ragdoll.yawPatternCount; ++index)
			spec->ragdoll.yawPatternDegrees[index] = ReadFloat(&reader);
		spec->ragdoll.triggerRowSpeed = ReadFloat(&reader);
		spec->ragdoll.followerRowSpeed = ReadFloat(&reader);
		spec->ragdoll.stairCount = ReadU32(&reader);
		spec->ragdoll.stairRise = ReadFloat(&reader);
		spec->ragdoll.stairDepth = ReadFloat(&reader);
		spec->ragdoll.stairHalfWidth = ReadFloat(&reader);
		spec->ragdoll.stairHalfHeight = ReadFloat(&reader);
		spec->ragdoll.stairHalfDepth = ReadFloat(&reader);
		spec->ragdoll.extraStaticBoxCount = ReadU16(&reader);
		if (spec->ragdoll.extraStaticBoxCount > kCaseExecutionStaticBoxCapacity)
			reader.valid = 0;
		for (std::uint16_t index = 0; reader.valid && index < spec->ragdoll.extraStaticBoxCount; ++index)
			spec->ragdoll.extraStaticBoxes[index] = ReadBox(&reader);
		spec->ragdoll.partCount = ReadU16(&reader);
		if (spec->ragdoll.partCount == 0 || spec->ragdoll.partCount > kCaseExecutionRagdollPartCapacity)
			reader.valid = 0;
		for (std::uint16_t index = 0; reader.valid && index < spec->ragdoll.partCount; ++index)
		{
			CaseExecutionRagdollPart& part = spec->ragdoll.parts[index];
			part.center = ReadVector3(&reader);
			const CaseExecutionGeometry geometry = ReadGeometry(&reader);
			part.shape = geometry.shape;
			part.halfExtents = geometry.halfExtents;
			part.radius = geometry.radius;
			part.halfSegment = geometry.halfSegment;
			part.axis = geometry.axis;
		}
		spec->ragdoll.linkCount = ReadU16(&reader);
		if (spec->ragdoll.linkCount > kCaseExecutionRagdollLinkCapacity)
			reader.valid = 0;
		for (std::uint16_t index = 0; reader.valid && index < spec->ragdoll.linkCount; ++index)
		{
			CaseExecutionRagdollLink& link = spec->ragdoll.links[index];
			link.parentPart = ReadU16(&reader);
			link.childPart = ReadU16(&reader);
			link.anchor = ReadVector3(&reader);
			link.parentLocalAnchor = ReadVector3(&reader);
			link.childLocalAnchor = ReadVector3(&reader);
			if (link.parentPart >= spec->ragdoll.partCount || link.childPart >= spec->ragdoll.partCount)
				reader.valid = 0;
		}
	}
	else if (spec->fixtureKind == CaseFixtureKind_PyramidWall)
	{
		spec->pyramidWall.rowCount = ReadU32(&reader);
		spec->pyramidWall.halfExtent = ReadFloat(&reader);
		spec->pyramidWall.density = ReadFloat(&reader);
		spec->pyramidWall.floorHalfExtents = ReadVector3(&reader);
	}
	else if (spec->fixtureKind == CaseFixtureKind_RayTracing)
	{
		CaseExecutionRayTracing& ray = spec->rayTracing;
		for (std::uint32_t* value : {&ray.recipeRevision, &ray.width, &ray.height, &ray.viewCount, &ray.primitiveCount,
		                             &ray.meshCount, &ray.trianglesPerMesh, &ray.movingCount, &ray.seedLow, &ray.seedHigh})
			*value = ReadU32(&reader);
	}
	else
	{
		spec->largePyramid.rowCount = ReadU32(&reader);
		spec->largePyramid.boxHalfExtents = ReadVector3(&reader);
		spec->largePyramid.boxSpacing = ReadVector3(&reader);
		spec->largePyramid.baseCenter = ReadVector3(&reader);
		spec->largePyramid.floorHalfExtents = ReadVector3(&reader);
		spec->largePyramid.boxDensity = ReadFloat(&reader);
		spec->largePyramid.projectileCount = ReadU32(&reader);
		spec->largePyramid.projectileRadius = ReadFloat(&reader);
		spec->largePyramid.projectileDensity = ReadFloat(&reader);
		spec->largePyramid.projectileInitialCenter = ReadVector3(&reader);
		spec->largePyramid.projectileCenterSpacing = ReadVector3(&reader);
		spec->largePyramid.projectileLaunchVelocity = ReadVector3(&reader);
		spec->largePyramid.projectileLaunchAfterWorkUnits = ReadU32(&reader);
	}
	if (!reader.valid || reader.offset != byteCount || !ValidSpec(*spec))
		return CaseExecutionDecodeStatus_Invalid;
	return CaseExecutionDecodeStatus_Ok;
}

inline CaseExecutionDecodeStatus DecodeCaseExecutionHex(const char* hex, std::uint32_t hexSize, std::uint8_t* bytes,
                                                        std::uint32_t capacity, std::uint32_t* byteCount,
                                                        CaseExecutionSpec* spec)
{
	if (hex == nullptr || bytes == nullptr || byteCount == nullptr || spec == nullptr || hexSize == 0 ||
	    (hexSize & 1U) != 0 || hexSize > kCaseExecutionHexCapacity || capacity < hexSize / 2)
		return CaseExecutionDecodeStatus_Invalid;
	const std::uint32_t size = hexSize / 2;
	for (std::uint32_t index = 0; index < size; ++index)
	{
		auto nibble = [](char value) -> int
		{
			if (value >= '0' && value <= '9')
				return value - '0';
			if (value >= 'a' && value <= 'f')
				return value - 'a' + 10;
			return -1;
		};
		const int high = nibble(hex[index * 2]);
		const int low = nibble(hex[index * 2 + 1]);
		if (high < 0 || low < 0)
			return CaseExecutionDecodeStatus_Invalid;
		bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
	}
	const CaseExecutionDecodeStatus status = DecodeCaseExecution(bytes, size, spec);
	if (status != CaseExecutionDecodeStatus_Ok)
		return status;
	*byteCount = size;
	return CaseExecutionDecodeStatus_Ok;
}
