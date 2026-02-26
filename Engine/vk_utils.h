#pragma once

#ifndef __VK_UTILS_H__
#define __VK_UTILS_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include "core_types.h"

#include "vk_types.h"
#include "ht_error.h"

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
	case SAMPLER: return VK_DESCRIPTOR_TYPE_SAMPLER;
	case STORAGE_BUFFER: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	case STORAGE_IMAGE: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	case SAMPLED_IMAGE: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	default: HT_ASSERT( 0 && "Wrong descriptor type" ); 
	}
	return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

inline constexpr vk_desc_binding_t VkDescTypeToBinding( VkDescriptorType type )
{
	using enum vk_desc_binding_t;
	switch( type )
	{
	case VK_DESCRIPTOR_TYPE_SAMPLER: return SAMPLER;
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: return STORAGE_BUFFER;
	case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: return STORAGE_IMAGE;
	case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return SAMPLED_IMAGE;
	default: HT_ASSERT( 0 && "Wrong descriptor type" ); 
	}
	return COUNT;
}


#endif // !__VK_UTILS_H__

