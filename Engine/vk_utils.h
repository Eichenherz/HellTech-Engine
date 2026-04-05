#pragma once

#ifndef __VK_UTILS_H__
#define __VK_UTILS_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include "ht_core_types.h"

#include "vk_types.h"
#include "ht_error.h"

#include <dds.h>

inline VkViewport VkGetViewport( float width, float height )
{
	return { 0.0f, height, width, -height, 0.0f, 1.0f };
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

struct vk_buffer_copy
{
    VkCopyBufferInfo2 cpyInfo2;
    VkBufferCopy2     region;

    inline vk_buffer_copy( 
        VkBuffer        src, 
        VkBuffer        dst, 
        VkDeviceSize    srcOffset, 
        VkDeviceSize    dstOffset, 
        VkDeviceSize    size 
    ) {
        region = {
            .sType     = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
            .srcOffset = srcOffset,
            .dstOffset = dstOffset,
            .size      = size,
        };

        cpyInfo2 = {
            .sType       = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
            .srcBuffer   = src,
            .dstBuffer   = dst,
            .regionCount = 1,
            .pRegions    = &region,
        };
    }
};

#endif // !__VK_UTILS_H__

