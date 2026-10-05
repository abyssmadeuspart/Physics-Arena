#pragma once

#include "box3d_runner_args.h"

#include "box3d_case_registry.h"
#include "pyramid_wall.h"

#include <array>
#include <chrono>
#include <vector>

namespace box3d_benchmark
{
struct Box3DWallBodyInput
{
	b3Pos position;
	b3Quat rotation;
	b3Vec3 linear;
	b3Vec3 angular;
	b3Matrix3 inertia;
	float mass;
	std::uint32_t sleepFlags;
};
struct Box3DPyramidWallState
{
	VerificationMode verificationMode = VerificationMode_On;
	Box3DCaseConfig config;
	b3WorldId world;
	std::vector<b3BodyId> bodies;
	std::vector<std::chrono::steady_clock::duration::rep> durations;
	std::array<PyramidWallObservation, 4> observations;
	std::array<std::vector<Box3DWallBodyInput>, 4> observationInputs;
	double initialPotentialEnergy;
	double elapsedMs;
	std::uint32_t completed;
};

const Box3DCaseDescriptor& Box3DPyramidWallCaseDescriptor();
}
