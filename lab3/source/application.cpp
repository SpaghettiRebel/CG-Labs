#include "application.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

#include <glm/gtc/color_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include "geometry.hpp"
#include "graphics.hpp"
#include "options.hpp"
#include "scene.hpp"
#include "uniforms.hpp"

namespace application {

namespace {

// Elements of the dynamic uniform buffer
constexpr uint32_t max_objects = scene::max_objects;

constexpr uint32_t cylinder_segments = 50;
constexpr float cylinder_radius = 0.5f;
constexpr float cylinder_height = 1.0f;

// Light markers
constexpr float marker_size = 0.1f; // Edge of the cube
constexpr uint32_t cone_segments = 16;
constexpr float cone_length = 0.6f;

struct GpuMesh {
	graphics::Buffer vertices;
	graphics::Buffer indices;
	uint32_t vertex_count = 0;
	uint32_t index_count = 0;
};

// Vulkan objects
GpuMesh cylinder_mesh;
GpuMesh cube_mesh;
GpuMesh cone_mesh;

graphics::Buffer scene_uniform_buffer;
graphics::Buffer point_light_buffer;
graphics::Buffer spot_light_buffer;
graphics::Buffer object_uniform_buffer; // Dynamic: array of ObjectUniforms
VkDeviceSize object_uniforms_stride;    // Distance between its elements

// Albedo, specular and emissive of every material
std::array<std::array<graphics::Texture, 3>, scene::material_count> material_textures;
graphics::Texture black_pixel; // Instead of the missing textures
VkSampler sampler;

VkDescriptorSetLayout scene_set_layout;
VkDescriptorSetLayout material_set_layout;
VkDescriptorPool descriptor_pool;
VkDescriptorSet scene_set;
std::array<VkDescriptorSet, scene::material_count> material_sets;

VkPipelineLayout object_pipeline_layout;
VkPipeline object_pipeline;
VkPipelineLayout marker_pipeline_layout;
VkPipeline cube_pipeline;
VkPipeline cone_pipeline;

// Different materials, the last object shows the animated sampling
std::vector<scene::Object> defaultObjects() {
	std::vector<scene::Object> result = { scene::makeObject(0), scene::makeObject(1), scene::makeObject(2) };
	result.back().animated_sampling = true;
	return result;
}

// Scene state, changed in update() and uploaded to buffers in render()
options::StartupOptions startup_options;
const scene::Camera default_camera = scene::cameraAround(3, 0, 25);
scene::Camera camera = default_camera;
scene::Projection projection;
std::vector<scene::Object> objects = defaultObjects();
size_t selected_object = 0;
scene::Lights lights;
bool show_lights = true;

bool animate = false; // Move objects along the trajectory
scene::Trajectory trajectory;
scene::AnimationClock clock;
double previous_time = -1;
float sampling_time = 0;

void applyStartupOptions() {
	startup_options = options::parseStartupOptions([](const char* name) -> const char* {
		return std::getenv(name);
	});
	for (const auto& error : startup_options.errors) {
		std::cerr << error << '\n';
	}

	if (startup_options.projection) {
		projection.type = *startup_options.projection == options::Projection::orthographic
		                      ? scene::ProjectionType::orthographic
		                      : scene::ProjectionType::perspective;
	}
	if (startup_options.animate) {
		animate = *startup_options.animate;
		clock.playing = *startup_options.animate;
	}
	if (startup_options.camera_pitch) {
		camera = scene::cameraAround(3, 0, *startup_options.camera_pitch);
	}
	if (startup_options.objects) {
		const int count = std::min(*startup_options.objects, scene::max_objects);
		objects.clear();
		for (int i = 0; i < count; ++i) {
			objects.push_back(scene::makeObject(i));
		}
	}
	for (auto& object : objects) {
		if (startup_options.material) {
			object.material = std::min(*startup_options.material, scene::material_count - 1);
		}
		if (startup_options.animated_sampling) {
			object.animated_sampling = *startup_options.animated_sampling;
		}
	}
	if (startup_options.lights) {
		if (!startup_options.lights->directional) {
			lights.directional.intensity = 0;
		}
		if (!startup_options.lights->point) {
			lights.points.clear();
		}
		if (!startup_options.lights->spot) {
			lights.spots.clear();
		}
	}
	if (startup_options.show_lights) {
		show_lights = *startup_options.show_lights;
	}
}

bool createMesh(const geometry::Mesh& mesh, GpuMesh& result) {
	const VkDeviceSize vertices_size = mesh.vertices.size() * sizeof(geometry::Vertex);
	const VkDeviceSize indices_size = mesh.indices.size() * sizeof(uint32_t);

	if (!graphics::createGpuBuffer(mesh.vertices.data(), vertices_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, result.vertices) ||
	    !graphics::createGpuBuffer(mesh.indices.data(), indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, result.indices)) {
		return false;
	}

	result.vertex_count = uint32_t(mesh.vertices.size());
	result.index_count = uint32_t(mesh.indices.size());

	return true;
}

void destroyMesh(GpuMesh& mesh) {
	graphics::destroyBuffer(mesh.indices);
	graphics::destroyBuffer(mesh.vertices);
}

bool createGeometry() {
	return createMesh(geometry::makeCylinder(cylinder_segments, cylinder_radius, cylinder_height), cylinder_mesh) &&
	       createMesh(geometry::makeCube(), cube_mesh) &&
	       createMesh(geometry::makeConeLines(cone_segments), cone_mesh);
}

bool createTextures() {
	const uint8_t black[] = { 0, 0, 0, 255 };
	if (!graphics::createTexture(black, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, black_pixel)) {
		return false;
	}

	sampler = graphics::createSampler();
	if (sampler == VK_NULL_HANDLE) {
		return false;
	}

	for (size_t i = 0; i < material_textures.size(); ++i) {
		// Albedo and emissive are colors in sRGB, specular is a mask
		const scene::Material& material = scene::materials[i];
		const std::pair<const char*, VkFormat> files[] = {
			{ material.albedo, VK_FORMAT_R8G8B8A8_SRGB },
			{ material.specular, VK_FORMAT_R8G8B8A8_UNORM },
			{ material.emissive, VK_FORMAT_R8G8B8A8_SRGB },
		};
		for (size_t k = 0; k < material_textures[i].size(); ++k) {
			const auto [path, format] = files[k];
			if (path != nullptr && !graphics::loadTexture(path, format, material_textures[i][k])) {
				return false;
			}
		}
	}

	return true;
}

bool createDescriptors() {
	auto& context = graphics::internal::context;

	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(context.physical_device, &properties);
	object_uniforms_stride = uniforms::alignUp(sizeof(uniforms::ObjectUniforms),
	                                           properties.limits.minUniformBufferOffsetAlignment);

	if (!graphics::createBuffer(sizeof(uniforms::SceneUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                            scene_uniform_buffer) ||
	    !graphics::createBuffer(scene::max_point_lights * sizeof(uniforms::PointLight),
	                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, point_light_buffer) ||
	    !graphics::createBuffer(scene::max_spot_lights * sizeof(uniforms::SpotLight),
	                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, spot_light_buffer) ||
	    !graphics::createBuffer(max_objects * object_uniforms_stride, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                            object_uniform_buffer)) {
		return false;
	}

	// set = 0: scene data and arrays of lights, as in shaders/uniforms.glsl and object.frag
	const VkDescriptorSetLayoutBinding scene_bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		},
		{
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		},
	};

