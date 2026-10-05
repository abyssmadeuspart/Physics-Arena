#include "json_contracts_internal.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwchar>
#include <exception>
#include <string_view>

namespace physics_arena
{
std::string_view CatalogTextView(const Catalog* catalog, CatalogText text)
{
	if (catalog == nullptr || text.offset > catalog->textArenaUsed || text.size > catalog->textArenaUsed - text.offset)
		return {};
	return std::string_view(catalog->textArena.data() + text.offset, text.size);
}
namespace
{
constexpr std::size_t kJsonPathCapacity = 4096;
constexpr std::size_t kJsonFileCapacity = 262144;

template <std::size_t Capacity>
ArenaStatus CopyText(std::array<char, Capacity>* destination, std::uint32_t* size, std::string_view source)
{
	if (source.empty() || source.size() >= Capacity)
		return ArenaStatus_InvalidResult;
	std::fill(destination->begin(), destination->end(), '\0');
	std::copy(source.begin(), source.end(), destination->begin());
	*size = static_cast<std::uint32_t>(source.size());
	return ArenaStatus_Ok;
}

} // namespace

ArenaStatus SetErrorParts(StatusRecord* error, std::initializer_list<std::string_view> parts)
{
	*error = {};
	CopyText(&error->component, &error->componentSize, "manifest_validation");
	CopyText(&error->status, &error->statusSize, "invalid_result");
	for (const std::string_view part : parts)
	{
		if (part.size() > error->detail.size() - error->detailSize)
		{
			CopyText(&error->detail, &error->detailSize, "validation_detail_exceeded_capacity");
			error->code = ArenaStatus_InvalidResult;
			return ArenaStatus_InvalidResult;
		}
		std::copy(part.begin(), part.end(), error->detail.begin() + error->detailSize);
		error->detailSize += static_cast<std::uint32_t>(part.size());
	}
	error->code = ArenaStatus_InvalidResult;
	return ArenaStatus_InvalidResult;
}

ArenaStatus SetError(StatusRecord* error, std::string_view detail)
{
	return SetErrorParts(error, {detail});
}

ArenaStatus StoreText(Catalog* catalog, CatalogText* destination, std::string_view source)
{
	if (source.empty() || source.size() > UINT32_MAX ||
	    catalog->textArenaUsed + source.size() > catalog->textArena.size())
		return ArenaStatus_InvalidResult;
	destination->offset = catalog->textArenaUsed;
	destination->size = static_cast<std::uint32_t>(source.size());
	std::copy(source.begin(), source.end(), catalog->textArena.begin() + catalog->textArenaUsed);
	catalog->textArenaUsed += destination->size;
	return ArenaStatus_Ok;
}

ArenaStatus LoadDocument(const wchar_t* repositoryRoot, std::string_view relative, OrderedJson* document,
                         StatusRecord* error)
{
	std::array<wchar_t, kJsonPathCapacity> path = {};
	const std::size_t rootSize = std::wcslen(repositoryRoot);
	if (rootSize + relative.size() + 2 >= path.size())
		return SetErrorParts(error, {"json_path_capacity path=", relative});
	std::copy(repositoryRoot, repositoryRoot + rootSize, path.begin());
	std::uint32_t pathSize = static_cast<std::uint32_t>(rootSize);
	if (pathSize != 0 && path[pathSize - 1] != L'/' && path[pathSize - 1] != L'\\')
		path[pathSize++] = L'\\';
	const int written =
	    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, relative.data(), static_cast<int>(relative.size()),
		                    path.data() + pathSize, static_cast<int>(path.size() - pathSize - 1));
	if (written <= 0)
		return SetErrorParts(error, {"json_path_encoding path=", relative});
	path[pathSize + static_cast<std::uint32_t>(written)] = L'\0';
	HANDLE file =
	    CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return SetErrorParts(error, {"missing_json path=", relative});
	LARGE_INTEGER size = {};
	if (GetFileSizeEx(file, &size) == 0 || size.QuadPart <= 0 ||
	    size.QuadPart > static_cast<LONGLONG>(kJsonFileCapacity))
	{
		CloseHandle(file);
		return SetErrorParts(error, {"json_file_capacity path=", relative});
	}
	std::array<char, kJsonFileCapacity> bytes = {};
	DWORD read = 0;
	const DWORD expected = static_cast<DWORD>(size.QuadPart);
	const int readStatus = ReadFile(file, bytes.data(), expected, &read, nullptr) != 0 ? 1 : 0;
	CloseHandle(file);
	if (readStatus == 0 || read != expected)
		return SetErrorParts(error, {"json_read_failed path=", relative});
	*document = OrderedJson::parse(bytes.begin(), bytes.begin() + read);
	if (!document->is_object())
		return SetErrorParts(error, {"object_required location=", relative});
	return ArenaStatus_Ok;
}

