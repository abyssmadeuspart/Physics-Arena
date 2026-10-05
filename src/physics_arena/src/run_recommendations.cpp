#include "physics_arena/run_recommendations.h"

#include <algorithm>
#include <string>

namespace physics_arena
{
constexpr std::array<std::string_view, kRecommendedRunCount> kRecommendedCaseIds =
{
	"box_container_pile_10k", "box_contact_islands_10k", "large_pyramid_16206"
};

PresenceStatus RecommendedRunFamily(CaseFixtureKind family)
{
	return family >= CaseFixtureKind_OpenContainerFallingPile && family <= CaseFixtureKind_PyramidWall
	    ? PresenceStatus_Present : PresenceStatus_Absent;
}

ArenaStatus ResolveRecommendedRunCases(const Catalog* catalog, RecommendedRunCases* cases, StatusRecord* error)
{
	RecommendedRunCases resolved = {};
	for (std::size_t ordinal = 0; ordinal < kRecommendedCaseIds.size(); ++ordinal)
	{
		std::uint32_t matches = 0;
		for (std::uint32_t index = 0; index < catalog->caseCount; ++index)
			if (catalog->cases[index].shapePreset == CaseShapePreset_Authored &&
			    CatalogTextView(catalog, catalog->cases[index].id) == kRecommendedCaseIds[ordinal])
			{
				resolved.indexes[ordinal] = index;
				++matches;
			}
		if (matches != 1)
		{
			*error = {};
			const std::string detail = "recommended authored case missing or duplicate: " + std::string(kRecommendedCaseIds[ordinal]);
			std::copy(detail.begin(), detail.end(), error->detail.begin());
			error->detailSize = static_cast<std::uint32_t>(detail.size());
			error->code = ArenaStatus_InvalidResult;
			return error->code;
		}
	}
	*cases = resolved;
	return ArenaStatus_Ok;
}
}
