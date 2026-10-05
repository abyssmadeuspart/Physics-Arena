#include "benchmark_visual/physics_arena.h"
#include "benchmark_visual/visual_renderer.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace benchmark_visual
{
constexpr std::size_t kPhysicsArenaArgumentCapacity = 512;
constexpr std::size_t kPhysicsArenaArgumentTextCapacity = 32768;

struct PhysicsArenaArguments
{
	std::array<char, kPhysicsArenaArgumentTextCapacity> text;
	std::array<char*, kPhysicsArenaArgumentCapacity> values;
	std::uint32_t textUsed;
	std::uint32_t count;
};

int DecodePhysicsArenaArguments(int argc, wchar_t** wideArguments, PhysicsArenaArguments* arguments)
{
	if (argc <= 0 || static_cast<std::size_t>(argc) > arguments->values.size())
		return RenderViewerStatus_InvalidArgument;
	for (int index = 0; index < argc; ++index)
	{
		const int remaining = static_cast<int>(arguments->text.size() - arguments->textUsed);
		const int written =
		    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideArguments[index], -1,
			                    arguments->text.data() + arguments->textUsed, remaining, nullptr, nullptr);
		if (written <= 0)
			return RenderViewerStatus_InvalidArgument;
		arguments->values[arguments->count++] = arguments->text.data() + arguments->textUsed;
		arguments->textUsed += static_cast<std::uint32_t>(written);
	}
	return RenderViewerStatus_Ok;
}

int DispatchPhysicsArena(PhysicsArenaArguments* arguments)
{
	return RunPhysicsArena(static_cast<int>(arguments->count), arguments->values.data());
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	int argc = 0;
	wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv == nullptr || argc <= 0)
		return benchmark_visual::RenderViewerStatus_InvalidArgument;
	benchmark_visual::PhysicsArenaArguments arguments = {};
	const int decodeStatus = benchmark_visual::DecodePhysicsArenaArguments(argc, argv, &arguments);
	LocalFree(argv);
	return decodeStatus == benchmark_visual::RenderViewerStatus_Ok ? benchmark_visual::DispatchPhysicsArena(&arguments)
	                                                               : decodeStatus;
}
