#pragma once

#include "physics_arena/catalog.h"

#include "case_execution_wire.h"

#include <cstdint>

namespace physics_arena
{
ArenaStatus ResolveCaseExecutionPreset(CaseExecutionSpec* spec, CaseShapePreset preset, StatusRecord* error);
ArenaStatus StoreCatalogCaseExecution(Catalog* catalog, CaseRecord* record, const CaseExecutionSpec* spec,
                                      StatusRecord* error);
ArenaStatus DecodeCatalogCaseExecution(const Catalog* catalog, std::uint32_t caseIndex, CaseExecutionSpec* spec,
                                       StatusRecord* error);
ArenaStatus EncodeCatalogCaseExecutionHex(const Catalog* catalog, std::uint32_t caseIndex, char* hex,
                                          std::uint32_t capacity, std::uint32_t* hexSize, StatusRecord* error);
}
