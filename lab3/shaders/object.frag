#version 450 core
#extension GL_GOOGLE_include_directive : require

#include "uniforms.glsl"

// World space
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

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

layout(std430, set = 0, binding = 1) readonly buffer PointLights {
	PointLight point_lights[];
};

layout(std430, set = 0, binding = 2) readonly buffer SpotLights {
	SpotLight spot_lights[];
};

layout(set = 1, binding = 1) uniform sampler2D albedo_texture;
layout(set = 1, binding = 2) uniform sampler2D specular_texture;
layout(set = 1, binding = 3) uniform sampler2D emissive_texture;

layout(push_constant) uniform ObjectConstants {
	bool animated_sampling;
	float time;
} constants;

// Material of this pixel, from textures
vec3 albedo;
vec3 specular_color;

vec3 blinnPhong(vec3 n, vec3 v, vec3 l, vec3 radiance) {
	const float n_dot_l = dot(n, l);
	if (n_dot_l <= 0) {
		return vec3(0); // Light is behind the surface: no diffuse light and no highlight
	}

	const vec3 h = normalize(l + v);
	const vec3 diffuse = albedo * n_dot_l;
	const vec3 specular = specular_color * pow(max(dot(n, h), 0), object_uniforms.shininess);
	return radiance * (diffuse + specular);
}

// Two copies of the same image slide in different directions, their average changes its tint over time
vec3 animatedAlbedo() {
	const vec3 first = texture(albedo_texture, in_uv + vec2(0.1, 0) * constants.time).rgb;
	const vec3 second = texture(albedo_texture, 2 * in_uv + vec2(0, 0.2) * constants.time).rgb;
	const vec3 tint = 0.6 + 0.4 * cos(constants.time + vec3(0, 2.094, 4.189));
	return 0.5 * (first + second) * tint;
}

void main() {
	albedo = constants.animated_sampling ? animatedAlbedo() : texture(albedo_texture, in_uv).rgb;
	specular_color = object_uniforms.specular * texture(specular_texture, in_uv).rgb;

	const vec3 n = normalize(in_normal);
	const vec3 v = normalize(scene_uniforms.camera_position - in_position);

	vec3 color = scene_uniforms.ambient_light * albedo;

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

	// Glow is added after the lighting and does not depend on it
	color += texture(emissive_texture, in_uv).rgb;

	// Linear color, the sRGB swapchain image encodes it on write
	out_color = vec4(color, 1);
}
