#include "vkx/vkx_core.h"

#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <stdio.h>

#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb_image.h"

VkxInstance vkx_instance = {0};
VkxSwapChain vkx_swap_chain = {0};
VkxSyncObjects vkx_sync_objects = {0};

VkxSwapChainSupportDetails vkx_query_swap_chain_support(VkPhysicalDevice device, VkSurfaceKHR surface) {
	VkxSwapChainSupportDetails details = {0};

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

	vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &details.formats_count, NULL);
	details.formats = malloc(sizeof(VkSurfaceFormatKHR) * details.formats_count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &details.formats_count, details.formats);

	vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &details.present_modes_count, NULL);
	details.present_modes = malloc(sizeof(VkPresentModeKHR) * details.present_modes_count);
	vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &details.present_modes_count, details.present_modes);
	
	return details;
}

void vkx_free_swap_chain_support(VkxSwapChainSupportDetails* details) {
	free(details->formats);
	free(details->present_modes);
}

VkxQueueFamilyIndices vkx_find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface) {
	VkxQueueFamilyIndices indices = {0};

	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, NULL);

	VkQueueFamilyProperties* queue_families = malloc(sizeof(VkQueueFamilyProperties) * queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families);

	for (uint32_t i = 0; i < queue_family_count; i++) {
		if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			indices.graphics_family = i;
			indices.has_graphics_family = true;
		}

		VkBool32 present_support = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);

		if (present_support) {
			indices.present_family = i;
			indices.has_present_family = true;
		}

		if (indices.has_graphics_family && indices.has_present_family) {
			break;
		}
	}

	free(queue_families);

	return indices;
}

static uint32_t vkx_find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) {
	/*
	 * See https://vulkan-tutorial.com/Vertex_buffers/Staging_buffer#page_Memory_types
	 * for an explanation of how this works
	 */
	VkPhysicalDeviceMemoryProperties mem_properties;
	vkGetPhysicalDeviceMemoryProperties(vkx_instance.physical_device, &mem_properties);

	for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
		if ((type_filter & (1 << i))
				&& (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}

	fprintf(stderr, "failed to find suitable memory type!");
	exit(1);
}

VkxBuffer vkx_create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
		VkMemoryPropertyFlags properties) {
	
	VkxBuffer buffer = {0};

	VkBufferCreateInfo buffer_info = {0};
	buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_info.size = size;
	buffer_info.usage = usage;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateBuffer(vkx_instance.device, &buffer_info, NULL, &buffer.buffer) != VK_SUCCESS) {
		fprintf(stderr, "Failed to create buffer");
		exit(1);
	}

	VkMemoryRequirements mem_requirements = {0};
	vkGetBufferMemoryRequirements(vkx_instance.device, buffer.buffer, &mem_requirements);

	VkMemoryAllocateInfo alloc_info = {0};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = mem_requirements.size;
	alloc_info.memoryTypeIndex = vkx_find_memory_type(mem_requirements.memoryTypeBits, properties);

	if (vkAllocateMemory(vkx_instance.device, &alloc_info, NULL, &buffer.memory) != VK_SUCCESS) {
		fprintf(stderr, "Failed to allocate buffer memory");
		exit(1);
	}

	vkBindBufferMemory(vkx_instance.device, buffer.buffer, buffer.memory, 0);

	return buffer;
}

void vkx_cleanup_buffer(VkxBuffer* buffer) {
	vkDestroyBuffer(vkx_instance.device, buffer->buffer, NULL);
	vkFreeMemory(vkx_instance.device, buffer->memory, NULL);

	buffer->buffer = VK_NULL_HANDLE;
	buffer->memory = VK_NULL_HANDLE;
}

VkImageView vkx_create_image_view(VkImage image, VkFormat format,
		VkImageAspectFlags aspect_flags) {
	VkImageViewCreateInfo view_info = {0};
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.image = image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = format;
	view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_info.subresourceRange.aspectMask = aspect_flags;
	view_info.subresourceRange.baseMipLevel = 0;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = 1;
	
	VkImageView image_view;
	if (vkCreateImageView(vkx_instance.device, &view_info, NULL, &image_view) != VK_SUCCESS) {
		fprintf(stderr, "failed to create image view!\n");
		exit(1);
	}

	return image_view;
}

