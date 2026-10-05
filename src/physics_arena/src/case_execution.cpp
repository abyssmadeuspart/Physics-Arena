#include "physics_arena/case_execution.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <string_view>

namespace physics_arena
{
namespace
{
ArenaStatus SetCaseExecutionError(StatusRecord* error, std::string_view detail)
{
	if (error == nullptr)
		return ArenaStatus_InvalidResult;
	*error = {};
	auto copy = [](std::span<char> destination, std::uint32_t* size, std::string_view text)
	{
		if (text.size() > destination.size())
			return 0;
		std::copy(text.begin(), text.end(), destination.begin());
		*size = static_cast<std::uint32_t>(text.size());
		return 1;
	};
	copy(error->component, &error->componentSize, "case_execution");
	copy(error->status, &error->statusSize, "invalid_result");
	copy(error->detail, &error->detailSize, detail);
	error->code = ArenaStatus_InvalidResult;
	return ArenaStatus_InvalidResult;
}
} // namespace

CaseExecutionGeometry ResolveBoxGeometry(CaseExecutionVector3 halfExtents, CaseShapePreset preset)
{
	CaseExecutionGeometry geometry = {CaseExecutionShape_Box, halfExtents, 0.0f, 0.0f, CaseExecutionAxis_Y};
	if (preset == CaseShapePreset_Sphere)
	{
		geometry.shape = CaseExecutionShape_Sphere;
		geometry.radius = std::min({halfExtents.x, halfExtents.y, halfExtents.z});
	}
	else if (preset == CaseShapePreset_Capsule)
	{
		geometry.shape = CaseExecutionShape_Capsule;
		float longitudinal = halfExtents.y;
		float transverse = std::min(halfExtents.x, halfExtents.z);
		if (halfExtents.x > longitudinal)
		{
			geometry.axis = CaseExecutionAxis_X;
			longitudinal = halfExtents.x;
			transverse = std::min(halfExtents.y, halfExtents.z);
		}
		if (halfExtents.z > longitudinal)
		{
			geometry.axis = CaseExecutionAxis_Z;
			longitudinal = halfExtents.z;
			transverse = std::min(halfExtents.x, halfExtents.y);
		}
		geometry.radius = 0.5f * transverse;
		geometry.halfSegment = longitudinal - geometry.radius;
	}
	else if (preset == CaseShapePreset_ConvexHull)
		geometry.shape = CaseExecutionShape_ConvexHull;
	return geometry;
}

ArenaStatus ResolveCaseExecutionPreset(CaseExecutionSpec* spec, CaseShapePreset preset, StatusRecord* error)
{
	const char* semantic = case_execution_wire_detail::ExpectedSemantic(spec->fixtureKind, preset);
	if (semantic == nullptr || spec->shapePreset != CaseShapePreset_Authored)
		return SetCaseExecutionError(error, "case_shape_preset");
	spec->shapePreset = preset;
	std::memcpy(spec->fixtureSemantic, semantic, std::strlen(semantic) + 1);
	if (preset != CaseShapePreset_Authored && spec->fixtureKind != CaseFixtureKind_BoxContactIslands)
		spec->fixtureRevision = 1;
	if (preset == CaseShapePreset_ConvexHull)
	{
		std::uint32_t pointIndex = 0;
		for (std::uint32_t halfAxis = 0; halfAxis < 3; ++halfAxis)
		{
			for (std::uint32_t signs = 0; signs < 8; ++signs)
			{
				const float x = (signs & 1u) != 0 ? 1.0f : -1.0f;
				const float y = (signs & 2u) != 0 ? 1.0f : -1.0f;
				const float z = (signs & 4u) != 0 ? 1.0f : -1.0f;
				spec->hullPoints[pointIndex++] = {x * (halfAxis == 0 ? 0.5f : 1.0f), y * (halfAxis == 1 ? 0.5f : 1.0f),
				                                  z * (halfAxis == 2 ? 0.5f : 1.0f)};
			}
		}
	}
	if (spec->fixtureKind == CaseFixtureKind_RagdollStairTumble)
	{
		CaseExecutionRagdoll& ragdoll = spec->ragdoll;
		for (std::uint16_t index = 0; index < ragdoll.partCount; ++index)
		{
			CaseExecutionRagdollPart& part = ragdoll.parts[index];
			if (part.shape != CaseExecutionShape_Box)
				continue;
			const CaseExecutionGeometry geometry = ResolveBoxGeometry(part.halfExtents, preset);
			part.shape = geometry.shape;
			part.radius = geometry.radius;
			part.halfSegment = geometry.halfSegment;
			part.axis = geometry.axis;
		}
		for (std::uint16_t index = 0; index < ragdoll.linkCount; ++index)
		{
			CaseExecutionRagdollLink& link = ragdoll.links[index];
			const CaseExecutionRagdollPart& parent = ragdoll.parts[link.parentPart];
			const CaseExecutionRagdollPart& child = ragdoll.parts[link.childPart];
			link.parentLocalAnchor = CaseExecutionAxisLocalVector(
			    parent.axis,
			    {link.anchor.x - parent.center.x, link.anchor.y - parent.center.y, link.anchor.z - parent.center.z});
			link.childLocalAnchor = CaseExecutionAxisLocalVector(
			    child.axis,
			    {link.anchor.x - child.center.x, link.anchor.y - child.center.y, link.anchor.z - child.center.z});
		}
	}
	else if (spec->fixtureKind == CaseFixtureKind_PyramidWall)
	{
		const float h = spec->pyramidWall.halfExtent;
		spec->selectedGeometry = ResolveBoxGeometry({h, h, h}, preset);
	}
	else if (spec->fixtureKind != CaseFixtureKind_RayTracing)
	{
		const CaseExecutionVector3 halfExtents =
		    spec->fixtureKind == CaseFixtureKind_OpenContainerFallingPile ? spec->openContainer.dynamicHalfExtents
			: spec->fixtureKind == CaseFixtureKind_BoxContactIslands      ? spec->contactIslands.bodyHalfExtents
			: spec->fixtureKind == CaseFixtureKind_SpatialQueryTrace      ? spec->spatialQuery.staticHalfExtents
			                                                              : spec->largePyramid.boxHalfExtents;
		spec->selectedGeometry = ResolveBoxGeometry(halfExtents, preset);
	}
	if (!case_execution_wire_detail::ValidSpec(*spec))
		return SetCaseExecutionError(error, "case_shape_geometry");
	return ArenaStatus_Ok;
}

ArenaStatus StoreCatalogCaseExecution(Catalog* catalog, CaseRecord* record, const CaseExecutionSpec* spec,
                                      StatusRecord* error)
{
	if (catalog == nullptr || record == nullptr || spec == nullptr ||
	    catalog->caseExecutionArenaUsed > catalog->caseExecutionArena.size())
		return SetCaseExecutionError(error, "case_execution_input");
	std::uint32_t size = 0;
	const std::uint32_t remaining =
	    static_cast<std::uint32_t>(catalog->caseExecutionArena.size() - catalog->caseExecutionArenaUsed);
	if (remaining < kCaseExecutionPayloadCapacity ||
	    EncodeCaseExecution(spec, catalog->caseExecutionArena.data() + catalog->caseExecutionArenaUsed,
	                        kCaseExecutionPayloadCapacity, &size) != CaseExecutionDecodeStatus_Ok ||
	    size == 0)
		return SetCaseExecutionError(error, "case_execution_capacity");
	record->caseExecutionOffset = catalog->caseExecutionArenaUsed;
	record->caseExecutionSize = size;
	record->fixtureKind = spec->fixtureKind;
	record->shapePreset = spec->shapePreset;
	catalog->caseExecutionArenaUsed += size;
	return ArenaStatus_Ok;
}

ArenaStatus DecodeCatalogCaseExecution(const Catalog* catalog, std::uint32_t caseIndex, CaseExecutionSpec* spec,
                                       StatusRecord* error)
{
	if (catalog == nullptr || spec == nullptr || caseIndex >= catalog->caseCount)
		return SetCaseExecutionError(error, "case_execution_index");
	const CaseRecord& record = catalog->cases[caseIndex];
	if (record.caseExecutionSize == 0 || record.caseExecutionSize > kCaseExecutionPayloadCapacity ||
	    record.caseExecutionOffset > catalog->caseExecutionArenaUsed ||
	    record.caseExecutionSize > catalog->caseExecutionArenaUsed - record.caseExecutionOffset ||
	    DecodeCaseExecution(catalog->caseExecutionArena.data() + record.caseExecutionOffset, record.caseExecutionSize,
	                        spec) != CaseExecutionDecodeStatus_Ok ||
	    spec->fixtureKind != record.fixtureKind)
		return SetCaseExecutionError(error, "case_execution_decode");
	return ArenaStatus_Ok;
}

ArenaStatus EncodeCatalogCaseExecutionHex(const Catalog* catalog, std::uint32_t caseIndex, char* hex,
                                          std::uint32_t capacity, std::uint32_t* hexSize, StatusRecord* error)
{
	if (catalog == nullptr || hex == nullptr || hexSize == nullptr || caseIndex >= catalog->caseCount)
		return SetCaseExecutionError(error, "case_execution_hex_input");
	const CaseRecord& record = catalog->cases[caseIndex];
	if (record.caseExecutionSize == 0 || record.caseExecutionSize > kCaseExecutionPayloadCapacity ||
	    record.caseExecutionOffset > catalog->caseExecutionArenaUsed ||
	    record.caseExecutionSize > catalog->caseExecutionArenaUsed - record.caseExecutionOffset ||
	    capacity <= record.caseExecutionSize * 2)
		return SetCaseExecutionError(error, "case_execution_hex_capacity");
	constexpr char digits[] = "0123456789abcdef";
	for (std::uint32_t index = 0; index < record.caseExecutionSize; ++index)
	{
		const std::uint8_t value = catalog->caseExecutionArena[record.caseExecutionOffset + index];
		hex[index * 2] = digits[value >> 4];
		hex[index * 2 + 1] = digits[value & 0x0f];
	}
	*hexSize = record.caseExecutionSize * 2;
	hex[*hexSize] = '\0';
	return ArenaStatus_Ok;
}
} // namespace physics_arena
