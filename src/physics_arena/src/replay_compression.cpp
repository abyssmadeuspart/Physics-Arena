#include "physics_arena/replay.h"
#include "physics_arena/run.h"
#include "zstd_seekable.h"

#include <algorithm>
#include <cstring>
#include <io.h>

namespace physics_arena
{
using namespace benchmark_replay;

namespace
{
enum CodecBoundary : std::uint8_t
{
	CodecBoundary_Frame,
	CodecBoundary_Stream,
};

int ReadPayload(void* opaque, void* buffer, std::size_t count)
{
	ReplayRecording* recording = static_cast<ReplayRecording*>(opaque);
	const std::uint64_t length = recording->layout.trailerOffset - recording->layout.framesOffset;
	if (count > length - recording->payloadPosition)
		return -1;
	if (std::fread(buffer, 1, count, recording->file) != count)
		return -1;
	recording->payloadPosition += count;
	return 0;
}

int SeekPayload(void* opaque, long long offset, int origin)
{
	ReplayRecording* recording = static_cast<ReplayRecording*>(opaque);
	const std::uint64_t length = recording->layout.trailerOffset - recording->layout.framesOffset;
	if (origin != SEEK_SET && origin != SEEK_END)
		return -1;
	const std::int64_t base = origin == SEEK_END ? static_cast<std::int64_t>(length) : 0;
	if (offset < -base || offset > static_cast<std::int64_t>(length) - base)
		return -1;
	const std::uint64_t position = static_cast<std::uint64_t>(base + offset);
	if (_fseeki64(recording->file, static_cast<std::int64_t>(recording->layout.framesOffset + position), SEEK_SET) != 0)
		return -1;
	recording->payloadPosition = position;
	return 0;
}

int WriteBytes(std::FILE* file, const void* bytes, std::size_t count)
{
	return std::fwrite(bytes, 1, count, file) == count ? 1 : 0;
}

int FlushCodec(ZSTD_seekable_CStream* codec, std::FILE* file, std::vector<std::uint8_t>* output, CodecBoundary boundary)
{
	std::size_t remaining = 1;
	while (remaining != 0)
	{
		ZSTD_outBuffer buffer = {output->data(), output->size(), 0};
		remaining = boundary == CodecBoundary_Stream ? ZSTD_seekable_endStream(codec, &buffer)
		                                             : ZSTD_seekable_endFrame(codec, &buffer);
		if (ZSTD_isError(remaining) || WriteBytes(file, buffer.dst, buffer.pos) == 0)
			return 0;
	}
	return 1;
}
}

void CloseCompressedReplay(ReplayRecording* recording)
{
	if (recording->decoder != nullptr)
		ZSTD_seekable_free(recording->decoder);
	recording->decoder = nullptr;
}

ArenaStatus OpenCompressedReplay(ReplayRecording* recording, StatusRecord* error)
{
	const RecordingLayout& layout = recording->layout;
	const std::uint64_t blockCount = (layout.frameCount + kReplayBlockFrames - 1) / kReplayBlockFrames;
	const std::uint64_t tableBytes = 17 + 12 * blockCount;
	const std::uint64_t payloadBytes = layout.trailerOffset - layout.framesOffset;
	if (payloadBytes <= tableBytes ||
	    layout.frameStride * kReplayBlockFrames > ZSTD_SEEKABLE_MAX_FRAME_DECOMPRESSED_SIZE)
		return ReplayError(error, "compressed_payload_bounds");
	std::array<std::uint8_t, 9> footer = {};
	if (_fseeki64(recording->file, static_cast<std::int64_t>(layout.trailerOffset - footer.size()), SEEK_SET) != 0 ||
	    std::fread(footer.data(), 1, footer.size(), recording->file) != footer.size())
		return ReplayError(error, "compressed_seek_footer_missing");
	const std::uint8_t* input = footer.data();
	if (GetU32(input) != blockCount || *input++ != 0x80 || GetU32(input) != ZSTD_SEEKABLE_MAGICNUMBER)
		return ReplayError(error, "compressed_seek_footer_invalid");
	recording->decoder = ZSTD_seekable_create();
	if (recording->decoder == nullptr)
		return ReplayError(error, "compressed_decoder_allocation");
	const ZSTD_seekable_customFile view = {recording, ReadPayload, SeekPayload};
	if (ZSTD_isError(ZSTD_seekable_initAdvanced(recording->decoder, view)))
		return ReplayError(error, "compressed_seek_table_invalid");
	std::uint64_t compressedOffset = 0;
	std::uint64_t decodedOffset = 0;
	for (unsigned block = 0; block < blockCount; ++block)
	{
		const std::uint64_t frames =
		    std::min<std::uint64_t>(kReplayBlockFrames, layout.frameCount - std::uint64_t{block} * kReplayBlockFrames);
		const std::size_t compressedSize = ZSTD_seekable_getFrameCompressedSize(recording->decoder, block);
		if (ZSTD_seekable_getFrameCompressedOffset(recording->decoder, block) != compressedOffset ||
		    ZSTD_seekable_getFrameDecompressedOffset(recording->decoder, block) != decodedOffset ||
		    ZSTD_seekable_getFrameDecompressedSize(recording->decoder, block) != frames * layout.frameStride ||
		    compressedSize == 0 || compressedSize > payloadBytes - tableBytes - compressedOffset)
			return ReplayError(error, "compressed_block_bounds");
		compressedOffset += compressedSize;
		decodedOffset += frames * layout.frameStride;
	}
	if (compressedOffset != payloadBytes - tableBytes)
		return ReplayError(error, "compressed_payload_length");
	recording->decodedBlock.resize(
	    static_cast<std::size_t>(layout.frameStride * std::min<std::uint64_t>(kReplayBlockFrames, layout.frameCount)));
	recording->cachedBlock = UINT64_MAX;
	return ArenaStatus_Ok;
}

ArenaStatus ReadCompressedReplayFrame(ReplayRecording* recording, std::uint64_t ordinal, StatusRecord* error)
{
	const std::uint64_t block = ordinal / kReplayBlockFrames;
	const std::size_t stride = static_cast<std::size_t>(recording->layout.frameStride);
	if (recording->cachedBlock != block)
	{
		recording->cachedBlock = UINT64_MAX;
		const std::size_t frames = static_cast<std::size_t>(
		    std::min<std::uint64_t>(kReplayBlockFrames, recording->layout.frameCount - block * kReplayBlockFrames));
		const std::size_t decoded = ZSTD_seekable_decompressFrame(recording->decoder, recording->decodedBlock.data(),
		                                                          frames * stride, static_cast<unsigned>(block));
		if (ZSTD_isError(decoded) || decoded != frames * stride)
			return ReplayError(error, "compressed_block_decode_failed");
		for (std::size_t frame = 0; frame < frames; ++frame)
		{
			std::uint8_t* current = recording->decodedBlock.data() + frame * stride;
			const std::uint8_t* input = current;
			if (GetU64(input) != block * kReplayBlockFrames + frame)
				return ReplayError(error, "compressed_block_ordinal");
			if (frame == 0)
				continue;
			const std::uint8_t* previous = current - stride + 8;
			std::uint8_t* output = current + 8;
			for (std::size_t word = 8; word < stride; word += 4)
			{
				const std::uint32_t bits = GetU32(input) ^ GetU32(previous);
				PutU32(output, bits);
			}
		}
		recording->cachedBlock = block;
	}
	std::memcpy(recording->encodedFrame.data(),
	            recording->decodedBlock.data() + (ordinal % kReplayBlockFrames) * stride, stride);
	return ArenaStatus_Ok;
}

ReplaySpaceProjection ProjectReplaySpace(const PreparedRunRequest& request)
{
	ReplaySpaceProjection projection = {};
	if (request.recordingMode == physics_arena::RecordingMode_Off)
		return projection;
	const CaseExecutionSpec& execution = request.configuration.execution;
	projection.tupleCount = std::uint64_t{request.engineCount} * request.recordingThreads.count * request.repeatCount;
	const std::uint64_t frames = std::uint64_t{execution.measuredWorkUnitCount} + 1;
	const std::uint64_t scene = kHeaderBytes + kTrailerBytes + 120 + 44ull * execution.visualInstanceCount +
	                            40ull * benchmark_visual::kMaxSceneGeometries +
	                            12ull * benchmark_visual::kMaxSceneVertices +
	                            4ull * benchmark_visual::kMaxSceneIndices + 8ull * benchmark_visual::kMaxSceneEdges;
	const std::uint64_t rawStride =
	    8ull + 32ull * execution.dynamicBodyCount + 40ull * execution.visualDebugPrimitiveCount;
	const std::uint64_t compactStride = rawStride - 4ull * execution.dynamicBodyCount;
	const std::uint64_t blocks = (frames + kReplayBlockFrames - 1) / kReplayBlockFrames;
	projection.rawTupleBytes = scene + frames * rawStride;
	projection.compressedTupleBound =
	    scene + blocks * (ZSTD_compressBound(static_cast<std::size_t>(compactStride * kReplayBlockFrames)) + 12) + 17;
	// completed tuples coexist with the current raw spool and compressed partial
	projection.requiredBytes = projection.tupleCount * projection.compressedTupleBound + projection.rawTupleBytes;
	return projection;
}

ArenaStatus FinalizeReplayCompression(const std::filesystem::path& spoolPath, const std::filesystem::path& finalPath,
                                      const benchmark_visual::VisualRunIdentity* expectedIdentity,
                                      const std::atomic<std::uint32_t>* cancellation, ReplayCompressionResult* result,
                                      StatusRecord* error)
{
	*result = {};
	ReplayRecording source = {};
	ArenaStatus status = OpenReplay(spoolPath, expectedIdentity, &source, error);
	if (status != ArenaStatus_Ok)
		return status;
	if (source.formatVersion != kFormatVersion)
	{
		CloseReplay(&source);
		return ReplayError(error, "raw_spool_required");
	}
	result->rawBytes = source.layout.fileBytes;
	const std::filesystem::path partialPath = finalPath.wstring() + L".partial";
	std::error_code staleError;
	if (std::filesystem::exists(partialPath, staleError) &&
	    (!std::filesystem::is_regular_file(std::filesystem::symlink_status(partialPath, staleError)) ||
	     !std::filesystem::remove(partialPath, staleError)))
	{
		CloseReplay(&source);
		result->retainedTemporaryBytes = result->rawBytes;
		return ReplayError(error, "compressed_stale_partial_cleanup_failed: " + partialPath.string());
	}
	std::FILE* outputFile = nullptr;
	if (_wfopen_s(&outputFile, partialPath.c_str(), L"wbx") != 0)
	{
		CloseReplay(&source);
		result->retainedTemporaryBytes = result->rawBytes;
		return ReplayError(error, "compressed_partial_create_failed: " + partialPath.string());
	}
	ZSTD_seekable_CStream* codec = ZSTD_seekable_createCStream();
	if (codec == nullptr || ZSTD_isError(ZSTD_seekable_initCStream(codec, 1, 1, 0)))
		status = ReplayError(error, "compressed_encoder_initialization");
	std::vector<std::uint8_t> buffer(kFileBufferBytes);
	std::uint64_t copyOffset = 0;
	while (status == ArenaStatus_Ok && copyOffset < source.layout.framesOffset)
	{
		const std::size_t count =
		    static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), source.layout.framesOffset - copyOffset));
		if (_fseeki64(source.file, static_cast<std::int64_t>(copyOffset), SEEK_SET) != 0 ||
		    std::fread(buffer.data(), 1, count, source.file) != count ||
		    WriteBytes(outputFile, buffer.data(), count) == 0)
			status = ReplayError(error, "compressed_scene_copy_failed");
		copyOffset += count;
	}
	const std::size_t stride =
	    static_cast<std::size_t>(source.layout.frameStride - std::uint64_t{source.scene.dynamicTransformCount} * 4);
	PresenceStatus invalidSpool = PresenceStatus_Absent;
	std::vector<std::uint8_t> previous(stride);
	std::vector<std::uint8_t> current(stride);
	std::vector<std::uint8_t> difference(stride);
	for (std::uint64_t ordinal = 0; status == ArenaStatus_Ok && ordinal < source.layout.frameCount; ++ordinal)
	{
		if (cancellation != nullptr && cancellation->load(std::memory_order_acquire) != 0)
		{
			ReplayError(error, "replay_compression_cancelled");
			error->code = ArenaStatus_Interrupted;
			status = ArenaStatus_Interrupted;
			break;
		}
		status = SeekReplay(&source, ordinal, error);
		if (status != ArenaStatus_Ok)
		{
			invalidSpool = PresenceStatus_Present;
			break;
		}
		const std::uint8_t* input = source.encodedFrame.data();
		std::uint8_t* output = current.data();
		PutU64(output, GetU64(input));
		for (std::uint32_t body = 0; body < source.scene.dynamicTransformCount; ++body)
		{
			input += 4;
			std::memcpy(output, input, 28);
			input += 28;
			output += 28;
		}
		std::memcpy(output, input, std::size_t{source.scene.debugPrimitiveCount} * 40);
		std::memcpy(difference.data(), current.data(), stride);
		if (ordinal % kReplayBlockFrames != 0)
		{
			const std::uint8_t* currentWord = current.data() + 8;
			const std::uint8_t* previousWord = previous.data() + 8;
			std::uint8_t* differenceWord = difference.data() + 8;
			for (std::size_t word = 8; word < stride; word += 4)
				PutU32(differenceWord, GetU32(currentWord) ^ GetU32(previousWord));
		}
		previous.swap(current);
		ZSTD_inBuffer encoded = {difference.data(), difference.size(), 0};
		while (encoded.pos < encoded.size)
		{
			ZSTD_outBuffer compressed = {buffer.data(), buffer.size(), 0};
			if (ZSTD_isError(ZSTD_seekable_compressStream(codec, &compressed, &encoded)) ||
			    WriteBytes(outputFile, buffer.data(), compressed.pos) == 0)
			{
				status = ReplayError(error, "compressed_frame_write_failed");
				break;
			}
		}
		if (status == ArenaStatus_Ok && (ordinal + 1) % kReplayBlockFrames == 0 &&
		    ordinal + 1 != source.layout.frameCount && FlushCodec(codec, outputFile, &buffer, CodecBoundary_Frame) == 0)
			status = ReplayError(error, "compressed_block_flush_failed");
	}
	if (status == ArenaStatus_Ok && FlushCodec(codec, outputFile, &buffer, CodecBoundary_Stream) == 0)
		status = ReplayError(error, "compressed_seek_table_write_failed");
	if (codec != nullptr)
		ZSTD_seekable_freeCStream(codec);
	if (status == ArenaStatus_Ok)
	{
		const std::int64_t trailerOffset = _ftelli64(outputFile);
		if (trailerOffset < 0)
			status = ReplayError(error, "compressed_output_position_failed");
		else
		{
			result->compressedBytes = static_cast<std::uint64_t>(trailerOffset) + kTrailerBytes;
			std::array<std::uint8_t, kTrailerBytes> trailer = {};
			std::memcpy(trailer.data(), "BPRDONE2", 8);
			std::uint8_t* output = trailer.data() + 8;
			PutU64(output, source.layout.frameCount);
			PutU64(output, result->compressedBytes);
			std::array<std::uint8_t, kHeaderBytes> header = {};
			RecordingLayout layout = source.layout;
			layout.frameStride = stride;
			layout.trailerOffset = static_cast<std::uint64_t>(trailerOffset);
			EncodeHeader(source.scene, source.workKind, source.timestep, layout, header.data());
			output = header.data() + 8;
			PutU32(output, kCompressedReplayVersion);
			if (WriteBytes(outputFile, trailer.data(), trailer.size()) == 0 ||
			    _fseeki64(outputFile, 0, SEEK_SET) != 0 || WriteBytes(outputFile, header.data(), header.size()) == 0 ||
			    std::fflush(outputFile) != 0 || _commit(_fileno(outputFile)) != 0)
				status = ReplayError(error, "compressed_completion_write_failed");
		}
	}
	if (std::fclose(outputFile) != 0 && status == ArenaStatus_Ok)
		status = ReplayError(error, "compressed_close_failed");
	CloseReplay(&source);
	if (status == ArenaStatus_Ok)
	{
		ReplayRecording admitted = {};
		status = OpenReplay(partialPath, expectedIdentity, &admitted, error);
		for (std::uint64_t ordinal = 0; status == ArenaStatus_Ok && ordinal < admitted.layout.frameCount;
		     ordinal += kReplayBlockFrames)
			status = SeekReplay(&admitted, ordinal, error);
		CloseReplay(&admitted);
	}
	std::error_code filesystemError;
	if (status == ArenaStatus_Ok)
	{
		std::filesystem::rename(partialPath, finalPath, filesystemError);
		if (filesystemError)
			status = ReplayError(error, "compressed_publication_failed");
	}
	if (status != ArenaStatus_Ok)
	{
		std::filesystem::remove(partialPath, filesystemError);
		if (filesystemError)
		{
			std::error_code sizeError;
			const std::uintmax_t retained = std::filesystem::file_size(partialPath, sizeError);
			if (!sizeError)
				result->retainedTemporaryBytes += retained;
			ReplayError(error, "compressed_partial_cleanup_failed: " + partialPath.string());
		}
		if (invalidSpool == PresenceStatus_Present)
			std::filesystem::remove(spoolPath, filesystemError);
		if (invalidSpool == PresenceStatus_Absent || filesystemError)
			result->retainedTemporaryBytes += result->rawBytes;
		return status;
	}
	std::filesystem::remove(spoolPath, filesystemError);
	if (filesystemError)
		result->retainedTemporaryBytes = result->rawBytes;
	return ArenaStatus_Ok;
}
}
