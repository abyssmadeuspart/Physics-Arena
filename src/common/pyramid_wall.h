#pragma once

#include "case_execution_wire.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

struct PyramidWallObservation
{
	double centreOfMassHeight;
	double lateralRms;
	double translationalEnergy;
	double rotationalEnergy;
	double potentialEnergy;
	double floorPenetration;
	std::uint64_t escapedBodies;
	std::uint64_t invalidBodies;
	double elapsedMs;
};

inline CaseExecutionVector3 PyramidWallPosition(const CaseExecutionPyramidWall& wall, std::uint32_t index)
{
	std::uint32_t row = 0;
	std::uint32_t width = wall.rowCount;
	while (index >= width)
	{
		index -= width;
		--width;
		++row;
	}
	const float h = wall.halfExtent;
	return {(static_cast<float>(row) + 1.0f) * h + 2.0f * static_cast<float>(index) * h -
	            h * static_cast<float>(wall.rowCount),
	        (2.0f * static_cast<float>(row) + 1.0f) * h, 0.0f};
}

inline double PyramidWallInitialPotentialEnergy(const CaseExecutionPyramidWall& wall, float gravityY)
{
	const double rows = wall.rowCount;
	const double mass = 8.0 * wall.halfExtent * wall.halfExtent * wall.halfExtent * wall.density;
	const double height = wall.halfExtent * (2.0 * rows + 1.0) / 3.0;
	return mass * rows * (rows + 1.0) * 0.5 * -gravityY * height;
}

inline void AccumulatePyramidWallObservation(const CaseExecutionPyramidWall& wall, std::uint32_t index,
                                            CaseExecutionVector3 position, CaseExecutionQuaternion rotation,
                                            CaseExecutionVector3 linearVelocity, CaseExecutionVector3 angularVelocity,
                                            double mass, double rotationalEnergy, int sleepMatches, CaseExecutionVector3 gravity,
                                            PyramidWallObservation* sample)
{
	for (double value : {static_cast<double>(position.x), static_cast<double>(position.y),
	                     static_cast<double>(position.z), static_cast<double>(rotation.x),
	                     static_cast<double>(rotation.y), static_cast<double>(rotation.z),
	                     static_cast<double>(rotation.w), static_cast<double>(linearVelocity.x),
	                     static_cast<double>(linearVelocity.y), static_cast<double>(linearVelocity.z),
	                     static_cast<double>(angularVelocity.x), static_cast<double>(angularVelocity.y),
	                     static_cast<double>(angularVelocity.z), mass, rotationalEnergy})
	{
		if (!std::isfinite(value))
		{
			++sample->invalidBodies;
			return;
		}
	}
	if (sleepMatches == 0 || mass <= 0.0)
		++sample->invalidBodies;
	const CaseExecutionVector3 initial = PyramidWallPosition(wall, index);
	const double dx = static_cast<double>(position.x) - initial.x;
	const double dz = static_cast<double>(position.z) - initial.z;
	sample->centreOfMassHeight += position.y;
	sample->lateralRms += dx * dx + dz * dz;
	sample->translationalEnergy += 0.5 * mass *
	    (static_cast<double>(linearVelocity.x) * linearVelocity.x +
	     static_cast<double>(linearVelocity.y) * linearVelocity.y +
	     static_cast<double>(linearVelocity.z) * linearVelocity.z);
	sample->rotationalEnergy += rotationalEnergy;
	sample->potentialEnergy -= mass * (static_cast<double>(gravity.x) * position.x +
	    static_cast<double>(gravity.y) * position.y + static_cast<double>(gravity.z) * position.z);
	const double x = rotation.x;
	const double y = rotation.y;
	const double z = rotation.z;
	const double w = rotation.w;
	const double h = wall.halfExtent;
	const double supportX = h * (std::abs(1.0 - 2.0 * (y * y + z * z)) +
	                             std::abs(2.0 * (x * y - z * w)) + std::abs(2.0 * (x * z + y * w)));
	const double supportY = h * (std::abs(2.0 * (x * y + z * w)) +
	                             std::abs(1.0 - 2.0 * (x * x + z * z)) + std::abs(2.0 * (y * z - x * w)));
	const double supportZ = h * (std::abs(2.0 * (x * z - y * w)) +
	                             std::abs(2.0 * (y * z + x * w)) + std::abs(1.0 - 2.0 * (x * x + y * y)));
	sample->floorPenetration = std::max(sample->floorPenetration, supportY - position.y);
	if (std::abs(position.x) + supportX > wall.floorHalfExtents.x ||
	    std::abs(position.z) + supportZ > wall.floorHalfExtents.z)
		++sample->escapedBodies;
}

inline void FinishPyramidWallObservation(std::uint32_t count, PyramidWallObservation* sample)
{
	sample->centreOfMassHeight /= count;
	sample->lateralRms = std::sqrt(sample->lateralRms / count);
}

inline std::uint32_t PyramidWallObservationStep(std::uint32_t measured, std::uint32_t ordinal)
{
	if (measured < 4 || ordinal == 0)
		return ordinal + 1;
	if (ordinal == 3)
		return measured;
	return std::max(ordinal + 1, (measured + 1) / (ordinal == 1 ? 4 : 2));
}

inline int PyramidWallObservationIndex(std::uint32_t completed, std::uint32_t measured)
{
	for (std::uint32_t index = 0; index < std::min(4u, measured); ++index)
		if (completed == PyramidWallObservationStep(measured, index))
			return static_cast<int>(index);
	return -1;
}
