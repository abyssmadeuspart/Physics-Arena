#include "launcher_app_internal.h"
#include "physics_arena/run_recommendations.h"

#include <imgui.h>
#include <implot.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <new>
#include <string_view>

namespace benchmark_visual
{
using namespace physics_arena;

std::string_view ErrorDetail(const StatusRecord& error)
{
	return std::string_view(error.detail.data(), error.detailSize);
}

PresenceStatus ActiveAction(const NativeActionState& action)
{
	const NativeActionPhase phase = static_cast<NativeActionPhase>(action.phase.load(std::memory_order_acquire));
	return phase == NativeActionPhase_Executing || phase == NativeActionPhase_Finalizing ||
	               action.threadHandle != nullptr
	           ? PresenceStatus_Present
			   : PresenceStatus_Absent;
}

ArenaStatus FindRepositoryRoot(std::array<wchar_t, kRunPathCapacity>* root)
{
	const DWORD count = GetModuleFileNameW(nullptr, root->data(), static_cast<DWORD>(root->size()));
	if (count == 0 || count >= root->size())
		return ArenaStatus_InvalidResult;
	wchar_t* separator = std::wcsrchr(root->data(), L'\\');
	if (separator == nullptr)
		return ArenaStatus_InvalidResult;
	*separator = L'\0';
	for (std::uint32_t depth = 0; depth < 6; ++depth)
	{
		std::array<wchar_t, kRunPathCapacity> probe = *root;
		if (AppendWide(&probe, L"config\\cases.json") == ArenaStatus_Ok &&
		    GetFileAttributesW(probe.data()) != INVALID_FILE_ATTRIBUTES)
			return ArenaStatus_Ok;
		separator = std::wcsrchr(root->data(), L'\\');
		if (separator == nullptr)
			break;
		*separator = L'\0';
	}
	return ArenaStatus_InvalidResult;
}

void DrawStatusError(const StatusRecord& error)
{
	const std::string_view component(error.component.data(), error.componentSize);
	const std::string_view detail = ErrorDetail(error);
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextColored(ImVec4(0.90f, 0.28f, 0.25f, 1.0f), "%.*s: %.*s", static_cast<int>(component.size()),
	                   component.data(), static_cast<int>(detail.size()), detail.data());
	ImGui::PopTextWrapPos();
}

void DrawCaseControl(PhysicsArenaApp* app)
{
	Catalog& catalog = app->model.catalog;
	const CaseRecord& selectedCase = catalog.cases[app->model.selection.caseIndex];
	std::uint32_t familyIndex = app->model.selection.caseIndex;
	for (std::uint32_t index = 0; index < catalog.caseCount; ++index)
		if (catalog.cases[index].fixtureKind == selectedCase.fixtureKind &&
		    catalog.cases[index].shapePreset == CaseShapePreset_Authored)
			familyIndex = index;
	const std::string_view preview = CatalogTextView(&catalog, catalog.cases[familyIndex].displayName);
	std::array<char, 192> previewText = {};
	std::snprintf(previewText.data(), previewText.size(), "%.*s", static_cast<int>(preview.size()), preview.data());
	const float scale = app->platform.dpiScale;
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("Case");
	ImGui::SameLine();
	ImGui::SetNextItemWidth((std::min)(300.0f, ImGui::GetContentRegionAvail().x / scale * 0.25f) * scale);
	if (ImGui::BeginCombo("##run_case", previewText.data()))
	{
		for (std::uint32_t index = 0; index < catalog.caseCount; ++index)
		{
			if (catalog.cases[index].shapePreset != CaseShapePreset_Authored)
				continue;
			const std::string_view label = CatalogTextView(&catalog, catalog.cases[index].displayName);
			std::array<char, 192> text = {};
			std::snprintf(text.data(), text.size(), "%.*s", static_cast<int>(label.size()), label.data());
			if (ImGui::Selectable(text.data(), index == familyIndex))
			{
				SelectNativeRunFamily(&app->model, catalog.cases[index].fixtureKind, &app->uiError);
				app->ui.runPage = NativeRunPage_Simulation;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("Shape");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120 * scale);
	static constexpr const char* labels[] = {"Authored", "Sphere", "Capsule", "Convex Hull"};
	const CaseRecord& current = catalog.cases[app->model.selection.caseIndex];
	ImGui::BeginDisabled();
	if (ImGui::BeginCombo("##run_shape", labels[current.shapePreset], ImGuiComboFlags_NoArrowButton))
	{
		for (std::uint32_t index = 0; index < catalog.caseCount; ++index)
		{
			const CaseRecord& candidate = catalog.cases[index];
			if (candidate.fixtureKind != current.fixtureKind || candidate.shapePreset != CaseShapePreset_Authored)
				continue;
			if (ImGui::Selectable(labels[candidate.shapePreset], candidate.shapePreset == current.shapePreset))
			{
				SelectNativeRunShape(&app->model, candidate.shapePreset, &app->uiError);
				app->ui.runPage = NativeRunPage_Simulation;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();
}

ArenaStatus DrawEngineControls(PhysicsArenaApp* app)
{
	const ImVec2 heading = ImGui::GetCursorPos();
	std::uint32_t selectedCount = 0;
	for (std::uint32_t index = 0; index < app->model.catalog.engineCount; ++index)
		selectedCount += app->model.selection.engines[index] == PresenceStatus_Present ? 1u : 0u;
	std::array<char, 64> headingLabel = {};
	std::snprintf(headingLabel.data(), headingLabel.size(), "Engine set  %u / %u", selectedCount, app->model.catalog.engineCount);
	DrawPaneHeading(headingLabel.data());
	ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 85 * app->platform.dpiScale, heading.y + 6 * app->platform.dpiScale));
	if (ImGui::SmallButton("All##run_engines"))
	{
		std::array<PresenceStatus, kEngineCapacity> engines = app->model.selection.engines;
		engines.fill(PresenceStatus_Absent);
		for (std::uint32_t index = 0; index < app->model.catalog.engineCount; ++index)
			engines[index] = CaseRouteSupported(&app->model.catalog, app->model.catalog.engines[index],
			                                    app->model.selection.caseIndex) != 0
			                     ? PresenceStatus_Present
			                     : PresenceStatus_Absent;
		if (SelectNativeRunEngines(&app->model, engines, &app->uiError) != ArenaStatus_Ok)
			return app->uiError.code;
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("None##run_engines"))
	{
		std::array<PresenceStatus, kEngineCapacity> engines = app->model.selection.engines;
		engines.fill(PresenceStatus_Absent);
		if (SelectNativeRunEngines(&app->model, engines, &app->uiError) != ArenaStatus_Ok)
			return app->uiError.code;
	}
	ImGui::SetCursorPos(ImVec2(heading.x, heading.y + 32 * app->platform.dpiScale));
	const int columnCount = ImGui::GetContentRegionAvail().x / app->platform.dpiScale >= 400 ? 2 : 1;
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(7 * app->platform.dpiScale, 6.5f * app->platform.dpiScale));
	if (ImGui::BeginTable("run_engine_choices", columnCount, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX))
	{
		for (std::uint32_t ordinal = 0; ordinal < app->model.catalog.engineCount; ++ordinal)
		{
			const std::uint32_t index = app->model.engineDisplayOrder[ordinal];
			if (ordinal % columnCount == 0)
				ImGui::TableNextRow(0, 27 * app->platform.dpiScale);
			ImGui::TableNextColumn();
			if (app->model.selection.engines[index] == PresenceStatus_Present)
				ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(27, 45, 67, 255));
			const EngineRecord& engine = app->model.catalog.engines[index];
			const std::string_view engineId = CatalogTextView(&app->model.catalog, engine.id);
			const std::string_view name = CatalogTextView(&app->model.catalog, engine.displayName);
			const PresenceStatus available = app->model.releaseCatalog.engineArtifactAvailability[index];
			const int supported = CaseRouteSupported(&app->model.catalog, engine, app->model.selection.caseIndex);
			std::string_view reportVersion;
			if (available == PresenceStatus_Present)
			{
				const std::uint32_t artifactIndex = app->model.releaseCatalog.engineArtifactIndexes[index];
				reportVersion = ReleaseTextView(&app->model.releaseCatalog,
				                                app->model.releaseCatalog.artifacts[artifactIndex].reportVersion);
			}
			EngineProvenanceLabelProjection label = {};
			StatusRecord labelError = {};
			if (ProjectEngineProvenanceLabel(engineId, name, reportVersion, &label, &labelError) != ArenaStatus_Ok)
			{
				app->uiError = labelError;
				ImGui::EndTable();
				ImGui::PopStyleVar();
				return app->uiError.code;
			}
			if (supported == 0)
				ImGui::BeginDisabled();
			unsigned int selected = app->model.selection.engines[index] == PresenceStatus_Present ? 1U : 0U;
			ImGui::PushID(static_cast<int>(index));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(app->platform.dpiScale, app->platform.dpiScale));
			const int toggled = ImGui::CheckboxFlags("##engine", &selected, 1U);
			ImGui::PopStyleVar();
			if (toggled != 0)
			{
				std::array<PresenceStatus, kEngineCapacity> engines = app->model.selection.engines;
				engines[index] = selected != 0 ? PresenceStatus_Present : PresenceStatus_Absent;
				if (SelectNativeRunEngines(&app->model, engines, &app->uiError) != ArenaStatus_Ok)
				{
					ImGui::PopID();
					if (supported == 0)
						ImGui::EndDisabled();
					ImGui::EndTable();
					ImGui::PopStyleVar();
					return app->uiError.code;
				}
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(name.data(), name.data() + name.size());
			if (!reportVersion.empty())
			{
				ImGui::SameLine(0, 3 * app->platform.dpiScale);
				ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 10);
				ImGui::TextDisabled("%.*s", static_cast<int>(reportVersion.size()), reportVersion.data());
				ImGui::PopFont();
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
				ImGui::SetTooltip("%s", label.text.data());
			ImGui::PopID();
			if (supported == 0)
				ImGui::EndDisabled();
		}
		ImGui::EndTable();
	}
	ImGui::PopStyleVar();
	return ArenaStatus_Ok;
}

void DrawRepeatControl(PhysicsArenaApp* app)
{
	const float scale = app->platform.dpiScale;
	std::uint32_t& count = app->model.selection.repeatCount;
	const ImVec2 origin = ImGui::GetCursorPos();
	const float right = origin.x + ImGui::GetContentRegionAvail().x;
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Repeats");
	ImGui::SameLine(right - 104 * scale);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
	ImGui::BeginDisabled(count <= 1);
	if (ImGui::Button("-##repeat", ImVec2(25 * scale, 25 * scale)))
		--count;
	ImGui::EndDisabled();
	ImGui::SameLine(0, 0);
	ImGui::SetNextItemWidth(54 * scale);
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 11);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4 * scale, (25 * scale - ImGui::GetFontSize()) * 0.5f));
	ImGui::InputScalar("##run_repeats", ImGuiDataType_U32, &count);
	if (!ImGui::IsItemActive())
	{
		const ImVec2 start = ImGui::GetItemRectMin();
		const ImVec2 end = ImGui::GetItemRectMax();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(ImVec2(start.x + scale, start.y + scale), ImVec2(end.x - scale, end.y - scale),
		    ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg));
		std::array<char, 16> text = {};
		std::snprintf(text.data(), text.size(), "%u", count);
		const ImVec2 size = ImGui::CalcTextSize(text.data());
		draw->AddText(ImVec2(start.x + (end.x - start.x - size.x) * 0.5f, start.y + (end.y - start.y - size.y) * 0.5f),
		    ImGui::GetColorU32(ImGuiCol_Text), text.data());
	}
	ImGui::PopStyleVar();
	ImGui::PopFont();
	ImGui::SameLine(0, 0);
	ImGui::BeginDisabled(count >= kRunRepeatCapacity);
	if (ImGui::Button("+##repeat", ImVec2(25 * scale, 25 * scale)))
		++count;
	ImGui::EndDisabled();
	ImGui::PopStyleVar();
}

