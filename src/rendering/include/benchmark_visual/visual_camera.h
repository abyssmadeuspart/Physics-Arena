#pragma once

#include <cstdint>

namespace benchmark_visual
{
struct VisualScene;
struct VisualInstance;
struct VisualStableTransform;

enum VisualCameraMode : std::uint32_t
{
	VisualCameraMode_Unknown = 0,
	VisualCameraMode_FitScene = 1,
	VisualCameraMode_FitBounds = 2,
	VisualCameraMode_Fixed = 3,
	VisualCameraMode_FollowStableSlot = 4,
};

enum VisualCameraStatus
{
	VisualCameraStatus_Ok = 0,
	VisualCameraStatus_InvalidArgument = 1,
	VisualCameraStatus_UnsupportedGeometry = 2,
	VisualCameraStatus_FollowTargetMissing = 3,
};

enum VisualCameraPreparationStatus
{
	VisualCameraPreparationStatus_Unprepared = 0,
	VisualCameraPreparationStatus_Ready = 1,
};

struct VisualCameraVector
{
	float x;
	float y;
	float z;
};

struct VisualCameraPolicy
{
	VisualCameraVector direction;
	VisualCameraVector up;
	VisualCameraVector minimum;
	VisualCameraVector maximum;
	VisualCameraVector eye;
	VisualCameraVector target;
	VisualCameraVector eyeOffset;
	VisualCameraVector targetOffset;
	float verticalFovDegrees;
	float viewportFill;
	float nearPlane;
	float farPlane;
	std::uint32_t stableSlot;
	VisualCameraMode mode;
};

struct VisualCameraContext
{
	VisualCameraPolicy policy;
	VisualCameraVector minimum;
	VisualCameraVector maximum;
	VisualCameraVector followInitialPosition;
	std::uint32_t followTransformSlot;
	VisualCameraPreparationStatus preparationStatus;
};

struct ResolvedVisualCamera
{
	VisualCameraVector eye;
	VisualCameraVector target;
	VisualCameraVector up;
	float verticalFovDegrees;
	float nearPlane;
	float farPlane;
};

int ValidateVisualCameraPolicy(const VisualCameraPolicy* policy);
int IncludeVisualInstanceBounds(const VisualScene* scene, const VisualInstance& instance,
                                VisualCameraVector* minimum, VisualCameraVector* maximum);
int PrepareVisualCamera(const VisualScene* scene, VisualCameraContext* context);
int ResolveVisualCamera(const VisualCameraContext* context, float aspect, const VisualStableTransform* transforms,
                        std::uint32_t transformCount, ResolvedVisualCamera* resolved);
}
