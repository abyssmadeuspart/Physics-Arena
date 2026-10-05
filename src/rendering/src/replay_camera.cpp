#include "benchmark_visual/replay_camera.h"
#include "benchmark_visual/visual_snapshot.h"
#include "case_execution_wire.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace benchmark_visual
{
namespace
{
void IncludeBounds(VisualCameraVector minimum, VisualCameraVector maximum,
                   VisualCameraVector* combinedMinimum, VisualCameraVector* combinedMaximum)
{
	combinedMinimum->x = std::min(combinedMinimum->x, minimum.x);
	combinedMinimum->y = std::min(combinedMinimum->y, minimum.y);
	combinedMinimum->z = std::min(combinedMinimum->z, minimum.z);
	combinedMaximum->x = std::max(combinedMaximum->x, maximum.x);
	combinedMaximum->y = std::max(combinedMaximum->y, maximum.y);
	combinedMaximum->z = std::max(combinedMaximum->z, maximum.z);
}
}

int PrepareReplayCamera(const VisualScene& scene, const CaseExecutionSpec* saved, ReplayCameraContext* context)
{
	if (ValidateVisualCameraPolicy(&scene.camera) != VisualCameraStatus_Ok || scene.instanceCount == 0)
		return VisualCameraStatus_InvalidArgument;
	*context = {};
	const float largest = std::numeric_limits<float>::max();
	context->sceneMinimum = context->framing.minimum = {largest, largest, largest};
	context->sceneMaximum = context->framing.maximum = {-largest, -largest, -largest};
	context->objectScale = largest;
	for (std::uint32_t index = 0; index < scene.instanceCount; ++index)
	{
		const VisualInstance& instance = scene.instances[index];
		VisualCameraVector minimum = {largest, largest, largest};
		VisualCameraVector maximum = {-largest, -largest, -largest};
		const int status = IncludeVisualInstanceBounds(&scene, instance, &minimum, &maximum);
		if (status != VisualCameraStatus_Ok)
			return status;
		IncludeBounds(minimum, maximum, &context->sceneMinimum, &context->sceneMaximum);
		// the pyramid fixtures each own exactly one static support floor
		if (saved != nullptr && (saved->fixtureKind == CaseFixtureKind_LargePyramid ||
		    saved->fixtureKind == CaseFixtureKind_PyramidWall) && instance.transformSlot == UINT32_MAX)
			continue;
		IncludeBounds(minimum, maximum, &context->framing.minimum, &context->framing.maximum);
		context->objectScale = std::min(context->objectScale,
		    std::min({maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z}));
	}
	if (saved != nullptr && (saved->fixtureKind == CaseFixtureKind_LargePyramid || saved->fixtureKind == CaseFixtureKind_PyramidWall))
		context->framing.minimum.y = std::min(context->framing.minimum.y, 0.0f);
	if (saved != nullptr && saved->fixtureKind == CaseFixtureKind_SpatialQueryTrace)
	{
		const CaseExecutionSpatialQuery& query = saved->spatialQuery;
		const float queryRadius = std::max({query.sphereCastRadius, query.overlapHalfExtents.x,
		    query.overlapHalfExtents.y, query.overlapHalfExtents.z});
		const VisualCameraVector minimum = context->framing.minimum;
		const VisualCameraVector maximum = context->framing.maximum;
		// miss rays continue for the saved query distance beyond the opposite face
		context->framing.minimum = {
		    std::min(minimum.x - 5.0f, maximum.x + 5.0f - query.queryDistance) - queryRadius,
		    std::min(minimum.y - 5.0f, maximum.y + 5.0f - query.queryDistance) - queryRadius,
		    std::min(minimum.z - 5.0f, maximum.z + 5.0f - query.queryDistance) - queryRadius};
		context->framing.maximum = {
		    std::max({maximum.x + 5.0f, minimum.x - 5.0f + query.queryDistance, maximum.x + query.missOffset}) + queryRadius,
		    std::max({maximum.y + 5.0f, minimum.y - 5.0f + query.queryDistance, maximum.y + query.missOffset}) + queryRadius,
		    std::max({maximum.z + 5.0f, minimum.z - 5.0f + query.queryDistance, maximum.z + query.missOffset}) + queryRadius};
		IncludeBounds(context->framing.minimum, context->framing.maximum, &context->sceneMinimum, &context->sceneMaximum);
	}
	VisualCameraPolicy policy = scene.camera;
	if (saved == nullptr && policy.mode == VisualCameraMode_FitBounds)
	{
		context->framing.minimum = policy.minimum;
		context->framing.maximum = policy.maximum;
	}
	if (policy.mode == VisualCameraMode_Fixed)
		policy.direction = {policy.eye.x - policy.target.x, policy.eye.y - policy.target.y, policy.eye.z - policy.target.z};
	else if (policy.mode == VisualCameraMode_FollowStableSlot)
		policy.direction = {policy.eyeOffset.x - policy.targetOffset.x, policy.eyeOffset.y - policy.targetOffset.y,
		    policy.eyeOffset.z - policy.targetOffset.z};
	if (policy.mode == VisualCameraMode_Fixed || policy.mode == VisualCameraMode_FollowStableSlot)
		policy.viewportFill = 0.9f;
	policy.mode = VisualCameraMode_FitBounds;
	policy.minimum = context->framing.minimum;
	policy.maximum = context->framing.maximum;
	if (ValidateVisualCameraPolicy(&policy) != VisualCameraStatus_Ok || context->objectScale <= 0.0f)
		return VisualCameraStatus_InvalidArgument;
	context->framing.policy = policy;
	context->framing.preparationStatus = VisualCameraPreparationStatus_Ready;
	return VisualCameraStatus_Ok;
}

int ComposeReplayCamera(const ReplayCameraContext& context, float aspect,
                        NativeReplayCameraPreferences* controls, ResolvedVisualCamera* camera)
{
	const int status = ResolveVisualCamera(&context.framing, aspect, nullptr, 0, camera);
	if (status != VisualCameraStatus_Ok)
		return status;
	const float x = camera->eye.x - camera->target.x;
	const float y = camera->eye.y - camera->target.y;
	const float z = camera->eye.z - camera->target.z;
	const float distance = std::sqrt(x * x + y * y + z * z);
	const float yaw = std::atan2(x, z) + controls->yawRadians;
	const float basePitch = std::asin(std::clamp(y / distance, -1.0f, 1.0f));
	const float pitch = std::clamp(basePitch + controls->pitchRadians, -1.55f, 1.55f);
	controls->pitchRadians = pitch - basePitch;
	controls->distanceScale = std::clamp(controls->distanceScale, context.objectScale * 0.02f / distance, 100.0f);
	camera->target.x += controls->pan[0];
	camera->target.y += controls->pan[1];
	camera->target.z += controls->pan[2];
	const float radius = distance * controls->distanceScale;
	camera->eye = {camera->target.x + radius * std::sin(yaw) * std::cos(pitch),
	    camera->target.y + radius * std::sin(pitch), camera->target.z + radius * std::cos(yaw) * std::cos(pitch)};
	// retain depth precision at scene scale while allowing object-scale inspection
	camera->nearPlane = std::max(std::min(0.01f, context.objectScale * 0.005f), radius * 0.01f);
	const float farX = std::max(std::abs(context.sceneMinimum.x - camera->eye.x), std::abs(context.sceneMaximum.x - camera->eye.x));
	const float farY = std::max(std::abs(context.sceneMinimum.y - camera->eye.y), std::abs(context.sceneMaximum.y - camera->eye.y));
	const float farZ = std::max(std::abs(context.sceneMinimum.z - camera->eye.z), std::abs(context.sceneMaximum.z - camera->eye.z));
	camera->farPlane = std::sqrt(farX * farX + farY * farY + farZ * farZ) + context.objectScale;
	return VisualCameraStatus_Ok;
}
}
