#include "benchmark_visual/visual_renderer.h"
#include "benchmark_visual/gui_frame_sync.h"
#include <cwchar>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#define LoadImage Win32LoadImage
#define DrawText Win32DrawText
#include <windows.h>
#include <commctrl.h>
#undef CloseWindow
#undef ShowCursor
#undef LoadImage
#undef DrawText
#undef DrawTextEx

#include <raylib.h>
#include <backends/imgui_impl_win32.h>
#include <imgui.h>
#include <imgui_internal.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace benchmark_visual
{
LRESULT CALLBACK RenderWindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                                       DWORD_PTR reference)
{
	const ULONG_PTR inputIdentity = static_cast<ULONG_PTR>(GetMessageExtraInfo());
	ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);
	RenderPlatformState* state = reinterpret_cast<RenderPlatformState*>(reference);
	if (message == kGuiFrameRequestMessage)
	{
		wchar_t name[128] = {};
		std::swprintf(name, 128, kGuiFrameEventName, GetCurrentProcessId(), static_cast<DWORD>(wparam),
		              static_cast<DWORD>(lparam));
		if (state->frameCompletionEvent == nullptr)
			state->frameCompletionEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
		state->frameInputRequired = (static_cast<std::uint64_t>(wparam) >> 32) != 0
		                               ? kGuiFrameInputTag | (static_cast<DWORD>(lparam) & 0xffff)
		                               : 0;
		++state->eventSerial;
		return 0;
	}
	switch (message)
	{
	case WM_CLOSE:
		state->windowEventStatus = RenderWindowEventStatus_CloseRequested;
		++state->eventSerial;
		return 0;
	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	case WM_CHAR:
	case WM_MOUSEMOVE:
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONDBLCLK:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONDBLCLK:
	case WM_MBUTTONUP:
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
		if ((inputIdentity & kGuiFrameInputMask) == kGuiFrameInputTag && inputIdentity > state->frameInputObserved)
		{
			state->frameInputObserved = inputIdentity;
			const ImGuiContext* context = ImGui::GetCurrentContext();
			state->frameInputEventId = context != nullptr ? context->InputEventsNextEventId - 1 : 0;
		}
		++state->eventSerial;
		break;
	case WM_SIZE:
	case WM_MOVE:
	case WM_DPICHANGED:
	case WM_SETFOCUS:
	case WM_KILLFOCUS:
	case WM_PAINT:
		++state->eventSerial;
		break;
	}
	return DefSubclassProc(window, message, wparam, lparam);
}

void PlatformRaylibCompleteFrame(RenderPlatformState* state)
{
	if (state->frameCompletionEvent != nullptr &&
	    (state->frameInputRequired == 0 || state->frameInputObserved == state->frameInputRequired))
	{
		SetEvent(static_cast<HANDLE>(state->frameCompletionEvent));
		CloseHandle(static_cast<HANDLE>(state->frameCompletionEvent));
		state->frameCompletionEvent = nullptr;
	}
}

int PlatformRaylibRefreshDesktopDisplayMode(RenderPlatformState* state)
{
	const int monitor = GetCurrentMonitor();
	state->desktopWidthPixels = GetMonitorWidth(monitor);
	state->desktopHeightPixels = GetMonitorHeight(monitor);
	return state->desktopWidthPixels > 0 && state->desktopHeightPixels > 0 ? RenderViewerStatus_Ok
	                                                                       : RenderViewerStatus_PlatformInitFailed;
}

