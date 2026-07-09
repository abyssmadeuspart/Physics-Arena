#include "benchmark_visual/visual_renderer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>

namespace benchmark_visual
{
int PlatformSdl3Probe()
{
	return RenderViewerStatus_RouteUnavailable;
}

int PlatformSdl3Create(RenderPlatformState* state, RenderWindowDesc desc)
{
	if (state == nullptr || desc.title == nullptr || desc.width <= 0 || desc.height <= 0)
	{
		return RenderViewerStatus_InvalidArgument;
	}

	state->window = nullptr;
	state->nativeHandle = nullptr;
	state->width = desc.width;
	state->height = desc.height;
	state->windowEventStatus = RenderWindowEventStatus_Running;

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		return RenderViewerStatus_PlatformInitFailed;
	}

	SDL_Window* window = SDL_CreateWindow(
		desc.title,
		desc.width,
		desc.height,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	if (window == nullptr)
	{
		SDL_Quit();
		return RenderViewerStatus_WindowCreateFailed;
	}

	void* nativeHandle = SDL_GetPointerProperty(
		SDL_GetWindowProperties(window),
		SDL_PROP_WINDOW_WIN32_HWND_POINTER,
		nullptr);
	if (nativeHandle == nullptr)
	{
		SDL_DestroyWindow(window);
		SDL_Quit();
		return RenderViewerStatus_NativeHandleMissing;
	}

	SDL_GetWindowSizeInPixels(window, &state->width, &state->height);
	state->window = window;
	state->nativeHandle = nativeHandle;
	return RenderViewerStatus_Ok;
}

int PlatformSdl3Poll(RenderPlatformState* state)
{
	if (state == nullptr || state->window == nullptr)
	{
		return RenderViewerStatus_InvalidArgument;
	}

	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		ImGuiBgfxProcessSdlEvent(&event);
		if (event.type == SDL_EVENT_QUIT)
		{
			state->windowEventStatus = RenderWindowEventStatus_CloseRequested;
		}
	}

	SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(state->window), &state->width, &state->height);
	return RenderViewerStatus_Ok;
}

void PlatformSdl3Destroy(RenderPlatformState* state)
{
	if (state == nullptr)
	{
		return;
	}

	if (state->window != nullptr)
	{
		SDL_DestroyWindow(static_cast<SDL_Window*>(state->window));
		state->window = nullptr;
	}

	state->nativeHandle = nullptr;
	SDL_Quit();
}
}
