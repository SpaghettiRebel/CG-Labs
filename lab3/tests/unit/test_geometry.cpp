#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <tuple>
#include <utility>

#include <glm/gtc/constants.hpp>

#include "geometry.hpp"
#include "scene.hpp"
#include "test_helpers.hpp"

using geometry::Mesh;
using glm::vec3;

namespace {

constexpr uint32_t segments = 50;
constexpr float radius = 0.5f;
constexpr float height = 1.0f;

const Mesh& cylinder() {
	static const Mesh mesh = geometry::makeCylinder(segments, radius, height);
	return mesh;
}

struct Triangle {
	vec3 a, b, c;
};

Triangle triangle(const Mesh& mesh, size_t index) {
	return {
		mesh.vertices[mesh.indices[3 * index + 0]].position,
		mesh.vertices[mesh.indices[3 * index + 1]].position,
		mesh.vertices[mesh.indices[3 * index + 2]].position,
	};
}

size_t triangleCount(const Mesh& mesh) {
	return mesh.indices.size() / 3;
}

// Normal of the triangle plane pointing out of a convex mesh around the origin, independent of winding
vec3 outwardNormal(const Triangle& t) {
	const vec3 normal = glm::normalize(glm::cross(t.b - t.a, t.c - t.a));
	const vec3 center = (t.a + t.b + t.c) / 3.0f;
	return glm::dot(normal, center) < 0 ? -normal : normal;
}

// Indices where vertices with the same position have the same id: cap and side vertices of a rim point become one
std::vector<uint32_t> weldedIndices(const Mesh& mesh) {
	std::map<std::tuple<float, float, float>, uint32_t> ids;
	std::vector<uint32_t> result;
	for (uint32_t index : mesh.indices) {
		const vec3 p = mesh.vertices[index].position;
		result.push_back(ids.try_emplace({p.x, p.y, p.z}, uint32_t(ids.size())).first->second);
	}
	return result;
}

} // namespace

TEST_CASE("cylinder has 50 points on each base rim with cap and side vertices (one more column for the seam), plus base centers") {
	const Mesh& mesh = cylinder();

	CHECK(mesh.vertices.size() == 4 * segments + 4);
	CHECK(mesh.indices.size() == 3 * 4 * segments); // Two caps and a side of 2 triangles per segment

	size_t top = 0, bottom = 0;
	for (const auto& vertex : mesh.vertices) {
		top += vertex.position.y == -height / 2;
		bottom += vertex.position.y == height / 2;
	}
	CHECK(top == 2 * segments + 2);
	CHECK(bottom == 2 * segments + 2);

	const std::vector<uint32_t> welded = weldedIndices(mesh);
	CHECK(*std::max_element(welded.begin(), welded.end()) + 1 == 2 * segments + 2);
}

TEST_CASE("segment count is a parameter") {
	const Mesh mesh = geometry::makeCylinder(8, 1, 2);
	CHECK(mesh.vertices.size() == 36);
	CHECK(mesh.indices.size() == 96);
}

TEST_CASE("rim vertices lie on circles of the given radius around the y axis") {
	for (const auto& vertex : cylinder().vertices) {
		const float distance = std::hypot(vertex.position.x, vertex.position.z);
		const bool is_center = distance < 1e-6f;
		if (!is_center) {
			CHECK(distance == doctest::Approx(radius));
		}
	}
}

TEST_CASE("bounding box is inside [-R, R] x [-H/2, H/2] x [-R, R]") {
	vec3 low(1e9f), high(-1e9f);
	for (const auto& vertex : cylinder().vertices) {
		low = glm::min(low, vertex.position);
		high = glm::max(high, vertex.position);
	}

	// Angles 0 and 180 degrees are rim vertices, so x reaches the radius exactly
	CHECK(low.x == doctest::Approx(-radius));
	CHECK(high.x == doctest::Approx(radius));
	CHECK(low.y == doctest::Approx(-height / 2));
	CHECK(high.y == doctest::Approx(height / 2));

	// 90 degrees is between rim vertices when segments = 50: z is within half a segment of the radius
	const float min_extent = radius * std::cos(glm::pi<float>() / segments);
	CHECK(high.z <= radius);
	CHECK(high.z >= min_extent);
	CHECK(low.z >= -radius);
	CHECK(low.z <= -min_extent);
}

TEST_CASE("indices are valid and triangles are not degenerate") {
	const Mesh& mesh = cylinder();

	for (uint32_t index : mesh.indices) {
		REQUIRE(index < mesh.vertices.size());
	}
	for (size_t i = 0; i < triangleCount(mesh); ++i) {
		const Triangle t = triangle(mesh, i);
		CAPTURE(i);
		CHECK(glm::length(glm::cross(t.b - t.a, t.c - t.a)) > 1e-6f);
	}
}

