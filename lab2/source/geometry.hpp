#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace geometry {

struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec3 color;
};

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
};

// Caps and side have separate vertices: a rim vertex can't have both the cap normal and the side normal
Mesh makeCylinder(uint32_t segments, float radius, float height);

// Light markers are drawn without lighting, so only positions are set

// Cube with edge 1 centered at the origin
Mesh makeCube();

// Lines (pairs of indices) of a cone: apex at the origin, base circle of radius 1 at z = 1
Mesh makeConeLines(uint32_t segments);

glm::vec3 colorFromPosition(glm::vec3 position, glm::vec3 box_min, glm::vec3 box_max);

} // namespace geometry
