#include "benchmark_visual/visual_renderer.h"

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>

#include <cmath>
#include <cstdint>

#include "vs_debugdraw_fill_lit.bin.h"
#include "fs_debugdraw_fill_lit.bin.h"

namespace benchmark_visual
{
struct SceneVertex
{
	float x;
	float y;
	float z;
	uint8_t indices[4];
};

struct SceneVec3
{
	float x;
	float y;
	float z;
};

constexpr int kCubeVertexCount = 8;
constexpr int kCubeIndexCount = 36;
constexpr int kSceneBoxesPerChunk = 1024;
constexpr int kViewId = 0;

static const float kCubeSigns[kCubeVertexCount][3] =
{
	{ -1.0f, -1.0f, -1.0f },
	{  1.0f, -1.0f, -1.0f },
	{ -1.0f,  1.0f, -1.0f },
	{  1.0f,  1.0f, -1.0f },
	{ -1.0f, -1.0f,  1.0f },
	{  1.0f, -1.0f,  1.0f },
	{ -1.0f,  1.0f,  1.0f },
	{  1.0f,  1.0f,  1.0f },
};

static const uint16_t kCubeIndices[kCubeIndexCount] =
{
	0, 2, 1,
	1, 2, 3,
	4, 5, 6,
	5, 7, 6,
	0, 1, 4,
	1, 5, 4,
	2, 6, 3,
	3, 6, 7,
	0, 4, 2,
	2, 4, 6,
	1, 3, 5,
	3, 7, 5,
};

static const bgfx::EmbeddedShader kSceneShaders[] =
{
	BGFX_EMBEDDED_SHADER(vs_debugdraw_fill_lit),
	BGFX_EMBEDDED_SHADER(fs_debugdraw_fill_lit),
	BGFX_EMBEDDED_SHADER_END()
};

static bgfx::VertexLayout s_sceneVertexLayout;
static bgfx::ProgramHandle s_sceneProgram = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle s_sceneParams = BGFX_INVALID_HANDLE;
static int s_sceneReady = 0;

SceneVec3 Subtract(SceneVec3 left, SceneVec3 right)
{
	return { left.x - right.x, left.y - right.y, left.z - right.z };
}

SceneVec3 Cross(SceneVec3 left, SceneVec3 right)
{
	return {
		left.y * right.z - left.z * right.y,
		left.z * right.x - left.x * right.z,
		left.x * right.y - left.y * right.x,
	};
}

float Dot(SceneVec3 left, SceneVec3 right)
{
	return left.x * right.x + left.y * right.y + left.z * right.z;
}

SceneVec3 Normalize(SceneVec3 value)
{
	float length = std::sqrt(Dot(value, value));
	if (length <= 0.000001f)
	{
		return { 0.0f, 1.0f, 0.0f };
	}

	float inverseLength = 1.0f / length;
	return { value.x * inverseLength, value.y * inverseLength, value.z * inverseLength };
}

void BuildLookAt(float* result, SceneVec3 eye, SceneVec3 target)
{
	SceneVec3 upSeed = { 0.0f, 1.0f, 0.0f };
	SceneVec3 forward = Normalize(Subtract(target, eye));
	SceneVec3 right = Normalize(Cross(upSeed, forward));
	SceneVec3 up = Cross(forward, right);

	result[0] = right.x;
	result[1] = up.x;
	result[2] = forward.x;
	result[3] = 0.0f;
	result[4] = right.y;
	result[5] = up.y;
	result[6] = forward.y;
	result[7] = 0.0f;
	result[8] = right.z;
	result[9] = up.z;
	result[10] = forward.z;
	result[11] = 0.0f;
	result[12] = -Dot(right, eye);
	result[13] = -Dot(up, eye);
	result[14] = -Dot(forward, eye);
	result[15] = 1.0f;
}

void BuildPerspective(float* result, float fovYDegrees, float aspect, float nearPlane, float farPlane, int homogeneousNdc)
{
	for (int index = 0; index < 16; ++index)
	{
		result[index] = 0.0f;
	}

	float radians = fovYDegrees * 0.017453292519943295769f;
	float height = 1.0f / std::tan(radians * 0.5f);
	float width = height / aspect;
	float range = farPlane - nearPlane;
	float aa = homogeneousNdc != 0 ? (farPlane + nearPlane) / range : farPlane / range;
	float bb = homogeneousNdc != 0 ? (2.0f * farPlane * nearPlane) / range : nearPlane * aa;

	result[0] = width;
	result[5] = height;
	result[10] = aa;
	result[11] = 1.0f;
	result[14] = -bb;
}

void AppendIndexBlock(uint16_t* target, int boxIndex)
{
	uint16_t vertexOffset = static_cast<uint16_t>(boxIndex * kCubeVertexCount);
	int indexOffset = boxIndex * kCubeIndexCount;
	for (int index = 0; index < kCubeIndexCount; ++index)
	{
		target[indexOffset + index] = static_cast<uint16_t>(vertexOffset + kCubeIndices[index]);
	}
}

void RotateOffset(VisualTransform transform, float localX, float localY, float localZ, float* outX, float* outY, float* outZ)
{
	float qx = transform.rotationX;
	float qy = transform.rotationY;
	float qz = transform.rotationZ;
	float qw = transform.rotationW;

	float tx = 2.0f * (qy * localZ - qz * localY);
	float ty = 2.0f * (qz * localX - qx * localZ);
	float tz = 2.0f * (qx * localY - qy * localX);

	*outX = localX + qw * tx + (qy * tz - qz * ty);
	*outY = localY + qw * ty + (qz * tx - qx * tz);
	*outZ = localZ + qw * tz + (qx * ty - qy * tx);
}

void AppendDynamicBox(SceneVertex* target, int boxIndex, VisualTransform transform, float hx, float hy, float hz)
{
	int vertexOffset = boxIndex * kCubeVertexCount;
	for (int index = 0; index < kCubeVertexCount; ++index)
	{
		float offsetX = kCubeSigns[index][0] * hx;
		float offsetY = kCubeSigns[index][1] * hy;
		float offsetZ = kCubeSigns[index][2] * hz;
		float rotatedX = 0.0f;
		float rotatedY = 0.0f;
		float rotatedZ = 0.0f;
		RotateOffset(transform, offsetX, offsetY, offsetZ, &rotatedX, &rotatedY, &rotatedZ);

		SceneVertex* vertex = &target[vertexOffset + index];
		vertex->x = transform.positionX + rotatedX;
		vertex->y = transform.positionY + rotatedY;
		vertex->z = transform.positionZ + rotatedZ;
		vertex->indices[0] = 0;
		vertex->indices[1] = 0;
		vertex->indices[2] = 0;
		vertex->indices[3] = 0;
	}
}

void AppendStaticBox(SceneVertex* target, int boxIndex, VisualStaticBox box)
{
	int vertexOffset = boxIndex * kCubeVertexCount;
	for (int index = 0; index < kCubeVertexCount; ++index)
	{
		SceneVertex* vertex = &target[vertexOffset + index];
		vertex->x = box.positionX + kCubeSigns[index][0] * box.halfExtentX;
		vertex->y = box.positionY + kCubeSigns[index][1] * box.halfExtentY;
		vertex->z = box.positionZ + kCubeSigns[index][2] * box.halfExtentZ;
		vertex->indices[0] = 0;
		vertex->indices[1] = 0;
		vertex->indices[2] = 0;
		vertex->indices[3] = 0;
	}
}

void SubmitBatch(int boxCount, uint64_t state, const float* color)
{
	float params[16] =
	{
		-0.35f, 0.75f, -0.55f, 16.0f,
		1.10f, 1.06f, 0.96f, 1.0f,
		0.30f, 0.36f, 0.42f, 1.0f,
		color[0], color[1], color[2], color[3],
	};

	static const float kIdentity[16] =
	{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f,
	};

	bgfx::setTransform(kIdentity);
	bgfx::setUniform(s_sceneParams, params, 4);
	bgfx::setState(state);
	bgfx::submit(kViewId, s_sceneProgram);
}

int SubmitDynamicBoxes(VisualSnapshot snapshot, uint64_t state, const float* color)
{
	int submitted = 0;
	while (submitted < snapshot.transformCount)
	{
		int remaining = snapshot.transformCount - submitted;
		int chunkCount = remaining < kSceneBoxesPerChunk ? remaining : kSceneBoxesPerChunk;
		uint32_t vertexCount = static_cast<uint32_t>(chunkCount * kCubeVertexCount);
		uint32_t indexCount = static_cast<uint32_t>(chunkCount * kCubeIndexCount);

		if (bgfx::getAvailTransientVertexBuffer(vertexCount, s_sceneVertexLayout) < vertexCount ||
			bgfx::getAvailTransientIndexBuffer(indexCount) < indexCount)
		{
			return RenderViewerStatus_TransientBufferUnavailable;
		}

		bgfx::TransientVertexBuffer vertexBuffer;
		bgfx::TransientIndexBuffer indexBuffer;
		bgfx::allocTransientVertexBuffer(&vertexBuffer, vertexCount, s_sceneVertexLayout);
		bgfx::allocTransientIndexBuffer(&indexBuffer, indexCount);

		SceneVertex* vertices = reinterpret_cast<SceneVertex*>(vertexBuffer.data);
		uint16_t* indices = reinterpret_cast<uint16_t*>(indexBuffer.data);
		for (int boxIndex = 0; boxIndex < chunkCount; ++boxIndex)
		{
			AppendDynamicBox(
				vertices,
				boxIndex,
				snapshot.transforms[submitted + boxIndex],
				snapshot.dynamicHalfExtentX,
				snapshot.dynamicHalfExtentY,
				snapshot.dynamicHalfExtentZ);
			AppendIndexBlock(indices, boxIndex);
		}

		bgfx::setVertexBuffer(0, &vertexBuffer);
		bgfx::setIndexBuffer(&indexBuffer);
		SubmitBatch(chunkCount, state, color);
		submitted += chunkCount;
	}

	return RenderViewerStatus_Ok;
}

int SubmitStaticBoxes(VisualSnapshot snapshot, uint64_t state, const float* color)
{
	if (snapshot.staticBoxCount == 0)
	{
		return RenderViewerStatus_Ok;
	}

	uint32_t vertexCount = static_cast<uint32_t>(snapshot.staticBoxCount * kCubeVertexCount);
	uint32_t indexCount = static_cast<uint32_t>(snapshot.staticBoxCount * kCubeIndexCount);
	if (bgfx::getAvailTransientVertexBuffer(vertexCount, s_sceneVertexLayout) < vertexCount ||
		bgfx::getAvailTransientIndexBuffer(indexCount) < indexCount)
	{
		return RenderViewerStatus_TransientBufferUnavailable;
	}

	bgfx::TransientVertexBuffer vertexBuffer;
	bgfx::TransientIndexBuffer indexBuffer;
	bgfx::allocTransientVertexBuffer(&vertexBuffer, vertexCount, s_sceneVertexLayout);
	bgfx::allocTransientIndexBuffer(&indexBuffer, indexCount);

	SceneVertex* vertices = reinterpret_cast<SceneVertex*>(vertexBuffer.data);
	uint16_t* indices = reinterpret_cast<uint16_t*>(indexBuffer.data);
	for (int boxIndex = 0; boxIndex < snapshot.staticBoxCount; ++boxIndex)
	{
		AppendStaticBox(vertices, boxIndex, snapshot.staticBoxes[boxIndex]);
		AppendIndexBlock(indices, boxIndex);
	}

	bgfx::setVertexBuffer(0, &vertexBuffer);
	bgfx::setIndexBuffer(&indexBuffer);
	SubmitBatch(snapshot.staticBoxCount, state, color);
	return RenderViewerStatus_Ok;
}

void SetupCamera(RenderPlatformState state)
{
	float view[16];
	float proj[16];
	float aspect = static_cast<float>(state.width) / static_cast<float>(state.height);
	SceneVec3 eye = { 0.0f, 50.0f, 26.0f };
	SceneVec3 target = { 0.0f, 13.5f, 0.0f };
	BuildLookAt(view, eye, target);
	BuildPerspective(proj, 60.0f, aspect, 0.1f, 140.0f, bgfx::getCaps()->homogeneousDepth ? 1 : 0);
	bgfx::setViewTransform(kViewId, view, proj);
}

int PhysicsSceneViewCreate()
{
	s_sceneVertexLayout
		.begin()
		.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
		.add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Uint8)
		.end();

