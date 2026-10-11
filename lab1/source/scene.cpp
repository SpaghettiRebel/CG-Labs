#include "scene.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/color_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace scene {

namespace {

constexpr glm::vec3 x_axis{ 1, 0, 0 };
constexpr glm::vec3 y_axis{ 0, 1, 0 };
constexpr glm::vec3 z_axis{ 0, 0, 1 };

} // namespace

glm::mat4 modelMatrix(const Transform& transform, float spin) {
	glm::mat4 m = glm::translate(glm::mat4(1), transform.position);
	m = glm::rotate(m, glm::radians(spin), y_axis);
	m = glm::rotate(m, glm::radians(transform.rotation.z), z_axis);
	m = glm::rotate(m, glm::radians(transform.rotation.y), y_axis);
	m = glm::rotate(m, glm::radians(transform.rotation.x), x_axis);
	return glm::scale(m, glm::max(transform.scale, glm::vec3(min_scale)));
}

glm::vec3 trajectoryOffset(const Trajectory& trajectory, float time, float phase) {
	const float angle = glm::two_pi<float>() * time / trajectory.lap_time + glm::radians(phase);
	const float wobble_angle = float(trajectory.lobes) * angle;

	return {
		trajectory.radius * std::cos(angle) + trajectory.wobble_radius * std::cos(wobble_angle),
		trajectory.bob_height * std::sin(2 * angle),
		trajectory.radius * std::sin(angle) + trajectory.wobble_radius * std::sin(wobble_angle),
	};
}

glm::mat4 animatedModelMatrix(const Transform& transform, const Trajectory& trajectory, float time, float phase) {
	Transform moved = transform;
	moved.position += trajectoryOffset(trajectory, time, phase);
	return modelMatrix(moved, trajectory.spin * time);
}

void AnimationClock::advance(float dt) {
	if (playing) {
		time += std::clamp(dt, 0.0f, max_frame_time) * speed;
	}
}

void AnimationClock::reset() {
	time = 0;
}

Object makeObject(int index) {
	// sRGB
	static constexpr glm::vec3 palette[max_objects] = {
		{ 1, 1, 1 },
		{ 1, 0.55f, 0.35f },
		{ 0.4f, 0.8f, 1 },
		{ 0.55f, 1, 0.45f },
		{ 1, 0.45f, 0.75f },
		{ 1, 0.9f, 0.35f },
		{ 0.7f, 0.55f, 1 },
		{ 0.4f, 1, 0.85f },
	};

	index = std::clamp(index, 0, max_objects - 1);

	const int step = (index + 1) / 2;
	const int side = index % 2 == 1 ? 1 : -1;

	Object object;
	object.transform.position = { float(side * step) * object_spacing, 0, 0 };
	object.color = palette[index];
	object.trajectory_phase = 360.0f * float(index) / float(max_objects);
	object.slot = index;
	return object;
}

int freeSlot(const std::vector<Object>& objects) {
	for (int slot = 0; slot < max_objects; ++slot) {
		const bool used = std::any_of(objects.begin(), objects.end(),
		                              [slot](const Object& object) {
										  return object.slot == slot;
									  });
		if (!used) {
			return slot;
		}
	}
	return -1;
}

uniforms::ObjectUniforms objectUniforms(const Object& object, const glm::mat4& model) {
	return {
		.model = model,
		.color = glm::vec4(glm::convertSRGBToLinear(object.color), 1),
		.parameters = { object.vertex_colors ? 1.0f : 0.0f, 0, 0, 0 },
	};
}

void orbitCamera(Camera& camera, float dx, float dy) {
	camera.yaw = std::remainder(camera.yaw + dx * orbit_degrees_per_pixel, 360.0f);
	camera.pitch = std::clamp(camera.pitch + dy * orbit_degrees_per_pixel, -camera_max_pitch, camera_max_pitch);
}

void zoomCamera(Camera& camera, float wheel_steps) {
	camera.distance = std::clamp(camera.distance * std::pow(0.9f, wheel_steps),
	                             camera_min_distance, camera_max_distance);
}

glm::mat4 projectionMatrix(const Projection& projection, float aspect) {
	if (projection.type == ProjectionType::orthographic) {
		// GLM takes y range as (min, max). y points down, so min y is the top of the screen
		const float half_height = projection.ortho_height / 2;
		const float half_width = aspect * half_height;
		return glm::orthoLH_ZO(-half_width, half_width, -half_height, half_height,
		                       projection.z_near, projection.z_far);
	}

	return glm::perspectiveLH_ZO(glm::radians(projection.vertical_fov), aspect,
	                             projection.z_near, projection.z_far);
}

glm::mat4 viewMatrix(const Camera& camera) {
	// Rotate the world around the origin, then move it in front of the camera
	glm::mat4 view = glm::translate(glm::mat4(1), { 0, 0, camera.distance });
	view = glm::rotate(view, glm::radians(camera.pitch), x_axis);
	return glm::rotate(view, glm::radians(camera.yaw), y_axis);
}

} // namespace scene
