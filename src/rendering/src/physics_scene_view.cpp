#include "benchmark_visual/visual_renderer.h"

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
#include <new>

// altered raylib 6.0 instancing and shadowmap examples with original notices in SOURCES.md
namespace benchmark_visual
{
constexpr std::uint32_t kMaterialCount = 8;
enum SceneGroupSurface
{
	SceneGroupSurface_Solid,
	SceneGroupSurface_Outline,
};
struct SceneInstanceGroup
{
	std::uint32_t offset;
	std::uint32_t count;
	SceneGroupSurface surface;
};
struct PhysicsSceneResources
{
	Shader shader;
	Material material;
	std::array<Mesh, kMaxSceneGeometries> meshes;
	std::array<SceneInstanceGroup, kMaxSceneGeometries * kMaterialCount * 2> groups;
	std::vector<std::uint32_t> instanceOrder;
	std::vector<Matrix> transforms;
	const VisualScene* scene;
	VisualCameraContext cameraContext;
	ResolvedVisualCamera resolvedCamera;
	ResolvedVisualCamera cameraOverride;
	int resolvedCameraReady;
	int cameraOverrideActive;
	int step;
	ReplayAppearance appearance;
};

namespace
{
const Vector3 kLightDirection = Vector3Normalize({0.35f, -1, -0.35f});

const char* kVertexShader = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in mat4 instanceTransform;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec3 fragNormal;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragNormal = normalize(mat3(instanceTransform)*vertexNormal);
    gl_Position = mvp*instanceTransform*vec4(vertexPosition, 1.0);
}
)";
const char* kFragmentShader = R"(#version 330
in vec2 fragTexCoord;
in vec3 fragNormal;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 ambient;
out vec4 finalColor;

void main()
{
    vec4 albedo = texture(texture0, fragTexCoord)*colDiffuse;
    float diffuse = max(dot(normalize(fragNormal), -lightDir), 0.0);
    vec3 linearColor = albedo.rgb*(ambient + lightColor*diffuse);
    finalColor = vec4(pow(linearColor, vec3(1.0/2.2)), albedo.a);
}
)";

int CreateSceneResources(PhysicsSceneResources* resources)
{
	resources->shader = LoadShaderFromMemory(kVertexShader, kFragmentShader);
	if (resources->shader.id == 0 || resources->shader.id == rlGetShaderIdDefault())
		return RenderViewerStatus_RendererResourceFailed;
	resources->shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(resources->shader, "mvp");
	resources->shader.locs[SHADER_LOC_VERTEX_INSTANCETRANSFORM] = GetShaderLocationAttrib(resources->shader, "instanceTransform");
	if (resources->shader.locs[SHADER_LOC_MATRIX_MVP] < 0 || resources->shader.locs[SHADER_LOC_VERTEX_INSTANCETRANSFORM] < 0)
		return RenderViewerStatus_RendererResourceFailed;
	const Vector3 lightColor = {0.55f, 0.55f, 0.55f};
	const Vector3 ambient = {0.20f, 0.20f, 0.20f};
	SetShaderValue(resources->shader, GetShaderLocation(resources->shader, "lightDir"), &kLightDirection, SHADER_UNIFORM_VEC3);
	SetShaderValue(resources->shader, GetShaderLocation(resources->shader, "lightColor"), &lightColor, SHADER_UNIFORM_VEC3);
	SetShaderValue(resources->shader, GetShaderLocation(resources->shader, "ambient"), &ambient, SHADER_UNIFORM_VEC3);
	resources->material = LoadMaterialDefault();
	if (resources->material.maps == nullptr)
		return RenderViewerStatus_RendererResourceFailed;
	resources->material.shader = resources->shader;
	return RenderViewerStatus_Ok;
}

