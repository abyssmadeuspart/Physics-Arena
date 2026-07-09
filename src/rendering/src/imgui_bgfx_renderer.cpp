#include "benchmark_visual/visual_renderer.h"

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <imgui.h>
#include <implot.h>
#include <backends/imgui_impl_sdl3.h>

#include <cstdint>
#include <cstring>

#include "vs_ocornut_imgui.bin.h"
#include "fs_ocornut_imgui.bin.h"

namespace benchmark_visual
{
constexpr bgfx::ViewId kImGuiViewId = 1;

static const bgfx::EmbeddedShader kImGuiShaders[] =
{
	BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
	BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
	BGFX_EMBEDDED_SHADER_END()
};

static bgfx::VertexLayout s_imguiLayout;
static bgfx::ProgramHandle s_imguiProgram = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle s_imguiTextureUniform = BGFX_INVALID_HANDLE;
static bgfx::TextureHandle s_imguiFontTexture = BGFX_INVALID_HANDLE;
static ImGuiContext* s_imguiContext = nullptr;
static ImPlotContext* s_implotContext = nullptr;
static int s_imguiReady = 0;

uint16_t ClampUint16(float value)
{
	if (value <= 0.0f)
	{
		return 0;
	}

	if (value >= 65535.0f)
	{
		return 65535;
	}

	return static_cast<uint16_t>(value);
}

void BuildImGuiOrtho(float* result, float left, float right, float bottom, float top, int homogeneousNdc)
{
	for (int index = 0; index < 16; ++index)
	{
		result[index] = 0.0f;
	}

	float nearPlane = 0.0f;
	float farPlane = 1000.0f;
	float width = right - left;
	float height = top - bottom;
	float depth = farPlane - nearPlane;

	result[0] = 2.0f / width;
	result[5] = 2.0f / height;
	result[10] = homogeneousNdc != 0 ? 2.0f / depth : 1.0f / depth;
	result[12] = (left + right) / (left - right);
	result[13] = (top + bottom) / (bottom - top);
	result[14] = homogeneousNdc != 0 ? (nearPlane + farPlane) / (nearPlane - farPlane) : nearPlane / (nearPlane - farPlane);
	result[15] = 1.0f;
}

ImTextureID EncodeTextureId(bgfx::TextureHandle handle)
{
	return static_cast<ImTextureID>(static_cast<uint64_t>(handle.idx) + 1u);
}

bgfx::TextureHandle DecodeTextureId(ImTextureID textureId)
{
	if (textureId == ImTextureID_Invalid)
	{
		return s_imguiFontTexture;
	}

	uint64_t encoded = static_cast<uint64_t>(textureId);
	if (encoded == 0u || encoded > 65536u)
	{
		return s_imguiFontTexture;
	}

	bgfx::TextureHandle handle = { static_cast<uint16_t>(encoded - 1u) };
	return bgfx::isValid(handle) ? handle : s_imguiFontTexture;
}

int CreateFontTexture()
{
	ImGuiIO& io = ImGui::GetIO();
	unsigned char* pixels = nullptr;
	int width = 0;
	int height = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	if (pixels == nullptr || width <= 0 || height <= 0)
	{
		return RenderViewerStatus_RendererResourceFailed;
	}

	const bgfx::Memory* memory = bgfx::copy(pixels, static_cast<uint32_t>(width * height * 4));
	s_imguiFontTexture = bgfx::createTexture2D(
		static_cast<uint16_t>(width),
		static_cast<uint16_t>(height),
		false,
		1,
		bgfx::TextureFormat::RGBA8,
		0,
		memory);

	if (!bgfx::isValid(s_imguiFontTexture))
	{
		return RenderViewerStatus_RendererResourceFailed;
	}

	io.Fonts->SetTexID(EncodeTextureId(s_imguiFontTexture));
	return RenderViewerStatus_Ok;
}

int RenderImGuiDrawData(ImDrawData* drawData)
{
	int displayWidth = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
	int displayHeight = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
	if (displayWidth <= 0 || displayHeight <= 0)
	{
		return RenderViewerStatus_Ok;
	}

	bgfx::setViewMode(kImGuiViewId, bgfx::ViewMode::Sequential);

	float ortho[16];
	BuildImGuiOrtho(
		ortho,
		drawData->DisplayPos.x,
		drawData->DisplayPos.x + drawData->DisplaySize.x,
		drawData->DisplayPos.y + drawData->DisplaySize.y,
		drawData->DisplayPos.y,
		bgfx::getCaps()->homogeneousDepth ? 1 : 0);
	bgfx::setViewTransform(kImGuiViewId, nullptr, ortho);
	bgfx::setViewRect(kImGuiViewId, 0, 0, static_cast<uint16_t>(displayWidth), static_cast<uint16_t>(displayHeight));

	ImVec2 clipPos = drawData->DisplayPos;
	ImVec2 clipScale = drawData->FramebufferScale;
	for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex)
	{
		const ImDrawList* drawList = drawData->CmdLists[listIndex];
		uint32_t vertexCount = static_cast<uint32_t>(drawList->VtxBuffer.Size);
		uint32_t indexCount = static_cast<uint32_t>(drawList->IdxBuffer.Size);
		if (vertexCount == 0 || indexCount == 0)
		{
			continue;
		}

		if (bgfx::getAvailTransientVertexBuffer(vertexCount, s_imguiLayout) < vertexCount ||
			bgfx::getAvailTransientIndexBuffer(indexCount, sizeof(ImDrawIdx) == 4) < indexCount)
		{
			return RenderViewerStatus_TransientBufferUnavailable;
		}

		bgfx::TransientVertexBuffer vertexBuffer;
		bgfx::TransientIndexBuffer indexBuffer;
		bgfx::allocTransientVertexBuffer(&vertexBuffer, vertexCount, s_imguiLayout);
		bgfx::allocTransientIndexBuffer(&indexBuffer, indexCount, sizeof(ImDrawIdx) == 4);
		std::memcpy(vertexBuffer.data, drawList->VtxBuffer.Data, vertexCount * sizeof(ImDrawVert));
		std::memcpy(indexBuffer.data, drawList->IdxBuffer.Data, indexCount * sizeof(ImDrawIdx));

		for (int commandIndex = 0; commandIndex < drawList->CmdBuffer.Size; ++commandIndex)
		{
			const ImDrawCmd* command = &drawList->CmdBuffer[commandIndex];
			if (command->UserCallback != nullptr)
			{
				command->UserCallback(drawList, command);
				continue;
			}

			if (command->ElemCount == 0)
			{
				continue;
			}

			ImVec4 clipRect;
			clipRect.x = (command->ClipRect.x - clipPos.x) * clipScale.x;
			clipRect.y = (command->ClipRect.y - clipPos.y) * clipScale.y;
			clipRect.z = (command->ClipRect.z - clipPos.x) * clipScale.x;
			clipRect.w = (command->ClipRect.w - clipPos.y) * clipScale.y;
			if (clipRect.x >= static_cast<float>(displayWidth) ||
				clipRect.y >= static_cast<float>(displayHeight) ||
				clipRect.z < 0.0f ||
				clipRect.w < 0.0f)
			{
				continue;
			}

			uint16_t scissorX = ClampUint16(clipRect.x);
			uint16_t scissorY = ClampUint16(clipRect.y);
			uint16_t scissorW = ClampUint16(clipRect.z) - scissorX;
			uint16_t scissorH = ClampUint16(clipRect.w) - scissorY;
			bgfx::setScissor(scissorX, scissorY, scissorW, scissorH);
			bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
			bgfx::setTexture(0, s_imguiTextureUniform, DecodeTextureId(command->GetTexID()));
			bgfx::setVertexBuffer(0, &vertexBuffer, command->VtxOffset, vertexCount);
			bgfx::setIndexBuffer(&indexBuffer, command->IdxOffset, command->ElemCount);
			bgfx::submit(kImGuiViewId, s_imguiProgram);
		}
	}

