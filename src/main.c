/*
 * This is a sprite based renderer in Vulkan.  This uses Vulkan 1.3 and uses
 * dynamic rendering and synchronisation2.
 *
 * The main rendering process works as following:
 *
 * 1. Render the tilemap from vertex data to an offscreen image
 *
 *    This is straight forward - the vertices are stored in the obvious way
 *
 * 2. Render the sprites from a vertex buffer which contains the sprite data
 *
 *    This time the vertices are generated in the vertex shader, the transform
 *    data is stored in a uniform buffer and the "vertex" data actually stores
 *    offsets into the array of transforms in the uniform buffer.
 *
 *    Again this is rendered to an offscreen image.
 *
 * 3. Render the offscreen image to the swapchain image
 *
 *    This serves 2 purposes - it allows us to keep the same resolution even if
 *    the window is resized (useful for pixel art) and also allows us to implement
 *    post processing effects.
 * 
 * In both of the first steps we use a depth buffer so that we can order the sprites
 * by their z-coordinate.
 *
 * Copyright (c) 2025 Stephen Brown
 *
 * LICENSE: This program is distributed under the WTFPL license. You can do whatever
 * you want with it, as long as you don't hold me responsible for anything.
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <math.h>

#include <cglm/cglm.h>

#include "io.h"

#include "vkx/vkx.h"

// Struct for vertex based geometry (i.e. the tiles)
typedef struct {
	vec3 pos;
	vec2 tex_coord;
} Vertex;


// Struct for the uniform buffer object for all shaders
typedef struct {
	// Time to use in shaders
	float t;
	// Matrices for sprites
	// This is basically the limit to fit the ubo in 64k
	mat4 mvps[1000];
} UniformBufferObject;

// This struct stores a sprite in a vertex array
typedef struct {
	// RGBA colour for rendering
	vec4 color;
	// Texture coordinates
	vec2 uv;
	vec2 uv2;
	// Texture index
	uint32_t texture_index;
	// Index into arrays in ubo
	uint32_t sprite_index;
} VertexBufferSprite;

// Push constants - are used by the tilemap (default) shader
typedef struct {
	// Single combined model view projection matrix
    mat4 mvp;
	// RGBA colour for rendering
	vec4 color;
	// Texture index
	uint32_t texture_index;
} PushConstants;

// Monster struct for game logic
typedef struct {
	vec3 pos;
	vec2 spd;
	vec4 color;
	uint32_t texture;
} Monster;

// Texture indices
enum Texture {
	TEX_TILES,
	TEX_MONSTERS,
	TEX_MONSTERS2,
	TEX_MONSTERS3,
	TEX_MONSTERS4,

	_TEX_COUNT
};

#define TILESET_X_TILES 3
#define TILESET_Y_TILES 3

#define TILESET_TOTAL_TILES (TILESET_X_TILES * TILESET_Y_TILES)
#define EMPTY TILESET_TOTAL_TILES

#define X_TILES 32
#define Y_TILES 24

#define TOTAL_TILES (X_TILES * Y_TILES)

#define NUM_MONSTERS 1000

const bool limit_fps = false;
const double min_frame_time = 1.0 / 120.0;

uint8_t tiles[X_TILES * Y_TILES] = {0};

const uint32_t SCREEN_WIDTH = X_TILES * 32;
const uint32_t SCREEN_HEIGHT = Y_TILES * 32;

const uint32_t DEFAULT_WIDTH = X_TILES * 32;
const uint32_t DEFAULT_HEIGHT = Y_TILES * 32;

SDL_Window* window = NULL;

// Single descriptor pool for the whole app
VkDescriptorPool descriptor_pool = {0};
// Descriptor sets for the main pipelines
VkDescriptorSet descriptor_sets[VKX_FRAMES_IN_FLIGHT] = {0};
// Descriptor sets for the screen pipeline
VkDescriptorSet screen_descriptor_sets[VKX_FRAMES_IN_FLIGHT] = {0};

// Which frame in the frames in flight are we rendering?
uint32_t current_frame = 0;

// Used to recreate swap chain on resize
bool framebuffer_resized = false;

// To store our textures
VkxImage* textures;
VkSampler texture_sampler;

// Offscreen image for rendering to
VkxImage offscreen_images[VKX_FRAMES_IN_FLIGHT] = {0};
VkxImage depth_images[VKX_FRAMES_IN_FLIGHT] = {0};

// Vertices for the tilemap
size_t vertices_count = 0;
Vertex* vertices = NULL;

// Indices for the tilemap
size_t vertex_indices_count = 0;
uint16_t* vertex_indices = NULL;

// "Vertices" for the sprites
size_t vertex_sprites_count = 0;
VertexBufferSprite* vertex_sprites = NULL;

// Buffers to feed the pipelines
VkxBuffer vertex_buffer = {0};
VkxBuffer index_buffer = {0};
VkxBuffer sprite_vertex_buffer = {0};

// Uniform buffer used in all pipelines
VkxBuffer uniform_buffers[VKX_FRAMES_IN_FLIGHT] = {0};
void* uniform_buffers_mapped[VKX_FRAMES_IN_FLIGHT] = {0};

// Matrices for rendering
mat4 projection_matrix;
mat4 view_matrix;

// Last frame time (elapsed since start of app in seconds)
double t_last = 0.0;
// Current time
double t = 0.0;

// Length of texture array
const uint32_t num_textures = (uint32_t) _TEX_COUNT;

// Monster data
Monster monsters[NUM_MONSTERS] = {0};

// Tile pipeline draws the tiles from the vertex data
VkxPipeline tile_pipeline = {0};
// Screen pipeline for blitting offscreen image to the swapchain
VkxPipeline screen_pipeline = {0};
// Sprite pipeline generates its own vertices in the shader
VkxPipeline sprite_pipeline = {0};

bool fullscreen = false;

// FPS counter
uint32_t frame_count = 0;
double last_fps_time = 0.0;

// If the swap chain is suboptimal, we record how many cycles it was suboptimal for
// in this variable as some older graphics cards occasionally report this for just
// a single cycle and recreating the swap chain causes more problems than it solves
// in that particular situation
uint32_t suboptimal_swapchain_count = 0;
const uint32_t SUBOPTIMAL_SWAPCHAIN_THRESHOLD = 10;

VkVertexInputBindingDescription get_binding_description() {
	VkVertexInputBindingDescription binding_description = {0};
	binding_description.binding = 0;
	binding_description.stride = sizeof(Vertex);
	binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	return binding_description;
}

VkVertexInputAttributeDescription* get_attribute_descriptions(size_t* count) {
	*count = 2;

	VkVertexInputAttributeDescription* attribute_descriptions = malloc(sizeof(VkVertexInputAttributeDescription) * *count);
	
	attribute_descriptions[0] = (VkVertexInputAttributeDescription) {0};
	attribute_descriptions[0].binding = 0;
	attribute_descriptions[0].location = 0;
	attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	attribute_descriptions[0].offset = offsetof(Vertex, pos);

	attribute_descriptions[1] = (VkVertexInputAttributeDescription) {0};
	attribute_descriptions[1].binding = 0;
	attribute_descriptions[1].location = 1;
	attribute_descriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
	attribute_descriptions[1].offset = offsetof(Vertex, tex_coord);

	return attribute_descriptions;
}

VkVertexInputBindingDescription get_sprite_binding_description() {
	VkVertexInputBindingDescription binding_description = {0};
	binding_description.binding = 0;
	binding_description.stride = sizeof(VertexBufferSprite);
	binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	return binding_description;
}

VkVertexInputAttributeDescription* get_sprite_attribute_descriptions(size_t* count) {
	*count = 5;

	VkVertexInputAttributeDescription* attribute_descriptions = malloc(sizeof(VkVertexInputAttributeDescription) * *count);
	
	attribute_descriptions[0] = (VkVertexInputAttributeDescription) {0};
	attribute_descriptions[0].binding = 0;
	attribute_descriptions[0].location = 0;
	attribute_descriptions[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	attribute_descriptions[0].offset = offsetof(VertexBufferSprite, color);
	
	attribute_descriptions[1] = (VkVertexInputAttributeDescription) {0};
	attribute_descriptions[1].binding = 0;
	attribute_descriptions[1].location = 1;
	attribute_descriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
	attribute_descriptions[1].offset = offsetof(VertexBufferSprite, uv);

	attribute_descriptions[2] = (VkVertexInputAttributeDescription) {0};
	attribute_descriptions[2].binding = 0;
	attribute_descriptions[2].location = 2;
	attribute_descriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
	attribute_descriptions[2].offset = offsetof(VertexBufferSprite, uv2);

	attribute_descriptions[3] = (VkVertexInputAttributeDescription) {0};
	attribute_descriptions[3].binding = 0;
	attribute_descriptions[3].location = 3;
	attribute_descriptions[3].format = VK_FORMAT_R32_UINT;
	attribute_descriptions[3].offset = offsetof(VertexBufferSprite, texture_index);

	attribute_descriptions[4] = (VkVertexInputAttributeDescription) {0};
	attribute_descriptions[4].binding = 0;
	attribute_descriptions[4].location = 4;
	attribute_descriptions[4].format = VK_FORMAT_R32_UINT;
	attribute_descriptions[4].offset = offsetof(VertexBufferSprite, sprite_index);

	return attribute_descriptions;
}

void create_texture_sampler() {
	// Create the texture sampler
	
	// Query the physical device limits
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(vkx_instance.physical_device, &properties);

	VkSamplerCreateInfo sampler_info = {0};
	sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_info.magFilter = VK_FILTER_NEAREST;
	sampler_info.minFilter = VK_FILTER_LINEAR;
	sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.anisotropyEnable = VK_TRUE;
	sampler_info.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
	sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	sampler_info.unnormalizedCoordinates = VK_FALSE;
	sampler_info.compareEnable = VK_FALSE;
	sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler_info.mipLodBias = 0.0f;
	sampler_info.minLod = 0.0f;
	sampler_info.maxLod = 0.0f;
	
	if (vkCreateSampler(vkx_instance.device, &sampler_info, NULL, &texture_sampler) != VK_SUCCESS) {
		fprintf(stderr, "failed to create texture sampler!\n");
		exit(1);
	}
}

VkxBuffer vkx_create_and_populate_buffer(
		void* vertices,
		VkDeviceSize buffer_size,
		VkBufferUsageFlags usage_flags
) {
	/*
	 * Create a vertex buffer from the given vertices
	 *
	 * @param vertices The array of vertex data to create the buffer from
	 * @param buffer_size The size of the vertex data (i.e. sizeof(vertices[0]) * vertices_count)
	 * @param usage_flags The usage flags for the buffer (TRANSFER_DST_BIT is automatically added)
	 */
	// We should probably add a flag to decide if the buffer should be host coherent
	// (and not use a staging buffer)
	VkxBuffer staging_buffer = vkx_create_buffer(
		buffer_size,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
	);

	void *data;
	vkMapMemory(vkx_instance.device, staging_buffer.memory, 0, buffer_size, 0, &data);
	memcpy(data, vertices, (size_t) buffer_size);
	vkUnmapMemory(vkx_instance.device, staging_buffer.memory);

	VkxBuffer buffer = vkx_create_buffer(
		buffer_size,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage_flags,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
	);

	// Copy the staging buffer into the vertex buffer
	vkx_copy_buffer(staging_buffer.buffer, buffer.buffer, buffer_size);
	
	vkx_cleanup_buffer(&staging_buffer);

	return buffer;
}

