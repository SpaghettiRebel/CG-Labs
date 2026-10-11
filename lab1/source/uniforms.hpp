#pragma once

#include <cstddef>

#include <glm/glm.hpp>

namespace uniforms {

struct GlobalUniforms {
	glm::mat4 view_projection;
};

struct ObjectUniforms {
	glm::mat4 model;
	glm::vec4 color;      // Linear RGB, multiplies vertex colors
	glm::vec4 parameters; // x: weight of vertex colors, 0 = flat color, 1 = vertex colors * color
};

static_assert(sizeof(GlobalUniforms) == 64);
static_assert(sizeof(ObjectUniforms) == 96);
static_assert(offsetof(ObjectUniforms, color) == 64);
static_assert(offsetof(ObjectUniforms, parameters) == 80);

} // namespace uniforms