	// set = 1: properties of the objects and textures of a material
	const auto texture_binding = [](uint32_t binding) {
		return VkDescriptorSetLayoutBinding{
			.binding = binding,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		};
	};

	const VkDescriptorSetLayoutBinding material_bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		},
		texture_binding(1),
		texture_binding(2),
		texture_binding(3),
	};

	const VkDescriptorSetLayoutCreateInfo scene_layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = sizeof(scene_bindings) / sizeof(scene_bindings[0]),
		.pBindings = scene_bindings,
	};

	const VkDescriptorSetLayoutCreateInfo material_layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = sizeof(material_bindings) / sizeof(material_bindings[0]),
		.pBindings = material_bindings,
	};

	if (vkCreateDescriptorSetLayout(context.device, &scene_layout, nullptr, &scene_set_layout) != VK_SUCCESS ||
	    vkCreateDescriptorSetLayout(context.device, &material_layout, nullptr, &material_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor set layouts\n";
		return false;
	}

	// One scene set and one set per material
	const VkDescriptorPoolSize pool_sizes[] = {
		{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
		},
		{
			.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 2,
		},
		{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = scene::material_count,
		},
		{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 3 * scene::material_count,
		},
	};

	const VkDescriptorPoolCreateInfo pool = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1 + scene::material_count,
		.poolSizeCount = sizeof(pool_sizes) / sizeof(pool_sizes[0]),
		.pPoolSizes = pool_sizes,
	};

	if (vkCreateDescriptorPool(context.device, &pool, nullptr, &descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor pool\n";
		return false;
	}

	std::array<VkDescriptorSetLayout, 1 + scene::material_count> layouts;
	layouts[0] = scene_set_layout;
	for (int i = 0; i < scene::material_count; ++i) {
		layouts[size_t(1 + i)] = material_set_layout;
	}

	const VkDescriptorSetAllocateInfo allocation = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = uint32_t(layouts.size()),
		.pSetLayouts = layouts.data(),
	};

	std::array<VkDescriptorSet, 1 + scene::material_count> sets;
	if (vkAllocateDescriptorSets(context.device, &allocation, sets.data()) != VK_SUCCESS) {
		std::cerr << "Failed to allocate descriptor sets\n";
		return false;
	}
	scene_set = sets[0];
	std::copy(sets.begin() + 1, sets.end(), material_sets.begin());

	// Point every binding to its resource
	const VkDescriptorBufferInfo scene_buffers[] = {
		{ scene_uniform_buffer.buffer, 0, scene_uniform_buffer.size },
		{ point_light_buffer.buffer, 0, point_light_buffer.size },
		{ spot_light_buffer.buffer, 0, spot_light_buffer.size },
	};

	std::array<VkWriteDescriptorSet, 3> scene_writes;
	for (uint32_t i = 0; i < scene_writes.size(); ++i) {
		scene_writes[i] = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = scene_set,
			.dstBinding = i,
			.descriptorCount = 1,
			.descriptorType = scene_bindings[i].descriptorType,
			.pBufferInfo = &scene_buffers[i],
		};
	}
	vkUpdateDescriptorSets(context.device, uint32_t(scene_writes.size()), scene_writes.data(), 0, nullptr);

	// Range is one element: the offset to it is set in vkCmdBindDescriptorSets
	const VkDescriptorBufferInfo object_buffer = { object_uniform_buffer.buffer, 0, sizeof(uniforms::ObjectUniforms) };

	for (size_t i = 0; i < material_sets.size(); ++i) {
		std::array<VkDescriptorImageInfo, 3> images;
		std::array<VkWriteDescriptorSet, 4> writes;
		writes[0] = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = material_sets[i],
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.pBufferInfo = &object_buffer,
		};
		for (uint32_t k = 0; k < images.size(); ++k) {
			const VkImageView view = material_textures[i][k].view;
			images[k] = {
				.sampler = sampler,
				.imageView = view != VK_NULL_HANDLE ? view : black_pixel.view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			writes[1 + k] = {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = material_sets[i],
				.dstBinding = 1 + k,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &images[k],
			};
		}
		vkUpdateDescriptorSets(context.device, uint32_t(writes.size()), writes.data(), 0, nullptr);
	}

	return true;
}

