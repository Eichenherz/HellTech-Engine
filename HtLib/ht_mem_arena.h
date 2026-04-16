#pragma once

#ifndef __SYS_MEM_ARENA_H__
#define __SYS_MEM_ARENA_H__

#include <memory_resource>

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
		u64 alignedAddr = FwdAlign( base + offset, alignment );
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
		u64 alignedAddr = FwdAlign( base + offset, alignment );
		u64 newOffset = ( alignedAddr - base ) + bytes;

		HT_ASSERT( newOffset <= size );

		offset = newOffset;
		return ( void* ) alignedAddr;
	}
};

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
	inline	~stack_adaptor() { arena.Rewind( baseFrameOffset ); }
	u8*		BasePtr() { return arena.mem + baseFrameOffset; }
protected: // NOTE: std::pmr::memory_resource's API
	void*   do_allocate( size_t bytes, size_t alignment ) override { return arena.Alloc( bytes, alignment ); }
	void	do_deallocate( void*, size_t, size_t ) override { /* no-op */ }
	bool	do_is_equal( const std::pmr::memory_resource& other ) const noexcept override { return this == &other; }
};

#endif // !__SYS_MEM_ARENA_H__
