// Uniform blocks of object.vert and object.frag, same layout as source/uniforms.hpp.
// Colors are linear, light colors are already multiplied by the intensity.

layout(std140, set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
	vec3 camera_position;
	uint point_light_count;
	vec3 directional_light_direction; // Normalized, the way the light goes
	uint spot_light_count;
	vec3 directional_light_color;
	vec3 ambient_light;
} scene_uniforms;

layout(std140, set = 1, binding = 0) uniform ObjectUniforms {
	mat4 model;
	vec3 albedo;
	float vertex_colors; // Weight of vertex colors: 0 = albedo, 1 = albedo * vertex color
	vec3 specular;
	float shininess;
} object_uniforms;