ArenaStatus DrawRunSettings(PhysicsArenaApp* app)
{
	NativeRunSelection& selection = app->model.selection;
	DrawPaneHeading("Execution matrix", 28);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7 * app->platform.dpiScale, 5 * app->platform.dpiScale));
	ImGui::BeginChild("matrix_controls", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
	ImGui::PopStyleVar();

	if (ImGui::SmallButton("Recommended##run_threads") &&
	    ApplyRecommendedNativeRunThreads(&app->model, &app->uiError) != ArenaStatus_Ok)
	{
		ImGui::EndChild();
		return app->uiError.code;
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("All##run_threads"))
	{
		selection.threads.fill(PresenceStatus_Absent);
		for (std::uint32_t count = 1; count <= app->model.host.logicalThreadCount; ++count)
			selection.threads[count - 1] = PresenceStatus_Present;
		if (ReconcileNativeRunThreads(&app->model, &app->uiError) != ArenaStatus_Ok)
		{
			ImGui::EndChild();
			return app->uiError.code;
		}
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("None##run_threads"))
	{
		selection.threads.fill(PresenceStatus_Absent);
		selection.threadMode = ThreadSelectionMode_Explicit;
		ReconcileNativeRecordingThreads(&selection);
		app->uiError = {};
	}
	const int columnCount = 8;
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(1.5f * app->platform.dpiScale, 1.5f * app->platform.dpiScale));
	if (ImGui::BeginTable("run_thread_counts", columnCount, ImGuiTableFlags_SizingStretchSame))
	{
		for (std::uint32_t count = 1; count <= app->model.host.logicalThreadCount; ++count)
		{
			ImGui::TableNextColumn();
			std::array<char, 32> label = {};
			std::snprintf(label.data(), label.size(), "%u", count);
			PresenceStatus available = PresenceStatus_Absent;
			StatusRecord availabilityError = {};
			if (ResolveNativeRunThreadAvailability(&app->model, count, &available, &availabilityError) !=
			    ArenaStatus_Ok)
			{
				app->uiError = availabilityError;
				ImGui::EndTable();
				ImGui::PopStyleVar();
				ImGui::EndChild();
			return app->uiError.code;
			}
			unsigned int selected = selection.threads[count - 1] == PresenceStatus_Present ? 1U : 0U;
			if (available != PresenceStatus_Present)
				ImGui::BeginDisabled();
			ImGui::PushID(static_cast<int>(count));
			ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 10);
			if (selected != 0)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
				ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
			}
			const int pressed = ImGui::Button(label.data(), ImVec2(-1, 25 * app->platform.dpiScale));
			if (selected != 0)
				ImGui::PopStyleColor(2);
			ImGui::PopFont();
			if (pressed != 0)
			{
				selection.threads[count - 1] = selected == 0 ? PresenceStatus_Present : PresenceStatus_Absent;
				selection.threadMode = ThreadSelectionMode_Explicit;
				ReconcileNativeRecordingThreads(&selection);
				app->uiError = {};
			}
			ImGui::PopID();
			if (available != PresenceStatus_Present)
				ImGui::EndDisabled();
		}
		ImGui::EndTable();
	}

	ImGui::PopStyleVar();
	DrawRepeatControl(app);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(app->platform.dpiScale, app->platform.dpiScale));
	unsigned int record = app->model.selection.recordingMode == RecordingMode_On ? 1u : 0u;
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Save replay");
	ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetFrameHeight() - 7 * app->platform.dpiScale);
	if (ImGui::CheckboxFlags("##record_replay", &record, 1u))
	{
		if (record != 0)
			EnableNativeRecording(&selection);
		else
			selection.recordingMode = RecordingMode_Off;
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		ImGui::SetTooltip("Save every completed work unit. Disk output is outside native step timing");
	ImGui::PopStyleVar();
	RecordingThreadSet capture = {};
	for (std::uint32_t slot = 0; slot < selection.recordingThreads.size(); ++slot)
		if (selection.recordingThreads[slot] == PresenceStatus_Present)
			capture.counts[capture.count++] = slot + 1;
	const NativeValueText chosen = FormatNativeThreadSet(capture.counts.data(), capture.count);
	ImGui::TextUnformatted("Replay threads");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1);
	ImGui::BeginDisabled(selection.recordingMode == RecordingMode_Off);
	if (ImGui::BeginCombo("##replay_threads", capture.count == 0 ? "Select threads" : chosen.data()))
	{
		for (std::uint32_t slot = 0; slot < selection.threads.size(); ++slot)
		{
			if (selection.threads[slot] != PresenceStatus_Present)
				continue;
			std::array<char, 32> label = {};
			std::snprintf(label.data(), label.size(), "%u", slot + 1);
			unsigned int selected = selection.recordingThreads[slot] == PresenceStatus_Present ? 1u : 0u;
			if (ImGui::CheckboxFlags(label.data(), &selected, 1u))
				selection.recordingThreads[slot] = selected != 0 ? PresenceStatus_Present : PresenceStatus_Absent;
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();
	ImGui::TextUnformatted("Verify physics");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1);
	int verification = selection.verificationMode == VerificationMode_On ? 0 : 1;
	if (ImGui::Combo("##verify_physics", &verification, "On\0Off\0"))
		selection.verificationMode = verification == 0 ? VerificationMode_On : VerificationMode_Off;
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		ImGui::SetTooltip("Off skips physical-quality capture and assessment. Simulation, measurements and replay remain selected");
	ImGui::EndChild();
	return ArenaStatus_Ok;
}

