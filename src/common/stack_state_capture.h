#pragma once

#include "case_execution_wire.h"

#include <array>
#include <chrono>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// Windows segmented-pointer macros conflict with fixture identifiers
#undef near
#undef far
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace benchmark_stack
{
constexpr std::uint32_t kHeaderBytes = 236;
constexpr std::uint32_t kFrameHeaderBytes = 12;
constexpr std::uint32_t kPoseBytes = 32;
constexpr std::uint32_t kFooterBytes = 16;
constexpr char kMagic[8] = {'B', 'P', 'S', 'T', 'A', 'C', 'K', '\0'};

enum Phase : std::uint32_t
{
	Phase_Construction = 0,
	Phase_Warmup = 1,
	Phase_Measured = 2,
	Phase_Complete = 3,
};

struct Pose
{
	std::uint32_t slot;
	CaseExecutionVector3 position;
	CaseExecutionQuaternion orientation;
};

enum Reply : std::uint32_t
{
	Reply_Accepted = 0,
	Reply_Rejected = 1,
};

struct Capture
{
	HANDLE pipe;
	std::vector<Pose> poses;
	std::vector<std::uint8_t> bytes;
	std::chrono::steady_clock::time_point frameStart;
	double elapsedMs;
	std::uint32_t frameCount;
	std::uint32_t segment;
	int status;
};

inline int TargetFixture(CaseFixtureKind kind)
{
	return kind == CaseFixtureKind_OpenContainerFallingPile || kind == CaseFixtureKind_BoxContactIslands || kind == CaseFixtureKind_LargePyramid ||
	       kind == CaseFixtureKind_PyramidWall;
}

inline std::filesystem::path TracePath(const std::filesystem::path& rawOutput, std::uint32_t repeat)
{
	return rawOutput.wstring() + L".r" + std::to_wstring(repeat) + L".stack";
}

inline void EncodeU32(std::uint8_t* bytes, std::uint32_t value)
{
	for (std::uint32_t index = 0; index < 4; ++index)
		bytes[index] = static_cast<std::uint8_t>(value >> (8 * index));
}

inline void EncodeFloat(std::uint8_t* bytes, float value)
{
	std::uint32_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));
	EncodeU32(bytes, bits);
}

inline void EncodeDouble(std::uint8_t* bytes, double value)
{
	std::uint64_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));
	for (std::uint32_t index = 0; index < 8; ++index)
		bytes[index] = static_cast<std::uint8_t>(bits >> (8 * index));
}

inline int ExchangeRecord(Capture* capture, const std::uint8_t* bytes, std::size_t size)
{
	std::size_t offset = 0;
	while (offset < size)
	{
		DWORD written = 0;
		if (WriteFile(capture->pipe, bytes + offset, static_cast<DWORD>(size - offset), &written, nullptr) == 0 || written == 0) return 2;
		offset += written;
	}
	std::array<std::uint8_t, 4> reply = {};
	offset = 0;
	while (offset < reply.size())
	{
		DWORD received = 0;
		if (ReadFile(capture->pipe, reply.data() + offset, static_cast<DWORD>(reply.size() - offset), &received, nullptr) == 0 || received == 0) return 2;
		offset += received;
	}
	return reply == std::array<std::uint8_t, 4>{} ? 0 : 2;
}

