#include "benchmark_visual/visual_camera.h"

#include "benchmark_visual/visual_snapshot.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace benchmark_visual
{
namespace
{
constexpr float kRadiansPerDegree = 0.017453292519943295769f;
constexpr float kMinimumFitDistance = 0.05f;
constexpr float kBasisEpsilon = 1.0e-6f;
constexpr float kBoxSigns[8][3] = {
    {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, -1.0f},
    {-1.0f, -1.0f, 1.0f},  {1.0f, -1.0f, 1.0f},  {-1.0f, 1.0f, 1.0f},  {1.0f, 1.0f, 1.0f},
};

VisualCameraVector Add(VisualCameraVector left, VisualCameraVector right)
{
	return {left.x + right.x, left.y + right.y, left.z + right.z};
}

VisualCameraVector Subtract(VisualCameraVector left, VisualCameraVector right)
{
	return {left.x - right.x, left.y - right.y, left.z - right.z};
}

VisualCameraVector Scale(VisualCameraVector value, float scale)
{
	return {value.x * scale, value.y * scale, value.z * scale};
}

float Dot(VisualCameraVector left, VisualCameraVector right)
{
	return left.x * right.x + left.y * right.y + left.z * right.z;
}

VisualCameraVector Cross(VisualCameraVector left, VisualCameraVector right)
{
	return {
	    left.y * right.z - left.z * right.y,
	    left.z * right.x - left.x * right.z,
	    left.x * right.y - left.y * right.x,
	};
}

VisualCameraVector Normalize(VisualCameraVector value)
{
	const float inverseLength = 1.0f / std::sqrt(Dot(value, value));
	return Scale(value, inverseLength);
}

int FiniteVector(VisualCameraVector value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) ? 1 : 0;
}

int ValidBasis(VisualCameraVector direction, VisualCameraVector up)
{
	const float directionLengthSquared = Dot(direction, direction);
	const float upLengthSquared = Dot(up, up);
	const VisualCameraVector cross = Cross(direction, up);
	const float crossLengthSquared = Dot(cross, cross);
	return directionLengthSquared > kBasisEpsilon * kBasisEpsilon && upLengthSquared > kBasisEpsilon * kBasisEpsilon &&
	               crossLengthSquared > directionLengthSquared * upLengthSquared * kBasisEpsilon * kBasisEpsilon
	           ? 1
			   : 0;
}

VisualCameraVector TransformPosition(const VisualTransform& transform)
{
	return {transform.positionX, transform.positionY, transform.positionZ};
}

VisualCameraVector Rotate(const VisualTransform& transform, VisualCameraVector local)
{
	const VisualCameraVector quaternion = {
	    transform.rotationX,
	    transform.rotationY,
	    transform.rotationZ,
	};
	const VisualCameraVector twiceCross = Scale(Cross(quaternion, local), 2.0f);
	return Add(local, Add(Scale(twiceCross, transform.rotationW), Cross(quaternion, twiceCross)));
}

void IncludePoint(VisualCameraVector point, VisualCameraVector* minimum, VisualCameraVector* maximum)
{
	minimum->x = std::min(minimum->x, point.x);
	minimum->y = std::min(minimum->y, point.y);
	minimum->z = std::min(minimum->z, point.z);
	maximum->x = std::max(maximum->x, point.x);
	maximum->y = std::max(maximum->y, point.y);
	maximum->z = std::max(maximum->z, point.z);
}

int ResolveFitCamera(const VisualCameraContext* context, float aspect, ResolvedVisualCamera* resolved)
{
	const VisualCameraVector extent = Subtract(context->maximum, context->minimum);
	const VisualCameraVector halfExtents = Scale(extent, 0.5f);
	const VisualCameraVector target = Scale(Add(context->minimum, context->maximum), 0.5f);
	const float radius = std::sqrt(Dot(extent, extent)) * 0.5f;
	const float verticalHalfFov = context->policy.verticalFovDegrees * kRadiansPerDegree * 0.5f;
	const float horizontalHalfFov = std::atan(std::tan(verticalHalfFov) * aspect);
	const VisualCameraVector direction = Normalize(context->policy.direction);
	const VisualCameraVector forward = Scale(direction, -1.0f);
	const VisualCameraVector right = Normalize(Cross(context->policy.up, forward));
	const VisualCameraVector up = Cross(forward, right);
	const float horizontalLimit = std::tan(horizontalHalfFov) * context->policy.viewportFill;
	const float verticalLimit = std::tan(verticalHalfFov) * context->policy.viewportFill;
	if (!std::isfinite(horizontalLimit) || !std::isfinite(verticalLimit) || horizontalLimit <= 0.0f ||
	    verticalLimit <= 0.0f)
		return VisualCameraStatus_InvalidArgument;
	float distance = kMinimumFitDistance;
	for (const float (&signs)[3] : kBoxSigns)
	{
		const VisualCameraVector corner = {
		    halfExtents.x * signs[0],
		    halfExtents.y * signs[1],
		    halfExtents.z * signs[2],
		};
		const float depthOffset = Dot(forward, corner);
		distance = std::max(distance, std::abs(Dot(right, corner)) / horizontalLimit - depthOffset);
		distance = std::max(distance, std::abs(Dot(up, corner)) / verticalLimit - depthOffset);
	}
	resolved->eye = Add(target, Scale(direction, distance));
	resolved->target = target;
	resolved->up = context->policy.up;
	resolved->verticalFovDegrees = context->policy.verticalFovDegrees;
	resolved->nearPlane = std::max(kMinimumFitDistance, distance - radius * 1.5f);
	resolved->farPlane = distance + radius * 2.5f;
	return FiniteVector(resolved->eye) != 0 && FiniteVector(resolved->target) != 0 &&
	               std::isfinite(resolved->nearPlane) && std::isfinite(resolved->farPlane) &&
	               resolved->nearPlane < resolved->farPlane
	           ? VisualCameraStatus_Ok
			   : VisualCameraStatus_InvalidArgument;
}
} // namespace