NativeUiCommand DrawRunSummary(PhysicsArenaApp* app, const PreparedRunRequest& prepared, ArenaStatus ready,
                               const StatusRecord& error)
{
	const float scale = app->platform.dpiScale;
	EffectiveRunConfiguration current = {};
	StatusRecord configurationError = {};
	const EffectiveRunConfiguration* configuration = &prepared.configuration;
	if (ready != ArenaStatus_Ok)
		configuration = ComposeRunSettings(&app->model.catalog, app->model.selection.caseIndex,
		    &app->model.selection.settings, {}, &current, &configurationError) == ArenaStatus_Ok ? &current : nullptr;
	if (configuration != nullptr)
		DrawRunCaseSummary(*configuration);
	if (ready == ArenaStatus_Ok)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(7 * scale, 4 * scale));
		if (ImGui::BeginTable("execution_fact_strip", 7, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX))
		{
			DrawRunSummaryFact("Engines", FormatNativeCount(prepared.engineCount).data());
			DrawRunSummaryFact("Threads", FormatNativeThreadSet(prepared.threadCounts.data(), prepared.threadCount).data());
			DrawRunSummaryFact("Repeats", FormatNativeCount(prepared.repeatCount).data());
			DrawRunSummaryFact("Processes", FormatNativeCount(prepared.totalProcessCount).data());
			if (prepared.configuration.execution.timestepHz != 0)
			{
				NativeValueText duration = {};
				std::snprintf(duration.data(), duration.size(), "%.3f s",
				    static_cast<double>(prepared.configuration.execution.measuredWorkUnitCount) / prepared.configuration.execution.timestepHz);
				DrawRunSummaryFact("Simulation", duration.data());
			}
			else
				DrawRunSummaryFact("Simulation", "Not applicable");
			DrawRunSummaryFact("Save replay", prepared.recordingMode == RecordingMode_On ? "On" : "Off");
			DrawRunSummaryFact("Verify physics", prepared.verificationMode == VerificationMode_On ? "On" : "Off");
			ImGui::EndTable();
		}
		if (prepared.recordingMode == RecordingMode_On)
		{
			const ReplaySpaceProjection storage = ProjectReplaySpace(prepared);
			if (ImGui::BeginTable("recording_fact_strip", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX))
			{
				DrawRunSummaryFact("Replay threads", FormatNativeThreadSet(prepared.recordingThreads.counts.data(), prepared.recordingThreads.count).data());
				DrawRunSummaryFact("Recordings", FormatNativeCount(storage.tupleCount).data());
				DrawRunSummaryFact("Maximum working storage", FormatNativeBytes(storage.requiredBytes).data());
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
					ImGui::SetTooltip("%s bytes maximum, including temporary files. Saved size depends on compression. Every selected engine and repeat is captured for these threads",
					    FormatNativeRawCount(storage.requiredBytes).data());
				ImGui::EndTable();
			}
		}
		ImGui::PopStyleVar();
	}
	NativeUiCommand command = NativeUiCommand_None;
	const std::uint32_t pending = PendingNativeRunQueueCount(app->queue);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Draft output");
	ImGui::SameLine();
	if (ImGui::RadioButton("Test", app->model.selection.storage == ResultStorage_Local))
		app->model.selection.storage = ResultStorage_Local;
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		ImGui::SetTooltip("Save the draft to results/local (ignored by Git)");
	ImGui::SameLine();
	if (ImGui::RadioButton("Release", app->model.selection.storage == ResultStorage_Repository))
		app->model.selection.storage = ResultStorage_Repository;
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		ImGui::SetTooltip("Save the draft to results");
	if (RunWorkspaceBusy(app) == PresenceStatus_Present)
	{
		if (ImGui::Button("Cancel run and stop queue", ImVec2(0, 30 * scale)))
			command = NativeUiCommand_Cancel;
		if (ActiveAction(app->action) == PresenceStatus_Present)
		{
			ImGui::Text("Passed %u | completed unverified %u | quality failed %u",
			    app->action.control.passedRepeatCount.load(std::memory_order_relaxed),
			    app->action.control.unverifiedRepeatCount.load(std::memory_order_relaxed),
			    app->action.control.qualityFailedRepeatCount.load(std::memory_order_relaxed));
			ImGui::Text("Execution failed %u | skipped %u",
			    app->action.control.executionFailedRepeatCount.load(std::memory_order_relaxed),
			    app->action.control.skippedRepeatCount.load(std::memory_order_relaxed));
		}
	}
	else
	{
		std::array<char, 64> label = {};
		if (pending == 0)
			std::snprintf(label.data(), label.size(), "Start benchmark");
		else
			std::snprintf(label.data(), label.size(), app->queue.state == NativeRunQueueState_Stopped
			    ? "Start remaining (%u)" : "Start queue (%u)", pending);
		ImGui::BeginDisabled(app->queue.editingId != 0 || (pending == 0 && ready != ArenaStatus_Ok));
		if (ImGui::Button(label.data(), ImVec2(170 * scale, 30 * scale)))
			command = NativeUiCommand_Start;
		ImGui::EndDisabled();
	}
	if (pending != 0)
	{
		std::uint32_t pendingTest = 0;
		for (const NativeRunQueueEntry& entry : app->queue.entries)
			if (entry.state == NativeRunQueueEntryState_Pending && entry.selection.storage == ResultStorage_Local)
				pendingTest += 1;
		std::array<char, 96> outputs = {};
		std::snprintf(outputs.data(), outputs.size(), "Pending output: %u Test | %u Release", pendingTest, pending - pendingTest);
		if (ImGui::GetContentRegionAvail().x >= 170 * scale + ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize(outputs.data()).x)
			ImGui::SameLine();
		ImGui::TextUnformatted(outputs.data());
	}
	const NativeUiCommand preparation = DrawRunQueuePreparation(app, ready);
	if (preparation != NativeUiCommand_None)
		command = preparation;
	if (ready != ArenaStatus_Ok)
		DrawStatusError(error);
	return command;
}

