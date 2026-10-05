#include <ht_core_types.h>

#include "ht_memory.h"
#include "ht_math.h"

#include <ranges>
#include <System/sys_sync.h>

//============================GLOBALS=============================//
static ht_virtual_allocator     g_htVirtualAllocator    = {};
ht_virtual_allocator*           g_pVirtualAllocator     = nullptr;
//================================================================//

//============================CONSTS==============================//
constexpr u64 INVALID_RUN_MASK  = 0;
//================================================================//

template <typename F>
concept LambdaCasBreak_T = requires( F Lmbd, u64 val ) { { Lmbd( val ) } -> std::same_as<u64>; };

u64 HtCASLoopReserve( atomic_u64* pAddr, LambdaCasBreak_T auto&& PfnCasReserveMask )
{
    u64 originalVal = SysAtomicRead64<sys_fence_t::NONE>( pAddr );
    for( ;; )
    {
        // NOTE: this mask will tell us the start len in a bin
        const u64 reserveMask = PfnCasReserveMask( originalVal );
        if( INVALID_RUN_MASK == reserveMask ) return INVALID_RUN_MASK;

        u64 newVal = originalVal ^ reserveMask;
        const u64 seenVal = SysAtomicCas64<sys_fence_t::ACQ>( pAddr, newVal, originalVal );
        if( seenVal == originalVal ) return reserveMask;

        originalVal = seenVal;
    }
}

inline atomic_u64* GetBinAt( std::span<ht_virtual_chunk> chunkMap, u64 binIdx )
{
    HT_ASSERT( binIdx < ( std::size( chunkMap ) * BINS_PER_CHUNK ) );

    u64 whichChunk  = binIdx / BINS_PER_CHUNK;
    u64 whichBin    = binIdx % BINS_PER_CHUNK;
    return &chunkMap[ whichChunk ].blockBitmap[ whichBin ];
}

ht_virt_alloc ht_virtual_allocator::AllocVirtualBlock( u64 requestSzInBytes, u64 threadIdx )
{
    // NOTE: virtual alloc can only serve BLOCK_SZ_IN_BYTES <=
    HT_ASSERT( BLOCK_SZ_IN_BYTES <= requestSzInBytes );

    [[ unlikely ]]
    if( ( BLOCKS_PER_BIN * BLOCK_SZ_IN_BYTES ) < requestSzInBytes )
    {
        u64 allocSize = FwdAlignPot( requestSzInBytes, HT_INTERNAL_ALIGNMENT );
        u64 absOffset = SysAtomicAdd64<sys_fence_t::REL>( &dedicatedAllocOffset, allocSize );
        SysAtomicAdd64<sys_fence_t::NONE>( &committedInBytes, allocSize );
        return { ( u8* ) ht_os_virtual_commit( pMemBase + absOffset, allocSize ), allocSize };
    }

    u64 footprintInBlocks = ( requestSzInBytes + BLOCK_SZ_IN_BYTES - 1 ) / BLOCK_SZ_IN_BYTES;
    HT_ASSERT( footprintInBlocks <= BLOCKS_PER_BIN );

    auto LmbdGetReservedMaskCas = [ footprintInBlocks ]( u64 bin ) HT_LAMBDA_FORCEINLINE -> u64
    {
        // NOTE: Find...RunMask64 funcs work with our inverse conventions, ie 1 is taken 0 is free
        return FindNBitRunMask64( ~bin, footprintInBlocks );
    };

    // NOTE: we begin at a threadIdx offset to avoid some contention
    //     t0     t1
    // [ chk0 ][ chk1 ]
    u64 binCount = std::size( chunkMap ) * BINS_PER_CHUNK;
    u64 threadOffset = threadIdx * BINS_PER_CHUNK;
    for( u64 binIdx = 0; binIdx < binCount; binIdx++ )
    {
        u64 threadBinIdx = ( binIdx + threadOffset ) % binCount;

        u64 blockRunWithinBinMask = HtCASLoopReserve( GetBinAt( chunkMap, threadBinIdx ), LmbdGetReservedMaskCas );
        if( INVALID_RUN_MASK != blockRunWithinBinMask )
        {
            u64     blockIdxWithinBin   = std::countr_zero( blockRunWithinBinMask );
            u64     offInBlocks         = threadBinIdx * BLOCKS_PER_BIN + blockIdxWithinBin;
            u64     szInBytes           = footprintInBlocks * BLOCK_SZ_IN_BYTES;
            void*   pRaw                = ht_os_virtual_commit( pMemBase + BLOCK_SZ_IN_BYTES * offInBlocks, szInBytes );
            SysAtomicAdd64<sys_fence_t::NONE>( &committedInBytes, szInBytes );
            return { ( u8* ) pRaw, szInBytes };
        }
    }

    return INVALID_HALLOC;
}

