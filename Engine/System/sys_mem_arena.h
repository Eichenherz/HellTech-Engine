#pragma once

#ifndef __SYS_MEM_ARENA_H__
#define __SYS_MEM_ARENA_H__

#include <memory_resource>

#include "core_types.h"
#include "ht_error.h"
#include "ht_utils.h"

template<u64 SZ_IN_BYTES>
struct fixed_arena
{
	alignas( 8 ) u8 mem[ SZ_IN_BYTES ];
	u64             offset = 0;

	void	Rewind( u64 mark ) { offset = ( mark <= SZ_IN_BYTES ) ? mark : SZ_IN_BYTES; }
	void	Reset() { offset = 0; }
	void*	Alloc( u64 bytes, u64 alignment )
	{
		u64 base = ( u64 ) mem;
		u64 allignedAddr = FwdAlign( base + offset, alignment );
		u64 newOffset = ( allignedAddr - base ) + bytes;

		HT_ASSERT( newOffset <= SZ_IN_BYTES );

		offset = newOffset;
		return ( void* ) allignedAddr;
	}
};

struct virtual_arena
{
    static constexpr u64 PAGE_SIZE = 4096;

    u8*     base = nullptr;
    u64     offset = 0;
    u64     committed = 0;
    u64     reserved = 0;
     
            virtual_arena() = default;
            virtual_arena( u64 reservedBytesCount );

            ~virtual_arena();

            virtual_arena( const virtual_arena& ) = delete;
            virtual_arena& operator=( const virtual_arena& ) = delete;

    void    Rewind( u64 mark );
    void    Reset();
    void*   Alloc( u64 bytes, u64 alignment );
};

template<typename T>
concept arena_t = requires( T a, u64 bytes, u64 alignment, u64 mark )
{
	{ a.Alloc( bytes, alignment ) } -> std::same_as<void*>;
	{ a.Rewind( mark ) }            -> std::same_as<void>;
	{ a.Reset() }					-> std::same_as<void>;
	{ a.offset }					-> std::convertible_to<u64>;
};

template<arena_t Arena>
struct stack_adaptor : std::pmr::memory_resource
{
	Arena& arena;
	u64 baseFrameOffset;

	inline stack_adaptor( Arena& a ) : arena{ a }, baseFrameOffset{ a.offset }{}
	inline ~stack_adaptor() { arena.Rewind( baseFrameOffset ); }

protected: // NOTE: std::pmr::memory_resource's API
	void*   do_allocate( size_t bytes, size_t alignment ) override { return arena.Alloc( bytes, alignment ); }
	void	do_deallocate( void*, size_t, size_t ) override { /* no-op */ }
	bool	do_is_equal( const std::pmr::memory_resource& other ) const noexcept override { return this == &other; }
};

#endif // !__SYS_MEM_ARENA_H__
