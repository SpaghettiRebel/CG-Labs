#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <vector>

#include <glm/gtc/color_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "scene.hpp"
#include "test_helpers.hpp"

using test::project;

TEST_CASE("matrices are stored column by column, like mat4 in GLSL: memcpy to a uniform buffer works") {
	const glm::mat4 m = glm::translate(glm::mat4(1), {7, 8, 9});
	float flat[16];
	static_assert(sizeof(flat) == sizeof(m));
	std::memcpy(flat, &m, sizeof(flat));

	// Translation is the fourth column, i.e. floats 12..14 in memory
	CHECK(flat[12] == 7);
	CHECK(flat[13] == 8);
	CHECK(flat[14] == 9);
	CHECK(flat[3] == 0);
}

TEST_CASE("perspective projection is the matrix from lecture 1") {
	scene::Projection projection;
	projection.type = scene::ProjectionType::perspective;
	projection.vertical_fov = 70;
	projection.z_near = 0.5f;
	projection.z_far = 20;
	const float aspect = 1.5f;

	const float t = std::tan(glm::radians(70.0f) / 2), n = 0.5f, f = 20;
	glm::mat4 expected(0); // Written by rows as on the slide, GLM indexes [column][row]
	expected[0][0] = 1 / (aspect * t);
	expected[1][1] = 1 / t;
	expected[2][2] = f / (f - n);
	expected[3][2] = -f * n / (f - n);
	expected[2][3] = 1;

	CHECK(scene::projectionMatrix(projection, aspect) == test::approx(expected));
}

TEST_CASE("perspective projection maps the frustum to Vulkan canonical volume") {
	scene::Projection projection; // fov 60 degrees
	projection.type = scene::ProjectionType::perspective;
	const float aspect = 2;
	const glm::mat4 m = scene::projectionMatrix(projection, aspect);
	const float tan_half_fov = std::tan(glm::radians(projection.vertical_fov) / 2);

	CHECK(project(m, {0, 0, projection.z_near}).z == doctest::Approx(0));
	CHECK(project(m, {0, 0, projection.z_far}).z == doctest::Approx(1));
	CHECK((m * glm::vec4(1, 2, 7, 1)).w == doctest::Approx(7)); // w is the view depth

	for (float z : {1.0f, 4.0f, 50.0f}) {
		CAPTURE(z);
		// Corner of the frustum at depth z: positive y is the bottom of the screen
		const glm::vec3 corner = project(m, {aspect * z * tan_half_fov, z * tan_half_fov, z});
		CHECK(corner.x == doctest::Approx(1));
		CHECK(corner.y == doctest::Approx(1));
	}

	// Farther objects look smaller, depth grows with distance as depth test LESS expects
	CHECK(project(m, {1, 1, 4}).x == doctest::Approx(project(m, {1, 1, 2}).x / 2));
	CHECK(project(m, {0, 0, 5}).z > project(m, {0, 0, 4}).z);
}

TEST_CASE("perspective projection = orthographic * perspective matrix (lecture 1 slide)") {
	scene::Projection perspective;
	perspective.type = scene::ProjectionType::perspective;
	const float aspect = 16.0f / 9.0f, n = perspective.z_near, f = perspective.z_far;

	// Orthographic volume = the frustum cross-section at the near plane
	scene::Projection ortho = perspective;
	ortho.type = scene::ProjectionType::orthographic;
	ortho.ortho_height = 2 * n * std::tan(glm::radians(perspective.vertical_fov) / 2);

	glm::mat4 squash(0); // "Perspective Matrix" from the slide
	squash[0][0] = n;
	squash[1][1] = n;
	squash[2][2] = f + n;
	squash[3][2] = -f * n;
	squash[2][3] = 1;

	CHECK(scene::projectionMatrix(perspective, aspect) ==
	      test::approx(scene::projectionMatrix(ortho, aspect) * squash));
}

TEST_CASE("orthographic projection shows ortho_height units vertically, centered on the view axis") {
	scene::Projection projection;
	projection.type = scene::ProjectionType::orthographic;
	projection.ortho_height = 4;
	const float aspect = 2;
	const glm::mat4 m = scene::projectionMatrix(projection, aspect);

	CHECK(project(m, {0, 0, 5}).x == doctest::Approx(0));
	CHECK(project(m, {0, 0, 5}).y == doctest::Approx(0));
	CHECK(project(m, {4, 2, 5}).x == doctest::Approx(1)); // Half width = aspect * height / 2
	CHECK(project(m, {4, 2, 5}).y == doctest::Approx(1));  // Positive y is the bottom
	CHECK(project(m, {-4, -2, 5}).x == doctest::Approx(-1));
	CHECK(project(m, {-4, -2, 5}).y == doctest::Approx(-1));
	CHECK(project(m, {0, 0, projection.z_near}).z == doctest::Approx(0));
	CHECK(project(m, {0, 0, projection.z_far}).z == doctest::Approx(1));
}

