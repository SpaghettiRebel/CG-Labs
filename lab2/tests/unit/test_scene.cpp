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

TEST_CASE("rotation: positive yaw turns the forward axis right, positive pitch turns it down") {
	CHECK(scene::forwardDirection(0, 0) == test::approx(glm::vec3{0, 0, 1}));
	CHECK(scene::forwardDirection(90, 0) == test::approx(glm::vec3{1, 0, 0}));
	CHECK(scene::forwardDirection(-90, 0) == test::approx(glm::vec3{-1, 0, 0}));
	CHECK(scene::forwardDirection(0, 90) == test::approx(glm::vec3{0, 1, 0})); // y points down

	// Pitch is applied after yaw: looking right and 30 degrees down
	const float c = std::cos(glm::radians(30.0f)), s = std::sin(glm::radians(30.0f));
	CHECK(scene::forwardDirection(90, 30) == test::approx(glm::vec3{c, s, 0}));

	// No scaling, and the right axis stays horizontal (no roll)
	const glm::mat4 r = scene::rotationMatrix(40, -25);
	CHECK(glm::transpose(r) * r == test::approx(glm::mat4(1)));
	CHECK((r * glm::vec4(1, 0, 0, 0)).y == doctest::Approx(0));
}

TEST_CASE("view matrix is the inverse of the camera matrix T(position) * R(yaw, pitch)") {
	scene::Camera camera;
	camera.position = {1, -2, 3};
	camera.yaw = 35;
	camera.pitch = -20;
	const glm::mat4 view = scene::viewMatrix(camera);

	const glm::mat4 camera_matrix =
		glm::translate(glm::mat4(1), camera.position) * scene::rotationMatrix(camera.yaw, camera.pitch);
	CHECK(view == test::approx(glm::inverse(camera_matrix)));

	// The camera is at the view space origin and looks along +z
	CHECK(view * glm::vec4(camera.position, 1) == test::approx(glm::vec4{0, 0, 0, 1}));
	const glm::vec3 ahead = camera.position + 2.0f * scene::forwardDirection(camera.yaw, camera.pitch);
	CHECK(view * glm::vec4(ahead, 1) == test::approx(glm::vec4{0, 0, 2, 1}));
}

TEST_CASE("without rotation the view matrix moves the scene opposite to the camera") {
	scene::Camera camera;
	camera.position = {1, 2, -3};
	CHECK(scene::viewMatrix(camera) * glm::vec4(1, 2, 3, 1) == test::approx(glm::vec4{0, 0, 6, 1}));
}

TEST_CASE("camera around the origin looks at it from the given distance") {
	for (auto [yaw, pitch] : {std::pair{0.0f, 0.0f}, {30.0f, 45.0f}, {-120.0f, -60.0f}}) {
		CAPTURE(yaw);
		CAPTURE(pitch);
		const scene::Camera camera = scene::cameraAround(4, yaw, pitch);
		CHECK(glm::length(camera.position) == doctest::Approx(4));
		CHECK(scene::viewMatrix(camera) * glm::vec4(0, 0, 0, 1) == test::approx(glm::vec4{0, 0, 4, 1}));
	}
	CHECK(scene::cameraAround(3, 0, 0).position == test::approx(glm::vec3{0, 0, -3}));
}