void init_vulkan(void) {
	// ----- Initialise the vulkan instance and devices -----
	vkx_init(window);

	// ----- Create the swap chain -----
	vkx_create_swap_chain(false);
	
	// ----- Create the graphics pipeline -----
	// Vertex input bindng and attributes
	VkVertexInputBindingDescription binding_description = get_binding_description();
	size_t attribute_descriptions_count = 0;
	VkVertexInputAttributeDescription* attribute_descriptions = get_attribute_descriptions(&attribute_descriptions_count);

	// Push constants for pushing matrices etc.
	VkPushConstantRange push_constant_range = {0};
	push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	push_constant_range.offset = 0;
	push_constant_range.size = sizeof(PushConstants);

	tile_pipeline = vkx_create_vertex_buffer_pipeline(
		"shaders/tiles.vert.spv",
		"shaders/tiles.frag.spv",
		binding_description,
		attribute_descriptions,
		attribute_descriptions_count,
		push_constant_range,
		num_textures
	);
	
	// Create the sprite pipeline
	// Vertex input binding and attributes
	VkVertexInputBindingDescription sprite_binding_description = get_sprite_binding_description();
	size_t sprite_attribute_descriptions_count = 0;
	VkVertexInputAttributeDescription* sprite_attribute_descriptions = get_sprite_attribute_descriptions(&sprite_attribute_descriptions_count);

	// Push constants are the same (actually not used by the sprite shaders)
	sprite_pipeline = vkx_create_vertex_buffer_pipeline(
		"shaders/sprite.vert.spv",
		"shaders/sprite.frag.spv",
		sprite_binding_description,
		sprite_attribute_descriptions,
		sprite_attribute_descriptions_count,
		push_constant_range,
		num_textures
	);

	// Screen pipeline is simple and has no vertex input
	screen_pipeline = vkx_create_screen_pipeline(
		"shaders/screen.vert.spv",
		"shaders/screen.frag.spv"
	);

	free(sprite_attribute_descriptions);
	free(attribute_descriptions);

	
	// ----- Create the buffers -----
	// Vertex buffer
	vertex_buffer = vkx_create_and_populate_buffer(
			vertices, sizeof(vertices[0]) * vertices_count,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
	);
	// Index buffer
	index_buffer = vkx_create_and_populate_buffer(
			vertex_indices, sizeof(vertex_indices[0]) * vertex_indices_count,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT
	);
	// Sprite vertex buffer
	sprite_vertex_buffer = vkx_create_and_populate_buffer(
			vertex_sprites, sizeof(vertex_sprites[0]) * vertex_sprites_count,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
	);

	// ----- Load the texture images -----
	textures = malloc(sizeof(VkxImage) * num_textures);
	textures[0] = vkx_create_texture_image("textures/tiles.png");
	textures[1] = vkx_create_texture_image("textures/monsters1.png");
	textures[2] = vkx_create_texture_image("textures/monsters2.png");
	textures[3] = vkx_create_texture_image("textures/monsters3.png");
	textures[4] = vkx_create_texture_image("textures/monsters4.png");
	
	// Create the texture sampler
	create_texture_sampler();

	// ----- Create the uniform buffer -----
	VkDeviceSize uniform_buffer_size = sizeof(UniformBufferObject);

	if (uniform_buffer_size > 65536) {
		fprintf(stderr, "Tried to allocate a buffer with %zu bytes, which is greater than the maximum (65536)", uniform_buffer_size);
		exit(1);
	}

	for (size_t i = 0; i < VKX_FRAMES_IN_FLIGHT; i++) {
		uniform_buffers[i] = vkx_create_buffer(
			uniform_buffer_size,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);

		// Map the buffer memory and copy the vertex data into it
		vkMapMemory(vkx_instance.device, uniform_buffers[i].memory, 0, uniform_buffer_size, 0, &uniform_buffers_mapped[i]);
	}

	// ----- Create the offscreen images -----
	VkFormat depth_format = vkx_find_depth_format();

	for (size_t i=0; i<VKX_FRAMES_IN_FLIGHT; i++) {
		offscreen_images[i] = vkx_create_image(
			SCREEN_WIDTH,
			SCREEN_HEIGHT,
			vkx_swap_chain.image_format,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);

		offscreen_images[i].view = vkx_create_image_view(
			offscreen_images[i].image,
			vkx_swap_chain.image_format,
			VK_IMAGE_ASPECT_COLOR_BIT
		);

		// transition the image layout to color attachment optimal
		vkx_transition_image_layout_tmp_buffer(
			offscreen_images[i].image,
			vkx_swap_chain.image_format,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		);

		// And depth images too
		depth_images[i] = vkx_create_image(
			SCREEN_WIDTH,
			SCREEN_HEIGHT,
			depth_format,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);

		depth_images[i].view = vkx_create_image_view(depth_images[i].image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT);

		// Transition the image layout to depth stencil attachment
		vkx_transition_image_layout_tmp_buffer(
			depth_images[i].image, depth_format,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		);
	}

	// ----- Create the descriptor pool -----
	VkDescriptorPoolSize desc_pool_sizes[2] = {0};
	desc_pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	desc_pool_sizes[0].descriptorCount = VKX_FRAMES_IN_FLIGHT * 2;
	desc_pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	desc_pool_sizes[1].descriptorCount = VKX_FRAMES_IN_FLIGHT * num_textures + VKX_FRAMES_IN_FLIGHT;

	VkDescriptorPoolCreateInfo desc_pool_info = {0};
	desc_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	desc_pool_info.poolSizeCount = 2;
	desc_pool_info.pPoolSizes = desc_pool_sizes;
	desc_pool_info.maxSets = VKX_FRAMES_IN_FLIGHT * 2;

	if (vkCreateDescriptorPool(vkx_instance.device, &desc_pool_info, NULL, &descriptor_pool) != VK_SUCCESS) {
		fprintf(stderr, "failed to create descriptor pool!\n");
		exit(1);
	}
	
	{
		// ----- Create the descriptor sets -----
		VkDescriptorSetLayout ds_layouts[VKX_FRAMES_IN_FLIGHT] = {0};
		for (size_t i = 0; i < VKX_FRAMES_IN_FLIGHT; i++) {
			ds_layouts[i] = tile_pipeline.descriptor_set_layout;
		}

		VkDescriptorSetAllocateInfo ds_alloc_info = {0};
		ds_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		ds_alloc_info.descriptorPool = descriptor_pool;
		ds_alloc_info.descriptorSetCount = VKX_FRAMES_IN_FLIGHT;
		ds_alloc_info.pSetLayouts = ds_layouts;
		
		// Create the base descriptor sets
		if (vkAllocateDescriptorSets(vkx_instance.device, &ds_alloc_info, descriptor_sets) != VK_SUCCESS) {
			fprintf(stderr, "failed to allocate descriptor sets!\n");
			exit(1);
		}

		for (size_t i = 0; i < VKX_FRAMES_IN_FLIGHT; i++) {
			VkDescriptorBufferInfo buffer_info = {0};
			buffer_info.buffer = uniform_buffers[i].buffer;
			buffer_info.offset = 0;
			buffer_info.range = sizeof(UniformBufferObject);

			VkDescriptorImageInfo* image_infos = malloc(sizeof(VkDescriptorImageInfo) * num_textures);

			for (size_t j = 0; j < num_textures; j++) {
				image_infos[j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				image_infos[j].imageView = textures[j].view;
				image_infos[j].sampler = texture_sampler;
			}

			VkWriteDescriptorSet descriptor_writes[2] = {0};
			descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptor_writes[0].dstSet = descriptor_sets[i];
			descriptor_writes[0].dstBinding = 0;
			descriptor_writes[0].dstArrayElement = 0;
			descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptor_writes[0].descriptorCount = 1;
			descriptor_writes[0].pBufferInfo = &buffer_info;
			descriptor_writes[0].pImageInfo = NULL;
			descriptor_writes[0].pTexelBufferView = NULL;

			descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptor_writes[1].dstSet = descriptor_sets[i];
			descriptor_writes[1].dstBinding = 1;
			descriptor_writes[1].dstArrayElement = 0;
			descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptor_writes[1].descriptorCount = num_textures;
			descriptor_writes[1].pBufferInfo = NULL;
			descriptor_writes[1].pImageInfo = image_infos;
			descriptor_writes[1].pTexelBufferView = NULL;

			vkUpdateDescriptorSets(vkx_instance.device, 2, descriptor_writes, 0, NULL);

			free(image_infos);
		}
	}
	{
		// ----- Create the screen descriptor sets -----
		VkDescriptorSetLayout ds_layouts[VKX_FRAMES_IN_FLIGHT] = {0};
		for (size_t i = 0; i < VKX_FRAMES_IN_FLIGHT; i++) {
			ds_layouts[i] = screen_pipeline.descriptor_set_layout;
		}

		VkDescriptorSetAllocateInfo ds_alloc_info = {0};
		ds_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		ds_alloc_info.descriptorPool = descriptor_pool;
		ds_alloc_info.descriptorSetCount = VKX_FRAMES_IN_FLIGHT;
		ds_alloc_info.pSetLayouts = ds_layouts;
		
		// Create the base descriptor sets
		if (vkAllocateDescriptorSets(vkx_instance.device, &ds_alloc_info, screen_descriptor_sets) != VK_SUCCESS) {
			fprintf(stderr, "failed to allocate screen descriptor sets!\n");
			exit(1);
		}

		for (size_t i = 0; i < VKX_FRAMES_IN_FLIGHT; i++) {
			VkDescriptorBufferInfo buffer_info = {0};
			buffer_info.buffer = uniform_buffers[i].buffer;
			buffer_info.offset = 0;
			buffer_info.range = sizeof(UniformBufferObject);

			VkDescriptorImageInfo image_info = {0};

			image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			image_info.imageView = offscreen_images[i].view;
			image_info.sampler = texture_sampler;

			VkWriteDescriptorSet descriptor_writes[2] = {0};
			descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptor_writes[0].dstSet = screen_descriptor_sets[i];
			descriptor_writes[0].dstBinding = 0;
			descriptor_writes[0].dstArrayElement = 0;
			descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptor_writes[0].descriptorCount = 1;
			descriptor_writes[0].pBufferInfo = &buffer_info;
			descriptor_writes[0].pImageInfo = NULL;
			descriptor_writes[0].pTexelBufferView = NULL;

			descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptor_writes[1].dstSet = screen_descriptor_sets[i];
			descriptor_writes[1].dstBinding = 1;
			descriptor_writes[1].dstArrayElement = 0;
			descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptor_writes[1].descriptorCount = 1;
			descriptor_writes[1].pBufferInfo = NULL;
			descriptor_writes[1].pImageInfo = &image_info;
			descriptor_writes[1].pTexelBufferView = NULL;

			vkUpdateDescriptorSets(vkx_instance.device, 2, descriptor_writes, 0, NULL);
		}
	}

	// ----- Create the semaphores and fences -----
	vkx_init_sync_objects();

	printf("Initiialisation complete\n");
}

void record_command_buffer(VkCommandBuffer command_buffer, uint32_t image_index) {
	VkCommandBufferBeginInfo begin_info = {0};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
		fprintf(stderr, "failed to begin recording command buffer!\n");
		exit(1);
	}
	
	// Memory barrier to transition from present source to color attachment
	vkx_transition_image_layout(
		command_buffer,
		vkx_swap_chain.images[image_index],
		vkx_swap_chain.image_format,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	);

	// Memory barrier to transition the offscreen image from color attachment to shader read
	vkx_transition_image_layout(
		command_buffer,
		offscreen_images[current_frame].image,
		vkx_swap_chain.image_format,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	);

	VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};

	VkRenderingAttachmentInfo color_attachment_info = {0};
	color_attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	color_attachment_info.imageView = offscreen_images[current_frame].view;
	color_attachment_info.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
	color_attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment_info.clearValue = clear_color;
	
	VkClearValue depth_clear_value = {0};
	depth_clear_value.depthStencil.depth = 1.0f;
	depth_clear_value.depthStencil.stencil = 0;

	VkRenderingAttachmentInfo depth_info = {0};
	depth_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depth_info.imageView = depth_images[current_frame].view;
	depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depth_info.resolveMode = VK_RESOLVE_MODE_NONE;
	depth_info.resolveImageView = VK_NULL_HANDLE;
	depth_info.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depth_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_info.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth_info.clearValue = depth_clear_value;

	VkRenderingInfo rendering_info = {0};
	rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering_info.renderArea.offset.x = 0;
	rendering_info.renderArea.offset.y = 0;
	rendering_info.renderArea.extent.width = SCREEN_WIDTH;
	rendering_info.renderArea.extent.height = SCREEN_HEIGHT;
	rendering_info.layerCount = 1;
	rendering_info.colorAttachmentCount = 1;
	rendering_info.pColorAttachments = &color_attachment_info;
	rendering_info.pDepthAttachment = &depth_info;
	
	// --- Begin dynamic rendering --------------------------------------------
	vkCmdBeginRendering(command_buffer, &rendering_info);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, tile_pipeline.pipeline);

	VkBuffer vertex_buffers[] = {vertex_buffer.buffer};
	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);

	vkCmdBindIndexBuffer(command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);
	
	VkViewport viewport = {0};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float) SCREEN_WIDTH;
	viewport.height = (float) SCREEN_HEIGHT;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(command_buffer, 0, 1, &viewport);

	VkRect2D scissor = {0};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = SCREEN_WIDTH;
	scissor.extent.height = SCREEN_HEIGHT;
	vkCmdSetScissor(command_buffer, 0, 1, &scissor);
	
	// -- Render the tiles ----------------------------------------------------
	// Bind the descriptor set to update the uniform buffer
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, tile_pipeline.layout, 0, 1, &descriptor_sets[current_frame], 0, NULL);
	
	// Update push constants with the mvp matrix
	PushConstants push_constants = {0};
	// Copy projection and view matrix to the constants
	glm_mat4_mul(projection_matrix, view_matrix, push_constants.mvp);

	// Create a model matrix for the sprite
	mat4 tile_model_matrix = GLM_MAT4_IDENTITY_INIT;

	// Update the z-coordinate of the model matrix so that the tilemap is not
	// infront of everything else
	vec3 tile_translation = {
		0.0f,
		0.0f,
		10.0f,
	};
	glm_translate(tile_model_matrix, tile_translation);

	// Apply model matrix to the push constants
	glm_mat4_mul(push_constants.mvp, tile_model_matrix, push_constants.mvp);

	// Set the texture index to 0
	push_constants.texture_index = TEX_TILES;
	// Tiles always full white
	for (size_t i=0; i<4; i++) {
		push_constants.color[i] = 1.0f;
	}

	vkCmdPushConstants(command_buffer, tile_pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &push_constants);

	// Draw the triangles for the tiles
	vkCmdDrawIndexed(command_buffer, vertex_indices_count, 1, 0, 0, 0);
	
	// -- Render the sprites --------------------------------------------------
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, sprite_pipeline.pipeline);

	VkBuffer sprite_vertex_buffers[] = {sprite_vertex_buffer.buffer};
	VkDeviceSize sprite_offsets[] = {0};
	vkCmdBindVertexBuffers(command_buffer, 0, 1, sprite_vertex_buffers, sprite_offsets);

	vkCmdDraw(command_buffer, NUM_MONSTERS * 6, 1, 0, 0);

	vkCmdEndRendering(command_buffer);

	// Memory barrier to transition the offscreen image from shader read to color attachment
	vkx_transition_image_layout(
		command_buffer,
		offscreen_images[current_frame].image,
		vkx_swap_chain.image_format,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	// -- Render the screen ---------------------------------------------------
	// Update some parameters on the original structs
	viewport.width = (float) vkx_swap_chain.extent.width;
	viewport.height = (float) vkx_swap_chain.extent.height;
	vkCmdSetViewport(command_buffer, 0, 1, &viewport);

	scissor.extent = vkx_swap_chain.extent;
	vkCmdSetScissor(command_buffer, 0, 1, &scissor);

	rendering_info.pDepthAttachment = NULL;
	color_attachment_info.imageView = vkx_swap_chain.image_views[image_index];
	rendering_info.renderArea.extent = vkx_swap_chain.extent;
	
	// NOTE: an improvement we could make here would be to add a projection
	// matrix and feed it into the screen pipeline to ensure a consistent
	// aspect ratio (i.e. but black stripes down the sides of the screen).
	// This would probably involve adding the push constants to the screen
	// pipeline, or adding that projection matrix to the ubo.

	// Begin rendering again
	vkCmdBeginRendering(command_buffer, &rendering_info);
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, screen_pipeline.pipeline);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, screen_pipeline.layout, 0, 1, &screen_descriptor_sets[current_frame], 0, NULL);
	vkCmdDraw(command_buffer, 6, 1, 0, 0);

	// --- End dynamic rendering ----------------------------------------------
	vkCmdEndRendering(command_buffer);
	
	// Memory barrier to transition the swap chain image from color attachment to present
	vkx_transition_image_layout(
		command_buffer,
		vkx_swap_chain.images[image_index],
		vkx_swap_chain.image_format,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	);

	if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
		fprintf(stderr, "failed to record command buffer!\n");
		exit(1);
	}
}

