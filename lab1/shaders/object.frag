#version 450 core

layout(location = 0) in vec3 in_color;

layout(location = 0) out vec4 out_color;

void main() {
	// Linear color, the sRGB swapchain image encodes it on write
	out_color = vec4(in_color, 1);
}
