#pragma once

#include "physics_arena/catalog.h"

#include <span>
#include <string>

namespace physics_arena
{
CaseRecord RunObservationCase(const CaseRecord& benchmarkCase, VerificationMode verificationMode);

enum RunConfigurationMode
{
	RunConfigurationMode_Authored = 0,
	RunConfigurationMode_Custom = 1,
};

struct EngineRunSettings
{
	CaseNativeSolver solver;
	CaseExecutionToggle sleepMode;
	CaseExecutionToggle continuousCollisionMode;
	CaseExecutionToggle solverStabilization;
	PresenceStatus solverStabilizationPresence;
	float friction;
	float restitution;
	float linearDamping;
	float angularDamping;
};

enum RunConfigurationReadPurpose
{
	RunConfigurationReadPurpose_FrozenResult = 0,
	RunConfigurationReadPurpose_Editable = 1,
};

enum EnginePhysicsField
{
	EnginePhysicsField_Friction = 0,
	EnginePhysicsField_Restitution,
	EnginePhysicsField_SleepMode,
	EnginePhysicsField_ContinuousCollisionMode,
	EnginePhysicsField_LinearDamping,
	EnginePhysicsField_AngularDamping,
	EnginePhysicsField_SolverStabilization,
	EnginePhysicsField_Count,
};

std::string_view EnginePhysicsFieldName(EnginePhysicsField field);
std::string EnginePhysicsFieldValue(const EngineRunSettings& settings, EnginePhysicsField field);
struct EffectiveRunConfiguration;
CaseExecutionSpec ResolveEngineCaseExecution(const EffectiveRunConfiguration* configuration,
                                            std::uint32_t selectedEngineIndex);

struct RunSettings
{
	CaseExecutionSpec caseInputs;
	CaseVisualCameraPolicy camera;
	std::array<EngineRunSettings, kEngineCapacity> engineSettings;
	std::uint32_t caseIndex;
	PresenceStatus presence;
};

struct RunSettingsArgumentState
{
	std::uint32_t scheduleFields;
	std::array<std::uint32_t, kEngineCapacity> solverFields;
	std::array<std::uint32_t, kEngineCapacity> physicsFields;
};

ArenaStatus ApplyRunSettingsArgument(const Catalog* catalog, std::string_view option, std::string_view text,
                                     RunSettings* settings, RunSettingsArgumentState* seen, StatusRecord* error);

struct EffectiveRunConfiguration
{
	CaseExecutionSpec execution;
	CaseRecord benchmarkCase;
	std::array<EngineRunSettings, kEngineCapacity> selectedEngineSettings;
	std::array<ObservationDeclaration, kObservationPerCaseCapacity> observations;
	std::array<std::uint32_t, kObservationPerCaseCapacity * kObservationSamplePerDeclarationCapacity> sampleIndices;
	std::array<ResultGroupRecord, kObservationPerCaseCapacity> resultGroups;
	std::array<std::uint32_t, kThreadCountCapacity> authoredThreadCounts;
	std::array<char, kCatalogTextArenaCapacity> textArena;
	CatalogText authoredCaseId;
	CatalogText authoredCaseSlug;
	CatalogText authoredCaseDisplayName;
	CatalogText authoredCaseDescription;
	std::uint32_t authoredFixtureRevision;
	std::uint32_t textArenaUsed;
	std::uint32_t sampleIndexCount;
	RunConfigurationMode mode;
};

std::string_view RunConfigurationTextView(const EffectiveRunConfiguration* configuration, CatalogText text);
std::string_view CaseConfigurationTextView(const Catalog* catalog, const EffectiveRunConfiguration* configuration,
                                           CatalogText text);
const ObservationDeclaration& CaseConfigurationObservation(const Catalog* catalog, const CaseRecord& record,
                                                           const EffectiveRunConfiguration* configuration,
                                                           std::uint32_t ordinal);
std::uint32_t CaseConfigurationSampleIndex(const Catalog* catalog, const EffectiveRunConfiguration* configuration,
                                           const ObservationDeclaration& declaration, std::uint32_t ordinal);
ArenaStatus WriteRunConfigurationSnapshot(const Catalog* catalog, const EffectiveRunConfiguration* configuration,
                                          std::span<const std::uint32_t> engineIndexes, std::string* json,
                                          StatusRecord* error, const CaseExecutionSpec* editableInputs = nullptr);
ArenaStatus AdmitRunConfigurationJsonKeys(std::string_view json, StatusRecord* error);
ArenaStatus ReadRunConfigurationSnapshot(const Catalog* catalog, std::string_view json,
                                         std::span<const std::uint32_t> engineIndexes,
                                         EffectiveRunConfiguration* configuration, StatusRecord* error,
                                         RunSettings* editableSettings = nullptr,
                                         RunConfigurationReadPurpose purpose = RunConfigurationReadPurpose_FrozenResult);
std::string_view NativeSolverFieldName(CaseSolverField field);
ArenaStatus ParseNativeSolverField(std::string_view name, CaseSolverField* field);
ArenaStatus ResetRunSettings(const Catalog* catalog, std::uint32_t caseIndex, RunSettings* settings,
                             StatusRecord* error);
void ResetEngineRunSettings(const Catalog* catalog, std::uint32_t engineIndex, RunSettings* settings);
ArenaStatus AdmitEngineRunSettings(const Catalog* catalog, std::uint32_t engineIndex,
                                   const CaseExecutionSpec& execution, const EngineRunSettings& settings,
                                   StatusRecord* error);
ArenaStatus ComposeRunSettings(const Catalog* catalog, std::uint32_t caseIndex, const RunSettings* settings,
                               std::span<const std::uint32_t> engineIndexes, EffectiveRunConfiguration* configuration,
                               StatusRecord* error);
ArenaStatus EncodeEffectiveCaseExecutionHex(const EffectiveRunConfiguration* configuration,
                                            std::uint32_t selectedEngineIndex, char* hex, std::uint32_t capacity,
                                            std::uint32_t* size, StatusRecord* error);
}
