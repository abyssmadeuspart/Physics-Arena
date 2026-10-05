#include "physics_arena/ray_tracing_images.h"

#include <cstdio>
#include <cstring>
#include <zstd_seekable.h>

namespace physics_arena
{
namespace
{
using namespace benchmark_ray;
ArenaStatus ImageError(StatusRecord* error, std::string_view detail, ArenaStatus code = ArenaStatus_InvalidResult)
{
	*error = {};
	error->code = code;
	constexpr std::string_view component = "ray_tracing_images";
	const std::string_view status = ArenaStatusText(code);
	std::copy(component.begin(), component.end(), error->component.begin());
	error->componentSize = static_cast<std::uint32_t>(component.size());
	std::copy(status.begin(), status.end(), error->status.begin());
	error->statusSize = static_cast<std::uint32_t>(status.size());
	std::copy(detail.begin(), detail.end(), error->detail.begin());
	error->detailSize = static_cast<std::uint32_t>(detail.size());
	return code;
}
ArenaStatus ReadBytes(RayImageArchive* archive, std::uint64_t offset, void* output, std::size_t bytes,
                      StatusRecord* error)
{
	if (offset > archive->rawBytes || bytes > archive->rawBytes - offset)
		return ImageError(error, "truncated_chunk");
	if (archive->decoder != nullptr)
	{
		const std::size_t read =
		    ZSTD_seekable_decompress(static_cast<ZSTD_seekable*>(archive->decoder), output, bytes, offset);
		return !ZSTD_isError(read) && read == bytes ? ArenaStatus_Ok : ImageError(error, "compressed_chunk_read");
	}
	FILE* file = static_cast<FILE*>(archive->file);
	return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0 && std::fread(output, 1, bytes, file) == bytes
	           ? ArenaStatus_Ok
	           : ImageError(error, "chunk_read");
}
ArenaStatus ReadChunkIndex(RayImageArchive* archive, std::uint32_t chunkCount, StatusRecord* error)
{
	std::array<std::array<std::array<std::uint32_t, Api_Count>, Phase_Count>, kViewCount> seen = {};
	std::uint64_t offset = 64;
	for (std::uint32_t index = 0; index < chunkCount; ++index)
	{
		std::array<std::uint32_t, 8> header = {};
		if (ReadBytes(archive, offset, header.data(), sizeof(header), error) != ArenaStatus_Ok)
			return error->code;
		if (header[0] >= kViewCount || header[1] >= Phase_Count || header[2] >= Api_Count ||
		    (header[1] != Phase_Primary && header[1] != Phase_Shadow && header[1] != Phase_Reflection &&
		     header[1] != Phase_Ambient && header[1] != Phase_Updated) ||
		    header[3] > archive->width * archive->height || header[4] > kActiveByteLimit / sizeof(Hit) ||
		    header[5] != header[3] || header[7] != 0 || seen[header[0]][header[1]][header[2]]++ != 0)
			return ImageError(error, "chunk_identity_or_count");
		const std::uint64_t bytes = static_cast<std::uint64_t>(header[3]) * sizeof(Output) +
		                            static_cast<std::uint64_t>(header[4]) * sizeof(Hit);
		offset += sizeof(header);
		if (offset > archive->rawBytes || bytes > archive->rawBytes - offset || bytes > kActiveByteLimit)
			return ImageError(error, "chunk_size");
		archive->chunks.push_back({header[0], static_cast<Phase>(header[1]), static_cast<Api>(header[2]), header[3],
		                           header[4], header[6], offset});
		offset += bytes;
	}
	if (offset != archive->rawBytes)
		return ImageError(error, "trailing_or_missing_recording_bytes");
	const std::uint32_t phasesPerView = std::string_view(archive->engine.data()) == "entasis" ? 8u : 5u;
	const std::uint32_t viewCount = chunkCount / phasesPerView;
	if (viewCount == 0 || viewCount > kViewCount || chunkCount % phasesPerView != 0)
		return ImageError(error, "view_count");
	for (std::uint32_t view = 0; view < viewCount; ++view)
		for (const Phase phase : {Phase_Primary, Phase_Shadow, Phase_Reflection, Phase_Ambient, Phase_Updated})
		{
			if (seen[view][phase][Api_Ordinary] != 1)
				return ImageError(error, "missing_view_phase");
			const std::uint32_t expected =
			    std::string_view(archive->engine.data()) == "entasis" &&
			            (phase == Phase_Primary || phase == Phase_Reflection || phase == Phase_Updated)
			        ? 1u
			        : 0u;
			if (seen[view][phase][Api_NativeBatch] != expected)
				return ImageError(error, "batch_coverage");
		}
	return ArenaStatus_Ok;
}
}

std::filesystem::path RayImageTuplePath(const std::filesystem::path& result, std::string_view engine,
                                        std::uint32_t threads, std::uint32_t repeat)
{
	return result / "ray-images" /
	       (std::string(engine) + "_t" + std::to_string(threads) + "_r" + std::to_string(repeat) + ".rth");
}

void CloseRayImages(RayImageArchive* archive)
{
	if (archive->decoder != nullptr)
		ZSTD_seekable_free(static_cast<ZSTD_seekable*>(archive->decoder));
	if (archive->file != nullptr)
		std::fclose(static_cast<FILE*>(archive->file));
	*archive = {};
}

ArenaStatus OpenRayImages(const std::filesystem::path& path, std::string_view engine, std::uint32_t threads,
                          std::uint32_t repeat, RayImageArchive* archive, StatusRecord* error)
{
	*archive = {};
	if (path.extension() != ".rth" || (engine != "entasis" && engine != "box3d"))
		return ImageError(error, "recording_path_or_engine");
	FILE* file = nullptr;
	if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || file == nullptr)
		return ImageError(error, "recording_unavailable");
	archive->file = file;
	std::array<std::uint32_t, 16> header = {};
	ArenaStatus status = ArenaStatus_Ok;
	if (_fseeki64(file, 0, SEEK_END) != 0)
		status = ImageError(error, "file_size");
	const __int64 fileBytes = _ftelli64(file);
	if (status == ArenaStatus_Ok &&
	    (fileBytes < 64 || _fseeki64(file, 0, SEEK_SET) != 0 || std::fread(header.data(), 1, 64, file) != 64))
		status = ImageError(error, "recording_header");
	archive->rawBytes = fileBytes > 0 ? static_cast<std::uint64_t>(fileBytes) : 0;
	if (status == ArenaStatus_Ok && header[0] != 0x31485452)
	{
		ZSTD_seekable* decoder = ZSTD_seekable_create();
		archive->decoder = decoder;
		if (decoder == nullptr || ZSTD_isError(ZSTD_seekable_initFile(decoder, file)))
			status = ImageError(error, "compressed_recording_header");
		else
		{
			const unsigned frames = ZSTD_seekable_getNumFrames(decoder);
			if (frames == 0)
				status = ImageError(error, "empty_recording");
			else
				archive->rawBytes = ZSTD_seekable_getFrameDecompressedOffset(decoder, frames - 1) +
				                    ZSTD_seekable_getFrameDecompressedSize(decoder, frames - 1);
			if (status == ArenaStatus_Ok)
				status = ReadBytes(archive, 0, header.data(), 64, error);
		}
	}
	if (status == ArenaStatus_Ok)
	{
		std::memcpy(archive->engine.data(), reinterpret_cast<const char*>(header.data()) + 32, 32);
		if (header[0] != 0x31485452 || header[1] != 1 || header[2] != threads || header[3] != repeat ||
		    header[4] == 0 || header[4] > kWidth || header[5] == 0 || header[5] > kHeight || header[6] > 48 ||
		    header[7] != 1 || archive->engine.back() != 0 || std::string_view(archive->engine.data()) != engine ||
		    archive->rawBytes > kCorpusByteLimit)
			status = ImageError(error, "recording_identity_or_completion");
		else
		{
			archive->threads = threads;
			archive->repeat = repeat;
			archive->width = header[4];
			archive->height = header[5];
			status = ReadChunkIndex(archive, header[6], error);
		}
	}
	if (status != ArenaStatus_Ok)
		CloseRayImages(archive);
	return status;
}

