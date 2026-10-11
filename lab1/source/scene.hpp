#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "uniforms.hpp"

// Scene logic without Vulkan: camera, projections, objects, animation
namespace scene {

enum class ProjectionType {
	perspective,
	orthographic,
};

struct Projection {
	ProjectionType type = ProjectionType::perspective;
	float vertical_fov = 60; // Degrees, perspective only
	float ortho_height = 3;  // World units visible vertically, orthographic only
	float z_near = 0.1f;
	float z_far = 100;
};

// Orbits around the origin: yaw turns around the vertical axis, positive pitch looks from above
struct Camera {
	float distance = 3;
	float yaw = 0;    // Degrees
	float pitch = 25; // Degrees
};

inline constexpr float min_scale = 0.1f;

struct Transform {
	glm::vec3 position{ 0, 0, 0 };
	glm::vec3 rotation{ 0, 0, 0 }; // Degrees around x, then y, then z
	glm::vec3 scale{ 1, 1, 1 };
};

glm::mat4 modelMatrix(const Transform& transform, float spin = 0);

// Epicycloid-like "flower" in the horizontal plane around the object position:
// a small circle rolls along a big one, plus vertical oscillation
struct Trajectory {
	float radius = 1;            // Big circle
	float wobble_radius = 0.25f; // Small circle
	int lobes = 5;               // Turns of the small circle per lap, i.e. petals of the flower
	float bob_height = 0.25f;    // Amplitude of vertical oscillation
	float lap_time = 8;          // Seconds per lap at speed 1
	float spin = 90;             // Rotation around the vertical axis, degrees per second
};

glm::vec3 trajectoryOffset(const Trajectory& trajectory, float time, float phase);

glm::mat4 animatedModelMatrix(const Transform& transform, const Trajectory& trajectory, float time, float phase);

inline constexpr float max_frame_time = 0.1f;

struct AnimationClock {
	bool playing = true;
	float speed = 1;
	float time = 0;

	void advance(float dt);
	void reset();
};

struct Object {
	Transform transform;
	glm::vec3 color{ 1, 1, 1 }; // sRGB, as chosen in the color picker
	bool vertex_colors = true;  // Multiply color by procedural vertex colors
	float trajectory_phase = 0; // Degrees, start point on the trajectory
	int slot = 0;               // Index of the default place, color and phase, see makeObject
};

inline constexpr int max_objects = 8;
inline constexpr float object_spacing = 1.5f;

Object makeObject(int index);

int freeSlot(const std::vector<Object>& objects);

uniforms::ObjectUniforms objectUniforms(const Object& object, const glm::mat4& model);

inline constexpr float camera_min_distance = 1.5f;
inline constexpr float camera_max_distance = 15;
inline constexpr float camera_max_pitch = 89;
inline constexpr float orbit_degrees_per_pixel = 0.3f;

// aspect = width / height of the image
glm::mat4 projectionMatrix(const Projection& projection, float aspect);

glm::mat4 viewMatrix(const Camera& camera);

void orbitCamera(Camera& camera, float dx, float dy);

void zoomCamera(Camera& camera, float wheel_steps);

} // namespace scene
