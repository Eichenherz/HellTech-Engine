#ifndef __VK_DESCRIPTOR_H__
#define __VK_DESCRIPTOR_H__

#define VK_NO_PROTOTYPES

#include <vulkan.h>

#include <EASTL/fixed_vector.h>

#include "vector_freelist.h"

#include "ht_error.h"
#include "vk_types.h"
#include "vk_resources.h"
#include "core_types.h"

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

	vk_descriptor_info( VkDescriptorBufferInfo buffInfo ) : buff{ buffInfo }{}
	vk_descriptor_info( VkDescriptorImageInfo imgInfo ) : img{ imgInfo }{}
};

enum vk_desc_binding_t : u32
{
	SAMPLER = 0,
	STORAGE_BUFFER,
	STORAGE_IMAGE,
	SAMPLED_IMAGE,
	COUNT
};

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

// TODO: not here
static_assert( vk_renderer_config::MAX_DESCRIPTOR_COUNT_PER_TYPE == u64( u16( -1 ) ) );
struct desc_hndl32
{
	u32 slot : 16;
	u32 type : 2;
	u32 unused : 14;
};


struct vk_descriptor_write
{
	vk_descriptor_info descInfo;
	desc_hndl32 hndl;
};

using vk_desc_vector = eastl::fixed_vector<vector_freelist, vk_desc_binding_t::COUNT, false>;

struct vk_descriptor_allocator
{
	vk_desc_vector  bindingSlotFreelist;
	std::vector<vk_descriptor_write> pendingUpdates;

	VkDescriptorPool pool;
	VkDescriptorSetLayout setLayout;
	VkDescriptorSet set;

	inline void Free( desc_hndl32 descIdx )
	{
		vector_freelist& slotAlloc = bindingSlotFreelist[ descIdx.type ];
		slotAlloc.erase( descIdx.slot );
	}
};


#endif // !__VK_DESCRIPTOR_H__
