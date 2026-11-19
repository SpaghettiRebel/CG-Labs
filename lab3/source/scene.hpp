#pragma once

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

#include "uniforms.hpp"

namespace scene {

enum class ProjectionType {
	perspective,
	orthographic,
};

struct Projection {
	ProjectionType type = ProjectionType::perspective;
	float vertical_fov = 60; // Degrees, perspective only
	float ortho_height = 3;  // World units visible vertically, orthographic only
	float z_near = 0.1f;
	float z_far = 100;
};

glm::mat4 rotationMatrix(float yaw, float pitch);

glm::vec3 forwardDirection(float yaw, float pitch);

struct Camera {
	glm::vec3 position{ 0, 0, -3 };
	float yaw = 0;
	float pitch = 0;
};

Camera cameraAround(float distance, float yaw, float pitch);

inline constexpr float min_scale = 0.1f;

struct Transform {
	glm::vec3 position{ 0, 0, 0 };
	glm::vec3 rotation{ 0, 0, 0 }; // Degrees around x, then y, then z
	glm::vec3 scale{ 1, 1, 1 };
};

glm::mat4 modelMatrix(const Transform& transform, float spin = 0);

struct Trajectory {
	float radius = 1;
	float wobble_radius = 0.25f;
	int lobes = 5;
	float bob_height = 0.25f;
	float lap_time = 8;
	float spin = 90;
};

glm::vec3 trajectoryOffset(const Trajectory& trajectory, float time, float phase);

glm::mat4 animatedModelMatrix(const Transform& transform, const Trajectory& trajectory, float time, float phase);

inline constexpr float max_frame_time = 0.1f;

struct AnimationClock {
	bool playing = true;
	float speed = 1;
	float time = 0;

	void advance(float dt);
	void reset();
};

struct Object {
	Transform transform;
	int material = 0;                   // Index in materials
	glm::vec3 specular{ 1, 1, 1 };      // sRGB, multiplied by the specular texture
	float shininess = 32;
	bool animated_sampling = false;     // Non-trivial sampling of the albedo texture in the shader
	float trajectory_phase = 0;
	int slot = 0;
};

// Texture files of a material. Without specular or emissive texture a black pixel is used:
// no highlight and no glow
struct Material {
	const char* name;
	const char* albedo;
	const char* specular;
	const char* emissive;
};

inline constexpr Material materials[] = {
	{ "Bricks", "textures/bricks_albedo.png", "textures/bricks_specular.png", nullptr },
	{ "Lava", "textures/lava_albedo.png", "textures/lava_specular.png", "textures/lava_emissive.png" },
	{ "Metal", "textures/metal_albedo.png", "textures/metal_specular.png", nullptr },
	{ "Wood", "textures/wood_albedo.png", nullptr, nullptr },
};

inline constexpr int material_count = sizeof(materials) / sizeof(materials[0]);

inline constexpr int max_objects = 8;
inline constexpr float object_spacing = 1.5f;

Object makeObject(int index);

int freeSlot(const std::vector<Object>& objects);

uniforms::ObjectUniforms objectUniforms(const Object& object, const glm::mat4& model);

uniforms::ObjectConstants objectConstants(const Object& object, float time);

// Lights. Colors are sRGB, radiance = linear color * intensity (/ distance^2 for point and spot lights)

struct AmbientLight {
	glm::vec3 color{ 1, 1, 1 };
	float intensity = 0.05f;
};

struct DirectionalLight {
	glm::vec3 color{ 1, 1, 1 };
	float intensity = 0.6f;
	float yaw = 30; // Direction of the light, as for the camera
	float pitch = 50;
};

struct PointLight {
	glm::vec3 position{ 0.8f, -0.4f, -0.9f };
	glm::vec3 color{ 1, 0.6f, 0.3f };
	float intensity = 1;
};

struct SpotLight {
	glm::vec3 position{ -0.55f, -1, -1.4f };
	glm::vec3 color{ 0.5f, 0.8f, 1 };
	float intensity = 2;
	float yaw = 30;
	float pitch = 35;
	float inner_angle = 8;
	float outer_angle = 16;
};

inline constexpr size_t max_point_lights = 16;
inline constexpr size_t max_spot_lights = 16;

struct Lights {
	AmbientLight ambient;
	DirectionalLight directional;
	std::vector<PointLight> points{ PointLight{} };
	std::vector<SpotLight> spots{ SpotLight{} };
};

uniforms::SceneUniforms sceneUniforms(const glm::mat4& view_projection, const Camera& camera, const Lights& lights);

uniforms::PointLight pointLightUniforms(const PointLight& light);

uniforms::SpotLight spotLightUniforms(const SpotLight& light);

glm::mat4 spotConeMatrix(const SpotLight& light, float length);

glm::mat4 projectionMatrix(const Projection& projection, float aspect);

glm::mat4 viewMatrix(const Camera& camera);

} // namespace scene