// All meshes have geometry::Vertex, light markers read only the position (location 0)
VkPipeline createPipeline(VkPipelineLayout layout, const char* vertex_shader_path, const char* fragment_shader_path,
                          uint32_t attribute_count, VkPrimitiveTopology topology) {
	auto& context = graphics::internal::context;

	static constexpr VkVertexInputBindingDescription vertex_binding = {
		.binding = 0,
		.stride = sizeof(geometry::Vertex),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

	static constexpr VkVertexInputAttributeDescription vertex_attributes[] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(geometry::Vertex, position),
		},
		{
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(geometry::Vertex, normal),
		},
		{
			.location = 2,
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = offsetof(geometry::Vertex, uv),
		},
	};

	const VkShaderModule vertex_shader = graphics::loadShaderModule(vertex_shader_path);
	const VkShaderModule fragment_shader = graphics::loadShaderModule(fragment_shader_path);

	VkPipeline pipeline = VK_NULL_HANDLE;
	if (vertex_shader != VK_NULL_HANDLE && fragment_shader != VK_NULL_HANDLE) {
		pipeline = graphics::createGraphicsPipeline({
			.layout = layout,
			.render_pass = context.render_pass,
			.vertex_shader = vertex_shader,
			.fragment_shader = fragment_shader,
			.vertex_bindings = &vertex_binding,
			.vertex_binding_count = 1,
			.vertex_attributes = vertex_attributes,
			.vertex_attribute_count = attribute_count,
			.topology = topology,
		});
	}

	// Modules are only needed to create the pipeline
	vkDestroyShaderModule(context.device, vertex_shader, nullptr);
	vkDestroyShaderModule(context.device, fragment_shader, nullptr);

	return pipeline;
}

