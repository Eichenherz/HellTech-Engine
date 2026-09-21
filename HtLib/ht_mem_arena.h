#pragma once

#ifndef __HT_MEM_ARENA_H__
#define __HT_MEM_ARENA_H__

#include <array>
#include <iterator>
#include <ranges>
#include <span>

#include <ht_core_types.h>
#include <ht_error.h>
#include <ht_utils.h>
#include <ht_memory.h>

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
    { a.Alloc( bytes, alignment ) }         -> std::same_as<void*>;
    { a.TryStretchAlloc( alloc, bytes ) }   -> std::same_as<u64>;
	{ a.Rewind( mark ) }			        -> std::same_as<void>;
    { a.Mark() }                            -> std::same_as<u64>;
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

    u64     Mark() const { return offsetInBytes; }
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

// TODO: fold into scoped_arena
template<arena_t Arena>
struct ht_mem_scope
{
	Arena&	arena;
	u64		baseFrameOffset;

    ht_mem_scope( Arena& a ) : arena{ a }, baseFrameOffset{ a.Mark() }{}
    ~ht_mem_scope() { arena.Rewind( baseFrameOffset ); }

	NO_COPY();
	NO_MOVE();
};

template<arena_t Arena>
struct scoped_arena : ht_mem_scope<Arena>
{
    scoped_arena( Arena& a ) : ht_mem_scope<Arena>{ a }{}

    u64     Mark( this auto&& self ) { return self.arena.Mark(); }
    void    Rewind( this auto&& self, u64 markInBytes ) { self.arena.Rewind( markInBytes ); }
    void*   Alloc( this auto&& self, u64 szInBytes, u64 alignment ) { return self.arena.Alloc( szInBytes, alignment ); }
    u64     TryStretchAlloc( this auto&& self, std::span<u8> alloc, u64 stretchInBytes )
    {
        return self.arena.TryStretchAlloc( alloc, stretchInBytes );
    }

    operator Arena&( this auto&& self ) { return self.arena; }
};

struct virtual_arena
{
    static constexpr u64 COMMIT_SZ_IN_BYTES = 2 * MB;

    linear_arena    linear      = {};
    u64             commited    = 0; // NOTE: linear.sizeInBytes will double as reserved for US !

    virtual_arena() = default;
    virtual_arena( u64 reservedInBytes ) : linear{ ht_os_virtual_reserve( reservedInBytes ), reservedInBytes } {}

    u64     Mark() const { return linear.Mark(); }
    void    Rewind( u64 markInBytes )
    {
        linear.Rewind( markInBytes );
        Decommit( std::max( markInBytes, 2 * GB ) );
    }
    void*   Alloc( u64 szInBytes, u64 alignment );
    u64     TryStretchAlloc( std::span<u8> alloc, u64 stretchInBytes );

    void    Decommit( u64 keepBytes );
};

inline void virtual_arena::Decommit( u64 keepBytes )
{
    HT_ASSERT( keepBytes >= linear.offsetInBytes );

    u64 keepCommitted = FwdAlignPot( keepBytes, OS_RESERVE_PAGE_SIZE_IN_BYTES );
    if( keepCommitted >= commited ) return;

    ht_os_virtual_decommit( linear.mem + keepCommitted, commited - keepCommitted );
    commited = keepCommitted;
}

inline void VirtualArenaCommit( virtual_arena& arena, u64 reqSzInBytes )
{
    u64 reqEnd = FwdAlignPot( arena.linear.offsetInBytes + reqSzInBytes + HT_ASAN_BORDER,
        OS_RESERVE_PAGE_SIZE_IN_BYTES );
    if( reqEnd <= arena.commited ) return;

    u64 newCommitted = std::min( FwdAlignPot( reqEnd, virtual_arena::COMMIT_SZ_IN_BYTES ),
        arena.linear.sizeInBytes );
    ht_os_virtual_commit( arena.linear.mem + arena.commited, newCommitted - arena.commited );
    arena.commited = newCommitted;
}

inline void* virtual_arena::Alloc( u64 szInBytes, u64 alignment )
{
    VirtualArenaCommit( *this, alignment + szInBytes );
    return linear.Alloc( szInBytes, alignment );
}

inline u64 virtual_arena::TryStretchAlloc( std::span<u8> alloc, u64 stretchInBytes )
{
    VirtualArenaCommit( *this, stretchInBytes );
    return linear.TryStretchAlloc( alloc, stretchInBytes );
}

template<typename S, typename ELEM_T>
concept storage_t = std::ranges::contiguous_range<decltype( S::mem )>
    && std::same_as<std::ranges::range_value_t<decltype( S::mem )>, ELEM_T>
    && requires( S s, u64 reqSzInElems )
{
    { s.Grow( reqSzInElems ) }  -> std::same_as<void>;
    { S::CAN_GROW }             -> std::convertible_to<bool>;
    { S::OWNS_ELEMENTS }        -> std::convertible_to<bool>;
};

template<typename T, u64 N>
struct inline_storage
{
    static constexpr bool   CAN_GROW        = false;
    static constexpr bool   OWNS_ELEMENTS   = true;

    std::array<T, N>        mem = {};

    void    Grow( this inline_storage&, u64 reqSzInElems ) { HT_ASSERT( reqSzInElems <= N ); }
};

template<typename T>
struct borrowed_storage
{
    static constexpr bool   CAN_GROW        = false;
    static constexpr bool   OWNS_ELEMENTS   = false;

    std::span<T>            mem             = {};

    void    Grow( this borrowed_storage& self, u64 reqSzInElems ) { HT_ASSERT( reqSzInElems <= std::size( self.mem ) ); }
};

template<TRIVIAL_T T, arena_t ARENA_T = linear_arena>
struct arena_storage
{
    static constexpr bool   CAN_GROW        = true;
    static constexpr bool   OWNS_ELEMENTS   = false;

    using arena_type = ARENA_T;

    std::span<T>            mem             = {};
    ARENA_T*                pArena          = nullptr;

    void    Grow( this arena_storage& self, u64 reqSzInElems );
};

template<TRIVIAL_T T, arena_t ARENA_T>
void arena_storage<T, ARENA_T>::Grow( this arena_storage& self, u64 reqSzInElems )
{
    HT_ASSERT( self.pArena && ( reqSzInElems > std::size( self.mem ) ) );

    u64 reqSzInBytes = reqSzInElems * sizeof( T );

    if( 0 == std::size( self.mem ) )
    {
        self.mem = { ( T* ) self.pArena->Alloc( reqSzInBytes, alignof( T ) ), reqSzInElems };
        return;
    }

    u64 memSzInBytes        = std::size( self.mem ) * sizeof( T );
    u64 stretchedSzInBytes  = self.pArena->TryStretchAlloc(
        { ( u8* ) std::data( self.mem ), memSzInBytes }, reqSzInBytes - memSzInBytes );
    HT_ASSERT( ~0ull != stretchedSzInBytes );

    self.mem = { std::data( self.mem ), stretchedSzInBytes / sizeof( T ) };
}

static_assert( storage_t<inline_storage<u8, 4>, u8>
    && storage_t<borrowed_storage<u8>, u8>
    && storage_t<arena_storage<u8>, u8> );

#endif // !__HT_MEM_ARENA_H__