TEST_CASE("aspect ratio scales only x") {
	for (auto type : {scene::ProjectionType::perspective, scene::ProjectionType::orthographic}) {
		scene::Projection projection;
		projection.type = type;
		const glm::vec3 narrow = project(scene::projectionMatrix(projection, 1), {1, 1, 3});
		const glm::vec3 wide = project(scene::projectionMatrix(projection, 2), {1, 1, 3});

		CHECK(wide.x == doctest::Approx(narrow.x / 2));
		CHECK(wide.y == doctest::Approx(narrow.y));
	}
}

TEST_CASE("camera looks at the origin from the given distance") {
	scene::Camera camera;
	camera.distance = 4;

	for (auto [yaw, pitch] : {std::pair{0.0f, 0.0f}, {30.0f, 45.0f}, {-120.0f, -60.0f}}) {
		camera.yaw = yaw;
		camera.pitch = pitch;
		CHECK(scene::viewMatrix(camera) * glm::vec4(0, 0, 0, 1) == test::approx(glm::vec4{0, 0, 4, 1}));
	}

	// Without rotation the world axes are the view axes
	camera.yaw = 0;
	camera.pitch = 0;
	CHECK(scene::viewMatrix(camera) * glm::vec4(1, 2, 3, 1) == test::approx(glm::vec4{1, 2, 7, 1}));
}

TEST_CASE("positive camera pitch looks at the scene from above") {
	scene::Camera camera;
	camera.pitch = 30;
	const glm::mat4 view = scene::viewMatrix(camera);

	// Top cap normal points up (-y) and must face the camera at the view space origin
	const glm::vec4 top_normal = view * glm::vec4(0, -1, 0, 0);
	const glm::vec4 top_center = view * glm::vec4(0, -0.5f, 0, 1);
	CHECK(-glm::dot(glm::vec3(top_normal), glm::vec3(top_center)) > 0);

	// Top of the object is still above its bottom on screen
	const glm::mat4 view_projection = scene::projectionMatrix({}, 1) * view;
	CHECK(project(view_projection, {0, -0.5f, 0}).y < project(view_projection, {0, 0.5f, 0}).y);
}

TEST_CASE("default transform keeps the object as is") {
	CHECK(scene::modelMatrix(scene::Transform{}) == test::approx(glm::mat4(1)));
}

TEST_CASE("transform scales, then rotates, then moves") {
	scene::Transform transform;
	transform.scale = {2, 1, 1};
	transform.rotation = {0, 0, 90};
	transform.position = {1, 1, 1};

	// (1, 0, 0) -> scale -> (2, 0, 0) -> rotate around z -> (0, 2, 0) -> move -> (1, 3, 1)
	CHECK(scene::modelMatrix(transform) * glm::vec4(1, 0, 0, 1) == test::approx(glm::vec4{1, 3, 1, 1}));
}

TEST_CASE("rotation angles are degrees applied around x, then y, then z") {
	scene::Transform transform;
	transform.rotation = {90, 90, 0};

	// (0, 1, 0) -> around x -> (0, 0, 1) -> around y -> (1, 0, 0)
	CHECK(scene::modelMatrix(transform) * glm::vec4(0, 1, 0, 0) == test::approx(glm::vec4{1, 0, 0, 0}));

	transform.rotation = {0, 180, 0};
	CHECK(scene::modelMatrix(transform) * glm::vec4(1, 0, 0, 0) == test::approx(glm::vec4{-1, 0, 0, 0}));
}

TEST_CASE("scale can't go below the minimum, so the object is never mirrored or collapsed") {
	scene::Transform transform;
	transform.scale = {0, -1, 0.05f};

	const float m = scene::min_scale;
	CHECK(scene::modelMatrix(transform) * glm::vec4(1, 1, 1, 1) == test::approx(glm::vec4{m, m, m, 1}));
}

TEST_CASE("trajectory starts on the right of the object position") {
	scene::Trajectory trajectory;
	CHECK(scene::trajectoryOffset(trajectory, 0, 0) ==
	      test::approx(glm::vec3{trajectory.radius + trajectory.wobble_radius, 0, 0}));
}