TEST_CASE("meshes are closed and consistently oriented") {
	for (const Mesh& mesh : {cylinder(), geometry::makeCube()}) {
		CAPTURE(mesh.vertices.size());

		// Every directed edge a -> b is used once, and its opposite b -> a is used once too
		const std::vector<uint32_t> indices = weldedIndices(mesh);
		std::map<std::pair<uint32_t, uint32_t>, int> edges;
		for (size_t i = 0; i < indices.size(); i += 3) {
			for (size_t k = 0; k < 3; ++k) {
				edges[{indices[i + k], indices[i + (k + 1) % 3]}] += 1;
			}
		}

		for (const auto& [edge, count] : edges) {
			CAPTURE(edge.first);
			CAPTURE(edge.second);
			CHECK(count == 1);
			CHECK(edges.count({edge.second, edge.first}) == 1);
		}
	}
}

TEST_CASE("cylinder normals have unit length and point outward: vertical on caps, radial on the side") {
	for (const auto& vertex : cylinder().vertices) {
		const vec3 p = vertex.position;
		const vec3 n = vertex.normal;
		CAPTURE(p);
		CAPTURE(n);
		CHECK(glm::length(n) == doctest::Approx(1));
		CHECK(glm::dot(n, p) > 0);

		const bool is_cap = n.y != 0;
		if (is_cap) {
			CHECK(n == test::approx(vec3{0, p.y < 0 ? -1.0f : 1.0f, 0}));
		} else {
			CHECK(n == test::approx(vec3{p.x, 0, p.z} / radius));
		}
	}
}

TEST_CASE("vertex normals agree with the winding: (b - a) x (c - a) points inside") {
	const Mesh& mesh = cylinder();
	for (size_t i = 0; i < triangleCount(mesh); ++i) {
		const Triangle t = triangle(mesh, i);
		const vec3 inside = glm::normalize(glm::cross(t.b - t.a, t.c - t.a));
		CAPTURE(i);
		for (size_t k = 0; k < 3; ++k) {
			// Side faces are flat, vertex normals are 180 / segments degrees away from the face normal
			CHECK(glm::dot(mesh.vertices[mesh.indices[3 * i + k]].normal, -inside) > 0.99f);
		}
	}
}

TEST_CASE("triangles facing the camera are clockwise on screen, others are counterclockwise") {
	// Same rule as the pipeline: VK_FRONT_FACE_CLOCKWISE + VK_CULL_MODE_BACK_BIT in y-down screen space
	const glm::mat4 projection = scene::projectionMatrix({}, 16.0f / 9.0f);

	for (const Mesh& mesh : {cylinder(), geometry::makeCube()}) {
		for (const auto& [pitch, yaw] : {std::pair{30.0f, 0.0f}, {-45.0f, 70.0f}, {0.0f, 200.0f}, {80.0f, -30.0f}}) {
			CAPTURE(mesh.vertices.size());
			CAPTURE(pitch);
			CAPTURE(yaw);
			const glm::mat4 view = scene::viewMatrix(scene::cameraAround(3, yaw, pitch));

			size_t facing_count = 0, away_count = 0;
			for (size_t i = 0; i < triangleCount(mesh); ++i) {
				const Triangle t = triangle(mesh, i);

				// Camera is at the view space origin
				const glm::vec4 center = view * glm::vec4((t.a + t.b + t.c) / 3.0f, 1);
				const glm::vec4 normal = view * glm::vec4(outwardNormal(t), 0);
				const float facing = -glm::dot(vec3(normal), vec3(center));
				if (std::abs(facing) < 1e-3f) {
					continue; // Seen edge-on
				}

				const vec3 a = test::project(projection * view, t.a);
				const vec3 b = test::project(projection * view, t.b);
				const vec3 c = test::project(projection * view, t.c);
				const float clockwise_area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);

				CAPTURE(i);
				CHECK((facing > 0) == (clockwise_area > 0));
				(facing > 0 ? facing_count : away_count) += 1;
			}

			CHECK(facing_count > 0);
			CHECK(away_count > 0);
		}
	}
}

TEST_CASE("cube has edge 1 and is centered at the origin") {
	const Mesh cube = geometry::makeCube();
	CHECK(cube.vertices.size() == 8);
	CHECK(cube.indices.size() == 36);
	for (const auto& vertex : cube.vertices) {
		CHECK(glm::abs(vertex.position) == test::approx(vec3{0.5f, 0.5f, 0.5f}));
	}
}

