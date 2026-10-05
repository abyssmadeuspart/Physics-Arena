#pragma once

#include "Chaos/Core.h"

namespace BenchmarkPolygonChaos
{

inline constexpr Chaos::FReal kChaosUnitsPerBenchmarkMeter = 100.0;

inline constexpr Chaos::FReal MetersToChaosUnits(Chaos::FReal Meters)
{
	return Meters * kChaosUnitsPerBenchmarkMeter;
}

inline constexpr Chaos::FReal ChaosUnitsToMeters(Chaos::FReal ChaosUnits)
{
	return ChaosUnits / kChaosUnitsPerBenchmarkMeter;
}

inline Chaos::FVec3 MetersToChaosUnits(const Chaos::FVec3& Meters)
{
	return Chaos::FVec3(MetersToChaosUnits(Meters.X), MetersToChaosUnits(Meters.Y), MetersToChaosUnits(Meters.Z));
}

inline Chaos::FVec3 ChaosUnitsToMeters(const Chaos::FVec3& ChaosUnits)
{
	return Chaos::FVec3(ChaosUnitsToMeters(ChaosUnits.X), ChaosUnitsToMeters(ChaosUnits.Y),
	                    ChaosUnitsToMeters(ChaosUnits.Z));
}

static_assert(ChaosUnitsToMeters(MetersToChaosUnits(1.0)) == 1.0);

} // namespace BenchmarkPolygonChaos