ArenaStatus ReadRayImageChunk(RayImageArchive* archive, std::uint32_t view, benchmark_ray::Phase phase,
                              benchmark_ray::Api api, std::vector<benchmark_ray::Output>* outputs,
                              std::vector<benchmark_ray::Hit>* hits, StatusRecord* error)
{
	using namespace benchmark_ray;
	for (const RayImageChunk& chunk : archive->chunks)
		if (chunk.view == view && chunk.phase == phase && chunk.api == api)
		{
			outputs->resize(chunk.rayCount);
			hits->resize(chunk.hitCount);
			if (ReadBytes(archive, chunk.offset, outputs->data(), outputs->size() * sizeof(Output), error) !=
			        ArenaStatus_Ok ||
			    ReadBytes(archive, chunk.offset + outputs->size() * sizeof(Output), hits->data(),
			              hits->size() * sizeof(Hit), error) != ArenaStatus_Ok)
				return error->code;
			for (const Output& output : *outputs)
				if (output.written != 1 || output.status != Status_Ok || output.first > hits->size() ||
				    output.count > hits->size() - output.first || output.count > 1)
					return ImageError(error, "unwritten_or_invalid_image_output");
			for (const Output& output : *outputs)
				if (output.count != 0 && IsAny(phase) == 0 && !FiniteHit((*hits)[output.first]))
					return ImageError(error, "nonfinite_image_hit");
			return ArenaStatus_Ok;
		}
	return ImageError(error, "image_unavailable");
}

