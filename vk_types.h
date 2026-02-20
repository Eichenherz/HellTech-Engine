#ifndef __VK_TYPES_H__
#define __VK_TYPES_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include "core_types.h"

// TODO:
struct vk_renderer_config
{
	static constexpr u64 MAX_FRAMES_IN_FLIGHT_ALLOWED = 2;
	static constexpr u64 MAX_SWAPCHAIN_IMG_ALLOWED = 3;
	static constexpr u64 MAX_DESCRIPTOR_COUNT_PER_TYPE = u16( -1 );

	VkFormat		desiredDepthFormat = VK_FORMAT_D32_SFLOAT;
	VkFormat		desiredColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	VkFormat		desiredHiZFormat = VK_FORMAT_R32_SFLOAT;
	VkFormat        desiredSwapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
	u16             renderWidth;
	u16             rednerHeight;
	u8              framesInFlightCount = 2;
	u8              swapchainImageCount = 3;
};

enum vk_desc_binding_t : u32
{
	SAMPLER = 0,
	STORAGE_BUFFER,
	STORAGE_IMAGE,
	SAMPLED_IMAGE,
	COUNT
};

#endif // !__VK_TYPES_H__

