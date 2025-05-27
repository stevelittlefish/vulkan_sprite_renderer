#ifndef VKX_CORE_H
#define VKX_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>

#define VKX_FRAMES_IN_FLIGHT 2

typedef struct {
	VkBuffer buffer;
	VkDeviceMemory memory;
} VkxBuffer;

typedef struct {
	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
} VkxImage;

typedef struct {
	// Vulkan instance
	VkInstance instance;
	// Vulkan debug messenger
	VkDebugUtilsMessengerEXT debug_messenger;
	// SDL window that we are rendering to
	SDL_Window* window;
	// Surface from the SDL window
	VkSurfaceKHR surface;
	// Physical device that we are using
	VkPhysicalDevice physical_device;
	// Logical device that we are using
	VkDevice device;
	// Graphics queue
	VkQueue graphics_queue;
	// Presentation queue
	VkQueue present_queue;
	// Single command pool for the program
	VkCommandPool command_pool;
	// Command buffers for each frame in flight
	VkCommandBuffer command_buffers[VKX_FRAMES_IN_FLIGHT];
	// Number of command buffers
	uint32_t command_buffers_count;
} VkxInstance;

typedef struct {
    uint32_t graphics_family;
    uint32_t present_family;
	bool has_graphics_family;
	bool has_present_family;
} VkxQueueFamilyIndices;

typedef struct {
    VkSurfaceCapabilitiesKHR capabilities;
	uint32_t formats_count;
	uint32_t present_modes_count;
	VkSurfaceFormatKHR* formats;
	VkPresentModeKHR* present_modes;
} VkxSwapChainSupportDetails;

typedef struct {
	// Vulkan swap chain
	VkSwapchainKHR swap_chain;
	// Length of images and image_views arrays
	uint32_t images_count;
	// Array of swap chain images
	VkImage* images;
	// Array of swap chain image views
	VkImageView* image_views;
	// Array of swap chain images
	VkFormat image_format;
	// Size of the swap chain images
	VkExtent2D extent;
	// Semaphore for each swapchain image
	VkSemaphore* render_finished_semaphores;
	// Depth buffer
	VkxImage depth_image;
	// Is the depth image created?
	bool has_depth_image;
} VkxSwapChain;

typedef struct {
	VkDescriptorSetLayout descriptor_set_layout;
	VkPipelineLayout layout;
	VkPipeline pipeline;
} VkxPipeline;

typedef struct {
	VkSemaphore image_available_semaphore;
	VkFence in_flight_fence;
} VkxFrameSyncObjects;


extern VkxInstance vkx_instance;
extern VkxSwapChain vkx_swap_chain;
extern VkxFrameSyncObjects vkx_frame_sync_objects[VKX_FRAMES_IN_FLIGHT];

VkxSwapChainSupportDetails vkx_query_swap_chain_support(VkPhysicalDevice device, VkSurfaceKHR surface);

void vkx_free_swap_chain_support(VkxSwapChainSupportDetails* details);

VkxQueueFamilyIndices vkx_find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface);

VkxImage vkx_create_image(uint32_t width, uint32_t height, VkFormat format,
		VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties);
void vkx_cleanup_image(VkxImage* image);

VkImageView vkx_create_image_view(VkImage image, VkFormat format,
		VkImageAspectFlags aspect_flags);

VkxBuffer vkx_create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
		VkMemoryPropertyFlags properties);
void vkx_cleanup_buffer(VkxBuffer* buffer);

VkCommandBuffer vkx_begin_single_time_commands();

void vkx_end_single_time_commands(VkCommandBuffer command_buffer);

VkFormat vkx_find_supported_format(VkFormat* candidates, size_t candidates_count, VkImageTiling tiling, VkFormatFeatureFlags features);

VkFormat vkx_find_depth_format();

bool vkx_has_stencil_component(VkFormat format);

void vkx_transition_image_layout_tmp_buffer(VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout);

void vkx_transition_image_layout(VkCommandBuffer command_buffer, VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout);

void vkx_copy_buffer(VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size);

void vkx_copy_buffer_to_image(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

VkxImage vkx_create_texture_image(const char* filename);

#endif // VKX_CORE_H