Mesh ImportTriangles(const VisualScene& scene, const VisualGeometry& geometry)
{
	Mesh mesh = {};
	mesh.vertexCount = static_cast<int>(geometry.indexCount);
	mesh.triangleCount = mesh.vertexCount / 3;
	mesh.vertices = static_cast<float*>(MemAlloc(mesh.vertexCount * 3 * sizeof(float)));
	mesh.normals = static_cast<float*>(MemAlloc(mesh.vertexCount * 3 * sizeof(float)));
	mesh.texcoords = static_cast<float*>(MemAlloc(mesh.vertexCount * 2 * sizeof(float)));
	if (mesh.vertices == nullptr || mesh.normals == nullptr || mesh.texcoords == nullptr)
		return mesh;
	for (int triangle = 0; triangle < mesh.triangleCount; ++triangle)
	{
		std::array<Vector3, 3> corners = {};
		for (int corner = 0; corner < 3; ++corner)
		{
			const VisualMeshVertex& vertex =
			    scene.vertices[geometry.vertexOffset + scene.indices[geometry.indexOffset + triangle * 3 + corner]];
			corners[corner] = {vertex.x, vertex.y, vertex.z};
		}
		const Vector3 normal = Vector3Normalize(
		    Vector3CrossProduct(Vector3Subtract(corners[1], corners[0]), Vector3Subtract(corners[2], corners[0])));
		for (int corner = 0; corner < 3; ++corner)
		{
			const int index = triangle * 3 + corner;
			mesh.vertices[index * 3] = corners[corner].x;
			mesh.vertices[index * 3 + 1] = corners[corner].y;
			mesh.vertices[index * 3 + 2] = corners[corner].z;
			mesh.normals[index * 3] = normal.x;
			mesh.normals[index * 3 + 1] = normal.y;
			mesh.normals[index * 3 + 2] = normal.z;
			mesh.texcoords[index * 2] = mesh.texcoords[index * 2 + 1] = 0;
		}
	}
	UploadMesh(&mesh, false);
	return mesh;
}

void UpdateTransforms(PhysicsSceneResources* resources, const VisualSnapshot& snapshot)
{
	if (resources->step == snapshot.stepIndex)
		return;
	for (std::size_t index = 0; index < resources->instanceOrder.size(); ++index)
	{
		const VisualInstance& instance = resources->scene->instances[resources->instanceOrder[index]];
		const VisualTransform& pose = instance.transformSlot == UINT32_MAX
		                                  ? instance.initialTransform
		                                  : snapshot.transforms[instance.transformSlot].transform;
		Matrix matrix = QuaternionToMatrix({pose.rotationX, pose.rotationY, pose.rotationZ, pose.rotationW});
		matrix.m12 = pose.positionX;
		matrix.m13 = pose.positionY;
		matrix.m14 = pose.positionZ;
		resources->transforms[index] = matrix;
	}
	resources->step = snapshot.stepIndex;
}

void DrawGroups(PhysicsSceneResources* resources)
{
	for (std::uint32_t index = 0; index < resources->scene->geometryCount * kMaterialCount * 2; ++index)
	{
		const SceneInstanceGroup& group = resources->groups[index];
		if (group.count == 0 || group.surface == SceneGroupSurface_Outline)
			continue;
		const VisualInstance& first = resources->scene->instances[resources->instanceOrder[group.offset]];
		resources->material.maps[MATERIAL_MAP_DIFFUSE].color =
		    first.transformSlot == UINT32_MAX ? Color{87, 99, 107, 255} : Color{255, 102, 12, 255};
		DrawMeshInstanced(resources->meshes[index / (kMaterialCount * 2)], resources->material, resources->transforms.data() + group.offset,
		                  static_cast<int>(group.count));
	}
}

void DrawContainerOutlines(PhysicsSceneResources* resources)
{
	for (std::uint32_t index = 0; index < resources->scene->geometryCount * kMaterialCount * 2; ++index)
	{
		const SceneInstanceGroup& group = resources->groups[index];
		if (group.count == 0 || group.surface != SceneGroupSurface_Outline)
			continue;
		const VisualGeometry& geometry = resources->scene->geometries[index / (kMaterialCount * 2)];
		const Vector3 size = {2 * geometry.parameterX, 2 * geometry.parameterY, 2 * geometry.parameterZ};
		for (std::uint32_t instance = group.offset; instance < group.offset + group.count; ++instance)
		{
			rlPushMatrix();
			rlMultMatrixf(MatrixToFloat(resources->transforms[instance]));
			DrawCubeWiresV({}, size, {130, 145, 155, 255});
			rlPopMatrix();
		}
	}
}

void DrawSceneDebugPrimitives(const VisualSnapshot& snapshot)
{
	for (int index = 0; index < snapshot.debugPrimitiveCount; ++index)
	{
		const VisualDebugPrimitive& primitive = snapshot.debugPrimitives[index];
		const Color color = primitive.materialIndex == 6 ? Color{13, 255, 26, 255} : Color{255, 184, 61, 255};
		const Vector3 origin = {primitive.originOrCenterX, primitive.originOrCenterY, primitive.originOrCenterZ};
		const Vector3 end = {primitive.endOrHalfExtentsX, primitive.endOrHalfExtentsY, primitive.endOrHalfExtentsZ};
		if (primitive.kind == VisualDebugPrimitiveKind_AabbOverlap)
			DrawCubeWiresV(origin, Vector3Scale(end, 2), color);
		else
		{
			DrawLine3D(origin, end, color);
			if (primitive.kind == VisualDebugPrimitiveKind_SphereCast)
			{
				DrawSphereWires(origin, primitive.radius, 4, 8, color);
				DrawSphereWires(end, primitive.radius, 4, 8, color);
			}
		}
	}
}
}

