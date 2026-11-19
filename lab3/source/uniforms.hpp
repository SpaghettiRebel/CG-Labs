#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

namespace uniforms {

// set = 0, binding = 0
struct SceneUniforms {
	glm::mat4 view_projection;
	glm::vec3 camera_position;
	uint32_t point_light_count;
	glm::vec3 directional_light_direction;
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
	float cos_inner;
	glm::vec3 direction;
	float cos_outer;
	glm::vec3 color;
	float padding;
};

// set = 1, binding = 0: element of the dynamic uniform buffer
struct ObjectUniforms {
	glm::mat4 model;
	glm::vec3 specular;
	float shininess;
};

// Push constants of the objects
struct ObjectConstants {
	uint32_t animated_sampling;
	float time;
};

struct MarkerConstants {
	glm::mat4 model_view_projection;
	glm::vec4 color;
};

// Distance between elements of the dynamic uniform buffer: offsets must be multiples of the
// alignment reported by the driver (minUniformBufferOffsetAlignment, a power of two)
constexpr uint64_t alignUp(uint64_t size, uint64_t alignment) {
	return (size + alignment - 1) & ~(alignment - 1);
}

static_assert(alignUp(80, 16) == 80);
static_assert(alignUp(80, 64) == 128);
static_assert(alignUp(80, 256) == 256);

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

static_assert(sizeof(ObjectUniforms) == 80);
static_assert(offsetof(ObjectUniforms, specular) == 64);
static_assert(offsetof(ObjectUniforms, shininess) == 76);

static_assert(sizeof(ObjectConstants) == 8);
static_assert(offsetof(ObjectConstants, time) == 4);

static_assert(sizeof(MarkerConstants) == 80);
static_assert(offsetof(MarkerConstants, color) == 64);

} // namespace uniforms
