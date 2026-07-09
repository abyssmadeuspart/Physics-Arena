#include "physx34_result_writer.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace physx34_benchmark
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

int WritePhysXResultRow(const PhysXRunRequest& request, const PhysXCaseDescriptor& descriptor, const PhysXCaseState& state)
{
	FILE* existing = std::fopen(request.outputPath, "r");
	int writeHeader = existing == nullptr ? 1 : 0;
	if (existing != nullptr)
	{
		std::fclose(existing);
	}

	FILE* file = std::fopen(request.outputPath, "a");
	if (file == nullptr)
	{
		std::fprintf(stderr, "result_failed reason=open_output path=%s\n", request.outputPath);
		return 2;
	}
	if (writeHeader != 0)
	{
		std::fprintf(file, "%s", kCsvHeader);
	}

	std::vector<PhysXTransform> transforms(descriptor.dynamicBodyCount);
	if (descriptor.SampleTransforms(state, transforms.data(), descriptor.dynamicBodyCount) != 0)
	{
		std::fclose(file);
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
		std::fprintf(stderr,
			"invalid_result reason=metric elapsed_ms=%.9f step_count=%d completed_step_count=%d\n",
			state.physicsElapsedMs,
			request.stepCount,
			state.completedStepCount);
		std::fclose(file);
		return 2;
	}
	if (std::strcmp(metricStatus, "ok") != 0)
	{
		std::fprintf(stderr, "invalid_result reason=case_status status=%s\n", caseStatus);
		std::fclose(file);
		return 2;
	}
	std::fprintf(file,
		"%d,%d,%d,%d,%d,%s,%s,%d,%.9f\n",
		descriptor.bodyCount,
		descriptor.bodyCount,
		invalidTransformCount,
		belowFloorCount,
		outOfBoundsCount,
		caseStatus,
		metricStatus,
		state.completedStepCount,
		state.physicsElapsedMs);
	std::fclose(file);
	return 0;
}
}
