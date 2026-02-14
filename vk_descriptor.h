#ifndef __VK_DESCRIPTOR_H__
#define __VK_DESCRIPTOR_H__

#define VK_NO_PROTOTYPES

#include <vulkan.h>

#include <array>

#include "vector_freelist.h"

#include "vk_error.h"
#include "vk_resources.h"
#include "core_types.h"


enum class vk_descriptor_resource_type : u8
{
	BUFFER,
	IMAGE
};

struct vk_descriptor_info
{
	union
	{
		VkDescriptorBufferInfo buff;
		VkDescriptorImageInfo img;
	};
	VkDescriptorType descriptorType;
	vk_descriptor_resource_type rscType;

	vk_descriptor_info() = default;
	vk_descriptor_info( const vk_buffer& vkBuff ) : buff{ vkBuff.hndl, 0, vkBuff.sizeInBytes }
	{
		descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		rscType = vk_descriptor_resource_type::BUFFER;
	}
	vk_descriptor_info( VkBuffer buff, u64 offset, u64 range ) : buff{ buff, offset, range }
	{
		descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		rscType = vk_descriptor_resource_type::BUFFER;
	}
	vk_descriptor_info( VkImageView view, VkImageLayout imgLayout ) : img{ .imageView = view, .imageLayout = imgLayout }
	{
		descriptorType = ( imgLayout == VK_IMAGE_LAYOUT_GENERAL ) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE :
			VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		rscType = vk_descriptor_resource_type::IMAGE;
	}
	vk_descriptor_info( VkSampler sampler ) : img{ .sampler = sampler, .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED }
	{
		descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		rscType = vk_descriptor_resource_type::IMAGE;
	}

	vk_descriptor_info( VkDescriptorBufferInfo buffInfo ) : buff{ buffInfo }{}
	vk_descriptor_info( VkDescriptorImageInfo imgInfo ) : img{ imgInfo }{}
};

constexpr VkDescriptorType bindingToTypeMap[] = {
	VK_DESCRIPTOR_TYPE_SAMPLER,
	VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
	VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
};

inline constexpr u32 VkDescTypeToBinding( VkDescriptorType type )
{
	switch( type )
	{
	case VK_DESCRIPTOR_TYPE_SAMPLER: return 0;
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: return 1;
	case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: return 2;
	case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return 3;
	default: HT_ASSERT( 0 && "Wrong descriptor type" ); 
	}
	return INVALID_IDX;
}

struct vk_desc_table
{
	static constexpr u64 BINDING_COUNT = std::size( bindingToTypeMap );

	std::array<u32, BINDING_COUNT> bindingsSize;

	inline auto& operator[]( VkDescriptorType descType ) const { return bindingsSize[ VkDescTypeToBinding( descType ) ]; }
};

struct vk_desc_state
{
	VkDescriptorPool pool;
	VkDescriptorSetLayout setLayout;
	VkDescriptorSet set;
	alignas( 8 ) vk_desc_table descTable;
};

struct desc_slot_idx
{
	u32 slot : 30;
	u32 binding : 2;
};

struct vk_descriptor_write
{
	vk_descriptor_info descInfo;
	desc_slot_idx descIdx;
};

struct vk_descriptor_allocator
{
	vector_freelist bindingAlloc[ vk_desc_table::BINDING_COUNT ];

	std::vector<vk_descriptor_write> pendingUpdates;

	vk_descriptor_allocator( const vk_desc_table& descTable )
	{
		HT_ASSERT( std::size( bindingAlloc ) == vk_desc_table::BINDING_COUNT );
		for( u64 bi = 0; bi < vk_desc_table::BINDING_COUNT; bi++ )
		{
			bindingAlloc[ bi ] = vector_freelist( descTable.bindingsSize[ bi ] );
		}
	}

	inline desc_slot_idx Alloc( const vk_descriptor_info& rscDescInfo )
	{
		u32 bindingIdx = VkDescTypeToBinding( rscDescInfo.descriptorType );
		vector_freelist& slotAlloc = bindingAlloc[ bindingIdx ];

		desc_slot_idx descIdx = { .slot = slotAlloc.push(), .binding = bindingIdx };
		HT_ASSERT( INVALID_IDX != descIdx.slot );

		pendingUpdates.push_back( { rscDescInfo, descIdx } );

		return descIdx;
	}

	inline void Free( desc_slot_idx descIdx )
	{
		vector_freelist& slotAlloc = bindingAlloc[ descIdx.binding ];
		slotAlloc.erase( descIdx.slot );
	}
};


#endif // !__VK_DESCRIPTOR_H__
