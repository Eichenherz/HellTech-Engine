#ifndef __VK_SWAPCHAIN_H__
#define __VK_SWAPCHAIN_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include <EASTL/fixed_vector.h>

#include "core_types.h"
#include "vk_resources.h"

struct vk_swapchain_image
{
	VkSemaphore		canPresentSema;
	vk_image        img;
};

// TODO: don't hardcode 
using vk_image_vector = eastl::fixed_vector<vk_swapchain_image, 3, false>;

struct vk_swapchain
{
	vk_image_vector     imgs;
	VkSwapchainKHR		swapchain;
	VkFormat			imgFormat;
	u16					width;
	u16					height;
	u8					imgCount;
};

inline VkViewport VkGetViewport( float width, float height )
{
	return { 0.0f, height, width, -height, 0.0f, 1.0f };
}

inline VkRect2D VkGetScissor( u32 width, u32 height )
{
	return { { 0, 0 }, { width, height } };
}

#endif // !__VK_SWAPCHAIN_H__
