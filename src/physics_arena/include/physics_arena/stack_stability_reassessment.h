#pragma once

#include "physics_arena/release_contracts.h"

namespace physics_arena
{
enum StackReassessmentMode
{
	StackReassessmentMode_Inspect,
	StackReassessmentMode_Apply,
};

enum StackReassessmentSource
{
	StackReassessmentSource_RetainedTrace,
	StackReassessmentSource_CertifiedLegacyPass,
	StackReassessmentSource_CurrentAssessment,
	StackReassessmentSource_ExecutionFailure,
	StackReassessmentSource_Unavailable,
	StackReassessmentSource_Recording,
};

struct StackReassessmentTuple
{
	StackStabilityResult proposed;
	StackReassessmentSource source;
	std::string evidenceLimit;
};

struct StackReassessmentRecord
{
	std::vector<StackReassessmentTuple> tuples;
	std::vector<ExecutionFailure> obsoleteSkips;
	std::uint32_t changedTupleCount;
	std::uint32_t reassessedTupleCount;
};

ArenaStatus ReassessSavedStackStability(const wchar_t* repositoryRoot, const wchar_t* resultDirectory,
                                      const Catalog* catalog, StackReassessmentMode mode,
                                      StackReassessmentRecord* record, StatusRecord* error);
const char* StackReassessmentSourceName(StackReassessmentSource source);
}
