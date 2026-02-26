#pragma once

#ifndef __HT_MEM_ARENA_H__
#define __HT_MEM_ARENA_H__

#include <memory_resource>

#include "core_types.h"
#include "ht_error.h"
#include "ht_utils.h"

template<u64 BYTE_COUNT>
struct fixed_arena
{
	alignas( 8 ) u8 mem[ BYTE_COUNT ];
	u64 offset = 0;

	void	Rewind( u64 m ) { offset = ( m <= BYTE_COUNT ) ? m : BYTE_COUNT; }
	void	Reset() { offset = 0; }
	void*	Alloc( u64 bytes, u64 alignment )
	{
		u64 base = ( u64 ) mem;
		u64 allignedAddr = FwdAlign( base + offset, alignment );
		u64 newOffset = allignedAddr - base + bytes;

		HT_ASSERT( newOffset <= BYTE_COUNT );

		offset = newOffset;
		return ( void* ) allignedAddr;
	}
};

template<typename Arena>
struct scoped_stack : std::pmr::memory_resource
{
	Arena& arena;
	u64 baseFrameOffset;

	inline scoped_stack( Arena& a ) : arena{ a }, baseFrameOffset{ a.offset }{}
	inline ~scoped_stack() { arena.Rewind( baseFrameOffset ); }

protected: // NOTE: std::pmr::memory_resource's API
	void*   do_allocate( size_t bytes, size_t alignment ) override { return arena.Alloc( bytes, alignment ); }
	void	do_deallocate( void*, size_t, size_t ) override { /* no-op */ }
	bool	do_is_equal( const std::pmr::memory_resource& other ) const noexcept override { return this == &other; }
};

#endif // !__HT_MEM_ARENA_H__