	return RenderViewerStatus_Ok;
}

int ImGuiBgfxCreate(RenderPlatformState state)
{
	if (state.window == nullptr)
	{
		return RenderViewerStatus_InvalidArgument;
	}

	IMGUI_CHECKVERSION();
	s_imguiContext = ImGui::CreateContext();
	if (s_imguiContext == nullptr)
	{
		return RenderViewerStatus_RendererResourceFailed;
	}

	ImGui::SetCurrentContext(s_imguiContext);
	s_implotContext = ImPlot::CreateContext();
	if (s_implotContext == nullptr)
	{
		ImGui::DestroyContext(s_imguiContext);
		s_imguiContext = nullptr;
		return RenderViewerStatus_RendererResourceFailed;
	}

	ImPlot::SetCurrentContext(s_implotContext);
	ImGui::StyleColorsDark();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

	if (!ImGui_ImplSDL3_InitForOther(static_cast<SDL_Window*>(state.window)))
	{
		ImGui::DestroyContext(s_imguiContext);
		s_imguiContext = nullptr;
		return RenderViewerStatus_RendererResourceFailed;
	}

	s_imguiLayout
		.begin()
		.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
		.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
		.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
		.end();

	bgfx::RendererType::Enum type = bgfx::getRendererType();
	s_imguiProgram = bgfx::createProgram(
		bgfx::createEmbeddedShader(kImGuiShaders, type, "vs_ocornut_imgui"),
		bgfx::createEmbeddedShader(kImGuiShaders, type, "fs_ocornut_imgui"),
		true);
	s_imguiTextureUniform = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
	if (!bgfx::isValid(s_imguiProgram) || !bgfx::isValid(s_imguiTextureUniform))
	{
		ImGuiBgfxDestroy();
		return RenderViewerStatus_RendererResourceFailed;
	}

	int fontStatus = CreateFontTexture();
	if (fontStatus != RenderViewerStatus_Ok)
	{
		ImGuiBgfxDestroy();
		return fontStatus;
	}

	s_imguiReady = 1;
	return RenderViewerStatus_Ok;
}

