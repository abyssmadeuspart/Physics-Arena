#include "physics_arena/release_contracts.h"

#include <nlohmann/json.hpp>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwchar>
#include <initializer_list>
#include <string_view>

namespace physics_arena
{
namespace
{
using OrderedJson = nlohmann::ordered_json;
constexpr std::size_t kReleasePathCapacity = 4096;
constexpr std::size_t kReleaseFileCapacity = 262144;

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

ArenaStatus ReleaseErrorParts(StatusRecord* error, std::initializer_list<std::string_view> parts)
{
	*error = {};
	CopyText(&error->component, &error->componentSize, "release_manifest");
	CopyText(&error->status, &error->statusSize, "invalid_result");
	for (const std::string_view part : parts)
	{
		if (part.size() > error->detail.size() - error->detailSize)
		{
			CopyText(&error->detail, &error->detailSize, "release_detail_exceeded_capacity");
			error->code = ArenaStatus_InvalidResult;
			return ArenaStatus_InvalidResult;
		}
		std::copy(part.begin(), part.end(), error->detail.begin() + error->detailSize);
		error->detailSize += static_cast<std::uint32_t>(part.size());
	}
	error->code = ArenaStatus_InvalidResult;
	return ArenaStatus_InvalidResult;
}

ArenaStatus StoreText(ReleaseCatalog* catalog, CatalogText* destination, std::string_view source)
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

ArenaStatus RelativePath(std::string_view path)
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

ArenaStatus BuildPath(const wchar_t* repositoryRoot, std::string_view relative,
                      std::array<wchar_t, kReleasePathCapacity>* path)
{
	const std::size_t rootSize = std::wcslen(repositoryRoot);
	if (rootSize + relative.size() + 2 >= path->size())
		return ArenaStatus_InvalidResult;
	std::copy(repositoryRoot, repositoryRoot + rootSize, path->begin());
	std::uint32_t size = static_cast<std::uint32_t>(rootSize);
	if (size != 0 && (*path)[size - 1] != L'/' && (*path)[size - 1] != L'\\')
		(*path)[size++] = L'\\';
	const int written =
	    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, relative.data(), static_cast<int>(relative.size()),
		                    path->data() + size, static_cast<int>(path->size() - size - 1));
	if (written <= 0)
		return ArenaStatus_InvalidResult;
	(*path)[size + static_cast<std::uint32_t>(written)] = L'\0';
	return ArenaStatus_Ok;
}

ArenaStatus LoadDocument(const wchar_t* repositoryRoot, std::string_view relative, OrderedJson* document,
                         StatusRecord* error)
{
	std::array<wchar_t, kReleasePathCapacity> path = {};
	if (RelativePath(relative) != ArenaStatus_Ok || BuildPath(repositoryRoot, relative, &path) != ArenaStatus_Ok)
		return ReleaseErrorParts(error, {"invalid_manifest_path=", relative});
	HANDLE file =
	    CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return ReleaseErrorParts(error, {"missing_file=", relative});
	LARGE_INTEGER size = {};
	if (GetFileSizeEx(file, &size) == 0 || size.QuadPart <= 0 ||
	    size.QuadPart > static_cast<LONGLONG>(kReleaseFileCapacity))
	{
		CloseHandle(file);
		return ReleaseErrorParts(error, {"release_file_capacity=", relative});
	}
	std::array<char, kReleaseFileCapacity> bytes = {};
	DWORD read = 0;
	const DWORD expected = static_cast<DWORD>(size.QuadPart);
	const int readStatus = ReadFile(file, bytes.data(), expected, &read, nullptr) != 0 ? 1 : 0;
	CloseHandle(file);
	if (readStatus == 0 || read != expected)
		return ReleaseErrorParts(error, {"release_read_failed=", relative});
	*document = OrderedJson::parse(bytes.begin(), bytes.begin() + read);
	if (!document->is_object())
		return ReleaseErrorParts(error, {"manifest_type=", relative});
	return ArenaStatus_Ok;
}

ArenaStatus RequiredString(const OrderedJson& object, const char* key, std::string_view location,
                           std::string_view* output, StatusRecord* error)
{
	OrderedJson::const_iterator item = object.find(key);
	if (item == object.end() || !item->is_string() || item->get_ref<const OrderedJson::string_t&>().empty())
		return ReleaseErrorParts(error, {key, "_type manifest=", location});
	*output = item->get_ref<const OrderedJson::string_t&>();
	return ArenaStatus_Ok;
}

ArenaStatus ValidateKeys(const OrderedJson& object, std::initializer_list<std::string_view> keys,
                         std::string_view location, StatusRecord* error)
{
	if (!object.is_object() || object.size() != keys.size())
		return ReleaseErrorParts(error, {"object_key_count manifest=", location});
	for (OrderedJson::const_iterator item = object.begin(); item != object.end(); ++item)
		if (std::find(keys.begin(), keys.end(), item.key()) == keys.end())
			return ReleaseErrorParts(error, {"unknown_key manifest=", location, " key=", item.key()});
	return ArenaStatus_Ok;
}

ArenaStatus ValidateSchema(const OrderedJson& document, std::string_view location, StatusRecord* error)
{
	OrderedJson::const_iterator schema = document.find("schema_version");
	if (schema == document.end() || !schema->is_number_unsigned() || schema->get<std::uint64_t>() != 3)
		return ReleaseErrorParts(error, {"schema_version manifest=", location});
	return ArenaStatus_Ok;
}

ArenaStatus ValidateExecutable(const wchar_t* repositoryRoot, std::string_view relative, std::string_view manifest,
                               StatusRecord* error)
{
	std::array<wchar_t, kReleasePathCapacity> path = {};
	if (RelativePath(relative) != ArenaStatus_Ok || BuildPath(repositoryRoot, relative, &path) != ArenaStatus_Ok)
		return ReleaseErrorParts(error, {"invalid_executable_path manifest=", manifest});
	const DWORD attributes = GetFileAttributesW(path.data());
	if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		return ReleaseErrorParts(error, {"missing_file=", relative, " manifest=", manifest});
	return ArenaStatus_Ok;
}

ArenaStatus ParseEngineArtifact(const wchar_t* repositoryRoot, const Catalog* catalog, std::uint32_t engineIndex,
                                ReleaseCatalog* releaseCatalog, StatusRecord* error, ReleaseLoadMode mode)
{
	const EngineRecord& engine = catalog->engines[engineIndex];
	const std::string_view engineId = CatalogTextView(catalog, engine.id);
	const std::string_view manifestPath = CatalogTextView(catalog, engine.releaseArtifactManifest);
	if (mode == ReleaseLoadMode_Available)
	{
		std::array<wchar_t, kReleasePathCapacity> path = {};
		if (BuildPath(repositoryRoot, manifestPath, &path) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		if (GetFileAttributesW(path.data()) == INVALID_FILE_ATTRIBUTES)
		{
			const DWORD reason = GetLastError();
			if (reason == ERROR_FILE_NOT_FOUND || reason == ERROR_PATH_NOT_FOUND)
				return ArenaStatus_Ok;
		}
	}
	OrderedJson manifest;
	if (LoadDocument(repositoryRoot, manifestPath, &manifest, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	OrderedJson::const_iterator prefix = manifest.find("producer_prefix_args");
	if (ValidateKeys(manifest,
	                 prefix == manifest.end()
	                     ? std::initializer_list<std::string_view>{"schema_version", "artifact_role", "engine_id",
						                                           "executable_path", "source_version", "toolchain_id",
						                                           "report_version"}
						 : std::initializer_list<std::string_view>{"schema_version", "artifact_role", "engine_id",
						                                           "executable_path", "source_version", "toolchain_id",
						                                           "report_version", "producer_prefix_args"},
	                 manifestPath, error) != ArenaStatus_Ok ||
	    ValidateSchema(manifest, manifestPath, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	ReleaseArtifactRecord& record = releaseCatalog->artifacts[releaseCatalog->artifactCount];
	record.engineIndex = engineIndex;
	std::string_view text;
	if (StoreText(releaseCatalog, &record.manifestPath, manifestPath) != ArenaStatus_Ok ||
	    RequiredString(manifest, "artifact_role", manifestPath, &text, error) != ArenaStatus_Ok ||
	    text != "engine_runner")
		return ReleaseErrorParts(error, {"artifact_role manifest=", manifestPath});
	if (RequiredString(manifest, "engine_id", manifestPath, &text, error) != ArenaStatus_Ok || text != engineId)
		return ReleaseErrorParts(error, {"engine_id manifest=", manifestPath});
	if (RequiredString(manifest, "executable_path", manifestPath, &text, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	if (mode == ReleaseLoadMode_Available)
	{
		std::array<wchar_t, kReleasePathCapacity> path = {};
		if (RelativePath(text) != ArenaStatus_Ok || BuildPath(repositoryRoot, text, &path) != ArenaStatus_Ok)
			return ReleaseErrorParts(error, {"invalid_executable_path manifest=", manifestPath});
		if (GetFileAttributesW(path.data()) == INVALID_FILE_ATTRIBUTES)
		{
			const DWORD reason = GetLastError();
			if (reason == ERROR_FILE_NOT_FOUND || reason == ERROR_PATH_NOT_FOUND)
				return ArenaStatus_Ok;
		}
	}
	if (ValidateExecutable(repositoryRoot, text, manifestPath, error) != ArenaStatus_Ok ||
	    StoreText(releaseCatalog, &record.executablePath, text) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	for (std::pair<const char*, CatalogText*> field :
	     {std::pair<const char*, CatalogText*>{"source_version", &record.sourceVersion},
	      std::pair<const char*, CatalogText*>{"toolchain_id", &record.toolchainId},
	      std::pair<const char*, CatalogText*>{"report_version", &record.reportVersion}})
		if (RequiredString(manifest, field.first, manifestPath, &text, error) != ArenaStatus_Ok ||
		    StoreText(releaseCatalog, field.second, text) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
	record.producerPrefixOffset = releaseCatalog->producerPrefixCount;
	if (prefix != manifest.end())
	{
		if (!prefix->is_array() || prefix->empty() ||
		    releaseCatalog->producerPrefixCount + prefix->size() > releaseCatalog->producerPrefixArguments.size())
			return ReleaseErrorParts(error, {"producer_prefix_args manifest=", manifestPath});
		for (const OrderedJson& argument : *prefix)
		{
			if (!argument.is_string() || argument.get_ref<const OrderedJson::string_t&>().empty() ||
			    StoreText(releaseCatalog,
			              &releaseCatalog->producerPrefixArguments[releaseCatalog->producerPrefixCount++],
			              argument.get_ref<const OrderedJson::string_t&>()) != ArenaStatus_Ok)
				return ReleaseErrorParts(error, {"producer_prefix_arg manifest=", manifestPath});
			record.producerPrefixCount += 1;
		}
	}
	releaseCatalog->engineArtifactIndexes[engineIndex] = releaseCatalog->artifactCount;
	releaseCatalog->engineArtifactAvailability[engineIndex] = PresenceStatus_Present;
	releaseCatalog->artifactCount += 1;
	return ArenaStatus_Ok;
}

ArenaStatus ParseApplicationArtifact(const wchar_t* repositoryRoot, ReleaseCatalog* releaseCatalog, StatusRecord* error)
{
	constexpr std::string_view manifestPath = "release/windows-x64/physics_arena/artifact-manifest.json";
	OrderedJson manifest;
	if (LoadDocument(repositoryRoot, manifestPath, &manifest, error) != ArenaStatus_Ok ||
	    ValidateKeys(manifest, {"schema_version", "artifact_role", "executable_path"}, manifestPath, error) !=
	        ArenaStatus_Ok ||
	    ValidateSchema(manifest, manifestPath, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	std::string_view text;
	if (StoreText(releaseCatalog, &releaseCatalog->application.manifestPath, manifestPath) != ArenaStatus_Ok ||
	    RequiredString(manifest, "artifact_role", manifestPath, &text, error) != ArenaStatus_Ok ||
	    text != "physics_arena_host")
		return ReleaseErrorParts(error, {"application_artifact_role"});
	if (RequiredString(manifest, "executable_path", manifestPath, &text, error) != ArenaStatus_Ok ||
	    ValidateExecutable(repositoryRoot, text, manifestPath, error) != ArenaStatus_Ok ||
	    StoreText(releaseCatalog, &releaseCatalog->application.executablePath, text) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	return ArenaStatus_Ok;
}
} // namespace

ArenaStatus LoadReleaseCatalog(const wchar_t* repositoryRoot, const Catalog* catalog, ReleaseCatalog* releaseCatalog,
                               StatusRecord* error, ReleaseLoadMode mode)
{
	*releaseCatalog = {};
	*error = {};
	try
	{
		if (repositoryRoot == nullptr || catalog == nullptr || catalog->engineCount == 0 ||
		    StoreText(releaseCatalog, &releaseCatalog->hostRoute, "windows") != ArenaStatus_Ok)
			return ReleaseErrorParts(error, {"release_input"});
		for (std::uint32_t engineIndex = 0; engineIndex < catalog->engineCount; ++engineIndex)
		{
			if (ParseEngineArtifact(repositoryRoot, catalog, engineIndex, releaseCatalog, error, mode) !=
			    ArenaStatus_Ok)
			{
				*releaseCatalog = {};
				return ArenaStatus_InvalidResult;
			}
		}
		if (mode == ReleaseLoadMode_RequireAll &&
		    ParseApplicationArtifact(repositoryRoot, releaseCatalog, error) != ArenaStatus_Ok)
		{
			*releaseCatalog = {};
			return ArenaStatus_InvalidResult;
		}
		return ArenaStatus_Ok;
	}
	catch (const std::exception& exception)
	{
		*releaseCatalog = {};
		return ReleaseErrorParts(error, {"release_json_parse error=", exception.what()});
	}
}
} // namespace physics_arena
