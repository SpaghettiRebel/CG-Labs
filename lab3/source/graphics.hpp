#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <vk_mem_alloc.h>

namespace graphics {

struct Buffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	void* memory = nullptr;
	VkDeviceSize size = 0;
};

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& result);

void destroyBuffer(Buffer& buffer);

void writeBuffer(Buffer& buffer, const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

// Memory on the GPU (VRAM): the CPU can't write there, data is copied once through a staging buffer
bool createGpuBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, Buffer& result);

struct Texture {
	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
};

// pixels: width * height * 4 bytes (RGBA8)
bool createTexture(const void* pixels, uint32_t width, uint32_t height, VkFormat format, Texture& result);

// PNG file
bool loadTexture(const char* path, VkFormat format, Texture& result);

void destroyTexture(Texture& texture);

// Bilinear filtering, texture coordinates repeat outside of [0, 1]
VkSampler createSampler();

VkShaderModule loadShaderModule(const char* path);

struct GraphicsPipelineInfo {
	VkPipelineLayout layout;
	VkRenderPass render_pass;
	VkShaderModule vertex_shader;
	VkShaderModule fragment_shader;
	const VkVertexInputBindingDescription* vertex_bindings;
	uint32_t vertex_binding_count;
	const VkVertexInputAttributeDescription* vertex_attributes;
	uint32_t vertex_attribute_count;
	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
};

VkPipeline createGraphicsPipeline(const GraphicsPipelineInfo& info);

} // namespace graphics
