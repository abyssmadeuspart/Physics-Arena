#include "physics_arena/stack_stability.h"
#include "physics_arena/run.h"
#include "physics_arena/csv_io.h"
#include "physics_arena/replay.h"
#include "contact_islands_stability.h"
#include "pyramid_wall.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace physics_arena
{
namespace
{
using benchmark_stack::Pose;
using benchmark_stack::Phase;
constexpr std::array<std::string_view, 34> kColumns = {
	"run_id", "engine_id", "thread_count", "repeat_index", "assessment", "coverage", "reason", "margin_m",
	"first_segment", "first_phase", "first_step", "last_segment", "last_phase", "last_step",
	"impact_present", "impact_segment", "impact_phase", "impact_step", "sample_seconds",
	"first_rule", "first_body", "breach_segment", "breach_phase", "breach_step",
	"metric", "value_m", "body", "segment", "phase", "step", "capture_elapsed_ms", "analysis_elapsed_ms",
	"criterion", "margin_policy"};

ArenaStatus StabilityError(StatusRecord* error, std::string_view detail, ArenaStatus code = ArenaStatus_InvalidResult)
{
	*error = {};
	error->code = code;
	const std::string_view component = "stack_stability", status = ArenaStatusText(code);
	std::copy(component.begin(), component.end(), error->component.begin());
	error->componentSize = static_cast<std::uint32_t>(component.size());
	std::copy(status.begin(), status.end(), error->status.begin());
	error->statusSize = static_cast<std::uint32_t>(status.size());
	const std::size_t count = std::min(detail.size(), error->detail.size());
	std::copy_n(detail.begin(), count, error->detail.begin());
	error->detailSize = static_cast<std::uint32_t>(count);
	return error->code;
}

std::uint32_t DecodeU32(const std::uint8_t* bytes)
{
	std::uint32_t value = 0;
	for (std::uint32_t index = 0; index < 4; ++index)
		value |= static_cast<std::uint32_t>(bytes[index]) << (8 * index);
	return value;
}

float DecodeFloat(const std::uint8_t* bytes)
{
	const std::uint32_t bits = DecodeU32(bytes);
	float value = 0;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

double DecodeDouble(const std::uint8_t* bytes)
{
	std::uint64_t bits = 0;
	for (std::uint32_t index = 0; index < 8; ++index)
		bits |= static_cast<std::uint64_t>(bytes[index]) << (8 * index);
	double value = 0;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

int BuildContainerBounds(const CaseExecutionSpec& spec, ContainerBounds* bounds)
{
	const CaseExecutionOpenContainer& fixture = spec.openContainer;
	const CaseExecutionVector3 half = spec.selectedGeometry.halfExtents;
	if (spec.fixtureKind != CaseFixtureKind_OpenContainerFallingPile || spec.shapePreset != CaseShapePreset_Authored ||
	    spec.selectedGeometry.shape != CaseExecutionShape_Box || half.x <= 0 || half.x != half.y || half.x != half.z ||
	    fixture.staticBoxCount != 5)
		return 0;
	const CaseExecutionBox& floor = fixture.staticBoxes[0];
	bounds->floor = static_cast<double>(floor.center.y) + floor.halfExtents.y;
	bounds->rim = static_cast<double>(fixture.staticBoxes[1].center.y) + fixture.staticBoxes[1].halfExtents.y;
	for (std::uint32_t axis = 0; axis < 2; ++axis)
	{
		const CaseExecutionBox& negative = fixture.staticBoxes[1 + 2 * axis];
		const CaseExecutionBox& positive = fixture.staticBoxes[2 + 2 * axis];
		const double negativeCenter = axis == 0 ? negative.center.x : negative.center.z;
		const double positiveCenter = axis == 0 ? positive.center.x : positive.center.z;
		const double negativeHalf = axis == 0 ? negative.halfExtents.x : negative.halfExtents.z;
		const double positiveHalf = axis == 0 ? positive.halfExtents.x : positive.halfExtents.z;
		const double floorCenter = axis == 0 ? floor.center.x : floor.center.z;
		const double floorHalf = axis == 0 ? floor.halfExtents.x : floor.halfExtents.z;
		bounds->lower[axis] = negativeCenter + negativeHalf;
		bounds->upper[axis] = positiveCenter - positiveHalf;
		if (negativeHalf <= 0 || positiveHalf <= 0 || bounds->lower[axis] >= bounds->upper[axis] ||
		    bounds->lower[axis] < floorCenter - floorHalf || bounds->upper[axis] > floorCenter + floorHalf)
			return 0;
	}
	for (std::uint32_t index = 1; index < 5; ++index)
	{
		const CaseExecutionBox& wall = fixture.staticBoxes[index];
		const std::uint32_t axis = index <= 2 ? 1 : 0;
		const double center = axis == 0 ? wall.center.x : wall.center.z;
		const double extent = axis == 0 ? wall.halfExtents.x : wall.halfExtents.z;
		if (static_cast<double>(wall.center.y) + wall.halfExtents.y != bounds->rim ||
		    static_cast<double>(wall.center.y) - wall.halfExtents.y > bounds->floor ||
		    center - extent > bounds->lower[axis] || center + extent < bounds->upper[axis])
			return 0;
	}
	return bounds->floor < bounds->rim ? 1 : 0;
}

std::array<double, 3> Vector(CaseExecutionVector3 value)
{
	return {value.x, value.y, value.z};
}

int BuildEnvelopes(const CaseExecutionSpec& spec, std::uint32_t boxes, std::array<double, 3>* half,
	               std::vector<RestEnvelope>* references)
{
	if (benchmark_stack::TargetFixture(spec.fixtureKind) == 0 ||
	    (spec.fixtureKind != CaseFixtureKind_PyramidWall && spec.selectedGeometry.shape != CaseExecutionShape_Box) ||
	    spec.gravity.x != 0 || spec.gravity.z != 0 || spec.gravity.y >= 0 || spec.restitution != 0)
		return 0;
	*half = spec.fixtureKind == CaseFixtureKind_PyramidWall
	    ? std::array<double, 3>{spec.pyramidWall.halfExtent, spec.pyramidWall.halfExtent, spec.pyramidWall.halfExtent}
	    : Vector(spec.selectedGeometry.halfExtents);
	if ((*half)[0] <= 0 || (*half)[1] <= 0 || (*half)[2] <= 0)
		return 0;
	references->reserve(boxes);
	if (spec.fixtureKind == CaseFixtureKind_OpenContainerFallingPile)
	{
		ContainerBounds bounds = {};
		const CaseExecutionOpenContainer& fixture = spec.openContainer;
		if (BuildContainerBounds(spec, &bounds) == 0 ||
		    static_cast<std::uint64_t>(fixture.dynamicGrid[0]) * fixture.dynamicGrid[1] * fixture.dynamicGrid[2] != boxes)
			return 0;
		for (std::uint32_t y = 0; y < fixture.dynamicGrid[1]; ++y)
			for (std::uint32_t z = 0; z < fixture.dynamicGrid[2]; ++z)
				for (std::uint32_t x = 0; x < fixture.dynamicGrid[0]; ++x)
				{
					const std::array<double, 3> spawn = {(x - 0.5 * (fixture.dynamicGrid[0] - 1)) * fixture.dynamicSpacing.x,
					    fixture.dynamicInitialY + y * fixture.dynamicSpacing.y,
					    (z - 0.5 * (fixture.dynamicGrid[2] - 1)) * fixture.dynamicSpacing.z};
					references->push_back({spawn, spawn, {}, 0});
				}
	}
	else if (spec.fixtureKind == CaseFixtureKind_BoxContactIslands)
		return BuildContactIslandsEnvelopes(spec, boxes, *half, references);
	else if (spec.fixtureKind == CaseFixtureKind_LargePyramid)
	{
		const CaseExecutionLargePyramid& fixture = spec.largePyramid;
		const std::array<double, 3> spacing = Vector(fixture.boxSpacing);
		if (fixture.baseCenter.y != (*half)[1])
			return 0;
		for (std::uint32_t axis = 0; axis < 3; ++axis)
			if (spacing[axis] != 2 * (*half)[axis])
				return 0;
		std::uint32_t previousStart = 0, layerStart = 0;
		for (std::uint32_t layer = 0; layer < fixture.rowCount; ++layer)
		{
			const std::uint32_t width = fixture.rowCount - layer;
			for (std::uint32_t depth = 0; depth < width; ++depth)
				for (std::uint32_t column = 0; column < width; ++column)
				{
					const std::array<double, 3> position = {fixture.baseCenter.x + (column - 0.5 * (width - 1)) * spacing[0],
					    fixture.baseCenter.y + layer * spacing[1], fixture.baseCenter.z + (depth - 0.5 * (width - 1)) * spacing[2]};
					if (std::abs(position[0]) + (*half)[0] > fixture.floorHalfExtents.x ||
					    std::abs(position[2]) + (*half)[2] > fixture.floorHalfExtents.z)
						return 0;
					RestEnvelope reference = {position, position, {}, 0};
					if (layer != 0)
					{
						const std::uint32_t below = previousStart + depth * (width + 1) + column;
						reference.supports = {below, below + 1, below + width + 1, below + width + 2};
						reference.supportCount = 4;
					}
					references->push_back(reference);
					if (references->size() > boxes)
						return 0;
				}
			previousStart = layerStart;
			layerStart += width * width;
		}
	}
	else
	{
		const std::uint64_t rows = spec.pyramidWall.rowCount;
		if (rows * (rows + 1) / 2 != boxes)
			return 0;
		std::uint32_t rowStart = 0, previousStart = 0, width = spec.pyramidWall.rowCount;
		for (std::uint32_t index = 0; index < boxes; ++index)
		{
			if (index == rowStart + width)
			{
				previousStart = rowStart;
				rowStart = index;
				--width;
			}
			const std::array<double, 3> position = Vector(PyramidWallPosition(spec.pyramidWall, index));
			if (std::abs(position[0]) + (*half)[0] > spec.pyramidWall.floorHalfExtents.x ||
			    std::abs(position[2]) + (*half)[2] > spec.pyramidWall.floorHalfExtents.z)
				return 0;
			RestEnvelope reference = {position, position, {}, 0};
			if (rowStart != 0)
			{
				reference.supports = {previousStart + index - rowStart, previousStart + index - rowStart + 1, 0, 0};
				reference.supportCount = 2;
			}
			references->push_back(reference);
		}
	}
	return references->size() == boxes ? 1 : 0;
}

int FinitePose(const Pose& pose)
{
	for (float value : {pose.position.x, pose.position.y, pose.position.z, pose.orientation.x,
	                   pose.orientation.y, pose.orientation.z, pose.orientation.w})
		if (std::isfinite(value) == 0)
			return 0;
	const CaseExecutionQuaternion& q = pose.orientation;
	return static_cast<double>(q.x) * q.x + static_cast<double>(q.y) * q.y +
	       static_cast<double>(q.z) * q.z + static_cast<double>(q.w) * q.w > 0 ? 1 : 0;
}

int PossibleImpact(std::span<const Pose> previous, std::span<const Pose> current,
	               std::uint32_t boxes, double radius)
{
	for (std::size_t projectile = boxes; projectile < current.size(); ++projectile)
		for (std::uint32_t box = 0; box < boxes; ++box)
		{
			const std::array<double, 3> oldProjectile = Vector(previous[projectile].position);
			const std::array<double, 3> newProjectile = Vector(current[projectile].position);
			const std::array<double, 3> oldBox = Vector(previous[box].position);
			const std::array<double, 3> newBox = Vector(current[box].position);
			std::array<double, 3> start = {}, delta = {};
			int rejected = 0;
			for (std::uint32_t axis = 0; axis < 3; ++axis)
			{
				start[axis] = oldProjectile[axis] - oldBox[axis];
				const double end = newProjectile[axis] - newBox[axis];
				delta[axis] = end - start[axis];
				if (std::min(start[axis], end) > radius || std::max(start[axis], end) < -radius)
					rejected = 1;
			}
			if (rejected != 0)
				continue;
			const double length = delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2];
			const double dot = start[0] * delta[0] + start[1] * delta[1] + start[2] * delta[2];
			const double fraction = length > 0 ? std::clamp(-dot / length, 0.0, 1.0) : 0;
			double distance = 0;
			for (std::uint32_t axis = 0; axis < 3; ++axis)
			{
				const double value = start[axis] + fraction * delta[axis];
				distance += value * value;
			}
			if (distance <= radius * radius)
				return 1;
		}
	return 0;
}

void Breach(StackStabilityResult* result, StackRule rule, std::uint32_t body, StackSample sample)
{
	if (result->firstRule == StackRule_None)
	{
		result->firstRule = rule;
		result->firstBody = body;
		result->firstBreach = sample;
	}
	result->assessment = StackAssessment_Fail;
}

void AssessFrame(std::span<const Pose> poses, std::span<const RestEnvelope> references,
	             const std::array<double, 3>& half, std::vector<double>* minima,
	             const ContainerBounds& container, std::vector<PresenceStatus>* entered,
	             StackSample sample, StackStabilityResult* result,
                 const CaseExecutionContactIslands* islands = nullptr,
                 const CaseExecutionSpec* pyramid = nullptr)
{
	std::uint32_t rowStart = 0;
	std::uint32_t width = pyramid == nullptr ? 0 : pyramid->fixtureKind == CaseFixtureKind_LargePyramid ?
	    pyramid->largePyramid.rowCount : pyramid->pyramidWall.rowCount;
	for (std::uint32_t index = 0; index < references.size(); ++index)
	{
		if (pyramid != nullptr)
		{
			const int large = pyramid->fixtureKind == CaseFixtureKind_LargePyramid;
			std::uint32_t rowSize = large != 0 ? width * width : width;
			if (index == rowStart + rowSize)
			{
				rowStart = index;
				--width;
				rowSize = large != 0 ? width * width : width;
			}
		}
		const Pose& pose = poses[index];
		if (FinitePose(pose) == 0)
		{
			Breach(result, StackRule_InvalidState, index, sample);
			continue;
		}
		const double upward = static_cast<double>(pose.position.y) - (*minima)[index];
		(*minima)[index] = std::min((*minima)[index], static_cast<double>(pose.position.y));
		if (upward > result->upwardExcursion.value)
			result->upwardExcursion = {upward, index, sample};
		const CaseExecutionQuaternion& q = pose.orientation;
		const double x = q.x, y = q.y, z = q.z, w = q.w;
		const double norm = x * x + y * y + z * z + w * w;
		const double scale = 2 / norm;
		const double matrix[3][3] = {
		    {1 - scale * (y * y + z * z), scale * (x * y - z * w), scale * (x * z + y * w)},
		    {scale * (x * y + z * w), 1 - scale * (x * x + z * z), scale * (y * z - x * w)},
		    {scale * (x * z - y * w), scale * (y * z + x * w), 1 - scale * (x * x + y * y)}};
		const std::array<double, 3> position = Vector(pose.position);
		double violation = 0;
		for (std::uint32_t corner = 0; corner < 8; ++corner)
		{
			const std::array<double, 3> offset = {corner & 1 ? half[0] : -half[0],
			    corner & 2 ? half[1] : -half[1], corner & 4 ? half[2] : -half[2]};
			for (std::uint32_t axis = 0; axis < 3; ++axis)
			{
				const double coordinate = position[axis] + matrix[axis][0] * offset[0] +
				    matrix[axis][1] * offset[1] + matrix[axis][2] * offset[2];
				violation = std::max({violation, references[index].lower[axis] + offset[axis] - coordinate,
				    coordinate - references[index].upper[axis] - offset[axis]});
			}
		}
		if (violation > result->restViolation.value)
			result->restViolation = {violation, index, sample};
		if (result->criterion == StackCriterion_ContainerEscape)
		{
			std::array<double, 3> radius = {};
			for (std::uint32_t axis = 0; axis < 3; ++axis)
				for (std::uint32_t component = 0; component < 3; ++component)
					radius[axis] += std::abs(matrix[axis][component]) * half[component];
			if (position[0] - radius[0] >= container.lower[0] && position[0] + radius[0] <= container.upper[0] &&
			    position[2] - radius[2] >= container.lower[1] && position[2] + radius[2] <= container.upper[1] &&
			    position[1] + radius[1] <= container.rim)
				(*entered)[index] = PresenceStatus_Present;
			if ((*entered)[index] == PresenceStatus_Present && position[1] - radius[1] > container.rim)
				Breach(result, StackRule_ContainerEscape, index, sample);
			continue;
		}
		const RestEnvelope& reference = references[index];
		double supportLevel = reference.supportCount == 0 ? 0 : std::numeric_limits<double>::infinity();
		for (std::uint32_t slot = 0; slot < reference.supportCount; ++slot)
			supportLevel = std::min(supportLevel, static_cast<double>(poses[reference.supports[slot]].position.y) + half[1]);
		if (pose.position.y < supportLevel)
			Breach(result, StackRule_SupportPlane, index, sample);
		if (pyramid != nullptr)
		{
			const int large = pyramid->fixtureKind == CaseFixtureKind_LargePyramid;
			const std::uint32_t local = index - rowStart;
			const std::array<double, 3> radius = StackProjectedRadii(pose, half);
			double slotExcess = 0, compression = 0;
			for (std::uint32_t axis = 0; axis < 3; ++axis)
				slotExcess = std::max({slotExcess, reference.lower[axis] - half[axis] - position[axis] + radius[axis],
				    position[axis] + radius[axis] - reference.upper[axis] - half[axis]});
			const std::uint32_t neighbourSlots[] = {index + 1, index + width};
			const std::uint32_t neighbourAxes[] = {0, 2};
			const std::uint32_t neighbourCount = large != 0 ? 2 : 1;
			for (std::uint32_t pair = 0; pair < neighbourCount; ++pair)
			{
				if ((pair == 0 && local % width + 1 == width) || (pair == 1 && local / width + 1 == width))
					continue;
				const Pose& neighbour = poses[neighbourSlots[pair]];
				if (FinitePose(neighbour) == 0) continue;
				const std::uint32_t axis = neighbourAxes[pair];
				const std::array<double, 3> neighbourPosition = Vector(neighbour.position);
				compression = std::max(compression, radius[axis] + StackProjectedRadii(neighbour, half)[axis] -
				    (neighbourPosition[axis] - position[axis]));
			}
			for (std::uint32_t slot = 0; slot < reference.supportCount; ++slot)
			{
				const Pose& support = poses[reference.supports[slot]];
				if (FinitePose(support) != 0)
					compression = std::max(compression, radius[1] + StackProjectedRadii(support, half)[1] -
					    (position[1] - support.position.y));
			}
			const double excess = std::max(slotExcess, compression);
			if (excess > result->unforcedStackShapeExcess.value)
				result->unforcedStackShapeExcess = {excess, index, sample};
			if (slotExcess > result->margin)
				Breach(result, StackRule_UnforcedSlotEnvelope, index, sample);
			else if (compression > result->margin)
				Breach(result, StackRule_UnforcedAdjacentCompression, index, sample);
		}
		if (islands != nullptr)
		{
			const StackRule rule = AssessContactIslandsBody(*islands, poses, references, half, index, sample, result);
			if (rule != StackRule_None) Breach(result, rule, index, sample);
		}
	}
}

template <typename T>
int Number(std::string_view text, T* value)
{
	const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), *value);
	return !text.empty() && parsed.ec == std::errc() && parsed.ptr == text.data() + text.size() ? 1 : 0;
}

std::string Numeric(double value)
{
	std::ostringstream stream;
	stream << std::setprecision(17) << value;
	return stream.str();
}

int SameSample(const StackSample& left, const StackSample& right)
{
	return left.segment == right.segment && left.phase == right.phase && left.step == right.step;
}

struct ReadContext
{
	std::vector<StackStabilityResult>* results;
	std::vector<std::uint32_t> masks;
};

ArenaStatus ReadRow(const CsvHeader* header, const CsvRow* input, void* opaque, StatusRecord* error)
{
	if (header->fieldCount != kColumns.size() || input->fieldCount != kColumns.size())
		return StabilityError(error, "stability_columns");
	std::array<std::string_view, kColumns.size()> fields = {};
	for (std::size_t index = 0; index < fields.size(); ++index)
	{
		if (CsvHeaderTextView(header, header->fields[index]) != kColumns[index])
			return StabilityError(error, "stability_column_name");
		fields[index] = CsvRowTextView(input, input->fields[index]);
	}
	StackStabilityResult result = {};
	result.runId = fields[0];
	result.engineId = fields[1];
	result.reason = fields[6];
	std::uint32_t assessment = 0, coverage = 0, impact = 0, rule = 0;
	if (fields[0].empty() || fields[1].empty() || ((fields[32] != kStackCriterion || fields[33] != kStackMarginPolicy) &&
	     (fields[32] != kLegacyStackCriterion || fields[33] != kLegacyStackMarginPolicy) &&
	     (fields[32] != kContainerCriterion || fields[33] != kContainerMarginPolicy) &&
	     (fields[32] != kContactIslandsCriterion || (fields[33] != kContactIslandsMarginPolicy && fields[33] != kContactIslands10cmMarginPolicy)) &&
	     (fields[32] != kContactIslandsShapeCriterion || fields[33] != kContactIslandsShapeMarginPolicy) &&
	     (fields[32] != kPyramidUnforcedShapeCriterion || fields[33] != kPyramidUnforcedShapeMarginPolicy)) ||
	    Number(fields[2], &result.threadCount) == 0 || Number(fields[3], &result.repeatIndex) == 0 ||
	    Number(fields[4], &assessment) == 0 || assessment > StackAssessment_Fail ||
	    Number(fields[5], &coverage) == 0 || coverage > StackCoverage_Complete ||
	    Number(fields[7], &result.margin) == 0 || result.threadCount == 0 ||
	    Number(fields[14], &impact) == 0 || impact > PresenceStatus_Present ||
	    Number(fields[18], &result.sampleSeconds) == 0 || Number(fields[19], &rule) == 0 || rule > StackRule_UnforcedAdjacentCompression ||
	    Number(fields[20], &result.firstBody) == 0 || Number(fields[30], &result.captureElapsedMs) == 0 ||
	    Number(fields[31], &result.analysisElapsedMs) == 0)
		return StabilityError(error, "stability_tuple");
	result.criterion = fields[32] == kStackCriterion ? StackCriterion_SupportPlane :
	    fields[32] == kContainerCriterion ? StackCriterion_ContainerEscape :
	    fields[32] == kPyramidUnforcedShapeCriterion ? StackCriterion_PyramidUnforcedShape :
	    fields[32] == kContactIslandsShapeCriterion ? StackCriterion_ContactIslandsShapePreservation :
	    fields[32] == kContactIslandsCriterion ? (fields[33] == kContactIslands10cmMarginPolicy ? StackCriterion_ContactIslandsStabilization10cm : StackCriterion_ContactIslandsStabilization) : StackCriterion_LegacyMovement;
	result.assessment = static_cast<StackAssessment>(assessment);
	result.coverage = static_cast<StackCoverage>(coverage);
	result.impactPresence = static_cast<PresenceStatus>(impact);
	result.firstRule = static_cast<StackRule>(rule);
	const int islands = result.criterion == StackCriterion_ContactIslandsStabilization || result.criterion == StackCriterion_ContactIslandsStabilization10cm ||
	    result.criterion == StackCriterion_ContactIslandsShapePreservation;
	const std::uint32_t mask = fields[24] == "rest_envelope" ? 1 : fields[24] == "upward_excursion" ? 2 :
	    fields[24] == "settling_step_displacement" ? 4 : fields[24] == "terminal_stack_shape_excess" ? 8 : fields[24] == "unforced_stack_shape_excess" ? 16 : 0;
	const int pyramid = result.criterion == StackCriterion_PyramidUnforcedShape;
	if (mask == 0 || (mask == 16 && pyramid == 0) || (mask > 2 && mask != 16 && islands == 0) || (mask == 4 && result.criterion == StackCriterion_ContactIslandsShapePreservation))
		return StabilityError(error, "stability_metric_name");
	StackMetric metric = {};
	const PresenceStatus metricPresence = mask > 2 && fields[25].empty() ? PresenceStatus_Absent : PresenceStatus_Present;
	if (metricPresence == PresenceStatus_Present)
	{
		if (Number(fields[25], &metric.value) == 0 || Number(fields[26], &metric.body) == 0)
			return StabilityError(error, "stability_metric");
	}
	else
		for (std::uint32_t index = 26; index <= 29; ++index)
			if (!fields[index].empty()) return StabilityError(error, "stability_metric_absence");
	StackSample* samples[] = {&result.firstSample, &result.lastSample, &result.impactIntervalEnd, &result.firstBreach, &metric.sample};
	const std::uint32_t offsets[] = {8, 11, 15, 21, 27};
	for (std::uint32_t index = 0; index < std::size(samples); ++index)
	{
		if (index == 4 && metricPresence == PresenceStatus_Absent) continue;
		std::uint32_t phase = 0;
		if (Number(fields[offsets[index]], &samples[index]->segment) == 0 ||
		    Number(fields[offsets[index] + 1], &phase) == 0 || phase > benchmark_stack::Phase_Measured ||
		    Number(fields[offsets[index] + 2], &samples[index]->step) == 0)
			return StabilityError(error, "stability_sample");
		samples[index]->phase = static_cast<Phase>(phase);
	}
	for (double value : {result.margin, result.sampleSeconds, result.captureElapsedMs, result.analysisElapsedMs, metric.value})
		if (std::isfinite(value) == 0 || value < 0)
			return StabilityError(error, "stability_numeric");
	if ((result.criterion != StackCriterion_LegacyMovement &&
	     ((islands == 0 && pyramid == 0 && result.margin != 0) ||
	      result.firstRule == StackRule_RestEnvelope || result.firstRule == StackRule_UpwardExcursion)) ||
	    (islands != 0 && (result.firstRule == StackRule_ContainerEscape || result.sampleSeconds <= 0)) ||
	    (result.criterion == StackCriterion_ContactIslandsShapePreservation && result.firstRule == StackRule_TerminalMotion) ||
	    (islands == 0 && result.firstRule >= StackRule_TerminalSlotEnvelope &&
	     !(pyramid != 0 && result.firstRule >= StackRule_UnforcedSlotEnvelope)) ||
	    (islands != 0 && result.firstRule >= StackRule_UnforcedSlotEnvelope) ||
	    (pyramid != 0 && (result.margin != 0.10 || result.sampleSeconds <= 0 || result.firstRule == StackRule_ContainerEscape)) ||
	    (result.criterion == StackCriterion_SupportPlane && result.firstRule == StackRule_ContainerEscape) ||
	    (result.criterion == StackCriterion_ContainerEscape && result.firstRule == StackRule_SupportPlane) ||
	    (result.criterion == StackCriterion_LegacyMovement && result.firstRule >= StackRule_SupportPlane) ||
	    (result.assessment == StackAssessment_Pass && result.coverage != StackCoverage_Complete) ||
	    (result.assessment == StackAssessment_Fail && result.firstRule == StackRule_None) ||
	    (result.assessment != StackAssessment_Fail && result.firstRule != StackRule_None))
		return StabilityError(error, "stability_assessment");
	ReadContext* context = static_cast<ReadContext*>(opaque);
	std::size_t ordinal = 0;
	while (ordinal < context->results->size())
	{
		const StackStabilityResult& existing = (*context->results)[ordinal];
		if (existing.runId == result.runId && existing.engineId == result.engineId &&
		    existing.threadCount == result.threadCount && existing.repeatIndex == result.repeatIndex)
			break;
		++ordinal;
	}
	if (ordinal == context->results->size())
	{
		if (context->results->size() >= kEngineCapacity * kThreadCountCapacity * kRunRepeatCapacity)
			return StabilityError(error, "stability_tuple_capacity");
		context->results->push_back(std::move(result));
		context->masks.push_back(0);
	}
	else
	{
		const StackStabilityResult& existing = (*context->results)[ordinal];
		if (existing.criterion != result.criterion || existing.assessment != result.assessment || existing.coverage != result.coverage ||
		    existing.margin != result.margin || existing.reason != result.reason || existing.firstRule != result.firstRule ||
		    existing.firstBody != result.firstBody || existing.captureElapsedMs != result.captureElapsedMs ||
		    existing.analysisElapsedMs != result.analysisElapsedMs || existing.sampleSeconds != result.sampleSeconds ||
		    existing.impactPresence != result.impactPresence || SameSample(existing.firstSample, result.firstSample) == 0 ||
		    SameSample(existing.lastSample, result.lastSample) == 0 || SameSample(existing.firstBreach, result.firstBreach) == 0 ||
		    SameSample(existing.impactIntervalEnd, result.impactIntervalEnd) == 0)
			return StabilityError(error, "stability_metric_tuple_disagreement");
	}
	if ((context->masks[ordinal] & mask) != 0)
		return StabilityError(error, "stability_duplicate_metric");
	context->masks[ordinal] |= mask;
	if (mask > 2 && mask != 16)
	{
		StackStabilityResult& existing = (*context->results)[ordinal];
		if ((context->masks[ordinal] & (12 ^ mask)) != 0 && existing.terminalMetricsPresence != metricPresence)
			return StabilityError(error, "stability_terminal_availability_disagreement");
		existing.terminalMetricsPresence = metricPresence;
	}
	if (mask == 1)
		(*context->results)[ordinal].restViolation = metric;
	else if (mask == 2)
		(*context->results)[ordinal].upwardExcursion = metric;
	else if (mask == 4)
		(*context->results)[ordinal].settlingStepDisplacement = metric;
	else if (mask == 8)
		(*context->results)[ordinal].terminalStackShapeExcess = metric;
	else
	{
		(*context->results)[ordinal].unforcedStackShapeExcess = metric;
		(*context->results)[ordinal].unforcedShapePresence = metricPresence;
	}
	return ArenaStatus_Ok;
}

ArenaStatus WriteResult(CsvWriter* writer, const StackStabilityResult& result, StatusRecord* error)
{
	const StackMetric metrics[] = {result.restViolation, result.upwardExcursion, result.settlingStepDisplacement, result.terminalStackShapeExcess, result.unforcedStackShapeExcess};
	constexpr const char* names[] = {"rest_envelope", "upward_excursion", "settling_step_displacement", "terminal_stack_shape_excess", "unforced_stack_shape_excess"};
	const std::uint32_t count = result.criterion == StackCriterion_ContactIslandsShapePreservation || result.criterion == StackCriterion_PyramidUnforcedShape ? 3 :
	    result.criterion == StackCriterion_ContactIslandsStabilization || result.criterion == StackCriterion_ContactIslandsStabilization10cm ? 4 : 2;
	constexpr std::uint32_t shapeOrdinals[] = {0, 1, 3};
	for (std::uint32_t index = 0; index < count; ++index)
	{
		const std::uint32_t ordinal = result.criterion == StackCriterion_ContactIslandsShapePreservation ? shapeOrdinals[index] : result.criterion == StackCriterion_PyramidUnforcedShape && index == 2 ? 4 : index;
		const StackMetric& metric = metrics[ordinal];
		const PresenceStatus presence = ordinal < 2 ? PresenceStatus_Present : ordinal == 4 ? result.unforcedShapePresence : result.terminalMetricsPresence;
		const std::string fields[] = {result.runId, result.engineId, std::to_string(result.threadCount),
		    std::to_string(result.repeatIndex), std::to_string(result.assessment), std::to_string(result.coverage),
		    result.reason, Numeric(result.margin), std::to_string(result.firstSample.segment), std::to_string(result.firstSample.phase),
		    std::to_string(result.firstSample.step), std::to_string(result.lastSample.segment), std::to_string(result.lastSample.phase),
		    std::to_string(result.lastSample.step), std::to_string(result.impactPresence), std::to_string(result.impactIntervalEnd.segment),
		    std::to_string(result.impactIntervalEnd.phase), std::to_string(result.impactIntervalEnd.step), Numeric(result.sampleSeconds),
		    std::to_string(result.firstRule), std::to_string(result.firstBody), std::to_string(result.firstBreach.segment),
		    std::to_string(result.firstBreach.phase), std::to_string(result.firstBreach.step),
		    names[ordinal], presence == PresenceStatus_Present ? Numeric(metric.value) : "", presence == PresenceStatus_Present ? std::to_string(metric.body) : "",
		    presence == PresenceStatus_Present ? std::to_string(metric.sample.segment) : "", presence == PresenceStatus_Present ? std::to_string(metric.sample.phase) : "", presence == PresenceStatus_Present ? std::to_string(metric.sample.step) : "",
		    Numeric(result.captureElapsedMs), Numeric(result.analysisElapsedMs),
		    StackCriterionName(result.criterion), StackMarginPolicyName(result.criterion)};
		for (std::size_t index = 0; index < std::size(fields); ++index)
			if (WriteCsvField(writer, fields[index], index + 1 == std::size(fields) ? CsvFieldTerminator_EndRow : CsvFieldTerminator_MoreFields, error) != ArenaStatus_Ok)
				return error->code;
	}
	return ArenaStatus_Ok;
}
}