ArenaStatus ValidateRayImages(RayImageArchive* archive, StatusRecord* error)
{
	std::vector<benchmark_ray::Output> outputs;
	std::vector<benchmark_ray::Hit> hits;
	for (const RayImageChunk& chunk : archive->chunks)
	{
		if (ReadRayImageChunk(archive, chunk.view, chunk.phase, chunk.api, &outputs, &hits, error) != ArenaStatus_Ok)
			return error->code;
		if ((chunk.phase == benchmark_ray::Phase_Primary || chunk.phase == benchmark_ray::Phase_Updated) &&
		    chunk.rayCount != archive->width * archive->height)
			return ImageError(error, "primary_image_coverage");
	}
	return ArenaStatus_Ok;
}

ArenaStatus CompressRayImages(const std::filesystem::path& source, const std::filesystem::path& target,
                              std::string_view engine, std::uint32_t threads, std::uint32_t repeat,
                              const std::atomic<std::uint32_t>* cancellation, StatusRecord* error)
{
	RayImageArchive archive = {};
	ArenaStatus status = OpenRayImages(source, engine, threads, repeat, &archive, error);
	if (status == ArenaStatus_Ok)
		status = ValidateRayImages(&archive, error);
	if (status != ArenaStatus_Ok)
	{
		CloseRayImages(&archive);
		return status;
	}
	if (archive.decoder != nullptr)
	{
		CloseRayImages(&archive);
		return ImageError(error, "already_compressed_spool");
	}
	FILE* input = static_cast<FILE*>(archive.file);
	if (_fseeki64(input, 0, SEEK_SET) != 0)
	{
		CloseRayImages(&archive);
		return ImageError(error, "spool_seek");
	}
	const std::filesystem::path partial = target.wstring() + L".partial";
	FILE* output = nullptr;
	if (_wfopen_s(&output, partial.c_str(), L"wbx") != 0 || output == nullptr)
	{
		CloseRayImages(&archive);
		return ImageError(error, "compression_output");
	}
	ZSTD_seekable_CStream* encoder = ZSTD_seekable_createCStream();
	if (encoder == nullptr || ZSTD_isError(ZSTD_seekable_initCStream(encoder, 3, 1, 1024 * 1024)))
		status = ImageError(error, "compression_init");
	std::vector<std::uint8_t> inputBytes(1024 * 1024), outputBytes(2 * 1024 * 1024);
	std::uint64_t consumed = 0;
	while (status == ArenaStatus_Ok && consumed < archive.rawBytes)
	{
		if (cancellation != nullptr && cancellation->load(std::memory_order_acquire) != 0)
		{
			status = ImageError(error, "compression_cancelled", ArenaStatus_Interrupted);
			break;
		}
		const std::size_t count =
		    static_cast<std::size_t>(std::min<std::uint64_t>(inputBytes.size(), archive.rawBytes - consumed));
		if (std::fread(inputBytes.data(), 1, count, input) != count)
		{
			status = ImageError(error, "spool_read");
			break;
		}
		ZSTD_inBuffer from = {inputBytes.data(), count, 0};
		while (from.pos < from.size)
		{
			ZSTD_outBuffer to = {outputBytes.data(), outputBytes.size(), 0};
			const std::size_t encoded = ZSTD_seekable_compressStream(encoder, &to, &from);
			if (ZSTD_isError(encoded) || std::fwrite(to.dst, 1, to.pos, output) != to.pos)
			{
				status = ImageError(error, "compression_write");
				break;
			}
		}
		consumed += count;
	}
	std::size_t remaining = 1;
	while (status == ArenaStatus_Ok && remaining != 0)
	{
		ZSTD_outBuffer to = {outputBytes.data(), outputBytes.size(), 0};
		remaining = ZSTD_seekable_endStream(encoder, &to);
		if (ZSTD_isError(remaining) || std::fwrite(to.dst, 1, to.pos, output) != to.pos)
			status = ImageError(error, "compression_finish");
	}
	if (encoder != nullptr)
		ZSTD_seekable_freeCStream(encoder);
	if (std::fclose(output) != 0 && status == ArenaStatus_Ok)
		status = ImageError(error, "compression_close");
	CloseRayImages(&archive);
	if (status != ArenaStatus_Ok)
		return status;
	std::error_code filesystemError;
	std::filesystem::rename(partial, target, filesystemError);
	if (filesystemError)
		return ImageError(error, "compression_publish");
	status = OpenRayImages(target, engine, threads, repeat, &archive, error);
	CloseRayImages(&archive);
	if (status != ArenaStatus_Ok)
		return status;
	std::filesystem::remove(source, filesystemError);
	return filesystemError ? ImageError(error, "compression_spool_cleanup") : ArenaStatus_Ok;
}

