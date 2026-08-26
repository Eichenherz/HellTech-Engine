#pragma once

#ifndef __HT_MEMORY_H__
#define __HT_MEMORY_H__

#include <memory_resource>
#include <span>

#include <System/sys_sync.h>

#include "ht_core_types.h"
#include "ht_error.h"
#include "ht_utils.h"

template<u64 SZ_IN_BYTES>
struct static_arena
{
	alignas( 8 ) u8 mem[ SZ_IN_BYTES ] = {};
	u64             offset = 0;

	void	Rewind( u64 mark ) { offset = ( mark <= SZ_IN_BYTES ) ? mark : SZ_IN_BYTES; }
	void	Reset() { offset = 0; }
	void*	Alloc( u64 bytes, u64 alignment )
	{
		u64 base		= ( u64 ) mem;
		u64 alignedAddr = FwdAlignPot( base + offset, alignment );
		u64 newOffset	= ( alignedAddr - base ) + bytes;

		HT_ASSERT( newOffset <= SZ_IN_BYTES );

		offset = newOffset;
		return ( void* ) alignedAddr;
	}
};

struct dynamic_arena
{
	u8*     mem    = nullptr;
	u64     offset = 0;
	u64     size   = 0;

	        dynamic_arena() = default;
	        dynamic_arena( u8* mem, u64 size ) : mem{ mem }, size{ size } {}

	void	Rewind( u64 mark ) { offset = ( mark <= size ) ? mark : size; }
	void	Reset() { offset = 0; }
	void*	Alloc( u64 bytes, u64 alignment )
	{
		u64 base		= ( u64 ) mem;
		u64 alignedAddr = FwdAlignPot( base + offset, alignment );
		u64 newOffset	= ( alignedAddr - base ) + bytes;

		HT_ASSERT( newOffset <= size );

		offset = newOffset;
		return ( void* ) alignedAddr;
	}
};

constexpr u64 OS_PAGE_SIZE_IN_BYTES		= 4096;
// NOTE: on win user takes 0 to this and kernel takes the rest of tha range
constexpr u64 OS_USER_MAX_ADDR          = 0x00007ffffffeffff;
constexpr u64 OS_USER_ADDR_BIT_WIDTH	= std::bit_width( OS_USER_MAX_ADDR );

// NOTE: these crash on failure
void*	ht_os_virtual_reserve( u64 sizeInBytes );
// NOTE: release also frees all commited pages in that range
void	ht_os_virtual_release( void* mem );
void*	ht_os_virtual_commit( void* mem, u64 sizeInBytes );
void	ht_os_virtual_decommit( void* mem, u64 sizeInBytes );
void*   ht_os_virtual_alloc( u64 sizeInBytes );


template<typename T>
concept arena_t = requires( T a, u64 bytes, u64 alignment, u64 mark )
{
	{ a.mem }						-> std::convertible_to<u8*>;
	{ a.offset }					-> std::convertible_to<u64>;

	{ a.Alloc( bytes, alignment ) } -> std::same_as<void*>;
	{ a.Rewind( mark ) }            -> std::same_as<void>;
	{ a.Reset() }					-> std::same_as<void>;
};

template<typename T, arena_t Arena>
inline T* ArenaNew( Arena& arena )
{
	return new ( arena.Alloc( sizeof( T ), alignof( T ) ) ) T;
}

template<typename T, arena_t Arena>
inline T* ArenaNewArray( Arena& arena, u64 count )
{
	return new ( arena.Alloc( sizeof( T ) * count, alignof( T ) ) ) T[ count ];
}

template<arena_t Arena>
struct stack_adaptor : std::pmr::memory_resource
{
	Arena&	arena;
	u64		baseFrameOffset;

			stack_adaptor( Arena& a ) : arena{ a }, baseFrameOffset{ a.offset }{}
			~stack_adaptor() override { arena.Rewind( baseFrameOffset ); }
	u8*		BasePtr() { return arena.mem + baseFrameOffset; }
protected: // NOTE: std::pmr::memory_resource's API
	void*   do_allocate( size_t bytes, size_t alignment ) override { return arena.Alloc( bytes, alignment ); }
	void	do_deallocate( void*, size_t, size_t ) override { /* no-op */ }
	bool	do_is_equal( const std::pmr::memory_resource& other ) const noexcept override { return this == &other; }
};

#ifndef HT_BLOCK_SZ_IN_BYTES
#define HT_BLOCK_SZ_IN_BYTES	( 4 * MB )
#endif
constexpr u64 BLOCK_SZ_IN_BYTES	= HT_BLOCK_SZ_IN_BYTES;

// NOTE: we will demand every allocation be aligned to this, like that we'll get the low 16 bits for metadata as well
constexpr u64 HT_INTERNAL_ALIGNMENT = 64 * KB;
// NOTE: dedicated allocs have a bigger alignment bc it helps reduce the size of the 2 level void* to handle alloc;
// also note that the handle has no reason to use the adjusted bit count
constexpr u64 HT_DEDICATED_ALIGNMENT = BLOCK_SZ_IN_BYTES;