void InitializeStackAssessment(const CaseExecutionSpec& execution, std::string_view runId,
                               std::string_view engineId, std::uint32_t threads, std::uint32_t repeat,
                               StackAssessmentState* state)
{
	*state = {};
	state->execution = execution;
	StackStabilityResult* result = &state->result;
	*result = {};
	result->runId = runId;
	result->engineId = engineId;
	result->threadCount = threads;
	result->repeatIndex = repeat;
	result->criterion = CurrentStackCriterion(execution.fixtureKind);
	result->sampleSeconds = execution.timestepHz != 0 ? 1.0 / execution.timestepHz : 0;
	if (result->criterion == StackCriterion_ContactIslandsShapePreservation)
		result->margin = 0.20 * std::min({execution.selectedGeometry.halfExtents.x, execution.selectedGeometry.halfExtents.y, execution.selectedGeometry.halfExtents.z});
	if (result->criterion == StackCriterion_PyramidUnforcedShape) result->margin = 0.10;
	result->reason = "capture_missing";
}

int AcceptStackHeader(std::span<const std::uint8_t> header, StackAssessmentState* state)
{
	const CaseExecutionSpec& execution = state->execution;
	StackStabilityResult* result = &state->result;
	const std::uint32_t threads = result->threadCount, repeat = result->repeatIndex;
	const std::string_view engineId = result->engineId;
	if (header.size() != benchmark_stack::kHeaderBytes || state->streamState != StackStreamState_Header) return 2;
	state->boxes = execution.dynamicBodyCount -
	    (execution.fixtureKind == CaseFixtureKind_LargePyramid ? execution.largePyramid.projectileCount : 0);
	const std::uint32_t expected[] = {static_cast<std::uint32_t>(execution.fixtureKind), execution.fixtureRevision,
	    execution.dynamicBodyCount, state->boxes, threads, repeat, execution.warmupWorkUnitCount, execution.measuredWorkUnitCount, execution.timestepHz};
	const std::string_view identities[] = {execution.caseId, execution.fixtureSemantic, engineId};
	int matching = std::memcmp(header.data(), benchmark_stack::kMagic, sizeof(benchmark_stack::kMagic)) == 0 ? 1 : 0;
	for (std::uint32_t index = 0; index < std::size(identities); ++index)
	{
		const char* text = reinterpret_cast<const char*>(header.data() + 8 + 64 * index);
		const char* end = static_cast<const char*>(std::memchr(text, 0, 64));
		if (end == nullptr || std::string_view(text, static_cast<std::size_t>(end - text)) != identities[index])
			matching = 0;
	}
	for (std::uint32_t index = 0; index < std::size(expected); ++index)
		if (DecodeU32(header.data() + 200 + 4 * index) != expected[index])
			matching = 0;
	if (matching == 0)
	{
		result->reason = "capture_identity";
		state->streamState = StackStreamState_Rejected;
		return 2;
	}
	state->half = {};
	
	state->supported = BuildEnvelopes(execution, state->boxes, &state->half, &state->references);
	state->container = {};
	if (result->criterion == StackCriterion_ContainerEscape && state->supported != 0)
		BuildContainerBounds(execution, &state->container);
	state->impactAllowance = 0.1 * std::min({state->half[0], state->half[1], state->half[2]});
	result->sampleSeconds = execution.timestepHz != 0 ? 1.0 / execution.timestepHz : 0;
	state->current.resize(execution.dynamicBodyCount);
	state->previous.resize(state->supported != 0 && execution.fixtureKind == CaseFixtureKind_LargePyramid ? execution.dynamicBodyCount : 0);
	state->minima.assign(state->boxes, std::numeric_limits<double>::infinity());
	state->entered.resize(result->criterion == StackCriterion_ContainerEscape ? state->boxes : 0);
	state->terminalStart = execution.measuredWorkUnitCount > execution.timestepHz ? execution.measuredWorkUnitCount - execution.timestepHz : 0;
	state->unforced = 1;
	result->reason = "capture_incomplete";
	state->streamState = StackStreamState_Frames;
	return 0;
}

