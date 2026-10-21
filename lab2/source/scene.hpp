#pragma once

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

#include "uniforms.hpp"

// Scene logic without Vulkan: camera, projections, objects, animation, lights
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

// Turns the forward axis +z to the direction given by yaw (around the vertical axis, positive turns right)
// and pitch (positive turns down), both in degrees
glm::mat4 rotationMatrix(float yaw, float pitch);

glm::vec3 forwardDirection(float yaw, float pitch);

struct Camera {
	glm::vec3 position{ 0, 0, -3 };
	float yaw = 0;   // Degrees
	float pitch = 0; // Degrees
};

// Camera at the distance from the origin, looking at it
Camera cameraAround(float distance, float yaw, float pitch);

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

// Colors are sRGB, as chosen in the color picker
struct Object {
	Transform transform;
	glm::vec3 albedo{ 1, 1, 1 };
	bool vertex_colors = false; // Multiply albedo by procedural vertex colors
	glm::vec3 specular{ 0.5f, 0.5f, 0.5f };
	float shininess = 32;
	float trajectory_phase = 0; // Degrees, start point on the trajectory
	int slot = 0;               // Index of the default place, color and phase, see makeObject
};

inline constexpr int max_objects = 8;
inline constexpr float object_spacing = 1.5f;

Object makeObject(int index);

int freeSlot(const std::vector<Object>& objects);

uniforms::ObjectUniforms objectUniforms(const Object& object, const glm::mat4& model);

// Lights. Colors are sRGB, radiance = linear color * intensity (/ distance^2 for point and spot lights)

struct AmbientLight {
	glm::vec3 color{ 1, 1, 1 };
	float intensity = 0.05f;
};

struct DirectionalLight {
	glm::vec3 color{ 1, 1, 1 };
	float intensity = 0.6f;
	float yaw = 30; // Direction of the light, as for the camera
	float pitch = 50;
};

struct PointLight {
	glm::vec3 position{ 0.8f, -0.4f, -0.9f };
	glm::vec3 color{ 1, 0.6f, 0.3f };
	float intensity = 1;
};

struct SpotLight {
	glm::vec3 position{ -0.55f, -1, -1.4f };
	glm::vec3 color{ 0.5f, 0.8f, 1 };
	float intensity = 2;
	float yaw = 30; // Direction of the axis, as for the camera
	float pitch = 35;
	float inner_angle = 8; // Degrees from the axis: full light inside, fades out up to the outer angle
	float outer_angle = 16;
};

// Sizes of the storage buffers. Shaders have no limit, they take the counts from SceneUniforms
inline constexpr size_t max_point_lights = 16;
inline constexpr size_t max_spot_lights = 16;

struct Lights {
	AmbientLight ambient;
	DirectionalLight directional;
	std::vector<PointLight> points{ PointLight{} };
	std::vector<SpotLight> spots{ SpotLight{} };
};

uniforms::SceneUniforms sceneUniforms(const glm::mat4& view_projection, const Camera& camera, const Lights& lights);

uniforms::PointLight pointLightUniforms(const PointLight& light);

uniforms::SpotLight spotLightUniforms(const SpotLight& light);

// Places the cone of geometry::makeConeLines: apex at the light, axis along its direction,
// angle between the axis and the side = outer angle
glm::mat4 spotConeMatrix(const SpotLight& light, float length);

// aspect = width / height of the image
glm::mat4 projectionMatrix(const Projection& projection, float aspect);

// Inverse of the camera matrix T(position) * R(yaw, pitch)
glm::mat4 viewMatrix(const Camera& camera);

} // namespace scene
