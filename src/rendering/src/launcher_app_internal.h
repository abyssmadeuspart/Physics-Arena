#pragma once

#include "benchmark_visual/physics_arena.h"
#include "native_value_format.h"

#include <array>
#include <cstdint>
#include <string_view>

struct ImVec2;
struct ImVec4;

namespace benchmark_visual
{
inline constexpr float kNativeMenuHeight = 31;
inline constexpr float kNativeToolbarHeight = 39;
inline constexpr float kNativeDockHeaderHeight = 28;
inline constexpr float kNativeDockHeight = 142;
inline constexpr float kNativeStatusHeight = 22;
int DrawWorkspaceTab(const char* label, physics_arena::PresenceStatus selected, float width, float height);
void DrawPaneHeading(const char* label, float height = 31);
std::string_view IndexText(const physics_arena::ResultIndexText& text);
ImVec4 ResultEngineColor(std::uint32_t colorRgb);
std::uint32_t AvailableEngineMask(const physics_arena::ResultViewModel& model);
std::uint32_t CountSelectedEngines(std::uint32_t mask);
std::uint32_t SingleSelectedEngine(std::uint32_t mask);
physics_arena::PresenceStatus ActiveAction(const NativeActionState& action);
physics_arena::PresenceStatus RunWorkspaceBusy(const PhysicsArenaApp* app);
physics_arena::ArenaStatus AppendWide(std::array<wchar_t, physics_arena::kRunPathCapacity>* path,
                                      std::wstring_view value);
physics_arena::ArenaStatus AppendUtf8(std::array<wchar_t, physics_arena::kRunPathCapacity>* path,
                                      std::string_view value);
physics_arena::ArenaStatus FindRepositoryRoot(std::array<wchar_t, physics_arena::kRunPathCapacity>* root);
physics_arena::ArenaStatus ResolveResultDirectory(const NativeArenaModel& model, const wchar_t* supplied,
                                                  std::array<wchar_t, physics_arena::kRunPathCapacity>* output);
physics_arena::ArenaStatus BuildRecordDirectory(const NativeArenaModel& model,
                                                const physics_arena::ResultIndexRecord& record,
                                                std::array<wchar_t, physics_arena::kRunPathCapacity>* output);
physics_arena::PresenceStatus SelectResultRecord(PhysicsArenaApp* app, const wchar_t* directory);
void RebuildSummaryRows(PhysicsArenaApp* app);
void SetSummaryEngineSelection(PhysicsArenaApp* app, std::uint32_t selectedMask);
void ResetResultPresentationState(PhysicsArenaApp* app);
void ApplyStorageDeletion(PhysicsArenaApp* app);
void RefreshSelectedReplayStorage(PhysicsArenaApp* app);
NativeUiCommand DrawRecordingDeletionConfirmation(PhysicsArenaApp* app);
physics_arena::ArenaStatus PrepareStorageDeletion(PhysicsArenaApp* app, physics_arena::ReplayStorageScope scope,
                                                  const std::filesystem::path& unfinishedDirectory = {});

physics_arena::ArenaStatus LoadResultDirectory(PhysicsArenaApp* app, const wchar_t* directory,
                                               physics_arena::StatusRecord* error);
physics_arena::ArenaStatus RefreshResultLibrary(PhysicsArenaApp* app, physics_arena::StatusRecord* error);
physics_arena::ArenaStatus ResultLibraryError(physics_arena::StatusRecord* error, const wchar_t* path,
                                              std::string_view reason);
void InitializeResults(PhysicsArenaApp* app);
void DrawStatusError(const physics_arena::StatusRecord& error);
NativeUiCommand DrawRunView(PhysicsArenaApp* app);
NativeUiCommand DrawRunQueue(PhysicsArenaApp* app);
NativeUiCommand DrawRunQueuePreparation(PhysicsArenaApp* app, physics_arena::ArenaStatus ready);
void ApplyNativeRunQueueCommand(PhysicsArenaApp* app, NativeUiCommand command);
void AdvanceNativeRunQueue(PhysicsArenaApp* app);
void DrawRecentRuns(PhysicsArenaApp* app);
NativeUiCommand DrawRunPresetControls(PhysicsArenaApp* app);
void ApplyRunPresetCommand(PhysicsArenaApp* app, NativeUiCommand command);
void DrawEditableRunSettings(PhysicsArenaApp* app);
void DrawRunSummaryFact(const char* label, const char* value);
void DrawRunCaseSummary(const physics_arena::EffectiveRunConfiguration& configuration);
void DrawSavedCaseSummary(PhysicsArenaApp* app);
void DrawCaseInformation(const physics_arena::EffectiveRunConfiguration& configuration);
void DrawSavedCaseExplanation(PhysicsArenaApp* app);
void DrawSavedCaseConfiguration(PhysicsArenaApp* app);
void DrawSavedEngineConfiguration(PhysicsArenaApp* app, std::uint32_t index);
void DrawSavedEngineSettings(const physics_arena::EngineRunSettings& profile, CaseFixtureKind fixture, std::string_view engineId, float scale);
void DrawSavedRunTechnicalDetails(PhysicsArenaApp* app);
void DrawRecordedEngineDetails(PhysicsArenaApp* app, std::uint32_t index);
void DrawSavedRunFacts(PhysicsArenaApp* app, physics_arena::PresenceStatus includeCase = physics_arena::PresenceStatus_Absent);
void DrawRunCaseExplanation(const physics_arena::EffectiveRunConfiguration& configuration);
void SelectAnalysisResult(PhysicsArenaApp* app, std::uint32_t engine, std::uint32_t thread, std::uint32_t repeat);
void DrawResultRepeats(PhysicsArenaApp* app);
void DrawResultTableValue(const char* text);
NativeUiCommand DrawResultInspector(PhysicsArenaApp* app);
NativeUiCommand RequestRecordingPlayback(PhysicsArenaApp* app);
NativeUiCommand DrawRecordingPicker(PhysicsArenaApp* app);
NativeUiCommand DrawRecordings(PhysicsArenaApp* app);
NativeUiCommand DrawResultRecordings(PhysicsArenaApp* app);
int DrawRecordingRow(std::string_view engine, std::uint32_t threads, std::uint32_t repeat,
                     const char* status, const char* savedSize, std::uint64_t bytes,
                     physics_arena::PresenceStatus sizePresence, physics_arena::PresenceStatus selected,
                     physics_arena::PresenceStatus selectable);
NativeUiCommand DrawReplayBrowser(PhysicsArenaApp* app);
int BeginRunPickerTable(physics_arena::PresenceStatus includeCase);
int DrawRunPickerRow(const physics_arena::ResultIndexRecord& record, physics_arena::PresenceStatus selected, physics_arena::PresenceStatus includeCase);
void DrawRunPickerLabel(const physics_arena::ResultIndexRecord& record, ImVec2 origin, float width);
void BeginRecordingChoice(PhysicsArenaApp* app, const std::array<std::uint32_t, 3>& selection);
void PauseReplayView(PhysicsArenaApp* app);
double ReplayNow();
void SeekReplayView(PhysicsArenaApp* app, std::uint64_t ordinal);
void ToggleReplayPlayback(PhysicsArenaApp* app);
void UpdateReplayCameraInput(PhysicsArenaApp* app);
void DrawReplayTransport(PhysicsArenaApp* app);
const char* ReplayOrdinalLabel(const NativeReplayView& replay);
NativeUiCommand DrawActivityDock(PhysicsArenaApp* app);
NativeUiCommand DrawReplayWorkspace(PhysicsArenaApp* app);
void DrawReplayScene(PhysicsArenaApp* app);
void ApplyReplayCamera(PhysicsArenaApp* app);
void DrawResultMetadata(PhysicsArenaApp* app);
void DrawSummaryAnalysis(PhysicsArenaApp* app, NativeSummaryChartMode chartMode);
NativeValueFormat SummaryNumberFormat(const physics_arena::ResultViewModel& model, physics_arena::ResultMetricId metric);
void DrawTimingView(PhysicsArenaApp* app);
void DrawCaseData(PhysicsArenaApp* app);
void RefreshCaseDataDetail(PhysicsArenaApp* app);
NativeUiCommand DrawResultsView(PhysicsArenaApp* app);
physics_arena::ArenaStatus OpenResultArtifact(PhysicsArenaApp* app, physics_arena::PresenceStatus report);
void DrainEvents(PhysicsArenaApp* app);
void CompleteActionUi(PhysicsArenaApp* app, NativeActionPhase current);
physics_arena::ArenaStatus OpenReplayView(PhysicsArenaApp* app, NativeReplayStartup startup);
void CloseReplayView(PhysicsArenaApp* app);
void CloseRayTracingView(PhysicsArenaApp* app);
void CloseRayTracingReplay(PhysicsArenaApp* app);
physics_arena::PresenceStatus RayTracingReplayPresence(const PhysicsArenaApp* app);
const std::array<std::uint32_t, 3>& RayTracingReplaySelection(const PhysicsArenaApp* app);
void DrawRayCaseData(PhysicsArenaApp* app);
void CompleteRayTracingRequest(PhysicsArenaApp* app);
void RequestRayTracingImage(PhysicsArenaApp* app, std::uint32_t view, std::uint32_t phase, std::uint32_t api, std::uint32_t channel);
void InspectorFact(const char* label, std::string_view value);
void DrawRayTracingReplay(PhysicsArenaApp* app);
physics_arena::ArenaStatus SelectRayTracingImage(PhysicsArenaApp* app, std::uint32_t view, std::uint32_t phase, std::uint32_t api, std::uint32_t channel);
void UpdateReplayView(PhysicsArenaApp* app);
void DrawReplayTupleControls(PhysicsArenaApp* app);
int RenderFrame(PhysicsArenaApp* app);
NativeUiCommand DrawPhysicsArenaUi(PhysicsArenaApp* app);
}
