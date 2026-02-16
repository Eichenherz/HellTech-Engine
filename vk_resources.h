#ifndef __VK_RESOURCES_H__
#define __VK_RESOURCES_H__

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include "DEFS_WIN32_NO_BS.h"
#include <vulkan.h>

#include "vk_error.h"
#include "core_types.h"
#include "sys_os_api.h"
#include <type_traits>

#include <3rdParty/vk_mem_alloc.h>

constexpr u64 MAX_MIP_LEVELS = 12;

enum class buffer_usage : u8
{
	GPU_ONLY,
	STAGING,
	HOST_VISIBLE
};

struct buffer_info
{
	const char*        name;
	VkBufferUsageFlags usageFlags;
	u64                sizeInBytes;
	buffer_usage       usage;
};

struct image_info
{
	const char*         name;
	VkFormat		    format;
	VkImageUsageFlags	usg;
	u16					width;
	u16					height;
	u8					layerCount;
	u8					mipCount;
};

enum class vk_resource_type : u8
{
	BUFFER,
	IMAGE
};

struct vk_buffer
{
	VmaAllocation		mem;
	VkBuffer			hndl;
	u64					sizeInBytes; 
	u8*					hostVisible;
	VkDeviceAddress		devicePointer;
	VkBufferUsageFlags  usgFlags;
};

inline VkDescriptorBufferInfo Descriptor( const vk_buffer& b )
{
	return VkDescriptorBufferInfo{ b.hndl, 0, b.sizeInBytes };
}

struct vk_image
{
	VmaAllocation		mem;
	VkImage				hndl;
	VkImageView			view;
	VkImageUsageFlags   usageFlags;
	VkFormat			format;
	u16					width;
	u16					height;
	u8					layerCount;
	u8					mipCount;

	inline VkExtent3D Extent3D() const
	{
		return { width, height, 1 };
	}
};

inline VkImageAspectFlags VkSelectAspectMaskFromFormat( VkFormat imgFormat )
{
	return ( imgFormat == VK_FORMAT_D32_SFLOAT ) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

inline VkImageView
VkMakeImgView(
	VkDevice		vkDevice,
	VkImage			vkImg,
	VkFormat		imgFormat,
	u32				mipLevel,
	u32				levelCount,
	VkImageViewType imgViewType = VK_IMAGE_VIEW_TYPE_2D,
	u32				arrayLayer = 0,
	u32				layerCount = 1
){
	VkImageAspectFlags aspectFlags = VkSelectAspectMaskFromFormat( imgFormat );
	VkImageViewCreateInfo viewInfo = { 
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = vkImg,
		.viewType = imgViewType,
		.format = imgFormat,
		.subresourceRange = {
			.aspectMask = aspectFlags,
			.baseMipLevel = mipLevel,
			.levelCount = levelCount,
			.baseArrayLayer = arrayLayer,
			.layerCount = layerCount,
	},
	};

	VkImageView view;
	VK_CHECK( vkCreateImageView( vkDevice, &viewInfo, 0, &view ) );

	return view;
}

inline static VkBufferMemoryBarrier2
VkMakeBufferBarrier2(
	VkBuffer					hBuff,
	VkAccessFlags2			srcAccess,
	VkPipelineStageFlags2	srcStage,
	VkAccessFlags2		dstAccess,
	VkPipelineStageFlags2	dstStage,
	VkDeviceSize				buffOffset = 0,
	VkDeviceSize				buffSize = VK_WHOLE_SIZE,
	u32							srcQueueFamIdx = VK_QUEUE_FAMILY_IGNORED,
	u32							dstQueueFamIdx = VK_QUEUE_FAMILY_IGNORED
){
	VkBufferMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	barrier.srcStageMask = srcStage;
	barrier.srcAccessMask = srcAccess;
	barrier.dstStageMask = dstStage;
	barrier.dstAccessMask = dstAccess;
	barrier.srcQueueFamilyIndex = srcQueueFamIdx;
	barrier.dstQueueFamilyIndex = dstQueueFamIdx;
	barrier.buffer = hBuff;
	barrier.offset = buffOffset;
	barrier.size = buffSize;

	return barrier;
}

inline static VkBufferMemoryBarrier2 VkReverseBufferBarrier2( const VkBufferMemoryBarrier2& b )
{
	VkBufferMemoryBarrier2 barrier = b;
	std::swap( barrier.srcAccessMask, barrier.dstAccessMask );
	std::swap( barrier.srcStageMask, barrier.dstStageMask );

	return barrier;
}

inline static VkImageMemoryBarrier2
VkMakeImageBarrier2(
	VkImage						hImg,
	VkAccessFlags2           srcAccessMask,
	VkPipelineStageFlags2    srcStageMask,
	VkAccessFlags2          dstAccessMask,
	VkPipelineStageFlags2	dstStageMask,
	VkImageLayout               oldLayout,
	VkImageLayout               newLayout,
	VkImageAspectFlags			aspectMask,
	u32							baseMipLevel = 0,
	u32							mipCount = 0,
	u32							baseArrayLayer = 0,
	u32							layerCount = 0,
	u32							srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	u32							dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED
){
	VkImageMemoryBarrier2 barrier = { 
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
	barrier.image = hImg;
	barrier.srcAccessMask = srcAccessMask;
	barrier.srcStageMask = srcStageMask;
	barrier.dstAccessMask = dstAccessMask;
	barrier.dstStageMask = dstStageMask;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = srcQueueFamilyIndex;
	barrier.dstQueueFamilyIndex = dstQueueFamilyIndex;
	barrier.subresourceRange.aspectMask = aspectMask;
	barrier.subresourceRange.baseArrayLayer = baseArrayLayer;
	barrier.subresourceRange.baseMipLevel = baseMipLevel;
	barrier.subresourceRange.layerCount = ( layerCount ) ? layerCount : VK_REMAINING_ARRAY_LAYERS;
	barrier.subresourceRange.levelCount = ( mipCount ) ? mipCount : VK_REMAINING_MIP_LEVELS;

	return barrier;
}



#endif // !__VK_RESOURCES_H__
