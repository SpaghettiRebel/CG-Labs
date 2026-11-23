#include "graphics.hpp"

#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <vector>

#include <lodepng.h>

#include "graphics_internal.hpp"

namespace graphics {

namespace {

// Records commands, sends them to the GPU and waits until they are executed
bool submitOneShot(const std::function<void(VkCommandBuffer)>& record) {
	const auto& context = internal::context;

	const VkCommandPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = context.graphics_queue_index,
	};

	VkCommandPool pool = VK_NULL_HANDLE;
	if (vkCreateCommandPool(context.device, &pool_info, nullptr, &pool) != VK_SUCCESS) {
		std::cerr << "Failed to create one-shot command pool\n";
		return false;
	}

	const VkCommandBufferAllocateInfo allocation = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	const VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	const VkFenceCreateInfo fence_info = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
	};

	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;
	bool success = vkAllocateCommandBuffers(context.device, &allocation, &command_buffer) == VK_SUCCESS &&
	               vkCreateFence(context.device, &fence_info, nullptr, &fence) == VK_SUCCESS &&
	               vkBeginCommandBuffer(command_buffer, &begin) == VK_SUCCESS;

	if (success) {
		record(command_buffer);

		const VkSubmitInfo submit = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &command_buffer,
		};

		success = vkEndCommandBuffer(command_buffer) == VK_SUCCESS &&
		          vkQueueSubmit(context.graphics_queue, 1, &submit, fence) == VK_SUCCESS &&
		          vkWaitForFences(context.device, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
	}
	if (!success) {
		std::cerr << "Failed to execute one-shot commands\n";
	}

	vkDestroyFence(context.device, fence, nullptr);
	vkDestroyCommandPool(context.device, pool, nullptr); // Frees its command buffer too
	return success;
}

} // namespace

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& result) {
	const auto& context = internal::context;

	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
		         VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	VmaAllocationInfo allocation{};
	if (vmaCreateBuffer(context.allocator, &buffer_info, &allocation_info,
	                    &result.buffer, &result.allocation, &allocation) != VK_SUCCESS) {
		std::cerr << "Failed to create and allocate buffer of " << size << " bytes\n";
		return false;
	}

	result.memory = allocation.pMappedData;
	result.size = size;
	return true;
}

void destroyBuffer(Buffer& buffer) {
	if (buffer.buffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(internal::context.allocator, buffer.buffer, buffer.allocation);
	}
	buffer = {};
}

void writeBuffer(Buffer& buffer, const void* data, VkDeviceSize size, VkDeviceSize offset) {
	std::memcpy(static_cast<char*>(buffer.memory) + offset, data, size);
	vmaFlushAllocation(internal::context.allocator, buffer.allocation, offset, size);
}

bool createGpuBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, Buffer& result) {
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	// No flags for CPU access: the memory is on the GPU
	const VmaAllocationCreateInfo allocation_info = {
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	if (vmaCreateBuffer(internal::context.allocator, &buffer_info, &allocation_info,
	                    &result.buffer, &result.allocation, nullptr) != VK_SUCCESS) {
		std::cerr << "Failed to create and allocate GPU buffer of " << size << " bytes\n";
		return false;
	}
	result.size = size;

	Buffer staging;
	if (!createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging)) {
		return false;
	}
	writeBuffer(staging, data, size);

	const bool success = submitOneShot([&](VkCommandBuffer command_buffer) {
		const VkBufferCopy copy = {
			.size = size,
		};
		vkCmdCopyBuffer(command_buffer, staging.buffer, result.buffer, 1, &copy);

		// Copy must end before the vertex stage reads the data
		const VkMemoryBarrier barrier = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT,
		};
		vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		                     0, 1, &barrier, 0, nullptr, 0, nullptr);
	});

	destroyBuffer(staging);
	return success;
}

bool createImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, Texture& result) {
	const auto& context = internal::context;

	const VkImageCreateInfo image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = { width, height, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	if (vmaCreateImage(context.allocator, &image_info, &allocation_info,
	                   &result.image, &result.allocation, nullptr) != VK_SUCCESS) {
		std::cerr << "Failed to create and allocate image of " << width << "x" << height << " pixels\n";
		return false;
	}

	const VkImageSubresourceRange whole_image = {
		.aspectMask = aspect,
		.levelCount = 1,
		.layerCount = 1,
	};

	const VkImageViewCreateInfo view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = result.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = format,
		.subresourceRange = whole_image,
	};

	if (vkCreateImageView(context.device, &view_info, nullptr, &result.view) != VK_SUCCESS) {
		std::cerr << "Failed to create image view\n";
		return false;
	}

	return true;
}