int AssessStackFrame(std::span<const std::uint8_t> frame, StackAssessmentState* state)
{
	const CaseExecutionSpec& execution = state->execution;
	StackStabilityResult* result = &state->result;
	if (state->streamState != StackStreamState_Frames || frame.size() != benchmark_stack::kFrameHeaderBytes +
	    static_cast<std::size_t>(benchmark_stack::kPoseBytes) * execution.dynamicBodyCount) return 2;
	const std::uint32_t phase = DecodeU32(frame.data());
	if (phase > benchmark_stack::Phase_Measured) return 2;
	const StackSample sample = {DecodeU32(frame.data() + 4), static_cast<Phase>(phase), DecodeU32(frame.data() + 8)};
	if (phase == benchmark_stack::Phase_Construction)
	{
		if (sample.step != 0 || (state->frames == 0 ? sample.segment != 0 : sample.segment != state->segment + 1))
			return 2;
		state->segment = sample.segment;
		state->havePrevious = 0;
		state->unforced = 1;
		result->impactPresence = PresenceStatus_Absent;
		result->impactIntervalEnd = {};
		std::fill(state->minima.begin(), state->minima.end(), std::numeric_limits<double>::infinity());
		std::fill(state->entered.begin(), state->entered.end(), PresenceStatus_Absent);
		if (state->warmup == execution.warmupWorkUnitCount) state->measuredConstruction = 1;
	}
	else if (state->frames == 0 || sample.segment != state->segment ||
	         (phase == benchmark_stack::Phase_Warmup ? state->measured != 0 || sample.step != state->warmup + 1 || state->warmup >= execution.warmupWorkUnitCount
	             : sample.step != state->measured + 1 || state->warmup != execution.warmupWorkUnitCount || state->measured >= execution.measuredWorkUnitCount))
		return 2;
	int validSlots = 1;
	for (std::uint32_t index = 0; index < state->current.size(); ++index)
	{
		const std::uint8_t* pose = frame.data() + benchmark_stack::kFrameHeaderBytes + benchmark_stack::kPoseBytes * index;
		if (DecodeU32(pose) != index)
			validSlots = 0;
		state->current[index] = {index, {DecodeFloat(pose + 4), DecodeFloat(pose + 8), DecodeFloat(pose + 12)},
		    {DecodeFloat(pose + 16), DecodeFloat(pose + 20), DecodeFloat(pose + 24), DecodeFloat(pose + 28)}};
	}
	if (validSlots == 0)
	{
		result->reason = "capture_body_identity";
		return 2;
	}
	if (state->supported == 0)
	{
		if (state->frames == 0) result->firstSample = sample;
		result->lastSample = sample;
	}
	for (std::uint32_t index = 0; index < state->current.size(); ++index)
		if ((result->criterion != StackCriterion_ContactIslandsShapePreservation || state->supported == 0) && FinitePose(state->current[index]) == 0)
			Breach(result, StackRule_InvalidState, index, sample);
	if (state->supported != 0 && state->unforced != 0 && execution.fixtureKind == CaseFixtureKind_LargePyramid)
	{
		const double radius = execution.largePyramid.projectileRadius + state->impactAllowance +
		    std::sqrt(state->half[0] * state->half[0] + state->half[1] * state->half[1] + state->half[2] * state->half[2]);
		if (PossibleImpact(state->havePrevious != 0 ? std::span<const Pose>(state->previous) : std::span<const Pose>(state->current), state->current, state->boxes, radius) != 0)
		{
			state->unforced = 0;
			if (result->impactPresence == PresenceStatus_Absent)
			{
				result->impactPresence = PresenceStatus_Present;
				result->impactIntervalEnd = sample;
			}
		}
	}
	if (state->supported != 0 && state->unforced != 0 && (result->criterion != StackCriterion_ContainerEscape ||
	    (state->measuredConstruction != 0 && state->warmup == execution.warmupWorkUnitCount && phase != benchmark_stack::Phase_Warmup)))
	{
		if (state->eligible++ == 0)
		{
			result->firstSample = sample;
			result->unforcedStackShapeExcess = {0, 0, sample};
		}
		if (sample.phase != benchmark_stack::Phase_Construction) ++state->eligibleSteps;
		if (result->criterion == StackCriterion_PyramidUnforcedShape)
			result->unforcedShapePresence = PresenceStatus_Present;
		result->lastSample = sample;
		const int terminal = result->criterion == StackCriterion_ContactIslandsShapePreservation && state->terminalStart != 0 &&
		    sample.phase == benchmark_stack::Phase_Measured && sample.step >= state->terminalStart;
		if (terminal != 0 && state->terminalPoses++ == 0)
		{
			result->terminalStackShapeExcess = {0, 0, sample};
			result->terminalMetricsPresence = PresenceStatus_Present;
		}
		AssessFrame(state->current, state->references, state->half, &state->minima, state->container, &state->entered, sample, result,
		    terminal != 0 ? &execution.contactIslands : nullptr,
		    result->criterion == StackCriterion_PyramidUnforcedShape ? &execution : nullptr);
	}
	if (!state->previous.empty() && state->unforced != 0)
	{
		state->previous.swap(state->current);
		state->havePrevious = 1;
	}
	++state->frames;
	if (phase == benchmark_stack::Phase_Warmup)
		++state->warmup;
	if (phase == benchmark_stack::Phase_Measured)
		++state->measured;
	return 0;
}

