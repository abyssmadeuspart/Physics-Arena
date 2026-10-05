#pragma once

#include "physics_arena/release_contracts.h"

#include <array>
#include <vector>
#include <cstdint>

namespace physics_arena
{
constexpr std::size_t kTimingUnitCapacity = 512;
constexpr std::size_t kTimingSampleCapacity = 98304;
constexpr std::size_t kTimingProjectionStepCapacity = 4096;
constexpr std::size_t kTimingRepeatCapacity = 64;
constexpr std::size_t kTimingSliceCapacity = kEngineCapacity * kThreadCountCapacity;
constexpr std::size_t kTimingArtifactPathCapacity = 4096;

enum TimingRenderSamplePresence : std::uint8_t
{
	TimingRenderSamplePresence_Absent = 0,
	TimingRenderSamplePresence_Present = 1,
};

struct TimingUnitRange
{
	std::uint32_t engineIndex;
	std::uint32_t threadCount;
	std::uint32_t repeatIndex;
	std::uint32_t sampleOffset;
	std::uint32_t sampleCount;
	std::uint32_t expectedWorkUnitCount;
	double previousCumulativePhysicsMs;
	double physicsTotalMs;
	double renderTotalMs;
	PresenceStatus completion;
};

struct TimingSeriesModel
{
	std::array<TimingUnitRange, kTimingUnitCapacity> units;
	std::array<std::uint32_t, kTimingSampleCapacity> stepIndexes;
	std::array<double, kTimingSampleCapacity> physicsStepMilliseconds;
	std::array<double, kTimingSampleCapacity> renderFrameMilliseconds;
	std::array<TimingRenderSamplePresence, kTimingSampleCapacity> renderSamplePresence;
	std::uint32_t unitCount;
	std::uint32_t sampleCount;
	PresenceStatus availability;
};

struct TimingProjection
{
	std::array<std::uint32_t, kTimingProjectionStepCapacity> stepIndexes;
	std::array<double, kTimingProjectionStepCapacity> physicsStepMilliseconds;
	std::array<double, kTimingProjectionStepCapacity> minimumPhysicsStepMilliseconds;
	std::array<double, kTimingProjectionStepCapacity> maximumPhysicsStepMilliseconds;
	double medianPhysicsStepMilliseconds;
	double p95PhysicsStepMilliseconds;
	double p99PhysicsStepMilliseconds;
	double maximumPhysicsStepMillisecondsValue;
	std::uint32_t sampleCount;
	std::uint32_t repeatCount;
	std::uint32_t worstStepIndex;
};

struct TimingArtifactSlice
{
	std::vector<std::uint32_t> repeatIndexes;
	std::uint64_t firstRowByteOffset;
	std::uint32_t engineIndex;
	std::uint32_t threadCount;
	std::uint32_t rowCount;
	std::uint32_t repeatCount;
	std::uint32_t measuredWorkUnitCount;
};

struct TimingArtifactTotals
{
	std::array<std::array<double, kTimingRepeatCapacity>, kTimingSliceCapacity> physicsMilliseconds;
};

struct TimingArtifactIndex
{
	std::array<TimingArtifactSlice, kTimingSliceCapacity> slices;
	std::array<wchar_t, kTimingArtifactPathCapacity> path;
	std::uint64_t sourceSize;
	std::uint64_t rowCount;
	std::uint32_t sliceCount;
	PresenceStatus availability;
	TimingRenderSeries renderSeries;
};

struct TimingProjectionScratch
{
	std::array<double, kTimingRepeatCapacity * kTimingProjectionStepCapacity> physicsStepMilliseconds;
};

enum TimingProjectionMode
{
	TimingProjectionMode_MedianAcrossRepeats = 0,
	TimingProjectionMode_ExactRepeat = 1,
};

struct TimingProjectionSelection
{
	std::uint32_t engineIndex;
	std::uint32_t threadCount;
	std::uint32_t repeatIndex;
	TimingProjectionMode mode;
};

struct TimingProjectionCache
{
	TimingProjection projection;
	TimingProjectionSelection selection;
	AvailabilityStatus availability;
	std::uint64_t generation;
};

static_assert(sizeof(TimingSeriesModel) < kMainStackReservationBytes / 3);

void InitializeTimingSeries(TimingSeriesModel* model, PresenceStatus availability);
ArenaStatus BeginTimingUnit(TimingSeriesModel* model, std::uint32_t engineIndex, std::uint32_t threadCount,
                            std::uint32_t repeatIndex, std::uint32_t expectedWorkUnitCount, std::uint32_t* unitIndex,
                            StatusRecord* error);
ArenaStatus AppendCumulativeTiming(TimingSeriesModel* model, std::uint32_t unitIndex, std::uint32_t stepIndex,
                                   double cumulativePhysicsMilliseconds, double renderFrameMilliseconds,
                                   TimingRenderSamplePresence renderPresence, StatusRecord* error);
ArenaStatus AppendTimingSample(TimingSeriesModel* model, std::uint32_t unitIndex, std::uint32_t stepIndex,
                               double physicsStepMilliseconds, double renderFrameMilliseconds,
                               TimingRenderSamplePresence renderPresence, StatusRecord* error);
ArenaStatus SetTimingRenderSample(TimingSeriesModel* model, std::uint32_t unitIndex, std::uint32_t stepIndex,
                                  double renderFrameMilliseconds, StatusRecord* error);
ArenaStatus CompleteTimingUnit(TimingSeriesModel* model, std::uint32_t unitIndex,
                               double expectedPhysicsTotalMilliseconds, double expectedRenderTotalMilliseconds,
                               double toleranceMilliseconds, StatusRecord* error);
ArenaStatus ProjectTimingMedian(const TimingSeriesModel* model, std::uint32_t engineIndex, std::uint32_t threadCount,
                                TimingProjection* projection, StatusRecord* error);
ArenaStatus LoadTimingArtifactIndex(const wchar_t* path, const Catalog* catalog, const ResultManifestRecord* manifest,
                                    TimingArtifactIndex* index, StatusRecord* error,
                                    TimingArtifactTotals* totals = nullptr);
ArenaStatus ProjectTimingSlice(const TimingArtifactIndex* index, const TimingProjectionSelection* selection,
                               TimingProjectionScratch* scratch, TimingProjection* projection, StatusRecord* error);
ArenaStatus LoadTimingCsv(const wchar_t* path, const Catalog* catalog, const ResultManifestRecord* manifest,
                          TimingSeriesModel* model, StatusRecord* error);
ArenaStatus WriteTimingCsv(const wchar_t* path, const Catalog* catalog, const ResultManifestRecord* manifest,
                           const TimingSeriesModel* model, StatusRecord* error);
}
