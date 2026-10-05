#pragma once

#include "physics_arena/catalog.h"
#include "physics_arena/case_execution.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace physics_arena
{
using OrderedJson = nlohmann::ordered_json;

ArenaStatus SetErrorParts(StatusRecord* error, std::initializer_list<std::string_view> parts);
ArenaStatus SetError(StatusRecord* error, std::string_view detail);
ArenaStatus StoreText(Catalog* catalog, CatalogText* destination, std::string_view source);
ArenaStatus LoadDocument(const wchar_t* repositoryRoot, std::string_view relative, OrderedJson* document,
                         StatusRecord* error);
ArenaStatus ValidateKeys(const OrderedJson& object, std::initializer_list<std::string_view> keys,
                         std::string_view location, StatusRecord* error);
ArenaStatus ValidateSchema(const OrderedJson& document, std::uint32_t expected, std::string_view location,
                           StatusRecord* error);
ArenaStatus RequiredString(const OrderedJson& object, const char* key, std::string_view location,
                           std::string_view* output, StatusRecord* error);
ArenaStatus RequiredUnsigned(const OrderedJson& object, const char* key, std::string_view location,
                             std::uint32_t minimum, std::uint32_t* output, StatusRecord* error);
ArenaStatus SafeRelativePath(std::string_view path);
RouteStatus ParseRouteStatus(std::string_view status);
ArenaStatus EngineIndex(const Catalog& catalog, std::string_view id, std::uint32_t* index);
ArenaStatus CaseIndex(const Catalog& catalog, std::string_view id, std::uint32_t* index);
PresenceStatus ValidWireId(std::string_view id);

ArenaStatus ParseCases(const OrderedJson& document, Catalog* catalog, StatusRecord* error);
OrderedJson WriteCaseFixture(const CaseExecutionSpec& spec);
OrderedJson WriteCaseCamera(const CaseVisualCameraPolicy& camera);
ArenaStatus ValidateEditedCase(CaseExecutionSpec* spec, CaseVisualCameraPolicy* camera, StatusRecord* error);
ArenaStatus ParseEngines(const OrderedJson& document, Catalog* catalog, StatusRecord* error);
ArenaStatus LoadEngineSettings(const wchar_t* repositoryRoot, Catalog* catalog, StatusRecord* error);
ArenaStatus ParseReports(const OrderedJson& document, Catalog* catalog, StatusRecord* error);
}
