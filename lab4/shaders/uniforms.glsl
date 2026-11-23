layout(std140, set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
	vec3 camera_position;
	uint point_light_count;
	vec3 directional_light_direction; // Normalized, the way the light goes
	uint spot_light_count;
	vec3 directional_light_color;
	vec3 ambient_light;
	mat4 shadow_projection[2];
} scene_uniforms;

// Dynamic uniform buffer: the offset to the object is set in vkCmdBindDescriptorSets
layout(std140, set = 1, binding = 0) uniform ObjectUniforms {
	mat4 model;
	vec3 specular;
	float shininess;
} object_uniforms;
