#ifndef __VK_SWAPCHAIN_H__
#define __VK_SWAPCHAIN_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include <EASTL/fixed_vector.h>

#include "core_types.h"

struct vk_swapchain_image
{
	VkImage			hndl;
	VkImageView		view;
	VkSemaphore		canPresentSema;
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

inline VkViewport VkGetSwapchainViewport( const vk_swapchain& sc )
{
	return { 0.0f, ( float ) sc.height, ( float ) sc.width, -( float ) sc.height, 0.0f, 1.0f };
}

inline VkRect2D VkGetSwapchianScissor( const vk_swapchain& sc )
{
	return { { 0, 0 }, { sc.width, sc.height } };
}

#endif // !__VK_SWAPCHAIN_H__