int AcceptStackFooter(std::span<const std::uint8_t> footer, StackAssessmentState* state)
{
	const CaseExecutionSpec& execution = state->execution;
	if (state->streamState != StackStreamState_Frames || footer.size() != benchmark_stack::kFooterBytes ||
	    DecodeU32(footer.data()) != benchmark_stack::Phase_Complete || DecodeU32(footer.data() + 4) != state->frames ||
	    state->frames == 0 || state->warmup != execution.warmupWorkUnitCount || state->measured != execution.measuredWorkUnitCount) return 2;
	const double elapsed = DecodeDouble(footer.data() + 8);
	if (std::isfinite(elapsed) == 0 || elapsed < 0)
	{
		state->result.reason = "capture_footer_duration";
		return 2;
	}
	state->result.captureElapsedMs = elapsed;
	state->streamState = StackStreamState_Footer;
	return 0;
}

int CompleteStackAssessment(StackAssessmentState* state)
{
	const CaseExecutionSpec& execution = state->execution;
	StackStabilityResult* result = &state->result;
	if (state->streamState != StackStreamState_Footer) return 2;
	if (result->criterion == StackCriterion_ContainerEscape && state->measuredConstruction == 0)
	{
		result->reason = "capture_measured_initial_missing";
		return 2;
	}
	
	result->coverage = StackCoverage_Complete;
	result->reason = state->supported == 0 ? "unsupported_authored_expectation" : state->eligible == 0 ? "no_assessment_window" : "evaluated";
	if (result->criterion == StackCriterion_ContactIslandsShapePreservation && state->supported != 0 &&
	    (state->terminalStart == 0 || state->terminalPoses != execution.timestepHz + 1))
		result->reason = "insufficient_terminal_window";
	if (result->criterion == StackCriterion_PyramidUnforcedShape && state->eligibleSteps != 0)
		result->unforcedShapePresence = PresenceStatus_Present;
	if (result->criterion == StackCriterion_PyramidUnforcedShape && state->eligibleSteps == 0 && state->supported != 0)
		result->reason = "no_assessment_window";
	if (state->supported != 0 && state->eligible != 0 && result->assessment != StackAssessment_Fail &&
	    (result->criterion != StackCriterion_PyramidUnforcedShape || state->eligibleSteps != 0) &&
	    (result->criterion != StackCriterion_ContactIslandsShapePreservation ||
	     (result->terminalMetricsPresence == PresenceStatus_Present && state->terminalPoses == execution.timestepHz + 1)))
		result->assessment = StackAssessment_Pass;
	state->streamState = StackStreamState_Ended;
	return 0;
}