TEST_CASE("positive camera pitch looks at the scene from above") {
	const scene::Camera camera = scene::cameraAround(3, 0, 30);
	const glm::mat4 view = scene::viewMatrix(camera);
	CHECK(camera.position.y < 0); // y points down

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

TEST_CASE("object uniforms: sRGB material colors become linear, vertex colors weight is 0 or 1") {
	scene::Object object;
	object.albedo = {0, 1, 0.5f};
	object.specular = {0.5f, 0.04045f, 1};
	object.shininess = 64;
	object.vertex_colors = false;

	const uniforms::ObjectUniforms data = scene::objectUniforms(object, glm::mat4(1));
	CHECK(data.albedo == test::approx(glm::vec3(0, 1, 0.21404f)));
	CHECK(data.specular == test::approx(glm::vec3(0.21404f, 0.0031308f, 1)));
	CHECK(data.shininess == 64);
	CHECK(data.vertex_colors == 0);

	// Linear segment near black and the power curve meet at 0.04045
	object.albedo = {0.04045f, 0.04f, 0.05f};
	CHECK(scene::objectUniforms(object, glm::mat4(1)).albedo ==
	      test::approx(glm::vec3(0.0031308f, 0.04f / 12.92f, 0.0039359f)));

	object.vertex_colors = true;
	CHECK(scene::objectUniforms(object, glm::mat4(1)).vertex_colors == 1);

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

TEST_CASE("every object slot has its own albedo and trajectory phase") {
	for (int i = 0; i < scene::max_objects; ++i) {
		const scene::Object a = scene::makeObject(i);
		CAPTURE(i);
		CHECK(a.albedo.x >= 0);
		CHECK(a.albedo.x <= 1);
		CHECK(a.trajectory_phase == doctest::Approx(360.0f * float(i) / float(scene::max_objects)));

		for (int k = 0; k < i; ++k) {
			const scene::Object b = scene::makeObject(k);
			CHECK(glm::length(a.albedo - b.albedo) > 0.1f);
		}
	}

	// The first object keeps the defaults
	CHECK(scene::makeObject(0).albedo == test::approx(glm::vec3{1, 1, 1}));
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

TEST_CASE("scene uniforms: camera position, linear light colors times intensity, light direction and counts") {
	scene::Lights lights;
	lights.ambient = {.color = {0.5f, 1, 1}, .intensity = 0.2f};
	lights.directional = {.color = {1, 0.5f, 0}, .intensity = 2, .yaw = 0, .pitch = 90};
	lights.points.resize(3);
	lights.spots.clear();
	scene::Camera camera;
	camera.position = {1, 2, 3};
	const glm::mat4 view_projection = glm::translate(glm::mat4(1), {4, 5, 6});

	const uniforms::SceneUniforms data = scene::sceneUniforms(view_projection, camera, lights);
	CHECK(data.view_projection == test::approx(view_projection));
	CHECK(data.camera_position == test::approx(glm::vec3{1, 2, 3}));
	CHECK(data.ambient_light == test::approx(glm::vec3{0.21404f, 1, 1} * 0.2f));
	CHECK(data.directional_light_color == test::approx(glm::vec3{2, 2 * 0.21404f, 0}));
	CHECK(data.directional_light_direction == test::approx(glm::vec3{0, 1, 0})); // Straight down
	CHECK(data.point_light_count == 3);
	CHECK(data.spot_light_count == 0);

	// Counts never exceed the sizes of the storage buffers
	lights.points.resize(scene::max_point_lights + 1);
	lights.spots.resize(scene::max_spot_lights + 5);
	const uniforms::SceneUniforms full = scene::sceneUniforms(view_projection, camera, lights);
	CHECK(full.point_light_count == scene::max_point_lights);
	CHECK(full.spot_light_count == scene::max_spot_lights);
}

TEST_CASE("point light uniforms: position and linear color times intensity") {
	const scene::PointLight light{.position = {1, -2, 3}, .color = {1, 0.5f, 0}, .intensity = 4};
	const uniforms::PointLight data = scene::pointLightUniforms(light);
	CHECK(data.position == test::approx(glm::vec3{1, -2, 3}));
	CHECK(data.color == test::approx(glm::vec3{4, 4 * 0.21404f, 0}));
}

TEST_CASE("spot light uniforms: direction from yaw and pitch, cosines of the angles") {
	scene::SpotLight light;
	light.position = {1, 2, 3};
	light.color = {1, 1, 1};
	light.intensity = 2;
	light.yaw = 90;
	light.pitch = 0;
	light.inner_angle = 10;
	light.outer_angle = 25;

	const uniforms::SpotLight data = scene::spotLightUniforms(light);
	CHECK(data.position == test::approx(glm::vec3{1, 2, 3}));
	CHECK(data.direction == test::approx(glm::vec3{1, 0, 0}));
	CHECK(data.color == test::approx(glm::vec3{2, 2, 2}));
	CHECK(data.cos_inner == doctest::Approx(std::cos(glm::radians(10.0f))));
	CHECK(data.cos_outer == doctest::Approx(std::cos(glm::radians(25.0f))));

	// Outer angle smaller than the inner one works as equal angles: a hard edge
	light.outer_angle = 5;
	CHECK(scene::spotLightUniforms(light).cos_outer == doctest::Approx(std::cos(glm::radians(10.0f))));
}

TEST_CASE("spot cone: apex at the light, axis along the light direction, side at the outer angle") {
	scene::SpotLight light;
	light.position = {1, -1, 2};
	light.yaw = -40;
	light.pitch = 20;
	light.inner_angle = 5;
	light.outer_angle = 30;
	const float length = 0.8f;

	const glm::mat4 m = scene::spotConeMatrix(light, length);
	const glm::vec3 axis = scene::forwardDirection(light.yaw, light.pitch);
	CHECK(glm::vec3(m * glm::vec4(0, 0, 0, 1)) == test::approx(light.position));
	CHECK(glm::vec3(m * glm::vec4(0, 0, 1, 1)) == test::approx(light.position + length * axis));

	// Points of the base circle of geometry::makeConeLines
	for (float angle : {0.0f, 1.5f, 4.0f}) {
		CAPTURE(angle);
		const glm::vec3 base_point = glm::vec3(m * glm::vec4(std::cos(angle), std::sin(angle), 1, 1));
		const glm::vec3 side = glm::normalize(base_point - light.position);
		CHECK(glm::degrees(std::acos(glm::dot(side, axis))) == doctest::Approx(30).epsilon(1e-3));
	}
}
