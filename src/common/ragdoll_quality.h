#pragma once

#include "case_execution_wire.h"
#include "benchmark_visual/visual_snapshot.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

constexpr std::uint32_t kRagdollQualityObservationCount = 10;

struct RagdollQualityAccumulator
{
	double squaredGapSum;
	double maximumGap;
	std::uint64_t jointSampleCount;
	std::uint64_t bodySampleCount;
	std::uint64_t invalidBodySampleCount;
	std::uint64_t missingBodySampleCount;
	std::uint32_t worstJoint;
	std::uint32_t worstStep;
	std::uint32_t firstInvalidBody;
	std::uint32_t firstInvalidStep;
};

struct RagdollQualityObservation
{
	const char* id;
	std::uint64_t bits;
	std::uint32_t valueType;
};

inline int RagdollQualityPoseValid(const benchmark_visual::VisualTransform& pose)
{
	return std::isfinite(pose.positionX) && std::isfinite(pose.positionY) && std::isfinite(pose.positionZ) &&
	               std::isfinite(pose.rotationX) && std::isfinite(pose.rotationY) && std::isfinite(pose.rotationZ) &&
	               std::isfinite(pose.rotationW) &&
	               (pose.rotationX != 0.0f || pose.rotationY != 0.0f || pose.rotationZ != 0.0f ||
	                pose.rotationW != 0.0f)
	           ? 1
			   : 0;
}

inline void RagdollQualityAnchor(const benchmark_visual::VisualTransform& pose, const CaseExecutionVector3& local,
                                 double* world)
{
	const double tx =
	    2.0 * (pose.rotationY * static_cast<double>(local.z) - pose.rotationZ * static_cast<double>(local.y));
	const double ty =
	    2.0 * (pose.rotationZ * static_cast<double>(local.x) - pose.rotationX * static_cast<double>(local.z));
	const double tz =
	    2.0 * (pose.rotationX * static_cast<double>(local.y) - pose.rotationY * static_cast<double>(local.x));
	world[0] =
	    pose.positionX + static_cast<double>(local.x) + pose.rotationW * tx + pose.rotationY * tz - pose.rotationZ * ty;
	world[1] =
	    pose.positionY + static_cast<double>(local.y) + pose.rotationW * ty + pose.rotationZ * tx - pose.rotationX * tz;
	world[2] =
	    pose.positionZ + static_cast<double>(local.z) + pose.rotationW * tz + pose.rotationX * ty - pose.rotationY * tx;
}

inline void AccumulateRagdollQuality(const CaseExecutionSpec& execution,
                                     const benchmark_visual::VisualStableTransform* poses, std::uint32_t poseCount,
                                     std::uint32_t step, RagdollQualityAccumulator* quality)
{
	for (std::uint32_t body = 0; body < execution.dynamicBodyCount; ++body)
	{
		if (body >= poseCount || poses[body].stableSlot != body)
		{
			++quality->missingBodySampleCount;
		}
		else
		{
			++quality->bodySampleCount;
			if (RagdollQualityPoseValid(poses[body].transform) != 0)
				continue;
			++quality->invalidBodySampleCount;
		}
		if (quality->firstInvalidStep == 0)
		{
			quality->firstInvalidBody = body;
			quality->firstInvalidStep = step;
		}
	}
	const CaseExecutionRagdoll& fixture = execution.ragdoll;
	const std::uint32_t ragdollCount = fixture.ragdollGrid[0] * fixture.ragdollGrid[1];
	for (std::uint32_t ragdoll = 0; ragdoll < ragdollCount; ++ragdoll)
	{
		for (std::uint32_t joint = 0; joint < fixture.linkCount; ++joint)
		{
			const CaseExecutionRagdollLink& link = fixture.links[joint];
			const std::uint32_t parent = ragdoll * fixture.partCount + link.parentPart;
			const std::uint32_t child = ragdoll * fixture.partCount + link.childPart;
			if (parent >= poseCount || child >= poseCount || poses[parent].stableSlot != parent ||
			    poses[child].stableSlot != child || RagdollQualityPoseValid(poses[parent].transform) == 0 ||
			    RagdollQualityPoseValid(poses[child].transform) == 0)
				continue;
			double anchorA[3];
			double anchorB[3];
			RagdollQualityAnchor(poses[parent].transform, link.parentLocalAnchor, anchorA);
			RagdollQualityAnchor(poses[child].transform, link.childLocalAnchor, anchorB);
			const double dx = anchorA[0] - anchorB[0];
			const double dy = anchorA[1] - anchorB[1];
			const double dz = anchorA[2] - anchorB[2];
			const double square = dx * dx + dy * dy + dz * dz;
			const double gap = std::sqrt(square);
			quality->squaredGapSum += square;
			++quality->jointSampleCount;
			if (quality->worstStep == 0 || gap > quality->maximumGap)
			{
				quality->maximumGap = gap;
				quality->worstJoint = ragdoll * fixture.linkCount + joint;
				quality->worstStep = step;
			}
		}
	}
}

inline RagdollQualityObservation RagdollQualityValue(const RagdollQualityAccumulator& quality, std::uint32_t ordinal)
{
	constexpr const char* ids[kRagdollQualityObservationCount] = {
	    "joint_anchor_gap_rms_m",    "joint_anchor_gap_max_m",    "worst_joint_id",
	    "worst_joint_step",          "joint_sample_count",        "body_sample_count",
	    "invalid_body_sample_count", "missing_body_sample_count", "first_invalid_body_id",
	    "first_invalid_step",
	};
	RagdollQualityObservation row = {ids[ordinal], 0, ordinal < 2 ? 2u : 1u};
	if (ordinal < 2)
	{
		const double value = ordinal == 0
		                         ? std::sqrt(quality.squaredGapSum / static_cast<double>(quality.jointSampleCount))
		                         : quality.maximumGap;
		std::memcpy(&row.bits, &value, sizeof(value));
	}
	else
	{
		const std::uint64_t values[8] = {
		    quality.worstJoint,
		    quality.worstStep,
		    quality.jointSampleCount,
		    quality.bodySampleCount,
		    quality.invalidBodySampleCount,
		    quality.missingBodySampleCount,
		    quality.firstInvalidBody,
		    quality.firstInvalidStep,
		};
		row.bits = values[ordinal - 2];
	}
	return row;
}
