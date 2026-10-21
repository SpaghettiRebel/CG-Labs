#version 450 core
#extension GL_GOOGLE_include_directive : require

#include "uniforms.glsl"

// World space
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_albedo;

layout(location = 0) out vec4 out_color;

struct PointLight {
	vec3 position;
	vec3 color;
};

struct SpotLight {
	vec3 position;
	float cos_inner; // Cosine of the angle between the axis and the edge of the full light
	vec3 direction;  // Normalized
	float cos_outer; // Cosine of the angle between the axis and the edge of the light, not more than cos_inner
	vec3 color;
};

// Arrays without size: the number of lights is in SceneUniforms
layout(std430, set = 0, binding = 1) readonly buffer PointLights {
	PointLight point_lights[];
};

layout(std430, set = 0, binding = 2) readonly buffer SpotLights {
	SpotLight spot_lights[];
};

// Blinn-Phong for one light: n - normal, v - to the camera, l - to the light, all normalized;
// radiance - light color reaching the point
vec3 blinnPhong(vec3 n, vec3 v, vec3 l, vec3 radiance) {
	const float n_dot_l = dot(n, l);
	if (n_dot_l <= 0) {
		return vec3(0); // Light is behind the surface: no diffuse light and no highlight
	}

	const vec3 h = normalize(l + v);
	const vec3 diffuse = in_albedo * n_dot_l;
	const vec3 specular = object_uniforms.specular * pow(max(dot(n, h), 0), object_uniforms.shininess);
	return radiance * (diffuse + specular);
}

void main() {
	const vec3 n = normalize(in_normal);
	const vec3 v = normalize(scene_uniforms.camera_position - in_position);

	vec3 color = scene_uniforms.ambient_light * in_albedo;

	color += blinnPhong(n, v, -scene_uniforms.directional_light_direction, scene_uniforms.directional_light_color);

	for (uint i = 0; i < scene_uniforms.point_light_count; ++i) {
		const vec3 to_light = point_lights[i].position - in_position;
		const float distance_squared = dot(to_light, to_light);
		color += blinnPhong(n, v, to_light / sqrt(distance_squared), point_lights[i].color / distance_squared);
	}

	for (uint i = 0; i < scene_uniforms.spot_light_count; ++i) {
		const SpotLight light = spot_lights[i];
		const vec3 to_light = light.position - in_position;
		const float distance_squared = dot(to_light, to_light);
		const vec3 l = to_light / sqrt(distance_squared);

		// 1 inside the inner cone, 0 outside the outer one, linear in the cosine between them.
		// Equal angles give a hard edge
		const float cos_angle = dot(-l, light.direction);
		const float cone = clamp((cos_angle - light.cos_outer) / max(light.cos_inner - light.cos_outer, 1e-4), 0, 1);

		color += blinnPhong(n, v, l, light.color * cone / distance_squared);
	}

	// Linear color, the sRGB swapchain image encodes it on write
	out_color = vec4(color, 1);
}