int ImGuiBgfxProcessSdlEvent(const void* sdlEvent)
{
	if (s_imguiReady == 0 || sdlEvent == nullptr)
	{
		return RenderViewerStatus_Ok;
	}

	ImGui::SetCurrentContext(s_imguiContext);
	ImPlot::SetCurrentContext(s_implotContext);
	ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(sdlEvent));
	return RenderViewerStatus_Ok;
}

int ImGuiBgfxRenderSnapshot(RenderPlatformState, VisualSnapshot snapshot)
{
	if (s_imguiReady == 0)
	{
		return RenderViewerStatus_RendererResourceFailed;
	}

	ImGui::SetCurrentContext(s_imguiContext);
	ImPlot::SetCurrentContext(s_implotContext);
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();
	DrawLiveMetricsUi(snapshot);
	ImGui::Render();
	return RenderImGuiDrawData(ImGui::GetDrawData());
}

void ImGuiBgfxDestroy()
{
	if (s_imguiContext != nullptr)
	{
		ImGui::SetCurrentContext(s_imguiContext);
		if (s_implotContext != nullptr)
		{
			ImPlot::SetCurrentContext(s_implotContext);
		}

		ImGui_ImplSDL3_Shutdown();
	}

	if (bgfx::isValid(s_imguiFontTexture))
	{
		bgfx::destroy(s_imguiFontTexture);
		s_imguiFontTexture = BGFX_INVALID_HANDLE;
	}

	if (bgfx::isValid(s_imguiTextureUniform))
	{
		bgfx::destroy(s_imguiTextureUniform);
		s_imguiTextureUniform = BGFX_INVALID_HANDLE;
	}

	if (bgfx::isValid(s_imguiProgram))
	{
		bgfx::destroy(s_imguiProgram);
		s_imguiProgram = BGFX_INVALID_HANDLE;
	}

	if (s_imguiContext != nullptr)
	{
		if (s_implotContext != nullptr)
		{
			ImPlot::DestroyContext(s_implotContext);
			s_implotContext = nullptr;
		}

		ImGui::DestroyContext(s_imguiContext);
		s_imguiContext = nullptr;
	}

	s_imguiReady = 0;
}
}
