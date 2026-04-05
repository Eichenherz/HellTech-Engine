#ifndef __VK_TYPES_H__
#define __VK_TYPES_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include "ht_core_types.h"

struct vk_swapchain_config
{
	VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
	VkImageUsageFlags imgUsage =
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
	u32 minNumImgs = 3;
};

// TODO:
struct vk_renderer_config
{
	static constexpr u64 MAX_FRAMES_IN_FLIGHT_ALLOWED = 2;
	static constexpr u64 MAX_DESCRIPTOR_COUNT_PER_TYPE = u16( -1 );

	vk_swapchain_config scConfig = {};

	VkFormat		desiredDepthFormat = VK_FORMAT_D32_SFLOAT;
	VkFormat		desiredColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	VkFormat		desiredHiZFormat = VK_FORMAT_R32_SFLOAT;
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

