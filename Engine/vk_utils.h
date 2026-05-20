#pragma once

#ifndef __VK_UTILS_H__
#define __VK_UTILS_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include "ht_core_types.h"

#include "vk_types.h"
#include "vk_resources.h"
#include "ht_error.h"

#include <dds.h>

// NOTE: we flip Y here to make the engine and shaders API agnostic
inline VkViewport VkCorrectedGetViewport( u32 width, u32 height )
{
	return { 0.0f, ( float ) height, ( float ) width, -( float ) height, 0.0f, 1.0f };
}

inline VkRect2D VkGetScissor( u32 width, u32 height )
{
	return { { 0, 0 }, { width, height } };
}

inline constexpr VkDescriptorType VkDescBindingToType( vk_desc_binding_t binding )
{
	using enum vk_desc_binding_t;
	switch( binding )
	{
	case SAMPLER:			return VK_DESCRIPTOR_TYPE_SAMPLER;
	case STORAGE_BUFFER:	return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	case STORAGE_IMAGE:		return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	case SAMPLED_IMAGE:		return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	default: HT_ASSERT( 0 && "Wrong descriptor type" ); 
	}
	return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

inline constexpr vk_desc_binding_t VkDescTypeToBinding( VkDescriptorType type )
{
	using enum vk_desc_binding_t;
	switch( type )
	{
	case VK_DESCRIPTOR_TYPE_SAMPLER:		return SAMPLER;
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: return STORAGE_BUFFER;
	case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:	return STORAGE_IMAGE;
	case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:	return SAMPLED_IMAGE;
	default: HT_ASSERT( 0 && "Wrong descriptor type" ); 
	}
	return COUNT;
}

inline VkFormat VkFromatFromDdsDxgi( dds::DXGI_FORMAT fmt )
{
    switch( fmt )
    {
    case dds::DXGI_FORMAT_R32G32B32A32_FLOAT:       return VK_FORMAT_R32G32B32A32_SFLOAT;
    case dds::DXGI_FORMAT_R32G32B32A32_UINT:        return VK_FORMAT_R32G32B32A32_UINT;
    case dds::DXGI_FORMAT_R32G32B32A32_SINT:        return VK_FORMAT_R32G32B32A32_SINT;
    case dds::DXGI_FORMAT_R32G32B32_FLOAT:          return VK_FORMAT_R32G32B32_SFLOAT;
    case dds::DXGI_FORMAT_R32G32B32_UINT:           return VK_FORMAT_R32G32B32_UINT;
    case dds::DXGI_FORMAT_R32G32B32_SINT:           return VK_FORMAT_R32G32B32_SINT;
    case dds::DXGI_FORMAT_R16G16B16A16_FLOAT:       return VK_FORMAT_R16G16B16A16_SFLOAT;
    case dds::DXGI_FORMAT_R16G16B16A16_UNORM:       return VK_FORMAT_R16G16B16A16_UNORM;
    case dds::DXGI_FORMAT_R16G16B16A16_UINT:        return VK_FORMAT_R16G16B16A16_UINT;
    case dds::DXGI_FORMAT_R16G16B16A16_SNORM:       return VK_FORMAT_R16G16B16A16_SNORM;
    case dds::DXGI_FORMAT_R16G16B16A16_SINT:        return VK_FORMAT_R16G16B16A16_SINT;
    case dds::DXGI_FORMAT_R32G32_FLOAT:             return VK_FORMAT_R32G32_SFLOAT;
    case dds::DXGI_FORMAT_R32G32_UINT:              return VK_FORMAT_R32G32_UINT;
    case dds::DXGI_FORMAT_R32G32_SINT:              return VK_FORMAT_R32G32_SINT;
    case dds::DXGI_FORMAT_R10G10B10A2_UNORM:        return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case dds::DXGI_FORMAT_R10G10B10A2_UINT:         return VK_FORMAT_A2B10G10R10_UINT_PACK32;
    case dds::DXGI_FORMAT_R11G11B10_FLOAT:          return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case dds::DXGI_FORMAT_R8G8B8A8_UNORM:           return VK_FORMAT_R8G8B8A8_UNORM;
    case dds::DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:      return VK_FORMAT_R8G8B8A8_SRGB;
    case dds::DXGI_FORMAT_R8G8B8A8_UINT:            return VK_FORMAT_R8G8B8A8_UINT;
    case dds::DXGI_FORMAT_R8G8B8A8_SNORM:           return VK_FORMAT_R8G8B8A8_SNORM;
    case dds::DXGI_FORMAT_R8G8B8A8_SINT:            return VK_FORMAT_R8G8B8A8_SINT;
    case dds::DXGI_FORMAT_R16G16_FLOAT:             return VK_FORMAT_R16G16_SFLOAT;
    case dds::DXGI_FORMAT_R16G16_UNORM:             return VK_FORMAT_R16G16_UNORM;
    case dds::DXGI_FORMAT_R16G16_UINT:              return VK_FORMAT_R16G16_UINT;
    case dds::DXGI_FORMAT_R16G16_SNORM:             return VK_FORMAT_R16G16_SNORM;
    case dds::DXGI_FORMAT_R16G16_SINT:              return VK_FORMAT_R16G16_SINT;
    case dds::DXGI_FORMAT_D32_FLOAT:                return VK_FORMAT_D32_SFLOAT;
    case dds::DXGI_FORMAT_R32_FLOAT:                return VK_FORMAT_R32_SFLOAT;
    case dds::DXGI_FORMAT_R32_UINT:                 return VK_FORMAT_R32_UINT;
    case dds::DXGI_FORMAT_R32_SINT:                 return VK_FORMAT_R32_SINT;
    case dds::DXGI_FORMAT_D24_UNORM_S8_UINT:        return VK_FORMAT_D24_UNORM_S8_UINT;
    case dds::DXGI_FORMAT_R8G8_UNORM:               return VK_FORMAT_R8G8_UNORM;
    case dds::DXGI_FORMAT_R8G8_UINT:                return VK_FORMAT_R8G8_UINT;
    case dds::DXGI_FORMAT_R8G8_SNORM:               return VK_FORMAT_R8G8_SNORM;
    case dds::DXGI_FORMAT_R8G8_SINT:                return VK_FORMAT_R8G8_SINT;
    case dds::DXGI_FORMAT_R16_FLOAT:                return VK_FORMAT_R16_SFLOAT;
    case dds::DXGI_FORMAT_D16_UNORM:                return VK_FORMAT_D16_UNORM;
    case dds::DXGI_FORMAT_R16_UNORM:                return VK_FORMAT_R16_UNORM;
    case dds::DXGI_FORMAT_R16_UINT:                 return VK_FORMAT_R16_UINT;
    case dds::DXGI_FORMAT_R16_SNORM:                return VK_FORMAT_R16_SNORM;
    case dds::DXGI_FORMAT_R16_SINT:                 return VK_FORMAT_R16_SINT;
    case dds::DXGI_FORMAT_R8_UNORM:                 return VK_FORMAT_R8_UNORM;
    case dds::DXGI_FORMAT_R8_UINT:                  return VK_FORMAT_R8_UINT;
    case dds::DXGI_FORMAT_R8_SNORM:                 return VK_FORMAT_R8_SNORM;
    case dds::DXGI_FORMAT_R8_SINT:                  return VK_FORMAT_R8_SINT;
    case dds::DXGI_FORMAT_A8_UNORM:                 return VK_FORMAT_R8_UNORM; // no VK_FORMAT_A8
    case dds::DXGI_FORMAT_R9G9B9E5_SHAREDEXP:       return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
    case dds::DXGI_FORMAT_B5G6R5_UNORM:             return VK_FORMAT_B5G6R5_UNORM_PACK16;
    case dds::DXGI_FORMAT_B5G5R5A1_UNORM:           return VK_FORMAT_B5G5R5A1_UNORM_PACK16;
    case dds::DXGI_FORMAT_B8G8R8A8_UNORM:           return VK_FORMAT_B8G8R8A8_UNORM;
    case dds::DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:      return VK_FORMAT_B8G8R8A8_SRGB;
    case dds::DXGI_FORMAT_B8G8R8X8_UNORM:           return VK_FORMAT_B8G8R8A8_UNORM; // no X8 in Vulkan
    case dds::DXGI_FORMAT_BC1_UNORM:                return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case dds::DXGI_FORMAT_BC1_UNORM_SRGB:           return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    case dds::DXGI_FORMAT_BC2_UNORM:                return VK_FORMAT_BC2_UNORM_BLOCK;
    case dds::DXGI_FORMAT_BC2_UNORM_SRGB:           return VK_FORMAT_BC2_SRGB_BLOCK;
    case dds::DXGI_FORMAT_BC3_UNORM:                return VK_FORMAT_BC3_UNORM_BLOCK;
    case dds::DXGI_FORMAT_BC3_UNORM_SRGB:           return VK_FORMAT_BC3_SRGB_BLOCK;
    case dds::DXGI_FORMAT_BC4_UNORM:                return VK_FORMAT_BC4_UNORM_BLOCK;
    case dds::DXGI_FORMAT_BC4_SNORM:                return VK_FORMAT_BC4_SNORM_BLOCK;
    case dds::DXGI_FORMAT_BC5_UNORM:                return VK_FORMAT_BC5_UNORM_BLOCK;
    case dds::DXGI_FORMAT_BC5_SNORM:                return VK_FORMAT_BC5_SNORM_BLOCK;
    case dds::DXGI_FORMAT_BC6H_UF16:                return VK_FORMAT_BC6H_UFLOAT_BLOCK;
    case dds::DXGI_FORMAT_BC6H_SF16:                return VK_FORMAT_BC6H_SFLOAT_BLOCK;
    case dds::DXGI_FORMAT_BC7_UNORM:                return VK_FORMAT_BC7_UNORM_BLOCK;
    case dds::DXGI_FORMAT_BC7_UNORM_SRGB:           return VK_FORMAT_BC7_SRGB_BLOCK;
    default:                                        return VK_FORMAT_UNDEFINED;
    }
}

inline image_info ImageInfoFromDds( const dds::Header& h, const char* nameStr )
{
	VkImageType imgType = h.is_1d() ? VK_IMAGE_TYPE_1D : h.is_3d() ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
	return {
		.name          = nameStr,
		.format        = VkFromatFromDdsDxgi( h.format() ),
		.createFlags   = h.is_cubemap() ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u,
		.type          = imgType,
		.usgFlags      = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.width         = ( u16 ) h.width(),
		.height        = ( u16 ) h.height(),
		.layerCount    = ( u8 ) h.array_size(),
		.mipCount      = ( u8 ) h.mip_levels(),
	};
}

inline VkBufferCopy2 MakeVkBufferCopy2( VkDeviceSize srcOffset, VkDeviceSize dstOffset, VkDeviceSize size )
{
	return {
		.sType     = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
		.srcOffset = srcOffset,
		.dstOffset = dstOffset,
		.size      = size
	};
}

// TODO: enforce some clearOp ---> clearVals params correctness ?
inline static VkRenderingAttachmentInfo VkMakeAttachmentInfo(
	VkImageView				view,
	VkAttachmentLoadOp      loadOp,
	VkAttachmentStoreOp     storeOp,
	VkClearValue            clearValue
) {
	return {
		.sType			= VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView		= view,
		.imageLayout	= VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
		.loadOp			= loadOp,
		.storeOp		= storeOp,
		.clearValue		= ( loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ) ? clearValue : VkClearValue{},
	};
}

// NOTE: we RevZ means 0 is the furthest away value
constexpr VkClearDepthStencilValue REV_Z_DEPTH_BUFFER_CLEAR_VAL = {};
// NOTE: we can't clear to 0 bc it's an index, instead we sacrifice the highest u32 val
constexpr VkClearColorValue GetVBufferClearValue()
{
	VkClearColorValue clearVal = {};
	clearVal.uint32[ 0 ] = ~u32( 0 );
	clearVal.uint32[ 1 ] = ~u32( 0 );
	clearVal.uint32[ 2 ] = ~u32( 0 );
	clearVal.uint32[ 3 ] = ~u32( 0 );

	return clearVal;
}

inline VkMemoryPropertyFlags VkChooseMemoryPropertiesFromBufferUsage( buffer_usage usage )
{
	using enum buffer_usage;
	switch( usage )
	{
		case GPU_ONLY: return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		case HOST_VISIBLE: return
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		case STAGING: return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		default: HT_ASSERT( 0 && "Unknown memory type" );
	}
	return 0;
}

// NOTE: from Sascha Willems
inline u32 VkGetQueueFamilyIndex(
	std::span<const VkQueueFamilyProperties>	queueFamProps,
	VkQueueFlags								queueFlags,
	VkBool32									mustPresent,
	VkPhysicalDevice							gpu,
	VkSurfaceKHR								vkSurf
) {
	if( ( queueFlags & VK_QUEUE_TRANSFER_BIT ) == queueFlags )
	{
		for( u32 qfi = 0; qfi < ( u32 ) std::size( queueFamProps ); qfi++ )
		{
			VkQueueFamilyProperties famProps = queueFamProps[ qfi ];
			bool hasTransfer = queueFamProps[ qfi ].queueFlags & VK_QUEUE_TRANSFER_BIT;
			bool hasCompute = famProps.queueFlags & VK_QUEUE_COMPUTE_BIT;
			bool hasGfx = queueFamProps[ qfi ].queueFlags & VK_QUEUE_GRAPHICS_BIT;
			if( hasTransfer && !hasCompute && !hasGfx )
			{
				return qfi;
			}
		}
	}

	if( ( queueFlags & VK_QUEUE_COMPUTE_BIT ) == queueFlags )
	{
		for( u32 qfi = 0; qfi < ( u32 ) std::size( queueFamProps ); qfi++)
		{
			VkQueueFamilyProperties famProps = queueFamProps[ qfi ];
			bool hasCompute = famProps.queueFlags & VK_QUEUE_COMPUTE_BIT;
			bool hasGfx = queueFamProps[ qfi ].queueFlags & VK_QUEUE_GRAPHICS_BIT;
			if( hasCompute && !hasGfx )
			{
				if( mustPresent )
				{
					VkBool32 hasPresent = 0;
					vkGetPhysicalDeviceSurfaceSupportKHR( gpu, qfi, vkSurf, &hasPresent );
					if( !hasPresent ) continue;
				}
				return qfi;
			}
		}
	}

	// NOTE: For other queue types or if no separate compute queue is present,
	// return the first one to support the requested flags
	for( u32 qfi = 0; qfi < ( u32 ) std::size( queueFamProps ); qfi++ )
	{
		if( ( queueFamProps[ qfi ].queueFlags & queueFlags ) == queueFlags )
		{
			if( mustPresent )
			{
				VkBool32 hasPresent = 0;
				vkGetPhysicalDeviceSurfaceSupportKHR( gpu, qfi, vkSurf, &hasPresent );
				if( !hasPresent ) continue;
			}
			return qfi;
		}
	}

	HT_ASSERT( 0 && "No queue found that matches the requirements !" );
	return ~0u;
}

inline void VkCheckFormatProperties( VkPhysicalDevice vkGpu, VkImageUsageFlags usg, VkFormat format )
{
	VkFormatFeatureFlags2 formatFeatures = 0;
	if( usg & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT )         formatFeatures |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT;
	if( usg & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;
	if( usg & VK_IMAGE_USAGE_TRANSFER_DST_BIT )             formatFeatures |= VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;
	if( usg & VK_IMAGE_USAGE_SAMPLED_BIT )                  formatFeatures |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;
	if( usg & VK_IMAGE_USAGE_HOST_TRANSFER_BIT )            formatFeatures |= VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT;


	VkFormatProperties3 formatProps3 = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3 };
	VkFormatProperties2 fomratProps2 = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, .pNext = &formatProps3 };

	vkGetPhysicalDeviceFormatProperties2( vkGpu, format, &fomratProps2 );

	HT_ASSERT( ( formatProps3.optimalTilingFeatures & formatFeatures ) == formatFeatures );
	// Fallback to a different format or use other means of uploading data
}

#endif // !__VK_UTILS_H__