ArenaStatus AnalyzeStackTrace(const std::filesystem::path& trace, const CaseExecutionSpec& execution,
                             std::string_view runId, std::string_view engineId, std::uint32_t threads,
                             std::uint32_t repeat, StackStabilityResult* result, StatusRecord* error)
{
	*error = {};
	StackAssessmentState state = {};
	InitializeStackAssessment(execution, runId, engineId, threads, repeat, &state);
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	std::ifstream input(trace, std::ios::binary);
	std::array<std::uint8_t, benchmark_stack::kHeaderBytes> header = {};
	if (input.read(reinterpret_cast<char*>(header.data()), header.size()) && AcceptStackHeader(header, &state) == 0)
	{
		std::vector<std::uint8_t> frame(benchmark_stack::kFrameHeaderBytes + static_cast<std::size_t>(benchmark_stack::kPoseBytes) * execution.dynamicBodyCount);
		while (input.read(reinterpret_cast<char*>(frame.data()), 4))
		{
			if (DecodeU32(frame.data()) == benchmark_stack::Phase_Complete)
			{
				std::array<std::uint8_t, benchmark_stack::kFooterBytes> footer = {};
				std::copy_n(frame.data(), 4, footer.data());
				if (input.read(reinterpret_cast<char*>(footer.data() + 4), footer.size() - 4) &&
				    AcceptStackFooter(footer, &state) == 0 && input.peek() == std::char_traits<char>::eof())
					CompleteStackAssessment(&state);
				break;
			}
			if (!input.read(reinterpret_cast<char*>(frame.data() + 4), static_cast<std::streamsize>(frame.size() - 4)) ||
			    AssessStackFrame(frame, &state) != 0) break;
		}
	}
	state.result.analysisElapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	*result = std::move(state.result);
	return ArenaStatus_Ok;
}