static_assert( FwdAlignPot( BLOCK_SZ_IN_BYTES, HT_INTERNAL_ALIGNMENT ) == BLOCK_SZ_IN_BYTES );
// NOTE: we capped the max alloc at this because we can only atomically CAS up to u64 bits
constexpr u64 BLOCKS_PER_BIN	= 64;
constexpr u64 BINS_PER_CHUNK	= 8;

// NOTE: inspired by mimalloc
struct alignas( 64 ) ht_virtual_chunk
{
	// NOTE: 0 for free, 1 for committed
	atomic_u64 blockBitmap[ BINS_PER_CHUNK ];
};

constexpr u64 CHUNK_SZ_IN_BYTES = BINS_PER_CHUNK * BLOCKS_PER_BIN * BLOCK_SZ_IN_BYTES;

enum ht_virt_alloc_type : u64
{
	DEDICATED	= 0,
	BLOCK		= 1
};

constexpr u64 HT_OS_ADDR_HIGHER_FREE_BITS_COUNT	= 64 - OS_USER_ADDR_BIT_WIDTH;
constexpr u64 HT_OS_ADDR_LOWER_FREE_BITS_COUNT	= std::bit_width( HT_INTERNAL_ALIGNMENT ) - 1;
// NOTE: OS_USER_MAX_ADDR is a max value, not a mask ( it has holes ), the mask is width worth of ones
constexpr u64 HT_OS_ADDR_MASK					= ( 1ull << OS_USER_ADDR_BIT_WIDTH ) - 1;

struct ht_virt_alloc
{
	static constexpr u64 HT_META_BIT_COUNT	= HT_OS_ADDR_HIGHER_FREE_BITS_COUNT + HT_OS_ADDR_LOWER_FREE_BITS_COUNT;
	static constexpr u64 HT_ADDR_BIT_COUNT	= 64 - HT_META_BIT_COUNT;

	u64 address		: HT_ADDR_BIT_COUNT		= 0;
	u64 metadata	: HT_META_BIT_COUNT - 1 = 0;
	u64 type		: 1						= 0;

	ht_virt_alloc() = default;
	ht_virt_alloc( void* ptr, u64 payload, ht_virt_alloc_type allocType ) :
		address	{ ( ( ( u64 ) ptr ) & HT_OS_ADDR_MASK ) >> ( HT_OS_ADDR_LOWER_FREE_BITS_COUNT ) },
		metadata{ payload },
		type	{ allocType }
	{
		HT_ASSERT( ( ( address << HT_OS_ADDR_LOWER_FREE_BITS_COUNT ) ) == ( u64 ) ptr );
		HT_ASSERT( ( payload & ( ( ( 1ull << ( HT_META_BIT_COUNT - 1 ) ) - 1 ) ) ) == payload );
	}

	// NOTE: C++20 will use this for !=
	bool operator==( ht_virt_alloc other ) const { return std::bit_cast<u64>( *this ) == std::bit_cast<u64>( other ); }
};

inline void* HtGetAllocPtr( ht_virt_alloc alloc )
{
	return ( void* ) ( u64( alloc.address ) << HT_OS_ADDR_LOWER_FREE_BITS_COUNT );
}

static_assert( sizeof( u64 ) == sizeof( ht_virt_alloc ) );

struct ht_virtual_allocator
{
	void*						pMemBase		= nullptr; // NOTE: points at the start of the blocks from chunkMap
	u64							reservedInBytes	= 0;
	std::span<ht_virtual_chunk>	chunkMap		= {};

	ht_virt_alloc	AllocVirtualBlock( u64 requestSzInBytes, u64 threadIndex );
	void			FreeVirtualBlock( ht_virt_alloc alloc, u64 threadIdx );

	NO_COPY();
	NO_MOVE();
};

ht_virtual_allocator HtMakeAllocator( u64 maxMemCapInBytes );

constexpr u64 THREAD_HEAP_PAGE_SIZE			= 64 * KB;
constexpr u64 THREAD_HEAP_MIN_SIZE_CLASS	= 1 * KB;
// NOTE: for simplicity and symmetry of design, we will use u64 bitmaps for the pages;
// the smallest size class will have the most items which must not exceed the num of bits of u64
static_assert( 64 == ( THREAD_HEAP_PAGE_SIZE / THREAD_HEAP_MIN_SIZE_CLASS ) );

struct ht_thread_heap_page_descriptor
{
	u64	blockBitmap;
	u8	sizeClass;
};

struct ht_thread_heap_block
{
	using ht_th_page_desc = ht_thread_heap_page_descriptor;

	ht_virt_alloc				hAlloc			= {};
	u8*							pMemBase		= nullptr;
	u64							freePagesBitmap = 0;
	std::span<ht_th_page_desc>	pages			= {};
};


#endif // !__HT_MEMORY_H__
