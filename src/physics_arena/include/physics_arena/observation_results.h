#pragma once

#include "physics_arena/timing_series.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace physics_arena
{
constexpr std::size_t kObservationArtifactSliceCapacity = kEngineCapacity * kThreadCountCapacity;
constexpr std::size_t kObservationAggregateCapacity = kObservationArtifactSliceCapacity * kObservationPerCaseCapacity;

enum ObservationOutcome
{
	ObservationOutcome_Unknown = 0,
	ObservationOutcome_Ok = 1,
	ObservationOutcome_Failed = 2,
};

union ObservationValue
{
	std::uint64_t unsignedValue;
	double float64Value;
};

struct ObservationAggregate
{
	ObservationValue minimumActual;
	ObservationValue maximumActual;
	ObservationValue actualTotal;
	ObservationValue expectedTotal;
	std::uint32_t sampleCount;
	std::uint32_t failedSampleCount;
	ObservationOutcome outcome;
};

struct ObservationArtifactSlice
{
	std::vector<std::uint64_t> repeatOffsets;
	std::vector<std::uint32_t> repeatRows;
	std::uint64_t firstRowByteOffset;
	std::uint32_t engineIndex;
	std::uint32_t threadCount;
	std::uint32_t rowCount;
	std::uint32_t repeatCount;
	std::uint32_t rowsPerRepeat;
};

struct ObservationResultModel
{
	std::array<ObservationArtifactSlice, kObservationArtifactSliceCapacity> slices;
	std::array<ObservationAggregate, kObservationAggregateCapacity> aggregates;
	std::array<wchar_t, kTimingArtifactPathCapacity> path;
	std::uint64_t sourceSize;
	std::uint64_t rowCount;
	std::uint32_t sliceCount;
	std::uint32_t aggregateCount;
	std::uint32_t engineCount;
	std::uint32_t threadCount;
	std::uint32_t declarationCount;
	PresenceStatus availability;
};

struct ObservationDetailRecord
{
	ObservationValue actual;
	ObservationValue expected;
	std::uint32_t declarationOrdinal;
	std::uint32_t sampleIndex;
	ObservationOutcome outcome;
};

struct ObservationDetailSelection
{
	std::uint32_t engineIndex;
	std::uint32_t threadCount;
	std::uint32_t repeatIndex;
};

struct ObservationDetailProjection
{
	std::array<ObservationDetailRecord, kObservationRepeatRowCapacity> rows;
	std::uint32_t rowCount;
	AvailabilityStatus availability;
};

std::string_view ObservationRoleWireText(ObservationRole role);
std::string_view ObservationOutcomeText(ObservationOutcome outcome);
const ObservationAggregate* ObservationAggregateAt(const ObservationResultModel* model, std::uint32_t engineOrdinal,
                                                   std::uint32_t threadOrdinal, std::uint32_t declarationOrdinal);
ArenaStatus LoadObservationResultModel(const wchar_t* path, const Catalog* catalog, const CaseRecord* benchmarkCase,
                                       const ResultManifestRecord* manifest, ObservationResultModel* model,
                                       StatusRecord* error);
ArenaStatus ProjectObservationDetail(const ObservationResultModel* model, const Catalog* catalog,
                                     const CaseRecord* benchmarkCase, const ObservationDetailSelection* selection,
                                     ObservationDetailProjection* projection, StatusRecord* error,
                                     const EffectiveRunConfiguration* configuration = nullptr);
}
