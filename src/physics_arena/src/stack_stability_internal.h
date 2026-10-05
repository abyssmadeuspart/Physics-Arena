#pragma once

#include <array>
#include <cstdint>
#include <cmath>
#include "physics_arena/stack_stability.h"

namespace physics_arena
{
struct RestEnvelope
{
	std::array<double, 3> lower;
	std::array<double, 3> upper;
	std::array<std::uint32_t, 4> supports;
	std::uint32_t supportCount;
};
struct ContainerBounds
{
	double floor;
	double rim;
	std::array<double, 2> lower;
	std::array<double, 2> upper;
};


enum StackStreamState
{
	StackStreamState_Header,
	StackStreamState_Frames,
	StackStreamState_Footer,
	StackStreamState_Ended,
	StackStreamState_Rejected,
};

struct StackAssessmentState
{
	CaseExecutionSpec execution;
	StackStabilityResult result;
	std::array<double, 3> half;
	std::vector<RestEnvelope> references;
	ContainerBounds container;
	std::vector<benchmark_stack::Pose> current;
	std::vector<benchmark_stack::Pose> previous;
	std::vector<double> minima;
	std::vector<PresenceStatus> entered;
	double impactAllowance;
	std::uint32_t boxes, frames, warmup, measured, segment, eligible, eligibleSteps, terminalStart, terminalPoses;
	int supported, havePrevious, unforced, measuredConstruction;
	StackStreamState streamState;
};
void InitializeStackAssessment(const CaseExecutionSpec& execution, std::string_view runId,
                               std::string_view engineId, std::uint32_t threads, std::uint32_t repeat,
                               StackAssessmentState* state);
int AcceptStackHeader(std::span<const std::uint8_t> header, StackAssessmentState* state);
int AssessStackFrame(std::span<const std::uint8_t> frame, StackAssessmentState* state);
int AcceptStackFooter(std::span<const std::uint8_t> footer, StackAssessmentState* state);
int CompleteStackAssessment(StackAssessmentState* state);

inline std::array<double, 3> StackProjectedRadii(const benchmark_stack::Pose& pose, const std::array<double, 3>& half)
{
	const CaseExecutionQuaternion& q = pose.orientation;
	const double x = q.x, y = q.y, z = q.z, w = q.w;
	const double scale = 2 / (x * x + y * y + z * z + w * w);
	const double matrix[3][3] = {
	    {1 - scale * (y * y + z * z), scale * (x * y - z * w), scale * (x * z + y * w)},
	    {scale * (x * y + z * w), 1 - scale * (x * x + z * z), scale * (y * z - x * w)},
	    {scale * (x * z - y * w), scale * (y * z + x * w), 1 - scale * (x * x + y * y)}};
	std::array<double, 3> radius = {};
	for (std::uint32_t axis = 0; axis < 3; ++axis)
		for (std::uint32_t component = 0; component < 3; ++component)
			radius[axis] += std::abs(matrix[axis][component]) * half[component];
	return radius;
}

}
