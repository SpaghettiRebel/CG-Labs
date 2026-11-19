#pragma once

#include <sstream>

#include <doctest/doctest.h>
#include <glm/glm.hpp>

// Approximate comparisons: CHECK(actual == test::approx(expected)).
// On failure doctest prints both values with the StringMaker specializations below.
namespace test {

template <typename T>
struct Approximately {
	T value;
};

template <typename T>
Approximately<T> approx(T value) {
	return {value};
}

template <glm::length_t L>
bool operator==(const glm::vec<L, float>& a, const Approximately<glm::vec<L, float>>& b) {
	for (glm::length_t i = 0; i < L; ++i) {
		if (a[i] != doctest::Approx(b.value[i]).epsilon(1e-4)) {
			return false;
		}
	}
	return true;
}

inline bool operator==(const glm::mat4& a, const Approximately<glm::mat4>& b) {
	for (glm::length_t column = 0; column < 4; ++column) {
		if (!(a[column] == approx(b.value[column]))) {
			return false;
		}
	}
	return true;
}

// Point after the perspective divide: clip space -> normalized device coordinates
inline glm::vec3 project(const glm::mat4& m, glm::vec3 p) {
	const glm::vec4 clip = m * glm::vec4(p, 1);
	return glm::vec3(clip) / clip.w;
}

} // namespace test

namespace doctest {

template <glm::length_t L>
struct StringMaker<glm::vec<L, float>> {
	static String convert(const glm::vec<L, float>& v) {
		std::ostringstream s;
		s << "(";
		for (glm::length_t i = 0; i < L; ++i) {
			s << (i > 0 ? ", " : "") << v[i];
		}
		s << ")";
		return s.str().c_str();
	}
};

template <>
struct StringMaker<glm::mat4> {
	static String convert(const glm::mat4& m) {
		std::ostringstream s;
		s << "\n";
		for (int row = 0; row < 4; ++row) {
			s << "  | ";
			for (int column = 0; column < 4; ++column) {
				s << m[column][row] << " "; // GLM indexes [column][row]
			}
			s << "|\n";
		}
		return s.str().c_str();
	}
};

template <typename T>
struct StringMaker<test::Approximately<T>> {
	static String convert(const test::Approximately<T>& a) {
		return StringMaker<T>::convert(a.value);
	}
};

} // namespace doctest
