#ifndef __VK_MEMORY_H__
#define __VK_MEMORY_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>
#include "vk_procs.h"

#include "core_types.h"
#include "ht_utils.h"
#include "vk_error.h"

#include <vector>

constexpr u64 VK_MIN_DEVICE_BLOCK_SIZE = 256 * MB;

// TODO: make this part of the device ?
// TODO: move to memory section
// TODO: pass VkPhysicalDevice differently
// TODO: multi gpu ?
// TODO: multi threaded
// TODO: better alloc strategy ?
// TODO: alloc vector for debug only ?
// TODO: check alloc num ?
// TODO: recycle memory ?
// TODO: redesign ?
// TODO: per mem-block flags
struct vk_mem_view
{
	VkDeviceMemory	device;
	void*			host = 0;
};

struct vk_allocation
{
	VkDeviceMemory  deviceMem;
	u8*				hostVisible = 0;
	u64				dataOffset;
};

struct vk_mem_arena
{
	std::vector<vk_mem_view>	mem;
	std::vector<vk_mem_view>	dedicatedAllocs;
	u64							maxParentHeapSize;
	u64							minVkAllocationSize = VK_MIN_DEVICE_BLOCK_SIZE;
	u64							size;
	u64							allocated;
	VkDevice					device;
	VkMemoryPropertyFlags		memTypeProperties;
	u32							memTypeIdx;
};

// TODO: integrate into rsc creation
inline i32
VkFindMemTypeIdx(
	const VkPhysicalDeviceMemoryProperties* pVkMemProps,
	VkMemoryPropertyFlags				requiredProps,
	u32									memTypeBitsRequirement
){
	for( u64 memIdx = 0; memIdx < pVkMemProps->memoryTypeCount; ++memIdx )
	{
		u32 memTypeBits = ( 1 << memIdx );
		bool isRequiredMemType = memTypeBitsRequirement & memTypeBits;

		VkMemoryPropertyFlags props = pVkMemProps->memoryTypes[ memIdx ].propertyFlags;
		bool hasRequiredProps = ( props & requiredProps ) == requiredProps;
		if( isRequiredMemType && hasRequiredProps ) return (i32) memIdx;
	}

	VK_CHECK( VK_INTERNAL_ERROR( "Memory type unmatch !" ) );

	return -1;
}

// TODO: move alloc flags ?
// NOTE: NV driver bug not allow HOST_VISIBLE + VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT for uniforms
inline static VkDeviceMemory
VkTryAllocDeviceMem(
	VkDevice								vkDevice,
	u64										size,
	u32										memTypeIdx,
	VkMemoryAllocateFlags					allocFlags,
	const VkMemoryDedicatedAllocateInfo*	dedicated
){
	VkMemoryAllocateFlagsInfo allocFlagsInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
	allocFlagsInfo.pNext = dedicated;
	allocFlagsInfo.flags = // allocFlags;
		//VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT |
		VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

	VkMemoryAllocateInfo memoryAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
#if 1
	memoryAllocateInfo.pNext = ( memTypeIdx == 0xA ) ? 0 : &allocFlagsInfo;
#else
	memoryAllocateInfo.pNext = &allocFlagsInfo;
#endif
	memoryAllocateInfo.allocationSize = size;
	memoryAllocateInfo.memoryTypeIndex = memTypeIdx;

	VkDeviceMemory mem;
	VK_CHECK( vkAllocateMemory( vkDevice, &memoryAllocateInfo, 0, &mem ) );

	return mem;
}

inline static vk_mem_arena
VkMakeMemoryArena(
	const VkPhysicalDeviceMemoryProperties& memProps,
	VkMemoryPropertyFlags				memType,
	VkDevice							vkDevice
){
	i32 i = 0;
	for( ; i < memProps.memoryTypeCount; ++i )
	{
		if( memProps.memoryTypes[ i ].propertyFlags == memType ) break;
	}

	VK_CHECK( VK_INTERNAL_ERROR( i == memProps.memoryTypeCount ) );

	VkMemoryHeap backingHeap = memProps.memoryHeaps[ memProps.memoryTypes[ i ].heapIndex ];

	vk_mem_arena vkArena = {};
	vkArena.allocated = 0;
	vkArena.size = 0;
	vkArena.memTypeIdx = i;
	vkArena.memTypeProperties = memProps.memoryTypes[ i ].propertyFlags;
	vkArena.maxParentHeapSize = backingHeap.size;
	vkArena.minVkAllocationSize = ( backingHeap.size < VK_MIN_DEVICE_BLOCK_SIZE ) ? ( 1 * MB ) : VK_MIN_DEVICE_BLOCK_SIZE;
	vkArena.device = vkDevice;

	return vkArena;
}

// TODO: offset the global, persistently mapped hostVisible pointer when sub-allocating
// TODO: assert vs VK_CHECK vs default + warning
// TODO: must alloc in block with BUFFER_ADDR
inline vk_allocation
VkArenaAlignAlloc(
	vk_mem_arena* vkArena,
	u64										size,
	u64										align,
	u32										memTypeIdx,
	VkMemoryAllocateFlags					allocFlags,
	const VkMemoryDedicatedAllocateInfo* dedicated
){
	assert( size <= vkArena->maxParentHeapSize );

	u64 allocatedWithOffset = FwdAlign( vkArena->allocated, align );
	vkArena->allocated = allocatedWithOffset;

	if( dedicated )
	{
		VkDeviceMemory deviceMem = VkTryAllocDeviceMem( vkArena->device, size, memTypeIdx, allocFlags, dedicated );
		void* hostVisible = 0;
		if( vkArena->memTypeProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT )
		{
			VK_CHECK( vkMapMemory( vkArena->device, deviceMem, 0, VK_WHOLE_SIZE, 0, &hostVisible ) );
		}
		vk_mem_view lastDedicated = { deviceMem,hostVisible };
		vkArena->dedicatedAllocs.push_back( lastDedicated );
		return { lastDedicated.device, (u8*) lastDedicated.host, 0 };
	}

	if( ( vkArena->allocated + size ) > vkArena->size )
	{
		u64 newArenaSize = std::max( size, vkArena->minVkAllocationSize );
		VkDeviceMemory deviceMem = VkTryAllocDeviceMem( vkArena->device, newArenaSize, memTypeIdx, allocFlags, 0 );
		void* hostVisible = 0;
		if( vkArena->memTypeProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT )
		{
			VK_CHECK( vkMapMemory( vkArena->device, deviceMem, 0, VK_WHOLE_SIZE, 0, &hostVisible ) );
		}
		vkArena->mem.push_back( { deviceMem,hostVisible } );
		vkArena->size = newArenaSize;
		vkArena->allocated = 0;
	}

	assert( ( vkArena->allocated + size ) <= vkArena->size );
	assert( vkArena->allocated % align == 0 );

	vk_mem_view lastBlock = vkArena->mem[ std::size( vkArena->mem ) - 1 ];
	vk_allocation allocId = { lastBlock.device, (u8*) lastBlock.host, vkArena->allocated };
	vkArena->allocated += size;

	return allocId;
}

// TODO: revisit
inline void
VkArenaTerimate( vk_mem_arena* vkArena )
{
	for( u64 i = 0; i < std::size( vkArena->mem ); ++i )
		vkFreeMemory( vkArena->device, vkArena->mem[ i ].device, 0 );
	for( u64 i = 0; i < std::size( vkArena->dedicatedAllocs ); ++i )
		vkFreeMemory( vkArena->device, vkArena->dedicatedAllocs[ i ].device, 0 );
}



#endif // !__VK_MEMORY_H__