ArenaStatus ValidateKeys(const OrderedJson& object, std::initializer_list<std::string_view> keys,
                         std::string_view location, StatusRecord* error)
{
	if (!object.is_object() || object.size() != keys.size())
		return SetErrorParts(error, {"object_key_count location=", location});
	for (OrderedJson::const_iterator item = object.begin(); item != object.end(); ++item)
		if (std::find(keys.begin(), keys.end(), item.key()) == keys.end())
			return SetErrorParts(error, {"unknown_key location=", location, " key=", item.key()});
	return ArenaStatus_Ok;
}

ArenaStatus ValidateSchema(const OrderedJson& document, std::uint32_t expected, std::string_view location,
                           StatusRecord* error)
{
	OrderedJson::const_iterator schema = document.find("schema_version");
	if (schema == document.end() || !schema->is_number_unsigned() || schema->get<std::uint64_t>() != expected)
		return SetErrorParts(error, {"schema_version=invalid location=", location});
	return ArenaStatus_Ok;
}

ArenaStatus RequiredString(const OrderedJson& object, const char* key, std::string_view location,
                           std::string_view* output, StatusRecord* error)
{
	OrderedJson::const_iterator item = object.find(key);
	if (item == object.end() || !item->is_string() || item->get_ref<const OrderedJson::string_t&>().empty() ||
	    std::all_of(item->get_ref<const OrderedJson::string_t&>().begin(),
	                item->get_ref<const OrderedJson::string_t&>().end(),
	                [](char value)
	                {
		                return std::isspace(static_cast<unsigned char>(value)) != 0;
	                }))
		return SetErrorParts(error, {"string_required location=", location, " key=", key});
	*output = item->get_ref<const OrderedJson::string_t&>();
	return ArenaStatus_Ok;
}

ArenaStatus RequiredUnsigned(const OrderedJson& object, const char* key, std::string_view location,
                             std::uint32_t minimum, std::uint32_t* output, StatusRecord* error)
{
	OrderedJson::const_iterator item = object.find(key);
	if (item == object.end() || !item->is_number_unsigned() || item->get<std::uint64_t>() < minimum ||
	    item->get<std::uint64_t>() > UINT32_MAX)
		return SetErrorParts(error, {"unsigned_required location=", location, " key=", key});
	*output = item->get<std::uint32_t>();
	return ArenaStatus_Ok;
}

ArenaStatus SafeRelativePath(std::string_view path)
{
	if (path.empty() || path.front() == '/' || path.front() == '\\' ||
	    (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) != 0 && path[1] == ':'))
		return ArenaStatus_InvalidResult;
	std::size_t partStart = 0;
	for (std::size_t index = 0; index <= path.size(); ++index)
	{
		if (index != path.size() && path[index] != '/' && path[index] != '\\')
			continue;
		const std::string_view part = path.substr(partStart, index - partStart);
		if (part.empty() || part == "." || part == "..")
			return ArenaStatus_InvalidResult;
		partStart = index + 1;
	}
	return ArenaStatus_Ok;
}

