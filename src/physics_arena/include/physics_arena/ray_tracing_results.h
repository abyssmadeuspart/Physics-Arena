#pragma once

#include "case_execution_wire.h"

#include "physics_arena/bench_types.h"
#include "ray_tracing.h"
#include <span>

namespace physics_arena
{
enum RayCapability
{
	RayCapability_Supported,
	RayCapability_Unsupported,
	RayCapability_NativeSemanticsDiffer,
	RayCapability_Failed,
	RayCapability_ExecutionFailed,
	RayCapability_NotRun
};
struct RayResultRow
{
	std::array<char, 32> engine;
	std::uint32_t threads, repeat, view, suite;
	benchmark_ray::Phase phase;
	benchmark_ray::Api api;
	RayCapability capability;
	std::uint16_t observedFields;
	std::uint64_t queries, hitRays, errors, written, bufferBytes;
	double queryMs, updateMs, conditioningMs, validationMs, setupMs, suiteMs;
};
struct RayPhaseStatistics
{
	std::uint64_t queries, hitRays, missRays, errors;
	double totalMs, medianMs, p95Ms, raysPerSecond, updateMedianMs, combinedMedianMs;
	double repeatMedianMinMs, repeatMedianMaxMs, repeatRaysPerSecondMin, repeatRaysPerSecondMax;
	double hitPercent, missPercent;
	std::uint32_t samples, repeats, requestedSamples, requestedRepeats, observedSamples;
	std::uint32_t partialRepeats, missingRepeats, executionFailedRepeats, notRunRepeats;
	PresenceStatus proportions, capabilityPresence, completeCoverage;
	RayCapability capability;
};
struct RayProbeRow
{
	std::array<char, 32> engine;
	std::uint32_t threads, repeat, view;
	benchmark_ray::Probe probe;
	RayCapability capability;
	std::uint32_t queries, hitRays, mismatches, nativeErrors, overflows, worldColliders;
};
struct RayProcessMemory
{
	std::array<char, 32> engine;
	std::uint32_t threads, repeat;
	std::uint64_t peakCommittedBytes;
	PresenceStatus available;
};
ArenaStatus ValidateRayAuxiliaryTuple(const std::vector<RayProbeRow>& probes,
                                      const std::vector<RayProcessMemory>& memory, std::string_view engine,
                                      std::uint32_t threads, std::uint32_t repeat, StatusRecord* error,
                                      PresenceStatus requireComplete = PresenceStatus_Present);
ArenaStatus WriteRayProcessMemory(const std::filesystem::path& directory, std::string_view engine,
                                  std::uint32_t threads, std::uint32_t repeat, void* process, StatusRecord* error);
ArenaStatus ReadRayProcessMemory(const std::filesystem::path& path, std::vector<RayProcessMemory>* rows,
                                 StatusRecord* error);
int RayMeasurementComplete(RayCapability capability);
const char* RayCapabilityName(RayCapability capability);
ArenaStatus ReadRayCapabilities(const std::filesystem::path& path, std::vector<RayProbeRow>* rows, StatusRecord* error);
ArenaStatus ReadRayTracingResults(const std::filesystem::path& path, std::vector<RayResultRow>* rows,
                                  StatusRecord* error);
ArenaStatus ValidateRaySecondaryWorkload(const std::vector<RayResultRow>& rows, const std::filesystem::path& corpus,
                                         std::uint32_t viewCount, StatusRecord* error);
ArenaStatus ValidateRayTracingTuple(const std::vector<RayResultRow>& rows, std::string_view engine,
                                    std::uint32_t threads, std::uint32_t repeat, const CaseExecutionSpec& execution, StatusRecord* error,
                                    PresenceStatus requireComplete = PresenceStatus_Present);
struct Catalog;
struct ResultManifestRecord;
struct ExecutionFailure;
ArenaStatus ValidateSavedRayResults(const std::filesystem::path& directory, const Catalog* catalog,
                                    const ResultManifestRecord* manifest, StatusRecord* error);
struct PreparedRunRequest;
struct RunPathRecord;
ArenaStatus MergeRayTracingResults(const Catalog* catalog, const PreparedRunRequest* request,
                                   const RunPathRecord* paths, StatusRecord* error);
RayPhaseStatistics SummarizeRayPhase(const std::vector<RayResultRow>& rows, std::string_view engine,
                                     std::uint32_t threads, benchmark_ray::Phase phase, benchmark_ray::Api api,
                                     std::uint32_t requestedRepeats, std::uint32_t measuredSuites, std::span<const ExecutionFailure> failures, std::uint32_t engineIndex);
const char* RayPhaseOutcomeText(const RayPhaseStatistics& statistics);
}