ArenaStatus LoadStackStability(const std::filesystem::path& path, std::vector<StackStabilityResult>* results, StatusRecord* error)
{
	results->clear();
	ReadContext context = {results, {}};
	CsvHeader header = {};
	CsvReadRecord read = {};
	if (ReadCsvFile(path.c_str(), &header, ReadRow, &context, &read, error) != ArenaStatus_Ok)
		return error->code;
	if (header.fieldCount != kColumns.size() || results->empty()) return StabilityError(error, "stability_empty_or_columns");
	for (std::size_t index = 0; index < kColumns.size(); ++index)
		if (CsvHeaderTextView(&header, header.fields[index]) != kColumns[index]) return StabilityError(error, "stability_column_name");
	for (std::size_t index = 0; index < context.masks.size(); ++index)
	{
		const StackStabilityResult& result = (*results)[index];
		const int islands = result.criterion == StackCriterion_ContactIslandsStabilization || result.criterion == StackCriterion_ContactIslandsStabilization10cm ||
		    result.criterion == StackCriterion_ContactIslandsShapePreservation;
		if (context.masks[index] != (result.criterion == StackCriterion_ContactIslandsShapePreservation ? 11U : islands != 0 ? 15U : result.criterion == StackCriterion_PyramidUnforcedShape ? 19U : 3U))
			return StabilityError(error, "stability_missing_metric");
		if (result.criterion == StackCriterion_PyramidUnforcedShape && result.assessment == StackAssessment_Pass &&
		    (result.unforcedShapePresence != PresenceStatus_Present || result.unforcedStackShapeExcess.value > 0.10))
			return StabilityError(error, "stability_unforced_pass_evidence");
		if (islands != 0 && result.assessment == StackAssessment_Pass &&
		    (result.terminalMetricsPresence != PresenceStatus_Present || result.margin <= 0 ||
		     result.lastSample.phase != benchmark_stack::Phase_Measured || result.lastSample.step * result.sampleSeconds < 1 + result.sampleSeconds ||
		     (result.criterion != StackCriterion_ContactIslandsShapePreservation && result.settlingStepDisplacement.value > ContactIslandsSettlingStepLimit(result)) ||
		     result.terminalStackShapeExcess.value > result.margin))
			return StabilityError(error, "stability_terminal_pass_evidence");
	}
	return ArenaStatus_Ok;
}

ArenaStatus CommitStackStability(const std::filesystem::path& path, const StackStabilityResult& result, StatusRecord* error)
{
	std::vector<StackStabilityResult> results;
	std::error_code filesystemError;
	const int exists = std::filesystem::exists(path, filesystemError) ? 1 : 0;
	if (filesystemError || (exists != 0 && LoadStackStability(path, &results, error) != ArenaStatus_Ok))
		return exists != 0 ? error->code : StabilityError(error, "stability_destination");
	for (const StackStabilityResult& saved : results)
		if (saved.runId != result.runId) return StabilityError(error, "stability_run_id_disagreement");
	std::size_t ordinal = 0;
	while (ordinal < results.size() && (results[ordinal].engineId != result.engineId ||
	       results[ordinal].threadCount != result.threadCount || results[ordinal].repeatIndex != result.repeatIndex))
		++ordinal;
	if (ordinal == results.size())
		results.push_back(result);
	else
		results[ordinal] = result;
	return WriteStackStability(path, results, error);
}

