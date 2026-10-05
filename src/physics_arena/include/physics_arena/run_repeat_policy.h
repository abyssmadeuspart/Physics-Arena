#pragma once

#include "physics_arena/release_contracts.h"

namespace physics_arena
{
enum RepeatFailureCause
{
	RepeatFailureCause_None,
	RepeatFailureCause_Execution,
	RepeatFailureCause_CaseStatus,
	RepeatFailureCause_InvalidTransforms,
	RepeatFailureCause_ExpectedObservation,
	RepeatFailureCause_StackAssessment,
	RepeatFailureCause_RayNumerical,
};

struct RunRepeatTuple
{
	std::uint32_t engineIndex, threadCount, repeatIndex;
};

struct CompletedRepeatEvidence
{
	RunRepeatTuple tuple;
	RepeatFailureCause cause;
	std::string detail;
};

const char* RepeatFailureCauseName(RepeatFailureCause cause);
void SuppressRemainingRepeatTuples(const Catalog* catalog, const CompletedRepeatEvidence& completed,
                                  std::span<const RunRepeatTuple> remaining, std::vector<ExecutionFailure>* failures);
}
