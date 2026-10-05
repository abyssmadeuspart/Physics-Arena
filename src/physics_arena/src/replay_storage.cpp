#include "physics_arena/ray_tracing_images.h"
#include "physics_arena/replay.h"
#include "physics_arena/release_contracts.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <memory>

namespace physics_arena
{
namespace
{
using PinnedDirectories = std::vector<std::unique_ptr<void, BOOL(WINAPI*)(HANDLE)>>;

DWORD PinParents(const std::filesystem::path& path, PinnedDirectories* pinned)
{
	if (!path.is_absolute())
		return ERROR_BAD_PATHNAME;
	for (const std::filesystem::path& component : path)
		if (component == L".." || component == L".")
			return ERROR_BAD_PATHNAME;
	std::filesystem::path current = path.root_path();
	for (const std::filesystem::path& component : path.parent_path().relative_path())
	{
		current /= component;
		HANDLE handle = CreateFileW(current.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		                            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (handle == INVALID_HANDLE_VALUE)
			return GetLastError();
		pinned->emplace_back(handle, &CloseHandle);
		BY_HANDLE_FILE_INFORMATION info = {};
		if (GetFileInformationByHandle(handle, &info) == 0)
			return GetLastError();
		if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
		    (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
			return ERROR_REPARSE_TAG_INVALID;
	}
	return ERROR_SUCCESS;
}

ArenaStatus StorageError(StatusRecord* error, const std::filesystem::path& path, std::string_view detail)
{
	const std::string message = std::string(detail) + ": " + path.string();
	return ReplayError(error, message);
}

int Missing(DWORD code)
{
	return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND;
}

ArenaStatus StorageCancellation(const ReplayStorageOperation* operation, StatusRecord* error)
{
	if (operation == nullptr || operation->cancellation == nullptr ||
	    operation->cancellation->load(std::memory_order_acquire) == 0)
		return ArenaStatus_Ok;
	ReplayError(error, "Recording deletion interrupted");
	error->code = ArenaStatus_Interrupted;
	const std::string_view status = "interrupted";
	std::copy(status.begin(), status.end(), error->status.begin());
	error->statusSize = static_cast<std::uint32_t>(status.size());
	return ArenaStatus_Interrupted;
}

void StorageProgress(const ReplayStorageOperation* operation, ReplayStoragePhase phase,
                     std::uint32_t completed, std::uint32_t total)
{
	if (operation != nullptr && operation->progress != nullptr)
		operation->progress(operation->context, phase, completed, total);
}

PresenceStatus MatchStorageTuple(const Catalog* catalog, const ResultManifestRecord& manifest,
                                 const ReplayStorageSelection& selection, std::wstring name,
                                 ReplayStorageFile* file)
{
	file->kind = ReplayStorageKind_Recording;
	file->availability = ReplayAvailability_Invalid;
	if (name.ends_with(L".partial"))
	{
		name.resize(name.size() - 8);
		file->kind = ReplayStorageKind_Temporary;
	}
	const std::wstring suffix = manifest.recordingKind == RecordingKind_NativeRayHits ? L".rth" : L".bpr";
	if (!name.ends_with(suffix))
		return PresenceStatus_Absent;
	for (std::uint32_t engine = 0; engine < manifest.engineCount; ++engine)
	{
		const std::string_view id = CatalogTextView(catalog, catalog->engines[manifest.engines[engine].engineIndex].id);
		const std::wstring prefix = std::wstring(id.begin(), id.end()) + L"_t";
		if (!name.starts_with(prefix))
			continue;
		unsigned threads = 0;
		unsigned repeat = 0;
		if (swscanf_s(name.c_str() + prefix.size(), L"%u_r%u", &threads, &repeat) != 2 || repeat >= manifest.repeatCount)
			return PresenceStatus_Absent;
		for (std::uint32_t thread = 0; thread < manifest.threadCount; ++thread)
		{
			if (manifest.threadCounts[thread] != threads ||
			    name != prefix + std::to_wstring(threads) + L"_r" + std::to_wstring(repeat) + suffix)
				continue;
			if (selection.scope == ReplayStorageScope_Tuple &&
			    (engine != selection.engineOrdinal || thread != selection.threadOrdinal || repeat != selection.repeatIndex))
				return PresenceStatus_Absent;
			file->engineOrdinal = engine;
			file->threadOrdinal = thread;
			file->repeatIndex = repeat;
			return PresenceStatus_Present;
		}
	}
	return PresenceStatus_Absent;
}

ArenaStatus InspectStorageDirectory(const Catalog* catalog, const ResultManifestRecord& manifest,
                                    const ReplayStorageSelection& selection, const std::filesystem::path& directory,
                                    ReplayStorageKind directoryKind, ReplayStorageInventory* inventory,
                                    StatusRecord* error, const ReplayStorageOperation* operation)
{
	PinnedDirectories pinned;
	const DWORD code = PinParents(directory / L"*", &pinned);
	if (Missing(code))
		return ArenaStatus_Ok;
	if (code != ERROR_SUCCESS)
		return StorageError(error, directory, "recording_parent_unavailable_or_reparse");
	WIN32_FIND_DATAW data = {};
	HANDLE search = FindFirstFileW((directory / L"*").c_str(), &data);
	if (search == INVALID_HANDLE_VALUE)
		return Missing(GetLastError()) ? ArenaStatus_Ok : StorageError(error, directory, "recording_storage_unreadable");
	ArenaStatus status = ArenaStatus_Ok;
	do
	{
		status = StorageCancellation(operation, error);
		if (status != ArenaStatus_Ok)
			break;
		ReplayStorageFile file = {};
		if (MatchStorageTuple(catalog, manifest, selection, data.cFileName, &file) == PresenceStatus_Absent)
			continue;
		file.path = directory / data.cFileName;
		if ((data.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0)
		{
			status = StorageError(error, file.path, "recording_storage_reparse_or_directory");
			break;
		}
		if (directoryKind == ReplayStorageKind_Temporary)
			file.kind = ReplayStorageKind_Temporary;
		file.bytes = (std::uint64_t{data.nFileSizeHigh} << 32) | data.nFileSizeLow;
		AddReplayStorageFile(inventory, file);
	} while (FindNextFileW(search, &data) != 0);
	const DWORD enumerationError = GetLastError();
	FindClose(search);
	if (status == ArenaStatus_Ok && enumerationError != ERROR_NO_MORE_FILES)
		return StorageError(error, directory, "recording_storage_enumeration_failed");
	return status;
}

int StoragePathLess(const ReplayStorageFile& left, const ReplayStorageFile& right)
{
	return left.path < right.path;
}
}

void AddReplayStorageFile(ReplayStorageInventory* inventory, const ReplayStorageFile& file)
{
	inventory->files.push_back(file);
	if (file.kind == ReplayStorageKind_Temporary)
	{
		inventory->temporaryBytes += file.bytes;
		++inventory->temporaryCount;
	}
	else
	{
		inventory->recordingBytes += file.bytes;
		++inventory->recordingCount;
	}
}

std::filesystem::path ReplayStorageRunDirectory(const ReplayStorageFile& file)
{
	std::filesystem::path directory = file.path.parent_path();
	if (directory.filename() == L".pending")
		directory = directory.parent_path();
	return directory.parent_path();
}

ArenaStatus InspectReplayStorage(const Catalog* catalog, const ReplayStorageSelection& selection,
                                 ReplayStorageInventory* inventory, StatusRecord* error,
                                 const ReplayStorageOperation* operation)
{
	*inventory = {};
	if (catalog == nullptr || selection.resultDirectories.empty() || selection.scope < ReplayStorageScope_Tuple ||
	    selection.scope > ReplayStorageScope_Leftovers ||
	    (selection.scope != ReplayStorageScope_Library && selection.resultDirectories.size() != 1))
		return ReplayError(error, "recording_storage_selection");
	for (const std::filesystem::path& directory : selection.resultDirectories)
	{
		if (StorageCancellation(operation, error) != ArenaStatus_Ok)
			return error->code;
		const std::filesystem::path manifestPath = directory / L"manifest.json";
		PinnedDirectories pinned;
		if (PinParents(manifestPath, &pinned) != ERROR_SUCCESS)
			return StorageError(error, manifestPath, "recording_storage_root");
		const DWORD attributes = GetFileAttributesW(manifestPath.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES ||
		    (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0)
			return StorageError(error, manifestPath, "recording_storage_manifest");
		ResultManifestRecord manifest = {};
		if (LoadResultManifest(manifestPath.c_str(), catalog, &manifest, error) != ArenaStatus_Ok)
			return error->code;
		if (selection.scope == ReplayStorageScope_Tuple &&
		    (selection.engineOrdinal >= manifest.engineCount || selection.threadOrdinal >= manifest.threadCount ||
		     selection.repeatIndex >= manifest.repeatCount))
			return ReplayError(error, "recording_storage_tuple_range");
		const std::filesystem::path recordings = directory /
		    (manifest.recordingKind == RecordingKind_NativeRayHits ? L"ray-images" : L"replays");
		ArenaStatus status = InspectStorageDirectory(catalog, manifest, selection, recordings,
		    ReplayStorageKind_Recording, inventory, error, operation);
		if (status == ArenaStatus_Ok)
			status = InspectStorageDirectory(catalog, manifest, selection, recordings / L".pending",
			    ReplayStorageKind_Temporary, inventory, error, operation);
		if (status != ArenaStatus_Ok)
			return status;
	}
	std::sort(inventory->files.begin(), inventory->files.end(), StoragePathLess);
	return ArenaStatus_Ok;
}

ArenaStatus DeleteReplayStorage(const Catalog* catalog, const ReplayStorageSelection& selection,
                                const ReplayStorageInventory& confirmed, ReplayStorageResult* result,
                                StatusRecord* error, const ReplayStorageOperation* operation)
{
	*result = {};
	result->remainingBytes = confirmed.recordingBytes + confirmed.temporaryBytes;
	StorageProgress(operation, ReplayStoragePhase_Inspecting, 0, 0);
	ReplayStorageInventory current = {};
	ArenaStatus status = InspectReplayStorage(catalog, selection, &current, error, operation);
	if (status != ArenaStatus_Ok)
		return status;
	result->inventoryComplete = PresenceStatus_Present;
	std::vector<ReplayStorageFile> preview = confirmed.files;
	std::sort(preview.begin(), preview.end(), StoragePathLess);
	std::size_t confirmedIndex = 0;
	std::filesystem::path pinnedParent;
	PinnedDirectories pinned;
	DWORD parentError = ERROR_SUCCESS;
	const std::uint32_t total = static_cast<std::uint32_t>(current.files.size());
	StorageProgress(operation, ReplayStoragePhase_Deleting, 0, total);
	for (ReplayStorageFile& file : current.files)
	{
		while (confirmedIndex < preview.size() && preview[confirmedIndex].path < file.path)
			++confirmedIndex;
		const ReplayStorageFile* matched = confirmedIndex < preview.size() && preview[confirmedIndex].path == file.path &&
		    preview[confirmedIndex].bytes == file.bytes ? &preview[confirmedIndex] : nullptr;
		if (matched != nullptr)
			file.availability = matched->availability;
		status = StorageCancellation(operation, error);
		if (status != ArenaStatus_Ok)
		{
			AddReplayStorageFile(&result->remaining, file);
			continue;
		}
		if (matched == nullptr)
		{
			result->failures.push_back({file.path, ERROR_FILE_INVALID});
			AddReplayStorageFile(&result->remaining, file);
			continue;
		}
		if (pinnedParent != file.path.parent_path())
		{
			pinned.clear();
			pinnedParent = file.path.parent_path();
			parentError = PinParents(file.path, &pinned);
		}
		DWORD code = parentError;
		HANDLE handle = INVALID_HANDLE_VALUE;
		if (code == ERROR_SUCCESS)
		{
			handle = CreateFileW(file.path.c_str(), DELETE | FILE_READ_ATTRIBUTES, 0, nullptr, OPEN_EXISTING,
			                     FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
				code = GetLastError();
		}
		std::uint64_t bytes = 0;
		if (handle != INVALID_HANDLE_VALUE)
		{
			BY_HANDLE_FILE_INFORMATION info = {};
			if (GetFileInformationByHandle(handle, &info) == 0)
				code = GetLastError();
			else if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
				code = ERROR_REPARSE_TAG_INVALID;
			else
			{
				bytes = (std::uint64_t{info.nFileSizeHigh} << 32) | info.nFileSizeLow;
				if (bytes != matched->bytes)
				{
					file.bytes = bytes;
					file.availability = ReplayAvailability_Invalid;
					code = ERROR_FILE_INVALID;
				}
			}
			FILE_DISPOSITION_INFO disposition = {TRUE};
			if (code == ERROR_SUCCESS &&
			    SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition)) == 0)
				code = GetLastError();
			CloseHandle(handle);
		}
		if (Missing(code))
			continue;
		if (code != ERROR_SUCCESS)
		{
			result->failures.push_back({file.path, code});
			AddReplayStorageFile(&result->remaining, file);
		}
		else
		{
			result->freedBytes += bytes;
			++result->removedCount;
			StorageProgress(operation, ReplayStoragePhase_Deleting, result->removedCount, total);
		}
	}
	pinned.clear();
	result->remainingBytes = result->remaining.recordingBytes + result->remaining.temporaryBytes;
	// validate only new or changed retained archives in the selected run, on this worker
	ResultManifestRecord manifest = {};
	PresenceStatus manifestLoaded = PresenceStatus_Absent;
	for (ReplayStorageFile& file : result->remaining.files)
	{
		if (file.kind != ReplayStorageKind_Recording || file.availability != ReplayAvailability_Invalid ||
		    selection.validationDirectory.empty() || ReplayStorageRunDirectory(file) != selection.validationDirectory)
			continue;
		if (StorageCancellation(operation, error) != ArenaStatus_Ok)
		{
			status = ArenaStatus_Interrupted;
			break;
		}
		StatusRecord validationError = {};
		if (manifestLoaded == PresenceStatus_Absent)
		{
			if (LoadResultManifest((selection.validationDirectory / L"manifest.json").c_str(), catalog, &manifest,
			                       &validationError) != ArenaStatus_Ok)
				break;
			manifestLoaded = PresenceStatus_Present;
		}
		file.availability = InspectReplayAvailability(catalog, &manifest, selection.validationDirectory,
		    file.engineOrdinal, file.threadOrdinal, file.repeatIndex);
	}
	StorageProgress(operation, ReplayStoragePhase_Finished, result->removedCount, total);
	if (status != ArenaStatus_Ok)
		return status;
	if (!result->failures.empty())
		return StorageError(error, result->failures.front().path, "recording_delete_failed");
	return ArenaStatus_Ok;
}

ReplayAvailability InspectReplayAvailability(const Catalog* catalog, const ResultManifestRecord* manifest,
                                             const std::filesystem::path& directory, std::uint32_t engine,
                                             std::uint32_t thread, std::uint32_t repeat)
{
	if (manifest->recordingMode == RecordingMode_Off || (thread < manifest->threadCount &&
	    RecordingForThread(manifest->recordingThreads, manifest->threadCounts[thread]) == RecordingMode_Off))
		return ReplayAvailability_NotRecorded;
	ReplayStorageSelection selection = {{directory}, ReplayStorageScope_Tuple, engine, thread, repeat};
	ReplayStorageInventory inventory = {};
	StatusRecord error = {};
	if (InspectReplayStorage(catalog, selection, &inventory, &error) != ArenaStatus_Ok)
		return ReplayAvailability_Invalid;
	if (inventory.recordingCount == 0)
		return inventory.temporaryCount != 0 ? ReplayAvailability_Temporary : ReplayAvailability_Unavailable;
	if (manifest->recordingKind == RecordingKind_NativeRayHits)
	{
		if (engine >= manifest->engineCount || thread >= manifest->threadCount || repeat >= manifest->repeatCount) return ReplayAvailability_Invalid;
		const std::string_view engineId = CatalogTextView(catalog, catalog->engines[manifest->engines[engine].engineIndex].id);
		RayImageArchive archive = {};
		const ArenaStatus imageStatus = OpenRayImages(RayImageTuplePath(directory, engineId, manifest->threadCounts[thread], repeat), engineId, manifest->threadCounts[thread], repeat, &archive, &error);
		CloseRayImages(&archive);
		return imageStatus == ArenaStatus_Ok ? ReplayAvailability_Available : ReplayAvailability_Invalid;
	}
	ReplayRecording recording = {};
	const ArenaStatus status =
	    OpenResultReplay(catalog, manifest, directory, engine, thread, repeat, &recording, &error);
	CloseReplay(&recording);
	return status == ArenaStatus_Ok ? ReplayAvailability_Available : ReplayAvailability_Invalid;
}
}
