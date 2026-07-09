#include "benchmark_visual/visual_renderer.h"

#include <bgfx/bgfx.h>

namespace benchmark_visual
{
int RendererBgfxProbe()
{
	return RenderViewerStatus_RouteUnavailable;
}

int RendererBgfxCreate(RenderPlatformState state)
{
	if (state.nativeHandle == nullptr || state.width <= 0 || state.height <= 0)
	{
		return RenderViewerStatus_InvalidArgument;
	}

	bgfx::Init init;
	init.type = bgfx::RendererType::Direct3D11;
	init.platformData.nwh = state.nativeHandle;
	init.resolution.width = static_cast<unsigned int>(state.width);
	init.resolution.height = static_cast<unsigned int>(state.height);
	init.resolution.reset = BGFX_RESET_NONE;

	if (!bgfx::init(init))
	{
		return RenderViewerStatus_RendererInitFailed;
	}

	bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x202833ff, 1.0f, 0);
	bgfx::setViewRect(0, 0, 0, static_cast<unsigned short>(state.width), static_cast<unsigned short>(state.height));
	bgfx::touch(0);
	bgfx::frame();
	int sceneStatus = PhysicsSceneViewCreate();
	if (sceneStatus != RenderViewerStatus_Ok)
	{
		bgfx::shutdown();
		return sceneStatus;
	}

	int imguiStatus = ImGuiBgfxCreate(state);
	if (imguiStatus != RenderViewerStatus_Ok)
	{
		PhysicsSceneViewDestroy();
		bgfx::shutdown();
		return imguiStatus;
	}

	return RenderViewerStatus_Ok;
}

int RendererBgfxFrame(RenderPlatformState state)
{
	if (state.width <= 0 || state.height <= 0)
	{
		return RenderViewerStatus_InvalidArgument;
	}

	bgfx::setViewRect(0, 0, 0, static_cast<unsigned short>(state.width), static_cast<unsigned short>(state.height));
	bgfx::touch(0);
	bgfx::frame();
	return RenderViewerStatus_Ok;
}

int RendererBgfxDrawSnapshot(RenderPlatformState state, VisualSnapshot snapshot)
{
	if (state.width <= 0 || state.height <= 0)
	{
		return RenderViewerStatus_InvalidArgument;
	}

	int drawStatus = PhysicsSceneViewDrawSnapshot(state, snapshot);
	if (drawStatus == RenderViewerStatus_Ok)
	{
		drawStatus = ImGuiBgfxRenderSnapshot(state, snapshot);
	}

	bgfx::frame();
	return drawStatus;
}

void RendererBgfxDestroy()
{
	ImGuiBgfxDestroy();
	PhysicsSceneViewDestroy();
	bgfx::shutdown();
}
}
