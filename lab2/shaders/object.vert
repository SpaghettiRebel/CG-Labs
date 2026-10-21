#version 450 core
#extension GL_GOOGLE_include_directive : require

#include "uniforms.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;

// World space
layout(location = 0) out vec3 out_position;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec3 out_albedo;

void main() {
	const vec4 world_position = object_uniforms.model * vec4(in_position, 1);
	gl_Position = scene_uniforms.view_projection * world_position;

	out_position = world_position.xyz;
	// Inverse transpose keeps normals perpendicular to the surface after non-uniform scaling
	out_normal = transpose(inverse(mat3(object_uniforms.model))) * in_normal;
	out_albedo = object_uniforms.albedo * mix(vec3(1), in_color, object_uniforms.vertex_colors);
}
