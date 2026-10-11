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
	CHECK_FALSE(result.color.has_value());
	CHECK_FALSE(result.vertex_colors.has_value());
	CHECK_FALSE(result.camera_pitch.has_value());
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
	CHECK(Environment{{{"LAB1_VERTEX_COLORS", "1"}}}.parse().vertex_colors == true);
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

TEST_CASE("LAB1_COLOR is three comma separated numbers in [0, 1]") {
	const auto result = Environment{{{"LAB1_COLOR", "0.5,0.25,1"}}}.parse();
	REQUIRE(result.color.has_value());
	CHECK((*result.color)[0] == doctest::Approx(0.5f));
	CHECK((*result.color)[1] == doctest::Approx(0.25f));
	CHECK((*result.color)[2] == doctest::Approx(1.0f));
	CHECK(result.errors.empty());

	for (const char* value : {"1,2", "a,b,c", "0.5,0.5,1.5", "0.1,0.2,0.3,0.4", "0.1,,0.3"}) {
		CAPTURE(value);
		const auto invalid = Environment{{{"LAB1_COLOR", value}}}.parse();
		CHECK_FALSE(invalid.color.has_value());
		CHECK(invalid.errors.size() == 1);
	}
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

TEST_CASE("every invalid variable is reported") {
	const auto result = Environment{{
		{"LAB1_PROJECTION", "x"},
		{"LAB1_OBJECTS", "x"},
		{"LAB1_COLOR", "x"},
	}}.parse();

	CHECK(result.errors.size() == 3);
}
