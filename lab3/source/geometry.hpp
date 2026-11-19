#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace geometry {

struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 uv;
};

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
};

// How many times the texture repeats around the side, so that it is not stretched:
// the circumference of the default cylinder is about 3 heights
inline constexpr float side_texture_repeats = 3;

Mesh makeCylinder(uint32_t segments, float radius, float height);

Mesh makeCube();

Mesh makeConeLines(uint32_t segments);

} // namespace geometry