void DrawRunConfigurationPane(PhysicsArenaApp* app)
{
	const std::array<const char*, 3> pages = {"Simulation", "Fixture", "About"};
	for (std::uint32_t index = 0; index < pages.size(); ++index)
	{
		if (index != 0)
			ImGui::SameLine(0, 0);
		if (DrawWorkspaceTab(pages[index], app->ui.runPage == static_cast<NativeRunPage>(index) ?
		    PresenceStatus_Present : PresenceStatus_Absent, 90, 31))
			app->ui.runPage = static_cast<NativeRunPage>(index);
	}
	ImGui::PushID(static_cast<int>(app->ui.runPage));
	ImGui::BeginChild("configuration_properties",
	    ImVec2(0, (std::max)(40 * app->platform.dpiScale, ImGui::GetContentRegionAvail().y)),
	    ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGuiStorage* scroll = ImGui::GetStateStorage();
	const ImGuiID heightId = ImGui::GetID("previous_viewport_height");
	const ImGuiID bottomId = ImGui::GetID("previous_bottom_anchor");
	const float previousHeight = scroll->GetFloat(heightId);
	const float height = ImGui::GetWindowHeight();
	const int keepBottom = previousHeight > 0 && previousHeight != height && scroll->GetInt(bottomId) != 0;
	if (app->ui.runPage == NativeRunPage_About)
	{
		EffectiveRunConfiguration configuration = {};
		StatusRecord error = {};
		if (ComposeRunSettings(&app->model.catalog, app->model.selection.caseIndex, &app->model.selection.settings,
		    {}, &configuration, &error) == ArenaStatus_Ok)
			DrawRunCaseExplanation(configuration);
		else
			DrawStatusError(error);
	}
	else
	{
		DrawEditableRunSettings(app);
	}
	if (keepBottom != 0)
		ImGui::SetScrollHereY(1);
	scroll->SetFloat(heightId, height);
	scroll->SetInt(bottomId, keepBottom != 0 || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1 ? 1 : 0);
	ImGui::EndChild();
	ImGui::PopID();
}

void DrawRecentRuns(PhysicsArenaApp* app)
{
	const float scale = app->platform.dpiScale;
	const ResultIndexWorkspace& indexes = app->workspace.finalization.indexes;
	const PresenceStatus busy = RunWorkspaceBusy(app);
	const std::uint32_t visibleRows = busy == PresenceStatus_Present ? 1u :
	    (std::max)(1u, (std::min)(indexes.recordCount, 5u));
	ImGui::BeginChild("recent_runs", ImVec2(0, (visibleRows * 42 + 14) * scale),
	    ImGuiChildFlags_AlwaysUseWindowPadding);
	if (busy == PresenceStatus_Present)
	{
		ImGui::TextWrapped("Recent runs are available after the current action finishes");
		ImGui::EndChild();
		return;
	}
	std::array<std::uint32_t, 5> recent = {};
	recent.fill(indexes.recordCount);
	for (std::uint32_t index = 0; index < indexes.recordCount; ++index)
	{
		for (std::size_t slot = 0; slot < recent.size(); ++slot)
		{
			if (recent[slot] != indexes.recordCount && IndexText(indexes.records[recent[slot]].runSlug) >= IndexText(indexes.records[index].runSlug))
				continue;
			for (std::size_t tail = recent.size() - 1; tail > slot; --tail)
				recent[tail] = recent[tail - 1];
			recent[slot] = index;
			break;
		}
	}
	for (std::uint32_t ordinal = 0; ordinal < (std::min)(indexes.recordCount, 5u); ++ordinal)
	{
		const ResultIndexRecord& record = indexes.records[recent[ordinal]];
		NativeRunLabelProjection label = {};
		ProjectNativeRunLabel(IndexText(record.runSlug), record.threadCount, record.repeatCount, &label);
		ImGui::PushID(static_cast<int>(ordinal));
		const ImVec2 row = ImGui::GetCursorScreenPos();
		if (ImGui::Selectable("##recent_run", false, 0, ImVec2(0, 38 * scale)))
		{
			std::array<wchar_t, kRunPathCapacity> path = {};
			if (BuildRecordDirectory(app->model, record, &path) == ArenaStatus_Ok && LoadResultDirectory(app, path.data(), &app->results.error) == ArenaStatus_Ok)
				app->model.view = NativeArenaView_Results;
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			ImGui::SetTooltip("%s\n%s", record.caseName.data.data(), label.text.data());
		ImGui::GetWindowDrawList()->AddText(row, ImGui::GetColorU32(ImGuiCol_Text), record.caseName.data.data());
		ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 10);
		ImGui::GetWindowDrawList()->AddText(ImVec2(row.x, row.y + 17 * scale), ImGui::GetColorU32(ImGuiCol_TextDisabled), label.text.data());
		ImGui::PopFont();
		ImGui::PopID();
	}
	if (indexes.recordCount == 0)
		ImGui::TextDisabled("No completed runs");
	ImGui::EndChild();
}

NativeUiCommand DrawRunView(PhysicsArenaApp* app)
{
	NativeUiCommand command = NativeUiCommand_None;
	const float scale = app->platform.dpiScale;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7 * scale, 5 * scale));
	const int wrapToolbar = ImGui::GetContentRegionAvail().x / scale < 1000;
	ImGui::BeginChild("run_toolbar", ImVec2(0, (kNativeToolbarHeight + (wrapToolbar != 0 ? 30 : 0)) * scale), ImGuiChildFlags_AlwaysUseWindowPadding,
	    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	DrawCaseControl(app);
	ImGui::SameLine();
	if (ImGui::Button("Reset physics"))
		ResetRunSettings(&app->model.catalog, app->model.selection.caseIndex, &app->model.selection.settings, &app->uiError);
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		ImGui::SetTooltip("Restore authored physics, solver and initial-camera settings. Keep engines, threads, repeats and output");
	if (wrapToolbar == 0)
		ImGui::SameLine();
	command = DrawRunPresetControls(app);
	ImGui::EndChild();
	ImGui::PopStyleVar();
	PreparedRunRequest prepared = {};
	StatusRecord error = {};
	const ArenaStatus ready = PrepareNativeRunRequest(&app->model, &prepared, &error);
	const float width = ImGui::GetContentRegionAvail().x;
	const int compact = width / scale < 1100 || app->platform.height / scale <= 600;
	const float setupWidth = (width / scale > 1120 ? 264 : 240) * scale;
	const float queueWidth = 270 * scale;
	if (compact != 0)
	{
		const NativeUiCommand action = DrawRunSummary(app, prepared, ready, error);
		if (action != NativeUiCommand_None)
			command = action;
		constexpr std::array<const char*, 3> pages = {"Setup", "Configuration", "Queue"};
		for (std::uint32_t index = 0; index < pages.size(); ++index)
		{
			if (index != 0)
				ImGui::SameLine(0, 0);
			if (DrawWorkspaceTab(pages[index], app->ui.compactRunPage == static_cast<NativeCompactRunPage>(index)
			    ? PresenceStatus_Present : PresenceStatus_Absent, index == 1 ? 110 : 82, 28))
				app->ui.compactRunPage = static_cast<NativeCompactRunPage>(index);
		}
	}
	if (compact == 0 || app->ui.compactRunPage == NativeCompactRunPage_Setup)
	{
		ImGui::BeginChild("run_setup", ImVec2(compact != 0 ? 0 : setupWidth, 0), ImGuiChildFlags_Borders);
		DrawEngineControls(app);
		DrawRunSettings(app);
		ImGui::EndChild();
		if (compact == 0)
			ImGui::SameLine(0, 0);
	}
	if (compact == 0 || app->ui.compactRunPage == NativeCompactRunPage_Configuration)
	{
		ImGui::BeginChild("run_configuration", ImVec2(compact != 0 ? 0 : width - setupWidth - queueWidth, 0),
		    ImGuiChildFlags_Borders);
		if (compact == 0)
		{
			const NativeUiCommand action = DrawRunSummary(app, prepared, ready, error);
			if (action != NativeUiCommand_None)
				command = action;
		}
		DrawRunConfigurationPane(app);
		ImGui::EndChild();
	}
	if (compact == 0 || app->ui.compactRunPage == NativeCompactRunPage_Queue)
	{
		if (compact == 0)
			ImGui::SameLine(0, 0);
		ImGui::BeginChild("run_history", ImVec2(compact != 0 ? 0 : queueWidth, 0), ImGuiChildFlags_Borders);
		const NativeUiCommand queueCommand = DrawRunQueue(app);
		if (queueCommand != NativeUiCommand_None)
			command = queueCommand;
		ImGui::EndChild();
	}
	return command;
}
} // namespace benchmark_visual
