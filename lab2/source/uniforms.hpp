#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

// Data for shaders, the same layout as the blocks in shaders/*.glsl.
// In std140 and std430 a vec3 takes 16 bytes unless a 4-byte member follows it, so every vec3 is paired with one.
// Colors are linear, light colors are already multiplied by the intensity.
namespace uniforms {

// set = 0, binding = 0
struct SceneUniforms {
	glm::mat4 view_projection;
	glm::vec3 camera_position;
	uint32_t point_light_count;
	glm::vec3 directional_light_direction; // Normalized, the way the light goes
	uint32_t spot_light_count;
	glm::vec3 directional_light_color;
	float padding0;
	glm::vec3 ambient_light;
	float padding1;
};

// set = 0, binding = 1: array element
struct PointLight {
	glm::vec3 position;
	float padding0;
	glm::vec3 color;
	float padding1;
};

// set = 0, binding = 2: array element
struct SpotLight {
	glm::vec3 position;
	float cos_inner; // Cosine of the angle between the axis and the edge of the full light
	glm::vec3 direction; // Normalized
	float cos_outer; // Cosine of the angle between the axis and the edge of the light, not more than cos_inner
	glm::vec3 color;
	float padding;
};

// set = 1, binding = 0
struct ObjectUniforms {
	glm::mat4 model;
	glm::vec3 albedo;
	float vertex_colors; // Weight of vertex colors: 0 = albedo, 1 = albedo * vertex color
	glm::vec3 specular;
	float shininess;
};

// Push constants of the light markers
struct MarkerConstants {
	glm::mat4 model_view_projection;
	glm::vec4 color;
};

static_assert(sizeof(SceneUniforms) == 128);
static_assert(offsetof(SceneUniforms, camera_position) == 64);
static_assert(offsetof(SceneUniforms, point_light_count) == 76);
static_assert(offsetof(SceneUniforms, directional_light_direction) == 80);
static_assert(offsetof(SceneUniforms, spot_light_count) == 92);
static_assert(offsetof(SceneUniforms, directional_light_color) == 96);
static_assert(offsetof(SceneUniforms, ambient_light) == 112);

static_assert(sizeof(PointLight) == 32);
static_assert(offsetof(PointLight, color) == 16);

static_assert(sizeof(SpotLight) == 48);
static_assert(offsetof(SpotLight, cos_inner) == 12);
static_assert(offsetof(SpotLight, direction) == 16);
static_assert(offsetof(SpotLight, cos_outer) == 28);
static_assert(offsetof(SpotLight, color) == 32);

static_assert(sizeof(ObjectUniforms) == 96);
static_assert(offsetof(ObjectUniforms, albedo) == 64);
static_assert(offsetof(ObjectUniforms, vertex_colors) == 76);
static_assert(offsetof(ObjectUniforms, specular) == 80);
static_assert(offsetof(ObjectUniforms, shininess) == 92);

static_assert(sizeof(MarkerConstants) == 80);
static_assert(offsetof(MarkerConstants, color) == 64);

} // namespace uniforms