inline int OpenCapture(std::string_view endpoint, const CaseExecutionSpec& spec,
	                    std::string_view engine, std::uint32_t threads, std::uint32_t repeat, Capture* capture)
{
	if (TargetFixture(spec.fixtureKind) == 0)
		return 0;
	capture->pipe = CreateFileA(std::string(endpoint).c_str(), GENERIC_WRITE | GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (capture->pipe == INVALID_HANDLE_VALUE)
	{
		capture->pipe = nullptr;
		return capture->status = 2;
	}
	capture->poses.resize(spec.dynamicBodyCount);
	capture->bytes.resize(kFrameHeaderBytes + static_cast<std::size_t>(kPoseBytes) * spec.dynamicBodyCount);
	std::array<std::uint8_t, kHeaderBytes> header = {};
	std::memcpy(header.data(), kMagic, sizeof(kMagic));
	std::memcpy(header.data() + 8, spec.caseId, std::strlen(spec.caseId));
	std::memcpy(header.data() + 72, spec.fixtureSemantic, std::strlen(spec.fixtureSemantic));
	if (engine.empty() || engine.size() >= 64)
		return capture->status = 2;
	std::memcpy(header.data() + 136, engine.data(), engine.size());
	const std::uint32_t boxes = spec.dynamicBodyCount -
	    (spec.fixtureKind == CaseFixtureKind_LargePyramid ? spec.largePyramid.projectileCount : 0);
	const std::uint32_t fields[] = {static_cast<std::uint32_t>(spec.fixtureKind), spec.fixtureRevision,
	    spec.dynamicBodyCount, boxes, threads, repeat, spec.warmupWorkUnitCount, spec.measuredWorkUnitCount, spec.timestepHz};
	for (std::uint32_t index = 0; index < std::size(fields); ++index)
		EncodeU32(header.data() + 200 + 4 * index, fields[index]);
	capture->status = ExchangeRecord(capture, header.data(), header.size());
	return capture->status;
}

inline void BeginFrame(Capture* capture)
{
	capture->frameStart = std::chrono::steady_clock::now();
}

inline void EncodeFrame(Capture* capture, Phase phase, std::uint32_t segment, std::uint32_t step)
{
	EncodeU32(capture->bytes.data(), static_cast<std::uint32_t>(phase));
	EncodeU32(capture->bytes.data() + 4, segment);
	EncodeU32(capture->bytes.data() + 8, step);
	for (std::size_t index = 0; index < capture->poses.size(); ++index)
	{
		const Pose& pose = capture->poses[index];
		std::uint8_t* output = capture->bytes.data() + kFrameHeaderBytes + kPoseBytes * index;
		EncodeU32(output, pose.slot);
		const float values[] = {pose.position.x, pose.position.y, pose.position.z, pose.orientation.x,
		    pose.orientation.y, pose.orientation.z, pose.orientation.w};
		for (std::uint32_t field = 0; field < std::size(values); ++field)
			EncodeFloat(output + 4 + field * 4, values[field]);
	}
}

inline int AppendFrame(Capture* capture, Phase phase, std::uint32_t segment, std::uint32_t step)
{
	if (capture->pipe == nullptr) return capture->status;
	EncodeFrame(capture, phase, segment, step);
	if (capture->status == 0) capture->status = ExchangeRecord(capture, capture->bytes.data(), capture->bytes.size());
	++capture->frameCount;
	capture->elapsedMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - capture->frameStart).count();
	return capture->status;
}

template <typename Transform>
inline int AppendTransforms(Capture* capture, Phase phase, std::uint32_t segment, std::uint32_t step,
	                       const Transform* transforms)
{
	for (std::size_t index = 0; index < capture->poses.size(); ++index)
	{
		const Transform& value = transforms[index];
		capture->poses[index] = {value.stableSlot,
		    {value.transform.positionX, value.transform.positionY, value.transform.positionZ},
		    {value.transform.rotationX, value.transform.rotationY, value.transform.rotationZ, value.transform.rotationW}};
	}
	return AppendFrame(capture, phase, segment, step);
}

inline int CloseCapture(Capture* capture)
{
	if (capture->pipe == nullptr)
		return capture->status;
	std::array<std::uint8_t, kFooterBytes> footer = {};
	EncodeU32(footer.data(), Phase_Complete);
	EncodeU32(footer.data() + 4, capture->frameCount);
	EncodeDouble(footer.data() + 8, capture->elapsedMs);
	if (capture->status == 0) capture->status = ExchangeRecord(capture, footer.data(), footer.size());
	CloseHandle(capture->pipe);
	capture->pipe = nullptr;
	return capture->status;
}
}
