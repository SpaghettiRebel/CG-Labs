#version 450 core

// Light markers: constant color, no lighting

layout(location = 0) in vec3 in_position;

layout(location = 0) out vec3 out_color;

// Same layout as uniforms::MarkerConstants in source/uniforms.hpp
layout(push_constant) uniform MarkerConstants {
	mat4 model_view_projection;
	vec4 color; // Linear
} marker;

void main() {
	gl_Position = marker.model_view_projection * vec4(in_position, 1);
	out_color = marker.color.rgb;
}