void draw_frame() {
	vkWaitForFences(vkx_instance.device, 1, &vkx_sync_objects.in_flight_fences[current_frame], VK_TRUE, UINT64_MAX);

	uint32_t image_index;
	VkResult result = vkAcquireNextImageKHR(vkx_instance.device, vkx_swap_chain.swap_chain, UINT64_MAX, vkx_sync_objects.image_available_semaphores[current_frame], VK_NULL_HANDLE, &image_index);

	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		printf("Couldn't acquire swap chain image - recreating swap chain\n");
		vkx_recreate_swap_chain();
		return;
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		fprintf(stderr, "Failed to acquire swap chain image (result: %d)\n", result);
		exit(1);
	}
	
	// Update all uniform buffers with transform data
	UniformBufferObject ubo = {0};
	ubo.t = (float) t;
	// Update monster transforms
	for (size_t i=0; i<NUM_MONSTERS; i++) {
		// Copy projection and view matrix to the uniform buffer
		glm_mat4_mul(projection_matrix, view_matrix, ubo.mvps[i]);
		// Create a model matrix for the sprite
		mat4 model_matrix = GLM_MAT4_IDENTITY_INIT;
		const float monster_size = 2.0f;
		// Centre the sprite around it's position
		vec3 offset = {-monster_size / 2, -monster_size / 2, 0.0f};
		glm_translate(model_matrix, offset);

		// Move it up and down
		vec3 translation = {
			monsters[i].pos[0],
			monsters[i].pos[1] + (float) sin(t * 4.0f + i * 5) * 0.2f,
			monsters[i].pos[2]
		};
		glm_translate(model_matrix, translation);

		// Monsters are 8 tiles wide
		vec3 scale = {monster_size, monster_size, 1.0f};
		// Pulsating effect
		float sin_val = (float) sin(t * 2.0f + i * 5) * 0.15f;
		scale[0] *= (1 + sin_val);
		scale[1] *= (1 - sin_val);
		glm_scale(model_matrix, scale);
		
		// Apply model matrix to the push constants
		glm_mat4_mul(ubo.mvps[i], model_matrix, ubo.mvps[i]);
	}
	
	memcpy(uniform_buffers_mapped[current_frame], &ubo, sizeof(ubo));

	vkResetFences(vkx_instance.device, 1, &vkx_sync_objects.in_flight_fences[current_frame]);
	
	vkResetCommandBuffer(vkx_instance.command_buffers[current_frame], /*VkCommandBufferResetFlagBits*/ 0);
	
	// Write our draw commands into the command buffer
	record_command_buffer(vkx_instance.command_buffers[current_frame], image_index);

	VkSubmitInfo submit_info = {0};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	
	VkSemaphore wait_semaphores[] = {vkx_sync_objects.image_available_semaphores[current_frame]};
	VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	submit_info.waitSemaphoreCount = 1;
	submit_info.pWaitSemaphores = wait_semaphores;
	submit_info.pWaitDstStageMask = wait_stages;

	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &vkx_instance.command_buffers[current_frame];

	VkSemaphore signal_semaphores[] = {vkx_sync_objects.render_finished_semaphores[current_frame]};
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores = signal_semaphores;

	if (vkQueueSubmit(vkx_instance.graphics_queue, 1, &submit_info, vkx_sync_objects.in_flight_fences[current_frame]) != VK_SUCCESS) {
		fprintf(stderr, "failed to submit draw command buffer!");
		exit(1);
	}

	VkPresentInfoKHR present_info = {0};
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = signal_semaphores;

	VkSwapchainKHR swap_chains[] = {vkx_swap_chain.swap_chain};
	present_info.swapchainCount = 1;
	present_info.pSwapchains = swap_chains;

	present_info.pImageIndices = &image_index;

	result = vkQueuePresentKHR(vkx_instance.present_queue, &present_info);

	if (result == VK_SUBOPTIMAL_KHR) {
		if (suboptimal_swapchain_count == 0) {
			printf("Swapchain was suboptimal\n");
		}
		suboptimal_swapchain_count++;
	}
	else {
		// Reset to 0 if it doesn't come back like that every frame
		suboptimal_swapchain_count = 0;
	}
	
	if (framebuffer_resized) {
		printf("Framebuffer resized - recreating swap chain\n");
		framebuffer_resized = false;
		vkx_recreate_swap_chain();
	}
	else if (suboptimal_swapchain_count >= SUBOPTIMAL_SWAPCHAIN_THRESHOLD) {
		suboptimal_swapchain_count = 0;
		printf("Swapchain is still suboptimal - recreating\n");
		vkx_recreate_swap_chain();
	}
	else if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		printf("Couldn't present swap chain image - recreating swap chain (result: %d)\n", result);
		vkx_recreate_swap_chain();
	}
	else if (result != VK_SUBOPTIMAL_KHR && result != VK_SUCCESS) {
		fprintf(stderr, "failed to present swap chain image! (result: %d)\n", result);
		exit(1);
	}

	current_frame = (current_frame + 1) % VKX_FRAMES_IN_FLIGHT;
}