TEST_CASE("trajectory is a closed curve: one lap takes lap_time") {
	scene::Trajectory trajectory;
	trajectory.lobes = 7;

	for (float time : {0.0f, 1.3f, 5.9f}) {
		CAPTURE(time);
		CHECK(scene::trajectoryOffset(trajectory, time + trajectory.lap_time, 0) ==
		      test::approx(scene::trajectoryOffset(trajectory, time, 0)));
	}
}

TEST_CASE("trajectory stays within its radii and bob height") {
	scene::Trajectory trajectory;
	trajectory.radius = 1.5f;
	trajectory.wobble_radius = 0.4f;
	trajectory.bob_height = 0.3f;

	bool moved_vertically = false;
	for (float time = 0; time < trajectory.lap_time; time += 0.05f) {
		const glm::vec3 p = scene::trajectoryOffset(trajectory, time, 0);
		CHECK(std::hypot(p.x, p.z) <= 1.9f + 1e-4f);
		CHECK(std::hypot(p.x, p.z) >= 1.1f - 1e-4f);
		CHECK(std::abs(p.y) <= 0.3f + 1e-4f);
		moved_vertically = moved_vertically || std::abs(p.y) > 0.2f;
	}
	CHECK(moved_vertically);
}

TEST_CASE("trajectory phase is a shift along the curve") {
	scene::Trajectory trajectory;
	CHECK(scene::trajectoryOffset(trajectory, 1, 90) ==
	      test::approx(scene::trajectoryOffset(trajectory, 1 + trajectory.lap_time / 4, 0)));
}

TEST_CASE("animated object moves along the trajectory and spins around the vertical axis") {
	scene::Trajectory trajectory;
	trajectory.spin = 90;
	scene::Transform transform;
	transform.position = {1, 2, 3};
	const float time = 1;

	const glm::mat4 model = scene::animatedModelMatrix(transform, trajectory, time, 0);
	const glm::vec3 offset = scene::trajectoryOffset(trajectory, time, 0);

	CHECK(model * glm::vec4(0, 0, 0, 1) == test::approx(glm::vec4(transform.position + offset, 1)));
	// 90 degrees per second around y for one second: x -> -z
	CHECK(model * glm::vec4(1, 0, 0, 0) == test::approx(glm::vec4{0, 0, -1, 0}));

	// Spin is around the world vertical axis even for a rotated object:
	// cylinder axis (0, 1, 0) -> lying down (0, 0, 1) -> spun around the vertical (1, 0, 0)
	transform.rotation = {90, 0, 0};
	CHECK(scene::animatedModelMatrix(transform, trajectory, time, 0) * glm::vec4(0, 1, 0, 0) ==
	      test::approx(glm::vec4{1, 0, 0, 0}));
}

TEST_CASE("animation clock") {
	scene::AnimationClock clock;
	CHECK(clock.time == 0);

	clock.advance(0.05f);
	CHECK(clock.time == doctest::Approx(0.05f));

	SUBCASE("pause stops the time") {
		clock.playing = false;
		clock.advance(0.05f);
		CHECK(clock.time == doctest::Approx(0.05f));
	}

	SUBCASE("speed scales the time, changing it doesn't make a jump") {
		clock.speed = 3;
		CHECK(clock.time == doctest::Approx(0.05f));
		clock.advance(0.01f);
		CHECK(clock.time == doctest::Approx(0.08f));
	}

	SUBCASE("long frames (window dragging, debugger) are limited") {
		clock.advance(5);
		CHECK(clock.time == doctest::Approx(0.05f + scene::max_frame_time));
	}

	SUBCASE("time never goes back") {
		clock.advance(-1);
		CHECK(clock.time == doctest::Approx(0.05f));
	}

	SUBCASE("reset") {
		clock.reset();
		CHECK(clock.time == 0);
	}
}

TEST_CASE("object uniforms: picked sRGB color becomes linear, vertex colors weight is 0 or 1") {
	scene::Object object;
	object.color = {0, 1, 0.5f};
	object.vertex_colors = false;

	const uniforms::ObjectUniforms data = scene::objectUniforms(object, glm::mat4(1));
	CHECK(data.color == test::approx(glm::vec4(0, 1, 0.21404f, 1)));
	CHECK(data.parameters.x == 0);

	// Linear segment near black and the power curve meet at 0.04045
	object.color = {0.04045f, 0.04f, 0.05f};
	CHECK(glm::vec3(scene::objectUniforms(object, glm::mat4(1)).color) ==
	      test::approx(glm::vec3(0.0031308f, 0.04f / 12.92f, 0.0039359f)));

	object.vertex_colors = true;
	CHECK(scene::objectUniforms(object, glm::mat4(1)).parameters.x == 1);

	const glm::mat4 model = glm::translate(glm::mat4(1), {1, 2, 3});
	CHECK(scene::objectUniforms(object, model).model == test::approx(model));
}

