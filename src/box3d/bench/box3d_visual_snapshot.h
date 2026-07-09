#pragma once

#include "box3d_case_registry.h"
#include "benchmark_visual/visual_transport.h"

#include <cstdint>
#include <vector>

namespace box3d_benchmark
{
uint32_t BuildBox3DVisualSnapshotPayload(
	const Box3DCaseDescriptor& descriptor,
	const Box3DCaseState& state,
	Box3DTransform* caseTransforms,
	Box3DStaticBox* caseBoxes,
	std::vector<uint8_t>& payload);
}
