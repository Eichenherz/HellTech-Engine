
#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>

#include "win32_err.h"

#include "ht_mem_arena.h"
// ===============================================================================================================
// ht_mem_arena.h
// ===============================================================================================================

void*	ht_os_virtual_reserve( u64 sizeInBytes )
{
    void* mem = VirtualAlloc( nullptr, sizeInBytes, MEM_RESERVE, PAGE_READWRITE );
    WIN_CHECK( mem );
    return mem;
}
void	ht_os_virtual_release( void* mem ) { WIN_CHECK( VirtualFree( mem, 0, MEM_RELEASE ) ); }
void*	ht_os_virtual_commit( void* mem, u64 sizeInBytes )
{
    u64 alignedSize = ( ( sizeInBytes + OS_PAGE_SIZE_IN_BYTES - 1 ) / OS_PAGE_SIZE_IN_BYTES ) * OS_PAGE_SIZE_IN_BYTES;

    void* newBase = VirtualAlloc( mem, alignedSize, MEM_COMMIT, PAGE_READWRITE );
    WIN_CHECK( newBase );
    return newBase;
}
void	ht_os_virtual_decommit( void* mem, u64 sizeInBytes )
{
    WIN_CHECK( VirtualFree( mem, sizeInBytes, MEM_DECOMMIT ) );
}

void*   ht_os_virtual_alloc( u64 sizeInBytes )
{
    void* mem = VirtualAlloc( nullptr, sizeInBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
    WIN_CHECK( mem );
    return mem;
}

// ===============================================================================================================