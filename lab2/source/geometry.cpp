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
	const glm::vec3 up{ 0, -1, 0 }; // y points down

	const uint32_t top_center = 0;
	const uint32_t bottom_center = 1;
	enum Ring : uint32_t { top_cap, bottom_cap, side_top, side_bottom };
	const auto ring = [segments](Ring ring, uint32_t i) {
		return 2 + ring * segments + i % segments;
	};

	Mesh mesh;
	mesh.vertices.resize(2 + 4 * segments);
	mesh.vertices[top_center] = { .position = { 0, top_y, 0 }, .normal = up };
	mesh.vertices[bottom_center] = { .position = { 0, bottom_y, 0 }, .normal = -up };

	for (uint32_t i = 0; i < segments; ++i) {
		const float angle = glm::two_pi<float>() * float(i) / float(segments);
		const glm::vec3 radial{ std::cos(angle), 0, std::sin(angle) };
		const glm::vec3 top{ radius * radial.x, top_y, radius * radial.z };
		const glm::vec3 bottom{ top.x, bottom_y, top.z };

		mesh.vertices[ring(top_cap, i)] = { .position = top, .normal = up };
		mesh.vertices[ring(bottom_cap, i)] = { .position = bottom, .normal = -up };
		mesh.vertices[ring(side_top, i)] = { .position = top, .normal = radial };
		mesh.vertices[ring(side_bottom, i)] = { .position = bottom, .normal = radial };
	}

	for (auto& vertex : mesh.vertices) {
		vertex.color = colorFromPosition(vertex.position, box_min, box_max);
	}

	mesh.indices.reserve(12 * segments);
	const auto add_triangle = [&mesh](uint32_t a, uint32_t b, uint32_t c) {
		mesh.indices.insert(mesh.indices.end(), { a, b, c });
	};

	for (uint32_t i = 0; i < segments; ++i) {
		add_triangle(top_center, ring(top_cap, i + 1), ring(top_cap, i));
		add_triangle(bottom_center, ring(bottom_cap, i), ring(bottom_cap, i + 1));

		add_triangle(ring(side_top, i), ring(side_top, i + 1), ring(side_bottom, i));
		add_triangle(ring(side_top, i + 1), ring(side_bottom, i + 1), ring(side_bottom, i));
	}

	return mesh;
}

Mesh makeCube() {
	Mesh mesh;

	// Bits 0, 1, 2 of the corner index choose -0.5 or 0.5 for x, y, z
	for (uint32_t i = 0; i < 8; ++i) {
		mesh.vertices.push_back({ .position = glm::vec3(i & 1, (i >> 1) & 1, (i >> 2) & 1) - 0.5f });
	}

	// Faces -x, +x, -y, +y, -z, +z, same winding as the cylinder
	mesh.indices = {
		0, 2, 4, 2, 6, 4,
		1, 5, 3, 3, 5, 7,
		0, 4, 1, 1, 4, 5,
		2, 3, 6, 3, 7, 6,
		0, 1, 2, 1, 3, 2,
		4, 6, 5, 5, 6, 7,
	};

	return mesh;
}

Mesh makeConeLines(uint32_t segments) {
	segments = std::max(segments, 3u);

	Mesh mesh;
	mesh.vertices.push_back({ .position = { 0, 0, 0 } });
	for (uint32_t i = 0; i < segments; ++i) {
		const float angle = glm::two_pi<float>() * float(i) / float(segments);
		mesh.vertices.push_back({ .position = { std::cos(angle), std::sin(angle), 1 } });
	}

	// From the apex to every base point, and along the base circle
	for (uint32_t i = 0; i < segments; ++i) {
		const uint32_t point = 1 + i;
		const uint32_t next = 1 + (i + 1) % segments;
		mesh.indices.insert(mesh.indices.end(), { 0, point, point, next });
	}

	return mesh;
}

} // namespace geometry