void cleanup_vulkan(void) {
	printf("Cleaning up Vulkan\n");

	vkx_cleanup_swap_chain();
	
	vkDestroySampler(vkx_instance.device, texture_sampler, NULL);
	for (size_t i = 0; i < num_textures; i++) {
		vkx_cleanup_image(&textures[i]);
	}

	for (size_t i = 0; i < VKX_FRAMES_IN_FLIGHT; i++) {
		vkx_cleanup_buffer(&uniform_buffers[i]);
	}
	
	vkDestroyDescriptorPool(vkx_instance.device, descriptor_pool, NULL);
	
	vkx_cleanup_pipeline(tile_pipeline);
	vkx_cleanup_pipeline(screen_pipeline);
	vkx_cleanup_pipeline(sprite_pipeline);

	vkx_cleanup_buffer(&vertex_buffer);
	vkx_cleanup_buffer(&index_buffer);
	vkx_cleanup_buffer(&sprite_vertex_buffer);
	
	vkx_cleanup_sync_objects();

	for (size_t i = 0; i < VKX_FRAMES_IN_FLIGHT; i++) {
		vkx_cleanup_image(&offscreen_images[i]);
		vkx_cleanup_image(&depth_images[i]);
	}

	vkx_cleanup_instance();
}

double rand_double(double max) {
	return ((double)rand() / (double)RAND_MAX) * max;
}