void PhysicsSceneViewSetAppearance(PhysicsSceneResources* resources, ReplayAppearance appearance)
{
	resources->appearance = appearance;
}

void PhysicsSceneViewReleaseScene(PhysicsSceneResources* resources)
{
	if (resources == nullptr)
		return;
	for (Mesh& mesh : resources->meshes)
	{
		if (mesh.vertices != nullptr || mesh.normals != nullptr || mesh.texcoords != nullptr || mesh.vaoId != 0)
			UnloadMesh(mesh);
		mesh = {};
	}
	// maps use the library default texture while the scene shader has one separate owner
	if (resources->material.maps != nullptr)
		MemFree(resources->material.maps);
	resources->material = {};
	if (resources->shader.id != 0 && resources->shader.id != rlGetShaderIdDefault())
		UnloadShader(resources->shader);
	resources->shader = {};
	delete resources;
}

int PhysicsSceneViewInstallScene(const VisualScene* scene, PhysicsSceneResources** output)
{
	PhysicsSceneResources* resources = new (std::nothrow) PhysicsSceneResources{};
	if (resources == nullptr)
		return RenderViewerStatus_RendererResourceFailed;
	resources->step = -1;
	if (ValidateScene(scene) != VisualBridgeStatus_Ok ||
	    PrepareVisualCamera(scene, &resources->cameraContext) != VisualCameraStatus_Ok)
	{
		delete resources;
		return RenderViewerStatus_InvalidArgument;
	}
	if (CreateSceneResources(resources) != RenderViewerStatus_Ok)
	{
		PhysicsSceneViewReleaseScene(resources);
		return RenderViewerStatus_RendererResourceFailed;
	}
	for (std::uint32_t index = 0; index < scene->geometryCount; ++index)
	{
		const VisualGeometry& geometry = scene->geometries[index];
		resources->meshes[index] = geometry.kind == VisualGeometryKind_Box
		                      ? GenMeshCube(2 * geometry.parameterX, 2 * geometry.parameterY, 2 * geometry.parameterZ)
		                  : geometry.kind == VisualGeometryKind_Sphere ? GenMeshSphere(geometry.parameterX, 16, 32)
		                                                               : ImportTriangles(*scene, geometry);
		if (resources->meshes[index].vaoId == 0)
		{
			PhysicsSceneViewReleaseScene(resources);
			return RenderViewerStatus_RendererResourceFailed;
		}
	}
	resources->instanceOrder.resize(scene->instanceCount);
	resources->transforms.resize(scene->instanceCount);
	for (std::uint32_t index = 0; index < scene->instanceCount; ++index)
	{
		const VisualInstance& instance = scene->instances[index];
		if (instance.materialIndex >= kMaterialCount)
		{
			PhysicsSceneViewReleaseScene(resources);
			return RenderViewerStatus_InvalidArgument;
		}
		++resources->groups[(instance.geometryIndex * kMaterialCount + instance.materialIndex) * 2 +
		           (instance.transformSlot != UINT32_MAX ? 1 : 0)]
		      .count;
	}
	std::uint32_t offset = 0;
	for (SceneInstanceGroup& group : resources->groups)
	{
		group.offset = offset;
		offset += group.count;
		group.count = 0;
	}
	for (std::uint32_t index = 0; index < scene->instanceCount; ++index)
	{
		const VisualInstance& instance = scene->instances[index];
		SceneInstanceGroup& group = resources->groups[(instance.geometryIndex * kMaterialCount + instance.materialIndex) * 2 +
		                                     (instance.transformSlot != UINT32_MAX ? 1 : 0)];
		resources->instanceOrder[group.offset + group.count++] = index;
		if (instance.transformSlot == UINT32_MAX &&
		    scene->geometries[instance.geometryIndex].kind == VisualGeometryKind_Box &&
		    std::string_view(scene->identity.fixtureSemantic).starts_with("open_container_falling_pile"))
			group.surface = SceneGroupSurface_Outline;
	}
	resources->scene = scene;
	*output = resources;
	return RenderViewerStatus_Ok;
}

int PhysicsSceneViewGetCameraControl(const PhysicsSceneResources* resources, ResolvedVisualCamera* camera, int* overrideActive)
{
	if (camera == nullptr || overrideActive == nullptr || resources->resolvedCameraReady == 0)
		return RenderViewerStatus_RouteUnavailable;
	*camera = resources->resolvedCamera;
	*overrideActive = resources->cameraOverrideActive;
	return RenderViewerStatus_Ok;
}