bool createTexture(const void* pixels, uint32_t width, uint32_t height, VkFormat format, Texture& result) {
	if (!createImage(width, height, format, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
	                 VK_IMAGE_ASPECT_COLOR_BIT, result)) return false;
	const VkImageSubresourceRange whole_image{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	const VkDeviceSize size = VkDeviceSize(width) * height * 4;
	Buffer staging;
	if (!createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging)) {
		return false;
	}
	writeBuffer(staging, pixels, size);

	const bool success = submitOneShot([&](VkCommandBuffer command_buffer) {
		VkImageMemoryBarrier barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = result.image,
			.subresourceRange = whole_image,
		};
		vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                     0, 0, nullptr, 0, nullptr, 1, &barrier);

		const VkBufferImageCopy copy = {
			.imageSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.layerCount = 1,
			},
			.imageExtent = { width, height, 1 },
		};
		vkCmdCopyBufferToImage(command_buffer, staging.buffer, result.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                       1, &copy);

		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		                     0, 0, nullptr, 0, nullptr, 1, &barrier);
	});

	destroyBuffer(staging);
	return success;
}

bool loadTexture(const char* path, VkFormat format, Texture& result) {
	std::vector<unsigned char> pixels;
	unsigned width = 0, height = 0;
	const unsigned error = lodepng::decode(pixels, width, height, path); // RGBA8 by default
	if (error != 0) {
		std::cerr << "Failed to load texture " << path << ": " << lodepng_error_text(error)
		          << " (is the working directory the project root?)\n";
		return false;
	}

	return createTexture(pixels.data(), width, height, format, result);
}

void destroyTexture(Texture& texture) {
	const auto& context = internal::context;
	vkDestroyImageView(context.device, texture.view, nullptr);
	if (texture.image != VK_NULL_HANDLE) {
		vmaDestroyImage(context.allocator, texture.image, texture.allocation);
	}
	texture = {};
}

VkSampler createSampler() {
	const VkSamplerCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
	};

	VkSampler result = VK_NULL_HANDLE;
	if (vkCreateSampler(internal::context.device, &info, nullptr, &result) != VK_SUCCESS) {
		std::cerr << "Failed to create sampler\n";
	}
	return result;
}

VkShaderModule loadShaderModule(const char* path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		std::cerr << "Failed to open shader " << path << " (is the working directory the project root?)\n";
		return VK_NULL_HANDLE;
	}

	const size_t size = file.tellg();
	if (size == 0 || size % sizeof(uint32_t) != 0) {
		std::cerr << "Shader " << path << " is not a SPIR-V binary\n";
		return VK_NULL_HANDLE;
	}

	std::vector<uint32_t> code(size / sizeof(uint32_t));
	file.seekg(0);
	file.read(reinterpret_cast<char*>(code.data()), size);

	const VkShaderModuleCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = code.data(),
	};

	VkShaderModule result = VK_NULL_HANDLE;
	if (vkCreateShaderModule(internal::context.device, &info, nullptr, &result) != VK_SUCCESS) {
		std::cerr << "Failed to create shader module from " << path << '\n';
		return VK_NULL_HANDLE;
	}

	return result;
}

VkPipeline createGraphicsPipeline(const GraphicsPipelineInfo& info) {
	const bool shadow = info.shadow_format != VK_FORMAT_UNDEFINED;
	const VkPipelineShaderStageCreateInfo stages[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = info.vertex_shader,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = info.fragment_shader,
			.pName = "main",
		},
	};

	const VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = info.vertex_binding_count,
		.pVertexBindingDescriptions = info.vertex_bindings,
		.vertexAttributeDescriptionCount = info.vertex_attribute_count,
		.pVertexAttributeDescriptions = info.vertex_attributes,
	};

	const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = info.topology,
	};

	// Actual values are set with vkCmdSetViewport/vkCmdSetScissor
	const VkPipelineViewportStateCreateInfo viewport = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	const VkPipelineRasterizationStateCreateInfo rasterization = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable = shadow,
		.lineWidth = 1.0f,
	};

	const VkPipelineMultisampleStateCreateInfo multisample = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	};

	const VkPipelineColorBlendAttachmentState color_blend_attachment = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	const VkPipelineColorBlendStateCreateInfo color_blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &color_blend_attachment,
	};

	const VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_DEPTH_BIAS,
	};

	const VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = shadow ? 3u : 2u,
		.pDynamicStates = dynamic_states,
	};

	const VkPipelineRenderingCreateInfo rendering_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.depthAttachmentFormat = info.shadow_format,
	};
	const VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = shadow ? &rendering_info : nullptr,
		.stageCount = shadow ? 1u : 2u,
		.pStages = stages,
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport,
		.pRasterizationState = &rasterization,
		.pMultisampleState = &multisample,
		.pDepthStencilState = &depth_stencil,
		.pColorBlendState = shadow ? nullptr : &color_blend,
		.pDynamicState = &dynamic_state,
		.layout = info.layout,
		.renderPass = info.render_pass,
		.subpass = 0,
	};

	VkPipeline pipeline = VK_NULL_HANDLE;
	if (vkCreateGraphicsPipelines(internal::context.device, VK_NULL_HANDLE, 1, &pipeline_info,
	                              nullptr, &pipeline) != VK_SUCCESS) {
		std::cerr << "Failed to create graphics pipeline\n";
		return VK_NULL_HANDLE;
	}

	return pipeline;
}

} // namespace graphics
