
#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>

#include "win32_err.h"

#include "ht_mem_arena.h"
// ===============================================================================================================
// ht_mem_arena.h
// ===============================================================================================================
virtual_arena::virtual_arena( u64 reservedBytesCount ) : reserved{ reservedBytesCount }
{
    mem = ( u8* ) VirtualAlloc( nullptr, reserved, MEM_RESERVE, PAGE_READWRITE );
	WIN_CHECK( mem );
}

void VirtualArenaFree( virtual_arena& arena )
{
    WIN_CHECK( VirtualFree( arena.mem, 0, MEM_RELEASE ) );
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
		WIN_CHECK( VirtualFree( mem, committed, MEM_DECOMMIT ) );
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
		WIN_CHECK( VirtualAlloc( mem + committed, newCommitted - committed, MEM_COMMIT, PAGE_READWRITE ) );
        committed = newCommitted;
    }

    void* alignedAddr = ( void* ) ( mem + alignedOffset );

    offset = newOffset;
    return alignedAddr;
}
// ===============================================================================================================