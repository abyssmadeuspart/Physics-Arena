#include "jolt_result_writer.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

namespace jolt_benchmark
{
constexpr const char* kCsvHeader =
	"body_count,shape_count,invalid_transform_count,below_floor_count,out_of_bounds_count,case_status,metric_status,completed_step_count,physics_elapsed_ms\n";

const char* CaseStatus(int invalidTransformCount, int belowFloorCount, int outOfBoundsCount)
{
	(void)outOfBoundsCount;
	return invalidTransformCount == 0 && belowFloorCount == 0 ? "ok" : "invalid_stability_counters";
}

const char* MetricStatus(const char* caseStatus)
{
	return std::strcmp(caseStatus, "ok") == 0 ? "ok" : "invalid_result";
}

int WriteJoltResultRow(const JoltRunRequest& request, const JoltCaseDescriptor& descriptor, const JoltCaseState& state)
{
	std::ifstream existing(request.outputPath);
	std::ofstream file(request.outputPath, std::ios::app);
	if (!file)
	{
		std::cerr << "result_failed reason=open_output path=" << request.outputPath << '\n';
		return 2;
	}
	if (!existing.good())
	{
		file << kCsvHeader;
	}

	std::vector<JoltTransform> transforms(descriptor.dynamicBodyCount);
	if (descriptor.SampleTransforms(state, transforms.data(), descriptor.dynamicBodyCount) != 0)
	{
		return 2;
	}
	int invalidTransformCount = 0;
	int belowFloorCount = 0;
	int outOfBoundsCount = 0;
	for (int index = 0; index < descriptor.dynamicBodyCount; ++index)
	{
		double px = static_cast<double>(transforms[index].positionX);
		double py = static_cast<double>(transforms[index].positionY);
		double pz = static_cast<double>(transforms[index].positionZ);
		double rx = static_cast<double>(transforms[index].rotationX);
		double ry = static_cast<double>(transforms[index].rotationY);
		double rz = static_cast<double>(transforms[index].rotationZ);
		double rw = static_cast<double>(transforms[index].rotationW);
		if (std::isfinite(px) == 0 || std::isfinite(py) == 0 || std::isfinite(pz) == 0 ||
			std::isfinite(rx) == 0 || std::isfinite(ry) == 0 || std::isfinite(rz) == 0 ||
			std::isfinite(rw) == 0)
		{
			invalidTransformCount += 1;
		}
		if (py < 0.0)
		{
			belowFloorCount += 1;
		}
		if (px < -descriptor.lateralEscapeLimit || px > descriptor.lateralEscapeLimit ||
			pz < -descriptor.lateralEscapeLimit || pz > descriptor.lateralEscapeLimit ||
			py < 0.0 || py > descriptor.maxY)
		{
			outOfBoundsCount += 1;
		}
	}

	double msPerStep = state.physicsElapsedMs / static_cast<double>(request.stepCount);
	double stepsPerSecond = 1000.0 / msPerStep;
	const char* caseStatus = CaseStatus(invalidTransformCount, belowFloorCount, outOfBoundsCount);
	const char* metricStatus = MetricStatus(caseStatus);
	if (state.physicsElapsedMs <= 0.0 || std::isfinite(state.physicsElapsedMs) == 0 ||
		msPerStep <= 0.0 || std::isfinite(msPerStep) == 0 ||
		stepsPerSecond <= 0.0 || std::isfinite(stepsPerSecond) == 0 ||
		state.completedStepCount != request.stepCount)
	{
		std::cerr << "invalid_result reason=metric elapsed_ms=" << state.physicsElapsedMs
				  << " step_count=" << request.stepCount
				  << " completed_step_count=" << state.completedStepCount << '\n';
		return 2;
	}
	if (std::strcmp(metricStatus, "ok") != 0)
	{
		std::cerr << "invalid_result reason=case_status status=" << caseStatus << '\n';
		return 2;
	}
	file << std::fixed << std::setprecision(9)
		 << descriptor.bodyCount << ',' << descriptor.bodyCount << ','
		 << invalidTransformCount << ',' << belowFloorCount << ',' << outOfBoundsCount << ',' << caseStatus << ','
		 << metricStatus << ',' << state.completedStepCount << ',' << state.physicsElapsedMs << '\n';
	return 0;
}
}
