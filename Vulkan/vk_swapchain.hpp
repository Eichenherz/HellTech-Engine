#pragma once

#include "vk_common.hpp"
#include "vk_resources.hpp"

//====================CONSTS====================//
constexpr u32 VK_SWAPCHAIN_MAX_IMG_ALLOWED = 3;

struct vk_swapchain
{
	VkSwapchainKHR	hndl;
	VkImageView		imgViews[ VK_SWAPCHAIN_MAX_IMG_ALLOWED ];
	VkImage			imgs[ VK_SWAPCHAIN_MAX_IMG_ALLOWED ];
	VkFormat		imgFormat;
	u16				width;
	u16				height;
	u8				imgCount;

	VkViewport GetRenderViewport() const
	{
		return { 0, ( float ) height, ( float ) width, -( float ) height, 0, 1.0f };
	}
};
