#pragma once

#include "benchmark_visual/visual_snapshot.h"

#include <string_view>

struct ImFont;

namespace benchmark_visual
{
enum NativeUiFont
{
	NativeUiFont_Body,
	NativeUiFont_Heading,
	NativeUiFont_Data,
	NativeUiFont_DataStrong,
	NativeUiFont_Count,
};

ImFont* NativeUiFontFace(NativeUiFont role);
inline constexpr float kNativeUiBodyLineScale = 2724.0f / 2048.0f;

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
	RenderViewerStatus_WindowResizeFailed = 11,
};

enum RenderWindowEventStatus
{
	RenderWindowEventStatus_Running = 0,
	RenderWindowEventStatus_CloseRequested = 1,
};

enum RenderWindowMode
{
	RenderWindowMode_Normal = 0,
	RenderWindowMode_Maximized = 1,
};

enum RenderWindowResizable
{
	RenderWindowResizable_Disabled = 0,
	RenderWindowResizable_Enabled = 1,
};

enum RenderWindowRestoreStatus
{
	RenderWindowRestoreStatus_Absent = 0,
	RenderWindowRestoreStatus_Ready = 1,
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

struct RenderViewport
{
	int x;
	int y;
	int width;
	int height;
};

struct RenderPlatformState
{
	void* window;
	void* nativeHandle;
	void* wakeEvent;
	void* frameCompletionEvent;
	std::uint64_t frameInputRequired;
	std::uint64_t frameInputObserved;
	unsigned int frameInputEventId;
	int width;
	int height;
	float dpiScale;
	int desktopWidthPixels;
	int desktopHeightPixels;
	int windowEventStatus;
	unsigned int eventSerial;
};

struct VisualCaseStatistics
{
	std::uint32_t dynamicBodyCount;
	std::uint32_t kinematicBodyCount;
	std::uint32_t staticBodyCount;
	std::uint32_t bodyCount;
	std::uint32_t shapeCount;
	std::uint32_t meshTriangleCount;
	std::uint32_t queryCount;
	std::uint32_t constraintCount;
};

struct RenderWindowRestoreRecord
{
	int widthPixels;
	int heightPixels;
	int minimumWidth;
	int minimumHeight;
	RenderWindowMode mode;
	RenderWindowResizable resizable;
	RenderWindowRestoreStatus status;
};

enum ReplaySurfaceMode
{
	ReplaySurfaceMode_Solid = 0,
	ReplaySurfaceMode_Wireframe = 1
};
struct ReplayAppearance
{
	ReplaySurfaceMode surface;
};
struct RenderAntialiasing
{
	int sampleBuffers;
	int samples;
};
RenderAntialiasing RendererRaylibAntialiasing();
struct PhysicsSceneResources;
void PhysicsSceneViewSetAppearance(PhysicsSceneResources* resources, ReplayAppearance appearance);

int PlatformRaylibCreate(RenderPlatformState* state, RenderWindowDesc desc);
int PlatformRaylibPoll(RenderPlatformState* state);
int PlatformRaylibWait(RenderPlatformState* state, int timeoutMilliseconds);
int PlatformRaylibWake(RenderPlatformState* state);
void PlatformRaylibCompleteFrame(RenderPlatformState* state);
int PlatformRaylibRefreshDesktopDisplayMode(RenderPlatformState* state);
int PlatformRaylibBeginFixedDrawable(RenderPlatformState* state, int widthPixels, int heightPixels,
                                     RenderWindowRestoreRecord* restore);
int PlatformRaylibEndFixedDrawable(RenderPlatformState* state, RenderWindowRestoreRecord* restore);
void PlatformRaylibDestroy(RenderPlatformState* state);
int RendererRaylibCreate(RenderPlatformState state);
int RendererRaylibResize(RenderPlatformState state);
void RendererRaylibDestroy();
void RendererRaylibBeginFrame();
void RendererRaylibPresent();
int PhysicsSceneViewInstallScene(const VisualScene* scene, PhysicsSceneResources** resources);
void PhysicsSceneViewReleaseScene(PhysicsSceneResources* resources);
int PhysicsSceneViewDrawSnapshot(PhysicsSceneResources* resources, RenderPlatformState state, RenderViewport viewport, VisualSnapshot snapshot, RenderViewport clip);
int PhysicsSceneViewGetCameraControl(const PhysicsSceneResources* resources, ResolvedVisualCamera* camera, int* overrideActive);
int PhysicsSceneViewSetCameraControl(PhysicsSceneResources* resources, const ResolvedVisualCamera* camera);
void PhysicsSceneViewResetCameraControl(PhysicsSceneResources* resources);
int ImGuiRaylibCreate(RenderPlatformState state);
int ImGuiRaylibBeginFrame(RenderPlatformState state);
int ImGuiRaylibEndFrame();
void ImGuiRaylibDestroy();
}
