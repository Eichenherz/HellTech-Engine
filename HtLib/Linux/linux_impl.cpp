#include <sys/mman.h>
#include <cstring>

#include "HtLib/ht_mem_arena.h"
// ===============================================================================================================
// ht_mem_arena.h
// ===============================================================================================================
virtual_arena::virtual_arena( u64 reservedBytesCount ) : reserved{ reservedBytesCount }
{
    base = ( u8* ) mmap( nullptr, reserved, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0 );
    HT_ASSERT( base != MAP_FAILED );
}

virtual_arena::~virtual_arena()
{
    if( base )
    {
        HT_ASSERT( munmap( base, reserved ) == 0 );
    }
}

virtual_arena::virtual_arena( virtual_arena&& o )
    : base{ std::exchange( o.base, nullptr ) }
    , offset{ std::exchange( o.offset, 0 ) }
    , committed{ std::exchange( o.committed, 0 ) }
    , reserved{ std::exchange( o.reserved, 0 ) }
{}

virtual_arena& virtual_arena::operator=( virtual_arena&& o )
{
    HT_ASSERT( this != &o && "self move-assign" );
    base      = std::exchange( o.base, nullptr );
    offset    = std::exchange( o.offset, 0 );
    committed = std::exchange( o.committed, 0 );
    reserved  = std::exchange( o.reserved, 0 );
    return *this;
}

void virtual_arena::Rewind( u64 mark )
{
    HT_ASSERT( mark <= offset && "rewind past current offset" );
#ifdef _DEBUG
    std::memset( base + mark, 0xDE, offset - mark );
#endif
    offset = mark;
}

void virtual_arena::Reset()
{
    if( committed )
    {
    #ifdef _DEBUG
        std::memset( base, 0xDE, committed );
    #endif
        HT_ASSERT( madvise( base, committed, MADV_DONTNEED ) == 0 );
        HT_ASSERT( mprotect( base, committed, PROT_NONE ) == 0 );
    }

    committed = 0;
    offset = 0;
}

void* virtual_arena::Alloc( u64 bytes, u64 alignment )
{
    u64 alignedOffset = FwdAlign( offset, alignment );
    u64 newOffset = alignedOffset + bytes;

    HT_ASSERT( newOffset <= reserved );

    if( newOffset > committed )
    {
        u64 newCommitted = ( ( newOffset + PAGE_SIZE - 1 ) / PAGE_SIZE ) * PAGE_SIZE;
        if( newCommitted > reserved ) newCommitted = reserved;
        HT_ASSERT( mprotect( base + committed, newCommitted - committed, PROT_READ | PROT_WRITE ) == 0 );
        committed = newCommitted;
    }

    void* alignedAddr = ( void* ) ( base + alignedOffset );

    offset = newOffset;
    return alignedAddr;
}
// ===============================================================================================================
