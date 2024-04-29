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

#include <string_view>
#include <assert.h>
#include <vector>


#include "sys_os_api.h"
#include "core_lib_api.h"

#include "vk_utils.hpp"
#include "math_util.hpp"

constexpr u64 VK_MIN_DEVICE_BLOCK_SIZE = 256 * MB;
constexpr VkMemoryPropertyFlags VK_MEMORY_PROPERTY_HOST_VISIBLE = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

// TODO: better alloc strategy ?
// TODO: check alloc num ?
// TODO: recycle memory ?
// TODO: per mem-block flags
struct vk_mem_view
{
	VkDeviceMemory	device;
	u8* host = 0;
};

struct vk_allocation
{
	VkDeviceMemory  deviceMem;
	u8* hostVisible = 0;
	u64				dataOffset;
};

struct vk_mem_arena
{
	std::vector<vk_mem_view>	mem;
	std::vector<vk_mem_view>	dedicatedAllocs;
	u64							maxParentHeapSize;
	u64							minVkAllocationSize;
	u64							size;
	u64							allocated;
	VkMemoryPropertyFlags		memTypeProperties;
	u32							memTypeIdx;
};

// TODO: move alloc flags ?
// NOTE: NV driver bug not allow HOST_VISIBLE + VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT for uniforms
vk_mem_view
VkTryAllocDeviceMem(
	VkDevice								vkDevice,
	u64										size,
	u32										memTypeIdx,
	VkMemoryAllocateFlags					allocFlags,
	bool isHostVisible,
	const VkMemoryDedicatedAllocateInfo* dedicated
) {
	VkMemoryAllocateFlagsInfo allocFlagsInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
		.pNext = dedicated,
		.flags = // allocFlags;
		//VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT |
		VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
	};

	VkMemoryAllocateInfo memoryAllocateInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
#if 1
		.pNext = ( memTypeIdx == 0xA ) ? 0 : &allocFlagsInfo,
#else
		.pNext = &allocFlagsInfo,
#endif
		.allocationSize = size,
		.memoryTypeIndex = memTypeIdx
	};

	VkDeviceMemory mem;
	VK_CHECK( vkAllocateMemory( vkDevice, &memoryAllocateInfo, 0, &mem ) );

	void* hostVisible = 0;
	if( isHostVisible )
	{
		VK_CHECK( vkMapMemory( vkDevice, mem, 0, VK_WHOLE_SIZE, 0, &hostVisible ) );
	}
	return { mem, ( u8* ) hostVisible };
}

vk_mem_arena VkMakeMemoryArena( const VkPhysicalDeviceMemoryProperties& memProps, VkMemoryPropertyFlags memType )
{
	u32 i = 0;
	for( ; i < memProps.memoryTypeCount; ++i )
	{
		if( memProps.memoryTypes[ i ].propertyFlags == memType ) break;
	}

	VK_CHECK( VK_INTERNAL_ERROR( i == memProps.memoryTypeCount ) );

	VkMemoryHeap backingHeap = memProps.memoryHeaps[ memProps.memoryTypes[ i ].heapIndex ];

	return {
		.maxParentHeapSize = backingHeap.size,
		.minVkAllocationSize = ( backingHeap.size < VK_MIN_DEVICE_BLOCK_SIZE ) ? ( 1 * MB ) : VK_MIN_DEVICE_BLOCK_SIZE,
		.size = 0,
		.allocated = 0,
		.memTypeProperties = memProps.memoryTypes[ i ].propertyFlags,
		.memTypeIdx = i
	};
}

struct vk_mem_requirements
{
	VkMemoryDedicatedAllocateInfo dedicated;
	u64										size;
	u64										align;
	i32										memTypeIdx;
	VkMemoryAllocateFlags					allocFlags;
	bool requiresDedicated;
};

