#include <doctest/doctest.h>

#include <map>
#include <string>

#include "options.hpp"

namespace {

// Fake environment for parseStartupOptions
struct Environment {
	std::map<std::string, std::string> variables;

	options::StartupOptions parse() const {
		return options::parseStartupOptions([this](const char* name) -> const char* {
			auto it = variables.find(name);
			return it == variables.end() ? nullptr : it->second.c_str();
		});
	}
};

} // namespace

TEST_CASE("empty environment keeps all defaults") {
	const auto result = Environment{}.parse();

	CHECK_FALSE(result.projection.has_value());
	CHECK_FALSE(result.animate.has_value());
	CHECK_FALSE(result.objects.has_value());
	CHECK_FALSE(result.material.has_value());
	CHECK_FALSE(result.animated_sampling.has_value());
	CHECK_FALSE(result.sampling_time.has_value());
	CHECK_FALSE(result.camera_pitch.has_value());
	CHECK_FALSE(result.lights.has_value());
	CHECK_FALSE(result.show_lights.has_value());
	CHECK_FALSE(result.hide_ui);
	CHECK(result.errors.empty());
}

TEST_CASE("LAB1_PROJECTION") {
	CHECK(Environment{{{"LAB1_PROJECTION", "perspective"}}}.parse().projection ==
	      options::Projection::perspective);
	CHECK(Environment{{{"LAB1_PROJECTION", "orthographic"}}}.parse().projection ==
	      options::Projection::orthographic);

	const auto invalid = Environment{{{"LAB1_PROJECTION", "ortho"}}}.parse();
	CHECK_FALSE(invalid.projection.has_value());
	CHECK(invalid.errors.size() == 1);
}

TEST_CASE("boolean flags accept only 0 and 1") {
	CHECK(Environment{{{"LAB1_ANIMATE", "1"}}}.parse().animate == true);
	CHECK(Environment{{{"LAB1_ANIMATE", "0"}}}.parse().animate == false);
	CHECK(Environment{{{"LAB1_ANIMATED_SAMPLING", "1"}}}.parse().animated_sampling == true);
	CHECK(Environment{{{"LAB1_SHOW_LIGHTS", "0"}}}.parse().show_lights == false);
	CHECK(Environment{{{"LAB1_HIDE_UI", "1"}}}.parse().hide_ui);
	CHECK_FALSE(Environment{{{"LAB1_HIDE_UI", "0"}}}.parse().hide_ui);

	const auto invalid = Environment{{{"LAB1_ANIMATE", "yes"}}}.parse();
	CHECK_FALSE(invalid.animate.has_value());
	CHECK(invalid.errors.size() == 1);
}

TEST_CASE("LAB1_OBJECTS must be a positive integer") {
	CHECK(Environment{{{"LAB1_OBJECTS", "3"}}}.parse().objects == 3);

	for (const char* value : {"0", "-2", "abc", "3x", ""}) {
		CAPTURE(value);
		const auto result = Environment{{{"LAB1_OBJECTS", value}}}.parse();
		CHECK_FALSE(result.objects.has_value());
		CHECK(result.errors.size() == 1);
	}
}

TEST_CASE("LAB1_MATERIAL is an index of a material") {
	CHECK(Environment{{{"LAB1_MATERIAL", "0"}}}.parse().material == 0);
	CHECK(Environment{{{"LAB1_MATERIAL", "3"}}}.parse().material == 3);

	for (const char* value : {"-1", "wood", "1x", ""}) {
		CAPTURE(value);
		const auto result = Environment{{{"LAB1_MATERIAL", value}}}.parse();
		CHECK_FALSE(result.material.has_value());
		CHECK(result.errors.size() == 1);
	}
}

TEST_CASE("LAB1_SAMPLING_TIME is a number of seconds") {
	const auto time = Environment{{{"LAB1_SAMPLING_TIME", "1.5"}}}.parse().sampling_time;
	REQUIRE(time.has_value());
	CHECK(*time == doctest::Approx(1.5f));

	const auto invalid = Environment{{{"LAB1_SAMPLING_TIME", "soon"}}}.parse();
	CHECK_FALSE(invalid.sampling_time.has_value());
	CHECK(invalid.errors.size() == 1);
}

TEST_CASE("LAB1_CAMERA_PITCH is a number of degrees") {
	const auto negative = Environment{{{"LAB1_CAMERA_PITCH", "-30.5"}}}.parse().camera_pitch;
	REQUIRE(negative.has_value());
	CHECK(*negative == doctest::Approx(-30.5f));

	const auto zero = Environment{{{"LAB1_CAMERA_PITCH", "0"}}}.parse().camera_pitch;
	REQUIRE(zero.has_value());
	CHECK(*zero == doctest::Approx(0.0f));

	const auto invalid = Environment{{{"LAB1_CAMERA_PITCH", "up"}}}.parse();
	CHECK_FALSE(invalid.camera_pitch.has_value());
	CHECK(invalid.errors.size() == 1);
}

TEST_CASE("LAB1_LIGHTS is none or a list of light types") {
	const auto none = Environment{{{"LAB1_LIGHTS", "none"}}}.parse().lights;
	REQUIRE(none.has_value());
	CHECK_FALSE(none->directional);
	CHECK_FALSE(none->point);
	CHECK_FALSE(none->spot);

	const auto one = Environment{{{"LAB1_LIGHTS", "point"}}}.parse().lights;
	REQUIRE(one.has_value());
	CHECK_FALSE(one->directional);
	CHECK(one->point);
	CHECK_FALSE(one->spot);

	const auto two = Environment{{{"LAB1_LIGHTS", "spot,directional"}}}.parse().lights;
	REQUIRE(two.has_value());
	CHECK(two->directional);
	CHECK_FALSE(two->point);
	CHECK(two->spot);

	for (const char* value : {"", "sun", "point,", ",spot", "point,,spot", "none,point"}) {
		CAPTURE(value);
		const auto invalid = Environment{{{"LAB1_LIGHTS", value}}}.parse();
		CHECK_FALSE(invalid.lights.has_value());
		CHECK(invalid.errors.size() == 1);
	}
}

TEST_CASE("every invalid variable is reported") {
	const auto result = Environment{{
		{"LAB1_PROJECTION", "x"},
		{"LAB1_OBJECTS", "x"},
		{"LAB1_MATERIAL", "x"},
	}}.parse();

	CHECK(result.errors.size() == 3);
}
