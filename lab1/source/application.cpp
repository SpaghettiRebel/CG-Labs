#include "application.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include "geometry.hpp"
#include "graphics.hpp"
#include "options.hpp"
#include "scene.hpp"
#include "uniforms.hpp"

namespace application {

namespace {

// One uniform buffer and one descriptor set per object slot
constexpr uint32_t max_objects = scene::max_objects;

constexpr uint32_t cylinder_segments = 50;
constexpr float cylinder_radius = 0.5f;
constexpr float cylinder_height = 1.0f;

// Vulkan objects
graphics::Buffer vertex_buffer;
graphics::Buffer index_buffer;

graphics::Buffer global_uniform_buffer;
std::array<graphics::Buffer, max_objects> object_uniform_buffers;

VkDescriptorSetLayout global_set_layout;
VkDescriptorSetLayout object_set_layout;
VkDescriptorPool descriptor_pool;
VkDescriptorSet global_set;
std::array<VkDescriptorSet, max_objects> object_sets;

VkPipelineLayout pipeline_layout;
VkPipeline pipeline;

// Scene state, changed in update() and uploaded to uniform buffers in render()
options::StartupOptions startup_options;
geometry::Mesh mesh;
scene::Camera camera;
scene::Projection projection;
std::vector<scene::Object> objects = { scene::makeObject(0) };
size_t selected_object = 0;

bool animate = false; // Move objects along the trajectory
scene::Trajectory trajectory;
scene::AnimationClock clock;
double previous_time = -1;

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
		camera.pitch = *startup_options.camera_pitch;
	}
	if (startup_options.objects) {
		const int count = std::min(*startup_options.objects, scene::max_objects);
		objects.clear();
		for (int i = 0; i < count; ++i) {
			objects.push_back(scene::makeObject(i));
		}
	}
	for (auto& object : objects) {
		if (startup_options.color) {
			object.color = *startup_options.color;
		}
		if (startup_options.vertex_colors) {
			object.vertex_colors = *startup_options.vertex_colors;
		}
	}
}

bool createGeometry() {
	mesh = geometry::makeCylinder(cylinder_segments, cylinder_radius, cylinder_height);

	const VkDeviceSize vertices_size = mesh.vertices.size() * sizeof(geometry::Vertex);
	const VkDeviceSize indices_size = mesh.indices.size() * sizeof(uint32_t);

	if (!graphics::createBuffer(vertices_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer) ||
	    !graphics::createBuffer(indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, index_buffer)) {
		return false;
	}

	graphics::writeBuffer(vertex_buffer, mesh.vertices.data(), vertices_size);
	graphics::writeBuffer(index_buffer, mesh.indices.data(), indices_size);

	return true;
}

