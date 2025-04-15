#include "vkx/vkx_init.h"
#include "vkx/vkx_core.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

const bool enable_validation_layers = true;

#define VKX_NUM_VALIDATION_LAYERS 1
static const char* validation_layers[VKX_NUM_VALIDATION_LAYERS] = {
	"VK_LAYER_KHRONOS_validation"
};

#define VKX_NUM_DEVICE_EXTENSIONS 2
static const char* device_extensions[VKX_NUM_DEVICE_EXTENSIONS] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
};

static bool vkx_check_validation_layer_support() {
	uint32_t layer_count;
	vkEnumerateInstanceLayerProperties(&layer_count, NULL);
	
	VkLayerProperties* available_layers = malloc(sizeof(VkLayerProperties) * layer_count);
	vkEnumerateInstanceLayerProperties(&layer_count, available_layers);

	for (uint32_t i = 0; i < VKX_NUM_VALIDATION_LAYERS; i++) {
		bool layer_found = false;
		
		for (uint32_t j = 0; j < layer_count; j++) {
			if (strcmp(validation_layers[i], available_layers[j].layerName) == 0) {
				layer_found = true;
				break;
			}
		}

		if (!layer_found) {
			return false;
		}
	}

	return true;
}

static char const * const * vkx_get_required_extensions(uint32_t* count) {
	/*
	 * If validation layers are enabled, we need to request the VK_EXT_DEBUG_UTILS_EXTENSION_NAME extension
	 * as well as the extensions required by GLFW.
	 *
	 * Note: calling this repeatedly could cause a memory leak as we create a new array each time
	 * if validation layers are enabled.
	 */
	assert(count != NULL);
	
	// Get the required extensions from GLFW and set count to the number of extensions
	char const * const * sdl_extensions = SDL_Vulkan_GetInstanceExtensions(count);

	if (sdl_extensions == NULL) {
		fprintf(stderr, "Failed to get required extensions from GLFW\n");
		exit(1);
	}
	
	if (!enable_validation_layers) {
		return sdl_extensions;
	}

	// If validation layers are enabled, add the debug utils extension
	const char** extensions = malloc(sizeof(const char*) * (*count + 1));
	memcpy(extensions, sdl_extensions, sizeof(const char*) * *count);
	extensions[*count] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	*count += 1;
	
	return (char const * const *) extensions;
}

// static VKAPI_ATTR VkBool32 VKAPI_CALL vkx_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
static VKAPI_ATTR VkBool32 VKAPI_CALL vkx_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
	fprintf(stderr, "Validation Layer (%d, %d): ", messageSeverity, messageType);
	fprintf(stderr, "%s\n", pCallbackData->pMessage);

	// Ignore user data but prevent compiler warning
	if (pUserData) {
		// Do nothing
	}

	return VK_FALSE;
}

static void vkx_populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT* create_info) {
	*create_info = (VkDebugUtilsMessengerCreateInfoEXT) {0};
	create_info->sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	create_info->messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	create_info->messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	create_info->pfnUserCallback = vkx_debug_callback;
}

static VkPhysicalDevice vkx_pick_physical_device(void) {
	uint32_t device_count = 0;
	vkEnumeratePhysicalDevices(vkx_instance.instance, &device_count, NULL);

	if (device_count == 0) {
		fprintf(stderr, "failed to find GPUs with Vulkan support!\n");
		exit(1);
	}

	VkPhysicalDevice* devices = malloc(sizeof(VkPhysicalDevice) * device_count);
	vkEnumeratePhysicalDevices(vkx_instance.instance, &device_count, devices);

	// First let's print some info about all of the found devices
	printf(" Found %d physical devices:\n", device_count);
	for (uint32_t i = 0; i < device_count; i++) {
		VkPhysicalDeviceProperties device_properties;
		vkGetPhysicalDeviceProperties(devices[i], &device_properties);
		printf("  Device %d: %s\n", i, device_properties.deviceName);
	}
	
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;

	for (uint32_t i = 0; i < device_count; i++) {
		printf(" Physical Device %d\n", i);
        VkxQueueFamilyIndices indices = vkx_find_queue_families(devices[i], vkx_instance.surface);

		printf("  Graphics Family: %d\n", indices.graphics_family);
		printf("  Present Family: %d\n", indices.present_family);
		
        uint32_t extension_count;
        vkEnumerateDeviceExtensionProperties(devices[i], NULL, &extension_count, NULL);
		
		VkExtensionProperties* available_extensions = malloc(sizeof(VkExtensionProperties) * extension_count);
        vkEnumerateDeviceExtensionProperties(devices[i], NULL, &extension_count, available_extensions);
		
		// Check all of the required extensions are supported
		bool required_extensions_supported = true;

		for (uint32_t j = 0; j < VKX_NUM_DEVICE_EXTENSIONS; j++) {
			bool extension_found = false;
			for (uint32_t k = 0; k < extension_count; k++) {
				if (strcmp(device_extensions[j], available_extensions[k].extensionName) == 0) {
					extension_found = true;
					break;
				}
			}

			if (!extension_found) {
				required_extensions_supported = false;
				printf("Extension %s not supported\n", device_extensions[j]);
				break;
			}
		}

		free(available_extensions);

		if (!required_extensions_supported) {
			continue;
		}

		// Check the features we need are supported
		VkPhysicalDeviceFeatures features;
		vkGetPhysicalDeviceFeatures(devices[i], &features);

		if (!features.samplerAnisotropy) {
			printf("Sampler anisotropy not supported\n");
			continue;
		}

		// Check the device supports the required swap chain features
		bool swap_chain_adequate = false;

		if (indices.has_present_family) {
			VkxSwapChainSupportDetails swap_chain_support = vkx_query_swap_chain_support(devices[i], vkx_instance.surface);
			swap_chain_adequate = swap_chain_support.formats_count > 0 && swap_chain_support.present_modes_count > 0;
			vkx_free_swap_chain_support(&swap_chain_support);
		}

        if (indices.has_graphics_family
				&& indices.has_present_family
				&& swap_chain_adequate) {

			VkPhysicalDeviceProperties device_properties;
			vkGetPhysicalDeviceProperties(devices[i], &device_properties);
			printf(" Device %d (%s) is suitable\n", i, device_properties.deviceName);

			physical_device = devices[i];
			break;
		}
	}

	free(devices);
	return physical_device;
}

