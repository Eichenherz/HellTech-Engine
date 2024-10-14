#pragma once

#include "vk_common.hpp"

#include "vk_mem_alloc.h"

constexpr u64 MAX_MIP_LEVELS = 12;

constexpr VkBufferUsageFlags STORAGE_INDIRECT = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
constexpr VkBufferUsageFlags STORAGE_INDIRECT_DST = STORAGE_INDIRECT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
constexpr VkBufferUsageFlags STORAGE_INDIRECT_DST_BDA = STORAGE_INDIRECT_DST | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
constexpr VkBufferUsageFlags STORAGE_DST_BDA =
VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

constexpr VkMemoryPropertyFlags BAR_MEMORY_FLAG = 
VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

constexpr VkImageUsageFlags HiZ_IMAGE_USG = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;



enum class vk_memory_type : u32
{
	AUTO = 0,
	BAR = BAR_MEMORY_FLAG
};

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
		return { width, height, 1u };
	}

	// TODO: enforce some clearOp ---> clearVals params correctness ?
	inline VkRenderingAttachmentInfo MakeAttachemntInfo( 
		VkAttachmentLoadOp loadOp, 
		VkAttachmentStoreOp storeOp, 
		VkClearValue clearValue 
	) {
		return {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = this->view,
			.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			.loadOp = loadOp,
			.storeOp = storeOp,
			.clearValue = ( loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ) ? clearValue : VkClearValue{},
		};
	}
};

struct buffer_info
{
	const char* name;
	VkBufferUsageFlags usage;
	vk_memory_type memType;
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