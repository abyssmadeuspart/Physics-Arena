#pragma once

#include "physics_arena/catalog.h"

namespace physics_arena
{
constexpr std::size_t kRecommendedRunCount = 3;
struct RecommendedRunCases
{
	std::array<std::uint32_t, kRecommendedRunCount> indexes;
};

ArenaStatus ResolveRecommendedRunCases(const Catalog* catalog, RecommendedRunCases* cases, StatusRecord* error);
PresenceStatus RecommendedRunFamily(CaseFixtureKind family);
}