void vkx_init(SDL_Window* window) {
	printf("Initialising Vulkan (VKX)\n");

	// Keep a reference to the window to avoid passing it around later
	vkx_instance.window = window;

	if (enable_validation_layers && !vkx_check_validation_layer_support()) {
		fprintf(stderr, "validation layers requested, but not available!");
		exit(1);
	}
	
	VkApplicationInfo app_info = {0};
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = "Hello Triangle";
	app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.pEngineName = "No Engine";
	app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.apiVersion = VK_API_VERSION_1_3;

	VkInstanceCreateInfo instance_create_info = {0};
	instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_create_info.pApplicationInfo = &app_info;

	instance_create_info.ppEnabledExtensionNames = vkx_get_required_extensions(&instance_create_info.enabledExtensionCount);

	printf(" Requesting instance extensions:\n");
	for (uint32_t i = 0; i < instance_create_info.enabledExtensionCount; i++) {
		printf("  Extension: %s\n", instance_create_info.ppEnabledExtensionNames[i]);
	}

	VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {0};
	if (enable_validation_layers) {
		printf(" Enabling validation layers:\n");
		for (uint32_t i = 0; i < VKX_NUM_VALIDATION_LAYERS; i++) {
			printf("  Layer: %s\n", validation_layers[i]);
		}

		instance_create_info.enabledLayerCount = VKX_NUM_VALIDATION_LAYERS;
		instance_create_info.ppEnabledLayerNames = validation_layers;

		vkx_populate_debug_messenger_create_info(&debug_create_info);
		instance_create_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT*) &debug_create_info;
	} else {
		instance_create_info.enabledLayerCount = 0;
		instance_create_info.pNext = NULL;
	}
	
	// ----- Create the Vulkan instance -----
	if (vkCreateInstance(&instance_create_info, NULL, &vkx_instance.instance) != VK_SUCCESS) {
		fprintf(stderr, "failed to create instance!");
		exit(1);
	}

	// ----- Create the debug messenger -----
	if (enable_validation_layers) {
		VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info;
		vkx_populate_debug_messenger_create_info(&debug_messenger_create_info);
		
		// Create the debug messenger
		PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(vkx_instance.instance, "vkCreateDebugUtilsMessengerEXT");
		if (func == NULL) {
			fprintf(stderr, "failed to get vkCreateDebugUtilsMessengerEXT function pointer\n");
			exit(VK_ERROR_EXTENSION_NOT_PRESENT);
		}
	}

	// ----- Create the window surface -----
	if (!SDL_Vulkan_CreateSurface(window, vkx_instance.instance, NULL, &vkx_instance.surface)) {
		fprintf(stderr, "failed to create window surface!");
		exit(1);
	}
	
	// ----- Pick a physical device -----
	
	// Next find a phyiscal device (i.e. a GPU) that supports the required features
	vkx_instance.physical_device = vkx_pick_physical_device();

	if (vkx_instance.physical_device == VK_NULL_HANDLE) {
		fprintf(stderr, "failed to find a suitable GPU!\n");
		exit(1);
	}

	// ----- Create the logical device -----

	// Next we need to create a logical device to interface with the physical device
	// (and also the graphics and presentation queues)
	
	VkxQueueFamilyIndices physical_indices = vkx_find_queue_families(vkx_instance.physical_device, vkx_instance.surface);
	
	// I don't fully understand why, but sometimes it looks like both families could be the same
	uint32_t unique_queue_families[2] = {physical_indices.graphics_family, physical_indices.present_family};
	uint32_t num_unique_queue_families = unique_queue_families[0] == unique_queue_families[1] ? 1 : 2;
	VkDeviceQueueCreateInfo* queue_create_infos = malloc(sizeof(VkDeviceQueueCreateInfo) * num_unique_queue_families);

	float queue_priority = 1.0f;
	for (uint32_t i = 0; i < num_unique_queue_families; i++) {
		VkDeviceQueueCreateInfo queue_create_info = {0};
		queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_create_info.queueFamilyIndex = unique_queue_families[i];
		queue_create_info.queueCount = 1;
		queue_create_info.pQueuePriorities = &queue_priority;

		queue_create_infos[i] = queue_create_info;
	}

	VkPhysicalDeviceVulkan13Features vulkan13_features = {0};
	vulkan13_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	vulkan13_features.dynamicRendering = VK_TRUE;
	vulkan13_features.synchronization2 = VK_TRUE;

	VkPhysicalDeviceVulkan12Features vulkan12_features = {0};
	vulkan12_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	vulkan12_features.descriptorIndexing = VK_TRUE;
	vulkan12_features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	vulkan12_features.pNext = &vulkan13_features;
	
	VkPhysicalDeviceFeatures2 features2 = {0};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features2.features.samplerAnisotropy = VK_TRUE;
	features2.pNext = &vulkan12_features;

	VkDeviceCreateInfo create_info = {0};
	create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

	create_info.queueCreateInfoCount = num_unique_queue_families;
	create_info.pQueueCreateInfos = queue_create_infos;

	create_info.enabledExtensionCount = VKX_NUM_DEVICE_EXTENSIONS;
	create_info.ppEnabledExtensionNames = device_extensions;

	create_info.pNext = &features2;

	printf(" Requesting device extensions:\n");

	for (uint32_t i = 0; i < create_info.enabledExtensionCount; i++) {
		printf("  Extension: %s\n", create_info.ppEnabledExtensionNames[i]);
	}

	if (enable_validation_layers) {
		create_info.enabledLayerCount = VKX_NUM_VALIDATION_LAYERS;
		create_info.ppEnabledLayerNames = validation_layers;
	} else {
		create_info.enabledLayerCount = 0;
	}

	if (vkCreateDevice(vkx_instance.physical_device, &create_info, NULL, &vkx_instance.device) != VK_SUCCESS) {
		fprintf(stderr, "failed to create logical device!\n");
		exit(1);
	}

	free(queue_create_infos);

	vkGetDeviceQueue(vkx_instance.device, physical_indices.graphics_family, 0, &vkx_instance.graphics_queue);
	vkGetDeviceQueue(vkx_instance.device, physical_indices.present_family, 0, &vkx_instance.present_queue);

	// ----- Create the command pool -----
	VkCommandPoolCreateInfo command_pool_info = {0};
	command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_info.queueFamilyIndex = physical_indices.graphics_family;

	if (vkCreateCommandPool(vkx_instance.device, &command_pool_info, NULL, &vkx_instance.command_pool) != VK_SUCCESS) {
		fprintf(stderr, "failed to create command pool!\n");
		exit(1);
	}

	// ----- Create the command buffers -----
	vkx_instance.command_buffers_count = VKX_FRAMES_IN_FLIGHT;

	VkCommandBufferAllocateInfo buf_alloc_info = {0};
	buf_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	buf_alloc_info.commandPool = vkx_instance.command_pool;
	buf_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	buf_alloc_info.commandBufferCount = vkx_instance.command_buffers_count;

	if (vkAllocateCommandBuffers(vkx_instance.device, &buf_alloc_info, vkx_instance.command_buffers) != VK_SUCCESS) {
		fprintf(stderr, "failed to allocate command buffers!\n");
		exit(1);
	}
}

void vkx_cleanup_instance() {
	printf("Cleaning up Vulkan Instance (VKX)\n");

	vkDestroyCommandPool(vkx_instance.device, vkx_instance.command_pool, NULL);

	vkDestroyDevice(vkx_instance.device, NULL);

	if (enable_validation_layers) {
		PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(vkx_instance.instance, "vkDestroyDebugUtilsMessengerEXT");
		if (func != NULL) {
			func(vkx_instance.instance, vkx_instance.debug_messenger, NULL);
		}
	}

	vkDestroySurfaceKHR(vkx_instance.instance, vkx_instance.surface, NULL);
	vkDestroyInstance(vkx_instance.instance, NULL);
}
