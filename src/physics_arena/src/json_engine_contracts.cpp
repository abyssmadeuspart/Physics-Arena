#include "json_contracts_internal.h"
#include "physics_arena/run_settings.h"

#include <cmath>
#include <cstdio>

#include <initializer_list>
#include <string_view>

namespace physics_arena
{
namespace
{
ArenaStatus ParseThreadSupport(const OrderedJson& value, std::string_view engineId, EngineRecord* engine,
                               Catalog* catalog, StatusRecord* error)
{
	OrderedJson::const_iterator counts = value.find("supported_thread_counts");
	if (ValidateKeys(value,
	                 counts == value.end()
	                     ? std::initializer_list<std::string_view>{"mode", "requested_worker_count",
						                                           "effective_worker_count", "main_thread_participates"}
						 : std::initializer_list<std::string_view>{"mode", "requested_worker_count",
						                                           "effective_worker_count", "main_thread_participates",
						                                           "supported_thread_counts"},
	                 engineId, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	std::string_view text;
	if (RequiredString(value, "mode", engineId, &text, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	engine->threadSupportMode =
	    text == "single"
	        ? ThreadSupportMode_Single
	        : (text == "host_bounded"
	               ? ThreadSupportMode_HostBounded
	               : (text == "explicit_thread_counts" ? ThreadSupportMode_Explicit : ThreadSupportMode_Unknown));
	if (engine->threadSupportMode == ThreadSupportMode_Unknown)
		return SetErrorParts(error, {"thread_support_mode engine=", engineId});
	if (RequiredString(value, "requested_worker_count", engineId, &text, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	engine->requestedWorkerPolicy =
	    text == "thread_count"
	        ? WorkerCountPolicy_ThreadCount
	        : (text == "thread_count_minus_one" ? WorkerCountPolicy_ThreadCountMinusOne : WorkerCountPolicy_Unknown);
	if (RequiredString(value, "effective_worker_count", engineId, &text, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	engine->effectiveWorkerPolicy =
	    text == "thread_count"
	        ? WorkerCountPolicy_ThreadCount
	        : (text == "thread_count_minus_one" ? WorkerCountPolicy_ThreadCountMinusOne : WorkerCountPolicy_Unknown);
	if (RequiredString(value, "main_thread_participates", engineId, &text, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	engine->mainThreadParticipation =
	    text == "yes" ? MainThreadParticipation_Yes
		              : (text == "no" ? MainThreadParticipation_No : MainThreadParticipation_Unknown);
	if (engine->requestedWorkerPolicy == WorkerCountPolicy_Unknown ||
	    engine->effectiveWorkerPolicy == WorkerCountPolicy_Unknown ||
	    engine->mainThreadParticipation == MainThreadParticipation_Unknown)
		return SetErrorParts(error, {"thread_support_policy engine=", engineId});
	if (engine->threadSupportMode == ThreadSupportMode_Explicit)
	{
		if (counts == value.end() || !counts->is_array() || counts->empty() || counts->size() > kThreadCountCapacity)
			return SetErrorParts(error, {"supported_thread_counts engine=", engineId});
		engine->supportedThreadCountsOffset = catalog->valueCount;
		for (const OrderedJson& countValue : *counts)
		{
			if (!countValue.is_number_unsigned() || countValue.get<std::uint64_t>() == 0 ||
			    countValue.get<std::uint64_t>() > UINT32_MAX || catalog->valueCount >= catalog->values.size())
				return SetErrorParts(error, {"supported_thread_count engine=", engineId});
			const std::uint32_t count = countValue.get<std::uint32_t>();
			if (engine->supportedThreadCount != 0 && count <= catalog->values[catalog->valueCount - 1])
				return SetErrorParts(error, {"supported_thread_count_order engine=", engineId});
			catalog->values[catalog->valueCount++] = count;
			engine->supportedThreadCount += 1;
		}
	}
	else if (counts != value.end())
		return SetErrorParts(error, {"unexpected_supported_thread_counts engine=", engineId});
	return ArenaStatus_Ok;
}

ArenaStatus BuildEngineSets(Catalog* catalog, StatusRecord* error)
{
	for (std::uint32_t engineIndex = 0; engineIndex < catalog->engineCount; ++engineIndex)
	{
		const EngineRecord& engine = catalog->engines[engineIndex];
		for (std::uint32_t membership = 0; membership < engine.engineSetCount; ++membership)
		{
			const std::string_view id =
			    CatalogTextView(catalog, catalog->textValues[engine.engineSetsOffset + membership]);
			if (id != "core" && id != "default_release")
				return SetErrorParts(error, {"invalid_engine_set set=", id});
			std::uint32_t setIndex = 0;
			while (setIndex < catalog->engineSetCount &&
			       CatalogTextView(catalog, catalog->engineSets[setIndex].id) != id)
				++setIndex;
			if (setIndex == catalog->engineSetCount)
			{
				if (catalog->engineSetCount >= catalog->engineSets.size() ||
				    StoreText(catalog, &catalog->engineSets[setIndex].id, id) != ArenaStatus_Ok)
					return SetError(error, "engine_set_capacity");
				catalog->engineSetCount += 1;
			}
		}
	}
	for (std::uint32_t setIndex = 0; setIndex < catalog->engineSetCount; ++setIndex)
	{
		EngineSetRecord& set = catalog->engineSets[setIndex];
		const std::string_view id = CatalogTextView(catalog, catalog->engineSets[setIndex].id);
		set.engineIndexesOffset = catalog->valueCount;
		for (std::uint32_t engineIndex = 0; engineIndex < catalog->engineCount; ++engineIndex)
		{
			const EngineRecord& engine = catalog->engines[engineIndex];
			for (std::uint32_t membership = 0; membership < engine.engineSetCount; ++membership)
			{
				if (CatalogTextView(catalog, catalog->textValues[engine.engineSetsOffset + membership]) != id)
					continue;
				if (catalog->valueCount >= catalog->values.size())
					return SetError(error, "engine_set_member_capacity");
				catalog->values[catalog->valueCount++] = engineIndex;
				set.engineCount += 1;
			}
		}
		if (id == "default_release")
		{
			catalog->defaultReleaseSetIndex = setIndex;
			catalog->defaultReleaseSetPresence = PresenceStatus_Present;
		}
	}
	return catalog->defaultReleaseSetPresence == PresenceStatus_Present
	           ? ArenaStatus_Ok
			   : SetError(error, "required_default_engine_set_missing");
}

} // namespace

ArenaStatus ParseEngines(const OrderedJson& document, Catalog* catalog, StatusRecord* error)
{
	if (ValidateSchema(document, 2, "config/engines.json", error) != ArenaStatus_Ok ||
	    ValidateKeys(document, {"schema_version", "engines"}, "config/engines.json", error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	OrderedJson::const_iterator engines = document.find("engines");
	if (engines == document.end() || !engines->is_array() || engines->empty() || engines->size() > kEngineCapacity)
		return SetError(error, "invalid_engines");
	for (const OrderedJson& value : *engines)
	{
		if (ValidateKeys(value,
		                 {"id", "display_name", "color", "sets", "headless_status", "thread_support",
		                  "supported_case_families", "artifact_manifest"},
		                 "engine", error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		EngineRecord& engine = catalog->engines[catalog->engineCount];
		std::string_view id;
		std::string_view text;
		if (RequiredString(value, "id", "engine", &id, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		std::uint32_t duplicate = 0;
		if (EngineIndex(*catalog, id, &duplicate) == ArenaStatus_Ok)
			return SetErrorParts(error, {"duplicate_engine_id=", id});
		if (StoreText(catalog, &engine.id, id) != ArenaStatus_Ok ||
		    RequiredString(value, "display_name", id, &text, error) != ArenaStatus_Ok ||
		    StoreText(catalog, &engine.displayName, text) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		const OrderedJson::const_iterator families = value.find("supported_case_families");
		if (families == value.end() || !families->is_array() || families->empty() ||
		    families->size() > catalog->caseCount)
			return SetErrorParts(error, {"supported_case_families engine=", id});
		for (const OrderedJson& family : *families)
		{
			std::uint32_t caseIndex = 0;
			if (!family.is_string() ||
			    CaseIndex(*catalog, family.get<std::string_view>(), &caseIndex) != ArenaStatus_Ok ||
			    catalog->cases[caseIndex].shapePreset != CaseShapePreset_Authored ||
			    family.get<std::string_view>() != CatalogTextView(catalog, catalog->cases[caseIndex].id) ||
			    (engine.supportedCaseFamilyMask & (1u << caseIndex)) != 0)
				return SetErrorParts(error, {"supported_case_families engine=", id});
			engine.supportedCaseFamilyMask |= 1u << caseIndex;
		}
		OrderedJson::const_iterator color = value.find("color");
		if (color == value.end() || !color->is_number_unsigned() || color->get<std::uint64_t>() > 0xffffff)
			return SetErrorParts(error, {"color engine=", id});
		engine.colorRgb = color->get<std::uint32_t>();
		OrderedJson::const_iterator sets = value.find("sets");
		if (sets == value.end() || !sets->is_array() || sets->size() > kEngineSetCapacity ||
		    catalog->textValueCount + sets->size() > catalog->textValues.size())
			return SetErrorParts(error, {"sets engine=", id});
		engine.engineSetsOffset = catalog->textValueCount;
		for (const OrderedJson& setValue : *sets)
		{
			if (!setValue.is_string() || setValue.get_ref<const OrderedJson::string_t&>().empty())
				return SetErrorParts(error, {"set engine=", id});
			const std::string_view setId = setValue.get_ref<const OrderedJson::string_t&>();
			for (std::uint32_t prior = 0; prior < engine.engineSetCount; ++prior)
				if (CatalogTextView(catalog, catalog->textValues[engine.engineSetsOffset + prior]) == setId)
					return SetErrorParts(error, {"duplicate_engine_set engine=", id});
			if (StoreText(catalog, &catalog->textValues[catalog->textValueCount++], setId) != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
			engine.engineSetCount += 1;
		}
		if (RequiredString(value, "headless_status", id, &text, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		engine.runStatus = ParseRouteStatus(text);
		if (engine.runStatus == RouteStatus_Unknown)
			return SetErrorParts(error, {"route_status engine=", id});
		OrderedJson::const_iterator threadSupport = value.find("thread_support");
		if (threadSupport == value.end() ||
		    ParseThreadSupport(*threadSupport, id, &engine, catalog, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		if (RequiredString(value, "artifact_manifest", id, &text, error) != ArenaStatus_Ok ||
		    SafeRelativePath(text) != ArenaStatus_Ok ||
		    StoreText(catalog, &engine.releaseArtifactManifest, text) != ArenaStatus_Ok)
			return SetErrorParts(error, {"artifact_manifest engine=", id});
		catalog->engineCount += 1;
	}
	return BuildEngineSets(catalog, error);
}

ArenaStatus LoadEngineSettings(const wchar_t* repositoryRoot, Catalog* catalog, StatusRecord* error)
{
	constexpr std::string_view fixtureNames[] = {"",
	                                             "open_container_falling_pile",
	                                             "box_contact_islands",
	                                             "spatial_query_trace",
	                                             "ragdoll_stair_tumble",
	                                             "large_pyramid",
	                                             "pyramid_wall",
	                                             "ray_tracing"};
	for (std::uint32_t index = 0; index < catalog->engineCount; ++index)
	{
		EngineRecord& engine = catalog->engines[index];
		const std::string_view id = CatalogTextView(catalog, engine.id);
		std::array<char, 256> path = {};
		const int length =
		    std::snprintf(path.data(), path.size(), "src/%.*s/engine.json", static_cast<int>(id.size()), id.data());
		OrderedJson document;
		if (length <= 0 || static_cast<std::size_t>(length) >= path.size() ||
		    LoadDocument(repositoryRoot, path.data(), &document, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		std::string_view sourceId;
		if (ValidateSchema(document, 2, path.data(), error) != ArenaStatus_Ok ||
		    RequiredString(document, "engine_id", path.data(), &sourceId, error) != ArenaStatus_Ok || sourceId != id)
			return SetErrorParts(error, {"native_settings_identity engine=", id});
		const OrderedJson::const_iterator settings = document.find("native_settings");
		if (settings == document.end() || ValidateKeys(*settings,
		                                               id == "unity_physics" ? std::initializer_list<std::string_view>{
		                                                "solver", "sleep_enabled_fixtures", "sleep_disabled_fixtures",
		                                                "continuous_collision_fixtures", "maximum_restitution",
		                                                "ragdoll_damping_mode", "continuous_collision_semantics", "solver_stabilization"}
		                                               : std::initializer_list<std::string_view>{
		                                                "solver", "sleep_enabled_fixtures", "sleep_disabled_fixtures",
		                                                "continuous_collision_fixtures", "maximum_restitution",
		                                                "ragdoll_damping_mode", "continuous_collision_semantics"},
		                                               id, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		std::string_view damping;
		std::string_view collision;
		if (RequiredString(*settings, "ragdoll_damping_mode", id, &damping, error) != ArenaStatus_Ok ||
		    RequiredString(*settings, "continuous_collision_semantics", id, &collision, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		if (damping != "native_coefficient" && damping != "per_second_fraction" && damping != "fixed_zero")
			return SetErrorParts(error, {"native_ragdoll_damping_mode engine=", id});
		engine.settings.ragdollDampingMode = damping == "native_coefficient" ? RagdollDampingMode_NativeCoefficient :
		    damping == "per_second_fraction" ? RagdollDampingMode_PerSecondFraction : RagdollDampingMode_FixedZero;
		constexpr std::string_view collisionNames[] = {"native_toggle", "passive_continuous", "discrete_linear_cast",
		                                               "discrete_swept_ccd", "unavailable"};
		std::uint32_t semantic = 0;
		while (semantic < std::size(collisionNames) && collisionNames[semantic] != collision)
			++semantic;
		if (semantic == std::size(collisionNames))
			return SetErrorParts(error, {"native_continuous_collision_semantics engine=", id});
		engine.settings.continuousCollisionSemantics = static_cast<ContinuousCollisionSemantics>(semantic);
		if (id == "unity_physics")
		{
			const OrderedJson& stabilization = settings->at("solver_stabilization");
			std::string_view toggle;
			if (ValidateKeys(stabilization, {"authored_default", "fixtures"}, id, error) != ArenaStatus_Ok ||
			    RequiredString(stabilization, "authored_default", id, &toggle, error) != ArenaStatus_Ok ||
			    (toggle != "enabled" && toggle != "disabled") || !stabilization.at("fixtures").is_array())
				return SetErrorParts(error, {"native_solver_stabilization engine=", id});
			engine.settings.solverStabilizationDefault = toggle == "enabled" ? CaseExecutionToggle_Enabled : CaseExecutionToggle_Disabled;
			for (const OrderedJson& fixture : stabilization.at("fixtures"))
			{
				std::uint32_t bit = 0;
				for (std::uint32_t kind = 1; kind < std::size(fixtureNames); ++kind)
					if (fixture.is_string() && fixture.get<std::string_view>() == fixtureNames[kind] &&
					    !CaseExecutionIsQuery(static_cast<CaseFixtureKind>(kind)))
						bit = 1u << kind;
				if (bit == 0 || (engine.settings.solverStabilizationFixtures & bit) != 0)
					return SetErrorParts(error, {"native_solver_stabilization_fixture engine=", id});
				engine.settings.solverStabilizationFixtures |= bit;
			}
		}
		const OrderedJson& solver = settings->at("solver");
		if (!solver.is_object() || solver.empty() || solver.size() > CaseSolverField_Count)
			return SetErrorParts(error, {"native_solver_fields engine=", id});
		for (OrderedJson::const_iterator value = solver.begin(); value != solver.end(); ++value)
		{
			CaseSolverField field = CaseSolverField_Count;
			if (ParseNativeSolverField(value.key(), &field) != ArenaStatus_Ok ||
			    ValidateKeys(*value, {"native_name", "minimum", "maximum", "authored_default"}, id, error) !=
			        ArenaStatus_Ok)
				return SetErrorParts(error, {"native_solver_field engine=", id, " setting=", value.key()});
			NativeSolverCapability& capability = engine.settings.solver[field];
			std::string_view nativeName;
			if (RequiredString(*value, "native_name", id, &nativeName, error) != ArenaStatus_Ok ||
			    StoreText(catalog, &capability.nativeName, nativeName) != ArenaStatus_Ok ||
			    RequiredUnsigned(*value, "minimum", id, 0, &capability.minimum, error) != ArenaStatus_Ok ||
			    RequiredUnsigned(*value, "maximum", id, capability.minimum, &capability.maximum, error) !=
			        ArenaStatus_Ok ||
			    RequiredUnsigned(*value, "authored_default", id, capability.minimum, &capability.authoredDefault,
			                     error) != ArenaStatus_Ok ||
			    capability.authoredDefault > capability.maximum)
				return SetErrorParts(error, {"native_solver_range engine=", id, " setting=", value.key()});
			engine.settings.solverFields |= 1u << field;
		}
		const char* keys[] = {"sleep_enabled_fixtures", "sleep_disabled_fixtures", "continuous_collision_fixtures"};
		std::uint32_t* masks[] = {&engine.settings.sleepEnabledFixtures, &engine.settings.sleepDisabledFixtures,
		                          &engine.settings.continuousCollisionFixtures};
		for (std::uint32_t mode = 0; mode < std::size(keys); ++mode)
		{
			const OrderedJson& fixtures = settings->at(keys[mode]);
			if (!fixtures.is_array())
				return SetErrorParts(error, {"native_fixture_modes engine=", id});
			for (const OrderedJson& fixture : fixtures)
			{
				std::uint32_t bit = 0;
				for (std::uint32_t kind = 1; kind < std::size(fixtureNames); ++kind)
					if (fixture.is_string() && fixture.get<std::string_view>() == fixtureNames[kind])
						bit = 1u << kind;
				if (bit == 0 || (*masks[mode] & bit) != 0)
					return SetErrorParts(error, {"native_fixture_mode engine=", id, " setting=", keys[mode]});
				*masks[mode] |= bit;
			}
		}
		const OrderedJson& restitution = settings->at("maximum_restitution");
		if (!restitution.is_number())
			return SetErrorParts(error, {"native_restitution_range engine=", id});
		const double maximum = restitution.get<double>();
		if (!std::isfinite(maximum) || maximum < 0.0 || maximum > 1.0)
			return SetErrorParts(error, {"native_restitution_range engine=", id});
		engine.settings.maximumRestitution = static_cast<float>(maximum);
	}
	return ArenaStatus_Ok;
}

} // namespace physics_arena