int rand_range(int min, int max) {
	/*
	 * Return a random number between min and max
	 */
	return (int)((double)rand() / ((double)RAND_MAX + 1) * (max - min) + min);
}

size_t get_tile_index(size_t x, size_t y) {
	/*
	 * Return the index of the tile at (x, y)
	 */
	return x + y * X_TILES;
}

void create_tiles(void) {
	// The number of occupied tiles
	size_t num_tiles = 0;

	// Generate a random set of tiles
	srand(time(NULL));
	for (int y = Y_TILES - 1; y >= 0; y--) {
		for (size_t x = 0; x < X_TILES; x++) {
			size_t idx = get_tile_index(x, y);

			// Edge tiles are always occupied
			if(x == 0 || x == X_TILES - 1 || y == 0 || y == Y_TILES - 1) {
				tiles[idx] = 0;
			}
			// 2/3 of the rest are empty
			else if(rand() % 3 >= 2) {
				tiles[idx] = (uint8_t) rand_range(0, TILESET_TOTAL_TILES);
			} else {
				tiles[idx] = EMPTY;
			}

			if (tiles[idx] != EMPTY) {
				num_tiles++;
			}

			// Debug test stuff
			if (tiles[idx] == EMPTY) {
				printf("-- ");
			}
			else {
				if (tiles[idx] < 10) {
					printf("0");
				}
				printf("%d ", tiles[idx]);
			}
		}
		printf("\n");
	}

	// Generate the mesh for the tilemap
	
	// First let's allocate some memory for the vertices and indices
	// We need 4 vertices per tile, and 6 indices per tile
	vertices_count = num_tiles * 4;
	vertices = malloc(sizeof(Vertex) * vertices_count);
	vertex_indices_count = num_tiles * 6;
	vertex_indices = malloc(sizeof(uint16_t) * vertex_indices_count);

	// Loop through and add the geometry for each tile
	size_t vertex_idx = 0;
	size_t index_idx = 0;

	for (size_t x = 0; x < X_TILES; x++) {
		for (size_t y = 0; y < Y_TILES; y++) {
			size_t idx = get_tile_index(x, y);
			if (tiles[idx] == EMPTY) {
				continue;
			}

			// We need to calculate the grid coords of the texture tile
			size_t tileset_idx = tiles[idx];
			size_t tileset_x = tileset_idx % TILESET_X_TILES;
			size_t tileset_y = tileset_idx / TILESET_X_TILES;

			// Tiles are 1x1, so we can just use the x and y coordinates as the vertex positions
			// We can leave all z coordinates as 0.0f. We'll update colours later
			
			// Bottom left
			vertices[vertex_idx].pos[0] = (float) x;
			vertices[vertex_idx].pos[1] = (float) y;
			vertices[vertex_idx].tex_coord[0] = (float) tileset_x / TILESET_X_TILES;
			vertices[vertex_idx].tex_coord[1] = (float) tileset_y / TILESET_Y_TILES + 1.0f / TILESET_Y_TILES;
			vertex_idx++;

			// Bottom right
			vertices[vertex_idx].pos[0] = (float) x + 1.0f;
			vertices[vertex_idx].pos[1] = (float) y;
			vertices[vertex_idx].tex_coord[0] = (float) tileset_x / TILESET_X_TILES + 1.0f / TILESET_X_TILES;
			vertices[vertex_idx].tex_coord[1] = (float) tileset_y / TILESET_Y_TILES + 1.0f / TILESET_Y_TILES;
			vertex_idx++;

			// Top right
			vertices[vertex_idx].pos[0] = (float) x + 1.0f;
			vertices[vertex_idx].pos[1] = (float) y + 1.0f;
			vertices[vertex_idx].tex_coord[0] = (float) tileset_x / TILESET_X_TILES + 1.0f / TILESET_X_TILES;
			vertices[vertex_idx].tex_coord[1] = (float) tileset_y / TILESET_Y_TILES;
			vertex_idx++;

			// Top left
			vertices[vertex_idx].pos[0] = (float) x;
			vertices[vertex_idx].pos[1] = (float) y + 1.0f;
			vertices[vertex_idx].tex_coord[0] = (float) tileset_x / TILESET_X_TILES;
			vertices[vertex_idx].tex_coord[1] = (float) tileset_y / TILESET_Y_TILES;
			vertex_idx++;

			// Now we need to add the indices for this tile
			vertex_indices[index_idx++] = vertex_idx - 4; // Bottom left
			vertex_indices[index_idx++] = vertex_idx - 3; // Bottom right
			vertex_indices[index_idx++] = vertex_idx - 2; // Top right
			vertex_indices[index_idx++] = vertex_idx - 2; // Top right
			vertex_indices[index_idx++] = vertex_idx - 1; // Top left
			vertex_indices[index_idx++] = vertex_idx - 4; // Bottom left
		}
	}

	// Loop through all vertices now and set z coord
	for (size_t i = 0; i < vertices_count; i++) {
		vertices[i].pos[2] = 0.0f;
	}
}

