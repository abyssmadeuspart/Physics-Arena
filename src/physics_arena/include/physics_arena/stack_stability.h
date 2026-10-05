#pragma once

#include "physics_arena/bench_types.h"
#include "stack_state_capture.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace physics_arena
{
constexpr const char* kStackCriterion = "box-fall-below-support-plane";
constexpr const char* kStackMarginPolicy = "current-authored-support-layer";
constexpr const char* kLegacyStackCriterion = "authored-box-rest-envelope";
constexpr const char* kLegacyStackMarginPolicy = "0.05-min-full-edge";
constexpr const char* kContainerCriterion = "box-container-upward-escape";
constexpr const char* kContainerMarginPolicy = "authored-container-rim";
constexpr const char* kContactIslandsCriterion = "contact-islands-settling-and-shape";
constexpr const char* kContactIslandsMarginPolicy = "terminal-1s-shape-0.02-edge-motion-0.01-edge-per-s";
constexpr const char* kContactIslands10cmMarginPolicy = "terminal-1s-shape-0.10-edge-motion-0.01-edge-per-s";
constexpr const char* kContactIslandsShapeCriterion = "contact-islands-safety-and-shape";
constexpr const char* kContactIslandsShapeMarginPolicy = "terminal-1s-shape-0.10-edge";

constexpr const char* kPyramidUnforcedShapeCriterion = "pyramid-safety-and-unforced-shape";
constexpr const char* kPyramidUnforcedShapeMarginPolicy = "unforced-shape-0.10-m";

enum StackCriterion
{
	StackCriterion_SupportPlane = 0,
	StackCriterion_LegacyMovement = 1,
	StackCriterion_ContainerEscape = 2,
	StackCriterion_ContactIslandsStabilization = 3,
	StackCriterion_ContactIslandsStabilization10cm = 4,
	StackCriterion_ContactIslandsShapePreservation = 5,
	StackCriterion_PyramidUnforcedShape = 6,
};

enum StackAssessment
{
	StackAssessment_Unassessed = 0,
	StackAssessment_Pass = 1,
	StackAssessment_Fail = 2,
};
enum StackCoverage
{
	StackCoverage_Incomplete = 0,
	StackCoverage_Complete = 1,
};
enum StackRule
{
	StackRule_None = 0,
	StackRule_RestEnvelope = 1,
	StackRule_UpwardExcursion = 2,
	StackRule_InvalidState = 3,
	StackRule_SupportPlane = 4,
	StackRule_ContainerEscape = 5,
	StackRule_TerminalSlotEnvelope = 6,
	StackRule_TerminalAdjacentCompression = 7,
	StackRule_TerminalMotion = 8,
	StackRule_UnforcedSlotEnvelope = 9,
	StackRule_UnforcedAdjacentCompression = 10,
};

struct StackSample
{
	std::uint32_t segment;
	benchmark_stack::Phase phase;
	std::uint32_t step;
};
struct StackMetric
{
	double value;
	std::uint32_t body;
	StackSample sample;
};
struct StackStabilityResult
{
	std::string runId;
	std::string engineId;
	std::uint32_t threadCount;
	std::uint32_t repeatIndex;
	StackAssessment assessment;
	StackCoverage coverage;
	StackCriterion criterion;
	std::string reason;
	double margin;
	StackSample firstSample;
	StackSample lastSample;
	PresenceStatus impactPresence;
	StackSample impactIntervalEnd;
	double sampleSeconds;
	StackRule firstRule;
	std::uint32_t firstBody;
	StackSample firstBreach;
	StackMetric restViolation;
	StackMetric upwardExcursion;
	StackMetric settlingStepDisplacement;
	StackMetric terminalStackShapeExcess;
	PresenceStatus terminalMetricsPresence;
	StackMetric unforcedStackShapeExcess;
	PresenceStatus unforcedShapePresence;
	double captureElapsedMs;
	double analysisElapsedMs;
};

ArenaStatus AnalyzeStackTrace(const std::filesystem::path& trace, const CaseExecutionSpec& execution,
	                          std::string_view runId, std::string_view engineId, std::uint32_t threads,
	                          std::uint32_t repeat, StackStabilityResult* result, StatusRecord* error);
struct ReplayRecording;
ArenaStatus AnalyzeContainerRecording(ReplayRecording* recording, const CaseExecutionSpec& execution,
                                     std::string_view runId, std::string_view engineId, std::uint32_t threads,
                                     std::uint32_t repeat, StackStabilityResult* result, StatusRecord* error);
ArenaStatus AnalyzeContactIslandsRecording(ReplayRecording* recording, const CaseExecutionSpec& execution,
                                          std::string_view runId, std::string_view engineId, std::uint32_t threads,
                                          std::uint32_t repeat, StackStabilityResult* result, StatusRecord* error);
StackCriterion CurrentStackCriterion(CaseFixtureKind fixture);
const char* StackCriterionName(StackCriterion criterion);
const char* StackMarginPolicyName(StackCriterion criterion);
ArenaStatus LoadStackStability(const std::filesystem::path& path, std::vector<StackStabilityResult>* results,
	                           StatusRecord* error);
ArenaStatus CommitStackStability(const std::filesystem::path& path, const StackStabilityResult& result,
	                             StatusRecord* error);
ArenaStatus WriteStackStability(const std::filesystem::path& path, std::span<const StackStabilityResult> results,
                               StatusRecord* error);
ArenaStatus CertifyLegacyStackPass(const CaseExecutionSpec& execution, const StackStabilityResult& saved,
                                 StackStabilityResult* corrected, StatusRecord* error);
StackAssessment StackQualificationAssessment(const StackStabilityResult& result, StackCriterion requiredCriterion);
double ContactIslandsSettlingStepLimit(const StackStabilityResult& result);
ArenaStatus ValidateStackShapeStabilityEvidence(const CaseExecutionSpec& execution,
                                                   const StackStabilityResult& result, StatusRecord* error);
StackAssessment AggregateStackStability(std::span<const StackStabilityResult> results, std::string_view engine,
	                                    std::uint32_t threads, std::uint32_t repeats, StackCriterion requiredCriterion);
}
