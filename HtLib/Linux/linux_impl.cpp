#include <sys/mman.h>
#include <cstring>

#include "HtLib/ht_mem_arena.h"
// ===============================================================================================================
// ht_mem_arena.h
// ===============================================================================================================
virtual_arena::virtual_arena( u64 reservedBytesCount ) : reserved{ reservedBytesCount }
{
    mem = ( u8* ) mmap( nullptr, reserved, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0 );
    HT_ASSERT( mem != MAP_FAILED );
}

void VirtualArenaFree( virtual_arena& arena )
{
    HT_ASSERT( 0 == munmap( mem, reserved ) );
    arena = {};
}

void virtual_arena::Rewind( u64 mark )
{
    HT_ASSERT( mark <= offset && "rewind past current offset" );
#ifdef _DEBUG
    std::memset( mem + mark, 0xDE, offset - mark );
#endif
    offset = mark;
}

void virtual_arena::Reset()
{
    if( committed )
    {
    #ifdef _DEBUG
        std::memset( mem, 0xDE, committed );
    #endif
        HT_ASSERT( madvise( mem, committed, MADV_DONTNEED ) == 0 );
        HT_ASSERT( mprotect( mem, committed, PROT_NONE ) == 0 );
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
        HT_ASSERT( mprotect( mem + committed, newCommitted - committed, PROT_READ | PROT_WRITE ) == 0 );
        committed = newCommitted;
    }

    void* alignedAddr = ( void* ) ( mem + alignedOffset );

    offset = newOffset;
    return alignedAddr;
}
// ===============================================================================================================