ArenaStatus WriteStackStability(const std::filesystem::path& path, std::span<const StackStabilityResult> results, StatusRecord* error)
{
	const std::filesystem::path temporary = path.wstring() + L".partial";
	CsvWriter writer = {};
	ArenaStatus status = OpenCsvWriter(temporary.c_str(), &writer, error);
	for (std::size_t index = 0; status == ArenaStatus_Ok && index < kColumns.size(); ++index)
		status = WriteCsvField(&writer, kColumns[index], index + 1 == kColumns.size() ? CsvFieldTerminator_EndRow : CsvFieldTerminator_MoreFields, error);
	for (const StackStabilityResult& tuple : results)
		if (status == ArenaStatus_Ok)
			status = WriteResult(&writer, tuple, error);
	if (status == ArenaStatus_Ok)
		status = FinishCsvWriter(&writer, error);
	DestroyCsvWriter(&writer);
	if (status == ArenaStatus_Ok)
	{
		HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			status = StabilityError(error, "stability_data_open", ArenaStatus_RunFailed);
		else
		{
			if (FlushFileBuffers(file) == 0)
				status = StabilityError(error, "stability_data_flush", ArenaStatus_RunFailed);
			const int closed = CloseHandle(file);
			if (closed == 0 && status == ArenaStatus_Ok)
				status = StabilityError(error, "stability_data_close", ArenaStatus_RunFailed);
		}
	}
	if (status == ArenaStatus_Ok && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
		status = StabilityError(error, "stability_atomic_commit", ArenaStatus_RunFailed);
	return status;
}

double ContactIslandsSettlingStepLimit(const StackStabilityResult& result)
{
	return (result.margin / (result.criterion == StackCriterion_ContactIslandsStabilization10cm ? 10 : 2)) * result.sampleSeconds;
}

ArenaStatus ValidateStackShapeStabilityEvidence(const CaseExecutionSpec& execution,
                                                   const StackStabilityResult& result, StatusRecord* error)
{
	if (result.criterion == StackCriterion_PyramidUnforcedShape)
	{
		if ((execution.fixtureKind != CaseFixtureKind_LargePyramid && execution.fixtureKind != CaseFixtureKind_PyramidWall) ||
		    execution.timestepHz == 0 || result.sampleSeconds != 1.0 / execution.timestepHz || result.margin != 0.10)
			return StabilityError(error, "stability_pyramid_frozen_policy");
		const std::uint32_t boxes = execution.dynamicBodyCount -
		    (execution.fixtureKind == CaseFixtureKind_LargePyramid ? execution.largePyramid.projectileCount : 0);
		const std::uint32_t finalSegment = execution.fixtureKind == CaseFixtureKind_LargePyramid &&
		    result.engineId == "entasis" && execution.warmupWorkUnitCount != 0 ? 1 : 0;
		const auto validSample = [&execution, finalSegment](const StackSample& sample) -> int
		{
			if (sample.segment > finalSegment) return 0;
			if (sample.phase == benchmark_stack::Phase_Construction) return sample.step == 0 ? 1 : 0;
			if (sample.phase == benchmark_stack::Phase_Warmup)
				return sample.segment == 0 && sample.step != 0 && sample.step <= execution.warmupWorkUnitCount ? 1 : 0;
			return sample.phase == benchmark_stack::Phase_Measured && sample.segment == finalSegment &&
			    sample.step != 0 && sample.step <= execution.measuredWorkUnitCount ? 1 : 0;
		};
		const auto before = [](const StackSample& left, const StackSample& right) -> int
		{
			return left.segment < right.segment || (left.segment == right.segment &&
			    (left.phase < right.phase || (left.phase == right.phase && left.step < right.step))) ? 1 : 0;
		};
		if (validSample(result.firstSample) == 0 || validSample(result.lastSample) == 0 || before(result.lastSample, result.firstSample) != 0)
			return StabilityError(error, "stability_pyramid_window_identity");
		if (result.impactPresence == PresenceStatus_Present &&
		    (execution.fixtureKind != CaseFixtureKind_LargePyramid || result.impactIntervalEnd.segment != finalSegment ||
		     validSample(result.impactIntervalEnd) == 0 || before(result.lastSample, result.impactIntervalEnd) == 0))
			return StabilityError(error, "stability_pyramid_impact_window");
		if (execution.fixtureKind == CaseFixtureKind_PyramidWall && result.coverage == StackCoverage_Complete &&
		    (result.lastSample.phase != benchmark_stack::Phase_Measured || result.lastSample.step != execution.measuredWorkUnitCount))
			return StabilityError(error, "stability_pyramid_wall_final_sample");
		if (result.unforcedShapePresence == PresenceStatus_Present)
		{
			const StackMetric& metric = result.unforcedStackShapeExcess;
			if (metric.body >= boxes || validSample(metric.sample) == 0 || before(metric.sample, result.firstSample) != 0 ||
			    before(result.lastSample, metric.sample) != 0)
				return StabilityError(error, "stability_pyramid_metric_identity");
		}
		if (result.firstRule == StackRule_UnforcedSlotEnvelope || result.firstRule == StackRule_UnforcedAdjacentCompression)
		{
			if (result.unforcedShapePresence != PresenceStatus_Present || result.firstBody >= boxes ||
			    validSample(result.firstBreach) == 0 || before(result.lastSample, result.firstBreach) != 0)
				return StabilityError(error, "stability_pyramid_breach_identity");
		}
		if (result.assessment == StackAssessment_Pass &&
		    (result.coverage != StackCoverage_Complete || result.unforcedShapePresence != PresenceStatus_Present ||
		     result.unforcedStackShapeExcess.value > 0.10 || result.lastSample.phase == benchmark_stack::Phase_Construction ||
		     (result.impactPresence == PresenceStatus_Absent && (result.lastSample.segment != finalSegment ||
		      result.lastSample.phase != benchmark_stack::Phase_Measured || result.lastSample.step != execution.measuredWorkUnitCount))))
			return StabilityError(error, "stability_pyramid_frozen_pass_limits");
		return ArenaStatus_Ok;
	}
	if (result.criterion != StackCriterion_ContactIslandsStabilization && result.criterion != StackCriterion_ContactIslandsStabilization10cm &&
	    result.criterion != StackCriterion_ContactIslandsShapePreservation)
		return ArenaStatus_Ok;
	const double sampleSeconds = 1.0 / execution.timestepHz;
	const double margin = (result.criterion == StackCriterion_ContactIslandsStabilization ? 0.04 : 0.20) *
	    std::min({execution.selectedGeometry.halfExtents.x, execution.selectedGeometry.halfExtents.y, execution.selectedGeometry.halfExtents.z});
	if (result.sampleSeconds != sampleSeconds || result.margin != margin)
		return StabilityError(error, "stability_contact_islands_frozen_policy");
	const std::uint32_t finalStep = execution.measuredWorkUnitCount;
	const int partialTerminal = result.criterion == StackCriterion_ContactIslandsShapePreservation &&
	    result.coverage == StackCoverage_Incomplete && result.terminalMetricsPresence == PresenceStatus_Present;
	if ((result.coverage == StackCoverage_Complete || (result.terminalMetricsPresence == PresenceStatus_Present && partialTerminal == 0)) &&
	    (result.lastSample.segment != 0 || result.lastSample.phase != benchmark_stack::Phase_Measured || result.lastSample.step != finalStep))
		return StabilityError(error, "stability_contact_islands_final_sample");
	if (result.terminalMetricsPresence == PresenceStatus_Present)
	{
		if (finalStep <= execution.timestepHz)
			return StabilityError(error, "stability_contact_islands_terminal_window");
		const std::uint32_t firstStep = finalStep - execution.timestepHz;
		if (partialTerminal != 0 && (result.lastSample.segment != 0 || result.lastSample.phase != benchmark_stack::Phase_Measured ||
		    result.lastSample.step < firstStep || result.lastSample.step > finalStep))
			return StabilityError(error, "stability_contact_islands_final_sample");
		const std::uint32_t observedEnd = partialTerminal != 0 ? result.lastSample.step : finalStep;
		const StackMetric metrics[] = {result.terminalStackShapeExcess, result.settlingStepDisplacement};
		const std::uint32_t count = result.criterion == StackCriterion_ContactIslandsShapePreservation ? 1 : 2;
		for (std::uint32_t index = 0; index < count; ++index)
		{
			const StackMetric& metric = metrics[index];
			if (metric.body >= execution.dynamicBodyCount || metric.sample.segment != 0 ||
			    metric.sample.phase != benchmark_stack::Phase_Measured || metric.sample.step < firstStep + index || metric.sample.step > observedEnd)
				return StabilityError(error, "stability_contact_islands_terminal_metric_identity");
		}
	}
	if (result.firstRule >= StackRule_TerminalSlotEnvelope)
	{
		if (result.terminalMetricsPresence != PresenceStatus_Present || result.firstBody >= execution.dynamicBodyCount ||
		    result.firstBreach.segment != 0 || result.firstBreach.phase != benchmark_stack::Phase_Measured ||
		    result.firstBreach.step < finalStep - execution.timestepHz + (result.firstRule == StackRule_TerminalMotion ? 1U : 0U) ||
		    result.firstBreach.step > (partialTerminal != 0 ? result.lastSample.step : finalStep))
			return StabilityError(error, "stability_contact_islands_terminal_breach_identity");
	}
	if (result.assessment == StackAssessment_Pass &&
	    (result.coverage != StackCoverage_Complete || result.terminalMetricsPresence != PresenceStatus_Present ||
	     result.terminalStackShapeExcess.value > margin ||
	     (result.criterion != StackCriterion_ContactIslandsShapePreservation && result.settlingStepDisplacement.value > ContactIslandsSettlingStepLimit(result))))
		return StabilityError(error, "stability_contact_islands_frozen_pass_limits");
	return ArenaStatus_Ok;
}

StackAssessment StackQualificationAssessment(const StackStabilityResult& result, StackCriterion requiredCriterion)
{
	if (result.criterion == StackCriterion_LegacyMovement) return StackAssessment_Unassessed;
	if (result.criterion == requiredCriterion) return result.assessment;
	if ((requiredCriterion == StackCriterion_ContactIslandsStabilization || requiredCriterion == StackCriterion_ContactIslandsStabilization10cm ||
	     requiredCriterion == StackCriterion_ContactIslandsShapePreservation || requiredCriterion == StackCriterion_PyramidUnforcedShape) &&
	    (result.criterion == StackCriterion_SupportPlane || result.criterion == StackCriterion_ContactIslandsStabilization ||
	     result.criterion == StackCriterion_ContactIslandsStabilization10cm) &&
	    result.assessment == StackAssessment_Fail && (result.firstRule == StackRule_SupportPlane || result.firstRule == StackRule_InvalidState))
		return StackAssessment_Fail;
	return StackAssessment_Unassessed;
}

StackCriterion CurrentStackCriterion(CaseFixtureKind fixture)
{
	return fixture == CaseFixtureKind_OpenContainerFallingPile ? StackCriterion_ContainerEscape :
	    fixture == CaseFixtureKind_BoxContactIslands ? StackCriterion_ContactIslandsShapePreservation :
	    fixture == CaseFixtureKind_LargePyramid || fixture == CaseFixtureKind_PyramidWall ? StackCriterion_PyramidUnforcedShape : StackCriterion_SupportPlane;
}

const char* StackCriterionName(StackCriterion criterion)
{
	constexpr std::array<const char*, 7> names = {kStackCriterion, kLegacyStackCriterion, kContainerCriterion, kContactIslandsCriterion, kContactIslandsCriterion, kContactIslandsShapeCriterion, kPyramidUnforcedShapeCriterion};
	return names[criterion];
}

const char* StackMarginPolicyName(StackCriterion criterion)
{
	constexpr std::array<const char*, 7> names = {kStackMarginPolicy, kLegacyStackMarginPolicy, kContainerMarginPolicy, kContactIslandsMarginPolicy, kContactIslands10cmMarginPolicy, kContactIslandsShapeMarginPolicy, kPyramidUnforcedShapeMarginPolicy};
	return names[criterion];
}

ArenaStatus AnalyzeContainerRecording(ReplayRecording* recording, const CaseExecutionSpec& execution,
                                     std::string_view runId, std::string_view engineId, std::uint32_t threads,
                                     std::uint32_t repeat, StackStabilityResult* result, StatusRecord* error)
{
	*result = {};
	result->runId = runId;
	result->engineId = engineId;
	result->threadCount = threads;
	result->repeatIndex = repeat;
	result->criterion = StackCriterion_ContainerEscape;
	result->reason = "recording_measured_window_capture_overhead_unavailable";
	result->captureElapsedMs = std::numeric_limits<double>::quiet_NaN();
	result->sampleSeconds = execution.timestepHz != 0 ? 1.0 / execution.timestepHz : 0;
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	std::array<double, 3> half = {};
	std::vector<RestEnvelope> references;
	ContainerBounds container = {};
	const int supported = BuildContainerBounds(execution, &container) != 0 &&
	    BuildEnvelopes(execution, execution.dynamicBodyCount, &half, &references) != 0;
	if (supported == 0) result->reason = "unsupported_container_expectation";
	if (recording->layout.frameCount != static_cast<std::uint64_t>(execution.measuredWorkUnitCount) + 1 ||
	    recording->scene.dynamicTransformCount != execution.dynamicBodyCount ||
	    recording->workKind != benchmark_replay::WorkKind_Dynamics || recording->timestep != result->sampleSeconds)
		return StabilityError(error, "recording_measured_coverage");
	std::vector<Pose> poses(execution.dynamicBodyCount);
	std::vector<double> minima(execution.dynamicBodyCount, std::numeric_limits<double>::infinity());
	std::vector<PresenceStatus> entered(execution.dynamicBodyCount);
	for (std::uint32_t step = 0; step <= execution.measuredWorkUnitCount; ++step)
	{
		if (SeekReplay(recording, step, error) != ArenaStatus_Ok || recording->frame.ordinal != step ||
		    recording->frame.transforms.size() != poses.size())
			return StabilityError(error, "recording_frame_unavailable step=" + std::to_string(step));
		const StackSample sample = {0, step == 0 ? benchmark_stack::Phase_Construction : benchmark_stack::Phase_Measured, step};
		for (std::uint32_t body = 0; body < poses.size(); ++body)
		{
			const benchmark_visual::VisualStableTransform& value = recording->frame.transforms[body];
			if (value.stableSlot != body)
				return StabilityError(error, "recording_body_identity");
			poses[body] = {body, {value.transform.positionX, value.transform.positionY, value.transform.positionZ},
			    {value.transform.rotationX, value.transform.rotationY, value.transform.rotationZ, value.transform.rotationW}};
			if (FinitePose(poses[body]) == 0)
				return StabilityError(error, "recording_invalid_pose");
		}
		if (step == 0) result->firstSample = sample;
		result->lastSample = sample;
		if (supported != 0) AssessFrame(poses, references, half, &minima, container, &entered, sample, result);
	}
	result->coverage = StackCoverage_Complete;
	if (supported != 0 && result->assessment != StackAssessment_Fail) result->assessment = StackAssessment_Pass;
	result->analysisElapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	return ArenaStatus_Ok;
}

ArenaStatus AnalyzeContactIslandsRecording(ReplayRecording* recording, const CaseExecutionSpec& execution,
                                          std::string_view runId, std::string_view engineId, std::uint32_t threads,
                                          std::uint32_t repeat, StackStabilityResult* result, StatusRecord* error)
{
	*result = {};
	result->runId = runId;
	result->engineId = engineId;
	result->threadCount = threads;
	result->repeatIndex = repeat;
	result->criterion = StackCriterion_ContactIslandsShapePreservation;
	result->margin = 0.20 * std::min({execution.selectedGeometry.halfExtents.x, execution.selectedGeometry.halfExtents.y, execution.selectedGeometry.halfExtents.z});
	result->sampleSeconds = execution.timestepHz != 0 ? 1.0 / execution.timestepHz : 0;
	result->reason = "recording_measured_only_warmup_safety_unavailable";
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	if (recording->layout.frameCount != static_cast<std::uint64_t>(execution.measuredWorkUnitCount) + 1 ||
	    recording->scene.dynamicTransformCount != execution.dynamicBodyCount ||
	    recording->workKind != benchmark_replay::WorkKind_Dynamics || recording->timestep != result->sampleSeconds)
		return StabilityError(error, "recording_measured_coverage");
	std::array<double, 3> half = {};
	std::vector<RestEnvelope> references;
	const int supported = execution.fixtureKind == CaseFixtureKind_BoxContactIslands && BuildEnvelopes(execution, execution.dynamicBodyCount, &half, &references) != 0;
	const std::uint32_t terminalStart = execution.measuredWorkUnitCount > execution.timestepHz ? execution.measuredWorkUnitCount - execution.timestepHz : 0;
	if (supported == 0) result->reason = "unsupported_authored_expectation";
	else if (terminalStart == 0) result->reason = "insufficient_terminal_window";
	std::vector<Pose> poses(execution.dynamicBodyCount);
	std::vector<double> minima(execution.dynamicBodyCount, std::numeric_limits<double>::infinity());
	std::vector<PresenceStatus> entered;
	for (std::uint32_t step = 0; step <= execution.measuredWorkUnitCount; ++step)
	{
		if (SeekReplay(recording, step, error) != ArenaStatus_Ok || recording->frame.ordinal != step || recording->frame.transforms.size() != poses.size())
			return StabilityError(error, "recording_frame_unavailable step=" + std::to_string(step));
		const StackSample sample = {0, step == 0 ? benchmark_stack::Phase_Construction : benchmark_stack::Phase_Measured, step};
		for (std::uint32_t body = 0; body < poses.size(); ++body)
		{
			const benchmark_visual::VisualStableTransform& value = recording->frame.transforms[body];
			if (value.stableSlot != body) return StabilityError(error, "recording_body_identity");
			poses[body] = {body, {value.transform.positionX, value.transform.positionY, value.transform.positionZ},
			    {value.transform.rotationX, value.transform.rotationY, value.transform.rotationZ, value.transform.rotationW}};
		}
		if (step == 0) result->firstSample = sample;
		result->lastSample = sample;
		const int terminal = supported != 0 && terminalStart != 0 && step >= terminalStart;
		if (terminal != 0 && step == terminalStart)
		{
			result->terminalStackShapeExcess = {0, 0, sample};
		}
		if (supported != 0)
			AssessFrame(poses, references, half, &minima, {}, &entered, sample, result,
			    terminal != 0 ? &execution.contactIslands : nullptr);
	}
	if (supported != 0 && terminalStart != 0) result->terminalMetricsPresence = PresenceStatus_Present;
	result->analysisElapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	return ArenaStatus_Ok;
}

ArenaStatus CertifyLegacyStackPass(const CaseExecutionSpec& execution, const StackStabilityResult& saved,
                                 StackStabilityResult* corrected, StatusRecord* error)
{
	if (benchmark_stack::TargetFixture(execution.fixtureKind) != 0 ||
	    saved.criterion != StackCriterion_LegacyMovement || saved.assessment != StackAssessment_Pass ||
	    saved.coverage != StackCoverage_Complete)
		return StabilityError(error, "legacy_pass_evidence_unavailable");
	const std::uint32_t boxes = execution.dynamicBodyCount -
	    (execution.fixtureKind == CaseFixtureKind_LargePyramid ? execution.largePyramid.projectileCount : 0);
	std::array<double, 3> half = {};
	std::vector<RestEnvelope> references;
	if (BuildEnvelopes(execution, boxes, &half, &references) == 0 ||
	    saved.margin != 0.1 * std::min({half[0], half[1], half[2]}))
		return StabilityError(error, "legacy_pass_geometry_unproved");
	for (const RestEnvelope& reference : references)
	{
		double supportUpper = reference.supportCount == 0 ? 0 : std::numeric_limits<double>::infinity();
		for (std::uint32_t slot = 0; slot < reference.supportCount; ++slot)
			supportUpper = std::min(supportUpper, references[reference.supports[slot]].upper[1] + saved.margin + half[1]);
		if (reference.lower[1] - saved.margin < supportUpper)
			return StabilityError(error, "legacy_pass_support_implication_unproved");
	}
	*corrected = saved;
	corrected->criterion = StackCriterion_SupportPlane;
	corrected->margin = 0;
	corrected->reason = "certified_legacy_pass";
	return ArenaStatus_Ok;
}

StackAssessment AggregateStackStability(std::span<const StackStabilityResult> results, std::string_view engine,
	                                    std::uint32_t threads, std::uint32_t repeats, StackCriterion requiredCriterion)
{
	std::uint32_t passing = 0;
	std::uint64_t seen = 0;
	if (repeats == 0 || repeats > 64)
		return StackAssessment_Unassessed;
	for (const StackStabilityResult& result : results)
	{
		if (result.engineId != engine || result.threadCount != threads || result.repeatIndex >= repeats)
			continue;
		if (StackQualificationAssessment(result, requiredCriterion) == StackAssessment_Fail)
			return StackAssessment_Fail;
		const std::uint64_t bit = UINT64_C(1) << result.repeatIndex;
		if ((seen & bit) != 0)
			return StackAssessment_Unassessed;
		seen |= bit;
		if (StackQualificationAssessment(result, requiredCriterion) == StackAssessment_Pass && result.coverage == StackCoverage_Complete)
			++passing;
	}
	return repeats != 0 && passing == repeats ? StackAssessment_Pass : StackAssessment_Unassessed;
}

}
