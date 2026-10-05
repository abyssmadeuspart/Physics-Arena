#include "physics_arena/run_repeat_policy.h"

#include <algorithm>
#include <cstdio>

namespace physics_arena
{
const char* RepeatFailureCauseName(RepeatFailureCause cause)
{
	constexpr std::array<const char*, 7> names = {
		"none", "execution_failed", "case_status=failed", "invalid_transforms",
		"expected_observation_failed", "stack_assessment_failed", "ray_numerical_failed"
	};
	return names[cause];
}

void SuppressRemainingRepeatTuples(const Catalog* catalog, const CompletedRepeatEvidence& completed,
                                  std::span<const RunRepeatTuple> remaining, std::vector<ExecutionFailure>* failures)
{
	if (completed.cause == RepeatFailureCause_None)
		return;
	const std::string_view engine = CatalogTextView(catalog, catalog->engines[completed.tuple.engineIndex].id);
	for (const RunRepeatTuple& tuple : remaining)
	{
		if (tuple.engineIndex != completed.tuple.engineIndex ||
		    FindExecutionFailure(*failures, tuple.engineIndex, tuple.threadCount, tuple.repeatIndex) != nullptr)
			continue;
		ExecutionFailure skipped = {};
		skipped.engineIndex = tuple.engineIndex;
		skipped.threadCount = tuple.threadCount;
		skipped.repeatIndex = tuple.repeatIndex;
		skipped.stage = ExecutionStage_Benchmark;
		skipped.outcome = ExecutionOutcome_NotRun;
		skipped.reason = ExecutionFailureReason_PreviousRepeatFailed;
		std::snprintf(skipped.detail.data(), skipped.detail.size(),
		    "Skipped after failed repeat: engine=%.*s thread=%u repeat=%u reason=%s %s",
		    static_cast<int>(engine.size()), engine.data(), completed.tuple.threadCount,
		    completed.tuple.repeatIndex, RepeatFailureCauseName(completed.cause), completed.detail.c_str());
		failures->push_back(skipped);
	}
}
}