// TODO: offset the global, persistently mapped hostVisible pointer when sub-allocating
// TODO: assert vs VK_CHECK vs default + warning
// TODO: must alloc in block with BUFFER_ADDR
inline vk_allocation
VkArenaAlignAlloc(
	vk_mem_arena* vkArena,
	u64										size,
	u64										align,
	i32										memTypeIdx,
	VkMemoryAllocateFlags					allocFlags,
	const VkMemoryDedicatedAllocateInfo* dedicated
) {
	VK_CHECK( VK_INTERNAL_ERROR( !( memTypeIdx == vkArena->memTypeIdx ) ) );
	assert( memTypeIdx == vkArena->memTypeIdx );
	assert( size <= vkArena->maxParentHeapSize );

	u64 allocatedWithOffset = FwdAlign( vkArena->allocated, align );
	vkArena->allocated = allocatedWithOffset;
	bool isHostVisible = vkArena->memTypeProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

	if( dedicated )
	{
		vk_mem_view alloc = VkTryAllocDeviceMem( vkArena->device, size, memTypeIdx, allocFlags, isHostVisible, dedicated );
		vkArena->dedicatedAllocs.push_back( alloc );
		return { alloc.device, ( u8* ) alloc.host, 0 };
	}

	if( ( vkArena->allocated + size ) > vkArena->size )
	{
		u64 newArenaSize = std::max( size, vkArena->minVkAllocationSize );
		vk_mem_view alloc = VkTryAllocDeviceMem( vkArena->device, newArenaSize, memTypeIdx, allocFlags, isHostVisible, 0 );
		vkArena->mem.push_back( alloc );
		vkArena->size = newArenaSize;
		vkArena->allocated = 0;
	}

	assert( ( vkArena->allocated + size ) <= vkArena->size );
	assert( vkArena->allocated % align == 0 );

	vk_mem_view lastBlock = vkArena->mem[ std::size( vkArena->mem ) - 1 ];
	vk_allocation allocId = { lastBlock.device, ( u8* ) lastBlock.host, vkArena->allocated };
	vkArena->allocated += size;

	return allocId;
}

inline vk_allocation
VkArenaAlignAlloc(
	vk_mem_arena* vkArena,
	const vk_mem_requirements& memReqs )
{
	return VkArenaAlignAlloc(
		vkArena,
		memReqs.size,
		memReqs.align,
		memReqs.memTypeIdx,
		memReqs.allocFlags,
		memReqs.requiresDedicated ? &memReqs.dedicated : 0
	);
}

// TODO: revisit
inline void VkArenaTerimate( vk_mem_arena* vkArena )
{
	for( u64 i = 0; i < std::size( vkArena->mem ); ++i )
	{
		vkFreeMemory( vkArena->device, vkArena->mem[ i ].device, 0 );
	}
		
	for( u64 i = 0; i < std::size( vkArena->dedicatedAllocs ); ++i )
	{
		vkFreeMemory( vkArena->device, vkArena->dedicatedAllocs[ i ].device, 0 );
	}
}



inline u64 VkGetBufferDeviceAddress( VkDevice vkDevice, VkBuffer hndl )
{
	static_assert( std::is_same<VkDeviceAddress, u64>::value );

	VkBufferDeviceAddressInfo deviceAddrInfo = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
	deviceAddrInfo.buffer = hndl;

	return vkGetBufferDeviceAddress( vkDevice, &deviceAddrInfo );
}


enum vk_arena_type : u8
{
	BUFFERS = 0,
	IMAGES = 1,
	HOST_VISIBLE = 2,
	STAGING = 3,
	DEBUG = 4,
	MAX_MEM_TYPES = 5
};

struct vk_memory
{
	vk_mem_arena arenas[ vk_arena_type::MAX_MEM_TYPES ];
};



inline vk_memory VkInitGfxMemory( VkPhysicalDevice vkPhysicalDevice, VkDevice vkDevice )
{
	VkPhysicalDeviceMemoryProperties memProps = {};
	vkGetPhysicalDeviceMemoryProperties( vkPhysicalDevice, &memProps );

	vk_memory vkMem = {};
	vkMem.arenas[ vk_arena_type::BUFFERS ] = VkMakeMemoryArena( memProps, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	vkMem.arenas[ vk_arena_type::IMAGES ] = VkMakeMemoryArena( memProps, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	vkMem.arenas[ vk_arena_type::HOST_VISIBLE ] =
		VkMakeMemoryArena( memProps, VK_MEMORY_PROPERTY_HOST_VISIBLE | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	vkMem.arenas[ vk_arena_type::STAGING ] = VkMakeMemoryArena( memProps, VK_MEMORY_PROPERTY_HOST_VISIBLE );
	vkMem.arenas[ vk_arena_type::DEBUG ] = VkMakeMemoryArena( memProps, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

	return vkMem;
}
