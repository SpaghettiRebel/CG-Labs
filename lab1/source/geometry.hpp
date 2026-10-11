#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace geometry {

struct Vertex {
	glm::vec3 position;
	glm::vec3 color;
};

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
};

Mesh makeCylinder(uint32_t segments, float radius, float height);

glm::vec3 colorFromPosition(glm::vec3 position, glm::vec3 box_min, glm::vec3 box_max);

} // namespace geometry