int ValidateVisualCameraPolicy(const VisualCameraPolicy* policy)
{
	if (policy == nullptr || FiniteVector(policy->direction) == 0 || FiniteVector(policy->up) == 0 ||
	    FiniteVector(policy->minimum) == 0 || FiniteVector(policy->maximum) == 0 || FiniteVector(policy->eye) == 0 ||
	    FiniteVector(policy->target) == 0 || FiniteVector(policy->eyeOffset) == 0 ||
	    FiniteVector(policy->targetOffset) == 0 || !std::isfinite(policy->verticalFovDegrees) ||
	    !std::isfinite(policy->viewportFill) || !std::isfinite(policy->nearPlane) || !std::isfinite(policy->farPlane) ||
	    policy->verticalFovDegrees <= 0.0f || policy->verticalFovDegrees >= 179.0f)
		return VisualCameraStatus_InvalidArgument;
	if (policy->mode == VisualCameraMode_FitScene || policy->mode == VisualCameraMode_FitBounds)
	{
		if (ValidBasis(policy->direction, policy->up) == 0 || policy->viewportFill <= 0.0f ||
		    policy->viewportFill > 1.0f)
			return VisualCameraStatus_InvalidArgument;
		if (policy->mode == VisualCameraMode_FitBounds &&
		    (policy->minimum.x >= policy->maximum.x || policy->minimum.y >= policy->maximum.y ||
		     policy->minimum.z >= policy->maximum.z))
			return VisualCameraStatus_InvalidArgument;
		return VisualCameraStatus_Ok;
	}
	if (policy->mode != VisualCameraMode_Fixed && policy->mode != VisualCameraMode_FollowStableSlot)
		return VisualCameraStatus_InvalidArgument;
	const VisualCameraVector view = policy->mode == VisualCameraMode_Fixed
	                                    ? Subtract(policy->target, policy->eye)
	                                    : Subtract(policy->targetOffset, policy->eyeOffset);
	return policy->nearPlane > 0.0f && policy->nearPlane < policy->farPlane && ValidBasis(view, policy->up) != 0
	           ? VisualCameraStatus_Ok
			   : VisualCameraStatus_InvalidArgument;
}

int IncludeVisualInstanceBounds(const VisualScene* scene, const VisualInstance& instance,
                                VisualCameraVector* minimum, VisualCameraVector* maximum)
{
	if (instance.geometryIndex >= scene->geometryCount)
		return VisualCameraStatus_InvalidArgument;
	const VisualGeometry& geometry = scene->geometries[instance.geometryIndex];
	const VisualCameraVector position = TransformPosition(instance.initialTransform);
	if (geometry.kind == VisualGeometryKind_Sphere)
	{
		const VisualCameraVector radius = {
		    geometry.parameterX,
		    geometry.parameterX,
		    geometry.parameterX,
		};
		IncludePoint(Subtract(position, radius), minimum, maximum);
		IncludePoint(Add(position, radius), minimum, maximum);
	}
	else if (geometry.kind == VisualGeometryKind_Box)
	{
		for (const float (&signs)[3] : kBoxSigns)
		{
			const VisualCameraVector local = {
			    geometry.parameterX * signs[0],
			    geometry.parameterY * signs[1],
			    geometry.parameterZ * signs[2],
			};
			IncludePoint(Add(position, Rotate(instance.initialTransform, local)), minimum,
			             maximum);
		}
	}
	else if (geometry.kind == VisualGeometryKind_IndexedTriangles)
	{
		if (scene->vertices == nullptr || geometry.vertexCount == 0 ||
		    geometry.vertexOffset > scene->vertexCount ||
		    geometry.vertexCount > scene->vertexCount - geometry.vertexOffset)
			return VisualCameraStatus_InvalidArgument;
		for (uint32_t index = 0; index < geometry.vertexCount; ++index)
		{
			const VisualMeshVertex& vertex = scene->vertices[geometry.vertexOffset + index];
			const VisualCameraVector local = {vertex.x, vertex.y, vertex.z};
			if (FiniteVector(local) == 0)
				return VisualCameraStatus_InvalidArgument;
			IncludePoint(Add(position, Rotate(instance.initialTransform, local)), minimum,
			             maximum);
		}
	}
	else
		return VisualCameraStatus_UnsupportedGeometry;
	return VisualCameraStatus_Ok;
}