	bgfx::RendererType::Enum type = bgfx::getRendererType();
	s_sceneProgram = bgfx::createProgram(
		bgfx::createEmbeddedShader(kSceneShaders, type, "vs_debugdraw_fill_lit"),
		bgfx::createEmbeddedShader(kSceneShaders, type, "fs_debugdraw_fill_lit"),
		true);
	s_sceneParams = bgfx::createUniform("u_params", bgfx::UniformType::Vec4, 4);
	if (!bgfx::isValid(s_sceneProgram) || !bgfx::isValid(s_sceneParams))
	{
		PhysicsSceneViewDestroy();
		return RenderViewerStatus_RendererResourceFailed;
	}

	s_sceneReady = 1;
	return RenderViewerStatus_Ok;
}

int PhysicsSceneViewDrawSnapshot(RenderPlatformState state, VisualSnapshot snapshot)
{
	int snapshotStatus = ValidateSnapshot(snapshot);
	if (snapshotStatus != VisualBridgeStatus_Ok)
	{
		return RenderViewerStatus_InvalidArgument;
	}

	if (s_sceneReady == 0)
	{
		return RenderViewerStatus_RendererResourceFailed;
	}

	bgfx::setViewRect(kViewId, 0, 0, static_cast<unsigned short>(state.width), static_cast<unsigned short>(state.height));
	bgfx::setViewClear(kViewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x202833ff, 1.0f, 0);
	bgfx::touch(kViewId);
	SetupCamera(state);

	uint64_t stateFlags = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
	static const float kStaticColor[4] = { 0.34f, 0.39f, 0.42f, 1.0f };
	static const float kDynamicColor[4] = { 0.19f, 0.62f, 0.92f, 1.0f };

	int staticStatus = SubmitStaticBoxes(snapshot, stateFlags, kStaticColor);
	if (staticStatus != RenderViewerStatus_Ok)
	{
		return staticStatus;
	}

	return SubmitDynamicBoxes(snapshot, stateFlags, kDynamicColor);
}

void PhysicsSceneViewDestroy()
{
	if (bgfx::isValid(s_sceneProgram))
	{
		bgfx::destroy(s_sceneProgram);
		s_sceneProgram = BGFX_INVALID_HANDLE;
	}

	if (bgfx::isValid(s_sceneParams))
	{
		bgfx::destroy(s_sceneParams);
		s_sceneParams = BGFX_INVALID_HANDLE;
	}

	s_sceneReady = 0;
}
}
