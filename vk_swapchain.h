#ifndef __VK_SWAPCHAIN_H__
#define __VK_SWAPCHAIN_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include "core_types.h"

inline VkViewport VkGetViewport( float width, float height )
{
	return { 0.0f, height, width, -height, 0.0f, 1.0f };
}

inline VkRect2D VkGetScissor( u32 width, u32 height )
{
	return { { 0, 0 }, { width, height } };
}

#endif // !__VK_SWAPCHAIN_H__