void ht_virtual_allocator::FreeVirtualBlock( ht_virt_alloc alloc )
{
    if( ht::all( std::bit_cast<u32x4>( INVALID_HALLOC ) == std::bit_cast<u32x4>( alloc ) ) ) return;

    u8*     pAlloc          = std::data( alloc );
    u64     allocSzBytes    = std::size( alloc );
    bool    isDedicated     = ( pAlloc - pMemBase ) >= CHUNK_REGION_CAP_IN_BYTES;
    [[ unlikely ]]
    if( isDedicated )
    {
        SysAtomicAdd64<sys_fence_t::NONE>( &committedInBytes, -( i64 ) allocSzBytes );
        return ht_os_virtual_decommit( std::data( alloc ), allocSzBytes );
    }

    HT_ASSERT( IsMultipleOfPow2( allocSzBytes, BLOCK_SZ_IN_BYTES ) );
    u64 blockCount  = allocSzBytes / BLOCK_SZ_IN_BYTES;
    // NOTE: we need to decommit the block THEN free the bin bit ( we still have exclusive write grants on it )
    // otherwise we can race with another thread that basically reallocs the same block and free that
    ht_os_virtual_decommit( pAlloc, blockCount * BLOCK_SZ_IN_BYTES );
    // NOTE: decrement here to avoid losing this state and having alloc count the commit again
    SysAtomicAdd64<sys_fence_t::NONE>( &committedInBytes, -( i64 ) ( blockCount * BLOCK_SZ_IN_BYTES ) );

    u64 offInBlocks = ( pAlloc - pMemBase ) / BLOCK_SZ_IN_BYTES;
    u64 binIdx      = offInBlocks / BLOCKS_PER_BIN;
    u64 bitIdx      = offInBlocks % BLOCKS_PER_BIN;

    HT_ASSERT( blockCount && ( ( blockCount + bitIdx ) <= 64 ) );
    const u64 binCommittedBlocksMask = ( BIT_NPOS >> ( 64 - blockCount ) ) << bitIdx;

    // NOTE: we got exclusive ownership of the bit so we don't need to CAS, we just need to push the change
    SysAtomicAnd64<sys_fence_t::REL>( GetBinAt( chunkMap, binIdx ), ~binCommittedBlocksMask );
}

ht_virtual_allocator HtMakeVirtualAllocator()
{
    u64     chunkMapSzInBytes   = FwdAlignPot( CHUNK_REGION_ELEM_COUNT * sizeof( ht_virtual_chunk ),
        OS_COMMIT_PAGE_SIZE_IN_BYTES );
    u64     reservedInBytes      = CHUNK_REGION_CAP_IN_BYTES + DEDICATED_REGION_CAP_IN_BYTES;
    void*   pMemBase             = ht_os_virtual_reserve( reservedInBytes );
    HT_ASSERT( FwdAlignPot( ( u64 ) pMemBase, HT_INTERNAL_ALIGNMENT ) == ( u64 ) pMemBase );

    return {
        .pMemBase           = ( u8* ) pMemBase,
        .reservedInBytes    = reservedInBytes,
        .chunkMap           = { ( ht_virtual_chunk* ) ht_os_virtual_alloc( chunkMapSzInBytes ),
                            CHUNK_REGION_ELEM_COUNT }
    };
}

void HtInitMemorySystem()
{
    static bool notInit = true;
    HT_ASSERT( ( nullptr == g_pVirtualAllocator ) && notInit ); // TODO: this assert must always be there

    g_htVirtualAllocator = HtMakeVirtualAllocator();
    g_pVirtualAllocator = &g_htVirtualAllocator;
    notInit = false;
}
