#include "launcher_app_internal.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <string>

namespace benchmark_visual
{
using namespace physics_arena;

ArenaStatus RefreshRunPresets(PhysicsArenaApp* app)
{
	std::array<wchar_t, kRunPathCapacity> executable = {};
	const DWORD count = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
	if (count == 0 || count >= executable.size())
		return ResolveNativeRunPresetDirectory({}, &app->presets.directory, &app->uiError);
	if (ResolveNativeRunPresetDirectory(executable.data(), &app->presets.directory, &app->uiError) != ArenaStatus_Ok)
		return app->uiError.code;
	return EnumerateNativeRunPresets(app->presets.directory, &app->presets.names, &app->uiError);
}

std::wstring RunPresetName(const NativeRunPresetUi& presets)
{
	const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, presets.name.data(), -1, nullptr, 0);
	if (count == 0)
		return {};
	std::wstring name(static_cast<std::size_t>(count), L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, presets.name.data(), -1, name.data(), count);
	name.pop_back();
	return name;
}

NativeUiCommand DrawRunPresetControls(PhysicsArenaApp* app)
{
	NativeUiCommand command = NativeUiCommand_None;
	NativeRunPresetUi& presets = app->presets;
	if (ImGui::Button("Recommended"))
		command = NativeUiCommand_RecommendedPreset;
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		ImGui::SetTooltip("Apply all available engines, recommended threads and repeats, authored physics and Release output for this case and shape");
	ImGui::SameLine();
	if (ImGui::Button("Saved presets"))
	{
		RefreshRunPresets(app);
		ImGui::OpenPopup("saved_run_presets");
	}
	if (ImGui::BeginPopup("saved_run_presets"))
	{
		if (presets.names.empty())
			ImGui::TextDisabled("No saved presets");
		for (std::size_t index = 0; index < presets.names.size(); ++index)
		{
			const std::wstring& name = presets.names[index];
			const int count = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, nullptr, 0, nullptr, nullptr);
			std::string label(static_cast<std::size_t>(count), '\0');
			WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, label.data(), count, nullptr, nullptr);
			ImGui::PushID(static_cast<int>(index));
			const ImVec2 position = ImGui::GetCursorScreenPos();
			const float width = ImGui::CalcTextSize(label.c_str(), nullptr, false).x;
			if (ImGui::Selectable("##saved_preset", false, 0, ImVec2(width, 0)))
			{
				presets.selectedName = name;
				command = NativeUiCommand_LoadPreset;
			}
			ImGui::GetWindowDrawList()->AddText(position, ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
			ImGui::PopID();
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Save preset"))
	{
		if (RefreshRunPresets(app) == ArenaStatus_Ok)
		{
			presets.name.fill(0);
			presets.popup = NativeRunPresetPopup_Editing;
			ImGui::OpenPopup("Save preset");
		}
	}
	ImGui::SetNextWindowSize(ImVec2(360 * app->platform.dpiScale, 0), ImGuiCond_Appearing);
	if (ImGui::BeginPopupModal("Save preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Name");
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		ImGui::SetNextItemWidth(-1);
		const int submitted = ImGui::InputText("##preset_name", presets.name.data(), presets.name.size(), ImGuiInputTextFlags_EnterReturnsTrue);
		const std::wstring name = RunPresetName(presets);
		presets.saveMode = NativeRunPresetSaveMode_Create;
		for (const std::wstring& existing : presets.names)
			if (CompareStringOrdinal(name.c_str(), -1, existing.c_str(), -1, TRUE) == CSTR_EQUAL)
				presets.saveMode = NativeRunPresetSaveMode_Replace;
		const char* label = presets.saveMode == NativeRunPresetSaveMode_Replace ? "Replace preset" : "Save preset";
		if (presets.saveMode == NativeRunPresetSaveMode_Replace)
			ImGui::TextWrapped("A saved preset has this name. Replace its configuration or cancel");
		if (app->uiError.code != ArenaStatus_Ok)
			DrawStatusError(app->uiError);
		if (ImGui::Button(label) || submitted != 0)
			command = NativeUiCommand_SavePreset;
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			presets.popup = NativeRunPresetPopup_Closed;
			ImGui::CloseCurrentPopup();
			command = NativeUiCommand_None;
		}
		if (presets.popup == NativeRunPresetPopup_Published)
		{
			presets.popup = NativeRunPresetPopup_Closed;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	return command;
}

void ApplyRunPresetCommand(PhysicsArenaApp* app, NativeUiCommand command)
{
	if (command == NativeUiCommand_RecommendedPreset)
	{
		NativeRunSelection candidate = {};
		if (ComposeRecommendedNativeRunSelection(&app->model, app->model.selection.caseIndex, app->model.selection,
		    &candidate, &app->uiError) == ArenaStatus_Ok)
			app->model.selection = candidate;
	}
	else if (command == NativeUiCommand_LoadPreset)
	{
		std::filesystem::path path;
		if (AdmitNativeRunPresetName(app->presets.directory, app->presets.selectedName, &path, &app->uiError) == ArenaStatus_Ok)
			LoadNativeRunPreset(path.c_str(), &app->model, &app->uiError);
	}
	else
	{
		const std::wstring name = RunPresetName(app->presets);
		const ArenaStatus status = SaveNamedNativeRunPreset(app->presets.directory, name, app->presets.saveMode,
		    &app->model, &app->uiError);
		if (status == ArenaStatus_Ok)
		{
			app->presets.popup = NativeRunPresetPopup_Published;
			RefreshRunPresets(app);
		}
		else
		{
			const StatusRecord failure = app->uiError;
			RefreshRunPresets(app);
			app->uiError = failure;
		}
	}
	app->renderRequested = PresenceStatus_Present;
	app->interactionRenderFrames = 3;
}
}
