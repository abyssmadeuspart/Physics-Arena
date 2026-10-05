#include "physics_arena/replay_comparison.h"
#include <algorithm>
#include <cstring>

namespace physics_arena
{
int SameComparisonVector(CaseExecutionVector3 left, CaseExecutionVector3 right)
{
	return left.x == right.x && left.y == right.y && left.z == right.z ? 1 : 0;
}

ArenaStatus CheckReplayComparisonConfiguration(const ResultManifestRecord& primary,
                                               const ResultManifestRecord& peer, StatusRecord* error)
{
	if (ResultConfiguration(&primary) == nullptr || ResultConfiguration(&peer) == nullptr)
		return ReplayError(error, "comparison_requires_frozen_configuration");
	const CaseExecutionSpec& a = primary.configuration.execution;
	const CaseExecutionSpec& b = peer.configuration.execution;
	if (std::strcmp(a.caseId, b.caseId) != 0)
		return ReplayError(error, "comparison_caseId_differs");
	if (std::strcmp(a.fixtureSemantic, b.fixtureSemantic) != 0)
		return ReplayError(error, "comparison_fixtureSemantic_differs");
	if (a.fixtureKind != b.fixtureKind)
		return ReplayError(error, "comparison_fixtureKind_differs");
	if (a.fixtureRevision != b.fixtureRevision)
		return ReplayError(error, "comparison_fixtureRevision_differs");
	if (a.shapePreset != b.shapePreset)
		return ReplayError(error, "comparison_shapePreset_differs");
	if (a.timestepPresent != b.timestepPresent)
		return ReplayError(error, "comparison_timestepPresent_differs");
	if (a.timestepHz != b.timestepHz)
		return ReplayError(error, "comparison_timestepHz_differs");
	if (a.warmupWorkUnitCount != b.warmupWorkUnitCount)
		return ReplayError(error, "comparison_warmupWorkUnitCount_differs");
	if (SameComparisonVector(a.gravity, b.gravity) == 0)
		return ReplayError(error, "comparison_gravity_differs");
	if (a.fixtureKind != CaseFixtureKind_RagdollStairTumble)
	{
		if (CaseExecutionSameGeometry(a.selectedGeometry, b.selectedGeometry) == 0)
			return ReplayError(error, "comparison_selected_geometry_differs");
	}
	if (a.shapePreset == CaseShapePreset_ConvexHull)
		for (std::uint32_t index = 0; index < kCaseExecutionHullPointCount; ++index)
			if (SameComparisonVector(a.hullPoints[index], b.hullPoints[index]) == 0)
				return ReplayError(error, "comparison_hull_point_differs");
	if (a.fixtureKind == CaseFixtureKind_OpenContainerFallingPile)
	{
		if (a.openContainer.dynamicInitialY != b.openContainer.dynamicInitialY)
			return ReplayError(error, "comparison_openContainer_dynamicInitialY_differs");
		if (a.openContainer.density != b.openContainer.density)
			return ReplayError(error, "comparison_openContainer_density_differs");
		if (a.openContainer.staticBoxCount != b.openContainer.staticBoxCount)
			return ReplayError(error, "comparison_openContainer_staticBoxCount_differs");
		if (SameComparisonVector(a.openContainer.dynamicHalfExtents, b.openContainer.dynamicHalfExtents) == 0)
			return ReplayError(error, "comparison_openContainer_dynamicHalfExtents_differs");
		if (SameComparisonVector(a.openContainer.dynamicSpacing, b.openContainer.dynamicSpacing) == 0)
			return ReplayError(error, "comparison_openContainer_dynamicSpacing_differs");
		for (std::uint32_t index = 0; index < 3; ++index)
			if (a.openContainer.dynamicGrid[index] != b.openContainer.dynamicGrid[index])
				return ReplayError(error, "comparison_openContainer_dynamicGrid_differs");
		for (std::uint32_t index = 0; index < a.openContainer.staticBoxCount; ++index)
			if (SameComparisonVector(a.openContainer.staticBoxes[index].center, b.openContainer.staticBoxes[index].center) == 0 ||
			    SameComparisonVector(a.openContainer.staticBoxes[index].halfExtents, b.openContainer.staticBoxes[index].halfExtents) == 0)
				return ReplayError(error, "comparison_openContainer_static_box_differs");
	}
	if (a.fixtureKind == CaseFixtureKind_BoxContactIslands)
	{
		if (a.contactIslands.bodyInitialY != b.contactIslands.bodyInitialY)
			return ReplayError(error, "comparison_contactIslands_bodyInitialY_differs");
		if (a.contactIslands.density != b.contactIslands.density)
			return ReplayError(error, "comparison_contactIslands_density_differs");
		if (SameComparisonVector(a.contactIslands.bodyHalfExtents, b.contactIslands.bodyHalfExtents) == 0)
			return ReplayError(error, "comparison_contactIslands_bodyHalfExtents_differs");
		if (SameComparisonVector(a.contactIslands.bodySpacing, b.contactIslands.bodySpacing) == 0)
			return ReplayError(error, "comparison_contactIslands_bodySpacing_differs");
		if (SameComparisonVector(a.contactIslands.floorHalfExtents, b.contactIslands.floorHalfExtents) == 0)
			return ReplayError(error, "comparison_contactIslands_floorHalfExtents_differs");
		for (std::uint32_t index = 0; index < 2; ++index)
			if (a.contactIslands.islandGrid[index] != b.contactIslands.islandGrid[index])
				return ReplayError(error, "comparison_contactIslands_islandGrid_differs");
		for (std::uint32_t index = 0; index < 2; ++index)
			if (a.contactIslands.islandSpacing[index] != b.contactIslands.islandSpacing[index])
				return ReplayError(error, "comparison_contactIslands_islandSpacing_differs");
		for (std::uint32_t index = 0; index < 3; ++index)
			if (a.contactIslands.bodyGrid[index] != b.contactIslands.bodyGrid[index])
				return ReplayError(error, "comparison_contactIslands_bodyGrid_differs");
	}
	if (a.fixtureKind == CaseFixtureKind_SpatialQueryTrace)
	{
		if (a.spatialQuery.rayCount != b.spatialQuery.rayCount)
			return ReplayError(error, "comparison_spatialQuery_rayCount_differs");
		if (a.spatialQuery.sphereCastCount != b.spatialQuery.sphereCastCount)
			return ReplayError(error, "comparison_spatialQuery_sphereCastCount_differs");
		if (a.spatialQuery.overlapCount != b.spatialQuery.overlapCount)
			return ReplayError(error, "comparison_spatialQuery_overlapCount_differs");
		if (a.spatialQuery.queryDistance != b.spatialQuery.queryDistance)
			return ReplayError(error, "comparison_spatialQuery_queryDistance_differs");
		if (a.spatialQuery.sphereCastRadius != b.spatialQuery.sphereCastRadius)
			return ReplayError(error, "comparison_spatialQuery_sphereCastRadius_differs");
		if (a.spatialQuery.missOffset != b.spatialQuery.missOffset)
			return ReplayError(error, "comparison_spatialQuery_missOffset_differs");
		if (a.spatialQuery.debugSamplesPerFamily != b.spatialQuery.debugSamplesPerFamily)
			return ReplayError(error, "comparison_spatialQuery_debugSamplesPerFamily_differs");
		if (SameComparisonVector(a.spatialQuery.staticHalfExtents, b.spatialQuery.staticHalfExtents) == 0)
			return ReplayError(error, "comparison_spatialQuery_staticHalfExtents_differs");
		if (SameComparisonVector(a.spatialQuery.staticSpacing, b.spatialQuery.staticSpacing) == 0)
			return ReplayError(error, "comparison_spatialQuery_staticSpacing_differs");
		if (SameComparisonVector(a.spatialQuery.staticBaseCenter, b.spatialQuery.staticBaseCenter) == 0)
			return ReplayError(error, "comparison_spatialQuery_staticBaseCenter_differs");
		if (SameComparisonVector(a.spatialQuery.overlapHalfExtents, b.spatialQuery.overlapHalfExtents) == 0)
			return ReplayError(error, "comparison_spatialQuery_overlapHalfExtents_differs");
		for (std::uint32_t index = 0; index < 3; ++index)
			if (a.spatialQuery.staticGrid[index] != b.spatialQuery.staticGrid[index])
				return ReplayError(error, "comparison_spatialQuery_staticGrid_differs");
	}
	if (a.fixtureKind == CaseFixtureKind_RagdollStairTumble)
	{
		if (a.ragdoll.columnSpacing != b.ragdoll.columnSpacing)
			return ReplayError(error, "comparison_ragdoll_columnSpacing_differs");
		if (a.ragdoll.rowSpacing != b.ragdoll.rowSpacing)
			return ReplayError(error, "comparison_ragdoll_rowSpacing_differs");
		if (a.ragdoll.baseHeightOffset != b.ragdoll.baseHeightOffset)
			return ReplayError(error, "comparison_ragdoll_baseHeightOffset_differs");
		if (a.ragdoll.pitchDegrees != b.ragdoll.pitchDegrees)
			return ReplayError(error, "comparison_ragdoll_pitchDegrees_differs");
		if (a.ragdoll.yawPatternCount != b.ragdoll.yawPatternCount)
			return ReplayError(error, "comparison_ragdoll_yawPatternCount_differs");
		if (a.ragdoll.triggerRowSpeed != b.ragdoll.triggerRowSpeed)
			return ReplayError(error, "comparison_ragdoll_triggerRowSpeed_differs");
		if (a.ragdoll.followerRowSpeed != b.ragdoll.followerRowSpeed)
			return ReplayError(error, "comparison_ragdoll_followerRowSpeed_differs");
		if (a.ragdoll.stairCount != b.ragdoll.stairCount)
			return ReplayError(error, "comparison_ragdoll_stairCount_differs");
		if (a.ragdoll.stairRise != b.ragdoll.stairRise)
			return ReplayError(error, "comparison_ragdoll_stairRise_differs");
		if (a.ragdoll.stairDepth != b.ragdoll.stairDepth)
			return ReplayError(error, "comparison_ragdoll_stairDepth_differs");
		if (a.ragdoll.stairHalfWidth != b.ragdoll.stairHalfWidth)
			return ReplayError(error, "comparison_ragdoll_stairHalfWidth_differs");
		if (a.ragdoll.stairHalfHeight != b.ragdoll.stairHalfHeight)
			return ReplayError(error, "comparison_ragdoll_stairHalfHeight_differs");
		if (a.ragdoll.stairHalfDepth != b.ragdoll.stairHalfDepth)
			return ReplayError(error, "comparison_ragdoll_stairHalfDepth_differs");
		if (a.ragdoll.extraStaticBoxCount != b.ragdoll.extraStaticBoxCount)
			return ReplayError(error, "comparison_ragdoll_extraStaticBoxCount_differs");
		if (a.ragdoll.partCount != b.ragdoll.partCount)
			return ReplayError(error, "comparison_ragdoll_partCount_differs");
		if (a.ragdoll.linkCount != b.ragdoll.linkCount)
			return ReplayError(error, "comparison_ragdoll_linkCount_differs");
		if (a.ragdoll.partMass != b.ragdoll.partMass)
			return ReplayError(error, "comparison_ragdoll_partMass_differs");
		if (a.ragdoll.linkedCollisionMode != b.ragdoll.linkedCollisionMode)
			return ReplayError(error, "comparison_ragdoll_linkedCollisionMode_differs");
		for (std::uint32_t index = 0; index < 2; ++index)
			if (a.ragdoll.ragdollGrid[index] != b.ragdoll.ragdollGrid[index])
				return ReplayError(error, "comparison_ragdoll_ragdollGrid_differs");
		for (std::uint32_t index = 0; index < a.ragdoll.extraStaticBoxCount; ++index)
			if (SameComparisonVector(a.ragdoll.extraStaticBoxes[index].center, b.ragdoll.extraStaticBoxes[index].center) == 0 ||
			    SameComparisonVector(a.ragdoll.extraStaticBoxes[index].halfExtents, b.ragdoll.extraStaticBoxes[index].halfExtents) == 0)
				return ReplayError(error, "comparison_ragdoll_static_box_differs");
		for (std::uint32_t index = 0; index < a.ragdoll.yawPatternCount; ++index)
			if (a.ragdoll.yawPatternDegrees[index] != b.ragdoll.yawPatternDegrees[index])
				return ReplayError(error, "comparison_ragdoll_yaw_differs");
		for (std::uint32_t index = 0; index < a.ragdoll.partCount; ++index)
			if (SameComparisonVector(a.ragdoll.parts[index].center, b.ragdoll.parts[index].center) == 0 ||
			    CaseExecutionSameGeometry(CaseExecutionPartGeometry(a.ragdoll.parts[index]),
			                              CaseExecutionPartGeometry(b.ragdoll.parts[index])) == 0)
				return ReplayError(error, "comparison_ragdoll_part_differs");
		for (std::uint32_t index = 0; index < a.ragdoll.linkCount; ++index)
		{
			const CaseExecutionRagdollLink& x = a.ragdoll.links[index];
			const CaseExecutionRagdollLink& y = b.ragdoll.links[index];
			if (x.parentPart != y.parentPart || x.childPart != y.childPart ||
			    SameComparisonVector(x.anchor, y.anchor) == 0 ||
			    SameComparisonVector(x.parentLocalAnchor, y.parentLocalAnchor) == 0 ||
			    SameComparisonVector(x.childLocalAnchor, y.childLocalAnchor) == 0)
				return ReplayError(error, "comparison_ragdoll_link_differs");
		}
	}
	if (a.fixtureKind == CaseFixtureKind_LargePyramid)
	{
		if (a.largePyramid.rowCount != b.largePyramid.rowCount)
			return ReplayError(error, "comparison_largePyramid_rowCount_differs");
		if (a.largePyramid.boxDensity != b.largePyramid.boxDensity)
			return ReplayError(error, "comparison_largePyramid_boxDensity_differs");
		if (a.largePyramid.projectileCount != b.largePyramid.projectileCount)
			return ReplayError(error, "comparison_largePyramid_projectileCount_differs");
		if (a.largePyramid.projectileRadius != b.largePyramid.projectileRadius)
			return ReplayError(error, "comparison_largePyramid_projectileRadius_differs");
		if (a.largePyramid.projectileDensity != b.largePyramid.projectileDensity)
			return ReplayError(error, "comparison_largePyramid_projectileDensity_differs");
		if (a.largePyramid.projectileLaunchAfterWorkUnits != b.largePyramid.projectileLaunchAfterWorkUnits)
			return ReplayError(error, "comparison_largePyramid_projectileLaunchAfterWorkUnits_differs");
		if (SameComparisonVector(a.largePyramid.boxHalfExtents, b.largePyramid.boxHalfExtents) == 0)
			return ReplayError(error, "comparison_largePyramid_boxHalfExtents_differs");
		if (SameComparisonVector(a.largePyramid.boxSpacing, b.largePyramid.boxSpacing) == 0)
			return ReplayError(error, "comparison_largePyramid_boxSpacing_differs");
		if (SameComparisonVector(a.largePyramid.baseCenter, b.largePyramid.baseCenter) == 0)
			return ReplayError(error, "comparison_largePyramid_baseCenter_differs");
		if (SameComparisonVector(a.largePyramid.floorHalfExtents, b.largePyramid.floorHalfExtents) == 0)
			return ReplayError(error, "comparison_largePyramid_floorHalfExtents_differs");
		if (SameComparisonVector(a.largePyramid.projectileInitialCenter, b.largePyramid.projectileInitialCenter) == 0)
			return ReplayError(error, "comparison_largePyramid_projectileInitialCenter_differs");
		if (SameComparisonVector(a.largePyramid.projectileCenterSpacing, b.largePyramid.projectileCenterSpacing) == 0)
			return ReplayError(error, "comparison_largePyramid_projectileCenterSpacing_differs");
		if (SameComparisonVector(a.largePyramid.projectileLaunchVelocity, b.largePyramid.projectileLaunchVelocity) == 0)
			return ReplayError(error, "comparison_largePyramid_projectileLaunchVelocity_differs");
	}
	if (a.fixtureKind == CaseFixtureKind_PyramidWall)
	{
		if (a.pyramidWall.rowCount != b.pyramidWall.rowCount)
			return ReplayError(error, "comparison_pyramidWall_rowCount_differs");
		if (a.pyramidWall.halfExtent != b.pyramidWall.halfExtent)
			return ReplayError(error, "comparison_pyramidWall_halfExtent_differs");
		if (a.pyramidWall.density != b.pyramidWall.density)
			return ReplayError(error, "comparison_pyramidWall_density_differs");
		if (SameComparisonVector(a.pyramidWall.floorHalfExtents, b.pyramidWall.floorHalfExtents) == 0)
			return ReplayError(error, "comparison_pyramidWall_floorHalfExtents_differs");
	}
	if (a.fixtureKind == CaseFixtureKind_RayTracing)
		return ReplayError(error, "comparison_requires_3d_recording");
	*error = {};
	return ArenaStatus_Ok;
}

ArenaStatus AdmitReplayComparison(const ResultManifestRecord& primaryManifest, const ReplayRecording& primary,
                                  const ResultManifestRecord& peerManifest, const ReplayRecording& peer,
                                  std::uint64_t* commonFinalOrdinal, StatusRecord* error)
{
	const ArenaStatus status = CheckReplayComparisonConfiguration(primaryManifest, peerManifest, error);
	if (status != ArenaStatus_Ok)
		return status;
	if (primary.workKind != peer.workKind)
		return ReplayError(error, "comparison_work_kind_differs");
	if (primary.timestep != peer.timestep)
		return ReplayError(error, "comparison_timestep_differs");
	if (primary.scene.identity.warmupSteps != peer.scene.identity.warmupSteps)
		return ReplayError(error, "comparison_warmup_differs");
	*commonFinalOrdinal = (std::min)(primary.layout.frameCount, peer.layout.frameCount) - 1;
	return ArenaStatus_Ok;
}
}
