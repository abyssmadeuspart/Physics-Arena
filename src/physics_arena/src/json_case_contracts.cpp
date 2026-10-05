#include "json_contracts_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace physics_arena
{
namespace
{
ArenaStatus RequiredFiniteFloat(const OrderedJson& object, const char* key, std::string_view location, float* output,
                                StatusRecord* error)
{
	OrderedJson::const_iterator item = object.find(key);
	if (item == object.end() || !item->is_number())
		return SetErrorParts(error, {"finite_number_required location=", location, " key=", key});
	const double value = item->get<double>();
	if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
		return SetErrorParts(error, {"finite_number_required location=", location, " key=", key});
	*output = static_cast<float>(value);
	return ArenaStatus_Ok;
}

ArenaStatus RequiredVector3(const OrderedJson& object, const char* key, std::string_view location,
                            CaseVisualCameraVector* output, StatusRecord* error)
{
	OrderedJson::const_iterator item = object.find(key);
	if (item == object.end() || !item->is_array() || item->size() != 3)
		return SetErrorParts(error, {"finite_vector3_required location=", location, " key=", key});
	float* components[] = {&output->x, &output->y, &output->z};
	for (std::size_t index = 0; index < 3; ++index)
	{
		const OrderedJson& component = (*item)[index];
		if (!component.is_number())
			return SetErrorParts(error, {"finite_vector3_required location=", location, " key=", key});
		const double value = component.get<double>();
		if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
			return SetErrorParts(error, {"finite_vector3_required location=", location, " key=", key});
		*components[index] = static_cast<float>(value);
	}
	return ArenaStatus_Ok;
}

ArenaStatus RequiredVector3(const OrderedJson& object, const char* key, std::string_view location,
                            CaseExecutionVector3* output, StatusRecord* error)
{
	CaseVisualCameraVector value = {};
	if (RequiredVector3(object, key, location, &value, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	*output = {value.x, value.y, value.z};
	return ArenaStatus_Ok;
}

ArenaStatus RequiredPositiveVector3(const OrderedJson& object, const char* key, std::string_view location,
                                    CaseExecutionVector3* output, StatusRecord* error)
{
	if (RequiredVector3(object, key, location, output, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	if (output->x <= 0.0f || output->y <= 0.0f || output->z <= 0.0f)
		return SetErrorParts(error, {"fixture_positive case=", location, " key=", key});
	return ArenaStatus_Ok;
}

template <std::size_t Size>
ArenaStatus RequiredUnsignedArray(const OrderedJson& object, const char* key, std::string_view location,
                                  std::uint32_t (&output)[Size], StatusRecord* error)
{
	OrderedJson::const_iterator item = object.find(key);
	if (item == object.end() || !item->is_array() || item->size() != Size)
		return SetErrorParts(error, {"fixture_unsigned_array case=", location, " key=", key});
	for (std::size_t index = 0; index < Size; ++index)
	{
		if (!(*item)[index].is_number_unsigned() || (*item)[index].get<std::uint64_t>() == 0 ||
		    (*item)[index].get<std::uint64_t>() > UINT32_MAX)
			return SetErrorParts(error, {"fixture_unsigned_array case=", location, " key=", key});
		output[index] = (*item)[index].get<std::uint32_t>();
	}
	return ArenaStatus_Ok;
}

template <std::size_t Size>
ArenaStatus RequiredFiniteArray(const OrderedJson& object, const char* key, std::string_view location,
                                float (&output)[Size], StatusRecord* error)
{
	OrderedJson::const_iterator item = object.find(key);
	if (item == object.end() || !item->is_array() || item->size() != Size)
		return SetErrorParts(error, {"fixture_number_array case=", location, " key=", key});
	for (std::size_t index = 0; index < Size; ++index)
	{
		if (!(*item)[index].is_number())
			return SetErrorParts(error, {"fixture_number_array case=", location, " key=", key});
		const double value = (*item)[index].get<double>();
		if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
			return SetErrorParts(error, {"fixture_number_array case=", location, " key=", key});
		output[index] = static_cast<float>(value);
	}
	return ArenaStatus_Ok;
}

ArenaStatus RequiredPositiveFloat(const OrderedJson& object, const char* key, std::string_view location, float* output,
                                  StatusRecord* error)
{
	if (RequiredFiniteFloat(object, key, location, output, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	if (*output <= 0.0f)
		return SetErrorParts(error, {"fixture_positive case=", location, " key=", key});
	return ArenaStatus_Ok;
}

ArenaStatus RequiredNonnegativeFloat(const OrderedJson& object, const char* key, std::string_view location,
                                     float* output, StatusRecord* error)
{
	if (RequiredFiniteFloat(object, key, location, output, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	if (*output < 0.0f)
		return SetErrorParts(error, {"fixture_nonnegative case=", location, " key=", key});
	return ArenaStatus_Ok;
}

ArenaStatus RequiredToggle(const OrderedJson& object, const char* key, std::string_view location,
                           CaseExecutionToggle* output, StatusRecord* error)
{
	std::string_view value;
	if (RequiredString(object, key, location, &value, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	if (value == "disabled")
		*output = CaseExecutionToggle_Disabled;
	else if (value == "enabled")
		*output = CaseExecutionToggle_Enabled;
	else
		return SetErrorParts(error, {"fixture_toggle case=", location, " key=", key});
	return ArenaStatus_Ok;
}

ArenaStatus RequiredBoxArray(const OrderedJson& object, const char* key, std::string_view location,
                             CaseExecutionBox* output, std::uint16_t capacity, std::uint16_t* count,
                             StatusRecord* error)
{
	OrderedJson::const_iterator values = object.find(key);
	if (values == object.end() || !values->is_array() || values->empty() || values->size() > capacity)
		return SetErrorParts(error, {"fixture_box_count case=", location, " key=", key});
	for (const OrderedJson& value : *values)
	{
		if (ValidateKeys(value, {"center", "half_extents"}, location, error) != ArenaStatus_Ok ||
		    RequiredVector3(value, "center", location, &output[*count].center, error) != ArenaStatus_Ok ||
		    RequiredPositiveVector3(value, "half_extents", location, &output[*count].halfExtents, error) !=
		        ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		*count += 1;
	}
	return ArenaStatus_Ok;
}

double VectorLengthSquared(CaseVisualCameraVector value)
{
	return static_cast<double>(value.x) * value.x + static_cast<double>(value.y) * value.y +
	       static_cast<double>(value.z) * value.z;
}

CaseVisualCameraVector Subtract(CaseVisualCameraVector left, CaseVisualCameraVector right)
{
	return {left.x - right.x, left.y - right.y, left.z - right.z};
}

PresenceStatus ValidCameraBasis(CaseVisualCameraVector direction, CaseVisualCameraVector up)
{
	const double directionLength = VectorLengthSquared(direction);
	const double upLength = VectorLengthSquared(up);
	const CaseVisualCameraVector cross = {
	    direction.y * up.z - direction.z * up.y,
	    direction.z * up.x - direction.x * up.z,
	    direction.x * up.y - direction.y * up.x,
	};
	return directionLength > 1.0e-12 && upLength > 1.0e-12 &&
	               VectorLengthSquared(cross) > directionLength * upLength * 1.0e-12
	           ? PresenceStatus_Present
			   : PresenceStatus_Absent;
}

ArenaStatus ParseCaseVisualCamera(const OrderedJson& value, std::string_view caseId, std::uint32_t dynamicBodyCount,
                                  CaseVisualCameraPolicy* policy, StatusRecord* error)
{
	if (!value.is_object())
		return SetErrorParts(error, {"visual_camera_object case=", caseId});
	std::string_view mode;
	if (RequiredString(value, "mode", caseId, &mode, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	policy->mode = mode == "fit_scene"
	                   ? CaseVisualCameraMode_FitScene
	                   : (mode == "fit_bounds"
	                          ? CaseVisualCameraMode_FitBounds
	                          : (mode == "fixed" ? CaseVisualCameraMode_Fixed
	                                             : (mode == "follow_stable_slot" ? CaseVisualCameraMode_FollowStableSlot
	                                                                             : CaseVisualCameraMode_Unknown)));
	if (policy->mode == CaseVisualCameraMode_Unknown)
		return SetErrorParts(error, {"visual_camera_mode case=", caseId});

	if (policy->mode == CaseVisualCameraMode_FitScene || policy->mode == CaseVisualCameraMode_FitBounds)
	{
		const std::initializer_list<std::string_view> keys =
		    policy->mode == CaseVisualCameraMode_FitScene
		        ? std::initializer_list<std::string_view>{"mode", "direction", "up", "vertical_fov_degrees",
		                                                  "viewport_fill"}
		        : std::initializer_list<std::string_view>{
		              "mode", "minimum", "maximum", "direction", "up", "vertical_fov_degrees", "viewport_fill"};
		if (ValidateKeys(value, keys, caseId, error) != ArenaStatus_Ok ||
		    RequiredVector3(value, "direction", caseId, &policy->direction, error) != ArenaStatus_Ok ||
		    RequiredVector3(value, "up", caseId, &policy->up, error) != ArenaStatus_Ok ||
		    RequiredFiniteFloat(value, "vertical_fov_degrees", caseId, &policy->verticalFovDegrees, error) !=
		        ArenaStatus_Ok ||
		    RequiredFiniteFloat(value, "viewport_fill", caseId, &policy->viewportFill, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		if (ValidCameraBasis(policy->direction, policy->up) != PresenceStatus_Present)
			return SetErrorParts(error, {"visual_camera_basis case=", caseId});
		if (policy->verticalFovDegrees <= 0.0f || policy->verticalFovDegrees >= 179.0f ||
		    policy->viewportFill <= 0.0f || policy->viewportFill > 1.0f)
			return SetErrorParts(error, {"visual_camera_projection case=", caseId});
		if (policy->mode == CaseVisualCameraMode_FitBounds)
		{
			if (RequiredVector3(value, "minimum", caseId, &policy->minimum, error) != ArenaStatus_Ok ||
			    RequiredVector3(value, "maximum", caseId, &policy->maximum, error) != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
			if (policy->minimum.x >= policy->maximum.x || policy->minimum.y >= policy->maximum.y ||
			    policy->minimum.z >= policy->maximum.z)
				return SetErrorParts(error, {"visual_camera_bounds case=", caseId});
		}
		return ArenaStatus_Ok;
	}

	const std::initializer_list<std::string_view> keys =
	    policy->mode == CaseVisualCameraMode_Fixed
	        ? std::initializer_list<std::string_view>{"mode",       "eye",      "target", "up", "vertical_fov_degrees",
	                                                  "near_plane", "far_plane"}
	        : std::initializer_list<std::string_view>{"mode", "stable_slot",          "eye_offset", "target_offset",
	                                                  "up",   "vertical_fov_degrees", "near_plane", "far_plane"};
	if (ValidateKeys(value, keys, caseId, error) != ArenaStatus_Ok ||
	    RequiredVector3(value, "up", caseId, &policy->up, error) != ArenaStatus_Ok ||
	    RequiredFiniteFloat(value, "vertical_fov_degrees", caseId, &policy->verticalFovDegrees, error) !=
	        ArenaStatus_Ok ||
	    RequiredFiniteFloat(value, "near_plane", caseId, &policy->nearPlane, error) != ArenaStatus_Ok ||
	    RequiredFiniteFloat(value, "far_plane", caseId, &policy->farPlane, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	if (policy->verticalFovDegrees <= 0.0f || policy->verticalFovDegrees >= 179.0f || policy->nearPlane <= 0.0f ||
	    policy->nearPlane >= policy->farPlane)
		return SetErrorParts(error, {"visual_camera_clipping case=", caseId});
	CaseVisualCameraVector view = {};
	if (policy->mode == CaseVisualCameraMode_Fixed)
	{
		if (RequiredVector3(value, "eye", caseId, &policy->eye, error) != ArenaStatus_Ok ||
		    RequiredVector3(value, "target", caseId, &policy->target, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		view = Subtract(policy->target, policy->eye);
	}
	else
	{
		if (RequiredUnsigned(value, "stable_slot", caseId, 0, &policy->stableSlot, error) != ArenaStatus_Ok ||
		    policy->stableSlot >= dynamicBodyCount ||
		    RequiredVector3(value, "eye_offset", caseId, &policy->eyeOffset, error) != ArenaStatus_Ok ||
		    RequiredVector3(value, "target_offset", caseId, &policy->targetOffset, error) != ArenaStatus_Ok)
			return SetErrorParts(error, {"visual_camera_follow case=", caseId});
		view = Subtract(policy->targetOffset, policy->eyeOffset);
	}
	return ValidCameraBasis(view, policy->up) == PresenceStatus_Present
	           ? ArenaStatus_Ok
			   : SetErrorParts(error, {"visual_camera_basis case=", caseId});
}

ArenaStatus ParseResultGroups(const OrderedJson& values, std::string_view caseId, CaseRecord* record, Catalog* catalog,
                              StatusRecord* error)
{
	if (!values.is_array() || values.size() > kObservationPerCaseCapacity ||
	    catalog->resultGroupCount + values.size() > catalog->resultGroups.size())
		return SetErrorParts(error, {"result_groups case=", caseId});
	record->resultGroupOffset = catalog->resultGroupCount;
	for (const OrderedJson& value : values)
	{
		if (ValidateKeys(value, {"id", "label"}, caseId, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		std::string_view id;
		std::string_view label;
		if (RequiredString(value, "id", caseId, &id, error) != ArenaStatus_Ok ||
		    ValidWireId(id) != PresenceStatus_Present ||
		    RequiredString(value, "label", caseId, &label, error) != ArenaStatus_Ok)
			return SetErrorParts(error, {"result_group_record case=", caseId});
		for (std::uint32_t ordinal = 0; ordinal < record->resultGroupCount; ++ordinal)
			if (CatalogTextView(catalog, catalog->resultGroups[record->resultGroupOffset + ordinal].id) == id)
				return SetErrorParts(error, {"duplicate_result_group_id case=", caseId, " id=", id});
		ResultGroupRecord& group = catalog->resultGroups[catalog->resultGroupCount++];
		if (StoreText(catalog, &group.id, id) != ArenaStatus_Ok ||
		    StoreText(catalog, &group.label, label) != ArenaStatus_Ok)
			return SetErrorParts(error, {"result_group_capacity case=", caseId});
		record->resultGroupCount += 1;
	}
	return ArenaStatus_Ok;
}

ArenaStatus ParseObservations(const OrderedJson& values, std::string_view caseId, CaseRecord* record, Catalog* catalog,
                              StatusRecord* error)
{
	if (!values.is_array() || values.size() > kObservationPerCaseCapacity ||
	    catalog->observationCount + values.size() > catalog->observations.size())
		return SetErrorParts(error, {"observations case=", caseId});
	record->observationOffset = catalog->observationCount;
	std::uint32_t repeatRows = 0;
	for (const OrderedJson& value : values)
	{
		OrderedJson::const_iterator expected = value.find("expected_value");
		if (ValidateKeys(value,
		                 expected == value.end()
		                     ? std::initializer_list<std::string_view>{"id", "label", "result_group_id", "value_type",
							                                           "unit", "role", "phase_id", "sample_indices"}
							 : std::initializer_list<std::string_view>{"id", "label", "result_group_id", "value_type",
							                                           "unit", "role", "phase_id", "sample_indices",
							                                           "expected_value"},
		                 caseId, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		ObservationDeclaration& declaration = catalog->observations[catalog->observationCount];
		std::string_view id;
		std::string_view text;
		if (RequiredString(value, "id", caseId, &id, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		for (std::uint32_t index = record->observationOffset; index < catalog->observationCount; ++index)
			if (CatalogTextView(catalog, catalog->observations[index].id) == id)
				return SetErrorParts(error, {"duplicate_observation_id case=", caseId, " id=", id});
		if (StoreText(catalog, &declaration.id, id) != ArenaStatus_Ok ||
		    RequiredString(value, "label", caseId, &text, error) != ArenaStatus_Ok ||
		    StoreText(catalog, &declaration.label, text) != ArenaStatus_Ok ||
		    RequiredString(value, "unit", caseId, &text, error) != ArenaStatus_Ok ||
		    StoreText(catalog, &declaration.unit, text) != ArenaStatus_Ok ||
		    RequiredString(value, "phase_id", caseId, &text, error) != ArenaStatus_Ok ||
		    StoreText(catalog, &declaration.phaseId, text) != ArenaStatus_Ok ||
		    RequiredString(value, "value_type", caseId, &text, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		declaration.valueType = text == "uint64"
		                            ? ObservationValueType_Uint64
		                            : (text == "float64" ? ObservationValueType_Float64 : ObservationValueType_Unknown);
		if (declaration.valueType == ObservationValueType_Unknown)
			return SetErrorParts(error, {"observation_value_type case=", caseId});
		if (RequiredString(value, "result_group_id", caseId, &text, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		std::uint32_t groupMatches = 0;
		for (std::uint32_t ordinal = 0; ordinal < record->resultGroupCount; ++ordinal)
			if (CatalogTextView(catalog, catalog->resultGroups[record->resultGroupOffset + ordinal].id) == text)
			{
				declaration.resultGroupOrdinal = ordinal;
				groupMatches += 1;
			}
		if (groupMatches != 1)
			return SetErrorParts(error, {"observation_result_group case=", caseId, " id=", id});
		if (RequiredString(value, "role", caseId, &text, error) != ArenaStatus_Ok)
			return SetErrorParts(error, {"observation_value_type case=", caseId});
		declaration.role =
		    text == "validity_zero"
		        ? ObservationRole_ValidityZero
		        : (text == "validity_exact" ? ObservationRole_ValidityExact
		                                    : (text == "quality" ? ObservationRole_Quality
		                                                         : (text == "performance" ? ObservationRole_Performance
		                                                                                  : ObservationRole_Unknown)));
		if (declaration.role == ObservationRole_Unknown)
			return SetErrorParts(error, {"observation_role case=", caseId});
		const PresenceStatus validity =
		    declaration.role == ObservationRole_ValidityZero || declaration.role == ObservationRole_ValidityExact
		        ? PresenceStatus_Present
		        : PresenceStatus_Absent;
		if ((validity == PresenceStatus_Present) != (expected != value.end()))
			return SetErrorParts(error, {"observation_expected_value case=", caseId, " id=", id});
		if (validity == PresenceStatus_Present)
		{
			declaration.expectedValuePresence = PresenceStatus_Present;
			if (declaration.valueType == ObservationValueType_Uint64)
			{
				if (!expected->is_number_unsigned())
					return SetErrorParts(error, {"observation_expected_type case=", caseId});
				declaration.expectedUnsigned = expected->get<std::uint64_t>();
				declaration.expectedFloat64 = static_cast<double>(declaration.expectedUnsigned);
			}
			else
			{
				if (!expected->is_number() || !std::isfinite(expected->get<double>()))
					return SetErrorParts(error, {"observation_expected_type case=", caseId});
				declaration.expectedFloat64 = expected->get<double>();
			}
			if (declaration.role == ObservationRole_ValidityZero && declaration.expectedFloat64 != 0.0)
				return SetErrorParts(error, {"observation_expected_zero case=", caseId});
		}
		OrderedJson::const_iterator samples = value.find("sample_indices");
		if (samples == value.end() || !samples->is_array() || samples->empty() ||
		    samples->size() > kObservationSamplePerDeclarationCapacity ||
		    catalog->observationSampleIndexCount + samples->size() > catalog->observationSampleIndices.size())
			return SetErrorParts(error, {"observation_samples case=", caseId, " id=", id});
		declaration.sampleIndicesOffset = catalog->observationSampleIndexCount;
		std::uint32_t prior = 0;
		for (const OrderedJson& sample : *samples)
		{
			if (!sample.is_number_unsigned() || sample.get<std::uint64_t>() > UINT32_MAX)
				return SetErrorParts(error, {"observation_sample_type case=", caseId});
			const std::uint32_t index = sample.get<std::uint32_t>();
			if (declaration.sampleIndexCount != 0 && index <= prior)
				return SetErrorParts(error, {"observation_sample_order case=", caseId});
			catalog->observationSampleIndices[catalog->observationSampleIndexCount++] = index;
			declaration.sampleIndexCount += 1;
			prior = index;
		}
		for (std::uint32_t index = record->observationOffset; index < catalog->observationCount; ++index)
		{
			const ObservationDeclaration& priorDeclaration = catalog->observations[index];
			if (priorDeclaration.resultGroupOrdinal != declaration.resultGroupOrdinal)
				continue;
			if (CatalogTextView(catalog, priorDeclaration.phaseId) != CatalogTextView(catalog, declaration.phaseId) ||
			    priorDeclaration.sampleIndexCount != declaration.sampleIndexCount)
				return SetErrorParts(error, {"observation_group_schedule case=", caseId, " id=", id});
			for (std::uint32_t sample = 0; sample < declaration.sampleIndexCount; ++sample)
				if (catalog->observationSampleIndices[priorDeclaration.sampleIndicesOffset + sample] !=
				    catalog->observationSampleIndices[declaration.sampleIndicesOffset + sample])
					return SetErrorParts(error, {"observation_group_schedule case=", caseId, " id=", id});
		}
		repeatRows += declaration.sampleIndexCount;
		if (repeatRows > kObservationRepeatRowCapacity)
			return SetErrorParts(error, {"observation_repeat_rows case=", caseId});
		catalog->observationCount += 1;
		record->observationCount += 1;
	}
	return ArenaStatus_Ok;
}

ArenaStatus CopyExecutionText(std::string_view source, char* destination, std::string_view caseId, StatusRecord* error)
{
	if (source.empty() || source.size() >= kCaseExecutionTextCapacity)
		return SetErrorParts(error, {"case_execution_text case=", caseId});
	std::copy(source.begin(), source.end(), destination);
	destination[source.size()] = '\0';
	return ArenaStatus_Ok;
}

ArenaStatus ParseFixtureCommon(const OrderedJson& fixture, std::string_view caseId, CaseExecutionSpec* spec,
                               StatusRecord* error)
{
	if (RequiredVector3(fixture, "gravity", caseId, &spec->gravity, error) != ArenaStatus_Ok ||
	    RequiredToggle(fixture, "sleep_mode", caseId, &spec->sleepMode, error) != ArenaStatus_Ok ||
	    RequiredToggle(fixture, "continuous_collision_mode", caseId, &spec->continuousCollisionMode, error) !=
	        ArenaStatus_Ok ||
	    RequiredNonnegativeFloat(fixture, "friction", caseId, &spec->friction, error) != ArenaStatus_Ok ||
	    RequiredNonnegativeFloat(fixture, "restitution", caseId, &spec->restitution, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	return ArenaStatus_Ok;
}

ArenaStatus ParseOpenContainer(const OrderedJson& fixture, std::string_view caseId, CaseExecutionSpec* spec,
                               StatusRecord* error)
{
	if (ValidateKeys(fixture,
	                 {"gravity", "sleep_mode", "continuous_collision_mode", "friction", "restitution", "density",
	                  "dynamic_grid", "dynamic_half_extents", "dynamic_spacing", "dynamic_initial_y", "static_boxes"},
	                 caseId, error) != ArenaStatus_Ok ||
	    ParseFixtureCommon(fixture, caseId, spec, error) != ArenaStatus_Ok ||
	    RequiredPositiveFloat(fixture, "density", caseId, &spec->openContainer.density, error) != ArenaStatus_Ok ||
	    RequiredUnsignedArray(fixture, "dynamic_grid", caseId, spec->openContainer.dynamicGrid, error) !=
	        ArenaStatus_Ok ||
	    RequiredPositiveVector3(fixture, "dynamic_half_extents", caseId, &spec->openContainer.dynamicHalfExtents,
	                            error) != ArenaStatus_Ok ||
	    RequiredPositiveVector3(fixture, "dynamic_spacing", caseId, &spec->openContainer.dynamicSpacing, error) !=
	        ArenaStatus_Ok ||
	    RequiredPositiveFloat(fixture, "dynamic_initial_y", caseId, &spec->openContainer.dynamicInitialY, error) !=
	        ArenaStatus_Ok ||
	    RequiredBoxArray(fixture, "static_boxes", caseId, spec->openContainer.staticBoxes,
	                     kCaseExecutionStaticBoxCapacity, &spec->openContainer.staticBoxCount, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	const std::uint64_t dynamics = static_cast<std::uint64_t>(spec->openContainer.dynamicGrid[0]) *
	                               spec->openContainer.dynamicGrid[1] * spec->openContainer.dynamicGrid[2];
	if (dynamics != spec->dynamicBodyCount || spec->openContainer.staticBoxCount != spec->staticBodyCount ||
	    spec->kinematicBodyCount != 0 || spec->bodyCount != dynamics + spec->openContainer.staticBoxCount ||
	    spec->shapeCount != spec->bodyCount || spec->visualInstanceCount != spec->bodyCount ||
	    spec->meshTriangleCount != 0 || spec->queryCount != 0 || spec->constraintCount != 0 ||
	    spec->visualDebugPrimitiveCount != 0)
		return SetErrorParts(error, {"fixture_count case=", caseId});
	return ArenaStatus_Ok;
}

ArenaStatus ParseContactIslands(const OrderedJson& fixture, std::string_view caseId, CaseExecutionSpec* spec,
                                StatusRecord* error)
{
	if (ValidateKeys(fixture,
	                 {"gravity", "sleep_mode", "continuous_collision_mode", "friction", "restitution", "density",
	                  "island_grid", "island_spacing", "body_grid", "body_half_extents", "body_spacing",
	                  "body_initial_y", "floor_half_extents"},
	                 caseId, error) != ArenaStatus_Ok ||
	    ParseFixtureCommon(fixture, caseId, spec, error) != ArenaStatus_Ok ||
	    RequiredPositiveFloat(fixture, "density", caseId, &spec->contactIslands.density, error) != ArenaStatus_Ok ||
	    RequiredUnsignedArray(fixture, "island_grid", caseId, spec->contactIslands.islandGrid, error) !=
	        ArenaStatus_Ok ||
	    RequiredFiniteArray(fixture, "island_spacing", caseId, spec->contactIslands.islandSpacing, error) !=
	        ArenaStatus_Ok ||
	    RequiredUnsignedArray(fixture, "body_grid", caseId, spec->contactIslands.bodyGrid, error) != ArenaStatus_Ok ||
	    RequiredPositiveVector3(fixture, "body_half_extents", caseId, &spec->contactIslands.bodyHalfExtents, error) !=
	        ArenaStatus_Ok ||
	    RequiredPositiveVector3(fixture, "body_spacing", caseId, &spec->contactIslands.bodySpacing, error) !=
	        ArenaStatus_Ok ||
	    RequiredPositiveFloat(fixture, "body_initial_y", caseId, &spec->contactIslands.bodyInitialY, error) !=
	        ArenaStatus_Ok ||
	    RequiredPositiveVector3(fixture, "floor_half_extents", caseId, &spec->contactIslands.floorHalfExtents, error) !=
	        ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	if (spec->contactIslands.islandSpacing[0] <= 0.0f || spec->contactIslands.islandSpacing[1] <= 0.0f)
		return SetErrorParts(error, {"fixture_positive case=", caseId, " key=island_spacing"});
	const std::uint64_t islandCount =
	    static_cast<std::uint64_t>(spec->contactIslands.islandGrid[0]) * spec->contactIslands.islandGrid[1];
	const std::uint64_t bodiesPerIsland = static_cast<std::uint64_t>(spec->contactIslands.bodyGrid[0]) *
	                                      spec->contactIslands.bodyGrid[1] * spec->contactIslands.bodyGrid[2];
	if (islandCount * bodiesPerIsland != spec->dynamicBodyCount || islandCount != spec->staticBodyCount ||
	    spec->kinematicBodyCount != 0 || spec->bodyCount != spec->dynamicBodyCount + spec->staticBodyCount ||
	    spec->shapeCount != spec->bodyCount || spec->visualInstanceCount != spec->bodyCount ||
	    spec->meshTriangleCount != 0 || spec->queryCount != 0 || spec->constraintCount != 0 ||
	    spec->visualDebugPrimitiveCount != 0)
		return SetErrorParts(error, {"fixture_count case=", caseId});
	return ArenaStatus_Ok;
}

ArenaStatus ParseSpatialQuery(const OrderedJson& fixture, std::string_view caseId, CaseExecutionSpec* spec,
                              StatusRecord* error)
{
	if (ValidateKeys(fixture,
	                 {"gravity", "sleep_mode", "continuous_collision_mode", "friction", "restitution", "static_grid",
	                  "static_half_extents", "static_spacing", "static_base_center", "ray_count", "sphere_cast_count",
	                  "overlap_count", "query_distance", "sphere_cast_radius", "overlap_half_extents", "miss_offset",
	                  "debug_samples_per_family"},
	                 caseId, error) != ArenaStatus_Ok ||
	    ParseFixtureCommon(fixture, caseId, spec, error) != ArenaStatus_Ok ||
	    RequiredUnsignedArray(fixture, "static_grid", caseId, spec->spatialQuery.staticGrid, error) != ArenaStatus_Ok ||
	    RequiredPositiveVector3(fixture, "static_half_extents", caseId, &spec->spatialQuery.staticHalfExtents, error) !=
	        ArenaStatus_Ok ||
	    RequiredPositiveVector3(fixture, "static_spacing", caseId, &spec->spatialQuery.staticSpacing, error) !=
	        ArenaStatus_Ok ||
	    RequiredVector3(fixture, "static_base_center", caseId, &spec->spatialQuery.staticBaseCenter, error) !=
	        ArenaStatus_Ok ||
	    RequiredUnsigned(fixture, "ray_count", caseId, 1, &spec->spatialQuery.rayCount, error) != ArenaStatus_Ok ||
	    RequiredUnsigned(fixture, "sphere_cast_count", caseId, 1, &spec->spatialQuery.sphereCastCount, error) !=
	        ArenaStatus_Ok ||
	    RequiredUnsigned(fixture, "overlap_count", caseId, 1, &spec->spatialQuery.overlapCount, error) !=
	        ArenaStatus_Ok ||
	    RequiredPositiveFloat(fixture, "query_distance", caseId, &spec->spatialQuery.queryDistance, error) !=
	        ArenaStatus_Ok ||
	    RequiredPositiveFloat(fixture, "sphere_cast_radius", caseId, &spec->spatialQuery.sphereCastRadius, error) !=
	        ArenaStatus_Ok ||
	    RequiredPositiveVector3(fixture, "overlap_half_extents", caseId, &spec->spatialQuery.overlapHalfExtents,
	                            error) != ArenaStatus_Ok ||
	    RequiredPositiveFloat(fixture, "miss_offset", caseId, &spec->spatialQuery.missOffset, error) !=
	        ArenaStatus_Ok ||
	    RequiredUnsigned(fixture, "debug_samples_per_family", caseId, 1, &spec->spatialQuery.debugSamplesPerFamily,
	                     error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	const std::uint64_t statics = static_cast<std::uint64_t>(spec->spatialQuery.staticGrid[0]) *
	                              spec->spatialQuery.staticGrid[1] * spec->spatialQuery.staticGrid[2];
	const std::uint64_t queries = static_cast<std::uint64_t>(spec->spatialQuery.rayCount) +
	                              spec->spatialQuery.sphereCastCount + spec->spatialQuery.overlapCount;
	if (statics != spec->staticBodyCount || spec->dynamicBodyCount != 0 || spec->kinematicBodyCount != 0 ||
	    spec->bodyCount != statics || spec->shapeCount != statics || spec->visualInstanceCount != statics ||
	    spec->meshTriangleCount != 0 || queries != spec->queryCount || spec->constraintCount != 0 ||
	    static_cast<std::uint64_t>(spec->spatialQuery.debugSamplesPerFamily) * 3 != spec->visualDebugPrimitiveCount)
		return SetErrorParts(error, {"fixture_count case=", caseId});
	return ArenaStatus_Ok;
}

ArenaStatus ParseRagdoll(const OrderedJson& fixture, std::string_view caseId, CaseExecutionSpec* spec,
                         StatusRecord* error)
{
	if (ValidateKeys(fixture,
	                 {"gravity",
	                  "sleep_mode",
	                  "continuous_collision_mode",
	                  "linked_collision_mode",
	                  "friction",
	                  "restitution",
	                  "linear_damping",
	                  "angular_damping",
	                  "part_mass",
	                  "ragdoll_grid",
	                  "column_spacing",
	                  "row_spacing",
	                  "base_height_offset",
	                  "pitch_degrees",
	                  "yaw_pattern_degrees",
	                  "trigger_row_speed",
	                  "follower_row_speed",
	                  "staircase",
	                  "extra_static_boxes",
	                  "parts",
	                  "links"},
	                 caseId, error) != ArenaStatus_Ok ||
	    ParseFixtureCommon(fixture, caseId, spec, error) != ArenaStatus_Ok ||
	    RequiredToggle(fixture, "linked_collision_mode", caseId, &spec->ragdoll.linkedCollisionMode, error) !=
	        ArenaStatus_Ok ||
	    RequiredNonnegativeFloat(fixture, "linear_damping", caseId, &spec->ragdoll.linearDamping, error) !=
	        ArenaStatus_Ok ||
	    RequiredNonnegativeFloat(fixture, "angular_damping", caseId, &spec->ragdoll.angularDamping, error) !=
	        ArenaStatus_Ok ||
	    RequiredPositiveFloat(fixture, "part_mass", caseId, &spec->ragdoll.partMass, error) != ArenaStatus_Ok ||
	    RequiredUnsignedArray(fixture, "ragdoll_grid", caseId, spec->ragdoll.ragdollGrid, error) != ArenaStatus_Ok ||
	    RequiredPositiveFloat(fixture, "column_spacing", caseId, &spec->ragdoll.columnSpacing, error) !=
	        ArenaStatus_Ok ||
	    RequiredPositiveFloat(fixture, "row_spacing", caseId, &spec->ragdoll.rowSpacing, error) != ArenaStatus_Ok ||
	    RequiredPositiveFloat(fixture, "base_height_offset", caseId, &spec->ragdoll.baseHeightOffset, error) !=
	        ArenaStatus_Ok ||
	    RequiredFiniteFloat(fixture, "pitch_degrees", caseId, &spec->ragdoll.pitchDegrees, error) != ArenaStatus_Ok ||
	    RequiredPositiveFloat(fixture, "trigger_row_speed", caseId, &spec->ragdoll.triggerRowSpeed, error) !=
	        ArenaStatus_Ok ||
	    RequiredPositiveFloat(fixture, "follower_row_speed", caseId, &spec->ragdoll.followerRowSpeed, error) !=
	        ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	OrderedJson::const_iterator yaw = fixture.find("yaw_pattern_degrees");
	if (yaw == fixture.end() || !yaw->is_array() || yaw->empty() || yaw->size() > kCaseExecutionYawCapacity)
		return SetErrorParts(error, {"ragdoll_yaw_count case=", caseId});
	for (const OrderedJson& value : *yaw)
	{
		if (!value.is_number() || !std::isfinite(value.get<double>()) ||
		    std::abs(value.get<double>()) > std::numeric_limits<float>::max())
			return SetErrorParts(error, {"ragdoll_yaw_value case=", caseId});
		spec->ragdoll.yawPatternDegrees[spec->ragdoll.yawPatternCount++] = value.get<float>();
	}
	OrderedJson::const_iterator staircase = fixture.find("staircase");
	if (staircase == fixture.end() ||
	    ValidateKeys(*staircase, {"count", "rise", "depth", "half_width", "half_height", "half_depth"}, caseId,
	                 error) != ArenaStatus_Ok ||
	    RequiredUnsigned(*staircase, "count", caseId, 1, &spec->ragdoll.stairCount, error) != ArenaStatus_Ok ||
	    RequiredPositiveFloat(*staircase, "rise", caseId, &spec->ragdoll.stairRise, error) != ArenaStatus_Ok ||
	    RequiredPositiveFloat(*staircase, "depth", caseId, &spec->ragdoll.stairDepth, error) != ArenaStatus_Ok ||
	    RequiredPositiveFloat(*staircase, "half_width", caseId, &spec->ragdoll.stairHalfWidth, error) !=
	        ArenaStatus_Ok ||
	    RequiredPositiveFloat(*staircase, "half_height", caseId, &spec->ragdoll.stairHalfHeight, error) !=
	        ArenaStatus_Ok ||
	    RequiredPositiveFloat(*staircase, "half_depth", caseId, &spec->ragdoll.stairHalfDepth, error) !=
	        ArenaStatus_Ok ||
	    RequiredBoxArray(fixture, "extra_static_boxes", caseId, spec->ragdoll.extraStaticBoxes,
	                     kCaseExecutionStaticBoxCapacity, &spec->ragdoll.extraStaticBoxCount, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	OrderedJson::const_iterator parts = fixture.find("parts");
	if (parts == fixture.end() || !parts->is_array() || parts->empty() ||
	    parts->size() > kCaseExecutionRagdollPartCapacity)
		return SetErrorParts(error, {"ragdoll_part_count case=", caseId});
	for (const OrderedJson& value : *parts)
	{
		std::string_view shape;
		if (RequiredString(value, "shape", caseId, &shape, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		CaseExecutionRagdollPart& part = spec->ragdoll.parts[spec->ragdoll.partCount];
		if (shape == "box")
		{
			if (ValidateKeys(value, {"shape", "half_extents", "center"}, caseId, error) != ArenaStatus_Ok ||
			    RequiredPositiveVector3(value, "half_extents", caseId, &part.halfExtents, error) != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
			part.shape = CaseExecutionShape_Box;
		}
		else if (shape == "sphere")
		{
			if (ValidateKeys(value, {"shape", "radius", "center"}, caseId, error) != ArenaStatus_Ok ||
			    RequiredPositiveFloat(value, "radius", caseId, &part.radius, error) != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
			part.shape = CaseExecutionShape_Sphere;
		}
		else
			return SetErrorParts(error, {"ragdoll_part_shape case=", caseId});
		if (RequiredVector3(value, "center", caseId, &part.center, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		spec->ragdoll.partCount += 1;
	}
	OrderedJson::const_iterator links = fixture.find("links");
	if (links == fixture.end() || !links->is_array() || links->size() > kCaseExecutionRagdollLinkCapacity)
		return SetErrorParts(error, {"ragdoll_link_count case=", caseId});
	for (const OrderedJson& value : *links)
	{
		if (ValidateKeys(value, {"parent_part", "child_part", "anchor"}, caseId, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		std::uint32_t parent = 0;
		std::uint32_t child = 0;
		CaseExecutionRagdollLink& link = spec->ragdoll.links[spec->ragdoll.linkCount];
		if (RequiredUnsigned(value, "parent_part", caseId, 0, &parent, error) != ArenaStatus_Ok ||
		    RequiredUnsigned(value, "child_part", caseId, 0, &child, error) != ArenaStatus_Ok ||
		    parent >= spec->ragdoll.partCount || child >= spec->ragdoll.partCount || parent == child ||
		    RequiredVector3(value, "anchor", caseId, &link.anchor, error) != ArenaStatus_Ok)
			return SetErrorParts(error, {"ragdoll_link case=", caseId});
		link.parentPart = static_cast<std::uint16_t>(parent);
		link.childPart = static_cast<std::uint16_t>(child);
		spec->ragdoll.linkCount += 1;
	}
	const std::uint64_t ragdolls =
	    static_cast<std::uint64_t>(spec->ragdoll.ragdollGrid[0]) * spec->ragdoll.ragdollGrid[1];
	if (ragdolls * spec->ragdoll.partCount != spec->dynamicBodyCount ||
	    spec->ragdoll.stairCount + spec->ragdoll.extraStaticBoxCount != spec->staticBodyCount ||
	    ragdolls * spec->ragdoll.linkCount != spec->constraintCount || spec->kinematicBodyCount != 0 ||
	    spec->bodyCount != spec->dynamicBodyCount + spec->staticBodyCount || spec->shapeCount != spec->bodyCount ||
	    spec->visualInstanceCount + spec->visualDebugPrimitiveCount != spec->bodyCount ||
	    spec->visualDebugPrimitiveCount != 3 || spec->meshTriangleCount != 0 || spec->queryCount != 0)
		return SetErrorParts(error, {"fixture_count case=", caseId});
	return ArenaStatus_Ok;
}

ArenaStatus ValidateLargePyramidRecipe(const CaseExecutionSpec* spec, std::string_view caseId, StatusRecord* error)
{
	const CaseExecutionLargePyramid& pyramid = spec->largePyramid;
	const std::uint64_t rowCount = pyramid.rowCount;
	if (rowCount > std::numeric_limits<std::uint32_t>::max() / rowCount)
		return SetErrorParts(error, {"fixture_count case=", caseId});
	const std::uint64_t cubeCount = rowCount * (rowCount + 1u) * (2u * rowCount + 1u) / 6u;
	const double pyramidMaximumZ = static_cast<double>(pyramid.baseCenter.z) +
	                               0.5 * static_cast<double>(rowCount - 1u) * pyramid.boxSpacing.z +
	                               pyramid.boxHalfExtents.z;
	const double launchLengthSquared =
	    static_cast<double>(pyramid.projectileLaunchVelocity.x) * pyramid.projectileLaunchVelocity.x +
	    static_cast<double>(pyramid.projectileLaunchVelocity.y) * pyramid.projectileLaunchVelocity.y +
	    static_cast<double>(pyramid.projectileLaunchVelocity.z) * pyramid.projectileLaunchVelocity.z;
	const double spacingLengthSquared =
	    static_cast<double>(pyramid.projectileCenterSpacing.x) * pyramid.projectileCenterSpacing.x +
	    static_cast<double>(pyramid.projectileCenterSpacing.y) * pyramid.projectileCenterSpacing.y +
	    static_cast<double>(pyramid.projectileCenterSpacing.z) * pyramid.projectileCenterSpacing.z;
	if (cubeCount + pyramid.projectileCount != spec->dynamicBodyCount || spec->staticBodyCount != 1u ||
	    spec->kinematicBodyCount != 0u || spec->bodyCount != spec->dynamicBodyCount + 1u ||
	    spec->shapeCount != spec->bodyCount || spec->visualInstanceCount != spec->bodyCount ||
	    spec->meshTriangleCount != 0u || spec->queryCount != 0u || spec->constraintCount != 0u ||
	    spec->visualDebugPrimitiveCount != 0u)
		return SetErrorParts(error, {"fixture_count case=", caseId});
	if (pyramid.boxSpacing.x != 2.0f * pyramid.boxHalfExtents.x ||
	    pyramid.boxSpacing.y != 2.0f * pyramid.boxHalfExtents.y ||
	    pyramid.boxSpacing.z != 2.0f * pyramid.boxHalfExtents.z ||
	    pyramid.baseCenter.y - pyramid.boxHalfExtents.y != 0.0f ||
	    spacingLengthSquared < 4.0 * pyramid.projectileRadius * pyramid.projectileRadius ||
	    launchLengthSquared <= 0.0 || pyramid.projectileLaunchAfterWorkUnits == 0)
		return SetErrorParts(error, {"large_pyramid_contract case=", caseId});
	for (std::uint32_t index = 0; index < pyramid.projectileCount; ++index)
	{
		const double centerX = pyramid.projectileInitialCenter.x + index * pyramid.projectileCenterSpacing.x;
		const double centerY = pyramid.projectileInitialCenter.y + index * pyramid.projectileCenterSpacing.y;
		const double centerZ = pyramid.projectileInitialCenter.z + index * pyramid.projectileCenterSpacing.z;
		const double targetX = static_cast<double>(pyramid.baseCenter.x) - centerX;
		const double targetY = static_cast<double>(pyramid.baseCenter.y) - centerY;
		const double targetZ = static_cast<double>(pyramid.baseCenter.z) - centerZ;
		const double launchTowardPyramid = static_cast<double>(pyramid.projectileLaunchVelocity.x) * targetX +
		                                   static_cast<double>(pyramid.projectileLaunchVelocity.y) * targetY +
		                                   static_cast<double>(pyramid.projectileLaunchVelocity.z) * targetZ;
		if (centerY - pyramid.projectileRadius != 0.0 || centerZ - pyramid.projectileRadius <= pyramidMaximumZ ||
		    std::abs(centerX) + pyramid.projectileRadius > pyramid.floorHalfExtents.x ||
		    std::abs(centerZ) + pyramid.projectileRadius > pyramid.floorHalfExtents.z || launchTowardPyramid <= 0.0)
			return SetErrorParts(error, {"large_pyramid_contract case=", caseId});
	}
	return ArenaStatus_Ok;
}

ArenaStatus ParseLargePyramid(const OrderedJson& fixture, std::string_view caseId, CaseExecutionSpec* spec,
                              StatusRecord* error)
{
	CaseExecutionLargePyramid& pyramid = spec->largePyramid;
	if (ValidateKeys(fixture,
	                 {"gravity", "sleep_mode", "continuous_collision_mode", "friction", "restitution", "row_count",
	                  "box_half_extents", "box_spacing", "base_center", "floor_half_extents", "box_density",
	                  "projectile_count", "projectile_radius", "projectile_density", "projectile_initial_center",
	                  "projectile_center_spacing", "projectile_launch_velocity", "projectile_launch_after_work_units"},
	                 caseId, error) != ArenaStatus_Ok ||
	    ParseFixtureCommon(fixture, caseId, spec, error) != ArenaStatus_Ok ||
	    RequiredUnsigned(fixture, "row_count", caseId, 1, &pyramid.rowCount, error) != ArenaStatus_Ok ||
	    RequiredPositiveVector3(fixture, "box_half_extents", caseId, &pyramid.boxHalfExtents, error) !=
	        ArenaStatus_Ok ||
	    RequiredPositiveVector3(fixture, "box_spacing", caseId, &pyramid.boxSpacing, error) != ArenaStatus_Ok ||
	    RequiredVector3(fixture, "base_center", caseId, &pyramid.baseCenter, error) != ArenaStatus_Ok ||
	    RequiredPositiveVector3(fixture, "floor_half_extents", caseId, &pyramid.floorHalfExtents, error) !=
	        ArenaStatus_Ok ||
	    RequiredPositiveFloat(fixture, "box_density", caseId, &pyramid.boxDensity, error) != ArenaStatus_Ok ||
	    RequiredUnsigned(fixture, "projectile_count", caseId, 1, &pyramid.projectileCount, error) != ArenaStatus_Ok ||
	    RequiredPositiveFloat(fixture, "projectile_radius", caseId, &pyramid.projectileRadius, error) !=
	        ArenaStatus_Ok ||
	    RequiredPositiveFloat(fixture, "projectile_density", caseId, &pyramid.projectileDensity, error) !=
	        ArenaStatus_Ok ||
	    RequiredVector3(fixture, "projectile_initial_center", caseId, &pyramid.projectileInitialCenter, error) !=
	        ArenaStatus_Ok ||
	    RequiredVector3(fixture, "projectile_center_spacing", caseId, &pyramid.projectileCenterSpacing, error) !=
	        ArenaStatus_Ok ||
	    RequiredVector3(fixture, "projectile_launch_velocity", caseId, &pyramid.projectileLaunchVelocity, error) !=
	        ArenaStatus_Ok ||
	    RequiredUnsigned(fixture, "projectile_launch_after_work_units", caseId, 1,
	                     &pyramid.projectileLaunchAfterWorkUnits, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;

	return ValidateLargePyramidRecipe(spec, caseId, error);
}

ArenaStatus ParsePyramidWall(const OrderedJson& fixture, std::string_view caseId, CaseExecutionSpec* spec,
                            StatusRecord* error)
{
	CaseExecutionPyramidWall& wall = spec->pyramidWall;
	if (ValidateKeys(fixture, {"gravity", "sleep_mode", "continuous_collision_mode", "friction", "restitution",
	                           "row_count", "half_extent", "density", "floor_half_extents"}, caseId, error) != ArenaStatus_Ok ||
	    ParseFixtureCommon(fixture, caseId, spec, error) != ArenaStatus_Ok ||
	    RequiredUnsigned(fixture, "row_count", caseId, 1, &wall.rowCount, error) != ArenaStatus_Ok ||
	    RequiredPositiveFloat(fixture, "half_extent", caseId, &wall.halfExtent, error) != ArenaStatus_Ok ||
	    RequiredPositiveFloat(fixture, "density", caseId, &wall.density, error) != ArenaStatus_Ok ||
	    RequiredPositiveVector3(fixture, "floor_half_extents", caseId, &wall.floorHalfExtents, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	return ArenaStatus_Ok;
}

ArenaStatus ParseRayTracing(const OrderedJson& fixture, std::string_view caseId, CaseExecutionSpec* spec,
                           StatusRecord* error)
{
	CaseExecutionRayTracing& ray = spec->rayTracing;
	if (ValidateKeys(fixture, {"gravity", "sleep_mode", "continuous_collision_mode", "friction", "restitution",
	                           "recipe_revision", "width", "height", "view_count", "primitive_count", "mesh_count",
	                           "triangles_per_mesh", "moving_count", "seed_low", "seed_high"}, caseId, error) != ArenaStatus_Ok ||
	    ParseFixtureCommon(fixture, caseId, spec, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	for (std::pair<const char*, std::uint32_t*> field :
	     {std::pair<const char*, std::uint32_t*>{"recipe_revision", &ray.recipeRevision},
	      {"width", &ray.width}, {"height", &ray.height}, {"view_count", &ray.viewCount},
	      {"primitive_count", &ray.primitiveCount}, {"mesh_count", &ray.meshCount},
	      {"triangles_per_mesh", &ray.trianglesPerMesh}, {"moving_count", &ray.movingCount},
	      {"seed_low", &ray.seedLow}, {"seed_high", &ray.seedHigh}})
	{
		if (RequiredUnsigned(fixture, field.first, caseId, 0, field.second, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
	}
	return ArenaStatus_Ok;
}

ArenaStatus ParseCaseExecution(const OrderedJson& value, std::string_view caseId, CaseRecord* record, Catalog* catalog,
                               StatusRecord* error)
{
	CaseExecutionSpec spec = {};
	const std::string_view semantic = CatalogTextView(catalog, record->fixtureSemantic);
	spec.fixtureKind = semantic == "open_container_falling_pile"
	                       ? CaseFixtureKind_OpenContainerFallingPile
	                       : (semantic == "box_contact_islands_10k"
	                              ? CaseFixtureKind_BoxContactIslands
	                              : (semantic == "spatial_query_trace"
	                                     ? CaseFixtureKind_SpatialQueryTrace
	                                     : (semantic == "ragdoll_stair_tumble"
	                                            ? CaseFixtureKind_RagdollStairTumble
	                                            : (semantic == "large_pyramid" ? CaseFixtureKind_LargePyramid
	                                                                           : CaseFixtureKind_Unknown))));
	if (semantic == "pyramid_wall")
		spec.fixtureKind = CaseFixtureKind_PyramidWall;
	else if (semantic == "ray_tracing")
		spec.fixtureKind = CaseFixtureKind_RayTracing;
	if (spec.fixtureKind == CaseFixtureKind_Unknown ||
	    CopyExecutionText(caseId, spec.caseId, caseId, error) != ArenaStatus_Ok ||
	    CopyExecutionText(semantic, spec.fixtureSemantic, caseId, error) != ArenaStatus_Ok)
		return SetErrorParts(error, {"fixture_semantic case=", caseId});
	spec.fixtureRevision = record->fixtureRevision;
	spec.dynamicBodyCount = record->dynamicBodyCount;
	spec.kinematicBodyCount = record->kinematicBodyCount;
	spec.staticBodyCount = record->staticBodyCount;
	spec.bodyCount = record->bodyCount;
	spec.shapeCount = record->shapeCount;
	spec.visualInstanceCount = record->visualInstanceCount;
	spec.meshTriangleCount = record->meshTriangleCount;
	spec.queryCount = record->queryCount;
	spec.constraintCount = record->constraintCount;
	spec.timestepPresent = record->timestepPresence == PresenceStatus_Present ? 1 : 0;
	spec.timestepHz = record->timestepHz;
	spec.warmupWorkUnitCount = record->warmupWorkUnitCount;
	spec.measuredWorkUnitCount = record->measuredWorkUnitCount;
	if (RequiredUnsigned(value, "visual_debug_primitive_count", caseId, 0, &spec.visualDebugPrimitiveCount, error) !=
	    ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	OrderedJson::const_iterator fixture = value.find("fixture");
	if (fixture == value.end() || !fixture->is_object())
		return SetErrorParts(error, {"fixture_object case=", caseId});
	const ArenaStatus fixtureStatus = spec.fixtureKind == CaseFixtureKind_PyramidWall
	                                      ? ParsePyramidWall(*fixture, caseId, &spec, error)
	                                  : spec.fixtureKind == CaseFixtureKind_RayTracing
	                                      ? ParseRayTracing(*fixture, caseId, &spec, error)
	                                  : spec.fixtureKind == CaseFixtureKind_OpenContainerFallingPile
	                                      ? ParseOpenContainer(*fixture, caseId, &spec, error)
	                                      : (spec.fixtureKind == CaseFixtureKind_BoxContactIslands
	                                             ? ParseContactIslands(*fixture, caseId, &spec, error)
	                                             : (spec.fixtureKind == CaseFixtureKind_SpatialQueryTrace
	                                                    ? ParseSpatialQuery(*fixture, caseId, &spec, error)
	                                                    : (spec.fixtureKind == CaseFixtureKind_RagdollStairTumble
	                                                           ? ParseRagdoll(*fixture, caseId, &spec, error)
	                                                           : ParseLargePyramid(*fixture, caseId, &spec, error))));
	if (fixtureStatus != ArenaStatus_Ok ||
	    ResolveCaseExecutionPreset(&spec, CaseShapePreset_Authored, error) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	return StoreCatalogCaseExecution(catalog, record, &spec, error);
}

} // namespace

OrderedJson WriteCaseFixture(const CaseExecutionSpec& spec)
{
	const CaseExecutionVector3 gravity = spec.gravity;
	OrderedJson value = {{"gravity", {gravity.x, gravity.y, gravity.z}},
	                     {"sleep_mode", spec.sleepMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled"},
	                     {"continuous_collision_mode",
	                      spec.continuousCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled"},
	                     {"friction", spec.friction},
	                     {"restitution", spec.restitution}};
	if (spec.fixtureKind == CaseFixtureKind_OpenContainerFallingPile)
	{
		const CaseExecutionOpenContainer& fixture = spec.openContainer;
		value["density"] = fixture.density;
		value["dynamic_grid"] = fixture.dynamicGrid;
		value["dynamic_half_extents"] = {fixture.dynamicHalfExtents.x, fixture.dynamicHalfExtents.y,
		                                 fixture.dynamicHalfExtents.z};
		value["dynamic_spacing"] = {fixture.dynamicSpacing.x, fixture.dynamicSpacing.y, fixture.dynamicSpacing.z};
		value["dynamic_initial_y"] = fixture.dynamicInitialY;
		value["static_boxes"] = OrderedJson::array();
		for (std::uint16_t index = 0; index < fixture.staticBoxCount; ++index)
		{
			const CaseExecutionBox& box = fixture.staticBoxes[index];
			value["static_boxes"].push_back(
			    {{"center", {box.center.x, box.center.y, box.center.z}},
				 {"half_extents", {box.halfExtents.x, box.halfExtents.y, box.halfExtents.z}}});
		}
	}
	else if (spec.fixtureKind == CaseFixtureKind_BoxContactIslands)
	{
		const CaseExecutionContactIslands& fixture = spec.contactIslands;
		value["density"] = fixture.density;
		value["island_grid"] = fixture.islandGrid;
		value["island_spacing"] = fixture.islandSpacing;
		value["body_grid"] = fixture.bodyGrid;
		value["body_half_extents"] = {fixture.bodyHalfExtents.x, fixture.bodyHalfExtents.y, fixture.bodyHalfExtents.z};
		value["body_spacing"] = {fixture.bodySpacing.x, fixture.bodySpacing.y, fixture.bodySpacing.z};
		value["body_initial_y"] = fixture.bodyInitialY;
		value["floor_half_extents"] = {fixture.floorHalfExtents.x, fixture.floorHalfExtents.y,
		                               fixture.floorHalfExtents.z};
	}
	else if (spec.fixtureKind == CaseFixtureKind_SpatialQueryTrace)
	{
		const CaseExecutionSpatialQuery& fixture = spec.spatialQuery;
		value["static_grid"] = fixture.staticGrid;
		value["static_half_extents"] = {fixture.staticHalfExtents.x, fixture.staticHalfExtents.y,
		                                fixture.staticHalfExtents.z};
		value["static_spacing"] = {fixture.staticSpacing.x, fixture.staticSpacing.y, fixture.staticSpacing.z};
		value["static_base_center"] = {fixture.staticBaseCenter.x, fixture.staticBaseCenter.y,
		                               fixture.staticBaseCenter.z};
		value["ray_count"] = fixture.rayCount;
		value["sphere_cast_count"] = fixture.sphereCastCount;
		value["overlap_count"] = fixture.overlapCount;
		value["query_distance"] = fixture.queryDistance;
		value["sphere_cast_radius"] = fixture.sphereCastRadius;
		value["overlap_half_extents"] = {fixture.overlapHalfExtents.x, fixture.overlapHalfExtents.y,
		                                 fixture.overlapHalfExtents.z};
		value["miss_offset"] = fixture.missOffset;
		value["debug_samples_per_family"] = fixture.debugSamplesPerFamily;
	}
	else if (spec.fixtureKind == CaseFixtureKind_RagdollStairTumble)
	{
		const CaseExecutionRagdoll& fixture = spec.ragdoll;
		value["ragdoll_grid"] = fixture.ragdollGrid;
		value["column_spacing"] = fixture.columnSpacing;
		value["row_spacing"] = fixture.rowSpacing;
		value["base_height_offset"] = fixture.baseHeightOffset;
		value["pitch_degrees"] = fixture.pitchDegrees;
		value["trigger_row_speed"] = fixture.triggerRowSpeed;
		value["follower_row_speed"] = fixture.followerRowSpeed;
		value["linear_damping"] = fixture.linearDamping;
		value["angular_damping"] = fixture.angularDamping;
		value["part_mass"] = fixture.partMass;
		value["linked_collision_mode"] =
		    fixture.linkedCollisionMode == CaseExecutionToggle_Enabled ? "enabled" : "disabled";
		value["yaw_pattern_degrees"] = OrderedJson::array();
		for (std::uint16_t index = 0; index < fixture.yawPatternCount; ++index)
			value["yaw_pattern_degrees"].push_back(fixture.yawPatternDegrees[index]);
		value["staircase"] = {{"count", fixture.stairCount},
		                      {"rise", fixture.stairRise},
		                      {"depth", fixture.stairDepth},
		                      {"half_width", fixture.stairHalfWidth},
		                      {"half_height", fixture.stairHalfHeight},
		                      {"half_depth", fixture.stairHalfDepth}};
		value["extra_static_boxes"] = OrderedJson::array();
		for (std::uint16_t index = 0; index < fixture.extraStaticBoxCount; ++index)
		{
			const CaseExecutionBox& box = fixture.extraStaticBoxes[index];
			value["extra_static_boxes"].push_back(
			    {{"center", {box.center.x, box.center.y, box.center.z}},
				 {"half_extents", {box.halfExtents.x, box.halfExtents.y, box.halfExtents.z}}});
		}
		value["parts"] = OrderedJson::array();
		for (std::uint16_t index = 0; index < fixture.partCount; ++index)
		{
			const CaseExecutionRagdollPart& part = fixture.parts[index];
			OrderedJson body = {{"center", {part.center.x, part.center.y, part.center.z}}};
			if (part.shape == CaseExecutionShape_Box ||
			    (spec.shapePreset != CaseShapePreset_Authored && part.halfExtents.x > 0.0f))
			{
				body["shape"] = "box";
				body["half_extents"] = {part.halfExtents.x, part.halfExtents.y, part.halfExtents.z};
			}
			else
			{
				body["shape"] = "sphere";
				body["radius"] = part.radius;
			}
			value["parts"].push_back(std::move(body));
		}
		value["links"] = OrderedJson::array();
		for (std::uint16_t index = 0; index < fixture.linkCount; ++index)
		{
			const CaseExecutionRagdollLink& link = fixture.links[index];
			value["links"].push_back({{"parent_part", link.parentPart},
			                          {"child_part", link.childPart},
			                          {"anchor", {link.anchor.x, link.anchor.y, link.anchor.z}}});
		}
	}
	else if (spec.fixtureKind == CaseFixtureKind_LargePyramid)
	{
		const CaseExecutionLargePyramid& fixture = spec.largePyramid;
		value["row_count"] = fixture.rowCount;
		value["box_half_extents"] = {fixture.boxHalfExtents.x, fixture.boxHalfExtents.y, fixture.boxHalfExtents.z};
		value["box_spacing"] = {fixture.boxSpacing.x, fixture.boxSpacing.y, fixture.boxSpacing.z};
		value["base_center"] = {fixture.baseCenter.x, fixture.baseCenter.y, fixture.baseCenter.z};
		value["floor_half_extents"] = {fixture.floorHalfExtents.x, fixture.floorHalfExtents.y,
		                               fixture.floorHalfExtents.z};
		value["box_density"] = fixture.boxDensity;
		value["projectile_count"] = fixture.projectileCount;
		value["projectile_radius"] = fixture.projectileRadius;
		value["projectile_density"] = fixture.projectileDensity;
		value["projectile_initial_center"] = {fixture.projectileInitialCenter.x, fixture.projectileInitialCenter.y,
		                                      fixture.projectileInitialCenter.z};
		value["projectile_center_spacing"] = {fixture.projectileCenterSpacing.x, fixture.projectileCenterSpacing.y,
		                                      fixture.projectileCenterSpacing.z};
		value["projectile_launch_velocity"] = {fixture.projectileLaunchVelocity.x, fixture.projectileLaunchVelocity.y,
		                                       fixture.projectileLaunchVelocity.z};
		value["projectile_launch_after_work_units"] = fixture.projectileLaunchAfterWorkUnits;
	}
	else if (spec.fixtureKind == CaseFixtureKind_PyramidWall)
	{
		const CaseExecutionPyramidWall& wall = spec.pyramidWall;
		value["row_count"] = wall.rowCount;
		value["half_extent"] = wall.halfExtent;
		value["density"] = wall.density;
		value["floor_half_extents"] = {wall.floorHalfExtents.x, wall.floorHalfExtents.y, wall.floorHalfExtents.z};
	}
	else if (spec.fixtureKind == CaseFixtureKind_RayTracing)
	{
		const CaseExecutionRayTracing& ray = spec.rayTracing;
		value["recipe_revision"] = ray.recipeRevision;
		value["width"] = ray.width;
		value["height"] = ray.height;
		value["view_count"] = ray.viewCount;
		value["primitive_count"] = ray.primitiveCount;
		value["mesh_count"] = ray.meshCount;
		value["triangles_per_mesh"] = ray.trianglesPerMesh;
		value["moving_count"] = ray.movingCount;
		value["seed_low"] = ray.seedLow;
		value["seed_high"] = ray.seedHigh;
	}
	return value;
}

OrderedJson WriteCaseCamera(const CaseVisualCameraPolicy& camera)
{
	constexpr const char* modes[] = {"unknown", "fit_scene", "fit_bounds", "fixed", "follow_stable_slot"};
	OrderedJson value = {
	    {"mode", camera.mode >= CaseVisualCameraMode_FitScene && camera.mode <= CaseVisualCameraMode_FollowStableSlot
	                 ? modes[camera.mode]
	                 : modes[0]},
	    {"up", {camera.up.x, camera.up.y, camera.up.z}},
	    {"vertical_fov_degrees", camera.verticalFovDegrees}};
	if (camera.mode == CaseVisualCameraMode_FitScene || camera.mode == CaseVisualCameraMode_FitBounds)
	{
		value["direction"] = {camera.direction.x, camera.direction.y, camera.direction.z};
		value["viewport_fill"] = camera.viewportFill;
		if (camera.mode == CaseVisualCameraMode_FitBounds)
		{
			value["minimum"] = {camera.minimum.x, camera.minimum.y, camera.minimum.z};
			value["maximum"] = {camera.maximum.x, camera.maximum.y, camera.maximum.z};
		}
	}
	else
	{
		value["near_plane"] = camera.nearPlane;
		value["far_plane"] = camera.farPlane;
		if (camera.mode == CaseVisualCameraMode_Fixed)
		{
			value["eye"] = {camera.eye.x, camera.eye.y, camera.eye.z};
			value["target"] = {camera.target.x, camera.target.y, camera.target.z};
		}
		else
		{
			value["stable_slot"] = camera.stableSlot;
			value["eye_offset"] = {camera.eyeOffset.x, camera.eyeOffset.y, camera.eyeOffset.z};
			value["target_offset"] = {camera.targetOffset.x, camera.targetOffset.y, camera.targetOffset.z};
		}
	}
	return value;
}

ArenaStatus ValidateEditedCase(CaseExecutionSpec* spec, CaseVisualCameraPolicy* camera, StatusRecord* error)
{
	using namespace case_execution_wire_detail;
	if (spec->fixtureKind == CaseFixtureKind_OpenContainerFallingPile)
	{
		if (!ValidFloat(spec->openContainer.density) || spec->openContainer.density <= 0.0f)
			return SetError(error, "setting=density must_be_positive_finite");
		if (!ValidFloat(spec->openContainer.dynamicInitialY) || spec->openContainer.dynamicInitialY <= 0.0f)
			return SetError(error, "setting=dynamicInitialY must_be_positive_finite");
		if (!ValidVector3(spec->openContainer.dynamicHalfExtents) || spec->openContainer.dynamicHalfExtents.x <= 0.0f ||
		    spec->openContainer.dynamicHalfExtents.y <= 0.0f || spec->openContainer.dynamicHalfExtents.z <= 0.0f)
			return SetError(error, "setting=dynamicHalfExtents must_be_positive_finite");
		if (!ValidVector3(spec->openContainer.dynamicSpacing) || spec->openContainer.dynamicSpacing.x <= 0.0f ||
		    spec->openContainer.dynamicSpacing.y <= 0.0f || spec->openContainer.dynamicSpacing.z <= 0.0f)
			return SetError(error, "setting=dynamicSpacing must_be_positive_finite");
		for (std::uint16_t index = 0; index < spec->openContainer.staticBoxCount; ++index)
			if (!ValidBox(spec->openContainer.staticBoxes[index]))
				return SetError(error, "setting=static_boxes invalid_geometry");
	}
	if (spec->fixtureKind == CaseFixtureKind_BoxContactIslands)
	{
		if (!ValidFloat(spec->contactIslands.density) || spec->contactIslands.density <= 0.0f)
			return SetError(error, "setting=density must_be_positive_finite");
		if (!ValidFloat(spec->contactIslands.bodyInitialY) || spec->contactIslands.bodyInitialY <= 0.0f)
			return SetError(error, "setting=bodyInitialY must_be_positive_finite");
		if (!ValidVector3(spec->contactIslands.bodyHalfExtents) || spec->contactIslands.bodyHalfExtents.x <= 0.0f ||
		    spec->contactIslands.bodyHalfExtents.y <= 0.0f || spec->contactIslands.bodyHalfExtents.z <= 0.0f)
			return SetError(error, "setting=bodyHalfExtents must_be_positive_finite");
		if (!ValidVector3(spec->contactIslands.bodySpacing) || spec->contactIslands.bodySpacing.x <= 0.0f ||
		    spec->contactIslands.bodySpacing.y <= 0.0f || spec->contactIslands.bodySpacing.z <= 0.0f)
			return SetError(error, "setting=bodySpacing must_be_positive_finite");
		if (!ValidVector3(spec->contactIslands.floorHalfExtents) || spec->contactIslands.floorHalfExtents.x <= 0.0f ||
		    spec->contactIslands.floorHalfExtents.y <= 0.0f || spec->contactIslands.floorHalfExtents.z <= 0.0f)
			return SetError(error, "setting=floorHalfExtents must_be_positive_finite");
		for (float spacing : spec->contactIslands.islandSpacing)
			if (!ValidFloat(spacing) || spacing <= 0.0f)
				return SetError(error, "setting=island_spacing must_be_positive_finite");
	}
	if (spec->fixtureKind == CaseFixtureKind_SpatialQueryTrace)
	{
		if (!ValidFloat(spec->spatialQuery.queryDistance) || spec->spatialQuery.queryDistance <= 0.0f)
			return SetError(error, "setting=queryDistance must_be_positive_finite");
		if (!ValidFloat(spec->spatialQuery.sphereCastRadius) || spec->spatialQuery.sphereCastRadius <= 0.0f)
			return SetError(error, "setting=sphereCastRadius must_be_positive_finite");
		if (!ValidFloat(spec->spatialQuery.missOffset) || spec->spatialQuery.missOffset <= 0.0f)
			return SetError(error, "setting=missOffset must_be_positive_finite");
		if (!ValidVector3(spec->spatialQuery.staticHalfExtents) || spec->spatialQuery.staticHalfExtents.x <= 0.0f ||
		    spec->spatialQuery.staticHalfExtents.y <= 0.0f || spec->spatialQuery.staticHalfExtents.z <= 0.0f)
			return SetError(error, "setting=staticHalfExtents must_be_positive_finite");
		if (!ValidVector3(spec->spatialQuery.staticSpacing) || spec->spatialQuery.staticSpacing.x <= 0.0f ||
		    spec->spatialQuery.staticSpacing.y <= 0.0f || spec->spatialQuery.staticSpacing.z <= 0.0f)
			return SetError(error, "setting=staticSpacing must_be_positive_finite");
		if (!ValidVector3(spec->spatialQuery.overlapHalfExtents) || spec->spatialQuery.overlapHalfExtents.x <= 0.0f ||
		    spec->spatialQuery.overlapHalfExtents.y <= 0.0f || spec->spatialQuery.overlapHalfExtents.z <= 0.0f)
			return SetError(error, "setting=overlapHalfExtents must_be_positive_finite");
	}
	if (spec->fixtureKind == CaseFixtureKind_RagdollStairTumble)
	{
		if (!ValidFloat(spec->ragdoll.partMass) || spec->ragdoll.partMass <= 0.0f)
			return SetError(error, "setting=partMass must_be_positive_finite");
		if (!ValidFloat(spec->ragdoll.columnSpacing) || spec->ragdoll.columnSpacing <= 0.0f)
			return SetError(error, "setting=columnSpacing must_be_positive_finite");
		if (!ValidFloat(spec->ragdoll.rowSpacing) || spec->ragdoll.rowSpacing <= 0.0f)
			return SetError(error, "setting=rowSpacing must_be_positive_finite");
		if (!ValidFloat(spec->ragdoll.baseHeightOffset) || spec->ragdoll.baseHeightOffset <= 0.0f)
			return SetError(error, "setting=baseHeightOffset must_be_positive_finite");
		if (!ValidFloat(spec->ragdoll.triggerRowSpeed) || spec->ragdoll.triggerRowSpeed <= 0.0f)
			return SetError(error, "setting=triggerRowSpeed must_be_positive_finite");
		if (!ValidFloat(spec->ragdoll.followerRowSpeed) || spec->ragdoll.followerRowSpeed <= 0.0f)
			return SetError(error, "setting=followerRowSpeed must_be_positive_finite");
		if (!ValidFloat(spec->ragdoll.stairRise) || spec->ragdoll.stairRise <= 0.0f)
			return SetError(error, "setting=stairRise must_be_positive_finite");
		if (!ValidFloat(spec->ragdoll.stairDepth) || spec->ragdoll.stairDepth <= 0.0f)
			return SetError(error, "setting=stairDepth must_be_positive_finite");
		if (!ValidFloat(spec->ragdoll.stairHalfWidth) || spec->ragdoll.stairHalfWidth <= 0.0f)
			return SetError(error, "setting=stairHalfWidth must_be_positive_finite");
		if (!ValidFloat(spec->ragdoll.stairHalfHeight) || spec->ragdoll.stairHalfHeight <= 0.0f)
			return SetError(error, "setting=stairHalfHeight must_be_positive_finite");
		if (!ValidFloat(spec->ragdoll.stairHalfDepth) || spec->ragdoll.stairHalfDepth <= 0.0f)
			return SetError(error, "setting=stairHalfDepth must_be_positive_finite");
		const CaseExecutionRagdoll& ragdoll = spec->ragdoll;
		if (!ValidToggle(ragdoll.linkedCollisionMode) || !ValidFloat(ragdoll.linearDamping) ||
		    !ValidFloat(ragdoll.angularDamping) || ragdoll.linearDamping < 0.0f || ragdoll.angularDamping < 0.0f)
			return SetError(error, "setting=ragdoll_damping_or_linked_collision invalid");
		for (std::uint16_t index = 0; index < ragdoll.partCount; ++index)
		{
			const CaseExecutionRagdollPart& part = ragdoll.parts[index];
			if ((part.shape != CaseExecutionShape_Box && part.shape != CaseExecutionShape_Sphere) ||
			    !ValidGeometry(CaseExecutionPartGeometry(part)) || !ValidVector3(part.center))
				return SetError(error, "setting=parts invalid_geometry");
		}
		for (std::uint16_t index = 0; index < ragdoll.linkCount; ++index)
		{
			const CaseExecutionRagdollLink& link = ragdoll.links[index];
			if (link.parentPart >= ragdoll.partCount || link.childPart >= ragdoll.partCount ||
			    link.parentPart == link.childPart || !ValidVector3(link.anchor))
				return SetError(error, "setting=links invalid_part_or_anchor");
		}
	}
	if (spec->fixtureKind == CaseFixtureKind_LargePyramid)
	{
		if (!ValidFloat(spec->largePyramid.boxDensity) || spec->largePyramid.boxDensity <= 0.0f)
			return SetError(error, "setting=boxDensity must_be_positive_finite");
		if (!ValidFloat(spec->largePyramid.projectileRadius) || spec->largePyramid.projectileRadius <= 0.0f)
			return SetError(error, "setting=projectileRadius must_be_positive_finite");
		if (!ValidFloat(spec->largePyramid.projectileDensity) || spec->largePyramid.projectileDensity <= 0.0f)
			return SetError(error, "setting=projectileDensity must_be_positive_finite");
		if (!ValidVector3(spec->largePyramid.boxHalfExtents) || spec->largePyramid.boxHalfExtents.x <= 0.0f ||
		    spec->largePyramid.boxHalfExtents.y <= 0.0f || spec->largePyramid.boxHalfExtents.z <= 0.0f)
			return SetError(error, "setting=boxHalfExtents must_be_positive_finite");
		if (!ValidVector3(spec->largePyramid.boxSpacing) || spec->largePyramid.boxSpacing.x <= 0.0f ||
		    spec->largePyramid.boxSpacing.y <= 0.0f || spec->largePyramid.boxSpacing.z <= 0.0f)
			return SetError(error, "setting=boxSpacing must_be_positive_finite");
		if (!ValidVector3(spec->largePyramid.floorHalfExtents) || spec->largePyramid.floorHalfExtents.x <= 0.0f ||
		    spec->largePyramid.floorHalfExtents.y <= 0.0f || spec->largePyramid.floorHalfExtents.z <= 0.0f)
			return SetError(error, "setting=floorHalfExtents must_be_positive_finite");
		if (ValidateLargePyramidRecipe(spec, spec->caseId, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
	}
	if (camera->mode < CaseVisualCameraMode_FitScene || camera->mode > CaseVisualCameraMode_FollowStableSlot ||
	    !std::isfinite(camera->verticalFovDegrees) || camera->verticalFovDegrees <= 0.0f ||
	    camera->verticalFovDegrees >= 179.0f)
		return SetError(error, "setting=camera_mode_or_vertical_fov invalid");
	CaseVisualCameraVector view = camera->direction;
	if (camera->mode == CaseVisualCameraMode_FitScene || camera->mode == CaseVisualCameraMode_FitBounds)
	{
		if (!std::isfinite(camera->viewportFill) || camera->viewportFill <= 0.0f || camera->viewportFill > 1.0f)
			return SetError(error, "setting=viewport_fill invalid");
		if (camera->mode == CaseVisualCameraMode_FitBounds &&
		    (!std::isfinite(VectorLengthSquared(camera->minimum)) ||
		     !std::isfinite(VectorLengthSquared(camera->maximum)) || camera->minimum.x >= camera->maximum.x ||
		     camera->minimum.y >= camera->maximum.y || camera->minimum.z >= camera->maximum.z))
			return SetError(error, "setting=camera_bounds invalid");
	}
	else
	{
		if (!std::isfinite(camera->nearPlane) || !std::isfinite(camera->farPlane) || camera->nearPlane <= 0.0f ||
		    camera->farPlane <= camera->nearPlane)
			return SetError(error, "setting=camera_clipping invalid");
		if (camera->mode == CaseVisualCameraMode_FollowStableSlot && camera->stableSlot >= spec->dynamicBodyCount)
			return SetError(error, "setting=stable_slot outside_population");
		view = camera->mode == CaseVisualCameraMode_Fixed ? Subtract(camera->target, camera->eye)
		                                                  : Subtract(camera->targetOffset, camera->eyeOffset);
	}
	if (!std::isfinite(VectorLengthSquared(view)) || !std::isfinite(VectorLengthSquared(camera->up)) ||
	    ValidCameraBasis(view, camera->up) != PresenceStatus_Present)
		return SetError(error, "setting=camera_basis invalid");
	return ArenaStatus_Ok;
}

ArenaStatus ParseCases(const OrderedJson& document, Catalog* catalog, StatusRecord* error)
{
	if (ValidateSchema(document, 5, "config/cases.json", error) != ArenaStatus_Ok ||
	    ValidateKeys(document, {"schema_version", "default_public_case", "cases"}, "config/cases.json", error) !=
	        ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	std::string_view defaultCase;
	if (RequiredString(document, "default_public_case", "config/cases.json", &defaultCase, error) != ArenaStatus_Ok ||
	    StoreText(catalog, &catalog->defaultPublicCase, defaultCase) != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	OrderedJson::const_iterator cases = document.find("cases");
	if (cases == document.end() || !cases->is_array() || cases->empty() || cases->size() > kCaseCapacity)
		return SetError(error, "invalid_cases");
	constexpr const char* presetNames[] = {"authored", "sphere", "capsule", "convex_hull"};
	std::array<std::uint32_t, kCaseCapacity> shapeMasks = {};
	std::size_t resolvedCount = 0;
	for (const OrderedJson& value : *cases)
	{
		if (!value.is_object())
			return SetError(error, "invalid_case_entry");
		CaseRecord& record = catalog->cases[catalog->caseCount];
		record.authoredCaseIndex = catalog->caseCount;
		std::string_view id;
		std::string_view text;
		if (RequiredString(value, "id", "case", &id, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		const OrderedJson::const_iterator presets = value.find("shape_presets");
		if (presets == value.end() || !presets->is_array() || presets->empty() ||
		    presets->size() > std::size(presetNames) || (*presets)[0] != "authored")
			return SetErrorParts(error, {"case_shape_presets case=", id});
		std::uint32_t& shapeMask = shapeMasks[catalog->caseCount];
		for (const OrderedJson& preset : *presets)
		{
			if (!preset.is_string())
				return SetErrorParts(error, {"case_shape_presets case=", id});
			std::uint32_t bit = 0;
			for (std::uint32_t index = 0; index < std::size(presetNames); ++index)
				if (preset.get<std::string_view>() == presetNames[index])
					bit = 1u << index;
			if (bit == 0 || (shapeMask & bit) != 0)
				return SetErrorParts(error, {"case_shape_presets case=", id});
			shapeMask |= bit;
		}
		resolvedCount += presets->size();
		if (resolvedCount > kCaseCapacity)
			return SetError(error, "case_shape_capacity");
		[[maybe_unused]] std::uint32_t visualDebugPrimitiveCount = 0;
		if (RequiredUnsigned(value, "visual_debug_primitive_count", id, 0, &visualDebugPrimitiveCount, error) !=
		    ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		const OrderedJson::const_iterator timestepField = value.find("timestep_hz");
		if (ValidateKeys(value,
		                 timestepField == value.end()
		                     ? std::initializer_list<std::string_view>{"id",
							                                           "shape_presets",
							                                           "slug",
							                                           "display_name",
							                                           "description",
							                                           "category",
							                                           "fixture_semantic",
							                                           "fixture_revision",
							                                           "benchmark_mode",
							                                           "work_unit_id",
							                                           "work_unit_label",
							                                           "dynamic_body_count",
							                                           "kinematic_body_count",
							                                           "static_body_count",
							                                           "body_count",
							                                           "shape_count",
							                                           "visual_instance_count",
							                                           "visual_debug_primitive_count",
							                                           "mesh_triangle_count",
							                                           "query_count",
							                                           "constraint_count",
							                                           "warmup_work_unit_count",
							                                           "measured_work_unit_count",
							                                           "fixture",
							                                           "thread_counts",
							                                           "repeat_presets",
							                                           "visual_camera",
							                                           "primary_metric",
							                                           "observations",
							                                           "result_groups"}
							 : std::initializer_list<std::string_view>{"id",
							                                           "shape_presets",
							                                           "slug",
							                                           "display_name",
							                                           "description",
							                                           "category",
							                                           "fixture_semantic",
							                                           "fixture_revision",
							                                           "benchmark_mode",
							                                           "work_unit_id",
							                                           "work_unit_label",
							                                           "dynamic_body_count",
							                                           "kinematic_body_count",
							                                           "static_body_count",
							                                           "body_count",
							                                           "shape_count",
							                                           "visual_instance_count",
							                                           "visual_debug_primitive_count",
							                                           "mesh_triangle_count",
							                                           "query_count",
							                                           "constraint_count",
							                                           "timestep_hz",
							                                           "warmup_work_unit_count",
							                                           "measured_work_unit_count",
							                                           "fixture",
							                                           "thread_counts",
							                                           "repeat_presets",
							                                           "visual_camera",
							                                           "primary_metric",
							                                           "observations",
							                                           "result_groups"},
		                 id, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		std::uint32_t duplicate = 0;
		if (CaseIndex(*catalog, id, &duplicate) == ArenaStatus_Ok)
			return SetErrorParts(error, {"duplicate_case_id=", id});
		if (StoreText(catalog, &record.id, id) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		for (std::pair<const char*, CatalogText*> field :
		     {std::pair<const char*, CatalogText*>{"slug", &record.slug},
		      std::pair<const char*, CatalogText*>{"display_name", &record.displayName},
		      std::pair<const char*, CatalogText*>{"description", &record.description},
		      std::pair<const char*, CatalogText*>{"category", &record.category},
		      std::pair<const char*, CatalogText*>{"benchmark_mode", &record.benchmarkMode},
		      std::pair<const char*, CatalogText*>{"fixture_semantic", &record.fixtureSemantic},
		      std::pair<const char*, CatalogText*>{"work_unit_id", &record.workUnitId},
		      std::pair<const char*, CatalogText*>{"work_unit_label", &record.workUnitLabel}})
		{
			if (RequiredString(value, field.first, id, &text, error) != ArenaStatus_Ok ||
			    StoreText(catalog, field.second, text) != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
		}
		for (std::uint32_t prior = 0; prior < catalog->caseCount; ++prior)
			if (CatalogTextView(catalog, catalog->cases[prior].slug) == CatalogTextView(catalog, record.slug))
				return SetErrorParts(error, {"duplicate_case_slug=", CatalogTextView(catalog, record.slug)});
		for (std::pair<const char*, std::uint32_t*> field :
		     {std::pair<const char*, std::uint32_t*>{"dynamic_body_count", &record.dynamicBodyCount},
		      std::pair<const char*, std::uint32_t*>{"kinematic_body_count", &record.kinematicBodyCount},
		      std::pair<const char*, std::uint32_t*>{"static_body_count", &record.staticBodyCount},
		      std::pair<const char*, std::uint32_t*>{"body_count", &record.bodyCount},
		      std::pair<const char*, std::uint32_t*>{"shape_count", &record.shapeCount},
		      std::pair<const char*, std::uint32_t*>{"visual_instance_count", &record.visualInstanceCount},
		      std::pair<const char*, std::uint32_t*>{"mesh_triangle_count", &record.meshTriangleCount},
		      std::pair<const char*, std::uint32_t*>{"query_count", &record.queryCount},
		      std::pair<const char*, std::uint32_t*>{"constraint_count", &record.constraintCount}})
			if (RequiredUnsigned(value, field.first, id, 0, field.second, error) != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
		if (static_cast<std::uint64_t>(record.dynamicBodyCount) + record.kinematicBodyCount + record.staticBodyCount !=
		        record.bodyCount ||
		    (record.visualInstanceCount == 0 && CatalogTextView(catalog, record.fixtureSemantic) != "ray_tracing") ||
		    record.visualInstanceCount > record.bodyCount ||
		    RequiredUnsigned(value, "fixture_revision", id, 1, &record.fixtureRevision, error) != ArenaStatus_Ok ||
		    RequiredUnsigned(value, "warmup_work_unit_count", id, 0, &record.warmupWorkUnitCount, error) !=
		        ArenaStatus_Ok ||
		    RequiredUnsigned(value, "measured_work_unit_count", id, 1, &record.measuredWorkUnitCount, error) !=
		        ArenaStatus_Ok)
			return SetErrorParts(error, {"case_contract case=", id});
		OrderedJson::const_iterator visualCamera = value.find("visual_camera");
		if (visualCamera == value.end() || ParseCaseVisualCamera(*visualCamera, id, record.dynamicBodyCount,
		                                                         &record.visualCamera, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		OrderedJson::const_iterator timestep = value.find("timestep_hz");
		if (timestep != value.end())
		{
			if (RequiredUnsigned(value, "timestep_hz", id, 1, &record.timestepHz, error) != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
			record.timestepPresence = PresenceStatus_Present;
		}
		record.measuredWorkUnitCountPresence = PresenceStatus_Present;
		OrderedJson::const_iterator primary = value.find("primary_metric");
		if (primary == value.end() ||
		    ValidateKeys(*primary, {"id", "unit", "direction", "label", "note"}, id, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		for (std::pair<const char*, CatalogText*> field :
		     {std::pair<const char*, CatalogText*>{"id", &record.primaryMetricId},
		      std::pair<const char*, CatalogText*>{"unit", &record.primaryMetricUnit},
		      std::pair<const char*, CatalogText*>{"label", &record.primaryMetricLabel},
		      std::pair<const char*, CatalogText*>{"note", &record.primaryMetricNote}})
			if (RequiredString(*primary, field.first, id, &text, error) != ArenaStatus_Ok ||
			    StoreText(catalog, field.second, text) != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
		if (RequiredString(*primary, "direction", id, &text, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		record.primaryMetricDirection =
		    text == "lower_is_better"
		        ? PrimaryMetricDirection_LowerIsBetter
		        : (text == "higher_is_better"
		               ? PrimaryMetricDirection_HigherIsBetter
		               : (text == "not_ranked" ? PrimaryMetricDirection_NotRanked : PrimaryMetricDirection_Unknown));
		if (record.primaryMetricDirection == PrimaryMetricDirection_Unknown)
			return SetErrorParts(error, {"primary_metric_direction case=", id});
		OrderedJson::const_iterator groups = value.find("result_groups");
		if (groups == value.end() || ParseResultGroups(*groups, id, &record, catalog, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		OrderedJson::const_iterator observations = value.find("observations");
		if (observations == value.end() ||
		    ParseObservations(*observations, id, &record, catalog, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		if ((record.observationCount == 0) != (record.resultGroupCount == 0))
			return SetErrorParts(error, {"case_result_group_contract case=", id});
		for (std::uint32_t group = 0; group < record.resultGroupCount; ++group)
		{
			PresenceStatus used = PresenceStatus_Absent;
			for (std::uint32_t observation = 0; observation < record.observationCount; ++observation)
				if (catalog->observations[record.observationOffset + observation].resultGroupOrdinal == group)
					used = PresenceStatus_Present;
			if (used != PresenceStatus_Present)
				return SetErrorParts(
				    error, {"empty_result_group case=", id, " id=",
					        CatalogTextView(catalog, catalog->resultGroups[record.resultGroupOffset + group].id)});
		}
		OrderedJson::const_iterator threads = value.find("thread_counts");
		if (threads == value.end() || !threads->is_array() || threads->empty() ||
		    threads->size() > kThreadCountCapacity)
			return SetErrorParts(error, {"invalid_thread_counts case=", id});
		record.threadCountsOffset = catalog->valueCount;
		for (const OrderedJson& thread : *threads)
		{
			if (!thread.is_number_unsigned() || thread.get<std::uint64_t>() == 0 ||
			    thread.get<std::uint64_t>() > UINT32_MAX || catalog->valueCount >= catalog->values.size())
				return SetErrorParts(error, {"invalid_thread_count case=", id});
			const std::uint32_t count = thread.get<std::uint32_t>();
			if (record.threadCount != 0 && count <= catalog->values[catalog->valueCount - 1])
				return SetErrorParts(error, {"thread_count_order case=", id});
			catalog->values[catalog->valueCount++] = count;
			record.threadCount += 1;
		}
		OrderedJson::const_iterator repeats = value.find("repeat_presets");
		if (repeats == value.end() || ValidateKeys(*repeats, {"qualification", "full"}, id, error) != ArenaStatus_Ok ||
		    RequiredUnsigned(*repeats, "qualification", id, 1, &record.qualificationRepeats, error) != ArenaStatus_Ok ||
		    RequiredUnsigned(*repeats, "full", id, 1, &record.fullRepeats, error) != ArenaStatus_Ok)
			return SetErrorParts(error, {"invalid_repeat_presets case=", id});
		if (ParseCaseExecution(value, id, &record, catalog, error) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		catalog->caseCount += 1;
	}
	constexpr const char* presetLabels[] = {"Authored", "Sphere", "Capsule", "Convex Hull"};
	constexpr const char* slugSuffixes[] = {"", "-sphere", "-capsule", "-convex-hull"};
	const std::uint32_t authoredCount = catalog->caseCount;
	for (std::uint32_t presetIndex = 1; presetIndex < 4; ++presetIndex)
	{
		for (std::uint32_t familyIndex = 0; familyIndex < authoredCount; ++familyIndex)
		{
			if ((shapeMasks[familyIndex] & (1u << presetIndex)) == 0)
				continue;
			const CaseRecord& authored = catalog->cases[familyIndex];
			CaseRecord& variant = catalog->cases[catalog->caseCount];
			variant = authored;
			CaseExecutionSpec spec = {};
			if (DecodeCatalogCaseExecution(catalog, familyIndex, &spec, error) != ArenaStatus_Ok ||
			    ResolveCaseExecutionPreset(&spec, static_cast<CaseShapePreset>(presetIndex), error) != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
			const std::string id = std::string(CatalogTextView(catalog, authored.id)) + "_" + presetNames[presetIndex];
			const std::string slug = std::string(CatalogTextView(catalog, authored.slug)) + slugSuffixes[presetIndex];
			const std::string label =
			    std::string(CatalogTextView(catalog, authored.displayName)) + " - " + presetLabels[presetIndex];
			const std::string description = std::string(presetLabels[presetIndex]) + " variant of " +
			                                std::string(CatalogTextView(catalog, authored.displayName)) +
			                                ". Authored environment, placement and timing are preserved.";
			std::uint32_t duplicate = 0;
			if (CaseIndex(*catalog, id, &duplicate) == ArenaStatus_Ok)
				return SetErrorParts(error, {"duplicate_case_id=", id});
			for (std::uint32_t prior = 0; prior < catalog->caseCount; ++prior)
				if (CatalogTextView(catalog, catalog->cases[prior].slug) == slug)
					return SetErrorParts(error, {"duplicate_case_slug=", slug});
			if (CopyExecutionText(id, spec.caseId, id, error) != ArenaStatus_Ok ||
			    StoreText(catalog, &variant.id, id) != ArenaStatus_Ok ||
			    StoreText(catalog, &variant.slug, slug) != ArenaStatus_Ok ||
			    StoreText(catalog, &variant.displayName, label) != ArenaStatus_Ok ||
			    StoreText(catalog, &variant.description, description) != ArenaStatus_Ok ||
			    StoreText(catalog, &variant.fixtureSemantic, spec.fixtureSemantic) != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
			variant.fixtureRevision = spec.fixtureRevision;
			if (StoreCatalogCaseExecution(catalog, &variant, &spec, error) != ArenaStatus_Ok)
				return ArenaStatus_InvalidResult;
			catalog->caseCount += 1;
		}
	}
	for (std::uint32_t index = 0; index < authoredCount; ++index)
		if (CatalogTextView(catalog, catalog->cases[index].slug) == defaultCase)
			return ArenaStatus_Ok;
	return SetErrorParts(error, {"invalid_default_public_case=", defaultCase});
}

} // namespace physics_arena
