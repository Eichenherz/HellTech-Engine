#ifndef __VK_MEMORY_H__
#define __VK_MEMORY_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>
#include "vk_procs.h"

#include "core_types.h"
#include "ht_utils.h"
#include "vk_error.h"

#include <vector>

constexpr u64 VK_MIN_DEVICE_BLOCK_SIZE = 32 * MB;

inline u32
VkFindMemTypeIdx(
	const VkPhysicalDeviceMemoryProperties& pVkMemProps,
	VkMemoryPropertyFlags				requiredProps,
	u32									memTypeBitsRequirement
){
	for( u32 memIdx = 0; memIdx < pVkMemProps.memoryTypeCount; ++memIdx )
	{
		u32 memTypeBits = ( 1 << memIdx );
		bool isRequiredMemType = memTypeBitsRequirement & memTypeBits;

		VkMemoryPropertyFlags props = pVkMemProps.memoryTypes[ memIdx ].propertyFlags;
		bool hasRequiredProps = ( props & requiredProps ) == requiredProps;
		if( isRequiredMemType && hasRequiredProps )
		{
			return memIdx;
		}
	}

	return u32( -1 );
}

struct vk_mem_block
{
	VkDeviceMemory mem;
	void* hostMapped;
	VkDeviceSize size; 
	VkDeviceSize offset;
	bool dedicated;
};

struct vk_allocation
{
	VkDeviceMemory  deviceMem;
	u8*				hostVisible = 0;
	u64				dataOffset;
};

struct vk_mem_arena
{
	std::vector<vk_mem_block>	memBlocks;
	u64							minVkAllocationSize;
	u64							allocated;
	VkMemoryPropertyFlags		memTypeProperties;
	u8							memTypeIdx;
};

inline static vk_mem_arena
VkMakeMemoryArena(
	const VkPhysicalDeviceMemoryProperties& memProps,
	VkMemoryPropertyFlags				memType
){
	i32 i = 0;
	for( ; i < memProps.memoryTypeCount; ++i )
	{
		if( memProps.memoryTypes[ i ].propertyFlags == memType ) break;
	}

	VK_CHECK( VK_INTERNAL_ERROR( i == memProps.memoryTypeCount ) );

	VkMemoryHeap backingHeap = memProps.memoryHeaps[ memProps.memoryTypes[ i ].heapIndex ];
	u64 minVkAllocationSize = ( backingHeap.size < VK_MIN_DEVICE_BLOCK_SIZE ) ? 
		( backingHeap.size / 2 ) : VK_MIN_DEVICE_BLOCK_SIZE;

	VkMemoryPropertyFlags memTypeProperties = memProps.memoryTypes[ i ].propertyFlags;

	return {
		.minVkAllocationSize = minVkAllocationSize,
		.allocated = 0,
		.memTypeProperties = memTypeProperties,
		.memTypeIdx = (u8)i,
	};
}


// NOTE: NV driver bug not allow HOST_VISIBLE + VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT for uniforms
inline vk_mem_block
VkAllocDeviceMemBlock(
	VkDevice								vkDevice,
	VkDeviceSize							size,
	u32										memTypeIdx,
	VkMemoryAllocateFlags					allocFlags,
	VkMemoryPropertyFlags				    memTypeProperties,
	const VkMemoryDedicatedAllocateInfo*	dedicated
) {
	VkMemoryAllocateFlagsInfo allocFlagsInfo = { 
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
		.pNext = dedicated,
		.flags = // allocFlags;
		//VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT |
		VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
	};

	VkMemoryAllocateInfo memoryAllocateInfo = { 
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
#if 1
		.pNext = ( memTypeIdx == 0xA ) ? 0 : &allocFlagsInfo,
#else
		.pNext = &allocFlagsInfo,
#endif
	    .allocationSize = size,
	    .memoryTypeIndex = memTypeIdx,
	};

	VkDeviceMemory deviceMem;
	VK_CHECK( vkAllocateMemory( vkDevice, &memoryAllocateInfo, 0, &deviceMem ) );

	void* hostVisible = 0;
	if( memTypeProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT )
	{
		VK_CHECK( vkMapMemory( vkDevice, deviceMem, 0, VK_WHOLE_SIZE, 0, &hostVisible ) );
	}

	return { .mem = deviceMem, .hostMapped = hostVisible, .size = size, .offset = 0, .dedicated = ( bool )dedicated };
}



inline vk_allocation
VkArenaAlignAlloc(
	VkDevice vkDevice,
	vk_mem_arena* vkArena,
	VkDeviceSize size,
	VkDeviceSize align,
	VkMemoryAllocateFlags				allocFlags,
	const VkMemoryDedicatedAllocateInfo*	dedicated
) {
	if( dedicated )
	{
		vk_mem_block memBlock = VkAllocDeviceMemBlock( 
			vkDevice, size, vkArena->memTypeIdx, vkArena->memTypeProperties, allocFlags, dedicated );
		vkArena->memBlocks.push_back( memBlock );
		return {
			.deviceMem = memBlock.mem,
			.hostVisible = ( u8* ) memBlock.hostMapped,
			.dataOffset = memBlock.offset
		};
	}

	u64 blockIdx = 0;
	for( ; blockIdx < std::size( vkArena->memBlocks ); ++blockIdx )
	{
		vk_mem_block& currMemBlock = vkArena->memBlocks[ blockIdx ];
		if( currMemBlock.dedicated )
		{
			continue;
		}
		u64 alignedOffset = FwdAlign( currMemBlock.offset, align );
		if( alignedOffset + size <= currMemBlock.size )
		{
			currMemBlock.offset = alignedOffset;
			break; // Found a block that can hold our alloc
		}
	}

	// Didn't find any block or there's none so we create one
	if( ( 0 == blockIdx ) || ( blockIdx == std::size( vkArena->memBlocks ) ) )
	{
		u64 newArenaSize = std::max( size, vkArena->minVkAllocationSize );
		vk_mem_block memBlock = VkAllocDeviceMemBlock( 
			vkDevice, newArenaSize, vkArena->memTypeIdx, vkArena->memTypeProperties, allocFlags, 0 );
		vkArena->memBlocks.push_back( memBlock );
		//vkArena->allocated += newArenaSize; // Note keep track

		blockIdx = std::size( vkArena->memBlocks ) - 1;
	}

	vk_mem_block& validBlock = vkArena->memBlocks[ blockIdx ];
	assert( validBlock.offset % align == 0 );

	vk_allocation alloc = { validBlock.mem, (u8*) validBlock.hostMapped, validBlock.offset };
	validBlock.offset += size;

	return alloc;
}

// TODO: revisit
//inline void
//VkArenaTerimate( vk_mem_arena* vkArena )
//{
//	for( u64 i = 0; i < std::size( vkArena->memBlocks ); ++i )
//		vkFreeMemory( vkArena->device, vkArena->memBlocks[ i ].device, 0 );
//	for( u64 i = 0; i < std::size( vkArena->dedicatedAllocs ); ++i )
//		vkFreeMemory( vkArena->device, vkArena->dedicatedAllocs[ i ].device, 0 );
//}



#endif // !__VK_MEMORY_H__
