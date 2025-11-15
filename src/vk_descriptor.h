#ifndef __VK_DESCRIPTOR_H__
#define __VK_DESCRIPTOR_H__

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define __VK
#include "DEFS_WIN32_NO_BS.h"
#include <vulkan.h>
#include "vk_procs.h"

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
	default: return (u32)INVALID_IDX;
	}
}

struct vk_descriptor_write
{
	vk_descriptor_info descInfo;
	u16 descIdx;
};

struct vk_descriptor_manager
{
	struct vk_table_entry
	{
		std::vector<u16> freeSlots;
		u16 slotsCount;
		u16 usedSlots;
	};

	vk_table_entry table[ std::size( bindingToTypeMap ) ];
	std::vector<vk_descriptor_write> updateCache;

	VkDescriptorPool pool;
	VkDescriptorSetLayout setLayout;
	VkDescriptorSet set;
};

inline vk_descriptor_manager VkMakeDescriptorManager( VkDevice vkDevice, const VkPhysicalDeviceProperties& gpuProps )
{
	constexpr u32 maxSize = u16( -1 );
	constexpr u32 maxSetCount = 1;

	u32 descCount[ std::size( bindingToTypeMap ) ] = {
		std::min( maxSize, gpuProps.limits.maxDescriptorSetSamplers ),
		std::min( maxSize, gpuProps.limits.maxDescriptorSetStorageBuffers ),
		std::min( maxSize, gpuProps.limits.maxDescriptorSetStorageImages ),
		std::min( maxSize, gpuProps.limits.maxDescriptorSetSampledImages )
	};

	VkDescriptorPoolSize poolSizes[ std::size( bindingToTypeMap ) ] = {};
	for( u32 i = 0; i < std::size( bindingToTypeMap ); ++i )
	{
		poolSizes[ i ] = { .type = bindingToTypeMap[ i ],.descriptorCount = descCount[ i ] };
	}

	VkDescriptorPool vkDescPool;
	VkDescriptorPoolCreateInfo descPoolInfo = { 
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		.maxSets = maxSetCount,
		.poolSizeCount = std::size( poolSizes ),
		.pPoolSizes = poolSizes
	};
	VK_CHECK( vkCreateDescriptorPool( vkDevice, &descPoolInfo, 0, &vkDescPool ) );


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

	VkDescriptorSetLayout vkDescSetLayout;
	VK_CHECK( vkCreateDescriptorSetLayout( vkDevice, &descSetLayoutInfo, 0, &vkDescSetLayout ) );

	VkDescriptorSetAllocateInfo descSetInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = vkDescPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &vkDescSetLayout
	};

	VkDescriptorSet vkDescSet;
	VK_CHECK( vkAllocateDescriptorSets( vkDevice, &descSetInfo, &vkDescSet ) );

	vk_descriptor_manager manager =  {
		.pool = vkDescPool,
		.setLayout = vkDescSetLayout,
		.set = vkDescSet
	};

	for( u32 i = 0; i < std::size( manager.table ); ++i )
	{
		manager.table[ i ] = { .slotsCount = maxSize, .usedSlots = 0 };
	}

	return manager;
}

inline u16 VkAllocDescriptorIdx( vk_descriptor_manager& manager, const vk_descriptor_info& rscDescInfo )
{
	u32 bindingSlotIdx = VkDescTypeToBinding( rscDescInfo.descriptorType );

	assert( bindingSlotIdx != INVALID_IDX );
	assert( rscDescInfo.descriptorType == VkDescBindingToType( bindingSlotIdx ) );

	u16 destIndex = (u16)INVALID_IDX;
	auto& binding = manager.table[ bindingSlotIdx ];

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

	manager.updateCache.push_back( { rscDescInfo, destIndex } );

	return destIndex;
}

inline void VkDescriptorManagerFlushUpdates( vk_descriptor_manager& manager, VkDevice vkDevice )
{
	if( std::size( manager.updateCache ) )
	{
		std::vector<VkWriteDescriptorSet> writes;
		for( const auto& update : manager.updateCache )
		{
			VkDescriptorType descType = update.descInfo.descriptorType;
			u16 updateIdx = update.descIdx;
			u32 bindingSlotIdx = VkDescTypeToBinding( descType );

			const VkDescriptorImageInfo* pImageInfo = 0;
			const VkDescriptorBufferInfo* pBufferInfo = 0;

			if( update.descInfo.rscType == vk_descriptor_resource_type::BUFFER )
			{
				pBufferInfo = &update.descInfo.buff;
			}
			else if( update.descInfo.rscType == vk_descriptor_resource_type::IMAGE )
			{
				pImageInfo = &update.descInfo.img;
			}

			VkWriteDescriptorSet writeEntryInfo = {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = manager.set,
				.dstBinding = bindingSlotIdx,
				.dstArrayElement = updateIdx,
				.descriptorCount = 1,
				.descriptorType = descType,
				.pImageInfo = pImageInfo,
				.pBufferInfo = pBufferInfo
			};
			writes.push_back( writeEntryInfo );
		}

		vkUpdateDescriptorSets( vkDevice, std::size( writes ), std::data( writes ),
			0, 0 );

		manager.updateCache.resize( 0 );
	}
}

inline VkPipelineLayout VkMakeGlobalPipelineLayout( 
	VkDevice vkDevice, 
	const VkPhysicalDeviceProperties& props, 
	const vk_descriptor_manager& vkDescDealer
) {
	VkPushConstantRange pushConstRange = { VK_SHADER_STAGE_ALL, 0, props.limits.maxPushConstantsSize };
	VkPipelineLayoutCreateInfo pipeLayoutInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &vkDescDealer.setLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstRange
	};

	VkPipelineLayout pipelineLayout = {};
	VK_CHECK( vkCreatePipelineLayout( vkDevice, &pipeLayoutInfo, 0, &pipelineLayout ) );

	return pipelineLayout;
}

#endif // !__VK_DESCRIPTOR_H__