void create_monsters(void) {
	// Create the array to hold sprite data
	vertex_sprites_count = NUM_MONSTERS * 6;
	vertex_sprites = malloc(sizeof(VertexBufferSprite) * vertex_sprites_count);

	// Create the monsters and their and their "sprites"
	for (size_t i=0; i<NUM_MONSTERS; i++) {
		monsters[i].pos[0] = rand_double(X_TILES);
		monsters[i].pos[1] = rand_double(Y_TILES);
		// Half of the monsters will be in front of the tiles and half
		// will be behind
		monsters[i].pos[2] = rand_double(18) + 1;

		monsters[i].spd[0] = rand_double(10) - 5;
		monsters[i].spd[1] = rand_double(10) - 5;
		
		// Fade to blue as the monsters z coord puts them in the background
		float blue_fade = monsters[i].pos[2] / 20.0f;
		monsters[i].color[0] = 1.0f - blue_fade;
		monsters[i].color[1] = 1.0f - blue_fade;
		monsters[i].color[2] = 1.0f - blue_fade * 0.6f;
		monsters[i].color[3] = 1.0f;

		monsters[i].texture = TEX_MONSTERS + (i / 16) % 4;

		assert(monsters[i].texture < _TEX_COUNT);
		assert(monsters[i].texture >= TEX_MONSTERS);

		// Create the sprite vertices
		for (size_t j=0; j<6; j++ ) {
			size_t idx = i * 6 + j;
			assert(idx < vertex_sprites_count);

			for (size_t k=0; k<4; k++) {
				vertex_sprites[idx].color[k] = monsters[i].color[k];
			}
			// Calculate uv index base on 8x8 grid of sprites
			size_t sprite_x = i % 4;
			size_t sprite_y = (i % 16) / 4;
			float uv_scale = 1.0f / 4.0f;

			vertex_sprites[idx].uv[0] = uv_scale * sprite_x;
			vertex_sprites[idx].uv[1] = uv_scale * sprite_y;
			vertex_sprites[idx].uv2[0] = vertex_sprites[idx].uv[0] + uv_scale;
			vertex_sprites[idx].uv2[1] = vertex_sprites[idx].uv[1] + uv_scale;
			vertex_sprites[idx].texture_index = monsters[i].texture;
			vertex_sprites[idx].sprite_index = i;

			assert(vertex_sprites[idx].uv[0] >= 0.0f);
			assert(vertex_sprites[idx].uv[0] <= 1.0f);
			assert(vertex_sprites[idx].uv[1] >= 0.0f);
			assert(vertex_sprites[idx].uv[1] <= 1.0f);
			assert(vertex_sprites[idx].uv2[0] >= 0.0f);
			assert(vertex_sprites[idx].uv2[0] <= 1.0f);
			assert(vertex_sprites[idx].uv2[1] >= 0.0f);
			assert(vertex_sprites[idx].uv2[1] <= 1.0f);
		}
	}
}

