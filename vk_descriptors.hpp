#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define __VK
#include "DEFS_WIN32_NO_BS.h"
// TODO: autogen custom vulkan ?
#include <vulkan.h>
// TODO: header + .cpp ?
// TODO: revisit this
#include "vk_procs.h"

#include <vector>

#include "core_types.h"
#include "vk_utils.hpp"


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

// TODO: better bindingSlot <--> descType mapping
// TODO: return u32 handles instead of raw u16 indices ?
struct vk_descriptor_manager
{
	// TODO: use freelist ?
	struct vk_table_entry
	{
		std::vector<u16> freeSlots;
		u16 slotsCount;
		u16 usedSlots;
	};

	vk_table_entry table[ std::size( bindingToTypeMap ) ];
	std::vector<VkWriteDescriptorSet> pendingUpdates;

	VkDescriptorPool pool;
	VkDescriptorSetLayout setLayout;
	VkDescriptorSet set;
	VkPipelineLayout globalPipelineLayout;
};


inline vk_descriptor_manager VkMakeDescriptorManager( VkDevice vkDevice, const VkPhysicalDeviceProperties& gpuProps )
{
	constexpr u32 maxSize = u16( -1 );
	constexpr u32 maxSetCount = 1;

	u32 descCount[ std::size( bindingToTypeMap ) ] = {
		std::min( 8u, gpuProps.limits.maxDescriptorSetSamplers ),
		std::min( maxSize, gpuProps.limits.maxDescriptorSetStorageBuffers ),
		std::min( maxSize, gpuProps.limits.maxDescriptorSetStorageImages ),
		std::min( maxSize, gpuProps.limits.maxDescriptorSetSampledImages )
	};

	vk_descriptor_manager mngr = {};


	VkDescriptorPoolSize poolSizes[ std::size( bindingToTypeMap ) ] = {};
	for( u32 i = 0; i < std::size( bindingToTypeMap ); ++i )
	{
		poolSizes[ i ] = { .type = bindingToTypeMap[ i ],.descriptorCount = descCount[ i ] };
	}

	VkDescriptorPoolCreateInfo descPoolInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		.maxSets = maxSetCount,
		.poolSizeCount = std::size( poolSizes ),
		.pPoolSizes = poolSizes
	};
	VK_CHECK( vkCreateDescriptorPool( vkDevice, &descPoolInfo, 0, &mngr.pool ) );


	constexpr VkDescriptorBindingFlags flag =
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

	VkDescriptorBindingFlags bindingFlags[ std::size( bindingToTypeMap ) ] = {};
	VkDescriptorSetLayoutBinding descSetLayout[ std::size( bindingToTypeMap ) ] = {};
	for( u32 i = 0; i < std::size( bindingToTypeMap ); ++i )
	{
		descSetLayout[ i ] = {
			.binding = i,
			.descriptorType = bindingToTypeMap[ i ],
			.descriptorCount = descCount[ i ],
			.stageFlags = VK_SHADER_STAGE_ALL
		};
		bindingFlags[ i ] = flag;
	}

	bindingFlags[ std::size( bindingToTypeMap ) ] |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

	VkDescriptorSetLayoutBindingFlagsCreateInfo descSetFalgs = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.bindingCount = std::size( bindingFlags ),
		.pBindingFlags = bindingFlags
	};
	VkDescriptorSetLayoutCreateInfo descSetLayoutInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &descSetFalgs,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
		.bindingCount = std::size( descSetLayout ),
		.pBindings = descSetLayout
	};

	VK_CHECK( vkCreateDescriptorSetLayout( vkDevice, &descSetLayoutInfo, 0, &mngr.setLayout ) );

	VkDescriptorSetAllocateInfo descSetInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = mngr.pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &mngr.setLayout
	};

	VkDescriptorSet set = {};
	VK_CHECK( vkAllocateDescriptorSets( vkDevice, &descSetInfo, &mngr.set ) );

	for( u32 i = 0; i < std::size( mngr.table ); ++i )
	{
		mngr.table[ i ] = { .slotsCount = maxSize, .usedSlots = 0 };
	}

	VkPushConstantRange pushConstRange = { VK_SHADER_STAGE_ALL, 0, gpuProps.limits.maxPushConstantsSize };
	VkPipelineLayoutCreateInfo pipeLayoutInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &mngr.setLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstRange
	};
	VK_CHECK( vkCreatePipelineLayout( vkDevice, &pipeLayoutInfo, 0, &mngr.globalPipelineLayout ) );
	VkDbgNameObj( mngr.globalPipelineLayout, vkDevice, "Vk_Pipeline_Layout_Global" );

	return mngr;
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

template<typename T>
inline u16 VkAllocDescriptorIdx( VkDevice vkDevice, const T& rscDescInfo, vk_descriptor_manager& dealer )
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
	auto& binding = dealer.table[ bindingSlotIdx ];

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
		.dstSet = dealer.set,
		.dstBinding = bindingSlotIdx,
		.dstArrayElement = destIndex,
		.descriptorCount = 1,
		.descriptorType = descriptorType,
		.pImageInfo = pImgInfo,
		.pBufferInfo = pBuffInfo
	};
	dealer.pendingUpdates.emplace_back( writeEntryInfo );
	return destIndex;
}

inline void VkFlushDescriptorUpdates( VkDevice vkDevice, vk_descriptor_manager& dealer )
{
	if( std::size( dealer.pendingUpdates ) == 0 )
	{
		return;
	}

	vkUpdateDescriptorSets( vkDevice, std::size( dealer.pendingUpdates ), std::data( dealer.pendingUpdates ), 0, 0 );
	dealer.pendingUpdates.resize( 0 );
}