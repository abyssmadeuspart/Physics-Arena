#pragma once

#include "physics_arena/catalog.h"

#include <cstdint>

namespace physics_arena
{
struct VisualProofRecord
{
	double physicsTotalMs;
	double renderTotalMs;
	double presentWaitMs;
	double primaryMetricValue;
	std::uint32_t completedStepCount;
	std::uint32_t receivedSnapshotCount;
	std::uint32_t renderedSnapshotCount;
	std::uint32_t supersededSnapshotCount;
	std::uint32_t firstDisplayedStepIndex;
	std::uint32_t finalDisplayedStepIndex;
};

ArenaStatus LoadVisualProof(const wchar_t* path, const Catalog* catalog, const CaseRecord& benchmarkCase,
                            std::uint32_t expectedSteps, VisualProofRecord* proof, StatusRecord* error);
}
