#pragma once

#ifndef __VK_RESOURCES_H__
#define __VK_RESOURCES_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include <ht_core_types.h>
#include <ht_renderer_types.h>
#include "vk_error.h"
#include "vk_types.h"

#include <vk_mem_alloc.h>

enum class vk_resource_type : u8
{
    INVALID,
    BUFFER,
    IMAGE
};

constexpr u32 MAX_MIP_LEVELS = 12;

enum class buffer_usage : u64
{
    INVALID         = 0,
	XFER_SRC	    = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	XFER_DST	    = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	STORAGE			= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
	INDEX			= VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
	INDIRECT		= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
	BDA	            = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
};

constexpr buffer_usage operator|( buffer_usage a, buffer_usage b ) { return ( buffer_usage ) ( ( u64 ) a | ( u64 ) b ); }

enum class buffer_mem_props : u32
{
    INVALID         = 0,
	GPU_ONLY		= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
	STAGING			= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	HOST_VISIBLE	= GPU_ONLY | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
};

struct buffer_info
{
	const char*         name        = nullptr;
	buffer_usage        usageFlags  = {};
    VkSharingMode       sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	u64                 sizeInBytes = 0;
	buffer_mem_props    memProps    = {};
    u64                 alignment   = 0; // NOTE: 0 means VMA will default to the required API minAlignment
};

enum class image_usage : u32
{
	XFER_SRC		= VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
	XFER_DST		= VK_IMAGE_USAGE_TRANSFER_DST_BIT,
	SAMPLED			= VK_IMAGE_USAGE_SAMPLED_BIT,
	STORAGE			= VK_IMAGE_USAGE_STORAGE_BIT,
	COLOR_TARGET	= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
	DEPTH_TARGET	= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
};

constexpr image_usage operator|( image_usage a, image_usage b ) { return ( image_usage ) ( ( u32 ) a | ( u32 ) b ); }

struct image_info
{
	const char*			name;
	VkFormat			format;
	VkImageCreateFlags  createFlags;
	VkImageType         type;
	image_usage			usgFlags;
	u16					width;
	u16					height;
	u8					layerCount;
	u8					mipCount;
};

struct vk_buffer
{
    static constexpr vk_resource_type TYPE = vk_resource_type::BUFFER;

	VmaAllocation		alloc;
	VkBuffer			hndl;
	u64					sizeInBytes; 
	u8*					hostVisible;
	VkDeviceAddress		devicePointer;
	VkBufferUsageFlags  usgFlags;
};

template<typename T>
std::span<T> VkBufferHostView( vk_buffer& vkBuff )
{
	return { ( T* ) vkBuff.hostVisible, vkBuff.sizeInBytes / sizeof( T ) };
}

template<typename T>
std::span<const T> VkBufferHostView( const vk_buffer& vkBuff )
{
	return { ( const T* ) vkBuff.hostVisible, vkBuff.sizeInBytes / sizeof( T ) };
}

inline VkDescriptorBufferInfo Descriptor( const vk_buffer& b ) { return { b.hndl, 0, b.sizeInBytes }; }

struct vk_image
{
    static constexpr vk_resource_type TYPE = vk_resource_type::IMAGE;

	VmaAllocation		alloc;
	VkImage				hndl;
	VkImageView			view;
	VkImageCreateFlags  createFlags;
	VkImageType         type;
	VkImageUsageFlags	usageFlags;
	VkFormat			format;
	u32					width		: 16;
	u32					height		: 16;
	u32					layerCount	: 8;
	u32					mipCount	: 8;
	u32					padding		: 16;

	VkExtent3D Extent3D() const { return { width, height, 1 }; }
};

constexpr VkImageAspectFlags VkSelectAspectMaskFromFormat( VkFormat imgFormat )
{
	return ( imgFormat == VK_FORMAT_D32_SFLOAT ) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

HT_FORCEINLINE VkImageSubresourceRange VkFullResource( const vk_image& img )
{
	return {
		.aspectMask		= VkSelectAspectMaskFromFormat( img.format ),
		.baseMipLevel	= 0,
		.levelCount		= VK_REMAINING_MIP_LEVELS,
		.baseArrayLayer = 0,
		.layerCount		= VK_REMAINING_ARRAY_LAYERS
	};
}

__forceinline VkImageSubresourceLayers VkFullResourceLayers( const vk_image& img ) 
{
	return {
		.aspectMask		= VkSelectAspectMaskFromFormat( img.format ),
		.mipLevel		= 0,
		.baseArrayLayer = 0,
		.layerCount		= VK_REMAINING_ARRAY_LAYERS
	};
}

inline VkImageView VkMakeImgView(
	VkDevice		vkDevice,
	VkImage			vkImg,
	VkFormat		imgFormat,
	u32				mipLevel,
	u32				levelCount,
	VkImageViewType imgViewType = VK_IMAGE_VIEW_TYPE_2D,
	u32				arrayLayer	= 0,
	u32				layerCount	= 1
){
	VkImageAspectFlags      aspectFlags = VkSelectAspectMaskFromFormat( imgFormat );
	VkImageViewCreateInfo   viewInfo    = {
		.sType				= VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image				= vkImg,
		.viewType			= imgViewType,
		.format				= imgFormat,
		.subresourceRange	= {
			.aspectMask		= aspectFlags,
			.baseMipLevel	= mipLevel,
			.levelCount		= levelCount,
			.baseArrayLayer = arrayLayer,
			.layerCount		= layerCount,
		},
	};

	VkImageView view;
	VK_CHECK( vkCreateImageView( vkDevice, &viewInfo, nullptr, &view ) );

	return view;
}


struct vk_descriptor_info
{
	union
	{
		VkDescriptorBufferInfo	buff;
		VkDescriptorImageInfo	img;
	};
	VkDescriptorType			descriptorType;
	vk_resource_type			rscType;

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

struct desc_hndl32
{
	u32 slot	: 16;
	u32 type	: 2;
	u32 inUse	: 1;
	u32 unused	: 13 = 0;
};

static_assert( IsPowOf2( vk_renderer_config::MAX_DESCRIPTOR_COUNT_PER_TYPE )
    && ( MAX_DESCRIPTOR_COUNT == vk_renderer_config::MAX_DESCRIPTOR_COUNT_PER_TYPE ) );
static_assert( ( MAX_DESCRIPTOR_COUNT - 1 ) == desc_hndl32{ .slot = MAX_DESCRIPTOR_COUNT - 1 }.slot );

struct vk_descriptor_write
{
	vk_descriptor_info	descInfo;
	desc_hndl32			hndl;
};


// NOTE: clear depth to 0 bc we	use RevZ
constexpr VkClearValue DEPTH_CLEAR_VAL	= {};
constexpr VkClearValue RT_CLEAR_VAL		= {};

enum render_target_op : u32
{
    LOAD		= VK_ATTACHMENT_LOAD_OP_LOAD,
    LOAD_CLEAR	= VK_ATTACHMENT_LOAD_OP_CLEAR,
    STORE		= VK_ATTACHMENT_STORE_OP_STORE
};

#endif // !__VK_RESOURCES_H__
