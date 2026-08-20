#pragma once

#ifndef __HT_MEMORY_H__
#define __HT_MEMORY_H__

#include <memory_resource>
#include <span>

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
		u64 base = ( u64 ) mem;
		u64 alignedAddr = FwdAlignPot( base + offset, alignment );
		u64 newOffset = ( alignedAddr - base ) + bytes;

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
		u64 base = ( u64 ) mem;
		u64 alignedAddr = FwdAlignPot( base + offset, alignment );
		u64 newOffset = ( alignedAddr - base ) + bytes;

		HT_ASSERT( newOffset <= size );

		offset = newOffset;
		return ( void* ) alignedAddr;
	}
};

constexpr u64 OS_PAGE_SIZE_IN_BYTES = 4096;
// NOTE: these crash on failure
void*	ht_os_virtual_reserve( u64 sizeInBytes );
// NOTE: release also frees all commited pages in that range
void	ht_os_virtual_release( void* mem );
void*	ht_os_virtual_commit( void* mem, u64 sizeInBytes );
void	ht_os_virtual_decommit( void* mem, u64 sizeInBytes );
void*   ht_os_virtual_alloc( u64 sizeInBytes );

struct virtual_arena
{
    static constexpr u64 PAGE_SIZE = 4096;

    u8*     mem			= nullptr;
    u64     offset		= 0;
    u64     committed	= 0;
    u64     reserved	= 0;

            virtual_arena() = default;
            virtual_arena( u64 reservedBytesCount );

    void    Rewind( u64 mark );
    void    Reset();
    void*   Alloc( u64 bytes, u64 alignment );
    // NOTE: commits an explicit page range without moving offset, for mem handed to an allocator
    //       that writes outside the bump frontier. Recommitting live pages is a no op.
    //       Returns the page aligned end it committed, clamped to reserved
    u64     CommitRange( u64 begOffset, u64 endOffset );
};

void VirtualArenaFree( virtual_arena& arena );


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

	inline	stack_adaptor( Arena& a ) : arena{ a }, baseFrameOffset{ a.offset }{}
	inline	~stack_adaptor() override { arena.Rewind( baseFrameOffset ); }
	u8*		BasePtr() { return arena.mem + baseFrameOffset; }
protected: // NOTE: std::pmr::memory_resource's API
	void*   do_allocate( size_t bytes, size_t alignment ) override { return arena.Alloc( bytes, alignment ); }
	void	do_deallocate( void*, size_t, size_t ) override { /* no-op */ }
	bool	do_is_equal( const std::pmr::memory_resource& other ) const noexcept override { return this == &other; }
};

constexpr u64 BLOCK_SZ_IN_BYTES		= 4 * MB;
// NOTE: we capped the max alloc at this because we can only atomically CAS up to u64 bits
constexpr u64 BLOCKS_PER_BIN = 64;

constexpr u64 BINS_PER_CHUNK = 8;

// NOTE: inspired by mimalloc
struct alignas( 64 ) ht_virtual_chunk
{
	// NOTE: 0 for free, 1 for committed
	// NOTE: must use CAS to claim these blocks, volatile to explicitly RW from mem
	volatile u64 blockBins[ BINS_PER_CHUNK ];
};

// NOTE: cache alignment means the payload START is SIMD ready and false sharing free
// NOTE: the false sharing between the nodes themselves doesn't really matter here
// as these might not get freed or VERY rarely
struct alignas( 64 ) ht_huge_alloc
{
	ht_huge_alloc*	pNext		= nullptr;
	ht_huge_alloc*	pPrev		= nullptr;
	u64				szInBytes	= 0;
};

// NOTE: circular list so no branches needed
inline void HtHugeAllocLink( ht_huge_alloc* list, ht_huge_alloc* pNode, u64 szInBytes )
{
	ht_huge_alloc*  pOldHead = list->pNext;
	pNode->pNext		= pOldHead;
	pNode->pPrev		= list;
	pOldHead->pPrev		= pNode;
	list->pNext			= pNode;
	pNode->szInBytes	= szInBytes;
}

inline u64 /* sizeInBytes */ HtHugeAllocUnlink( ht_huge_alloc* pNode )
{
	ht_huge_alloc*  pNext = pNode->pNext;
	ht_huge_alloc*  pPrev = pNode->pPrev;
	pPrev->pNext = pNext;
	pNext->pPrev = pPrev;

	return pNode->szInBytes;
}

constexpr u64 CHUNK_SZ_IN_BYTES = BINS_PER_CHUNK * BLOCKS_PER_BIN * BLOCK_SZ_IN_BYTES;

enum ht_virt_alloc_type : u16
{
	DEDICATED	= 0,
	BLOCK		= 1
};

struct ht_alloc_metadata
{
	u16 padding		: 8;
	u16 type		: 1;
	u16 blockCount	: 7; // NOTE: we CAN do this because we can alloc up to 64 CONTIGUOUS block
};

struct ht_virt_alloc
{
	static constexpr u64 TAGGED_PTR_BITS = 44;
	static constexpr u64 TAGGED_PTR_MASK = ( 1ull << TAGGED_PTR_BITS ) - 1;


	u64 metadata	: 64 - TAGGED_PTR_BITS	= 0;
	u64 address		: TAGGED_PTR_BITS		= 0;

	ht_virt_alloc() = default;
	ht_virt_alloc( void* ptr, ht_alloc_metadata meta ) :
		metadata{ BitCastIrregular<u64>( std::bit_cast<u16>( meta ), 64 - TAGGED_PTR_BITS ) },
		address{ BitCastIrregular<u64>( std::bit_cast<u64>( ptr ), TAGGED_PTR_BITS ) }{}

	// NOTE: C++20 will use this for !=
	bool operator==( ht_virt_alloc other ) const { return std::bit_cast<u64>( *this ) == std::bit_cast<u64>( other ); }
};

inline auto HtUnpackVirtualAllocation( ht_virt_alloc alloc )
{
	struct retval { void* ptr; ht_alloc_metadata meta; };
	return retval{ std::bit_cast<void*>( alloc.address ), std::bit_cast<ht_alloc_metadata>( ( u16 ) alloc.metadata ) };
}


static_assert( sizeof( u64 ) == sizeof( ht_virt_alloc ) );

struct ht_virtual_allocator
{
	void*						pMemBase		= nullptr; // NOTE: points at the start of the blocks from chunkMap
	u64							reservedInBytes	= 0;
	std::span<ht_virtual_chunk>	chunkMap		= {};
	// NOTE: using a circular doubly linked list we avoid branches
	// NOTE: the allocator will never be relocated ( we have NO COPY NO MOVE )
	ht_huge_alloc				circularList	= { .pNext = &circularList, .pPrev = &circularList };
	// TODO: add a lock for huge pages bc atomics are ABSOLUTELY NOT TRIVIAL for it

	ht_virt_alloc	AllocVirtualBlock( u64 requestSzInBytes, u64 threadIndex );
	void			FreeVirtualBlock( ht_virt_alloc alloc, u64 threadIdx );

	u8*	Alloc( u64 bytes, u64 alignment )
	{

	}
	void Free( void* mem ) {  }

	NO_COPY();
	NO_MOVE();
};

ht_virtual_allocator HtMakeAllocator( u64 maxMemCapInBytes );

#endif // !__HT_MEMORY_H__