int PlatformRaylibCreate(RenderPlatformState* state, RenderWindowDesc desc)
{
	if (state == nullptr || desc.title == nullptr || desc.width <= 0 || desc.height <= 0)
	{
		return RenderViewerStatus_InvalidArgument;
	}
	*state = {};
	ImGui_ImplWin32_EnableDpiAwareness();
	SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_ALWAYS_RUN | FLAG_MSAA_4X_HINT);
	InitWindow(desc.width, desc.height, desc.title);
	if (!IsWindowReady())
		return RenderViewerStatus_WindowCreateFailed;
	SetExitKey(KEY_NULL);
	SetWindowMinSize(760, 520);
	state->nativeHandle = GetWindowHandle();
	state->window = state->nativeHandle;
	state->wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (state->nativeHandle == nullptr || state->wakeEvent == nullptr ||
	    SetWindowSubclass(static_cast<HWND>(state->nativeHandle), RenderWindowProcedure, 1,
	                      reinterpret_cast<DWORD_PTR>(state)) == FALSE)
	{
		PlatformRaylibDestroy(state);
		return RenderViewerStatus_NativeHandleMissing;
	}
	const HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
	if (icon != nullptr)
	{
		SendMessageW(static_cast<HWND>(state->nativeHandle), WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
		SendMessageW(static_cast<HWND>(state->nativeHandle), WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
	}
	return PlatformRaylibPoll(state);
}

int PlatformRaylibPoll(RenderPlatformState* state)
{
	PollInputEvents();
	state->dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(state->nativeHandle);
	state->width = GetRenderWidth();
	state->height = GetRenderHeight();
	return PlatformRaylibRefreshDesktopDisplayMode(state);
}

int PlatformRaylibWait(RenderPlatformState* state, int timeoutMilliseconds)
{
	if (timeoutMilliseconds < 0)
		return RenderViewerStatus_InvalidArgument;
	const HANDLE wakeEvent = static_cast<HANDLE>(state->wakeEvent);
	const DWORD waitStatus = MsgWaitForMultipleObjectsEx(1, &wakeEvent, static_cast<DWORD>(timeoutMilliseconds),
	                                                     QS_ALLINPUT, MWMO_INPUTAVAILABLE);
	if (waitStatus == WAIT_FAILED)
		return RenderViewerStatus_PlatformInitFailed;
	return PlatformRaylibPoll(state);
}

int PlatformRaylibWake(RenderPlatformState* state)
{
	return SetEvent(static_cast<HANDLE>(state->wakeEvent)) != FALSE ? RenderViewerStatus_Ok
	                                                                : RenderViewerStatus_RouteUnavailable;
}

int PlatformRaylibBeginFixedDrawable(RenderPlatformState* state, int widthPixels, int heightPixels,
                                     RenderWindowRestoreRecord* restore)
{
	if (restore == nullptr || widthPixels <= 0 || heightPixels <= 0 || widthPixels > UINT16_MAX ||
	    heightPixels > UINT16_MAX)
	{
		return RenderViewerStatus_InvalidArgument;
	}
	*restore = {};
	if (IsWindowFullscreen() || IsWindowMinimized())
		return RenderViewerStatus_WindowResizeFailed;
	restore->mode = IsWindowMaximized() ? RenderWindowMode_Maximized : RenderWindowMode_Normal;
	restore->resizable =
	    IsWindowState(FLAG_WINDOW_RESIZABLE) ? RenderWindowResizable_Enabled : RenderWindowResizable_Disabled;
	restore->minimumWidth = 760;
	restore->minimumHeight = 520;
	if (restore->mode == RenderWindowMode_Maximized)
		RestoreWindow();
	restore->widthPixels = GetRenderWidth();
	restore->heightPixels = GetRenderHeight();
	restore->status = RenderWindowRestoreStatus_Ready;
	SetWindowMinSize(0, 0);
	SetWindowSize(widthPixels, heightPixels);
	ClearWindowState(FLAG_WINDOW_RESIZABLE);
	const int status = PlatformRaylibPoll(state);
	if (status == RenderViewerStatus_Ok && state->width == widthPixels && state->height == heightPixels)
	{
		return RenderViewerStatus_Ok;
	}
	PlatformRaylibEndFixedDrawable(state, restore);
	return RenderViewerStatus_WindowResizeFailed;
}

int PlatformRaylibEndFixedDrawable(RenderPlatformState* state, RenderWindowRestoreRecord* restore)
{
	if (restore == nullptr || restore->status != RenderWindowRestoreStatus_Ready)
	{
		return RenderViewerStatus_InvalidArgument;
	}
	SetWindowState(FLAG_WINDOW_RESIZABLE);
	RestoreWindow();
	SetWindowMinSize(0, 0);
	SetWindowSize(restore->widthPixels, restore->heightPixels);
	SetWindowMinSize(restore->minimumWidth, restore->minimumHeight);
	if (restore->mode == RenderWindowMode_Maximized)
		MaximizeWindow();
	if (restore->resizable == RenderWindowResizable_Disabled)
		ClearWindowState(FLAG_WINDOW_RESIZABLE);
	restore->status = RenderWindowRestoreStatus_Absent;
	return PlatformRaylibPoll(state);
}

void PlatformRaylibDestroy(RenderPlatformState* state)
{
	if (state->nativeHandle != nullptr)
	{
		RemoveWindowSubclass(static_cast<HWND>(state->nativeHandle), RenderWindowProcedure, 1);
	}
	if (state->wakeEvent != nullptr)
		CloseHandle(static_cast<HANDLE>(state->wakeEvent));
	if (state->frameCompletionEvent != nullptr)
		CloseHandle(static_cast<HANDLE>(state->frameCompletionEvent));
	CloseWindow();
	*state = {};
}
}
