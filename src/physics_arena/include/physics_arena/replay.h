#pragma once

#include "physics_arena/bench_types.h"
#include "replay_recording.h"
#include <string_view>
#include <atomic>

struct ZSTD_seekable_s;

namespace physics_arena
{
struct Catalog;
struct ResultManifestRecord;
enum ReplayPlayState
{
	ReplayPlayState_Paused = 0,
	ReplayPlayState_Playing = 1,
};

enum ReplayLoopMode
{
	ReplayLoopMode_Off = 0,
	ReplayLoopMode_On = 1,
};

enum ReplayVisibility
{
	ReplayVisibility_Visible = 0,
	ReplayVisibility_Minimized = 1,
};

struct ReplayClock
{
	double anchorSeconds;
	double speed;
	double framesPerSecond;
	std::uint64_t anchorOrdinal;
	std::uint64_t ordinal;
	std::uint64_t finalOrdinal;
	ReplayPlayState state;
	ReplayLoopMode loop;
	ReplayVisibility visibility;
};

struct ReplayFrame
{
	std::vector<benchmark_visual::VisualStableTransform> transforms;
	std::vector<benchmark_visual::VisualDebugPrimitive> debug;
	std::uint64_t ordinal;
	PresenceStatus presence;
};

struct ReplayRecording
{
	std::FILE* file;
	benchmark_visual::VisualScene scene;
	std::vector<benchmark_visual::VisualGeometry> geometries;
	std::vector<benchmark_visual::VisualInstance> instances;
	benchmark_visual::VisualMeshStorage mesh;
	std::vector<std::uint8_t> encodedFrame;
	ReplayFrame frame;
	benchmark_replay::RecordingLayout layout;
	benchmark_replay::WorkKind workKind;
	double timestep;
	ZSTD_seekable_s* decoder;
	std::vector<std::uint8_t> decodedBlock;
	std::uint64_t payloadPosition;
	std::uint64_t cachedBlock;
	std::uint32_t formatVersion;
};

constexpr std::uint32_t kCompressedReplayVersion = 2;
constexpr std::uint32_t kReplayBlockFrames = 16;

struct ReplayCompressionResult
{
	std::uint64_t rawBytes;
	std::uint64_t compressedBytes;
	std::uint64_t retainedTemporaryBytes;
};

struct PreparedRunRequest;
struct ReplaySpaceProjection
{
	std::uint64_t tupleCount;
	std::uint64_t rawTupleBytes;
	std::uint64_t compressedTupleBound;
	std::uint64_t requiredBytes;
};
ReplaySpaceProjection ProjectReplaySpace(const PreparedRunRequest& request);

enum ReplayAvailability
{
	ReplayAvailability_NotRecorded = 0,
	ReplayAvailability_Available = 1,
	ReplayAvailability_Unavailable = 2,
	ReplayAvailability_Invalid = 3,
	ReplayAvailability_Temporary = 4,
};
enum ReplayStorageScope
{
	ReplayStorageScope_Tuple = 0,
	ReplayStorageScope_Run = 1,
	ReplayStorageScope_Library = 2,
	ReplayStorageScope_Leftovers = 3,
};
enum ReplayStorageKind
{
	ReplayStorageKind_Recording = 0,
	ReplayStorageKind_Temporary = 1,
};
struct ReplayStorageSelection
{
	std::vector<std::filesystem::path> resultDirectories;
	ReplayStorageScope scope;
	std::uint32_t engineOrdinal;
	std::uint32_t threadOrdinal;
	std::uint32_t repeatIndex;
	std::filesystem::path validationDirectory = {};
};
struct ReplayStorageFile
{
	std::filesystem::path path;
	std::uint64_t bytes;
	ReplayStorageKind kind;
	std::uint32_t engineOrdinal = 0;
	std::uint32_t threadOrdinal = 0;
	std::uint32_t repeatIndex = 0;
	ReplayAvailability availability = ReplayAvailability_Invalid;
};
struct ReplayStorageInventory
{
	std::vector<ReplayStorageFile> files;
	std::uint64_t recordingBytes;
	std::uint64_t temporaryBytes;
	std::uint32_t recordingCount;
	std::uint32_t temporaryCount;
};
struct ReplayStorageFailure
{
	std::filesystem::path path;
	std::uint32_t systemError;
};
struct ReplayStorageResult
{
	std::vector<ReplayStorageFailure> failures;
	ReplayStorageInventory remaining;
	PresenceStatus inventoryComplete;
	std::uint64_t freedBytes;
	std::uint64_t remainingBytes;
	std::uint32_t removedCount;
};
enum ReplayStoragePhase
{
	ReplayStoragePhase_Inspecting = 0,
	ReplayStoragePhase_Deleting = 1,
	ReplayStoragePhase_Finished = 2,
};
struct ReplayStorageOperation
{
	const std::atomic<std::uint32_t>* cancellation;
	void* context;
	void (*progress)(void* context, ReplayStoragePhase phase, std::uint32_t completed, std::uint32_t total);
};
void AddReplayStorageFile(ReplayStorageInventory* inventory, const ReplayStorageFile& file);
std::filesystem::path ReplayStorageRunDirectory(const ReplayStorageFile& file);
ArenaStatus InspectReplayStorage(const Catalog* catalog, const ReplayStorageSelection& selection,
                                 ReplayStorageInventory* inventory, StatusRecord* error,
                                 const ReplayStorageOperation* operation = nullptr);
ArenaStatus DeleteReplayStorage(const Catalog* catalog, const ReplayStorageSelection& selection,
                                const ReplayStorageInventory& confirmed, ReplayStorageResult* result,
                                StatusRecord* error, const ReplayStorageOperation* operation = nullptr);
ReplayAvailability InspectReplayAvailability(const Catalog* catalog, const ResultManifestRecord* manifest,
                                             const std::filesystem::path& directory, std::uint32_t engineOrdinal,
                                             std::uint32_t threadOrdinal, std::uint32_t repeatIndex);

ArenaStatus FinalizeReplayCompression(const std::filesystem::path& spoolPath, const std::filesystem::path& finalPath,
                                      const benchmark_visual::VisualRunIdentity* expectedIdentity,
                                      const std::atomic<std::uint32_t>* cancellation, ReplayCompressionResult* result,
                                      StatusRecord* error);
ArenaStatus OpenCompressedReplay(ReplayRecording* recording, StatusRecord* error);
ArenaStatus ReadCompressedReplayFrame(ReplayRecording* recording, std::uint64_t ordinal, StatusRecord* error);
void CloseCompressedReplay(ReplayRecording* recording);
ArenaStatus ReplayError(StatusRecord* error, std::string_view detail);

std::filesystem::path ReplayTuplePath(const std::filesystem::path& resultDirectory, std::string_view engineId,
                                      std::uint32_t threadCount, std::uint32_t repeatIndex);

ArenaStatus OpenReplay(const std::filesystem::path& path, const benchmark_visual::VisualRunIdentity* expectedIdentity,
                       ReplayRecording* recording, StatusRecord* error);
ArenaStatus OpenResultReplay(const Catalog* catalog, const ResultManifestRecord* manifest,
                             const std::filesystem::path& resultDirectory, std::uint32_t engineOrdinal,
                             std::uint32_t threadOrdinal, std::uint32_t repeatIndex, ReplayRecording* recording,
                             StatusRecord* error);
ArenaStatus SeekReplay(ReplayRecording* recording, std::uint64_t ordinal, StatusRecord* error);
void CloseReplay(ReplayRecording* recording);
void InitializeReplayClock(const ReplayRecording& recording, ReplayClock* clock);
void SeekReplayClock(ReplayClock* clock, std::uint64_t ordinal, double nowSeconds);
void PlayReplayClock(ReplayClock* clock, ReplayPlayState state, double nowSeconds);
void SetReplaySpeed(ReplayClock* clock, double speed, double nowSeconds);
void SetReplayVisibility(ReplayClock* clock, ReplayVisibility visibility, double nowSeconds);
std::uint64_t AdvanceReplayClock(ReplayClock* clock, double nowSeconds);
}