namespace
{
Vector ImageAdd(Vector a, Vector b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vector ImageMul(Vector a, float scale)
{
	return {a.x * scale, a.y * scale, a.z * scale};
}
float ImageDot(Vector a, Vector b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vector Albedo(std::uint32_t id)
{
	return {.25f + .65f * static_cast<float>((id * 37) % 251) / 250.f,
	        .25f + .65f * static_cast<float>((id * 67) % 251) / 250.f,
	        .25f + .65f * static_cast<float>((id * 97) % 251) / 250.f};
}
std::uint32_t Pixel(Vector color)
{
	const std::uint32_t r = static_cast<std::uint32_t>(std::clamp(color.x, 0.f, 1.f) * 255.f);
	const std::uint32_t g = static_cast<std::uint32_t>(std::clamp(color.y, 0.f, 1.f) * 255.f);
	const std::uint32_t b = static_cast<std::uint32_t>(std::clamp(color.z, 0.f, 1.f) * 255.f);
	return r | (g << 8) | (b << 16) | 0xff000000u;
}
struct SecondaryImage
{
	std::vector<float> shadows, ambient;
	std::vector<Vector> reflection;
	std::vector<std::uint8_t> ambientCount, errors;
};
ArenaStatus ReadSecondary(const std::filesystem::path& corpus, RayImageArchive* archive, std::uint32_t view,
                          Phase phase, Api api, SecondaryImage* secondary, PresenceStatus* available,
                          StatusRecord* error)
{
	*available = PresenceStatus_Absent;
	const RayImageChunk* selected = nullptr;
	for (const RayImageChunk& chunk : archive->chunks)
		if (chunk.view == view && chunk.phase == phase && chunk.api == api)
			selected = &chunk;
	if (selected == nullptr)
		return ArenaStatus_Ok;
	CorpusPhase inputs;
	if (ReadPhase(PhasePath(corpus, view, phase), &inputs) != Status_Ok || inputs.view != view ||
	    inputs.phase != phase || inputs.width != archive->width || inputs.height != archive->height)
		return ImageError(error, "secondary_corpus");
	std::vector<Output> outputs;
	std::vector<Hit> hits;
	if (ReadRayImageChunk(archive, view, phase, api, &outputs, &hits, error) != ArenaStatus_Ok)
		return error->code;
	if (outputs.size() != inputs.rays.size())
		return ImageError(error, "secondary_recording_count");
	for (std::size_t index = 0; index < outputs.size(); ++index)
	{
		const std::uint32_t pixel = inputs.rays[index].pixel;
		if (pixel >= secondary->shadows.size())
			return ImageError(error, "secondary_pixel");
		const Output& output = outputs[index];
		const Range expected = inputs.expected[index];
		int valid = output.count == (expected.count == 0 ? 0u : 1u);
		if (valid != 0 && output.count != 0 && IsAny(phase) == 0)
		{
			valid = 0;
			for (std::uint32_t candidate = 0; candidate < expected.count; ++candidate)
				valid |= MatchingHit(hits[output.first], inputs.hits[expected.first + candidate]);
		}
		secondary->errors[pixel] |= valid == 0;
		if (phase == Phase_Shadow)
			secondary->shadows[pixel] = output.count == 0 ? 1.f : .12f;
		else if (phase == Phase_Ambient)
		{
			secondary->ambient[pixel] += output.count == 0 ? 1.f : 0.f;
			secondary->ambientCount[pixel] += 1;
		}
		else if (phase == Phase_Reflection && output.count != 0)
			secondary->reflection[pixel] = Albedo(hits[output.first].collider);
	}
	*available = PresenceStatus_Present;
	return ArenaStatus_Ok;
}
}

ArenaStatus ComposeRayImage(const std::filesystem::path& corpus, RayImageArchive* archive, std::uint32_t view,
                            benchmark_ray::Phase phase, benchmark_ray::Api api, RayImageChannel channel,
                            RayImage* image, StatusRecord* error)
{
	using namespace benchmark_ray;
	*image = {};
	if (view >= 6 || (phase != Phase_Primary && phase != Phase_Updated) || api >= Api_Count ||
	    channel > RayImageChannel_Errors)
		return ImageError(error, "image_selection");
	Scene scene;
	CorpusPhase inputs;
	if (ReadScene(corpus / "scene.rtc", &scene) != Status_Ok ||
	    ReadPhase(PhasePath(corpus, view, phase), &inputs) != Status_Ok || inputs.view != view ||
	    inputs.phase != phase || inputs.width != archive->width || inputs.height != archive->height)
		return ImageError(error, "image_corpus");
	std::vector<Output> outputs;
	std::vector<Hit> hits;
	if (ReadRayImageChunk(archive, view, phase, api, &outputs, &hits, error) != ArenaStatus_Ok)
		return error->code;
	if (outputs.size() != inputs.rays.size() ||
	    outputs.size() != static_cast<std::size_t>(archive->width) * archive->height)
		return ImageError(error, "image_primary_count");
	std::vector<std::uint8_t> pixels(outputs.size(), 0);
	for (const Ray& ray : inputs.rays)
		if (ray.pixel >= pixels.size() || pixels[ray.pixel]++ != 0)
			return ImageError(error, "primary_pixel_coverage");
	const Validation validation = ValidateOutputs(inputs, outputs, hits);
	image->width = archive->width;
	image->height = archive->height;
	image->queries = outputs.size();
	image->errors = validation.errors;
	image->camera = scene.cameras[view];
	image->rgba.resize(outputs.size());
	SecondaryImage secondary;
	if ((channel == RayImageChannel_Shaded || channel == RayImageChannel_Errors) && phase == Phase_Primary)
	{
		secondary.shadows.assign(outputs.size(), 1);
		secondary.ambient.assign(outputs.size(), 0);
		secondary.reflection.resize(outputs.size());
		secondary.ambientCount.assign(outputs.size(), 0);
		secondary.errors.assign(outputs.size(), 0);
		if (ReadSecondary(corpus, archive, view, Phase_Shadow, api, &secondary, &image->shadows, error) !=
		        ArenaStatus_Ok ||
		    ReadSecondary(corpus, archive, view, Phase_Reflection, api, &secondary, &image->reflections, error) !=
		        ArenaStatus_Ok ||
		    ReadSecondary(corpus, archive, view, Phase_Ambient, api, &secondary, &image->ambient, error) !=
		        ArenaStatus_Ok)
			return error->code;
	}
	for (std::size_t index = 0; index < outputs.size(); ++index)
	{
		const Ray& ray = inputs.rays[index];
		const Output& output = outputs[index];
		const Range expected = inputs.expected[index];
		if (ray.pixel >= image->rgba.size())
			return ImageError(error, "primary_pixel");
		Vector color = {.055f, .075f, .12f};
		int matches = output.count == 0 && expected.count == 0;
		if (output.count != 0)
		{
			const Hit& hit = hits[output.first];
			for (std::uint32_t candidate = 0; candidate < expected.count; ++candidate)
				matches |= MatchingHit(hit, inputs.hits[expected.first + candidate]);
			if (channel == RayImageChannel_Depth)
			{
				const float shade = 1.f / (1.f + static_cast<float>(hit.distance) * .035f);
				color = {shade, shade, shade};
			}
			else if (channel == RayImageChannel_Normals)
				color = ImageAdd(ImageMul(hit.normal, .5f), {.5f, .5f, .5f});
			else if (channel == RayImageChannel_Shaded)
			{
				const Vector point =
				    ImageAdd(ray.origin, ImageMul(ray.translation, static_cast<float>(hit.distance / ray.length)));
				const Vector light = {scene.light.x - point.x, scene.light.y - point.y, scene.light.z - point.z};
				const float diffuse = std::max(0.f, ImageDot(hit.normal, light) / std::sqrt(ImageDot(light, light)));
				float shadow = 1, ambient = 1;
				if (!secondary.shadows.empty())
				{
					shadow = secondary.shadows[ray.pixel];
					const std::uint32_t x = (ray.pixel % image->width) & ~1u, y = (ray.pixel / image->width) & ~1u;
					const std::uint32_t sample = y * image->width + x;
					if (secondary.ambientCount[sample] != 0)
						ambient = secondary.ambient[sample] / secondary.ambientCount[sample];
				}
				color = ImageMul(Albedo(hit.collider), (.15f + .85f * diffuse * shadow) * (.35f + .65f * ambient));
				if (!secondary.reflection.empty())
					color = ImageAdd(ImageMul(color, .85f), ImageMul(secondary.reflection[ray.pixel], .15f));
			}
		}
		if (!secondary.errors.empty() && secondary.errors[ray.pixel] != 0)
		{
			matches = 0;
			image->errors += 1;
		}
		if (channel == RayImageChannel_Errors)
			color = matches != 0 ? Vector{.08f, .35f, .16f} : Vector{1, 0, .1f};
		else if (matches == 0)
			color = ImageAdd(ImageMul(color, .35f), {.65f, 0, 0});
		image->rgba[ray.pixel] = Pixel(color);
	}
	return ArenaStatus_Ok;
}
}