RouteStatus ParseRouteStatus(std::string_view status)
{
	if (status == "supported")
		return RouteStatus_Supported;
	if (status == "working")
		return RouteStatus_Working;
	if (status == "experimental")
		return RouteStatus_Experimental;
	if (status == "unsupported")
		return RouteStatus_Unsupported;
	if (status == "route_unavailable")
		return RouteStatus_Unavailable;
	if (status == "tool_missing")
		return RouteStatus_ToolMissing;
	if (status == "ref_mismatch")
		return RouteStatus_ReferenceMismatch;
	if (status == "fetch_failed")
		return RouteStatus_FetchFailed;
	if (status == "unsupported_platform")
		return RouteStatus_UnsupportedPlatform;
	return RouteStatus_Unknown;
}

ArenaStatus EngineIndex(const Catalog& catalog, std::string_view id, std::uint32_t* index)
{
	for (std::uint32_t candidate = 0; candidate < catalog.engineCount; ++candidate)
	{
		if (CatalogTextView(&catalog, catalog.engines[candidate].id) != id)
			continue;
		*index = candidate;
		return ArenaStatus_Ok;
	}
	return ArenaStatus_InvalidResult;
}

ArenaStatus CaseIndex(const Catalog& catalog, std::string_view id, std::uint32_t* index)
{
	for (std::uint32_t candidate = 0; candidate < catalog.caseCount; ++candidate)
	{
		if (CatalogTextView(&catalog, catalog.cases[candidate].id) != id)
			continue;
		*index = candidate;
		return ArenaStatus_Ok;
	}
	return ArenaStatus_InvalidResult;
}

PresenceStatus ValidWireId(std::string_view id)
{
	if (id.empty() || id.front() < 'a' || id.front() > 'z')
		return PresenceStatus_Absent;
	for (const char value : id)
		if ((value < 'a' || value > 'z') && (value < '0' || value > '9') && value != '_')
			return PresenceStatus_Absent;
	return PresenceStatus_Present;
}

int CaseSupportsThreadCount(const Catalog* catalog, std::uint32_t caseIndex, std::uint32_t threadCount)
{
	if (catalog == nullptr || caseIndex >= catalog->caseCount || threadCount == 0)
		return -1;
	switch (catalog->cases[caseIndex].fixtureKind)
	{
	case CaseFixtureKind_OpenContainerFallingPile:
	case CaseFixtureKind_BoxContactIslands:
	case CaseFixtureKind_SpatialQueryTrace:
	case CaseFixtureKind_RagdollStairTumble:
	case CaseFixtureKind_LargePyramid:
	case CaseFixtureKind_PyramidWall:
	case CaseFixtureKind_RayTracing:
		return 1;
	default:
		return -1;
	}
}

ArenaStatus LoadCatalog(const wchar_t* repositoryRoot, Catalog* catalog, StatusRecord* error)
{
	*catalog = {};
	*error = {};
	try
	{
		OrderedJson cases;
		OrderedJson engines;
		OrderedJson reports;
		if (LoadDocument(repositoryRoot, "config/cases.json", &cases, error) != ArenaStatus_Ok ||
		    LoadDocument(repositoryRoot, "config/engines.json", &engines, error) != ArenaStatus_Ok ||
		    LoadDocument(repositoryRoot, "config/reports.json", &reports, error) != ArenaStatus_Ok ||
		    ParseCases(cases, catalog, error) != ArenaStatus_Ok ||
		    ParseEngines(engines, catalog, error) != ArenaStatus_Ok ||
		    LoadEngineSettings(repositoryRoot, catalog, error) != ArenaStatus_Ok ||
		    ParseReports(reports, catalog, error) != ArenaStatus_Ok)
		{
			*catalog = {};
			return ArenaStatus_InvalidResult;
		}
		return ArenaStatus_Ok;
	}
	catch (const std::exception& exception)
	{
		*catalog = {};
		return SetErrorParts(error, {"json_parse error=", exception.what()});
	}
}

} // namespace physics_arena
