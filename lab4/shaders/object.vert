#version 450 core
#extension GL_GOOGLE_include_directive : require

#include "uniforms.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

// World space
layout(location = 0) out vec3 out_position;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec2 out_uv;

layout(location = 3) out vec4 sun_position;
layout(location = 4) out vec4 spot_position;

void main() {
	const vec4 world_position = object_uniforms.model * vec4(in_position, 1);
	gl_Position = scene_uniforms.view_projection * world_position;

	sun_position = scene_uniforms.shadow_projection[0] * world_position;
	spot_position = scene_uniforms.shadow_projection[1] * world_position;
	out_position = world_position.xyz;
	out_normal = transpose(inverse(mat3(object_uniforms.model))) * in_normal;
	out_uv = in_uv;
}
