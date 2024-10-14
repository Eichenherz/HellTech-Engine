#ifndef __VK_DESCRIPTORS__
#define __VK_DESCRIPTORS__

#include "vk_common.hpp"

#include <vector>

#include "vk_utils.hpp"

// TODO: porper handle ?
using desc_index = u16;

// TODO: revisit
using vk_binding_list = std::initializer_list<std::pair<u32, u32>>;

constexpr VkDescriptorType bindingToTypeMap[] = {
	VK_DESCRIPTOR_TYPE_SAMPLER,
	//VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
	VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
};

constexpr VkDescriptorType VkDescBindingToType( u32 binding )
{
	switch( binding )
	{
	case 0: return VK_DESCRIPTOR_TYPE_SAMPLER;
	case 1: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	case 2: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	case 3: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	}
}
constexpr u32 VkDescTypeToBinding( VkDescriptorType type )
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

using vk_buffer_descriptor_info = VkDescriptorBufferInfo;
using vk_image_descriptor_info = VkDescriptorImageInfo;

struct vk_sampler_descriptor_info
{
	VkDescriptorImageInfo descInfo;
	vk_sampler_descriptor_info( VkSampler vkSampler )
	{
		this->descInfo = VkDescriptorImageInfo{ .sampler = vkSampler };
	}
};

// TODO: use freelist ?
struct vk_table_entry
{
	std::vector<u16> freeSlots;
	u16 slotsCount;
	u16 usedSlots;
};

// TODO: better bindingSlot <--> descType mapping
// TODO: return u32 handles instead of raw u16 indices ?
struct vk_descriptor_manager
{
	vk_table_entry table[ std::size( bindingToTypeMap ) ];
	std::vector<VkWriteDescriptorSet> pendingUpdates;

	VkDescriptorPool pool;
	VkDescriptorSetLayout setLayout;
	VkDescriptorSet set;
	VkPipelineLayout pipelineLayout;

	template<typename T>
	inline u16 AllocateDescriptorIndex( const T& rscDescInfo );
};


template<typename T>
inline u16 vk_descriptor_manager::AllocateDescriptorIndex( const T& rscDescInfo )
{
	VkDescriptorType descriptorType = {};
	const VkDescriptorBufferInfo* pBuffInfo = 0;
	const VkDescriptorImageInfo* pImgInfo = 0;
	if constexpr( std::is_same<T, vk_buffer_descriptor_info>::value )
	{
		descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		pBuffInfo = ( const VkDescriptorBufferInfo* ) &rscDescInfo;
	}
	else if constexpr( std::is_same<T, vk_image_descriptor_info>::value )
	{
		const VkDescriptorImageInfo& imgDescInfo = rscDescInfo;
		assert( imgDescInfo.sampler == 0 );
		assert( imgDescInfo.imageView && imgDescInfo.imageLayout );
		descriptorType = ( imgDescInfo.imageLayout == VK_IMAGE_LAYOUT_GENERAL ) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE :
			VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

		pImgInfo = &imgDescInfo;
	}
	else if constexpr( std::is_same<T, vk_sampler_descriptor_info>::value )
	{
		auto& imgDescInfo = ( const VkDescriptorImageInfo& ) rscDescInfo;
		assert( imgDescInfo.sampler != 0 );
		descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		pImgInfo = &imgDescInfo;
	}
	//else static_assert( 0 );

	u32 bindingSlotIdx = VkDescTypeToBinding( descriptorType );

	assert( bindingSlotIdx != INVALID_IDX );
	assert( descriptorType == VkDescBindingToType( bindingSlotIdx ) );
	assert( bool( pBuffInfo ) ^ bool( pImgInfo ) );


	u16 destIndex = INVALID_IDX;
	auto& binding = this->table[ bindingSlotIdx ];

	if( u64 sz = std::size( binding.freeSlots ); sz )
	{
		destIndex = binding.freeSlots[ sz - 1 ];
		binding.freeSlots.pop_back();
	}
	else if( binding.usedSlots + 1 < binding.slotsCount )
	{
		destIndex = binding.usedSlots++;
	}
	else assert( 0 && "Desc table overflow" );

	assert( destIndex != INVALID_IDX );


	VkWriteDescriptorSet writeEntryInfo = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = this->set,
		.dstBinding = bindingSlotIdx,
		.dstArrayElement = destIndex,
		.descriptorCount = 1,
		.descriptorType = descriptorType,
		.pImageInfo = pImgInfo,
		.pBufferInfo = pBuffInfo
	};
	this->pendingUpdates.emplace_back( writeEntryInfo );

	return destIndex;
}

#endif