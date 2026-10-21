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

// Light color as the shader needs it
glm::vec3 radiance(glm::vec3 color, float intensity) {
	return glm::convertSRGBToLinear(color) * intensity;
}

} // namespace

glm::mat4 rotationMatrix(float yaw, float pitch) {
	// Positive pitch turns +z to +y (down): rotation around x by -pitch
	const glm::mat4 rotation = glm::rotate(glm::mat4(1), glm::radians(yaw), y_axis);
	return glm::rotate(rotation, glm::radians(-pitch), x_axis);
}

glm::vec3 forwardDirection(float yaw, float pitch) {
	return glm::vec3(rotationMatrix(yaw, pitch) * glm::vec4(z_axis, 0));
}

Camera cameraAround(float distance, float yaw, float pitch) {
	return {
		.position = -distance * forwardDirection(yaw, pitch),
		.yaw = yaw,
		.pitch = pitch,
	};
}

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
	object.albedo = palette[index];
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
		.albedo = glm::convertSRGBToLinear(object.albedo),
		.vertex_colors = object.vertex_colors ? 1.0f : 0.0f,
		.specular = glm::convertSRGBToLinear(object.specular),
		.shininess = object.shininess,
	};
}

uniforms::SceneUniforms sceneUniforms(const glm::mat4& view_projection, const Camera& camera, const Lights& lights) {
	return {
		.view_projection = view_projection,
		.camera_position = camera.position,
		.point_light_count = uint32_t(std::min(lights.points.size(), max_point_lights)),
		.directional_light_direction = forwardDirection(lights.directional.yaw, lights.directional.pitch),
		.spot_light_count = uint32_t(std::min(lights.spots.size(), max_spot_lights)),
		.directional_light_color = radiance(lights.directional.color, lights.directional.intensity),
		.ambient_light = radiance(lights.ambient.color, lights.ambient.intensity),
	};
}

uniforms::PointLight pointLightUniforms(const PointLight& light) {
	return {
		.position = light.position,
		.color = radiance(light.color, light.intensity),
	};
}

uniforms::SpotLight spotLightUniforms(const SpotLight& light) {
	// Outer angle smaller than the inner one would make no fade at all, same as equal angles
	return {
		.position = light.position,
		.cos_inner = std::cos(glm::radians(light.inner_angle)),
		.direction = forwardDirection(light.yaw, light.pitch),
		.cos_outer = std::cos(glm::radians(std::max(light.outer_angle, light.inner_angle))),
		.color = radiance(light.color, light.intensity),
	};
}

glm::mat4 spotConeMatrix(const SpotLight& light, float length) {
	const float radius = length * std::tan(glm::radians(std::max(light.outer_angle, light.inner_angle)));
	const glm::mat4 placed = glm::translate(glm::mat4(1), light.position) * rotationMatrix(light.yaw, light.pitch);
	return glm::scale(placed, { radius, radius, length });
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
	// (T * R)^-1 = R^-1 * T^-1: inverse rotation is the transposed one, inverse translation moves back
	return glm::transpose(rotationMatrix(camera.yaw, camera.pitch)) * glm::translate(glm::mat4(1), -camera.position);
}

} // namespace scene