int PhysicsSceneViewSetCameraControl(PhysicsSceneResources* resources, const ResolvedVisualCamera* camera)
{
	if (camera == nullptr || resources->scene == nullptr)
		return RenderViewerStatus_InvalidArgument;
	VisualCameraPolicy policy = {};
	policy.eye = camera->eye;
	policy.target = camera->target;
	policy.up = camera->up;
	policy.verticalFovDegrees = camera->verticalFovDegrees;
	policy.nearPlane = camera->nearPlane;
	policy.farPlane = camera->farPlane;
	policy.mode = VisualCameraMode_Fixed;
	if (ValidateVisualCameraPolicy(&policy) != VisualCameraStatus_Ok)
		return RenderViewerStatus_InvalidArgument;
	resources->cameraOverride = *camera;
	resources->resolvedCamera = *camera;
	resources->resolvedCameraReady = 1;
	resources->cameraOverrideActive = 1;
	return RenderViewerStatus_Ok;
}

void PhysicsSceneViewResetCameraControl(PhysicsSceneResources* resources)
{
	resources->cameraOverride = {};
	resources->cameraOverrideActive = 0;
}

int PhysicsSceneViewDrawSnapshot(PhysicsSceneResources* resources, RenderPlatformState state, RenderViewport viewport, VisualSnapshot snapshot, RenderViewport clip)
{
	if (clip.width <= 0 || clip.height <= 0)
		return RenderViewerStatus_Ok;
	if (resources->scene == nullptr || resources->scene != snapshot.scene ||
	    snapshot.transformCount != static_cast<int>(resources->scene->dynamicTransformCount) ||
	    snapshot.debugPrimitiveCount != static_cast<int>(resources->scene->debugPrimitiveCount) ||
	    (snapshot.transformCount != 0 && snapshot.transforms == nullptr) ||
	    (snapshot.debugPrimitiveCount != 0 && snapshot.debugPrimitives == nullptr))
		return RenderViewerStatus_InvalidArgument;
	ResolvedVisualCamera camera = {};
	if (ResolveVisualCamera(&resources->cameraContext, static_cast<float>(viewport.width) / viewport.height, snapshot.transforms,
	                        static_cast<std::uint32_t>(snapshot.transformCount), &camera) != VisualCameraStatus_Ok)
		return RenderViewerStatus_InvalidArgument;
	if (resources->cameraOverrideActive != 0)
		camera = resources->cameraOverride;
	resources->resolvedCamera = camera;
	resources->resolvedCameraReady = 1;
	UpdateTransforms(resources, snapshot);
	RendererRaylibBeginFrame();
	rlDrawRenderBatchActive();
	rlDisableBackfaceCulling();
	rlSetClipPlanes(camera.nearPlane, camera.farPlane);
	const Camera3D rayCamera = {{camera.eye.x, camera.eye.y, camera.eye.z},
	                            {camera.target.x, camera.target.y, camera.target.z},
	                            {camera.up.x, camera.up.y, camera.up.z},
	                            camera.verticalFovDegrees,
	                            CAMERA_PERSPECTIVE};
	rlViewport(viewport.x, state.height - viewport.y - viewport.height, viewport.width, viewport.height);
	rlEnableScissorTest();
	rlScissor(clip.x, state.height - clip.y - clip.height, clip.width, clip.height);
	BeginMode3D(rayCamera);
	rlMatrixMode(RL_PROJECTION);
	rlLoadIdentity();
	const double top = camera.nearPlane * std::tan(camera.verticalFovDegrees * 0.008726646259971648);
	const double right = top * static_cast<double>(viewport.width) / viewport.height;
	rlFrustum(-right, right, -top, top, camera.nearPlane, camera.farPlane);
	// retain the saved camera's left-handed screen orientation
	rlMatrixMode(RL_PROJECTION);
	rlScalef(-1, 1, 1);
	rlMatrixMode(RL_MODELVIEW);
	if (resources->appearance.surface == ReplaySurfaceMode_Wireframe)
		rlEnableWireMode();
	DrawGroups(resources);
	rlDisableWireMode();
	rlDisableShader();
	rlEnableBackfaceCulling();
	DrawContainerOutlines(resources);
	DrawSceneDebugPrimitives(snapshot);
	EndMode3D();
	rlDisableScissorTest();
	rlViewport(0, 0, state.width, state.height);
	return RenderViewerStatus_Ok;
}

}
