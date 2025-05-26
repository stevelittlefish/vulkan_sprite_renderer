#include "vkx/vkx_swap_chain.h"
#include "vkx/vkx_core.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>

static VkExtent2D vkx_choose_swap_extent(SDL_Window* window, VkSurfaceCapabilitiesKHR *capabilities) {
	if (capabilities->currentExtent.width != 0xFFFFFFFF) {
		return capabilities->currentExtent;
	} else {
		int width, height;
		SDL_GetWindowSize(window, &width, &height);

		VkExtent2D actualExtent = {
			width,
			height
		};
		
		// Clamp the width and height to the min and max extents
		if (actualExtent.width < capabilities->minImageExtent.width) {
			actualExtent.width = capabilities->minImageExtent.width;
		}
		else if (actualExtent.width > capabilities->maxImageExtent.width) {
			actualExtent.width = capabilities->maxImageExtent.width;
		}

		if (actualExtent.height < capabilities->minImageExtent.height) {
			actualExtent.height = capabilities->minImageExtent.height;
		}
		else if (actualExtent.height > capabilities->maxImageExtent.height) {
			actualExtent.height = capabilities->maxImageExtent.height;
		}

		return actualExtent;
	}
}

void vkx_create_swap_chain(bool create_depth_image) {
	/*
	 * Create the swap chain
	 *
	 * @param create_depth_image Whether to create a depth image (for depth test)
	 */
	VkxSwapChainSupportDetails swap_chain_support = vkx_query_swap_chain_support(vkx_instance.physical_device, vkx_instance.surface);

	if (swap_chain_support.formats_count == 0) {
		fprintf(stderr, "Swap chain support not available (no formats)\n");
		exit(1);
	}
	else if (swap_chain_support.present_modes_count == 0) {
		fprintf(stderr, "Swap chain support not available (no present modes)\n");
		exit(1);
	}

	printf(" Swap chain support: %d formats, %d present modes\n", swap_chain_support.formats_count, swap_chain_support.present_modes_count);
	
	// Choose the best surface format from the available formats
	VkSurfaceFormatKHR surface_format = swap_chain_support.formats[0];
	for (uint32_t i = 0; i < swap_chain_support.formats_count; i++) {
		if (swap_chain_support.formats[i].format == VK_FORMAT_B8G8R8A8_SRGB && swap_chain_support.formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			surface_format = swap_chain_support.formats[i];
			break;
		}
	}
	
	// Choose the best present mode from the available present modes
	// VkPresentModeKHR presentMode = chooseSwapPresentMode(swap_chain_support.presentModes);
	VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
	for (uint32_t i = 0; i < swap_chain_support.present_modes_count; i++) {
		if (swap_chain_support.present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
			present_mode = swap_chain_support.present_modes[i];
			break;
		}
	}

	vkx_swap_chain.extent = vkx_choose_swap_extent(vkx_instance.window, &swap_chain_support.capabilities);
	printf(" Swap chain extent: %d x %d\n", vkx_swap_chain.extent.width, vkx_swap_chain.extent.height);

	uint32_t image_count = swap_chain_support.capabilities.minImageCount + 1;
	if (swap_chain_support.capabilities.maxImageCount > 0 && image_count > swap_chain_support.capabilities.maxImageCount) {
		image_count = swap_chain_support.capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR create_info = {0};
	create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	create_info.surface = vkx_instance.surface;

	create_info.minImageCount = image_count;
	create_info.imageFormat = surface_format.format;
	create_info.imageColorSpace = surface_format.colorSpace;
	create_info.imageExtent = vkx_swap_chain.extent;
	create_info.imageArrayLayers = 1;
	create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkxQueueFamilyIndices indices = vkx_find_queue_families(vkx_instance.physical_device, vkx_instance.surface);
	uint32_t queueFamilyIndices[] = {indices.graphics_family, indices.present_family};

	if (indices.graphics_family != indices.present_family) {
		create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		create_info.queueFamilyIndexCount = 2;
		create_info.pQueueFamilyIndices = queueFamilyIndices;
	} else {
		create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	}

	create_info.preTransform = swap_chain_support.capabilities.currentTransform;
	create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	create_info.presentMode = present_mode;
	create_info.clipped = VK_TRUE;

	if (vkCreateSwapchainKHR(vkx_instance.device, &create_info, NULL, &vkx_swap_chain.swap_chain) != VK_SUCCESS) {
		fprintf(stderr, "failed to create swap chain!");
		exit(1);
	}
	
	vkGetSwapchainImagesKHR(vkx_instance.device, vkx_swap_chain.swap_chain, &vkx_swap_chain.images_count, NULL);
	vkx_swap_chain.images = malloc(sizeof(VkImage) * vkx_swap_chain.images_count);
	vkGetSwapchainImagesKHR(vkx_instance.device, vkx_swap_chain.swap_chain, &vkx_swap_chain.images_count, vkx_swap_chain.images);
	
	// Transition the images to a valid layout
    // Begin the command buffer
    VkCommandBufferBeginInfo begin_info = {0};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = 0;

	// TODO: convert to single use command buffer?
	VkCommandBuffer command_buffer = vkx_instance.command_buffers[0];

    if (vkResetCommandBuffer(command_buffer, 0) != VK_SUCCESS) {
        fprintf(stderr, "failed to reset command buffer!");
        exit(1);
    }

    if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
        fprintf(stderr, "failed to begin recording command buffer!");
        exit(1);
    }
	
	// All of the swap chain images are now in the VK_IMAGE_LAYOUT_UNDEFINED layout, which is not a valid
	// layout for rendering - we need to transition them to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR

    // Transition the images to a valid layout
    for (uint32_t i = 0; i < vkx_swap_chain.images_count; i++) {
        VkImageMemoryBarrier barrier = {0};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = vkx_swap_chain.images[i];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dst_stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

        vkCmdPipelineBarrier(
            command_buffer,
            src_stage_mask, // Pipeline stage that source access mask applies to
            dst_stage_mask, // Pipeline stage that destination access mask applies to
            0,              // Dependency flags (not used in this case)
            0, NULL,        // Memory barriers
            0, NULL,        // Buffer memory barriers
            1, &barrier     // Image memory barriers
        );
    }

    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
        fprintf(stderr, "failed to record command buffer!");
        exit(1);
    }
	
	// Submit the command buffer
	VkSubmitInfo submit_info = {0};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;

	if (vkQueueSubmit(vkx_instance.graphics_queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS) {
		fprintf(stderr, "failed to submit command buffer!");
		exit(1);
	}
	
	// ----- Now create the image views -----
	vkx_swap_chain.image_views = malloc(sizeof(VkImageView) * vkx_swap_chain.images_count);

	for (size_t i = 0; i < vkx_swap_chain.images_count; i++) {
		vkx_swap_chain.image_views[i] = vkx_create_image_view(
			vkx_swap_chain.images[i], surface_format.format, VK_IMAGE_ASPECT_COLOR_BIT
		);
	}

	printf(" Image views created\n");

	// Wait for the queue to finish processing the command buffer
	vkQueueWaitIdle(vkx_instance.graphics_queue);

	vkx_swap_chain.image_format = surface_format.format;

	vkx_free_swap_chain_support(&swap_chain_support);

	// Create the depth resources
	// TODO: there should be 1 per frame
	vkx_swap_chain.has_depth_image = create_depth_image;
	if (create_depth_image) {
		VkFormat depth_format = vkx_find_depth_format();

		vkx_swap_chain.depth_image = vkx_create_image(
			vkx_swap_chain.extent.width,
			vkx_swap_chain.extent.height,
			depth_format,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);

		vkx_swap_chain.depth_image.view = vkx_create_image_view(vkx_swap_chain.depth_image.image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT);

		// Transition the image layout to depth stencil attachment
		vkx_transition_image_layout_tmp_buffer(
			vkx_swap_chain.depth_image.image, depth_format,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		);
		printf(" Depth image created\n");
	}

	printf(" Swap chain created with format: %d\n", vkx_swap_chain.image_format);
}

void vkx_cleanup_swap_chain() {
	printf("Cleaning up swap chain\n");
	
	if (vkx_swap_chain.has_depth_image) {
		vkx_cleanup_image(&vkx_swap_chain.depth_image);
	}

	for (size_t i = 0; i < vkx_swap_chain.images_count; i++) {
		vkDestroyImageView(vkx_instance.device, vkx_swap_chain.image_views[i], NULL);
	}

	free(vkx_swap_chain.image_views);
	vkx_swap_chain.image_views = NULL;
	free(vkx_swap_chain.images);
	vkx_swap_chain.images = NULL;
	vkx_swap_chain.images_count = 0;

	vkDestroySwapchainKHR(vkx_instance.device, vkx_swap_chain.swap_chain, NULL);
}

void vkx_recreate_swap_chain() {
	int width = 0, height = 0;
	SDL_GetWindowSize(vkx_instance.window, &width, &height);

	vkDeviceWaitIdle(vkx_instance.device);

	vkx_cleanup_swap_chain();

	vkx_create_swap_chain(vkx_swap_chain.has_depth_image);
}