void update(double dt) {
	for (size_t i=0; i<NUM_MONSTERS; i++) {
		if (monsters[i].spd[0] > 0 && monsters[i].pos[0] >= X_TILES) {
			monsters[i].spd[0] *= -1;
		}
		else if (monsters[i].spd[0] < 0 && monsters[i].pos[0] <= 0) {
			monsters[i].spd[0] *= -1;
		}
		else {
			monsters[i].pos[0] += dt * monsters[i].spd[0];
		}

		if (monsters[i].spd[1] > 0 && monsters[i].pos[1] >= Y_TILES) {
			monsters[i].spd[1] *= -1;
		}
		else if (monsters[i].spd[1] < 0 && monsters[i].pos[1] <= 0) {
			monsters[i].spd[1] *= -1;
		}
		else {
			monsters[i].pos[1] += dt * monsters[i].spd[1];
		}
	}

	// FPS count
	frame_count++;

	if (t - last_fps_time >= 1.0) {
		double total_time = t - last_fps_time;
		uint32_t fps = (uint32_t) (frame_count / total_time);
		printf("FPS: %d\n", fps);
		printf("Frame time: %f ms\n", (total_time / frame_count) * 1000.0);
		frame_count = 0;
		last_fps_time = t;
	}
}

int main(void) {
	printf("Hello, Vulkan!\n");

	// Initialise SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
		printf("SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

	// Create the window
	SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE;
	window = SDL_CreateWindow("Vulkan", DEFAULT_WIDTH, DEFAULT_HEIGHT, window_flags);
    if (!window) {
		printf("Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	printf("SDL window created\n");

	// Don't let the window shrink
	SDL_SetWindowMinimumSize(window, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);

	// Create the tiles
	create_tiles();

	// Create the monsters
	create_monsters();

	// Initialise Vulkan
	init_vulkan();
	
	// Make the window visible
	SDL_ShowWindow(window);

	// Initialise matrices
	// At the moment there is no camera - just use the identity matrix for view
	glm_mat4_identity(view_matrix);

	// Projection matrix
	// Orthographic projection with 0,0 in the bottom left hand corner, and each tile being 1x1
	// NOTE: z is inverted in OpenGL so we put -1.0f as the far plane
	// This seems to give values where 0 is closest and 20 is furthest away
	glm_ortho(0.0f, (float) X_TILES, (float) Y_TILES, 0.0f, 22.0f, -22.0f, projection_matrix);
	
	// ----- Main loop -----

    bool running = true;
    SDL_Event event;
    while (running) {
        // Poll for events
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
			else if (event.type == SDL_EVENT_KEY_DOWN) {
				if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_Q) {
					printf("Quitting...\n");
					running = false;
				}
				else if (event.key.key == SDLK_F11) {
					// Toggle fullscreen
					if (fullscreen) {
						SDL_SetWindowFullscreen(window, 0);
						fullscreen = false;
					}
					else {
						SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
						fullscreen = true;
					}
					framebuffer_resized = true;
				}
			}
        }

		uint64_t ticks = SDL_GetTicksNS();
		t = SDL_NS_TO_SECONDS((double) ticks);
		double dt = t - t_last;

		if (limit_fps && dt < min_frame_time) {
			int dt_ms = (int) (1000.0 * dt);
			int sleep_time = (int) (1000.0 * min_frame_time) - dt_ms;
			if (sleep_time <= 0) {
				sleep_time = 1;
			}
			SDL_Delay(sleep_time);
			ticks = SDL_GetTicksNS();
			t = SDL_NS_TO_SECONDS((double) ticks);
			dt = t - t_last;
		}
		else if (dt > 0.1) {
			// Clamp the delta time to 0.1 seconds
			dt = 0.1;
		}

		update(dt);

		draw_frame();

		t_last = t;
    }

	vkDeviceWaitIdle(vkx_instance.device);
	
	cleanup_vulkan();

	// Cleanup SDL
	printf("Cleaning up SDL\n");
    SDL_DestroyWindow(window);
    SDL_Quit();
	
	printf("Goodbye Vulkan!\n");
	printf("Built: %s %s\n", __DATE__, __TIME__);
}
