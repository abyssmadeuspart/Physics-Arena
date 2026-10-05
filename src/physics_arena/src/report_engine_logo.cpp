#include "report_pipeline_internal.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace physics_arena
{
namespace
{
struct EngineLogoRecord
{
	std::string_view engineId;
	const wchar_t* relativePath;
	std::string_view mediaType;
	std::string_view viewBox;
	double sourceWidth;
	double sourceHeight;
	double useWidthRatio;
};

constexpr std::array<EngineLogoRecord, 11> kEngineLogos = {{
    {"avian3d", L"assets/engine-icons/Avian-Logo.svg", "image/svg+xml", "0 0 497 182", 497.0, 182.0, 1.60},
    {"bepuphysics2", L"assets/engine-icons/BepuPhysics-2-Logo.png", "image/png", "5 46 248 162", 256.0, 256.0, 1.53},
    {"box3d", L"assets/engine-icons/Box3D-Logo.svg", "image/svg+xml", "0 0 23.160606 19.770294", 23.160606, 19.770294,
	 1.17},
    {"joltphysics", L"assets/engine-icons/Jolt-Physics-Logo.png", "image/png", "0 0 100 100", 100.0, 100.0, 1.00},
    {"nvidia_physx34", L"assets/engine-icons/PhysX-3.4-Logo.jpg", "image/jpeg", "0 0 450 427", 450.0, 427.0, 1.00},
    {"nvidia_physx5", L"assets/engine-icons/PhysX-5.6.1-Logo.png", "image/png", "0 0 960 339", 960.0, 339.0, 1.00},
    {"physx34", L"assets/engine-icons/Vite-Stuido-Logo.png", "image/png", "0 0 524 400", 524.0, 400.0, 1.31},
    {"rapier3d", L"assets/engine-icons/Rapier-Logo.report.png", "image/png", "0 0 101 96", 101.0, 96.0, 1.05},
    {"unity_physics", L"assets/engine-icons/Unity-Logo.png", "image/png", "0 0 512 512", 512.0, 512.0, 1.00},
    {"unreal_chaos", L"assets/engine-icons/Unreal-Chaos-Logo.svg", "image/svg+xml", "16.51 8.48 642.82 525.02", 642.82,
	 525.02, 1.22},
    {"entasis", L"src/entasis/assets/entasis_icon_full_black.png", "image/png", "0 0 1000 1000", 1000.0, 1000.0, 1.00},
}};

const EngineLogoRecord* FindEngineLogo(const ResultViewModel* model, std::uint32_t engineOrdinal)
{
	const std::string_view engineId = ResultViewTextView(model, model->engines[engineOrdinal].id);
	for (const EngineLogoRecord& logo : kEngineLogos)
		if (logo.engineId == engineId)
			return &logo;
	return nullptr;
}

ArenaStatus BuildEngineLogoPath(const wchar_t* repositoryRoot, const EngineLogoRecord& logo,
                                std::array<wchar_t, kReportPathCapacity>* path, StatusRecord* error)
{
	if (CopyWidePath(path, repositoryRoot) != ArenaStatus_Ok ||
	    AppendWideChild(path, logo.relativePath) != ArenaStatus_Ok)
		return ReportError(error, ArenaStatus_InvalidResult, "report_engine_logo_path");
	return ArenaStatus_Ok;
}

ArenaStatus WriteBase64File(ReportWriter* writer, const wchar_t* path, StatusRecord* error)
{
	HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
	                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return ReportError(error, ArenaStatus_InvalidResult, "report_engine_logo_open");
	constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::array<std::uint8_t, 3074> input = {};
	std::array<char, 4100> output = {};
	std::uint32_t retained = 0;
	std::uint64_t totalRead = 0;
	ArenaStatus status = ArenaStatus_Ok;
	for (;;)
	{
		DWORD read = 0;
		if (ReadFile(file, input.data() + retained, 3072, &read, nullptr) == 0)
		{
			status = ReportError(error, ArenaStatus_InvalidResult, "report_engine_logo_read");
			break;
		}
		totalRead += read;
		const std::uint32_t available = retained + read;
		const std::uint32_t complete = available - available % 3;
		std::uint32_t encoded = 0;
		for (std::uint32_t index = 0; index < complete; index += 3)
		{
			const std::uint32_t value = (static_cast<std::uint32_t>(input[index]) << 16) |
			                            (static_cast<std::uint32_t>(input[index + 1]) << 8) |
			                            static_cast<std::uint32_t>(input[index + 2]);
			output[encoded++] = alphabet[(value >> 18) & 63];
			output[encoded++] = alphabet[(value >> 12) & 63];
			output[encoded++] = alphabet[(value >> 6) & 63];
			output[encoded++] = alphabet[value & 63];
		}
		if (encoded != 0 && WriteReport(writer, std::string_view(output.data(), encoded), error) != ArenaStatus_Ok)
		{
			status = error->code;
			break;
		}
		retained = available - complete;
		std::copy(input.begin() + complete, input.begin() + available, input.begin());
		if (read == 0)
			break;
	}
	if (status == ArenaStatus_Ok && totalRead == 0)
		status = ReportError(error, ArenaStatus_InvalidResult, "report_engine_logo_empty");
	if (status == ArenaStatus_Ok && retained != 0)
	{
		const std::uint32_t value = (static_cast<std::uint32_t>(input[0]) << 16) |
		                            (retained == 2 ? static_cast<std::uint32_t>(input[1]) << 8 : 0);
		output[0] = alphabet[(value >> 18) & 63];
		output[1] = alphabet[(value >> 12) & 63];
		output[2] = retained == 2 ? alphabet[(value >> 6) & 63] : '=';
		output[3] = '=';
		if (WriteReport(writer, std::string_view(output.data(), 4), error) != ArenaStatus_Ok)
			status = error->code;
	}
	CloseHandle(file);
	return status;
}
}

ArenaStatus WriteEngineLogoDefinitions(const wchar_t* repositoryRoot, ReportWriter* writer,
                                       const ResultViewModel* model, StatusRecord* error)
{
	for (std::uint32_t ordinal = 0; ordinal < model->engineCount; ++ordinal)
	{
		const EngineLogoRecord* logo = FindEngineLogo(model, ordinal);
		if (logo == nullptr)
			return ReportError(error, ArenaStatus_InvalidResult, "report_engine_logo_metadata");
		std::array<wchar_t, kReportPathCapacity> path = {};
		if (BuildEngineLogoPath(repositoryRoot, *logo, &path, error) != ArenaStatus_Ok)
			return error->code;
		if (WriteFormat(writer, error,
		                "<symbol id=\"engine-logo-%.*s\" viewBox=\"%.*s\">\n"
		                "<image href=\"data:%.*s;base64,",
		                static_cast<int>(logo->engineId.size()), logo->engineId.data(),
		                static_cast<int>(logo->viewBox.size()), logo->viewBox.data(),
		                static_cast<int>(logo->mediaType.size()), logo->mediaType.data()) != ArenaStatus_Ok ||
		    WriteBase64File(writer, path.data(), error) != ArenaStatus_Ok ||
		    WriteFormat(writer, error,
		                "\" x=\"0\" y=\"0\" width=\"%.6g\" height=\"%.6g\" "
		                "preserveAspectRatio=\"xMidYMid meet\"/>\n</symbol>\n",
		                logo->sourceWidth, logo->sourceHeight) != ArenaStatus_Ok)
			return error->code;
	}
	return ArenaStatus_Ok;
}

ArenaStatus WriteEngineLogo(ReportWriter* writer, const ResultViewModel* model, std::uint32_t engineOrdinal, double x,
                            double centerY, double size, double* width, StatusRecord* error)
{
	const EngineLogoRecord* logo = FindEngineLogo(model, engineOrdinal);
	if (logo == nullptr)
		return ReportError(error, ArenaStatus_InvalidResult, "report_engine_logo_metadata");
	*width = size * logo->useWidthRatio;
	return WriteFormat(
	    writer, error, "<use href=\"#engine-logo-%.*s\" x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\"/>\n",
	    static_cast<int>(logo->engineId.size()), logo->engineId.data(), x, centerY - size / 2.0, *width, size);
}
}
