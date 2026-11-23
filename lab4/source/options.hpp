#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

// Startup options from LAB1_* environment variables, used by automated tests
// to get a reproducible scene. Unset variables keep the application defaults.
namespace options {

enum class Projection {
	perspective,
	orthographic,
};

// Default lights to keep, the others are removed (directional: intensity 0)
struct LightSet {
	bool directional = false;
	bool point = false;
	bool spot = false;
};

struct StartupOptions {
	std::optional<Projection> projection; // LAB1_PROJECTION = perspective | orthographic
	std::optional<bool> animate;          // LAB1_ANIMATE = 0 | 1
	std::optional<int> objects;           // LAB1_OBJECTS = positive integer
	std::optional<int> material;          // LAB1_MATERIAL = index of the material for all objects
	std::optional<bool> animated_sampling; // LAB1_ANIMATED_SAMPLING = 0 | 1, for all objects
	std::optional<float> sampling_time;   // LAB1_SAMPLING_TIME = seconds, stops the time of animated sampling
	std::optional<float> camera_pitch;    // LAB1_CAMERA_PITCH = degrees, camera at distance 3 looking at the origin
	std::optional<LightSet> lights;       // LAB1_LIGHTS = none | comma separated directional, point, spot
	std::optional<bool> show_lights;      // LAB1_SHOW_LIGHTS = 0 | 1
	bool hide_ui = false;                 // LAB1_HIDE_UI = 0 | 1

	std::vector<std::string> errors; // One message per invalid variable
};

// get_env returns variable value or nullptr, like std::getenv
StartupOptions parseStartupOptions(const std::function<const char*(const char*)>& get_env);

} // namespace options
