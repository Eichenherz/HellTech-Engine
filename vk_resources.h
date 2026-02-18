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
	u32					width : 16;
	u32					height : 16;
	u32					layerCount : 8;
	u32					mipCount : 8;
	u32                 padding : 16;

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

struct vk_rsc_hndl64
{
	static_assert( sizeof( VkBuffer ) == sizeof( VkImage ) );
	static_assert( sizeof( VkBuffer ) == sizeof( u64 ) );
	static_assert( alignof( VkBuffer ) == alignof( VkImage ) );
	static_assert( alignof( VkBuffer ) == alignof( u64 ) );

	union
	{
		VkBuffer buff;
		VkImage img;
	};
	vk_resource_type type;

	inline vk_rsc_hndl64() = default;
	inline vk_rsc_hndl64( const vk_buffer& b ) : buff{ b.hndl }, type{ vk_resource_type::BUFFER } {}
	inline vk_rsc_hndl64( const vk_image& i ) : img{ i.hndl }, type{ vk_resource_type::IMAGE } {}
};

inline bool operator==( const vk_rsc_hndl64& a, const vk_rsc_hndl64& b ) noexcept
{
	if( a.type != b.type ) return false;
	if( vk_resource_type::BUFFER == a.type )
	{
		return (u64)a.buff == (u64)b.buff;
	}
	else if( vk_resource_type::IMAGE == a.type )
	{
		return (u64)a.img == (u64)b.img;
	}
	HT_ASSERT( 0 && "WRONG TYPE" );
	return false;
}

inline bool operator!=( const vk_rsc_hndl64& a, const vk_rsc_hndl64& b ) noexcept
{
	return !(a == b);
}

namespace std
{
	template<>
	struct hash<vk_rsc_hndl64>
	{
		u64 operator()( const vk_rsc_hndl64& h ) const noexcept
		{
			u64 hsh = 0;
			if( vk_resource_type::BUFFER == h.type )
			{
				hsh = std::bit_cast< u64 >( h.buff );
			}
			else if( vk_resource_type::IMAGE == h.type )
			{
				hsh = std::bit_cast< u64 >( h.img );
			}

			return std::hash<u64>{}( hsh );
		}
	};
}


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

#endif // !__VK_RESOURCES_H__
