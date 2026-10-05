#include "benchmark_visual/visual_renderer.h"

#include <raylib.h>
#include <rlgl.h>

namespace benchmark_visual
{
enum RenderFrameState
{
	RenderFrameState_Idle = 0,
	RenderFrameState_Drawing = 1,
};

static RenderAntialiasing s_antialiasing = {};
RenderAntialiasing RendererRaylibAntialiasing()
{
	return s_antialiasing;
}

static RenderFrameState s_frameState = RenderFrameState_Idle;

int RendererRaylibCreate(RenderPlatformState state)
{
	if (state.nativeHandle == nullptr || state.width <= 0 || state.height <= 0)
	{
		return RenderViewerStatus_InvalidArgument;
	}
	if (!IsWindowReady() || rlGetVersion() != RL_OPENGL_33)
	{
		return RenderViewerStatus_RendererInitFailed;
	}
	using GetInteger = void(__stdcall*)(unsigned int, int*);
	const auto getInteger = reinterpret_cast<GetInteger>(rlGetProcAddress("glGetIntegerv"));
	s_antialiasing = {};
	if (getInteger != nullptr)
	{
		getInteger(0x80A8, &s_antialiasing.sampleBuffers);
		getInteger(0x80A9, &s_antialiasing.samples);
	}
	TraceLog(LOG_INFO, "RENDER: default framebuffer sample_buffers=%d samples=%d requested=4",
	         s_antialiasing.sampleBuffers, s_antialiasing.samples);
	return ImGuiRaylibCreate(state);
}

int RendererRaylibResize(RenderPlatformState state)
{
	return state.width > 0 && state.height > 0 ? RenderViewerStatus_Ok : RenderViewerStatus_InvalidArgument;
}

void RendererRaylibBeginFrame()
{
	if (s_frameState == RenderFrameState_Drawing)
		return;
	BeginDrawing();
	ClearBackground({32, 40, 51, 255});
	s_frameState = RenderFrameState_Drawing;
}

void RendererRaylibPresent()
{
	// event polling belongs to the platform loop, including frames with no drawing
	rlDrawRenderBatchActive();
	SwapScreenBuffer();
	s_frameState = RenderFrameState_Idle;
}

void RendererRaylibDestroy()
{
	ImGuiRaylibDestroy();
	s_frameState = RenderFrameState_Idle;
}
}
