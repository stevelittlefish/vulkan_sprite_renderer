#include "vkx/vkx_sync.h"

#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>

#include "vkx/vkx_core.h"

void vkx_init_sync_objects(void) {
	vkx_sync_objects = (VkxSyncObjects) {0};
	VkSemaphoreCreateInfo semaphore_info = {0};
	semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fence_info = {0};
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (size_t i = 0; i < VKX_FRAMES_IN_FLIGHT; i++) {
		if (vkCreateSemaphore(vkx_instance.device, &semaphore_info, NULL, &vkx_sync_objects.image_available_semaphores[i]) != VK_SUCCESS
				|| vkCreateSemaphore(vkx_instance.device, &semaphore_info, NULL, &vkx_sync_objects.render_finished_semaphores[i]) != VK_SUCCESS) {
			fprintf(stderr, "failed to create semaphores for a frame!\n");
			exit(1);
		}

		if (vkCreateFence(vkx_instance.device, &fence_info, NULL, &vkx_sync_objects.in_flight_fences[i]) != VK_SUCCESS) {
			fprintf(stderr, "failed to create synchronization objects for a frame!\n");
			exit(1);
		}
	}
}

void vkx_cleanup_sync_objects(void) {
	for (size_t i = 0; i < VKX_FRAMES_IN_FLIGHT; i++) {
		vkDestroySemaphore(vkx_instance.device, vkx_sync_objects.render_finished_semaphores[i], NULL);
		vkDestroySemaphore(vkx_instance.device, vkx_sync_objects.image_available_semaphores[i], NULL);
		vkDestroyFence(vkx_instance.device, vkx_sync_objects.in_flight_fences[i], NULL);
	}

}
