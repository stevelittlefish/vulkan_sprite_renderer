#ifndef VKX_PIPELINE_H
#define VKX_PIPELINE_H

#include <vulkan/vulkan.h>
#include "vkx/vkx_core.h"

VkShaderModule vkx_load_shader_module(const char* path);

VkxPipeline vkx_create_vertex_buffer_pipeline(
		const char* vert_shader_path,
		const char* frag_shader_path,
		VkVertexInputBindingDescription binding_description,
		VkVertexInputAttributeDescription* attribute_descriptions,
		size_t attribute_descriptions_count,
		VkPushConstantRange push_constant_range,
		uint32_t num_textures
);

VkxPipeline vkx_create_screen_pipeline(
		const char* vert_shader_path,
		const char* frag_shader_path
);

void vkx_cleanup_pipeline(VkxPipeline pipeline);

#endif  // VKX_PIPELINE_H
