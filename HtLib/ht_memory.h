#pragma once

#ifndef __HT_MEMORY_H__
#define __HT_MEMORY_H__

#include <span>

#include <System/sys_sync.h>

#include <ht_core_types.h>
#include <ht_error.h>
#include <ht_utils.h>
#include <ht_math.h>

constexpr u64 OS_COMMIT_PAGE_SIZE_IN_BYTES	= 4096;
constexpr u64 OS_RESERVE_PAGE_SIZE_IN_BYTES	= 64 << 10;
// NOTE: on win user takes 0 to this and kernel takes the rest of tha range
constexpr u64 OS_USER_MAX_ADDR              = 0x00007ffffffeffff;
constexpr u64 OS_USER_ADDR_BIT_WIDTH	    = std::bit_width( OS_USER_MAX_ADDR );

// NOTE: these crash on failure
void*	ht_os_virtual_reserve( u64 sizeInBytes );
// NOTE: release also frees all commited pages in that range
void	ht_os_virtual_release( void* mem );
void*	ht_os_virtual_commit( void* mem, u64 sizeInBytes );
void	ht_os_virtual_decommit( void* mem, u64 sizeInBytes );
void*   ht_os_virtual_alloc( u64 sizeInBytes );


#ifndef HT_BLOCK_SZ_IN_BYTES
#define HT_BLOCK_SZ_IN_BYTES	( 256 * KB )
#endif
constexpr u64 BLOCK_SZ_IN_BYTES	= HT_BLOCK_SZ_IN_BYTES;
static_assert( IsPowOf2( BLOCK_SZ_IN_BYTES ) );

// NOTE: we will demand every allocation be aligned to this, like that we'll get the low 16 bits for metadata as well
constexpr u64 HT_INTERNAL_ALIGNMENT = 64 << 10;
static_assert( FwdAlignPot( BLOCK_SZ_IN_BYTES, HT_INTERNAL_ALIGNMENT ) == BLOCK_SZ_IN_BYTES );

// NOTE: we capped the max alloc at this because we can only atomically CAS up to u64 bits
constexpr u64 BLOCKS_PER_BIN				= 64;
constexpr u64 BINS_PER_CHUNK				= 8;
constexpr u64 BIN_SZ_IN_BYTES				= BLOCKS_PER_BIN * BLOCK_SZ_IN_BYTES;
constexpr u64 CHUNK_SZ_IN_BYTES 			= BINS_PER_CHUNK * BIN_SZ_IN_BYTES;
// NOTE: while we can alloc anything from 1 block to full bin, this defeats the point of the chunks.
// Basically, if we alloc a big run we're killing the benefits of the amortized os_commit. ( ie 50/64 from the same bin )
// To avoid this we will cap the max bin alloc size.
constexpr u64 MAX_BIN_ALLOC_SZ_IN_BLOCKS	= 4;

// NOTE: for now it's hardcoded
constexpr u64 MAX_RESERVE_SZ_IN_BYTES	    = 32 * GB;
static_assert( IsPowOf2( MAX_RESERVE_SZ_IN_BYTES ) );
// NOTE: in order to drastically simplify our alloc process we'll reserve a huge chunk
// and split it into chunk/blocks and dedicated. The dedicated part, for simplicity,
// will be linear, so at one point we'll basically burn space ( atomic_u64 will track ).
// This is fine because we expect huge allocs to be few and this naturally provides a fail cap.
// However, the aforementioned cap is not enough for the chunk section
constexpr u64 RESERVED_SPACE_SPLIT          = 4;
constexpr u64 CHUNK_REGION_ELEM_COUNT       = ( MAX_RESERVE_SZ_IN_BYTES / RESERVED_SPACE_SPLIT ) / CHUNK_SZ_IN_BYTES;
constexpr u64 CHUNK_REGION_CAP_IN_BYTES     = CHUNK_REGION_ELEM_COUNT * CHUNK_SZ_IN_BYTES;
constexpr u64 DEDICATED_REGION_CAP_IN_BYTES = MAX_RESERVE_SZ_IN_BYTES - CHUNK_REGION_CAP_IN_BYTES;
static_assert( ( 0 != CHUNK_REGION_ELEM_COUNT ) && ( CHUNK_REGION_CAP_IN_BYTES < MAX_RESERVE_SZ_IN_BYTES ) );

using ht_virt_alloc = std::span<u8>;

constexpr ht_virt_alloc INVALID_HALLOC = {};

// NOTE: inspired by mimalloc
struct alignas( 64 ) ht_virtual_chunk
{
	// NOTE: 0 for free, 1 for committed
	atomic_u64 blockBitmap[ BINS_PER_CHUNK ];
};

struct ht_virtual_allocator
{
	u8*						    pMemBase		        = nullptr;
	u64							reservedInBytes	        = 0;
	std::span<ht_virtual_chunk>	chunkMap                = {};
    alignas( 64 ) atomic_u64    committedInBytes        = 0;
    alignas( 64 ) atomic_u64    dedicatedAllocOffset    = CHUNK_REGION_CAP_IN_BYTES;

	ht_virt_alloc	AllocVirtualBlock( u64 requestSzInBytes, u64 threadIndex );
	void			FreeVirtualBlock( ht_virt_alloc alloc, u64 threadIdx );
};

ht_virtual_allocator HtMakeVirtualAllocator( u64 maxMemCapInBytes );

extern thread_local ht_virtual_allocator* g_pVirtualAllocator;

#endif // !__HT_MEMORY_H__
