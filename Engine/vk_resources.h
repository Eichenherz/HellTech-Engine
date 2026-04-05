#ifndef __VK_RESOURCES_H__
#define __VK_RESOURCES_H__

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include "Win32/DEFS_WIN32_NO_BS.h"
#include <vulkan.h>

#include "vk_utils.h"
#include "vk_error.h"
#include "vk_types.h"
#include "ht_core_types.h"

#include <dds.h>

#include <vk_mem_alloc.h>

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
	const char*			name;
	VkFormat			format;
	VkImageCreateFlags  createFlags;
	VkImageType         type;
	VkImageUsageFlags	usgFlags;
	u16					width;
	u16					height;
	u8					layerCount;
	u8					mipCount;
};

inline image_info ImageInfoFromDds( const dds::Header& h, const char* nameStr )
{
	VkImageType imgType = h.is_1d() ? VK_IMAGE_TYPE_1D : 
		h.is_3d() ? VK_IMAGE_TYPE_3D :
		VK_IMAGE_TYPE_2D;

	return {
		.name          = nameStr,
		.format        = VkFromatFromDdsDxgi(h.format()),
		.createFlags   = h.is_cubemap() ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u,
		.type          = imgType,
		.usgFlags      = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.width         = ( u16 ) h.width(),
		.height        = ( u16 ) h.height(),
		.layerCount    = ( u8 ) h.array_size(),
		.mipCount      = ( u8 ) h.mip_levels(),
	};
}

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
	VkImageCreateFlags  createFlags;
	VkImageType         type;
	VkImageUsageFlags	usageFlags;
	VkFormat			format;
	u32					width : 16;
	u32					height : 16;
	u32					layerCount : 8;
	u32					mipCount : 8;
	u32					padding : 16;

	inline VkExtent3D Extent3D() const
	{
		return { width, height, 1 };
	}
};

enum class vk_resource_type : u8
{
	INVLAID,
	BUFFER,
	IMAGE
};

inline VkImageAspectFlags VkSelectAspectMaskFromFormat( VkFormat imgFormat )
{
	return ( imgFormat == VK_FORMAT_D32_SFLOAT ) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

__forceinline VkImageSubresourceRange VkFullResource( const vk_image& img ) 
{
	return {
		.aspectMask = VkSelectAspectMaskFromFormat( img.format ),
		.baseMipLevel = 0,
		.levelCount = VK_REMAINING_MIP_LEVELS,
		.baseArrayLayer = 0,
		.layerCount = VK_REMAINING_ARRAY_LAYERS
	};
}

__forceinline VkImageSubresourceLayers VkFullResourceLayers( const vk_image& img ) 
{
	return {
		.aspectMask = VkSelectAspectMaskFromFormat( img.format ),
		.mipLevel = 0,
		.baseArrayLayer = 0,
		.layerCount = VK_REMAINING_ARRAY_LAYERS
	};
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


struct vk_descriptor_info
{
	union
	{
		VkDescriptorBufferInfo buff;
		VkDescriptorImageInfo img;
	};
	VkDescriptorType descriptorType;
	vk_resource_type rscType;

	vk_descriptor_info() = default;
	vk_descriptor_info( const vk_buffer& vkBuff ) : buff{ vkBuff.hndl, 0, vkBuff.sizeInBytes }
	{
		descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		rscType = vk_resource_type::BUFFER;
	}
	vk_descriptor_info( VkBuffer buff, u64 offset, u64 range ) : buff{ buff, offset, range }
	{
		descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		rscType = vk_resource_type::BUFFER;
	}
	vk_descriptor_info( VkImageView view, VkImageLayout imgLayout ) : img{ .imageView = view, .imageLayout = imgLayout }
	{
		descriptorType = ( imgLayout == VK_IMAGE_LAYOUT_GENERAL ) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE :
			VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		rscType = vk_resource_type::IMAGE;
	}
	vk_descriptor_info( VkSampler sampler ) : img{ .sampler = sampler, .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED }
	{
		descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		rscType = vk_resource_type::IMAGE;
	}
	vk_descriptor_info( VkSampler sampler, VkImageView view, VkImageLayout imgLayout ) 
		: img{ .sampler = sampler, .imageView = view, .imageLayout = imgLayout }
	{
		descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		rscType = vk_resource_type::IMAGE;
	}
};

// TODO: not here
static_assert( vk_renderer_config::MAX_DESCRIPTOR_COUNT_PER_TYPE == u64( u16( -1 ) ) );
struct desc_hndl32
{
	u32 slot : 16;
	u32 type : 2;
	u32 inUse : 1;
	u32 unused : 13;
};

struct vk_descriptor_write
{
	vk_descriptor_info descInfo;
	desc_hndl32 hndl;
};

#endif // !__VK_RESOURCES_H__
