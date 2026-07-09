#include "benchmark_visual/visual_protocol.h"

#include <cmath>

namespace benchmark_visual
{
int ValidateSnapshotCapacity(int transformCount)
{
	if (transformCount < 0 || transformCount > kVisualBridgeMaxTransforms)
	{
		return VisualBridgeStatus_CapacityExceeded;
	}

	return VisualBridgeStatus_Ok;
}

int ValidateSnapshot(VisualSnapshot snapshot)
{
	int capacityStatus = ValidateSnapshotCapacity(snapshot.transformCount);
	if (capacityStatus != VisualBridgeStatus_Ok)
	{
		return capacityStatus;
	}

	if (snapshot.transformCapacity < snapshot.transformCount)
	{
		return VisualBridgeStatus_CapacityExceeded;
	}

	if (snapshot.staticBoxCount < 0 || snapshot.staticBoxCount > kVisualBridgeMaxStaticBoxes)
	{
		return VisualBridgeStatus_CapacityExceeded;
	}

	if (snapshot.transforms == nullptr || snapshot.stepIndex < 0 || snapshot.identity.stepCount < 0)
	{
		return VisualBridgeStatus_InvalidArgument;
	}

	if (snapshot.staticBoxCount > 0 && snapshot.staticBoxes == nullptr)
	{
		return VisualBridgeStatus_InvalidArgument;
	}

	if (snapshot.identity.threadCount <= 0 || snapshot.identity.repeatIndex < 0 || snapshot.identity.bodyCount <= 0 ||
		snapshot.identity.shapeCount <= 0 || snapshot.stepIndex > snapshot.identity.stepCount)
	{
		return VisualBridgeStatus_InvalidArgument;
	}

	if (snapshot.dynamicHalfExtentX <= 0.0f || snapshot.dynamicHalfExtentY <= 0.0f || snapshot.dynamicHalfExtentZ <= 0.0f)
	{
		return VisualBridgeStatus_InvalidArgument;
	}

	for (int index = 0; index < snapshot.transformCount; ++index)
	{
		VisualTransform transform = snapshot.transforms[index];
		if (std::isfinite(transform.positionX) == 0 || std::isfinite(transform.positionY) == 0 ||
			std::isfinite(transform.positionZ) == 0 || std::isfinite(transform.rotationX) == 0 ||
			std::isfinite(transform.rotationY) == 0 || std::isfinite(transform.rotationZ) == 0 ||
			std::isfinite(transform.rotationW) == 0)
		{
			return VisualBridgeStatus_InvalidArgument;
		}
	}

	for (int index = 0; index < snapshot.staticBoxCount; ++index)
	{
		VisualStaticBox box = snapshot.staticBoxes[index];
		if (std::isfinite(box.positionX) == 0 || std::isfinite(box.positionY) == 0 ||
			std::isfinite(box.positionZ) == 0 || std::isfinite(box.halfExtentX) == 0 ||
			std::isfinite(box.halfExtentY) == 0 || std::isfinite(box.halfExtentZ) == 0 ||
			box.halfExtentX <= 0.0f || box.halfExtentY <= 0.0f || box.halfExtentZ <= 0.0f)
		{
			return VisualBridgeStatus_InvalidArgument;
		}
	}

	return VisualBridgeStatus_Ok;
}
}
