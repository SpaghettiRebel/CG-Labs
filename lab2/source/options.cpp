#include "options.hpp"

#include <charconv>
#include <string_view>

namespace options {

namespace {

// Whole string must be a number, "3x" or "" are rejected
template <typename T>
std::optional<T> parseNumber(std::string_view text) {
	T value{};
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (error != std::errc() || end != text.data() + text.size()) {
		return std::nullopt;
	}
	return value;
}

std::optional<bool> parseFlag(std::string_view text) {
	if (text == "0") {
		return false;
	}
	if (text == "1") {
		return true;
	}
	return std::nullopt;
}

std::optional<Projection> parseProjection(std::string_view text) {
	if (text == "perspective") {
		return Projection::perspective;
	}
	if (text == "orthographic") {
		return Projection::orthographic;
	}
	return std::nullopt;
}

std::optional<int> parseObjects(std::string_view text) {
	const auto value = parseNumber<int>(text);
	if (!value || *value < 1) {
		return std::nullopt;
	}
	return value;
}

std::optional<glm::vec3> parseColor(std::string_view text) {
	glm::vec3 color{};

	for (glm::length_t i = 0; i < color.length(); ++i) {
		const size_t comma = text.find(',');
		const bool is_last = i + 1 == color.length();

		// Exactly two commas: the last component must not have one
		if ((comma == std::string_view::npos) != is_last) {
			return std::nullopt;
		}

		const auto value = parseNumber<float>(text.substr(0, comma));
		if (!value || *value < 0.0f || *value > 1.0f) {
			return std::nullopt;
		}

		color[i] = *value;
		text.remove_prefix(is_last ? text.size() : comma + 1);
	}

	return color;
}

std::optional<LightSet> parseLights(std::string_view text) {
	LightSet lights;
	if (text == "none") {
		return lights;
	}

	while (true) {
		const size_t comma = text.find(',');
		const std::string_view name = text.substr(0, comma);
		if (name == "directional") {
			lights.directional = true;
		} else if (name == "point") {
			lights.point = true;
		} else if (name == "spot") {
			lights.spot = true;
		} else {
			return std::nullopt;
		}

		if (comma == std::string_view::npos) {
			return lights;
		}
		text.remove_prefix(comma + 1);
	}
}

// Parses variable if it is set, invalid value is reported and ignored
template <typename T, typename Parser>
void parseVariable(const std::function<const char*(const char*)>& get_env, const char* name,
                   const char* expected, Parser parser, std::optional<T>& result,
                   std::vector<std::string>& errors) {
	const char* value = get_env(name);
	if (value == nullptr) {
		return;
	}

	result = parser(value);
	if (!result) {
		errors.push_back(std::string(name) + "=\"" + value + "\" is invalid, expected " + expected);
	}
}

} // namespace

StartupOptions parseStartupOptions(const std::function<const char*(const char*)>& get_env) {
	StartupOptions result;

	parseVariable(get_env, "LAB1_PROJECTION", "perspective or orthographic",
	              parseProjection, result.projection, result.errors);
	parseVariable(get_env, "LAB1_ANIMATE", "0 or 1", parseFlag, result.animate, result.errors);
	parseVariable(get_env, "LAB1_OBJECTS", "positive integer", parseObjects, result.objects, result.errors);
	parseVariable(get_env, "LAB1_COLOR", "r,g,b with values in [0, 1]", parseColor, result.color, result.errors);
	parseVariable(get_env, "LAB1_VERTEX_COLORS", "0 or 1", parseFlag, result.vertex_colors, result.errors);
	parseVariable(get_env, "LAB1_CAMERA_PITCH", "number of degrees",
	              parseNumber<float>, result.camera_pitch, result.errors);

	parseVariable(get_env, "LAB1_LIGHTS", "none or comma separated directional, point, spot",
	              parseLights, result.lights, result.errors);
	parseVariable(get_env, "LAB1_SHOW_LIGHTS", "0 or 1", parseFlag, result.show_lights, result.errors);

	std::optional<bool> hide_ui;
	parseVariable(get_env, "LAB1_HIDE_UI", "0 or 1", parseFlag, hide_ui, result.errors);
	result.hide_ui = hide_ui.value_or(false);

	return result;
}

} // namespace options