bool createDescriptors() {
	auto& context = graphics::internal::context;

	if (!graphics::createBuffer(sizeof(uniforms::GlobalUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                            global_uniform_buffer)) {
		return false;
	}
	for (auto& buffer : object_uniform_buffers) {
		if (!graphics::createBuffer(sizeof(uniforms::ObjectUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, buffer)) {
			return false;
		}
	}

	// Both sets have the same shape: one uniform buffer at binding 0
	const VkDescriptorSetLayoutBinding uniform_binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
	};

	const VkDescriptorSetLayoutCreateInfo set_layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &uniform_binding,
	};

	if (vkCreateDescriptorSetLayout(context.device, &set_layout, nullptr, &global_set_layout) != VK_SUCCESS ||
	    vkCreateDescriptorSetLayout(context.device, &set_layout, nullptr, &object_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor set layouts\n";
		return false;
	}

	// One global set and one set per object, each with a single uniform buffer
	const VkDescriptorPoolSize pool_size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1 + max_objects,
	};

	const VkDescriptorPoolCreateInfo pool = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1 + max_objects,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};

	if (vkCreateDescriptorPool(context.device, &pool, nullptr, &descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor pool\n";
		return false;
	}

	std::array<VkDescriptorSetLayout, 1 + max_objects> layouts;
	layouts[0] = global_set_layout;
	for (uint32_t i = 0; i < max_objects; ++i) {
		layouts[1 + i] = object_set_layout;
	}

	const VkDescriptorSetAllocateInfo allocation = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = uint32_t(layouts.size()),
		.pSetLayouts = layouts.data(),
	};

	std::array<VkDescriptorSet, 1 + max_objects> sets;
	if (vkAllocateDescriptorSets(context.device, &allocation, sets.data()) != VK_SUCCESS) {
		std::cerr << "Failed to allocate descriptor sets\n";
		return false;
	}
	global_set = sets[0];
	for (uint32_t i = 0; i < max_objects; ++i) {
		object_sets[i] = sets[1 + i];
	}

	// Point every set to its uniform buffer
	std::array<VkDescriptorBufferInfo, 1 + max_objects> buffer_infos;
	std::array<VkWriteDescriptorSet, 1 + max_objects> writes;
	for (uint32_t i = 0; i < sets.size(); ++i) {
		const graphics::Buffer& buffer = i == 0 ? global_uniform_buffer : object_uniform_buffers[i - 1];
		buffer_infos[i] = {
			.buffer = buffer.buffer,
			.offset = 0,
			.range = buffer.size,
		};
		writes[i] = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = sets[i],
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &buffer_infos[i],
		};
	}
	vkUpdateDescriptorSets(context.device, uint32_t(writes.size()), writes.data(), 0, nullptr);

	return true;
}

bool createPipeline() {
	auto& context = graphics::internal::context;

	// set = 0 is global, set = 1 is per object, as in shaders/object.vert
	const VkDescriptorSetLayout set_layouts[] = { global_set_layout, object_set_layout };

	const VkPipelineLayoutCreateInfo layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = sizeof(set_layouts) / sizeof(set_layouts[0]),
		.pSetLayouts = set_layouts,
	};

	if (vkCreatePipelineLayout(context.device, &layout, nullptr, &pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create pipeline layout\n";
		return false;
	}

	const VkVertexInputBindingDescription vertex_bindings[] = {
		{
			.binding = 0,
			.stride = sizeof(geometry::Vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		},
	};

	const VkVertexInputAttributeDescription vertex_attributes[] = {
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
			.offset = offsetof(geometry::Vertex, color),
		},
	};

	const VkShaderModule vertex_shader = graphics::loadShaderModule("shaders/object.vert.spv");
	const VkShaderModule fragment_shader = graphics::loadShaderModule("shaders/object.frag.spv");

	if (vertex_shader != VK_NULL_HANDLE && fragment_shader != VK_NULL_HANDLE) {
		pipeline = graphics::createGraphicsPipeline({
			.layout = pipeline_layout,
			.render_pass = context.render_pass,
			.vertex_shader = vertex_shader,
			.fragment_shader = fragment_shader,
			.vertex_bindings = vertex_bindings,
			.vertex_binding_count = sizeof(vertex_bindings) / sizeof(vertex_bindings[0]),
			.vertex_attributes = vertex_attributes,
			.vertex_attribute_count = sizeof(vertex_attributes) / sizeof(vertex_attributes[0]),
		});
	}

	// Modules are only needed to create the pipeline
	vkDestroyShaderModule(context.device, vertex_shader, nullptr);
	vkDestroyShaderModule(context.device, fragment_shader, nullptr);

	return pipeline != VK_NULL_HANDLE;
}

void uploadUniforms() {
	const VkExtent2D extent = graphics::internal::context.swapchain_extent;
	const float aspect = float(extent.width) / float(extent.height);

	const uniforms::GlobalUniforms global = {
		.view_projection = scene::projectionMatrix(projection, aspect) * scene::viewMatrix(camera),
	};
	graphics::writeBuffer(global_uniform_buffer, &global, sizeof(global));

	for (size_t i = 0; i < objects.size(); ++i) {
		const scene::Object& object = objects[i];
		const glm::mat4 model = animate
		                            ? scene::animatedModelMatrix(object.transform, trajectory, clock.time, object.trajectory_phase)
		                            : scene::modelMatrix(object.transform);
		const uniforms::ObjectUniforms data = scene::objectUniforms(object, model);
		graphics::writeBuffer(object_uniform_buffers[i], &data, sizeof(data));
	}
}