VkxImage vkx_create_image(uint32_t width, uint32_t height, VkFormat format,
		VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties) {

	VkxImage image = {0};

	VkImageCreateInfo image_info = {0};
	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.extent.width = width;
	image_info.extent.height = height;
	image_info.extent.depth = 1;
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.format = format;
	image_info.tiling = tiling;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_info.usage = usage;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateImage(vkx_instance.device, &image_info, NULL, &image.image) != VK_SUCCESS) {
		fprintf(stderr, "Failed to create image!\n");
		exit(1);
	}

	VkMemoryRequirements mem_requirements;
	vkGetImageMemoryRequirements(vkx_instance.device, image.image, &mem_requirements);

	VkMemoryAllocateInfo alloc_info = {0};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = mem_requirements.size;
	alloc_info.memoryTypeIndex = vkx_find_memory_type(mem_requirements.memoryTypeBits, properties);

	if (vkAllocateMemory(vkx_instance.device, &alloc_info, NULL, &image.memory) != VK_SUCCESS) {
		fprintf(stderr, "Failed to allocate image memory!\n");
	}

	vkBindImageMemory(vkx_instance.device, image.image, image.memory, 0);

	image.view = VK_NULL_HANDLE;

	return image;
}

void vkx_cleanup_image(VkxImage* image) {
	if (image->view != VK_NULL_HANDLE) {
		vkDestroyImageView(vkx_instance.device, image->view, NULL);
	}
	vkDestroyImage(vkx_instance.device, image->image, NULL);
	vkFreeMemory(vkx_instance.device, image->memory, NULL);

	image->image = VK_NULL_HANDLE;
	image->memory = VK_NULL_HANDLE;
	image->view = VK_NULL_HANDLE;
}


VkCommandBuffer vkx_begin_single_time_commands() {
	/*
	 * Begin a one-time command buffer
	 */
	VkCommandBufferAllocateInfo alloc_info = {0};
	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandPool = vkx_instance.command_pool;
	alloc_info.commandBufferCount = 1;

	VkCommandBuffer command_buffer;
	vkAllocateCommandBuffers(vkx_instance.device, &alloc_info, &command_buffer);

	VkCommandBufferBeginInfo begin_info = {0};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(command_buffer, &begin_info);

	return command_buffer;
}

void vkx_end_single_time_commands(VkCommandBuffer command_buffer) {
	/*
	 * End a one-time command buffer
	 */
	if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
		fprintf(stderr, "failed to record command buffer!");
		exit(1);
	}

	VkSubmitInfo submit_info = {0};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;

	vkQueueSubmit(vkx_instance.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
	vkQueueWaitIdle(vkx_instance.graphics_queue);

	vkFreeCommandBuffers(vkx_instance.device, vkx_instance.command_pool, 1, &command_buffer);
}


VkFormat vkx_find_supported_format(VkFormat* candidates, size_t candidates_count, VkImageTiling tiling, VkFormatFeatureFlags features) {
	/* 
	 * Search through the list of candidates and find a supported format.
	 *
	 * @param candidates The list of candidate formats to search through
	 * @param candidates_count The number of candidate formats
	 * @param tiling The image tiling to use (linear or optimal)
	 * @param features The format features to check for
	 *
	 * Used to find the format of the depth buffer from the tutorial at:
	 * https://vulkan-tutorial.com/Depth_buffer
	 */
	for (size_t i = 0; i < candidates_count; i++) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(vkx_instance.physical_device, candidates[i], &props);

		if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
			return candidates[i];
		}
		else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
			return candidates[i];
		}
	}

	fprintf(stderr, "Failed to find supported format");
	exit(1);
}

