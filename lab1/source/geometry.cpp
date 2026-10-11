#include "geometry.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace geometry {

glm::vec3 colorFromPosition(glm::vec3 position, glm::vec3 box_min, glm::vec3 box_max) {
	return (position - box_min) / (box_max - box_min);
}

Mesh makeCylinder(uint32_t segments, float radius, float height) {
	segments = std::max(segments, 3u);

	const float top_y = -height / 2;
	const float bottom_y = height / 2;
	const glm::vec3 box_min{ -radius, top_y, -radius };
	const glm::vec3 box_max{ radius, bottom_y, radius };

	const uint32_t top_center = 0;
	const uint32_t bottom_center = 1;
	const auto top_rim = [segments](uint32_t i) {
		return 2 + i % segments;
	};
	const auto bottom_rim = [segments](uint32_t i) {
		return 2 + segments + i % segments;
	};

	Mesh mesh;
	mesh.vertices.resize(2 + 2 * segments);
	mesh.vertices[top_center].position = { 0, top_y, 0 };
	mesh.vertices[bottom_center].position = { 0, bottom_y, 0 };

	for (uint32_t i = 0; i < segments; ++i) {
		const float angle = glm::two_pi<float>() * float(i) / float(segments);
		const float x = radius * std::cos(angle);
		const float z = radius * std::sin(angle);
		mesh.vertices[top_rim(i)].position = { x, top_y, z };
		mesh.vertices[bottom_rim(i)].position = { x, bottom_y, z };
	}

	for (auto& vertex : mesh.vertices) {
		vertex.color = colorFromPosition(vertex.position, box_min, box_max);
	}

	mesh.indices.reserve(12 * segments);
	const auto add_triangle = [&mesh](uint32_t a, uint32_t b, uint32_t c) {
		mesh.indices.insert(mesh.indices.end(), { a, b, c });
	};

	for (uint32_t i = 0; i < segments; ++i) {
		add_triangle(top_center, top_rim(i + 1), top_rim(i));
		add_triangle(bottom_center, bottom_rim(i), bottom_rim(i + 1));

		add_triangle(top_rim(i), top_rim(i + 1), bottom_rim(i));
		add_triangle(top_rim(i + 1), bottom_rim(i + 1), bottom_rim(i));
	}

	return mesh;
}

} // namespace geometry
