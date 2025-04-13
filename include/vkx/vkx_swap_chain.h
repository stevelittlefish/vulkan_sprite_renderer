#ifndef VXK_SWAP_CHAIN_H
#define VXK_SWAP_CHAIN_H

#include <vulkan/vulkan.h>
#include "vkx/vkx_core.h"

void vkx_create_swap_chain(bool create_depth_image);
void vkx_cleanup_swap_chain();
void vkx_recreate_swap_chain();

#endif // VXK_SWAP_CHAIN_H
