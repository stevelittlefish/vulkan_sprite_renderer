#ifndef VKX_INIT_H
#define VKX_INIT_H

#include <stdbool.h>
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include "vkx/vkx_core.h"

extern const bool enable_validation_layers;

void vkx_init(SDL_Window* window);
void vkx_cleanup_instance();

#endif // VKX_INIT_H