void updateMouse() {
	const ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureMouse) {
		return; // Mouse is over the interface
	}

	if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
		scene::orbitCamera(camera, io.MouseDelta.x, io.MouseDelta.y);
	}
	if (io.MouseWheel != 0) {
		scene::zoomCamera(camera, io.MouseWheel);
	}
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

void cameraInterface() {
	if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::SliderFloat("Distance", &camera.distance, scene::camera_min_distance, scene::camera_max_distance,
	                   "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Yaw", &camera.yaw, -180, 180, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Pitch", &camera.pitch, -scene::camera_max_pitch, scene::camera_max_pitch,
	                   "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
	if (ImGui::Button("Reset camera")) {
		camera = {};
	}
	ImGui::TextDisabled("Drag with mouse to orbit, wheel to zoom");
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
	ImGui::TextDisabled("Own descriptor set per object (set = 1)");
}

void animationInterface() {
	if (!ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
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

void colorInterface(scene::Object& object) {
	if (!ImGui::CollapsingHeader("Color", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::ColorEdit3("Color", glm::value_ptr(object.color));
	ImGui::Checkbox("Vertex colors", &object.vertex_colors);
	ImGui::SameLine();
	ImGui::TextDisabled("(multiplied by the color)");
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

} // namespace

bool initialize() {
	applyStartupOptions();

	if (!createGeometry() || !createDescriptors() || !createPipeline()) {
		shutdown();
		return false;
	}

	return true;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);

	vkDestroyPipeline(context.device, pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr); // Frees its sets too
	vkDestroyDescriptorSetLayout(context.device, object_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, global_set_layout, nullptr);

	for (auto& buffer : object_uniform_buffers) {
		graphics::destroyBuffer(buffer);
	}
	graphics::destroyBuffer(global_uniform_buffer);
	graphics::destroyBuffer(index_buffer);
	graphics::destroyBuffer(vertex_buffer);
}

void update(double time) {
	// Animation runs even with hidden interface
	if (previous_time >= 0 && animate) {
		clock.advance(float(time - previous_time));
	}
	previous_time = time;

	if (startup_options.hide_ui) {
		return;
	}

	updateMouse();

	// Scene settings on the left, objects on the right
	constexpr float window_width = 340;
	const ImVec2 screen = ImGui::GetMainViewport()->WorkSize;

	ImGui::SetNextWindowPos({ 10, 10 }, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize({ window_width, 0 }, ImGuiCond_FirstUseEver);
	ImGui::Begin("Scene");
	ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
	ImGui::Text("Cylinder: %zu vertices, %zu triangles", mesh.vertices.size(), mesh.indices.size() / 3);
	projectionInterface();
	cameraInterface();
	animationInterface();
	ImGui::End();

	ImGui::SetNextWindowPos({ screen.x - window_width - 10, 10 }, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize({ window_width, 0 }, ImGuiCond_FirstUseEver);
	ImGui::Begin("Objects");
	objectsInterface();
	ImGui::SeparatorText("Selected object");
	transformInterface(objects[selected_object].transform);
	colorInterface(objects[selected_object]);
	ImGui::End();
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;

	// Uniform buffers are written here and not in update(): update() runs before prepare()
	// waits for the previous frame, which may still read these buffers on the GPU
	uploadUniforms();

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

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	const VkDeviceSize vertex_buffer_offset = 0;
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer.buffer, &vertex_buffer_offset);
	vkCmdBindIndexBuffer(fd.command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	// Set 0 once per frame, set 1 before each object
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
	                        0, 1, &global_set, 0, nullptr);
	for (size_t i = 0; i < objects.size(); ++i) {
		vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
		                        1, 1, &object_sets[i], 0, nullptr);
		vkCmdDrawIndexed(fd.command_buffer, uint32_t(mesh.indices.size()), 1, 0, 0, 0);
	}

	vkCmdEndRenderPass(fd.command_buffer);

	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application