VkFormat vkx_find_depth_format() {
	/*
	 * Find a supported depth format
	 */
	VkFormat candidates[] = {
		VK_FORMAT_D32_SFLOAT,
		VK_FORMAT_D32_SFLOAT_S8_UINT,
		VK_FORMAT_D24_UNORM_S8_UINT
	};

	return vkx_find_supported_format(candidates, sizeof(candidates) / sizeof(candidates[0]), VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

bool vkx_has_stencil_component(VkFormat format) {
	/*
	 * Check if the format has a stencil component
	 */
	switch (format) {
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
		case VK_FORMAT_D24_UNORM_S8_UINT:
			return true;
		default:
			return false;
	}
}

void vkx_transition_image_layout(
		VkCommandBuffer command_buffer,
		VkImage image,
		VkFormat format,
		VkImageLayout old_layout,
		VkImageLayout new_layout
) {
	VkImageMemoryBarrier2 barrier = {0};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	if (new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

		if (vkx_has_stencil_component(format)) {
			barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	}
	else {
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	}

	if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED
			&& new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		barrier.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		barrier.srcAccessMask = 0;
		barrier.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	}
	else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED
			&& new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		barrier.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		barrier.srcAccessMask = 0;
		barrier.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	}
	else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED
		   	&& new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	}
	else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
			&& new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else if (old_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
			&& new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
		barrier.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		barrier.srcAccessMask = 0;
		barrier.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	}
	else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
			&& new_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
		barrier.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	}
	else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
			&& new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		barrier.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		barrier.srcAccessMask = 0;
		barrier.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	}
	else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			&& new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
		barrier.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	}
	else {
		fprintf(stderr, "Unsupported layout transition from %d to %d\n", old_layout, new_layout);
		exit(1);
	}

	VkDependencyInfo dependency_info = {0};
	dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependency_info.imageMemoryBarrierCount = 1;
	dependency_info.pImageMemoryBarriers = &barrier;

	vkCmdPipelineBarrier2(
		command_buffer,
		&dependency_info
	);
}

void vkx_transition_image_layout_tmp_buffer(
		VkImage image,
		VkFormat format,
		VkImageLayout old_layout,
		VkImageLayout new_layout
) {
	VkCommandBuffer command_buffer = vkx_begin_single_time_commands();
	
	vkx_transition_image_layout(
		command_buffer,
		image,
		format,
		old_layout,
		new_layout
	);
	
	vkx_end_single_time_commands(command_buffer);
}

void vkx_copy_buffer(VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size) {
	/*
	 * Copy buffers - used to copy from staging buffer into vertext buffer
	 */
	VkCommandBuffer command_buffer = vkx_begin_single_time_commands();

	VkBufferCopy copy_region = {0};
	copy_region.size = size;
	vkCmdCopyBuffer(command_buffer, src_buffer, dst_buffer, 1, &copy_region);

	vkx_end_single_time_commands(command_buffer);
}

void vkx_copy_buffer_to_image(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer command_buffer = vkx_begin_single_time_commands();

	VkBufferImageCopy region = {0};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;

	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	
	region.imageOffset.x = 0;
	region.imageOffset.y = 0;
	region.imageOffset.z = 0;
	region.imageExtent.width = width;
	region.imageExtent.height = height;
	region.imageExtent.depth = 1;

	vkCmdCopyBufferToImage(
		command_buffer,
		buffer,
		image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&region
	);

    vkx_end_single_time_commands(command_buffer);
}

VkxImage vkx_create_texture_image(const char* filename) {
	int width, height, channels;

	printf("Loading texture image %s\n", filename);

	stbi_uc* pixels = stbi_load(filename, &width, &height, &channels, STBI_rgb_alpha);
	if (!pixels) {
		fprintf(stderr, "failed to load texture image!\n");
		exit(1);
	}

	VkDeviceSize image_size = width * height * 4;

	// Create a buffer and load the image into it
	VkxBuffer staging_buffer = vkx_create_buffer(
		image_size,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
	);

	void *data;
	vkMapMemory(vkx_instance.device, staging_buffer.memory, 0, image_size, 0, &data);
	memcpy(data, pixels, (size_t) image_size);
	vkUnmapMemory(vkx_instance.device, staging_buffer.memory);

	stbi_image_free(pixels);

	// Create the image
	VkxImage image = vkx_create_image(
		width,
		height,
		VK_FORMAT_R8G8B8A8_SRGB,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
	);

	// Transition the image layout to transfer destination
	vkx_transition_image_layout_tmp_buffer(image.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	
	vkx_copy_buffer_to_image(staging_buffer.buffer, image.image, width, height);

	// Transition the image layout to shader read
	vkx_transition_image_layout_tmp_buffer(image.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	
	// Clean up the staging buffer
	vkx_cleanup_buffer(&staging_buffer);

	// Create the image view
	image.view = vkx_create_image_view(image.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);

	return image;
}
