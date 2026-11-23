#version 450 core
#extension GL_GOOGLE_include_directive : require
#include "uniforms.glsl"

layout(location = 0) in vec3 in_position;

void main() {
	gl_Position = scene_uniforms.shadow_projection[gl_InstanceIndex] * object_uniforms.model * vec4(in_position, 1);
}
