#include "benchmark_visual/visual_renderer.h"

#include <imgui.h>
#include <implot.h>
#include <imgui_impl_raylib.h>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <array>
#include <cstdio>
#include <cwchar>

#include <backends/imgui_impl_win32.h>
#include <rlgl.h>

namespace benchmark_visual
{
static ImGuiContext* s_imguiContext = nullptr;
static ImPlotContext* s_implotContext = nullptr;
static std::array<ImFont*, NativeUiFont_Count> s_fonts = {};

ImFont* NativeUiFontFace(NativeUiFont role)
{
	return s_fonts[role];
}

int LoadWindowsUiFonts()
{
	std::array<wchar_t, MAX_PATH> directory = {};
	if (SHGetFolderPathW(nullptr, CSIDL_FONTS, nullptr, SHGFP_TYPE_CURRENT, directory.data()) != S_OK)
	{
		std::fprintf(stderr, "ui_font,failed,Windows Fonts directory unavailable\n");
		return RenderViewerStatus_RendererResourceFailed;
	}
	constexpr std::array<const wchar_t*, NativeUiFont_Count> files =
	    {L"segoeui.ttf", L"seguisb.ttf", L"consola.ttf", L"consolab.ttf"};
	for (std::size_t index = 0; index < files.size(); ++index)
	{
		std::array<wchar_t, MAX_PATH> path = {};
		std::swprintf(path.data(), path.size(), L"%ls\\%ls", directory.data(), files[index]);
		std::array<char, MAX_PATH * 3> utf8 = {};
		if (WideCharToMultiByte(CP_UTF8, 0, path.data(), -1, utf8.data(), static_cast<int>(utf8.size()), nullptr, nullptr) == 0 ||
		    GetFileAttributesW(path.data()) == INVALID_FILE_ATTRIBUTES)
		{
			std::fprintf(stderr, "ui_font,failed,Required font unavailable: %ls\n", path.data());
			return RenderViewerStatus_RendererResourceFailed;
		}
		ImFontConfig font = {};
		// size the line box and glyphs together from the installed font's em metrics
		const float lineScale = index == NativeUiFont_Body || index == NativeUiFont_Heading ? kNativeUiBodyLineScale : 1;
		s_fonts[index] = ImGui::GetIO().Fonts->AddFontFromFileTTF(utf8.data(), 12 * lineScale, &font);
		if (s_fonts[index] == nullptr)
		{
			std::fprintf(stderr, "ui_font,failed,Required font could not be loaded: %s\n", utf8.data());
			return RenderViewerStatus_RendererResourceFailed;
		}
	}
	ImGui::GetIO().FontDefault = s_fonts[NativeUiFont_Body];
	return RenderViewerStatus_Ok;
}

int ImGuiRaylibCreate(RenderPlatformState state)
{
	IMGUI_CHECKVERSION();
	s_imguiContext = ImGui::CreateContext();
	s_implotContext = ImPlot::CreateContext();
	ImGui::StyleColorsDark();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	if (LoadWindowsUiFonts() != RenderViewerStatus_Ok)
	{
		ImPlot::DestroyContext(s_implotContext);
		ImGui::DestroyContext(s_imguiContext);
		s_implotContext = nullptr;
		s_imguiContext = nullptr;
		s_fonts = {};
		return RenderViewerStatus_RendererResourceFailed;
	}
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowPadding = ImVec2(7, 7);
	style.FramePadding = ImVec2(7, 3.5f);
	style.ItemSpacing = ImVec2(7, 4);
	style.ItemInnerSpacing = ImVec2(5, 3);
	style.CellPadding = ImVec2(7, 5.5f);
	style.ScrollbarSize = 9;
	style.WindowRounding = 0;
	style.ChildRounding = 0;
	style.FrameRounding = 2;
	style.PopupRounding = 2;
	style.TabRounding = 0;
	style.WindowBorderSize = 0;
	style.ChildBorderSize = 1;
	style.FrameBorderSize = 1;
	style.Colors[ImGuiCol_WindowBg] = ImColor(0x10, 0x11, 0x13);
	style.Colors[ImGuiCol_ChildBg] = ImColor(0x14, 0x16, 0x19);
	style.Colors[ImGuiCol_PopupBg] = ImColor(0x18, 0x1b, 0x1f);
	style.Colors[ImGuiCol_TitleBg] = ImColor(0x18, 0x1b, 0x1f);
	style.Colors[ImGuiCol_TitleBgActive] = ImColor(0x18, 0x1b, 0x1f);
	style.Colors[ImGuiCol_FrameBg] = ImColor(0x11, 0x13, 0x16);
	style.Colors[ImGuiCol_FrameBgHovered] = ImColor(0x1d, 0x21, 0x26);
	style.Colors[ImGuiCol_FrameBgActive] = ImColor(0x23, 0x28, 0x2f);
	style.Colors[ImGuiCol_Border] = ImColor(0x30, 0x36, 0x40);
	style.Colors[ImGuiCol_Separator] = style.Colors[ImGuiCol_Border];
	style.Colors[ImGuiCol_Text] = ImColor(0xd8, 0xdc, 0xe2);
	style.Colors[ImGuiCol_TextDisabled] = ImColor(0x8e, 0x98, 0xa5);
	style.Colors[ImGuiCol_Button] = ImColor(0x18, 0x1b, 0x1f);
	style.Colors[ImGuiCol_ButtonHovered] = ImColor(0x23, 0x28, 0x2f);
	style.Colors[ImGuiCol_ButtonActive] = ImColor(0x2b, 0x52, 0x7f);
	style.Colors[ImGuiCol_Header] = ImColor(0x1b, 0x2d, 0x43);
	style.Colors[ImGuiCol_HeaderHovered] = ImColor(0x23, 0x38, 0x50);
	style.Colors[ImGuiCol_HeaderActive] = ImColor(0x2b, 0x52, 0x7f);
	style.Colors[ImGuiCol_CheckMark] = ImColor(0x4c, 0x88, 0xd5);
	style.Colors[ImGuiCol_SliderGrab] = style.Colors[ImGuiCol_CheckMark];
	style.Colors[ImGuiCol_TableHeaderBg] = ImColor(0x18, 0x1b, 0x1f);
	style.Colors[ImGuiCol_TableBorderStrong] = style.Colors[ImGuiCol_Border];
	style.Colors[ImGuiCol_TableBorderLight] = ImColor(0x26, 0x2b, 0x32);
	style.FontScaleDpi = state.dpiScale;
	style.ScaleAllSizes(state.dpiScale);
	ImPlotStyle& plotStyle = ImPlot::GetStyle();
	plotStyle.PlotPadding = ImVec2(8 * state.dpiScale, 8 * state.dpiScale);
	plotStyle.LegendPadding = ImVec2(5 * state.dpiScale, 3 * state.dpiScale);
	plotStyle.LegendInnerPadding = ImVec2(4 * state.dpiScale, 3 * state.dpiScale);
	plotStyle.LegendSpacing = ImVec2(7 * state.dpiScale, 2 * state.dpiScale);
	if (!ImGui_ImplRaylib_Init())
	{
		ImPlot::DestroyContext(s_implotContext);
		ImGui::DestroyContext(s_imguiContext);
		s_implotContext = nullptr;
		s_imguiContext = nullptr;
		s_fonts = {};
		return RenderViewerStatus_RendererResourceFailed;
	}
	if (!ImGui_ImplWin32_Init(state.nativeHandle))
	{
		ImGui_ImplRaylib_Shutdown();
		ImPlot::DestroyContext(s_implotContext);
		ImGui::DestroyContext(s_imguiContext);
		s_implotContext = nullptr;
		s_imguiContext = nullptr;
		s_fonts = {};
		return RenderViewerStatus_RendererResourceFailed;
	}
	return RenderViewerStatus_Ok;
}

void FilterFontAtlas(const ImDrawList*, const ImDrawCmd*)
{
	// the backend uploads pending atlas textures before invoking draw callbacks
	for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
	{
		if (texture->Status != ImTextureStatus_OK)
			continue;
		const unsigned int id = static_cast<unsigned int>(texture->GetTexID());
		rlTextureParameters(id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
		rlTextureParameters(id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
	}
}

int ImGuiRaylibBeginFrame(RenderPlatformState state)
{
	RendererRaylibBeginFrame();
	ImGui::SetCurrentContext(s_imguiContext);
	ImPlot::SetCurrentContext(s_implotContext);
	ImGuiStyle& style = ImGui::GetStyle();
	if (style.FontScaleDpi != state.dpiScale)
	{
		style.ScaleAllSizes(state.dpiScale / style.FontScaleDpi);
		style.FontScaleDpi = state.dpiScale;
		ImPlotStyle& plotStyle = ImPlot::GetStyle();
		plotStyle.PlotPadding = ImVec2(8 * state.dpiScale, 8 * state.dpiScale);
		plotStyle.LegendPadding = ImVec2(5 * state.dpiScale, 3 * state.dpiScale);
		plotStyle.LegendInnerPadding = ImVec2(4 * state.dpiScale, 3 * state.dpiScale);
		plotStyle.LegendSpacing = ImVec2(7 * state.dpiScale, 2 * state.dpiScale);
	}
	ImGui_ImplWin32_NewFrame();
	ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(state.width), static_cast<float>(state.height));
	ImGui::NewFrame();
	ImGui::GetBackgroundDrawList()->AddCallback(FilterFontAtlas, nullptr);
	return RenderViewerStatus_Ok;
}

int ImGuiRaylibEndFrame()
{
	ImGui::Render();
	rlMatrixMode(RL_MODELVIEW);
	rlLoadIdentity();
	ImGui_ImplRaylib_RenderDrawData(ImGui::GetDrawData());
	return RenderViewerStatus_Ok;
}

void ImGuiRaylibDestroy()
{
	ImGui::SetCurrentContext(s_imguiContext);
	ImPlot::SetCurrentContext(s_implotContext);
	ImGui_ImplWin32_Shutdown();
	ImGui_ImplRaylib_Shutdown();
	ImPlot::DestroyContext(s_implotContext);
	ImGui::DestroyContext(s_imguiContext);
	s_implotContext = nullptr;
	s_imguiContext = nullptr;
	s_fonts = {};
}
}