int PrepareVisualCamera(const VisualScene* scene, VisualCameraContext* context)
{
	if (scene == nullptr || context == nullptr || scene->geometries == nullptr || scene->instances == nullptr ||
	    ValidateVisualCameraPolicy(&scene->camera) != VisualCameraStatus_Ok)
		return VisualCameraStatus_InvalidArgument;
	*context = {};
	context->policy = scene->camera;
	if (scene->camera.mode == VisualCameraMode_FitBounds)
	{
		context->minimum = scene->camera.minimum;
		context->maximum = scene->camera.maximum;
		const VisualCameraVector extent = Subtract(context->maximum, context->minimum);
		if (FiniteVector(extent) == 0 || extent.x <= 0.0f || extent.y <= 0.0f || extent.z <= 0.0f)
			return VisualCameraStatus_InvalidArgument;
	}
	else if (scene->camera.mode == VisualCameraMode_FitScene)
	{
		context->minimum = {
		    std::numeric_limits<float>::max(),
		    std::numeric_limits<float>::max(),
		    std::numeric_limits<float>::max(),
		};
		context->maximum = {
		    -std::numeric_limits<float>::max(),
		    -std::numeric_limits<float>::max(),
		    -std::numeric_limits<float>::max(),
		};
		if (scene->instanceCount == 0)
			return VisualCameraStatus_InvalidArgument;
		for (std::uint32_t instanceIndex = 0; instanceIndex < scene->instanceCount; ++instanceIndex)
		{
			const VisualInstance& instance = scene->instances[instanceIndex];
			const int status = IncludeVisualInstanceBounds(scene, instance, &context->minimum, &context->maximum);
			if (status != VisualCameraStatus_Ok)
				return status;
		}
		const VisualCameraVector extent = Subtract(context->maximum, context->minimum);
		if (FiniteVector(extent) == 0 || extent.x <= 0.0f || extent.y <= 0.0f || extent.z <= 0.0f)
			return VisualCameraStatus_InvalidArgument;
	}
	else if (scene->camera.mode == VisualCameraMode_FollowStableSlot)
	{
		std::uint32_t matches = 0;
		for (std::uint32_t instanceIndex = 0; instanceIndex < scene->instanceCount; ++instanceIndex)
		{
			const VisualInstance& instance = scene->instances[instanceIndex];
			if (instance.stableSlot != scene->camera.stableSlot || instance.transformSlot == UINT32_MAX)
				continue;
			if (instance.transformSlot >= scene->dynamicTransformCount)
				return VisualCameraStatus_FollowTargetMissing;
			context->followTransformSlot = instance.transformSlot;
			context->followInitialPosition = TransformPosition(instance.initialTransform);
			matches += 1;
		}
		if (matches != 1)
			return VisualCameraStatus_FollowTargetMissing;
	}
	context->preparationStatus = VisualCameraPreparationStatus_Ready;
	return VisualCameraStatus_Ok;
}

int ResolveVisualCamera(const VisualCameraContext* context, float aspect, const VisualStableTransform* transforms,
                        std::uint32_t transformCount, ResolvedVisualCamera* resolved)
{
	if (context == nullptr || resolved == nullptr ||
	    context->preparationStatus != VisualCameraPreparationStatus_Ready || !std::isfinite(aspect) || aspect <= 0.0f ||
	    (transforms == nullptr && transformCount != 0))
		return VisualCameraStatus_InvalidArgument;
	*resolved = {};
	if (context->policy.mode == VisualCameraMode_FitScene || context->policy.mode == VisualCameraMode_FitBounds)
		return ResolveFitCamera(context, aspect, resolved);
	if (context->policy.mode == VisualCameraMode_Fixed)
	{
		resolved->eye = context->policy.eye;
		resolved->target = context->policy.target;
		resolved->up = context->policy.up;
		resolved->verticalFovDegrees = context->policy.verticalFovDegrees;
		resolved->nearPlane = context->policy.nearPlane;
		resolved->farPlane = context->policy.farPlane;
		return VisualCameraStatus_Ok;
	}
	if (context->policy.mode != VisualCameraMode_FollowStableSlot)
		return VisualCameraStatus_InvalidArgument;
	VisualCameraVector position = context->followInitialPosition;
	if (transformCount != 0)
	{
		if (context->followTransformSlot >= transformCount ||
		    transforms[context->followTransformSlot].stableSlot != context->policy.stableSlot)
			return VisualCameraStatus_FollowTargetMissing;
		position = TransformPosition(transforms[context->followTransformSlot].transform);
	}
	resolved->eye = Add(position, context->policy.eyeOffset);
	resolved->target = Add(position, context->policy.targetOffset);
	resolved->up = context->policy.up;
	resolved->verticalFovDegrees = context->policy.verticalFovDegrees;
	resolved->nearPlane = context->policy.nearPlane;
	resolved->farPlane = context->policy.farPlane;
	return VisualCameraStatus_Ok;
}
} // namespace benchmark_visual
