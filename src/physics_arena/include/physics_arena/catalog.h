#pragma once

#include "physics_arena/bench_types.h"

#include "case_execution_wire.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace physics_arena
{
constexpr std::size_t kCaseCapacity = 32;
static_assert(kCaseCapacity <= 32);
constexpr std::size_t kReportCapacity = 16;
constexpr std::size_t kEngineSetCapacity = 16;
constexpr std::size_t kEngineSetMemberCapacity = 16;
constexpr std::size_t kCatalogTextArenaCapacity = 32768;
constexpr std::size_t kCatalogCaseExecutionArenaCapacity = 65536;
constexpr std::size_t kCatalogTextValueCapacity = 512;
constexpr std::size_t kCatalogValueCapacity = 512;
constexpr std::size_t kObservationPerCaseCapacity = 10;
constexpr std::size_t kObservationSamplePerDeclarationCapacity = 20;
constexpr std::size_t kCatalogObservationCapacity = kCaseCapacity * kObservationPerCaseCapacity;
constexpr std::size_t kCatalogResultGroupCapacity = kCaseCapacity * kObservationPerCaseCapacity;
constexpr std::size_t kCatalogObservationSampleCapacity =
    kCatalogObservationCapacity * kObservationSamplePerDeclarationCapacity;
constexpr std::size_t kObservationRepeatRowCapacity = 200;

enum RouteStatus
{
	RouteStatus_Unknown = 0,
	RouteStatus_Supported = 1,
	RouteStatus_Working = 2,
	RouteStatus_Experimental = 3,
	RouteStatus_Unsupported = 4,
	RouteStatus_Unavailable = 5,
	RouteStatus_ToolMissing = 6,
	RouteStatus_ReferenceMismatch = 7,
	RouteStatus_FetchFailed = 8,
	RouteStatus_UnsupportedPlatform = 9,
};

enum ThreadSupportMode
{
	ThreadSupportMode_Unknown = 0,
	ThreadSupportMode_Single = 1,
	ThreadSupportMode_HostBounded = 2,
	ThreadSupportMode_Explicit = 3,
};

enum WorkerCountPolicy
{
	WorkerCountPolicy_Unknown = 0,
	WorkerCountPolicy_ThreadCount = 1,
	WorkerCountPolicy_ThreadCountMinusOne = 2,
};

enum MainThreadParticipation
{
	MainThreadParticipation_Unknown = 0,
	MainThreadParticipation_No = 1,
	MainThreadParticipation_Yes = 2,
};

enum PrimaryMetricDirection
{
	PrimaryMetricDirection_Unknown = 0,
	PrimaryMetricDirection_LowerIsBetter = 1,
	PrimaryMetricDirection_HigherIsBetter = 2,
	PrimaryMetricDirection_NotRanked = 3,
};

enum ObservationValueType
{
	ObservationValueType_Unknown = 0,
	ObservationValueType_Uint64 = 1,
	ObservationValueType_Float64 = 2,
};

enum ObservationRole
{
	ObservationRole_Unknown = 0,
	ObservationRole_ValidityZero = 1,
	ObservationRole_ValidityExact = 2,
	ObservationRole_Quality = 3,
	ObservationRole_Performance = 4,
};

enum CaseVisualCameraMode
{
	CaseVisualCameraMode_Unknown = 0,
	CaseVisualCameraMode_FitScene = 1,
	CaseVisualCameraMode_FitBounds = 2,
	CaseVisualCameraMode_Fixed = 3,
	CaseVisualCameraMode_FollowStableSlot = 4,
};

struct CaseVisualCameraVector
{
	float x;
	float y;
	float z;
};

struct CaseVisualCameraPolicy
{
	CaseVisualCameraVector direction;
	CaseVisualCameraVector up;
	CaseVisualCameraVector minimum;
	CaseVisualCameraVector maximum;
	CaseVisualCameraVector eye;
	CaseVisualCameraVector target;
	CaseVisualCameraVector eyeOffset;
	CaseVisualCameraVector targetOffset;
	float verticalFovDegrees;
	float viewportFill;
	float nearPlane;
	float farPlane;
	std::uint32_t stableSlot;
	CaseVisualCameraMode mode;
};

struct CatalogText
{
	std::uint32_t offset;
	std::uint32_t size;
};

struct ObservationDeclaration
{
	CatalogText id;
	CatalogText label;
	CatalogText unit;
	CatalogText phaseId;
	std::uint32_t sampleIndicesOffset;
	std::uint32_t sampleIndexCount;
	std::uint32_t resultGroupOrdinal;
	std::uint64_t expectedUnsigned;
	double expectedFloat64;
	ObservationValueType valueType;
	ObservationRole role;
	PresenceStatus expectedValuePresence;
};

struct ResultGroupRecord
{
	CatalogText id;
	CatalogText label;
};

struct NativeSolverCapability
{
	CatalogText nativeName;
	std::uint32_t minimum;
	std::uint32_t maximum;
	std::uint32_t authoredDefault;
};

enum RagdollDampingMode
{
	RagdollDampingMode_NativeCoefficient = 0,
	RagdollDampingMode_PerSecondFraction,
	RagdollDampingMode_FixedZero,
};

enum ContinuousCollisionSemantics
{
	ContinuousCollisionSemantics_NativeToggle = 0,
	ContinuousCollisionSemantics_PassiveContinuous,
	ContinuousCollisionSemantics_DiscreteLinearCast,
	ContinuousCollisionSemantics_DiscreteSweptCCD,
	ContinuousCollisionSemantics_Unavailable,
};

struct EngineSettingsCapabilities
{
	std::array<NativeSolverCapability, CaseSolverField_Count> solver;
	std::uint32_t solverFields;
	std::uint32_t sleepEnabledFixtures;
	std::uint32_t sleepDisabledFixtures;
	std::uint32_t continuousCollisionFixtures;
	float maximumRestitution;
	RagdollDampingMode ragdollDampingMode;
	ContinuousCollisionSemantics continuousCollisionSemantics;
	std::uint32_t solverStabilizationFixtures;
	CaseExecutionToggle solverStabilizationDefault;
};

struct EngineRecord
{
	CatalogText id;
	CatalogText displayName;
	CatalogText releaseArtifactManifest;
	EngineSettingsCapabilities settings;
	std::uint32_t engineSetsOffset;
	std::uint32_t supportedThreadCountsOffset;
	std::uint32_t colorRgb;
	std::uint32_t engineSetCount;
	std::uint32_t supportedThreadCount;
	std::uint32_t supportedCaseFamilyMask;
	RouteStatus runStatus;
	ThreadSupportMode threadSupportMode;
	WorkerCountPolicy requestedWorkerPolicy;
	WorkerCountPolicy effectiveWorkerPolicy;
	MainThreadParticipation mainThreadParticipation;
};

struct EngineSetRecord
{
	CatalogText id;
	std::uint32_t engineIndexesOffset;
	std::uint32_t engineCount;
};

struct CaseRecord
{
	std::uint32_t authoredCaseIndex;
	CatalogText id;
	CatalogText slug;
	CatalogText displayName;
	CatalogText description;
	CatalogText category;
	CatalogText benchmarkMode;
	CatalogText fixtureSemantic;
	CatalogText workUnitId;
	CatalogText workUnitLabel;
	CatalogText primaryMetricId;
	CatalogText primaryMetricUnit;
	CatalogText primaryMetricLabel;
	CatalogText primaryMetricNote;
	std::uint32_t threadCountsOffset;
	std::uint32_t observationOffset;
	std::uint32_t resultGroupOffset;
	std::uint32_t caseExecutionOffset;
	std::uint32_t caseExecutionSize;
	std::uint32_t threadCount;
	std::uint32_t qualificationRepeats;
	std::uint32_t fullRepeats;
	std::uint32_t dynamicBodyCount;
	std::uint32_t kinematicBodyCount;
	std::uint32_t staticBodyCount;
	std::uint32_t bodyCount;
	std::uint32_t shapeCount;
	std::uint32_t visualInstanceCount;
	std::uint32_t meshTriangleCount;
	std::uint32_t queryCount;
	std::uint32_t constraintCount;
	std::uint32_t timestepHz;
	std::uint32_t measuredWorkUnitCount;
	std::uint32_t warmupWorkUnitCount;
	std::uint32_t fixtureRevision;
	std::uint32_t observationCount;
	std::uint32_t resultGroupCount;
	CaseVisualCameraPolicy visualCamera;
	CaseFixtureKind fixtureKind;
	CaseShapePreset shapePreset;
	PrimaryMetricDirection primaryMetricDirection;
	PresenceStatus measuredWorkUnitCountPresence;
	PresenceStatus timestepPresence;
};

struct ReportRecord
{
	CatalogText id;
	CatalogText title;
	CatalogText caseId;
	CatalogText resultSource;
	CatalogText chartMetric;
	CatalogText chartLabel;
	CatalogText chartNote;
	PresenceStatus chartOverridePresence;
};

struct Catalog
{
	std::array<EngineRecord, kEngineCapacity> engines;
	std::array<EngineSetRecord, kEngineSetCapacity> engineSets;
	std::array<CaseRecord, kCaseCapacity> cases;
	std::array<ReportRecord, kReportCapacity> reports;
	std::array<char, kCatalogTextArenaCapacity> textArena;
	std::array<std::uint8_t, kCatalogCaseExecutionArenaCapacity> caseExecutionArena;
	std::array<CatalogText, kCatalogTextValueCapacity> textValues;
	std::array<std::uint32_t, kCatalogValueCapacity> values;
	std::array<ObservationDeclaration, kCatalogObservationCapacity> observations;
	std::array<ResultGroupRecord, kCatalogResultGroupCapacity> resultGroups;
	std::array<std::uint32_t, kCatalogObservationSampleCapacity> observationSampleIndices;
	CatalogText defaultPublicCase;
	std::uint32_t engineCount;
	std::uint32_t engineSetCount;
	std::uint32_t caseCount;
	std::uint32_t reportCount;
	std::uint32_t textArenaUsed;
	std::uint32_t caseExecutionArenaUsed;
	std::uint32_t textValueCount;
	std::uint32_t valueCount;
	std::uint32_t observationCount;
	std::uint32_t resultGroupCount;
	std::uint32_t observationSampleIndexCount;
	std::uint32_t defaultReleaseSetIndex;
	PresenceStatus defaultReleaseSetPresence;
};

std::string_view CatalogTextView(const Catalog* catalog, CatalogText text);
int CaseSupportsThreadCount(const Catalog* catalog, std::uint32_t caseIndex, std::uint32_t threadCount);
ArenaStatus LoadCatalog(const wchar_t* repositoryRoot, Catalog* catalog, StatusRecord* error);
}
