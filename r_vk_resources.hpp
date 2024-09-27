#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define __VK
#include "DEFS_WIN32_NO_BS.h"
// TODO: autogen custom vulkan ?
#include <vulkan.h>

#include "vk_mem_alloc.h"

#include "core_types.h"

#include "vk_utils.hpp"

constexpr u64 MAX_MIP_LEVELS = 12;

// TODO: keep allocation in buffer ?
struct vk_buffer
{
	VkBuffer		hndl;
	VmaAllocation   allocation;
	u64				size;
	u8* hostVisible;
	u64				devicePointer;
	VkBufferUsageFlags usgFlags;
	u32             stride;

	inline VkDescriptorBufferInfo Descriptor() const
	{
		//{ hndl,offset,size }
		return { hndl, 0, size };
	}
};

struct vk_image
{
	VkImage			hndl;
	VkImageView		view;
	VkImageView     optionalViews[ MAX_MIP_LEVELS ];
	VmaAllocation   allocation;
	VkImageUsageFlags usageFlags;
	VkFormat		nativeFormat;
	u16				width;
	u16				height;
	u8				layerCount;
	u8				mipCount;

	inline VkExtent3D Extent3D() const
	{
		return { width,height,1 };
	}
};

struct buffer_info
{
	const char* name;
	VkBufferUsageFlags usage;
	u32 elemCount;
	u32 stride;
};

struct image_info
{
	const char*         name;
	VkFormat		    format;
	VkImageUsageFlags	usg;
	u16				    width;
	u16				    height;
	u8				    layerCount;
	u8				    mipCount;
};