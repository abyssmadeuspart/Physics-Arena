#include "contact_islands_stability.h"

#include <algorithm>
#include <cmath>

namespace physics_arena
{
int BuildContactIslandsEnvelopes(const CaseExecutionSpec& spec, std::uint32_t boxes,
                                const std::array<double, 3>& half, std::vector<RestEnvelope>* references)
{
	const CaseExecutionContactIslands& fixture = spec.contactIslands;
	if (fixture.bodyInitialY != half[1] || fixture.bodyGrid[0] == 0 || fixture.bodyGrid[1] == 0 || fixture.bodyGrid[2] == 0)
		return 0;
	const std::array<double, 3> spacing = std::array<double, 3>{fixture.bodySpacing.x, fixture.bodySpacing.y, fixture.bodySpacing.z};
	if ((fixture.islandGrid[0] > 1 && fixture.islandSpacing[0] < fixture.bodyGrid[0] * 2 * half[0]) ||
	    (fixture.islandGrid[1] > 1 && fixture.islandSpacing[1] < fixture.bodyGrid[2] * 2 * half[2]))
		return 0;
	for (std::uint32_t gz = 0; gz < fixture.islandGrid[1]; ++gz)
		for (std::uint32_t gx = 0; gx < fixture.islandGrid[0]; ++gx)
			for (std::uint32_t y = 0; y < fixture.bodyGrid[1]; ++y)
				for (std::uint32_t z = 0; z < fixture.bodyGrid[2]; ++z)
					for (std::uint32_t x = 0; x < fixture.bodyGrid[0]; ++x)
					{
						const double originX = fixture.islandSpacing[0] * (gx - 0.5 * (fixture.islandGrid[0] - 1));
						const double originZ = fixture.islandSpacing[1] * (gz - 0.5 * (fixture.islandGrid[1] - 1));
						const std::array<double, 3> grid = {x - 0.5 * (fixture.bodyGrid[0] - 1), static_cast<double>(y),
						    z - 0.5 * (fixture.bodyGrid[2] - 1)};
						const std::array<double, 3> origin = {originX, fixture.bodyInitialY, originZ};
						RestEnvelope reference = {};
						for (std::uint32_t axis = 0; axis < 3; ++axis)
						{
							const double authored = origin[axis] + grid[axis] * spacing[axis];
							const double touching = origin[axis] + grid[axis] * 2 * half[axis];
							reference.lower[axis] = std::min(authored, touching);
							reference.upper[axis] = std::max(authored, touching);
						}
						if (std::max(std::abs(reference.lower[0] - originX), std::abs(reference.upper[0] - originX)) + half[0] > fixture.floorHalfExtents.x ||
						    std::max(std::abs(reference.lower[2] - originZ), std::abs(reference.upper[2] - originZ)) + half[2] > fixture.floorHalfExtents.z)
							return 0;
						if (y != 0)
						{
							reference.supports[0] = static_cast<std::uint32_t>(references->size()) - fixture.bodyGrid[0] * fixture.bodyGrid[2];
							reference.supportCount = 1;
						}
						references->push_back(reference);
						if (references->size() > boxes)
							return 0;
					}
	return references->size() == boxes ? 1 : 0;
}

StackRule AssessContactIslandsBody(const CaseExecutionContactIslands& fixture,
                                 std::span<const benchmark_stack::Pose> poses,
                                 std::span<const RestEnvelope> references,
                                 const std::array<double, 3>& half, std::uint32_t body,
                                 StackSample sample, StackStabilityResult* result)
{
	const benchmark_stack::Pose& pose = poses[body];
	const std::array<double, 3> position = {pose.position.x, pose.position.y, pose.position.z};
	const std::array<double, 3> radius = StackProjectedRadii(pose, half);
	double slotExcess = 0, overlap = 0;
	for (std::uint32_t axis = 0; axis < 3; ++axis)
		slotExcess = std::max({slotExcess, references[body].lower[axis] - half[axis] - position[axis] + radius[axis],
		    position[axis] + radius[axis] - references[body].upper[axis] - half[axis]});
	const std::uint32_t layerSize = fixture.bodyGrid[0] * fixture.bodyGrid[2];
	const std::uint32_t local = body % (layerSize * fixture.bodyGrid[1]);
	const std::uint32_t coordinates[] = {local % fixture.bodyGrid[0], local / layerSize, local / fixture.bodyGrid[0] % fixture.bodyGrid[2]};
	const std::uint32_t strides[] = {1, layerSize, fixture.bodyGrid[0]};
	for (std::uint32_t axis = 0; axis < 3; ++axis)
	{
		if (coordinates[axis] + 1 == fixture.bodyGrid[axis]) continue;
		const benchmark_stack::Pose& neighbour = poses[body + strides[axis]];
		const std::array<double, 3> neighbourRadius = StackProjectedRadii(neighbour, half);
		const double neighbourPosition[] = {neighbour.position.x, neighbour.position.y, neighbour.position.z};
		const double projected = radius[axis] + neighbourRadius[axis] - (neighbourPosition[axis] - position[axis]);
		if (std::isfinite(projected)) overlap = std::max(overlap, projected);
	}
	const double shape = std::max(slotExcess, overlap);
	if (shape > result->terminalStackShapeExcess.value)
		result->terminalStackShapeExcess = {shape, body, sample};
	StackRule rule = slotExcess > result->margin ? StackRule_TerminalSlotEnvelope :
	    overlap > result->margin ? StackRule_TerminalAdjacentCompression : StackRule_None;
	return rule;
}
}
