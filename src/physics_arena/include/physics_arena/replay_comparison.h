#pragma once

#include "physics_arena/replay.h"
#include "physics_arena/release_contracts.h"

namespace physics_arena
{
ArenaStatus CheckReplayComparisonConfiguration(const ResultManifestRecord& primary,
                                               const ResultManifestRecord& peer, StatusRecord* error);
ArenaStatus AdmitReplayComparison(const ResultManifestRecord& primaryManifest, const ReplayRecording& primary,
                                  const ResultManifestRecord& peerManifest, const ReplayRecording& peer,
                                  std::uint64_t* commonFinalOrdinal, StatusRecord* error);
}
