#ifndef __VK_DESCRIPTOR_H__
#define __VK_DESCRIPTOR_H__

#define VK_NO_PROTOTYPES

#include <vulkan.h>



#include <array>

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

struct vk_descriptor_write
{
	vk_descriptor_info descInfo;
	u16 descIdx;
};

struct vk_table_entry
{
	std::vector<u16> freeSlots;
	u16 slotsCount;
	u16 usedSlots;
};

struct vk_desc_table
{
	static constexpr VkDescriptorType bindingToTypeMap[] = {
		VK_DESCRIPTOR_TYPE_SAMPLER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
	};

	static constexpr u32 DescTypeToBinding( VkDescriptorType type )
	{
		switch( type )
		{
		case VK_DESCRIPTOR_TYPE_SAMPLER: return 0;
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: return 1;
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: return 2;
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return 3;
		default: return INVALID_IDX;
		}
	}

	static constexpr u64 BINDING_COUNT = std::size( bindingToTypeMap );

	std::array<u32, BINDING_COUNT> bindings;

	inline auto& operator[]( VkDescriptorType descType ) { return bindings[ DescTypeToBinding( descType ) ]; }
};

struct vk_desc_state
{
	VkDescriptorPool pool;
	VkDescriptorSetLayout setLayout;
	VkDescriptorSet set;
	alignas( 8 ) vk_desc_table descTable;
};

struct vk_descriptor_manager
{
	struct vk_table_entry
	{
		std::vector<u16> freeSlots;
		u16 slotsCount;
		u16 usedSlots;
	};

	vk_table_entry table[ 4 ];
	std::vector<vk_descriptor_write> updateCache;

	VkDescriptorPool pool;
	VkDescriptorSetLayout setLayout;
	VkDescriptorSet set;
};

inline u16 VkAllocDescriptorIdx( vk_descriptor_manager& manager, const vk_descriptor_info& rscDescInfo )
{
	//u32 bindingSlotIdx = VkDescTypeToBinding( rscDescInfo.descriptorType );
	//
	//assert( bindingSlotIdx != INVALID_IDX );
	//assert( rscDescInfo.descriptorType == VkDescBindingToType( bindingSlotIdx ) );
	//
	//u16 destIndex = INVALID_IDX;
	//auto& binding = manager.table[ bindingSlotIdx ];
	//
	//if( u64 sz = std::size( binding.freeSlots ); sz )
	//{
	//	destIndex = binding.freeSlots[ sz - 1 ];
	//	binding.freeSlots.pop_back();
	//}
	//else if( binding.usedSlots + 1 < binding.slotsCount )
	//{
	//	destIndex = binding.usedSlots++;
	//}
	//else assert( 0 && "Desc table overflow" );
	//
	//assert( destIndex != INVALID_IDX );
	//
	//manager.updateCache.push_back( { rscDescInfo, destIndex } );
	//
	//return destIndex;
	return 0;
}

inline void VkDescriptorManagerFlushUpdates( vk_descriptor_manager& manager, VkDevice vkDevice )
{
	//if( std::size( manager.updateCache ) )
	//{
	//	std::vector<VkWriteDescriptorSet> writes;
	//	for( const auto& update : manager.updateCache )
	//	{
	//		VkDescriptorType descType = update.descInfo.descriptorType;
	//		u16 updateIdx = update.descIdx;
	//		u32 bindingSlotIdx = VkDescTypeToBinding( descType );
	//
	//		const VkDescriptorImageInfo* pImageInfo = 0;
	//		const VkDescriptorBufferInfo* pBufferInfo = 0;
	//
	//		if( update.descInfo.rscType == vk_descriptor_resource_type::BUFFER )
	//		{
	//			pBufferInfo = &update.descInfo.buff;
	//		}
	//		else if( update.descInfo.rscType == vk_descriptor_resource_type::IMAGE )
	//		{
	//			pImageInfo = &update.descInfo.img;
	//		}
	//
	//		VkWriteDescriptorSet writeEntryInfo = {
	//			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	//			.dstSet = manager.set,
	//			.dstBinding = bindingSlotIdx,
	//			.dstArrayElement = updateIdx,
	//			.descriptorCount = 1,
	//			.descriptorType = descType,
	//			.pImageInfo = pImageInfo,
	//			.pBufferInfo = pBufferInfo
	//		};
	//		writes.push_back( writeEntryInfo );
	//	}
	//
	//	vkUpdateDescriptorSets( vkDevice, std::size( writes ), std::data( writes ), 0, 0 );
	//
	//	manager.updateCache.resize( 0 );
	//}
}

#endif // !__VK_DESCRIPTOR_H__
