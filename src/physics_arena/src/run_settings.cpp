#include "pyramid_wall.h"
#include "physics_arena/run_settings.h"
#include "physics_arena/case_execution.h"
#include "physics_arena/timing_series.h"
#include "json_contracts_internal.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <memory>
#include <cmath>

namespace physics_arena
{
CaseRecord RunObservationCase(const CaseRecord& benchmarkCase, VerificationMode verificationMode)
{
	CaseRecord projected = benchmarkCase;
	if (verificationMode == VerificationMode_Off &&
	    (benchmarkCase.fixtureKind == CaseFixtureKind_PyramidWall ||
	     benchmarkCase.fixtureKind == CaseFixtureKind_RagdollStairTumble))
	{
		projected.observationCount = 0;
		projected.resultGroupCount = 0;
	}
	return projected;
}

std::string_view RunConfigurationTextView(const EffectiveRunConfiguration* configuration, CatalogText text)
{
	if (text.offset > configuration->textArenaUsed || text.size > configuration->textArenaUsed - text.offset)
		return {};
	return {configuration->textArena.data() + text.offset, text.size};
}

std::string_view CaseConfigurationTextView(const Catalog* catalog, const EffectiveRunConfiguration* configuration,
                                           CatalogText text)
{
	return configuration != nullptr ? RunConfigurationTextView(configuration, text) : CatalogTextView(catalog, text);
}

const ObservationDeclaration& CaseConfigurationObservation(const Catalog* catalog, const CaseRecord& record,
                                                           const EffectiveRunConfiguration* configuration,
                                                           std::uint32_t ordinal)
{
	return configuration != nullptr ? configuration->observations[ordinal]
	                                : catalog->observations[record.observationOffset + ordinal];
}

std::uint32_t CaseConfigurationSampleIndex(const Catalog* catalog, const EffectiveRunConfiguration* configuration,
                                           const ObservationDeclaration& declaration, std::uint32_t ordinal)
{
	return configuration != nullptr ? configuration->sampleIndices[declaration.sampleIndicesOffset + ordinal]
	                                : catalog->observationSampleIndices[declaration.sampleIndicesOffset + ordinal];
}

std::string_view NativeSolverFieldName(CaseSolverField field)
{
	constexpr std::string_view names[] = {
	    "velocity_iterations", "position_iterations", "projection_iterations", "solver_iterations", "substeps",
	    "collision_steps"};
	return field >= 0 && field < CaseSolverField_Count ? names[field] : std::string_view{};
}

ArenaStatus ParseNativeSolverField(std::string_view name, CaseSolverField* field)
{
	for (std::uint32_t index = 0; index < CaseSolverField_Count; ++index)
	{
		const CaseSolverField candidate = static_cast<CaseSolverField>(index);
		if (NativeSolverFieldName(candidate) != name)
			continue;
		*field = candidate;
		return ArenaStatus_Ok;
	}
	return ArenaStatus_InvalidArgument;
}

std::string_view EnginePhysicsFieldName(EnginePhysicsField field)
{
	constexpr std::string_view names[] = {"friction", "restitution", "sleep_mode", "continuous_collision_mode",
	                                      "linear_damping", "angular_damping", "solver_stabilization"};
	return field >= 0 && field < EnginePhysicsField_Count ? names[field] : std::string_view{};
}

std::string EnginePhysicsFieldValue(const EngineRunSettings& settings, EnginePhysicsField field)
{
	if (field == EnginePhysicsField_SleepMode || field == EnginePhysicsField_ContinuousCollisionMode ||
	    field == EnginePhysicsField_SolverStabilization)
	{
		const CaseExecutionToggle value = field == EnginePhysicsField_SleepMode ? settings.sleepMode :
		    field == EnginePhysicsField_ContinuousCollisionMode ? settings.continuousCollisionMode : settings.solverStabilization;
		return value == CaseExecutionToggle_Enabled ? "enabled" : "disabled";
	}
	const float value = field == EnginePhysicsField_Friction ? settings.friction :
	    field == EnginePhysicsField_Restitution ? settings.restitution :
	    field == EnginePhysicsField_LinearDamping ? settings.linearDamping : settings.angularDamping;
	std::array<char, 64> number = {};
	const std::to_chars_result result = std::to_chars(number.data(), number.data() + number.size(), value);
	return std::string(number.data(), result.ptr);
}

CaseExecutionSpec ResolveEngineCaseExecution(const EffectiveRunConfiguration* configuration,
                                            std::uint32_t selectedEngineIndex)
{
	CaseExecutionSpec execution = configuration->execution;
	const EngineRunSettings& settings = configuration->selectedEngineSettings[selectedEngineIndex];
	if (CaseExecutionIsQuery(execution.fixtureKind))
		return execution;
	execution.nativeSolver = settings.solver;
	execution.friction = settings.friction;
	execution.restitution = settings.restitution;
	execution.sleepMode = settings.sleepMode;
	execution.continuousCollisionMode = settings.continuousCollisionMode;
	if (execution.fixtureKind == CaseFixtureKind_RagdollStairTumble)
	{
		execution.ragdoll.linearDamping = settings.linearDamping;
		execution.ragdoll.angularDamping = settings.angularDamping;
	}
	return execution;
}

ArenaStatus ApplyRunSettingsArgument(const Catalog* catalog, std::string_view option, std::string_view text,
                                     RunSettings* settings, RunSettingsArgumentState* seen, StatusRecord* error)
{
	auto invalid = [error](std::string_view detail)
	{
		SetError(error, detail);
		error->code = ArenaStatus_InvalidArgument;
		const std::string_view status = ArenaStatusText(error->code);
		std::copy(status.begin(), status.end(), error->status.begin());
		error->statusSize = static_cast<std::uint32_t>(status.size());
		return error->code;
	};
	std::uint32_t value = 0;
	if (option == "--timestep-hz" || option == "--warmup-steps" || option == "--measured-steps")
	{
		const std::uint32_t bit = option == "--timestep-hz" ? 1 : option == "--warmup-steps" ? 2 : 4;
		const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
		if ((seen->scheduleFields & bit) != 0 || text.empty() || parsed.ec != std::errc() ||
		    parsed.ptr != text.data() + text.size() || (bit != 2 && value == 0) ||
		    (bit == 1 && CaseExecutionIsQuery(settings->caseInputs.fixtureKind)))
			return invalid("run_schedule_option");
		seen->scheduleFields |= bit;
		if (bit == 1)
			settings->caseInputs.timestepHz = value;
		else if (bit == 2)
			settings->caseInputs.warmupWorkUnitCount = value;
		else
			settings->caseInputs.measuredWorkUnitCount = value;
		return ArenaStatus_Ok;
	}
	if (option != "--solver" && option != "--physics")
		return invalid("unknown_run_option");
	const std::size_t colon = text.find(':');
	const std::size_t equal = text.find('=');
	if (colon == std::string_view::npos || equal == std::string_view::npos || equal <= colon + 1)
		return invalid("solver_assignment");
	std::uint32_t engine = 0;
	while (engine < catalog->engineCount &&
	       CatalogTextView(catalog, catalog->engines[engine].id) != text.substr(0, colon))
		++engine;
	if (option == "--physics")
	{
		if (engine == catalog->engineCount || CaseExecutionIsQuery(settings->caseInputs.fixtureKind))
			return invalid("physics_assignment_engine_or_query");
		EnginePhysicsField field = EnginePhysicsField_Count;
		for (std::uint32_t index = 0; index < EnginePhysicsField_Count; ++index)
			if (text.substr(colon + 1, equal - colon - 1) == EnginePhysicsFieldName(static_cast<EnginePhysicsField>(index)))
				field = static_cast<EnginePhysicsField>(index);
		if (field == EnginePhysicsField_Count || (seen->physicsFields[engine] & (1u << field)) != 0)
			return invalid("physics_assignment_unknown_or_duplicate_field");
		if ((field == EnginePhysicsField_LinearDamping || field == EnginePhysicsField_AngularDamping) &&
		    settings->caseInputs.fixtureKind != CaseFixtureKind_RagdollStairTumble)
			return invalid("physics_assignment_ragdoll_only");
		if (field == EnginePhysicsField_SolverStabilization &&
		    (catalog->engines[engine].settings.solverStabilizationFixtures & (1u << settings->caseInputs.fixtureKind)) == 0)
			return invalid("physics_assignment_solver_stabilization_inapplicable");
		EngineRunSettings& profile = settings->engineSettings[engine];
		const std::string_view input = text.substr(equal + 1);
		if (field == EnginePhysicsField_SleepMode || field == EnginePhysicsField_ContinuousCollisionMode ||
		    field == EnginePhysicsField_SolverStabilization)
		{
			if (input != "enabled" && input != "disabled")
				return invalid("physics_assignment_toggle");
			CaseExecutionToggle& toggle = field == EnginePhysicsField_SleepMode ? profile.sleepMode :
			    field == EnginePhysicsField_ContinuousCollisionMode ? profile.continuousCollisionMode : profile.solverStabilization;
			toggle = input == "enabled" ? CaseExecutionToggle_Enabled : CaseExecutionToggle_Disabled;
		}
		else
		{
			float number = 0;
			const std::from_chars_result parsed = std::from_chars(input.data(), input.data() + input.size(), number);
			if (input.empty() || parsed.ec != std::errc() || parsed.ptr != input.data() + input.size() ||
			    !std::isfinite(number) || number < 0 || (field == EnginePhysicsField_Restitution && number > 1))
				return invalid("physics_assignment_number");
			float& target = field == EnginePhysicsField_Friction ? profile.friction :
			    field == EnginePhysicsField_Restitution ? profile.restitution :
			    field == EnginePhysicsField_LinearDamping ? profile.linearDamping : profile.angularDamping;
			target = number;
		}
		seen->physicsFields[engine] |= 1u << field;
		return ArenaStatus_Ok;
	}
	CaseSolverField field = {};
	const std::string_view number = text.substr(equal + 1);
	const auto parsed = std::from_chars(number.data(), number.data() + number.size(), value);
	if (engine == catalog->engineCount ||
	    ParseNativeSolverField(text.substr(colon + 1, equal - colon - 1), &field) != ArenaStatus_Ok || number.empty() ||
	    parsed.ec != std::errc() || parsed.ptr != number.data() + number.size() ||
	    (seen->solverFields[engine] & (1u << field)) != 0 ||
	    (catalog->engines[engine].settings.solverFields & (1u << field)) == 0 ||
	    CaseExecutionIsQuery(settings->caseInputs.fixtureKind))
		return invalid("solver_assignment");
	seen->solverFields[engine] |= 1u << field;
	settings->engineSettings[engine].solver.values[field] = value;
	return ArenaStatus_Ok;
}

void ResetEngineRunSettings(const Catalog* catalog, std::uint32_t engineIndex, RunSettings* settings)
{
	EngineRunSettings& profile = settings->engineSettings[engineIndex];
	profile = {};
	CaseExecutionSpec authored = {};
	StatusRecord error = {};
	DecodeCatalogCaseExecution(catalog, catalog->cases[settings->caseIndex].authoredCaseIndex, &authored, &error);
	if (CaseExecutionIsQuery(authored.fixtureKind))
		return;
	profile.friction = authored.friction;
	profile.restitution = authored.restitution;
	profile.sleepMode = authored.sleepMode;
	profile.continuousCollisionMode = authored.continuousCollisionMode;
	if (authored.fixtureKind == CaseFixtureKind_RagdollStairTumble)
	{
		profile.linearDamping = authored.ragdoll.linearDamping;
		profile.angularDamping = authored.ragdoll.angularDamping;
	}
	const EngineSettingsCapabilities& capabilities = catalog->engines[engineIndex].settings;
	if ((capabilities.solverStabilizationFixtures & (1u << authored.fixtureKind)) != 0)
	{
		profile.solverStabilization = capabilities.solverStabilizationDefault;
		profile.solverStabilizationPresence = PresenceStatus_Present;
	}
	profile.solver.supportedFields = capabilities.solverFields;
	for (std::uint32_t field = 0; field < CaseSolverField_Count; ++field)
		profile.solver.values[field] = capabilities.solver[field].authoredDefault;
}

ArenaStatus ResetRunSettings(const Catalog* catalog, std::uint32_t caseIndex, RunSettings* settings,
                             StatusRecord* error)
{
	*settings = {};
	if (caseIndex >= catalog->caseCount)
		return SetError(error, "run_settings_case_index");
	const CaseRecord& selected = catalog->cases[caseIndex];
	if (DecodeCatalogCaseExecution(catalog, selected.authoredCaseIndex, &settings->caseInputs, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	settings->camera = selected.visualCamera;
	settings->caseIndex = caseIndex;
	settings->presence = PresenceStatus_Present;
	for (std::uint32_t index = 0; index < catalog->engineCount; ++index)
		ResetEngineRunSettings(catalog, index, settings);
	return ArenaStatus_Ok;
}

namespace
{
std::uint64_t BoundedPopulation(std::span<const std::uint32_t> dimensions)
{
	std::uint64_t product = 1;
	for (const std::uint32_t dimension : dimensions)
	{
		if (dimension == 0 || dimension > 16211 || product > 16211 / dimension)
			return 0;
		product *= dimension;
	}
	return product;
}

ArenaStatus DeriveCaseCounts(CaseExecutionSpec* spec, StatusRecord* error)
{
	if (spec->measuredWorkUnitCount == 0 || spec->measuredWorkUnitCount > kTimingProjectionStepCapacity)
		return SetError(error, "setting=measured_work_unit_count exceeds_timing_capacity");
	if (spec->warmupWorkUnitCount > 1000000)
		return SetError(error, "setting=warmup_work_unit_count exceeds_producer_capacity");
	if ((CaseExecutionIsQuery(spec->fixtureKind) &&
	     (spec->timestepPresent != 0 || spec->timestepHz != 0)) ||
	    (!CaseExecutionIsQuery(spec->fixtureKind) &&
	     (spec->timestepPresent != 1 || spec->timestepHz == 0)))
		return SetError(error, "setting=timestep_hz inapplicable_or_zero");
	if (spec->fixtureKind == CaseFixtureKind_RayTracing)
	{
		const CaseExecutionRayTracing& ray = spec->rayTracing;
		if (ray.primitiveCount == 0 || ray.primitiveCount > 65536 || ray.meshCount > 1024 ||
		    ray.trianglesPerMesh == 0 || ray.trianglesPerMesh > 1024 || ray.movingCount > ray.primitiveCount ||
		    ray.width == 0 || ray.width > 1920 || ray.height == 0 || ray.height > 1080 ||
		    ray.viewCount == 0 || ray.viewCount > 6)
			return SetError(error, "setting=ray_tracing exceeds_recipe_capacity");
		spec->dynamicBodyCount = 0;
		spec->kinematicBodyCount = ray.movingCount;
		spec->staticBodyCount = ray.primitiveCount - ray.movingCount + ray.meshCount;
		spec->bodyCount = ray.primitiveCount + ray.meshCount;
		spec->shapeCount = spec->bodyCount;
		spec->meshTriangleCount = ray.meshCount * ray.trianglesPerMesh;
		spec->queryCount = ray.width * ray.height;
		spec->visualInstanceCount = 0;
		spec->visualDebugPrimitiveCount = 0;
		spec->constraintCount = 0;
		return ArenaStatus_Ok;
	}
	std::uint64_t dynamicCount = 0;
	std::uint64_t staticCount = 0;
	std::uint64_t constraints = 0;
	std::uint64_t queries = 0;
	std::uint64_t debug = 0;
	if (spec->fixtureKind == CaseFixtureKind_OpenContainerFallingPile)
	{
		dynamicCount = BoundedPopulation(spec->openContainer.dynamicGrid);
		staticCount = spec->openContainer.staticBoxCount;
		if (dynamicCount == 0 || staticCount == 0 || staticCount > kCaseExecutionStaticBoxCapacity)
			return SetError(error, "setting=dynamic_grid_or_static_boxes invalid_capacity");
	}
	else if (spec->fixtureKind == CaseFixtureKind_BoxContactIslands)
	{
		staticCount = BoundedPopulation(spec->contactIslands.islandGrid);
		const std::uint64_t perIsland = BoundedPopulation(spec->contactIslands.bodyGrid);
		dynamicCount = staticCount * perIsland;
		if (staticCount == 0 || perIsland == 0)
			return SetError(error, "setting=island_grid_or_body_grid invalid_population");
	}
	else if (spec->fixtureKind == CaseFixtureKind_SpatialQueryTrace)
	{
		const CaseExecutionSpatialQuery& query = spec->spatialQuery;
		staticCount = BoundedPopulation(query.staticGrid);
		queries = static_cast<std::uint64_t>(query.rayCount) + query.sphereCastCount + query.overlapCount;
		debug = static_cast<std::uint64_t>(query.debugSamplesPerFamily) * 3;
		if (staticCount == 0 || query.rayCount == 0 || query.sphereCastCount == 0 || query.overlapCount == 0 ||
		    queries > 100000 || debug == 0 || debug > 768)
			return SetError(error, "setting=query_counts exceeds_recipe_capacity");
		const float maximumSpan =
		    std::max({(query.staticGrid[0] - 1) * query.staticSpacing.x + 2 * query.staticHalfExtents.x,
			          (query.staticGrid[1] - 1) * query.staticSpacing.y + 2 * query.staticHalfExtents.y,
			          (query.staticGrid[2] - 1) * query.staticSpacing.z + 2 * query.staticHalfExtents.z});
		const float maximumOverlap =
		    std::max({query.overlapHalfExtents.x, query.overlapHalfExtents.y, query.overlapHalfExtents.z});
		if (query.queryDistance <= maximumSpan + 5.0f ||
		    query.missOffset <= std::max(query.sphereCastRadius, maximumOverlap))
			return SetError(error, "setting=query_distance_or_miss_offset does_not_preserve_hit_miss_recipe");
	}
	else if (spec->fixtureKind == CaseFixtureKind_RagdollStairTumble)
	{
		const CaseExecutionRagdoll& ragdoll = spec->ragdoll;
		const std::uint64_t population = BoundedPopulation(ragdoll.ragdollGrid);
		if (population == 0 || ragdoll.partCount == 0 || ragdoll.partCount > kCaseExecutionRagdollPartCapacity ||
		    ragdoll.linkCount > kCaseExecutionRagdollLinkCapacity || ragdoll.yawPatternCount == 0 ||
		    ragdoll.yawPatternCount > kCaseExecutionYawCapacity || ragdoll.extraStaticBoxCount < 4 ||
		    ragdoll.extraStaticBoxCount > kCaseExecutionStaticBoxCapacity || ragdoll.stairCount == 0)
			return SetError(error, "setting=ragdoll_population_or_recipe_arrays invalid_capacity");
		dynamicCount = population * ragdoll.partCount;
		staticCount = static_cast<std::uint64_t>(ragdoll.stairCount) + ragdoll.extraStaticBoxCount;
		constraints = population * ragdoll.linkCount;
		debug = 3;
	}
	else if (spec->fixtureKind == CaseFixtureKind_LargePyramid)
	{
		const CaseExecutionLargePyramid& pyramid = spec->largePyramid;
		const std::uint64_t rows = pyramid.rowCount;
		if (rows == 0 || rows > 36 || pyramid.projectileCount == 0 || pyramid.projectileLaunchAfterWorkUnits == 0)
			return SetError(error, "setting=row_count_or_projectile_launch_after_work_units invalid");
		dynamicCount = rows * (rows + 1) * (2 * rows + 1) / 6 + pyramid.projectileCount;
		staticCount = 1;
	}
	else if (spec->fixtureKind == CaseFixtureKind_PyramidWall)
	{
		const std::uint64_t rows = spec->pyramidWall.rowCount;
		if (rows == 0 || rows > 180)
			return SetError(error, "setting=row_count exceeds_existing_scene_capacity");
		dynamicCount = rows * (rows + 1) / 2;
		staticCount = 1;
	}
	else
		return SetError(error, "setting=fixture_kind unsupported");
	const std::uint64_t dynamicCapacity = spec->fixtureKind == CaseFixtureKind_PyramidWall ? 16290 : 16210;
	if (dynamicCount > dynamicCapacity || dynamicCount + staticCount > dynamicCapacity + 1 || constraints > UINT32_MAX)
		return SetError(error, "setting=population exceeds_existing_scene_capacity");
	spec->dynamicBodyCount = static_cast<std::uint32_t>(dynamicCount);
	spec->kinematicBodyCount = 0;
	spec->staticBodyCount = static_cast<std::uint32_t>(staticCount);
	spec->bodyCount = static_cast<std::uint32_t>(dynamicCount + staticCount);
	spec->shapeCount = spec->bodyCount;
	spec->visualInstanceCount = spec->bodyCount - (spec->fixtureKind == CaseFixtureKind_RagdollStairTumble ? 3 : 0);
	spec->constraintCount = static_cast<std::uint32_t>(constraints);
	spec->queryCount = static_cast<std::uint32_t>(queries);
	spec->visualDebugPrimitiveCount = static_cast<std::uint32_t>(debug);
	spec->meshTriangleCount = 0;
	return ArenaStatus_Ok;
}

}

ArenaStatus AdmitEngineRunSettings(const Catalog* catalog, std::uint32_t engineIndex,
                                   const CaseExecutionSpec& execution, const EngineRunSettings& settings,
                                   StatusRecord* error)
{
	const CaseNativeSolver& solver = settings.solver;
	const EngineRecord& engine = catalog->engines[engineIndex];
	const EngineSettingsCapabilities& capabilities = engine.settings;
	const std::string_view id = CatalogTextView(catalog, engine.id);
	const std::uint32_t fields =
	    CaseExecutionIsQuery(execution.fixtureKind) ? 0 : capabilities.solverFields;
	if (solver.supportedFields != fields)
		return SetErrorParts(error, {"setting=native_solver unsupported engine=", id});
	for (std::uint32_t field = 0; field < CaseSolverField_Count; ++field)
	{
		const NativeSolverCapability& capability = capabilities.solver[field];
		const std::uint32_t value = solver.values[field];
		if (((fields & (1u << field)) == 0 && value != 0) ||
		    ((fields & (1u << field)) != 0 && (value < capability.minimum || value > capability.maximum)))
			return SetErrorParts(error, {"setting=", NativeSolverFieldName(static_cast<CaseSolverField>(field)),
			                             " unsupported_or_out_of_range engine=", id});
	}
	if (CaseExecutionIsQuery(execution.fixtureKind))
	{
		if (settings.friction != 0 || settings.restitution != 0 || settings.linearDamping != 0 || settings.angularDamping != 0 ||
		    settings.sleepMode != CaseExecutionToggle_Disabled || settings.continuousCollisionMode != CaseExecutionToggle_Disabled ||
		    settings.solverStabilizationPresence != PresenceStatus_Absent)
			return SetErrorParts(error, {"setting=physics inapplicable engine=", id});
		return ArenaStatus_Ok;
	}
	if (!std::isfinite(execution.friction) || execution.friction < 0 || !std::isfinite(execution.restitution) ||
	    execution.restitution < 0 || !case_execution_wire_detail::ValidToggle(execution.sleepMode) ||
	    !case_execution_wire_detail::ValidToggle(execution.continuousCollisionMode))
		return SetErrorParts(error, {"setting=physics invalid engine=", id});
	if (execution.fixtureKind == CaseFixtureKind_RagdollStairTumble &&
	    (!std::isfinite(execution.ragdoll.linearDamping) || execution.ragdoll.linearDamping < 0 ||
	     !std::isfinite(execution.ragdoll.angularDamping) || execution.ragdoll.angularDamping < 0 ||
	     (capabilities.ragdollDampingMode == RagdollDampingMode_FixedZero &&
	      (execution.ragdoll.linearDamping != 0 || execution.ragdoll.angularDamping != 0))))
		return SetErrorParts(error, {"setting=ragdoll_damping unsupported engine=", id});
	const std::uint32_t fixtureBit = 1u << execution.fixtureKind;
	if ((capabilities.solverStabilizationFixtures & fixtureBit) != 0 &&
	    (settings.solverStabilizationPresence != PresenceStatus_Present ||
	     !case_execution_wire_detail::ValidToggle(settings.solverStabilization)))
		return SetErrorParts(error, {"setting=solver_stabilization unavailable engine=", id});
	const std::uint32_t sleepFixtures = execution.sleepMode == CaseExecutionToggle_Enabled
	                                        ? capabilities.sleepEnabledFixtures
	                                        : capabilities.sleepDisabledFixtures;
	if ((sleepFixtures & fixtureBit) == 0)
		return SetErrorParts(error, {"setting=sleep_mode unsupported engine=", id});
	if (execution.continuousCollisionMode == CaseExecutionToggle_Enabled &&
	    (capabilities.continuousCollisionFixtures & fixtureBit) == 0)
		return SetErrorParts(error, {"setting=continuous_collision_mode unsupported engine=", id});
	if (execution.restitution > capabilities.maximumRestitution)
		return SetErrorParts(error, {"setting=restitution unsupported engine=", id});
	return ArenaStatus_Ok;
}

namespace
{
ArenaStatus ComposeCaseSettings(const Catalog* catalog, std::uint32_t caseIndex, const RunSettings* settings,
                                EffectiveRunConfiguration* configuration, StatusRecord* error)
{
	*configuration = {};
	if (caseIndex >= catalog->caseCount)
		return SetError(error, "run_settings_selection");
	RunSettings defaults = {};
	if (ResetRunSettings(catalog, caseIndex, &defaults, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	const RunSettings& selected =
	    settings != nullptr && settings->presence == PresenceStatus_Present ? *settings : defaults;
	if (selected.caseIndex != caseIndex || selected.caseInputs.shapePreset != CaseShapePreset_Authored ||
	    selected.caseInputs.fixtureKind != defaults.caseInputs.fixtureKind ||
	    std::strcmp(selected.caseInputs.caseId, defaults.caseInputs.caseId) != 0 ||
	    std::strcmp(selected.caseInputs.fixtureSemantic, defaults.caseInputs.fixtureSemantic) != 0 ||
	    selected.caseInputs.fixtureRevision != defaults.caseInputs.fixtureRevision)
		return SetError(error, "setting=case_identity differs_from_selection");
	CaseExecutionSpec& execution = configuration->execution;
	execution = selected.caseInputs;
	if (!case_execution_wire_detail::ValidToggle(execution.sleepMode) ||
	    !case_execution_wire_detail::ValidToggle(execution.continuousCollisionMode) ||
	    !case_execution_wire_detail::ValidVector3(execution.gravity) ||
	    !case_execution_wire_detail::ValidFloat(execution.friction) || execution.friction < 0.0f ||
	    !case_execution_wire_detail::ValidFloat(execution.restitution) || execution.restitution < 0.0f ||
	    execution.restitution > 1.0f)
		return SetError(error, "setting=environment_or_material invalid");
	if (CaseExecutionIsQuery(execution.fixtureKind) &&
	    (execution.sleepMode != defaults.caseInputs.sleepMode ||
	     execution.continuousCollisionMode != defaults.caseInputs.continuousCollisionMode ||
	     execution.gravity.x != defaults.caseInputs.gravity.x || execution.gravity.y != defaults.caseInputs.gravity.y ||
	     execution.gravity.z != defaults.caseInputs.gravity.z || execution.friction != defaults.caseInputs.friction ||
	     execution.restitution != defaults.caseInputs.restitution))
		return SetError(error, "setting=simulation_controls inapplicable_to_query");
	if (DeriveCaseCounts(&execution, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	configuration->benchmarkCase = catalog->cases[caseIndex];
	CaseRecord& record = configuration->benchmarkCase;
	record.visualCamera = selected.camera;
	if (ValidateEditedCase(&execution, &record.visualCamera, error) != ArenaStatus_Ok ||
	    ResolveCaseExecutionPreset(&execution, record.shapePreset, error) != ArenaStatus_Ok ||
	    ResolveCaseExecutionPreset(&defaults.caseInputs, record.shapePreset, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	std::array<std::uint8_t, kCaseExecutionPayloadCapacity> effectiveBytes = {};
	std::array<std::uint8_t, kCaseExecutionPayloadCapacity> authoredBytes = {};
	std::uint32_t effectiveSize = 0;
	std::uint32_t authoredSize = 0;
	if (EncodeCaseExecution(&execution, effectiveBytes.data(), effectiveBytes.size(), &effectiveSize) !=
	        CaseExecutionDecodeStatus_Ok ||
	    EncodeCaseExecution(&defaults.caseInputs, authoredBytes.data(), authoredBytes.size(), &authoredSize) !=
	        CaseExecutionDecodeStatus_Ok)
		return SetError(error, "setting=fixture exceeds_execution_payload_capacity");
	configuration->mode =
	    effectiveSize == authoredSize &&
	            std::equal(effectiveBytes.begin(), effectiveBytes.begin() + effectiveSize, authoredBytes.begin())
	        ? RunConfigurationMode_Authored
	        : RunConfigurationMode_Custom;
	const std::string_view id = CatalogTextView(catalog, record.id);
	if (id.size() >= sizeof(execution.caseId))
		return SetError(error, "run_settings_case_id_capacity");
	std::memcpy(execution.caseId, id.data(), id.size());
	execution.caseId[id.size()] = '\0';
	record.dynamicBodyCount = execution.dynamicBodyCount;
	record.kinematicBodyCount = execution.kinematicBodyCount;
	record.staticBodyCount = execution.staticBodyCount;
	record.bodyCount = execution.bodyCount;
	record.shapeCount = execution.shapeCount;
	record.visualInstanceCount = execution.visualInstanceCount;
	record.meshTriangleCount = execution.meshTriangleCount;
	record.queryCount = execution.queryCount;
	record.constraintCount = execution.constraintCount;
	record.timestepHz = execution.timestepHz;
	record.warmupWorkUnitCount = execution.warmupWorkUnitCount;
	record.measuredWorkUnitCount = execution.measuredWorkUnitCount;
	for (std::uint32_t index = 0; index < record.observationCount; ++index)
	{
		const ObservationDeclaration& authored = catalog->observations[record.observationOffset + index];
		ObservationDeclaration& effective = configuration->observations[index];
		effective = authored;
		effective.sampleIndicesOffset = configuration->sampleIndexCount;
		const std::string_view observation = CatalogTextView(catalog, authored.id);
		const int wallObservation = execution.fixtureKind == CaseFixtureKind_PyramidWall &&
		    CatalogTextView(catalog, authored.phaseId) == "observation";
		if (wallObservation != 0)
			effective.sampleIndexCount = std::min(4u, execution.measuredWorkUnitCount);
		for (std::uint32_t sample = 0; sample < effective.sampleIndexCount; ++sample)
		{
			const std::uint32_t authoredIndex =
			    catalog->observationSampleIndices[authored.sampleIndicesOffset + sample];
			const std::uint32_t effectiveIndex = wallObservation != 0
			    ? PyramidWallObservationStep(execution.measuredWorkUnitCount, sample)
			    : !CaseExecutionIsQuery(execution.fixtureKind) &&
			                                             CatalogTextView(catalog, authored.phaseId) == "final"
			                                         ? execution.measuredWorkUnitCount
			                                         : authoredIndex;
			if (effectiveIndex > execution.measuredWorkUnitCount)
				return SetErrorParts(error, {"setting=measured_work_unit_count excludes_observation=", observation});
			configuration->sampleIndices[configuration->sampleIndexCount++] = effectiveIndex;
		}
		if (execution.fixtureKind == CaseFixtureKind_RagdollStairTumble)
		{
			if (observation == "joint_sample_count")
				effective.expectedUnsigned =
				    static_cast<std::uint64_t>(execution.constraintCount) * execution.measuredWorkUnitCount;
			if (observation == "body_sample_count")
				effective.expectedUnsigned =
				    static_cast<std::uint64_t>(execution.dynamicBodyCount) * execution.measuredWorkUnitCount;
		}
		if (execution.fixtureKind == CaseFixtureKind_SpatialQueryTrace)
		{
			if (observation == "ray_hit_count")
				effective.expectedUnsigned = (execution.spatialQuery.rayCount + 1u) / 2u;
			if (observation == "sphere_cast_hit_count")
				effective.expectedUnsigned = (execution.spatialQuery.sphereCastCount + 1u) / 2u;
			if (observation == "overlap_hit_count")
				effective.expectedUnsigned = (execution.spatialQuery.overlapCount + 1u) / 2u;
		}
	}
	const CaseRecord& authored = catalog->cases[catalog->cases[caseIndex].authoredCaseIndex];
	configuration->authoredCaseId = authored.id;
	configuration->authoredCaseSlug = authored.slug;
	configuration->authoredCaseDisplayName = authored.displayName;
	configuration->authoredCaseDescription = authored.description;
	configuration->authoredFixtureRevision = authored.fixtureRevision;
	configuration->textArenaUsed = catalog->textArenaUsed;
	std::copy_n(catalog->textArena.begin(), catalog->textArenaUsed, configuration->textArena.begin());
	std::copy_n(catalog->values.begin() + record.threadCountsOffset, record.threadCount,
	            configuration->authoredThreadCounts.begin());
	std::copy_n(catalog->resultGroups.begin() + record.resultGroupOffset, record.resultGroupCount,
	            configuration->resultGroups.begin());
	return ArenaStatus_Ok;
}

}

ArenaStatus ComposeRunSettings(const Catalog* catalog, std::uint32_t caseIndex, const RunSettings* settings,
                               std::span<const std::uint32_t> engineIndexes, EffectiveRunConfiguration* configuration,
                               StatusRecord* error)
{
	if (engineIndexes.size() > kEngineCapacity)
		return SetError(error, "run_settings_selection");
	if (ComposeCaseSettings(catalog, caseIndex, settings, configuration, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	RunSettings defaults = {};
	if (ResetRunSettings(catalog, caseIndex, &defaults, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	const RunSettings& selected =
	    settings != nullptr && settings->presence == PresenceStatus_Present ? *settings : defaults;
	for (std::size_t selection = 0; selection < engineIndexes.size(); ++selection)
	{
		const std::uint32_t engine = engineIndexes[selection];
		if (engine >= catalog->engineCount)
			return SetError(error, "run_settings_engine_index");
		for (std::size_t prior = 0; prior < selection; ++prior)
			if (engineIndexes[prior] == engine)
				return SetError(error, "run_settings_duplicate_engine");
		const EngineRunSettings& profile = selected.engineSettings[engine];
		configuration->selectedEngineSettings[selection] = profile;
		const CaseExecutionSpec resolved = ResolveEngineCaseExecution(configuration, static_cast<std::uint32_t>(selection));
		if (AdmitEngineRunSettings(catalog, engine, resolved, profile, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		const EngineRunSettings& authored = defaults.engineSettings[engine];
		for (std::uint32_t field = 0; field < CaseSolverField_Count; ++field)
			if (profile.solver.values[field] != authored.solver.values[field])
				configuration->mode = RunConfigurationMode_Custom;
		if (profile.friction != authored.friction || profile.restitution != authored.restitution ||
		    profile.sleepMode != authored.sleepMode || profile.continuousCollisionMode != authored.continuousCollisionMode ||
		    profile.linearDamping != authored.linearDamping || profile.angularDamping != authored.angularDamping ||
		    profile.solverStabilization != authored.solverStabilization)
			configuration->mode = RunConfigurationMode_Custom;
	}
	return ArenaStatus_Ok;
}

ArenaStatus EncodeEffectiveCaseExecutionHex(const EffectiveRunConfiguration* configuration,
                                            std::uint32_t selectedEngineIndex, char* hex, std::uint32_t capacity,
                                            std::uint32_t* size, StatusRecord* error)
{
	if (selectedEngineIndex >= configuration->selectedEngineSettings.size())
		return SetError(error, "effective_case_engine_index");
	CaseExecutionSpec execution = ResolveEngineCaseExecution(configuration, selectedEngineIndex);
	const CaseVisualCameraPolicy& camera = configuration->benchmarkCase.visualCamera;
	execution.replayCamera.direction = {camera.direction.x, camera.direction.y, camera.direction.z};
	execution.replayCamera.up = {camera.up.x, camera.up.y, camera.up.z};
	execution.replayCamera.minimum = {camera.minimum.x, camera.minimum.y, camera.minimum.z};
	execution.replayCamera.maximum = {camera.maximum.x, camera.maximum.y, camera.maximum.z};
	execution.replayCamera.eye = {camera.eye.x, camera.eye.y, camera.eye.z};
	execution.replayCamera.target = {camera.target.x, camera.target.y, camera.target.z};
	execution.replayCamera.eyeOffset = {camera.eyeOffset.x, camera.eyeOffset.y, camera.eyeOffset.z};
	execution.replayCamera.targetOffset = {camera.targetOffset.x, camera.targetOffset.y, camera.targetOffset.z};
	execution.replayCamera.verticalFovDegrees = camera.verticalFovDegrees;
	execution.replayCamera.viewportFill = camera.viewportFill;
	execution.replayCamera.nearPlane = camera.nearPlane;
	execution.replayCamera.farPlane = camera.farPlane;
	execution.replayCamera.stableSlot = camera.stableSlot;
	execution.replayCamera.mode = camera.mode;
	std::array<std::uint8_t, kCaseExecutionPayloadCapacity> bytes = {};
	std::uint32_t byteCount = 0;
	if (EncodeCaseExecution(&execution, bytes.data(), static_cast<std::uint32_t>(bytes.size()), &byteCount) !=
	        CaseExecutionDecodeStatus_Ok ||
	    capacity <= byteCount * 2)
		return SetError(error, "effective_case_execution_capacity");
	constexpr char digits[] = "0123456789abcdef";
	for (std::uint32_t index = 0; index < byteCount; ++index)
	{
		hex[index * 2] = digits[bytes[index] >> 4];
		hex[index * 2 + 1] = digits[bytes[index] & 15];
	}
	*size = byteCount * 2;
	hex[*size] = '\0';
	return ArenaStatus_Ok;
}
ArenaStatus WriteRunConfigurationSnapshot(const Catalog* catalog, const EffectiveRunConfiguration* configuration,
                                          std::span<const std::uint32_t> engineIndexes, std::string* json,
                                          StatusRecord* error, const CaseExecutionSpec* editableInputs)
{
	try
	{
		const CaseRecord& record = configuration->benchmarkCase;
		const CaseExecutionSpec& execution = configuration->execution;
		const auto text = [configuration](CatalogText value) -> std::string_view
		{
			return RunConfigurationTextView(configuration, value);
		};
		constexpr const char* presets[] = {"authored", "sphere", "capsule", "convex_hull"};
		OrderedJson definition = {
		    {"id", text(configuration->authoredCaseId)},
		    {"shape_presets", OrderedJson::array({"authored"})},
		    {"slug", text(configuration->authoredCaseSlug)},
		    {"display_name", text(configuration->authoredCaseDisplayName)},
		    {"description", text(configuration->authoredCaseDescription)},
		    {"category", text(record.category)},
		    {"fixture_semantic",
			 case_execution_wire_detail::ExpectedSemantic(execution.fixtureKind, CaseShapePreset_Authored)},
		    {"fixture_revision", configuration->authoredFixtureRevision},
		    {"benchmark_mode", text(record.benchmarkMode)},
		    {"work_unit_id", text(record.workUnitId)},
		    {"work_unit_label", text(record.workUnitLabel)},
		    {"dynamic_body_count", execution.dynamicBodyCount},
		    {"kinematic_body_count", execution.kinematicBodyCount},
		    {"static_body_count", execution.staticBodyCount},
		    {"body_count", execution.bodyCount},
		    {"shape_count", execution.shapeCount},
		    {"visual_instance_count", execution.visualInstanceCount},
		    {"visual_debug_primitive_count", execution.visualDebugPrimitiveCount},
		    {"mesh_triangle_count", execution.meshTriangleCount},
		    {"query_count", execution.queryCount},
		    {"constraint_count", execution.constraintCount},
		    {"warmup_work_unit_count", execution.warmupWorkUnitCount},
		    {"measured_work_unit_count", execution.measuredWorkUnitCount},
		    {"fixture", WriteCaseFixture(editableInputs != nullptr ? *editableInputs : execution)},
		    {"thread_counts", OrderedJson::array()},
		    {"repeat_presets", {{"qualification", record.qualificationRepeats}, {"full", record.fullRepeats}}},
		    {"visual_camera", WriteCaseCamera(record.visualCamera)},
		    {"primary_metric",
			 {{"id", text(record.primaryMetricId)},
			  {"unit", text(record.primaryMetricUnit)},
			  {"direction", record.primaryMetricDirection == PrimaryMetricDirection_LowerIsBetter ? "lower_is_better"
			                : record.primaryMetricDirection == PrimaryMetricDirection_HigherIsBetter
		                        ? "higher_is_better"
		                        : "not_ranked"},
			  {"label", text(record.primaryMetricLabel)},
			  {"note", text(record.primaryMetricNote)}}},
		    {"observations", OrderedJson::array()},
		    {"result_groups", OrderedJson::array()}};
		if (record.shapePreset != CaseShapePreset_Authored)
			definition["shape_presets"].push_back(presets[record.shapePreset]);
		if (execution.timestepPresent != 0)
			definition["timestep_hz"] = execution.timestepHz;
		for (std::uint32_t index = 0; index < record.threadCount; ++index)
			definition["thread_counts"].push_back(configuration->authoredThreadCounts[index]);
		for (std::uint32_t index = 0; index < record.resultGroupCount; ++index)
		{
			const ResultGroupRecord& group = configuration->resultGroups[index];
			definition["result_groups"].push_back({{"id", text(group.id)}, {"label", text(group.label)}});
		}
		constexpr const char* roles[] = {"unknown", "validity_zero", "validity_exact", "quality", "performance"};
		for (std::uint32_t index = 0; index < record.observationCount; ++index)
		{
			const ObservationDeclaration& declaration = configuration->observations[index];
			OrderedJson observation = {
			    {"id", text(declaration.id)},
			    {"label", text(declaration.label)},
			    {"result_group_id", text(configuration->resultGroups[declaration.resultGroupOrdinal].id)},
			    {"value_type", declaration.valueType == ObservationValueType_Uint64 ? "uint64" : "float64"},
			    {"unit", text(declaration.unit)},
			    {"role", roles[declaration.role]},
			    {"phase_id", text(declaration.phaseId)},
			    {"sample_indices", OrderedJson::array()}};
			for (std::uint32_t sample = 0; sample < declaration.sampleIndexCount; ++sample)
				observation["sample_indices"].push_back(
				    configuration->sampleIndices[declaration.sampleIndicesOffset + sample]);
			if (declaration.expectedValuePresence == PresenceStatus_Present)
			{
				if (declaration.valueType == ObservationValueType_Uint64)
					observation["expected_value"] = declaration.expectedUnsigned;
				else
					observation["expected_value"] = declaration.expectedFloat64;
			}
			definition["observations"].push_back(std::move(observation));
		}
		OrderedJson profiles = OrderedJson::object();
		for (std::size_t index = 0; index < engineIndexes.size(); ++index)
		{
			if (engineIndexes[index] >= catalog->engineCount)
				return SetError(error, "snapshot_engine_index");
			const EngineRunSettings& selected = configuration->selectedEngineSettings[index];
			OrderedJson solver = OrderedJson::object();
			for (std::uint32_t field = 0; field < CaseSolverField_Count; ++field)
				if ((selected.solver.supportedFields & (1u << field)) != 0)
					solver[NativeSolverFieldName(static_cast<CaseSolverField>(field))] = selected.solver.values[field];
			OrderedJson profile = {{"solver", std::move(solver)}};
			if (!CaseExecutionIsQuery(execution.fixtureKind))
			{
				profile["friction"] = selected.friction;
				profile["restitution"] = selected.restitution;
				profile["sleep_mode"] = EnginePhysicsFieldValue(selected, EnginePhysicsField_SleepMode);
				profile["continuous_collision_mode"] = EnginePhysicsFieldValue(selected, EnginePhysicsField_ContinuousCollisionMode);
				if (execution.fixtureKind == CaseFixtureKind_RagdollStairTumble)
				{
					profile["linear_damping"] = selected.linearDamping;
					profile["angular_damping"] = selected.angularDamping;
				}
				if ((catalog->engines[engineIndexes[index]].settings.solverStabilizationFixtures & (1u << execution.fixtureKind)) != 0)
				{
					if (selected.solverStabilizationPresence != PresenceStatus_Present)
						return SetError(error, "snapshot_solver_stabilization_unavailable");
					profile["solver_stabilization"] = EnginePhysicsFieldValue(selected, EnginePhysicsField_SolverStabilization);
				}
			}
			profiles[CatalogTextView(catalog, catalog->engines[engineIndexes[index]].id)] = std::move(profile);
		}
		OrderedJson snapshot = {{"authored_case_id", text(configuration->authoredCaseId)},
		                        {"authored_fixture_revision", configuration->authoredFixtureRevision},
		                        {"shape_preset", presets[record.shapePreset]},
		                        {"mode", configuration->mode == RunConfigurationMode_Custom ? "custom" : "authored"},
		                        {"case_definition", std::move(definition)},
		                        {"engine_settings", std::move(profiles)}};
		*json = snapshot.dump();
		return ArenaStatus_Ok;
	}
	catch (const OrderedJson::exception&)
	{
		return SetError(error, "run_configuration_write");
	}
}

namespace
{
ArenaStatus ParseRunConfigurationDocument(std::string_view json, OrderedJson* document, StatusRecord* error)
{
	std::vector<std::vector<std::string>> objectKeys;
	int duplicateKey = 0;
	const auto inspectKey = [&objectKeys, &duplicateKey](int depth, OrderedJson::parse_event_t event, OrderedJson& value) -> bool
	{
		if (event == OrderedJson::parse_event_t::object_start)
		{
			objectKeys.resize(static_cast<std::size_t>(depth) + 2);
			objectKeys[depth + 1].clear();
		}
		else if (event == OrderedJson::parse_event_t::key)
		{
			std::vector<std::string>& keys = objectKeys[depth];
			const std::string& key = value.get_ref<const std::string&>();
			if (std::find(keys.begin(), keys.end(), key) != keys.end()) duplicateKey = 1;
			keys.push_back(key);
		}
		return true;
	};
	*document = OrderedJson::parse(json, inspectKey);
	if (duplicateKey != 0) return SetError(error, "run_configuration_duplicate_key");
	return ArenaStatus_Ok;
}
}

ArenaStatus AdmitRunConfigurationJsonKeys(std::string_view json, StatusRecord* error)
{
	try
	{
		OrderedJson document;
		return ParseRunConfigurationDocument(json, &document, error);
	}
	catch (const OrderedJson::exception&)
	{
		return SetError(error, "run_configuration_read");
	}
}

ArenaStatus ReadRunConfigurationSnapshot(const Catalog* catalog, std::string_view json,
                                         std::span<const std::uint32_t> engineIndexes,
                                         EffectiveRunConfiguration* configuration, StatusRecord* error,
                                         RunSettings* editableSettings, RunConfigurationReadPurpose purpose)
{
	try
	{
		OrderedJson snapshot;
		if (ParseRunConfigurationDocument(json, &snapshot, error) != ArenaStatus_Ok)
			return error->code;
		EffectiveRunConfiguration restoredConfiguration = {};
		const int legacy = snapshot.contains("solvers") ? 1 : 0;
		if (ValidateKeys(
		        snapshot,
		        legacy != 0 ? std::initializer_list<std::string_view>{"authored_case_id", "authored_fixture_revision", "shape_preset", "mode", "case_definition", "solvers"}
		                    : std::initializer_list<std::string_view>{"authored_case_id", "authored_fixture_revision", "shape_preset", "mode", "case_definition", "engine_settings"},
		        "run_configuration", error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		std::string_view authoredId;
		std::string_view presetName;
		std::string_view mode;
		std::uint32_t revision = 0;
		if (RequiredString(snapshot, "authored_case_id", "run_configuration", &authoredId, error) != ArenaStatus_Ok ||
		    RequiredString(snapshot, "shape_preset", "run_configuration", &presetName, error) != ArenaStatus_Ok ||
		    RequiredString(snapshot, "mode", "run_configuration", &mode, error) != ArenaStatus_Ok ||
		    RequiredUnsigned(snapshot, "authored_fixture_revision", "run_configuration", 1, &revision, error) !=
		        ArenaStatus_Ok ||
		    (mode != "authored" && mode != "custom"))
			return SetError(error, "run_configuration_identity");
		const CaseShapePreset preset = presetName == "authored"      ? CaseShapePreset_Authored
		                               : presetName == "sphere"      ? CaseShapePreset_Sphere
		                               : presetName == "capsule"     ? CaseShapePreset_Capsule
		                               : presetName == "convex_hull" ? CaseShapePreset_ConvexHull
		                                                             : static_cast<CaseShapePreset>(255);
		if (preset > CaseShapePreset_ConvexHull)
			return SetError(error, "run_configuration_shape");
		const OrderedJson& definition = snapshot.at("case_definition");
		if (definition.at("id").get_ref<const OrderedJson::string_t&>() != authoredId ||
		    definition.at("fixture_revision") != revision)
			return SetError(error, "run_configuration_authored_identity");
		const OrderedJson& profiles = snapshot.at(legacy != 0 ? "solvers" : "engine_settings");
		if (!profiles.is_object() || profiles.size() != engineIndexes.size())
			return SetError(error, "run_configuration_solvers");
		std::unique_ptr<Catalog> saved(new (std::nothrow) Catalog{});
		if (saved == nullptr)
			return SetError(error, "run_configuration_workspace");
		saved->engineCount = catalog->engineCount;
		for (std::uint32_t index = 0; index < catalog->engineCount; ++index)
		{
			saved->engines[index].settings = catalog->engines[index].settings;
			if (StoreText(saved.get(), &saved->engines[index].id,
			              CatalogTextView(catalog, catalog->engines[index].id)) != ArenaStatus_Ok)
				return SetError(error, "run_configuration_text_capacity");
		}
		const OrderedJson document = {{"schema_version", 5u},
		                              {"default_public_case", definition.at("slug")},
		                              {"cases", OrderedJson::array({definition})}};
		if (ParseCases(document, saved.get(), error) != ArenaStatus_Ok ||
		    saved->caseCount != (preset == CaseShapePreset_Authored ? 1u : 2u))
			return SetError(error, "run_configuration_case_definition");
		const std::uint32_t caseIndex = saved->caseCount - 1;
		if (saved->cases[caseIndex].shapePreset != preset)
			return SetError(error, "run_configuration_selected_shape");
		RunSettings settings = {};
		if (ResetRunSettings(saved.get(), caseIndex, &settings, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		for (std::uint32_t engine : engineIndexes)
		{
			if (engine >= catalog->engineCount)
				return SetError(error, "run_configuration_engine_index");
			const OrderedJson& profile = profiles.at(CatalogTextView(catalog, catalog->engines[engine].id));
			const OrderedJson& values = legacy != 0 ? profile : profile.at("solver");
			EngineRunSettings& selected = settings.engineSettings[engine];
			if (legacy != 0)
			{
				selected.solverStabilizationPresence = purpose == RunConfigurationReadPurpose_Editable
				    ? selected.solverStabilizationPresence : PresenceStatus_Absent;
			}
			else
			{
				const int query = CaseExecutionIsQuery(settings.caseInputs.fixtureKind);
				const int ragdoll = settings.caseInputs.fixtureKind == CaseFixtureKind_RagdollStairTumble;
				const int stabilization = (catalog->engines[engine].settings.solverStabilizationFixtures &
				                           (1u << settings.caseInputs.fixtureKind)) != 0;
				if (!profile.is_object() || profile.size() != (query != 0 ? 1u : 5u + (ragdoll != 0 ? 2u : 0u) + (stabilization != 0 ? 1u : 0u)))
					return SetError(error, "run_configuration_engine_fields");
				RunSettingsArgumentState seen = {};
				for (OrderedJson::const_iterator item = profile.begin(); item != profile.end(); ++item)
				{
					if (item.key() == "solver")
						continue;
					if (query != 0)
						return SetError(error, "run_configuration_query_physics");
					const int toggle = item.key() == "sleep_mode" || item.key() == "continuous_collision_mode" || item.key() == "solver_stabilization";
					if ((toggle != 0 && !item->is_string()) || (toggle == 0 && !item->is_number()))
						return SetError(error, "run_configuration_engine_field_type");
					const std::string value = toggle != 0 ? item->get<std::string>() : item->dump();
					const std::string assignment = std::string(CatalogTextView(catalog, catalog->engines[engine].id)) + ":" + item.key() + "=" + value;
					if (ApplyRunSettingsArgument(saved.get(), "--physics", assignment, &settings, &seen, error) != ArenaStatus_Ok)
						return ArenaStatus_InvalidResult;
				}
				const std::uint32_t required = (1u << EnginePhysicsField_Friction) | (1u << EnginePhysicsField_Restitution) |
				    (1u << EnginePhysicsField_SleepMode) | (1u << EnginePhysicsField_ContinuousCollisionMode) |
				    (ragdoll != 0 ? (1u << EnginePhysicsField_LinearDamping) | (1u << EnginePhysicsField_AngularDamping) : 0u) |
				    (stabilization != 0 ? 1u << EnginePhysicsField_SolverStabilization : 0u);
				if (query == 0 && seen.physicsFields[engine] != required)
					return SetError(error, "run_configuration_engine_fields");
			}
			if (!values.is_object())
				return SetError(error, "run_configuration_solver_values");
			CaseNativeSolver& solver = settings.engineSettings[engine].solver;
			solver = {};
			for (OrderedJson::const_iterator item = values.begin(); item != values.end(); ++item)
			{
				CaseSolverField field = CaseSolverField_Count;
				if (ParseNativeSolverField(item.key(), &field) != ArenaStatus_Ok || !item->is_number_unsigned() ||
				    item->get<std::uint64_t>() > UINT32_MAX)
					return SetError(error, "run_configuration_solver_field");
				solver.supportedFields |= 1u << field;
				solver.values[field] = item->get<std::uint32_t>();
			}
		}
		if (ComposeCaseSettings(saved.get(), caseIndex, &settings, &restoredConfiguration, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		for (std::size_t ordinal = 0; ordinal < engineIndexes.size(); ++ordinal)
			restoredConfiguration.selectedEngineSettings[ordinal] = settings.engineSettings[engineIndexes[ordinal]];
		const CaseRecord& record = saved->cases[caseIndex];
		for (std::uint32_t index = 0; index < record.observationCount; ++index)
		{
			const ObservationDeclaration& declared = saved->observations[record.observationOffset + index];
			const ObservationDeclaration& derived = restoredConfiguration.observations[index];
			if (declared.expectedValuePresence == PresenceStatus_Present &&
			    declared.valueType == ObservationValueType_Uint64 &&
			    declared.expectedUnsigned != derived.expectedUnsigned)
				return SetError(error, "run_configuration_observation_expectation");
			for (std::uint32_t sample = 0; sample < declared.sampleIndexCount; ++sample)
				if (saved->observationSampleIndices[declared.sampleIndicesOffset + sample] !=
				    restoredConfiguration.sampleIndices[derived.sampleIndicesOffset + sample])
					return SetError(error, "run_configuration_observation_schedule");
		}
		restoredConfiguration.mode = mode == "custom" ? RunConfigurationMode_Custom : RunConfigurationMode_Authored;
		if (purpose == RunConfigurationReadPurpose_Editable)
		{
			std::uint32_t liveCase = 0;
			const std::string_view id = RunConfigurationTextView(&restoredConfiguration, restoredConfiguration.benchmarkCase.id);
			if (CaseIndex(*catalog, id, &liveCase) != ArenaStatus_Ok)
				return SetError(error, "run_configuration_case_unavailable");
			RunSettings authored = {};
			if (ResetRunSettings(catalog, liveCase, &authored, error) != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
			settings.caseIndex = liveCase;
			settings.caseInputs.friction = authored.caseInputs.friction;
			settings.caseInputs.restitution = authored.caseInputs.restitution;
			settings.caseInputs.sleepMode = authored.caseInputs.sleepMode;
			settings.caseInputs.continuousCollisionMode = authored.caseInputs.continuousCollisionMode;
			if (settings.caseInputs.fixtureKind == CaseFixtureKind_RagdollStairTumble)
			{
				settings.caseInputs.ragdoll.linearDamping = authored.caseInputs.ragdoll.linearDamping;
				settings.caseInputs.ragdoll.angularDamping = authored.caseInputs.ragdoll.angularDamping;
			}
			for (std::uint32_t engine = 0; engine < catalog->engineCount; ++engine)
				if (std::find(engineIndexes.begin(), engineIndexes.end(), engine) == engineIndexes.end())
					settings.engineSettings[engine] = authored.engineSettings[engine];
			EffectiveRunConfiguration admitted = {};
			if (ComposeRunSettings(catalog, liveCase, &settings, engineIndexes, &admitted, error) != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
		}
		*configuration = restoredConfiguration;
		if (editableSettings != nullptr)
			*editableSettings = settings;
		return ArenaStatus_Ok;
	}
	catch (const OrderedJson::exception&)
	{
		return SetError(error, "run_configuration_read");
	}
}

}
