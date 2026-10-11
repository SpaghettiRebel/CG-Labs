#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

// Startup options from LAB1_* environment variables, used by automated tests
// to get a reproducible scene. Unset variables keep the application defaults.
namespace options {

enum class Projection {
	perspective,
	orthographic,
};

struct StartupOptions {
	std::optional<Projection> projection; // LAB1_PROJECTION = perspective | orthographic
	std::optional<bool> animate;          // LAB1_ANIMATE = 0 | 1
	std::optional<int> objects;           // LAB1_OBJECTS = positive integer
	std::optional<glm::vec3> color;       // LAB1_COLOR = r,g,b in sRGB, each in [0, 1]
	std::optional<bool> vertex_colors;    // LAB1_VERTEX_COLORS = 0 | 1
	std::optional<float> camera_pitch;    // LAB1_CAMERA_PITCH = degrees
	bool hide_ui = false;                 // LAB1_HIDE_UI = 0 | 1

	std::vector<std::string> errors; // One message per invalid variable
};

// get_env returns variable value or nullptr, like std::getenv
StartupOptions parseStartupOptions(const std::function<const char*(const char*)>& get_env);

} // namespace options
