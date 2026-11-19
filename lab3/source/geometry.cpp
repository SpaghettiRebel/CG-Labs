#include "geometry.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace geometry {

Mesh makeCylinder(uint32_t segments, float radius, float height) {
	segments = std::max(segments, 3u);

	const float top_y = -height / 2;
	const float bottom_y = height / 2;
	const glm::vec3 up{ 0, -1, 0 }; // y points down
	const glm::vec2 cap_center_uv{ 0.5f, 0.5f };

	const uint32_t top_center = 0;
	const uint32_t bottom_center = 1;
	const auto top_cap = [segments](uint32_t i) {
		return 2 + i % segments;
	};
	const auto bottom_cap = [segments](uint32_t i) {
		return 2 + segments + i % segments;
	};
	// The side has one more column: the first and the last are at the same place,
	// but with different u, otherwise the whole texture would be squeezed into the last segment
	const auto side_top = [segments](uint32_t i) {
		return 2 + 2 * segments + i;
	};
	const auto side_bottom = [segments](uint32_t i) {
		return 3 + 3 * segments + i;
	};

	Mesh mesh;
	mesh.vertices.resize(4 + 4 * segments);
	mesh.vertices[top_center] = { .position = { 0, top_y, 0 }, .normal = up, .uv = cap_center_uv };
	mesh.vertices[bottom_center] = { .position = { 0, bottom_y, 0 }, .normal = -up, .uv = cap_center_uv };

	for (uint32_t i = 0; i <= segments; ++i) {
		const float angle = glm::two_pi<float>() * float(i % segments) / float(segments);
		const glm::vec3 radial{ std::cos(angle), 0, std::sin(angle) };
		const glm::vec3 top{ radius * radial.x, top_y, radius * radial.z };
		const glm::vec3 bottom{ top.x, bottom_y, top.z };

		// Caps show a circle inscribed in the texture
		const glm::vec2 cap_uv = cap_center_uv + 0.5f * glm::vec2(radial.x, radial.z);
		mesh.vertices[top_cap(i)] = { .position = top, .normal = up, .uv = cap_uv };
		mesh.vertices[bottom_cap(i)] = { .position = bottom, .normal = -up, .uv = cap_uv };

		const float u = side_texture_repeats * float(i) / float(segments);
		mesh.vertices[side_top(i)] = { .position = top, .normal = radial, .uv = { u, 0 } };
		mesh.vertices[side_bottom(i)] = { .position = bottom, .normal = radial, .uv = { u, 1 } };
	}

	mesh.indices.reserve(12 * segments);
	const auto add_triangle = [&mesh](uint32_t a, uint32_t b, uint32_t c) {
		mesh.indices.insert(mesh.indices.end(), { a, b, c });
	};

	for (uint32_t i = 0; i < segments; ++i) {
		add_triangle(top_center, top_cap(i + 1), top_cap(i));
		add_triangle(bottom_center, bottom_cap(i), bottom_cap(i + 1));

		add_triangle(side_top(i), side_top(i + 1), side_bottom(i));
		add_triangle(side_top(i + 1), side_bottom(i + 1), side_bottom(i));
	}

	return mesh;
}

Mesh makeCube() {
	Mesh mesh;

	for (uint32_t i = 0; i < 8; ++i) {
		mesh.vertices.push_back({ .position = glm::vec3(i & 1, (i >> 1) & 1, (i >> 2) & 1) - 0.5f });
	}

	mesh.indices = {
		0,
		2,
		4,
		2,
		6,
		4,
		1,
		5,
		3,
		3,
		5,
		7,
		0,
		4,
		1,
		1,
		4,
		5,
		2,
		3,
		6,
		3,
		7,
		6,
		0,
		1,
		2,
		1,
		3,
		2,
		4,
		6,
		5,
		5,
		6,
		7,
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

	for (uint32_t i = 0; i < segments; ++i) {
		const uint32_t point = 1 + i;
		const uint32_t next = 1 + (i + 1) % segments;
		mesh.indices.insert(mesh.indices.end(), { 0, point, point, next });
	}

	return mesh;
}

} // namespace geometry