TEST_CASE("cone lines go from the apex at the origin to the base circle of radius 1 at z = 1, and along it") {
	const uint32_t cone_segments = 16;
	const Mesh cone = geometry::makeConeLines(cone_segments);

	REQUIRE(cone.vertices.size() == cone_segments + 1);
	CHECK(cone.vertices[0].position == test::approx(vec3{0, 0, 0}));
	for (size_t i = 1; i < cone.vertices.size(); ++i) {
		const vec3 p = cone.vertices[i].position;
		CHECK(p.z == doctest::Approx(1));
		CHECK(std::hypot(p.x, p.y) == doctest::Approx(1));
	}

	// Apex has a line to every base point, a base point has lines to the apex and to two neighbors
	REQUIRE(cone.indices.size() == 4 * cone_segments);
	std::map<uint32_t, int> lines_per_vertex;
	for (size_t i = 0; i < cone.indices.size(); i += 2) {
		const uint32_t a = cone.indices[i], b = cone.indices[i + 1];
		REQUIRE(a < cone.vertices.size());
		REQUIRE(b < cone.vertices.size());
		CHECK(a != b);
		lines_per_vertex[a] += 1;
		lines_per_vertex[b] += 1;
	}
	CHECK(lines_per_vertex[0] == int(cone_segments));
	for (uint32_t i = 1; i <= cone_segments; ++i) {
		CHECK(lines_per_vertex[i] == 3);
	}
}

TEST_CASE("side texture coordinates: u goes around the side and repeats, v goes from the top to the bottom") {
	for (const auto& vertex : cylinder().vertices) {
		const bool is_side = vertex.normal.y == 0;
		if (!is_side) {
			continue;
		}
		CAPTURE(vertex.position);
		CHECK(vertex.uv.y == doctest::Approx(vertex.position.y < 0 ? 0 : 1));
		CHECK(vertex.uv.x >= 0);
		CHECK(vertex.uv.x <= geometry::side_texture_repeats);

		// u is proportional to the angle around the axis, except the last column at the seam
		if (vertex.uv.x < geometry::side_texture_repeats) {
			float angle = std::atan2(vertex.position.z, vertex.position.x);
			if (angle < -1e-3f) {
				angle += glm::two_pi<float>();
			}
			CHECK(vertex.uv.x == doctest::Approx(geometry::side_texture_repeats * angle / glm::two_pi<float>()).epsilon(1e-3));
		}
	}
}

TEST_CASE("side seam: first and last columns are at the same place with u = 0 and u = repeats") {
	std::map<float, int> first_column, last_column;
	for (const auto& vertex : cylinder().vertices) {
		if (vertex.normal.y != 0) {
			continue;
		}
		if (vertex.uv.x == 0) {
			first_column[vertex.position.y] += 1;
			CHECK(vertex.position == test::approx(vec3{radius, vertex.position.y, 0}));
		}
		if (vertex.uv.x == doctest::Approx(geometry::side_texture_repeats)) {
			last_column[vertex.position.y] += 1;
			CHECK(vertex.position == test::approx(vec3{radius, vertex.position.y, 0}));
		}
	}
	CHECK(first_column.size() == 2);
	CHECK(last_column.size() == 2);
}

TEST_CASE("side triangles never jump across the texture: u differs by one segment") {
	const Mesh& mesh = cylinder();
	const float step = geometry::side_texture_repeats / segments;
	for (size_t i = 0; i < mesh.indices.size(); i += 3) {
		const auto& a = mesh.vertices[mesh.indices[i]];
		const auto& b = mesh.vertices[mesh.indices[i + 1]];
		const auto& c = mesh.vertices[mesh.indices[i + 2]];
		if (a.normal.y != 0) {
			continue;
		}
		CAPTURE(i);
		CHECK(std::abs(a.uv.x - b.uv.x) <= step * 1.001f);
		CHECK(std::abs(b.uv.x - c.uv.x) <= step * 1.001f);
		CHECK(std::abs(a.uv.x - c.uv.x) <= step * 1.001f);
	}
}

TEST_CASE("cap texture coordinates: a circle inscribed in the texture, the center in the middle") {
	for (const auto& vertex : cylinder().vertices) {
		if (vertex.normal.y == 0) {
			continue;
		}
		CAPTURE(vertex.position);
		const glm::vec2 expected = glm::vec2(0.5f) + glm::vec2(vertex.position.x, vertex.position.z);
		CHECK(vertex.uv.x == doctest::Approx(expected.x));
		CHECK(vertex.uv.y == doctest::Approx(expected.y));
	}
}

TEST_CASE("vertex layout matches pipeline vertex attributes") {
	static_assert(sizeof(geometry::Vertex) == 8 * sizeof(float));
	static_assert(offsetof(geometry::Vertex, position) == 0);
	static_assert(offsetof(geometry::Vertex, normal) == 3 * sizeof(float));
	static_assert(offsetof(geometry::Vertex, uv) == 6 * sizeof(float));
}
