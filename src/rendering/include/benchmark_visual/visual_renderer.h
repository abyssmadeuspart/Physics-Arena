#pragma once

#include "benchmark_visual/visual_protocol.h"

namespace benchmark_visual
{
enum RenderViewerStatus
{
	RenderViewerStatus_Ok = 0,
	RenderViewerStatus_InvalidArgument = 2,
	RenderViewerStatus_RouteUnavailable = 3,
	RenderViewerStatus_PlatformInitFailed = 4,
	RenderViewerStatus_WindowCreateFailed = 5,
	RenderViewerStatus_NativeHandleMissing = 6,
	RenderViewerStatus_RendererInitFailed = 7,
	RenderViewerStatus_RendererResourceFailed = 8,
	RenderViewerStatus_TransientBufferUnavailable = 9,
	RenderViewerStatus_WindowCloseRequested = 10,
};

enum RenderWindowEventStatus
{
	RenderWindowEventStatus_Running = 0,
	RenderWindowEventStatus_CloseRequested = 1,
};

struct RenderViewerArgs
{
	int argc;
	char** argv;
};

struct RenderWindowDesc
{
	const char* title;
	int width;
	int height;
};

struct RenderPlatformState
{
	void* window;
	void* nativeHandle;
	int width;
	int height;
	int windowEventStatus;
};

int Run(RenderViewerArgs args);
int PlatformSdl3Create(RenderPlatformState* state, RenderWindowDesc desc);
int PlatformSdl3Poll(RenderPlatformState* state);
void PlatformSdl3Destroy(RenderPlatformState* state);
int RendererBgfxCreate(RenderPlatformState state);
int RendererBgfxFrame(RenderPlatformState state);
int RendererBgfxDrawSnapshot(RenderPlatformState state, VisualSnapshot snapshot);
void RendererBgfxDestroy();
int PhysicsSceneViewCreate();
int PhysicsSceneViewDrawSnapshot(RenderPlatformState state, VisualSnapshot snapshot);
void PhysicsSceneViewDestroy();
int ImGuiBgfxCreate(RenderPlatformState state);
int ImGuiBgfxProcessSdlEvent(const void* sdlEvent);
int ImGuiBgfxRenderSnapshot(RenderPlatformState state, VisualSnapshot snapshot);
void ImGuiBgfxDestroy();
void DrawLiveMetricsUi(VisualSnapshot snapshot);
}
