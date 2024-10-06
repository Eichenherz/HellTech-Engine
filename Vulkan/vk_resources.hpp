#pragma once

#include "vk_common.hpp"

#include "vk_mem_alloc.h"

constexpr u64 MAX_MIP_LEVELS = 12;

constexpr VkBufferUsageFlags STORAGE_INDIRECT = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
constexpr VkBufferUsageFlags STORAGE_INDIRECT_DST = STORAGE_INDIRECT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
constexpr VkBufferUsageFlags STORAGE_INDIRECT_DST_BDA = STORAGE_INDIRECT_DST | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
constexpr VkBufferUsageFlags STORAGE_DST_BDA =
VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

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