#pragma once

#ifndef __HT_MEM_ARENA_H__
#define __HT_MEM_ARENA_H__

#include <iterator>
#include <span>

#include <ht_core_types.h>
#include <ht_error.h>
#include <ht_utils.h>

// NOTE: We poison the border between allocs to detect trampling;
// markers MUST start at an 8byte alignment to not be lost
#ifdef __HT_USE_ASAN__

#include <sanitizer/asan_interface.h>

constexpr u64 HT_ASAN_BORDER	= 16;
constexpr u64 HT_ASAN_MIN_ALIGN	= 8;

#define HT_POISON( ptr, bytes )		__asan_poison_memory_region( ( ptr ), ( bytes ) )
#define HT_POISON_IF( cond, ptr, bytes )	if( cond ) { HT_POISON( ptr, bytes ); }
#define HT_UNPOISON( ptr, bytes )	__asan_unpoison_memory_region( ( ptr ), ( bytes ) )
// NOTE: this is for sub marking the range that we already own
#define HT_ANNOTATE_CONTAINER( beg, end, oldMid, newMid )	\
	__sanitizer_annotate_contiguous_container( ( beg ), ( end ), ( oldMid ), ( newMid ) )

#else

constexpr u64 HT_ASAN_BORDER	= 0;
constexpr u64 HT_ASAN_MIN_ALIGN	= 1;

#define HT_POISON( ptr, bytes )
#define HT_POISON_IF( cond, ptr, bytes )
#define HT_UNPOISON( ptr, bytes )
#define HT_ANNOTATE_CONTAINER( beg, end, oldMid, newMid )

#endif // __HT_USE_ASAN__

template<typename T>
concept arena_t = requires( T a, std::span<u8> alloc, u64 bytes, u64 alignment, u64 mark )
{
	{ a.mem }				                -> std::convertible_to<u8*>;
	{ a.offsetInBytes }				        -> std::convertible_to<u64>;
	{ a.sizeInBytes }				        -> std::convertible_to<u64>;
    { a.Alloc( bytes, alignment ) }         -> std::same_as<void*>;
    { a.TryStretchAlloc( alloc, bytes ) }   -> std::same_as<u64>;
	{ a.Rewind( mark ) }			        -> std::same_as<void>;
};

struct linear_arena
{
    u8* mem             = nullptr;
    u64 offsetInBytes   = 0;
    u64 sizeInBytes     = 0;

    linear_arena() = default;
    linear_arena( void* mem, u64 szInBytes ) : mem{ ( u8* ) mem }, sizeInBytes{ szInBytes }
    {
        HT_ASSERT( ( nullptr != mem ) && ( 0 != sizeInBytes ) );
    }
    linear_arena( std::span<u8> alloc ) : mem{ std::data( alloc ) }, sizeInBytes{ std::size( alloc ) }
    {
        HT_ASSERT( ( nullptr != mem ) && ( 0 != sizeInBytes ) );
    }

    void    Rewind( u64 markInBytes )
    {
        HT_ASSERT( markInBytes <= sizeInBytes );
        HT_POISON_IF( markInBytes < offsetInBytes, mem + markInBytes, offsetInBytes - markInBytes );
        offsetInBytes = markInBytes;
    }
    void*   Alloc( u64 szInBytes, u64 alignment )
    {
        u64 base        = ( u64 ) mem;
        u64 align       = std::max( alignment, HT_ASAN_MIN_ALIGN );
        u64 alignedAddr = FwdAlignPot( base + offsetInBytes, align );
        u64 newOffset   = ( alignedAddr - base ) + szInBytes + HT_ASAN_BORDER;

        HT_ASSERT( newOffset <= sizeInBytes );
        offsetInBytes = newOffset;

        HT_UNPOISON( ( void* ) alignedAddr, szInBytes );
        HT_POISON( ( void* ) ( alignedAddr + szInBytes ), HT_ASAN_BORDER );
        return ( void* ) alignedAddr;
    }
    u64 TryStretchAlloc( std::span<u8> alloc, u64 stretchInBytes )
    {
        u64     allocOffset     = ( std::data( alloc ) - mem ) + std::size( alloc ) + HT_ASAN_BORDER;
        bool    canStretch      = allocOffset == offsetInBytes;
        bool    legalStretch    = ( allocOffset + stretchInBytes + HT_ASAN_BORDER ) <= sizeInBytes;

        if( !canStretch || !legalStretch ) return ~0ull;

        HT_UNPOISON( mem + allocOffset - HT_ASAN_BORDER, HT_ASAN_BORDER + stretchInBytes );
        HT_POISON( mem + allocOffset + stretchInBytes, HT_ASAN_BORDER );
        offsetInBytes = allocOffset + stretchInBytes + HT_ASAN_BORDER;
        return std::size( alloc ) + stretchInBytes;
    }
};


template<typename T, arena_t Arena>
T* ArenaNew( Arena& arena ) { return new ( arena.Alloc( sizeof( T ), alignof( T ) ) ) T; }

template<typename T, arena_t Arena, typename... Args>
T* ArenaMake( Arena& arena, Args&&... args )
{
	return new ( arena.Alloc( sizeof( T ), alignof( T ) ) ) T{ FWD( args )... };
}

template<typename T, arena_t Arena>
std::span<T> ArenaNewArray( Arena& arena, u64 elemCount )
{
	// NOTE: otherwise C++ adds 8 bytes top of our alloc
	static_assert( std::is_trivially_destructible_v<T> );
	return { new ( arena.Alloc( sizeof( T ) * elemCount, alignof( T ) ) ) T[ elemCount ], elemCount };
}

// NOTE: acts like a slim stack
template<arena_t Arena>
struct ht_mem_scope
{
	Arena&	arena;
	u64		baseFrameOffset;

    ht_mem_scope( Arena& a ) : arena{ a }, baseFrameOffset{ a.offsetInBytes }{}
    ~ht_mem_scope() { arena.Rewind( baseFrameOffset ); }

	NO_COPY();
	NO_MOVE();
};

#endif // !__HT_MEM_ARENA_H__