#include "native_run_file_io.h"
#include "physics_arena/run.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cwchar>

namespace benchmark_visual
{
using namespace physics_arena;

ArenaStatus RunFileError(StatusRecord* error, std::string_view detail)
{
	*error = {};
	constexpr std::string_view component = "native_run_file";
	const std::string_view status = ArenaStatusText(ArenaStatus_RunFailed);
	std::copy(component.begin(), component.end(), error->component.begin());
	error->componentSize = static_cast<std::uint32_t>(component.size());
	std::copy(status.begin(), status.end(), error->status.begin());
	error->statusSize = static_cast<std::uint32_t>(status.size());
	std::copy(detail.begin(), detail.end(), error->detail.begin());
	error->detailSize = static_cast<std::uint32_t>(detail.size());
	error->code = ArenaStatus_RunFailed;
	return error->code;
}

ArenaStatus WriteNativeRunFile(const wchar_t* path, std::string_view bytes, StatusRecord* error, NativeRunFilePublication mode)
{
	std::array<wchar_t, kRunPathCapacity> temporary = {};
	LARGE_INTEGER counter = {};
	QueryPerformanceCounter(&counter);
	const int size = std::swprintf(temporary.data(), temporary.size(), L"%ls.tmp-%lu-%llu", path,
	    GetCurrentProcessId(), static_cast<unsigned long long>(counter.QuadPart));
	if (size < 0 || static_cast<std::size_t>(size) >= temporary.size() || bytes.empty() || bytes.size() > MAXDWORD)
		return RunFileError(error, "File or temporary path exceeds capacity");
	HANDLE file = CreateFileW(temporary.data(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return RunFileError(error, "Cannot create temporary file beside destination");
	DWORD written = 0;
	const DWORD expected = static_cast<DWORD>(bytes.size());
	const int writeStatus = WriteFile(file, bytes.data(), expected, &written, nullptr) != 0 && written == expected;
	const int closeStatus = CloseHandle(file) != 0;
	if (writeStatus == 0 || closeStatus == 0)
	{
		DeleteFileW(temporary.data());
		return RunFileError(error, "Cannot write or close temporary file");
	}
	const DWORD flags = MOVEFILE_WRITE_THROUGH | (mode == NativeRunFilePublication_Replace ? MOVEFILE_REPLACE_EXISTING : 0);
	if (MoveFileExW(temporary.data(), path, flags) == 0)
	{
		DeleteFileW(temporary.data());
		return RunFileError(error, "Cannot replace destination file");
	}
	*error = {};
	return ArenaStatus_Ok;
}
}
