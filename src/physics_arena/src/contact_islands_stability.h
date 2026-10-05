#pragma once

#include "physics_arena/stack_stability.h"
#include "stack_stability_internal.h"

namespace physics_arena
{
int BuildContactIslandsEnvelopes(const CaseExecutionSpec& spec, std::uint32_t boxes,
                                const std::array<double, 3>& half, std::vector<RestEnvelope>* references);
StackRule AssessContactIslandsBody(const CaseExecutionContactIslands& fixture,
                                 std::span<const benchmark_stack::Pose> poses,
                                 std::span<const RestEnvelope> references,
                                 const std::array<double, 3>& half, std::uint32_t body,
                                 StackSample sample, StackStabilityResult* result);
}
