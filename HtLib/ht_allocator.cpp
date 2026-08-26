#include "ht_memory.h"

#include <System/sys_sync.h>

template <typename F>
concept LambdaCasBreak_T = requires( F Lmbd, u64 val ) {
    { Lmbd( val ) } -> std::same_as<u64>;
};

template<LambdaCasBreak_T Lmbd>
u64 HtCASLoopReserve( volatile u64* pAddr, Lmbd&& CasReserveMask, const u64 invalidMask )
{
    u64 originalVal = *pAddr;
    for( ;; )
    {
        // NOTE: this mask will tell us the start len in a bin
        const u64 reserveMask = CasReserveMask( originalVal );
        if( invalidMask == reserveMask ) return invalidMask;

        u64 newVal = originalVal ^ reserveMask;
        const u64 seenVal = SysAtomicCas64<sys_split_barrier_t::ACQUIRE>( pAddr, newVal, originalVal );
        if( seenVal == originalVal ) return reserveMask;

        originalVal = seenVal;
    }
}

inline volatile u64* GetBinAt( std::span<ht_virtual_chunk> chunkMap, u64 binIdx )
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
        u64 allocSize = FwdAlignPot( requestSzInBytes, HT_DEDICATED_ALIGNMENT );
        u64 szInPages = allocSize / HT_DEDICATED_ALIGNMENT;
        return { ht_os_virtual_alloc( allocSize ), szInPages, ht_virt_alloc_type::DEDICATED };
    }

    u64 footprintInBlocks = ( requestSzInBytes + BLOCK_SZ_IN_BYTES - 1 ) / BLOCK_SZ_IN_BYTES;
    HT_ASSERT( footprintInBlocks <= 64 );

    auto LmbdGetReservedMaskCas = [ footprintInBlocks ]( u64 bin )
    {
        // NOTE: FindNBitsFreeRunStartBitIdx works with our inverse conventions, but it's easier to write
        u64 bitIdx = FindNBitsFreeRunStartBitIdx( ~bin, footprintInBlocks );
        return ( BIT_NPOS != bitIdx ) ? ( ( BIT_NPOS >> ( 64 - footprintInBlocks ) ) << bitIdx ) : 0;
    };

    // NOTE: we begin at a threadIdx offset to avoid some contention
    //     t0     t1      |      t0       t1
    // [ bin0 ][ bin1 ]   |   [ bin1 ][ bin2 ]
    u64 binCount = std::size( chunkMap ) * BINS_PER_CHUNK;
    u64 threadOffset = threadIdx * BINS_PER_CHUNK;
    for( u64 binIdx = 0; binIdx < binCount; binIdx++ )
    {
        u64 threadBinIdx = ( binIdx + threadOffset ) % binCount;
        u64 blockStartAndLenWithinBinMask = HtCASLoopReserve( GetBinAt( chunkMap, threadBinIdx ),
            LmbdGetReservedMaskCas, 0 );
        if( 0 != blockStartAndLenWithinBinMask )
        {
            u64     blockIdxWithinBin = FirstSetBitIdx64( blockStartAndLenWithinBinMask );
            HT_ASSERT( BIT_NPOS != blockIdxWithinBin );

            u64     offInBlocks = threadBinIdx * BLOCKS_PER_BIN + blockIdxWithinBin;
            u64     szInBytes   = footprintInBlocks * BLOCK_SZ_IN_BYTES;
            void*   pRaw        = ht_os_virtual_commit( ( u8* ) pMemBase + BLOCK_SZ_IN_BYTES * offInBlocks, szInBytes );
            return { pRaw, footprintInBlocks, ht_virt_alloc_type::BLOCK };
        }
    }

    return {};
}

void ht_virtual_allocator::FreeVirtualBlock( ht_virt_alloc alloc, u64 threadIdx )
{
    if( ht_virt_alloc{} == alloc ) return;

    void* pAlloc = HtGetAllocPtr( alloc );
    [[ unlikely ]]
    if( ht_virt_alloc_type::DEDICATED == alloc.type )
    {
        return ht_os_virtual_release( pAlloc );
    }
    u64 blockCount = alloc.metadata;
    // NOTE: we need to decommit the block THEN free the bin bit ( we still have exclusive write grants on it )
    // otherwise we can race with another thread that basically reallocs the same block and free that
    ht_os_virtual_decommit( pAlloc, blockCount * BLOCK_SZ_IN_BYTES );

    u64 offInBlocks = ( ( u8* ) pAlloc - ( u8* ) pMemBase ) / BLOCK_SZ_IN_BYTES;
    u64 binIdx      = offInBlocks / BLOCKS_PER_BIN;
    u64 bitIdx      = offInBlocks % BLOCKS_PER_BIN;

    HT_ASSERT( ( 0 != blockCount ) && ( ( blockCount + bitIdx ) <= 64 ) );
    u64 binCommitedBlocksMask = ( BIT_NPOS >> ( 64 - blockCount ) ) << bitIdx;
    // NOTE: we got exclusive ownership so we don't need to CAS, we just need to push the change
    SysAtomicAnd64<sys_split_barrier_t::RELEASE>( GetBinAt( chunkMap, binIdx ), ~binCommitedBlocksMask );
}

ht_virtual_allocator HtMakeAllocator( u64 maxMemInBytes )
{
    u64 chunksCount             = ( ( maxMemInBytes + CHUNK_SZ_IN_BYTES - 1 ) / CHUNK_SZ_IN_BYTES );
    u64 blockRegionSzInBytes    = chunksCount * CHUNK_SZ_IN_BYTES;
    u64 chunkMapSzInBytes       = FwdAlignPot( chunksCount * sizeof( ht_virtual_chunk ), OS_PAGE_SIZE_IN_BYTES );

    void* pMemBase              = ht_os_virtual_reserve( blockRegionSzInBytes );
    HT_ASSERT( FwdAlignPot( ( u64 ) pMemBase, HT_INTERNAL_ALIGNMENT ) == ( u64 ) pMemBase );

    return {
        .pMemBase           = pMemBase,
        .reservedInBytes    = blockRegionSzInBytes,
        .chunkMap           = { ( ht_virtual_chunk* ) ht_os_virtual_alloc( chunkMapSzInBytes ), chunksCount }
    };
}