TEST_CASE("new objects are placed alternately right and left of the first one") {
	CHECK(scene::makeObject(0).transform.position == test::approx(glm::vec3{0, 0, 0}));
	CHECK(scene::makeObject(1).transform.position == test::approx(glm::vec3{scene::object_spacing, 0, 0}));
	CHECK(scene::makeObject(2).transform.position == test::approx(glm::vec3{-scene::object_spacing, 0, 0}));
	CHECK(scene::makeObject(3).transform.position == test::approx(glm::vec3{2 * scene::object_spacing, 0, 0}));

	// Cylinders of radius 0.5 don't overlap
	CHECK(scene::object_spacing > 1);

	// Interface shows "0.00", not "-0.00"
	CHECK_FALSE(std::signbit(scene::makeObject(0).transform.position.x));
}

TEST_CASE("every object slot has its own color and trajectory phase") {
	for (int i = 0; i < scene::max_objects; ++i) {
		const scene::Object a = scene::makeObject(i);
		CAPTURE(i);
		CHECK(a.color.x >= 0);
		CHECK(a.color.x <= 1);
		CHECK(a.trajectory_phase == doctest::Approx(360.0f * float(i) / float(scene::max_objects)));

		for (int k = 0; k < i; ++k) {
			const scene::Object b = scene::makeObject(k);
			CHECK(glm::length(a.color - b.color) > 0.1f);
		}
	}

	// The first object keeps the defaults
	CHECK(scene::makeObject(0).color == test::approx(glm::vec3{1, 1, 1}));
}

TEST_CASE("added object takes the smallest free slot, so it never duplicates an existing one") {
	std::vector<scene::Object> objects = {scene::makeObject(0), scene::makeObject(1), scene::makeObject(2)};
	CHECK(scene::freeSlot(objects) == 3);

	objects.erase(objects.begin() + 1); // Remove the middle one
	CHECK(scene::freeSlot(objects) == 1);

	objects.clear();
	CHECK(scene::freeSlot(objects) == 0);

	for (int i = 0; i < scene::max_objects; ++i) {
		objects.push_back(scene::makeObject(i));
	}
	CHECK(scene::freeSlot(objects) == -1);
}

TEST_CASE("mouse drag orbits the camera") {
	scene::Camera camera;
	camera.yaw = 0;
	camera.pitch = 0;

	scene::orbitCamera(camera, 100, -50);
	CHECK(camera.yaw == doctest::Approx(100 * scene::orbit_degrees_per_pixel));
	CHECK(camera.pitch == doctest::Approx(-50 * scene::orbit_degrees_per_pixel));

	SUBCASE("pitch stops before looking straight up or down") {
		scene::orbitCamera(camera, 0, 1e6f);
		CHECK(camera.pitch == doctest::Approx(scene::camera_max_pitch));
		scene::orbitCamera(camera, 0, -1e6f);
		CHECK(camera.pitch == doctest::Approx(-scene::camera_max_pitch));
	}

	SUBCASE("yaw wraps around to [-180, 180]") {
		camera.yaw = 170;
		scene::orbitCamera(camera, 30 / scene::orbit_degrees_per_pixel, 0);
		CHECK(camera.yaw == doctest::Approx(-160));
		camera.yaw = -170;
		scene::orbitCamera(camera, -30 / scene::orbit_degrees_per_pixel, 0);
		CHECK(camera.yaw == doctest::Approx(160));
	}
}

TEST_CASE("mouse wheel zooms the camera within limits") {
	scene::Camera camera;
	camera.distance = 4;

	scene::zoomCamera(camera, 1);
	CHECK(camera.distance < 4); // Wheel forward moves closer
	scene::zoomCamera(camera, -1);
	CHECK(camera.distance == doctest::Approx(4));

	scene::zoomCamera(camera, 1000);
	CHECK(camera.distance == doctest::Approx(scene::camera_min_distance));
	scene::zoomCamera(camera, -1000);
	CHECK(camera.distance == doctest::Approx(scene::camera_max_distance));
}

TEST_CASE("camera yaw rotates the scene around the vertical axis") {
	scene::Camera camera;
	camera.pitch = 0;
	camera.yaw = 90;
	const glm::vec4 p = scene::viewMatrix(camera) * glm::vec4(1, 0.5f, 0, 1);

	CHECK(p.y == doctest::Approx(0.5f));
	CHECK(glm::length(glm::vec2(p.x, p.z - camera.distance)) == doctest::Approx(1));
	CHECK(p.x == doctest::Approx(0).epsilon(1e-5));
}