bool createPipelines() {
	auto& context = graphics::internal::context;

	// Objects: set = 0 is the scene, set = 1 is the material, the flag of animated sampling is a push constant
	const VkDescriptorSetLayout set_layouts[] = { scene_set_layout, material_set_layout };

	const VkPushConstantRange object_constants = {
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(uniforms::ObjectConstants),
	};

	const VkPipelineLayoutCreateInfo object_layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = sizeof(set_layouts) / sizeof(set_layouts[0]),
		.pSetLayouts = set_layouts,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &object_constants,
	};

	// Light markers: no descriptor sets, the matrix and the color are push constants
	const VkPushConstantRange marker_constants = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		.offset = 0,
		.size = sizeof(uniforms::MarkerConstants),
	};

	const VkPipelineLayoutCreateInfo marker_layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &marker_constants,
	};

	if (vkCreatePipelineLayout(context.device, &object_layout, nullptr, &object_pipeline_layout) != VK_SUCCESS ||
	    vkCreatePipelineLayout(context.device, &marker_layout, nullptr, &marker_pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create pipeline layouts\n";
		return false;
	}

	object_pipeline = createPipeline(object_pipeline_layout, "shaders/object.vert.spv", "shaders/object.frag.spv",
	                                 3, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	cube_pipeline = createPipeline(marker_pipeline_layout, "shaders/light_marker.vert.spv",
	                               "shaders/light_marker.frag.spv", 1, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	cone_pipeline = createPipeline(marker_pipeline_layout, "shaders/light_marker.vert.spv",
	                               "shaders/light_marker.frag.spv", 1, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);

	return object_pipeline != VK_NULL_HANDLE && cube_pipeline != VK_NULL_HANDLE && cone_pipeline != VK_NULL_HANDLE;
}

// Returns view-projection matrix for the light markers
glm::mat4 uploadUniforms() {
	const VkExtent2D extent = graphics::internal::context.swapchain_extent;
	const float aspect = float(extent.width) / float(extent.height);
	const glm::mat4 view_projection = scene::projectionMatrix(projection, aspect) * scene::viewMatrix(camera);

	const uniforms::SceneUniforms scene_data = scene::sceneUniforms(view_projection, camera, lights);
	graphics::writeBuffer(scene_uniform_buffer, &scene_data, sizeof(scene_data));

	for (size_t i = 0; i < scene_data.point_light_count; ++i) {
		const uniforms::PointLight data = scene::pointLightUniforms(lights.points[i]);
		graphics::writeBuffer(point_light_buffer, &data, sizeof(data), i * sizeof(data));
	}
	for (size_t i = 0; i < scene_data.spot_light_count; ++i) {
		const uniforms::SpotLight data = scene::spotLightUniforms(lights.spots[i]);
		graphics::writeBuffer(spot_light_buffer, &data, sizeof(data), i * sizeof(data));
	}

	for (size_t i = 0; i < objects.size(); ++i) {
		const scene::Object& object = objects[i];
		const glm::mat4 model = animate
		                            ? scene::animatedModelMatrix(object.transform, trajectory, clock.time, object.trajectory_phase)
		                            : scene::modelMatrix(object.transform);
		const uniforms::ObjectUniforms data = scene::objectUniforms(object, model);
		graphics::writeBuffer(object_uniform_buffer, &data, sizeof(data), i * object_uniforms_stride);
	}

	return view_projection;
}

void projectionInterface() {
	if (!ImGui::CollapsingHeader("Projection", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	int type = int(projection.type);
	ImGui::RadioButton("Perspective", &type, int(scene::ProjectionType::perspective));
	ImGui::SameLine();
	ImGui::RadioButton("Orthographic", &type, int(scene::ProjectionType::orthographic));
	projection.type = scene::ProjectionType(type);

	if (projection.type == scene::ProjectionType::perspective) {
		ImGui::SliderFloat("Field of view", &projection.vertical_fov, 20, 120, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
	} else {
		ImGui::SliderFloat("View height", &projection.ortho_height, 0.5f, 10, "%.1f", ImGuiSliderFlags_AlwaysClamp);
	}
}

void positionInterface(glm::vec3& position) {
	ImGui::DragFloat3("Position", glm::value_ptr(position), 0.01f, -20, 20, "%.2f", ImGuiSliderFlags_AlwaysClamp);
}

void directionInterface(float& yaw, float& pitch) {
	ImGui::SliderFloat("Yaw", &yaw, -180, 180, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Pitch", &pitch, -90, 90, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
}

void lightColorInterface(glm::vec3& color, float& intensity, float max_intensity) {
	ImGui::ColorEdit3("Color", glm::value_ptr(color));
	ImGui::SliderFloat("Intensity", &intensity, 0, max_intensity, "%.2f", ImGuiSliderFlags_AlwaysClamp);
}

void cameraInterface() {
	if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	positionInterface(camera.position);
	directionInterface(camera.yaw, camera.pitch);
	if (ImGui::Button("Reset camera")) {
		camera = default_camera;
	}
}

void lightingInterface() {
	if (!ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::Checkbox("Show lights", &show_lights);

	ImGui::SeparatorText("Ambient light");
	ImGui::PushID("ambient");
	lightColorInterface(lights.ambient.color, lights.ambient.intensity, 1);
	ImGui::PopID();

	ImGui::SeparatorText("Directional light");
	ImGui::PushID("directional");
	lightColorInterface(lights.directional.color, lights.directional.intensity, 5);
	directionInterface(lights.directional.yaw, lights.directional.pitch);
	ImGui::PopID();
}

// List of lights with Add and Remove, edit_light shows the parameters of one light
template <typename Light, typename Editor>
void lightListInterface(const char* name, std::vector<Light>& list, size_t max_count, Editor edit_light) {
	if (!ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::PushID(name);
	std::optional<size_t> removed;
	for (size_t i = 0; i < list.size(); ++i) {
		ImGui::PushID(int(i));
		if (ImGui::TreeNodeEx("light", ImGuiTreeNodeFlags_DefaultOpen, "Light %zu", i + 1)) {
			edit_light(list[i]);
			if (ImGui::Button("Remove")) {
				removed = i;
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (removed) {
		list.erase(list.begin() + ptrdiff_t(*removed));
	}

	ImGui::BeginDisabled(list.size() >= max_count);
	if (ImGui::Button("Add")) {
		list.emplace_back();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled("%zu of %zu, storage buffer", list.size(), max_count);
	ImGui::PopID();
}

void pointLightsInterface() {
	lightListInterface("Point lights", lights.points, scene::max_point_lights, [](scene::PointLight& light) {
		positionInterface(light.position);
		lightColorInterface(light.color, light.intensity, 20);
	});
}

void spotLightsInterface() {
	lightListInterface("Spot lights", lights.spots, scene::max_spot_lights, [](scene::SpotLight& light) {
		positionInterface(light.position);
		lightColorInterface(light.color, light.intensity, 20);
		directionInterface(light.yaw, light.pitch);
		ImGui::SliderFloat("Inner angle", &light.inner_angle, 0, 80, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Outer angle", &light.outer_angle, 0, 80, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
	});
}

void objectsInterface() {
	for (size_t i = 0; i < objects.size(); ++i) {
		char label[32];
		std::snprintf(label, sizeof(label), "Cylinder %zu", i + 1);
		if (ImGui::Selectable(label, selected_object == i)) {
			selected_object = i;
		}
	}

	const int free_slot = scene::freeSlot(objects);
	ImGui::BeginDisabled(free_slot < 0);
	if (ImGui::Button("Add")) {
		objects.push_back(scene::makeObject(free_slot));
		selected_object = objects.size() - 1;
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(objects.size() <= 1);
	if (ImGui::Button("Remove selected")) {
		objects.erase(objects.begin() + ptrdiff_t(selected_object));
		selected_object = std::min(selected_object, objects.size() - 1);
	}
	ImGui::EndDisabled();
}

void animationInterface() {
	if (!ImGui::CollapsingHeader("Animation")) {
		return;
	}

	ImGui::Checkbox("Move along trajectory", &animate);
	ImGui::BeginDisabled(!animate);
	if (ImGui::Button(clock.playing ? "Pause" : "Play", { 60, 0 })) {
		clock.playing = !clock.playing;
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset")) {
		clock.reset();
	}
	ImGui::SameLine();
	ImGui::Text("t = %.1f s", clock.time);
	ImGui::SliderFloat("Speed", &clock.speed, 0, 4, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Radius", &trajectory.radius, 0, 2.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Wobble radius", &trajectory.wobble_radius, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderInt("Lobes", &trajectory.lobes, 1, 12, "%d", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Bob height", &trajectory.bob_height, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Spin", &trajectory.spin, -360, 360, "%.0f deg/s", ImGuiSliderFlags_AlwaysClamp);
	ImGui::EndDisabled();
}

void materialInterface(scene::Object& object) {
	if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	if (ImGui::BeginCombo("Texture", scene::materials[object.material].name)) {
		for (int i = 0; i < scene::material_count; ++i) {
			if (ImGui::Selectable(scene::materials[i].name, object.material == i)) {
				object.material = i;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::Checkbox("Animated sampling", &object.animated_sampling);
	ImGui::ColorEdit3("Specular", glm::value_ptr(object.specular));
	ImGui::SliderFloat("Shininess", &object.shininess, 1, 256, "%.0f",
	                   ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
}

void transformInterface(scene::Transform& transform) {
	if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::DragFloat3("Position", glm::value_ptr(transform.position), 0.01f, -5, 5, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat3("Rotation", glm::value_ptr(transform.rotation), -180, 180, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat3("Scale", glm::value_ptr(transform.scale), scene::min_scale, 3, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (ImGui::Button("Reset transform")) {
		transform = {};
	}
}

void bindMesh(VkCommandBuffer command_buffer, const GpuMesh& mesh) {
	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(command_buffer, 0, 1, &mesh.vertices.buffer, &offset);
	vkCmdBindIndexBuffer(command_buffer, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
}

void drawMarker(VkCommandBuffer command_buffer, const GpuMesh& mesh, const glm::mat4& model_view_projection,
                glm::vec3 color) {
	const uniforms::MarkerConstants constants = {
		.model_view_projection = model_view_projection,
		.color = glm::vec4(color, 1),
	};
	vkCmdPushConstants(command_buffer, marker_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(constants),
	                   &constants);
	vkCmdDrawIndexed(command_buffer, mesh.index_count, 1, 0, 0, 0);
}

// Cube of the light color at every point and spot light, white cone of every spot light
void drawLightMarkers(VkCommandBuffer command_buffer, const glm::mat4& view_projection) {
	const auto cube_matrix = [&](glm::vec3 position) {
		return glm::scale(glm::translate(view_projection, position), glm::vec3(marker_size));
	};

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, cube_pipeline);
	bindMesh(command_buffer, cube_mesh);
	for (const auto& light : lights.points) {
		drawMarker(command_buffer, cube_mesh, cube_matrix(light.position), glm::convertSRGBToLinear(light.color));
	}
	for (const auto& light : lights.spots) {
		drawMarker(command_buffer, cube_mesh, cube_matrix(light.position), glm::convertSRGBToLinear(light.color));
	}

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, cone_pipeline);
	bindMesh(command_buffer, cone_mesh);
	for (const auto& light : lights.spots) {
		drawMarker(command_buffer, cone_mesh, view_projection * scene::spotConeMatrix(light, cone_length),
		           glm::vec3(1));
	}
}

} // namespace

bool initialize() {
	applyStartupOptions();

	if (!createGeometry() || !createTextures() || !createDescriptors() || !createPipelines()) {
		shutdown();
		return false;
	}

	return true;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);

	vkDestroyPipeline(context.device, cone_pipeline, nullptr);
	vkDestroyPipeline(context.device, cube_pipeline, nullptr);
	vkDestroyPipeline(context.device, object_pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, marker_pipeline_layout, nullptr);
	vkDestroyPipelineLayout(context.device, object_pipeline_layout, nullptr);
	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr); // Frees its sets too
	vkDestroyDescriptorSetLayout(context.device, material_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, scene_set_layout, nullptr);

	vkDestroySampler(context.device, sampler, nullptr);
	for (auto& textures : material_textures) {
		for (auto& texture : textures) {
			graphics::destroyTexture(texture);
		}
	}
	graphics::destroyTexture(black_pixel);

	graphics::destroyBuffer(object_uniform_buffer);
	graphics::destroyBuffer(spot_light_buffer);
	graphics::destroyBuffer(point_light_buffer);
	graphics::destroyBuffer(scene_uniform_buffer);
	destroyMesh(cone_mesh);
	destroyMesh(cube_mesh);
	destroyMesh(cylinder_mesh);
}

void update(double time) {
	// Animation runs even with hidden interface
	if (previous_time >= 0 && animate) {
		clock.advance(float(time - previous_time));
	}
	previous_time = time;
	sampling_time = startup_options.sampling_time.value_or(float(time));

	if (startup_options.hide_ui) {
		return;
	}

	// Scene settings on the left, objects on the right
	constexpr float window_width = 340;
	const ImVec2 screen = ImGui::GetMainViewport()->WorkSize;

	ImGui::SetNextWindowPos({ 10, 10 }, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize({ window_width, screen.y - 20 }, ImGuiCond_FirstUseEver);
	ImGui::Begin("Scene");
	ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
	ImGui::Text("Cylinder: %u vertices, %u triangles", cylinder_mesh.vertex_count, cylinder_mesh.index_count / 3);
	projectionInterface();
	cameraInterface();
	lightingInterface();
	pointLightsInterface();
	spotLightsInterface();
	animationInterface();
	ImGui::End();

	ImGui::SetNextWindowPos({ screen.x - window_width - 10, 10 }, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize({ window_width, 0 }, ImGuiCond_FirstUseEver);
	ImGui::Begin("Objects");
	objectsInterface();
	ImGui::SeparatorText("Selected object");
	transformInterface(objects[selected_object].transform);
	materialInterface(objects[selected_object]);
	ImGui::End();
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;

	// Buffers are written here and not in update(): update() runs before prepare()
	// waits for the previous frame, which may still read these buffers on the GPU
	const glm::mat4 view_projection = uploadUniforms();

	vkResetCommandBuffer(fd.command_buffer, 0);

	const VkCommandBufferBeginInfo command_buffer_begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	vkBeginCommandBuffer(fd.command_buffer, &command_buffer_begin);

	// Order matches render pass attachments: color, then depth
	const VkClearValue clear_values[] = {
		{ .color = { { 0.1f, 0.1f, 0.1f, 1.0f } } },
		{ .depthStencil = { .depth = 1.0f, .stencil = 0 } },
	};

	const VkRenderPassBeginInfo render_pass_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = { .extent = context.swapchain_extent },
		.clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]),
		.pClearValues = clear_values,
	};

	vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

	const VkViewport viewport = {
		.x = 0,
		.y = 0,
		.width = float(context.swapchain_extent.width),
		.height = float(context.swapchain_extent.height),
		.minDepth = 0,
		.maxDepth = 1,
	};

	const VkRect2D scissor = { .extent = context.swapchain_extent };

	vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
	vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, object_pipeline);
	bindMesh(fd.command_buffer, cylinder_mesh);

	// Set 0 once per frame. Set 1 before each object: its material and the offset to its properties
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, object_pipeline_layout,
	                        0, 1, &scene_set, 0, nullptr);
	for (size_t i = 0; i < objects.size(); ++i) {
		const uint32_t offset = uint32_t(i * object_uniforms_stride);
		vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, object_pipeline_layout,
		                        1, 1, &material_sets[size_t(objects[i].material)], 1, &offset);

		const uniforms::ObjectConstants constants = scene::objectConstants(objects[i], sampling_time);
		vkCmdPushConstants(fd.command_buffer, object_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
		                   sizeof(constants), &constants);

		vkCmdDrawIndexed(fd.command_buffer, cylinder_mesh.index_count, 1, 0, 0, 0);
	}

	if (show_lights) {
		drawLightMarkers(fd.command_buffer, view_projection);
	}

	vkCmdEndRenderPass(fd.command_buffer);

	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application
