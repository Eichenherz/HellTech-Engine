#include <ht_core_types.h>

#include "ht_memory.h"
#include "ht_math.h"

#include <ranges>
#include <System/sys_sync.h>

//============================GLOBALS=============================//
static ht_virtual_allocator         g_htVirtualAllocator    = {};
static std::span<ht_thread_heap>    g_htThreadHeaps         = {};
//================================================================//

//============================CONSTS==============================//
constexpr u64 LOCKED_BIN        = ~0ull; // NOTE: in order to not use another value we alias full-bin as "locked"
constexpr u64 FREE_AS_A_BIRD    = 0;
constexpr u64 INVALID_RUN_MASK  = 0;
//================================================================//

template <typename F>
concept LambdaCasBreak_T = requires( F Lmbd, u64 val ) {
    { Lmbd( val ) } -> std::same_as<u64>;
};

template<LambdaCasBreak_T Lmbd>
u64 HtCASLoopReserve( atomic_u64* pAddr, Lmbd&& CasReserveMask )
{
    u64 originalVal = SysAtomicRead64<sys_fence_t::NONE>( pAddr );
    for( ;; )
    {
        // NOTE: We intentionally skip this state. If we get it means a free decommit just happened
        // and we missed our chance to commit it. Will move to the next bin
        if( FREE_AS_A_BIRD == originalVal ) return INVALID_RUN_MASK;

        // NOTE: this mask will tell us the start len in a bin
        const u64 reserveMask = CasReserveMask( originalVal );
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
    // NOTE: we only allow [1, 4] contiguous blocks to be a single allocation from the chunkMap;
    // otherwise we're taking the dedicated path
    if( ( MAX_BIN_ALLOC_SZ_IN_BLOCKS * BLOCK_SZ_IN_BYTES ) < requestSzInBytes )
    {
        u64 allocSize = FwdAlignPot( requestSzInBytes, HT_INTERNAL_ALIGNMENT );
        u64 szInPages = allocSize / HT_INTERNAL_ALIGNMENT;
        return { ht_os_virtual_alloc( allocSize ), szInPages, ht_virt_alloc_type::DEDICATED };
    }

    u64 footprintInBlocks = ( requestSzInBytes + BLOCK_SZ_IN_BYTES - 1 ) / BLOCK_SZ_IN_BYTES;
    HT_ASSERT( footprintInBlocks <= MAX_BIN_ALLOC_SZ_IN_BLOCKS );

    auto LmbdGetReservedMaskCas = [ footprintInBlocks ]( u64 bin )
    {
        // NOTE: FindSmall14RunMask64 works with our inverse conventions, ie 1 is taken 0 is free
        return FindSmall14RunMask64( ~bin, footprintInBlocks );
    };

    // NOTE: we begin at a threadIdx offset to avoid some contention
    //     t0     t1
    // [ chk0 ][ chk1 ]
    u64 binCount = std::size( chunkMap ) * BINS_PER_CHUNK;
    u64 threadOffset = threadIdx * BINS_PER_CHUNK;
    for( u64 binIdx = 0; binIdx < binCount; binIdx++ )
    {
        u64 threadBinIdx = ( binIdx + threadOffset ) % binCount;

        atomic_u64* pBin = GetBinAt( chunkMap, threadBinIdx );
        // NOTE: commit the full chunk when we touch the head of a new block to amortize OS calls
        // NOTE: bc other threads can alloc and use the memory we must "lock" before os_commit
        // if another thread reaches this point then whoever wins the cas commits, and we continue trying to alloc.
        // If we are the winner of CAS we publish OUR claim and exit.
        // NOTE: this is not mandatory as long as only the owner thread can free ( or it was given ownership on the alloc )
        if( ( FREE_AS_A_BIRD == SysAtomicRead64<sys_fence_t::NONE>( pBin ) ) &&
            ( FREE_AS_A_BIRD == SysAtomicCas64<sys_fence_t::ACQ>( pBin, LOCKED_BIN, FREE_AS_A_BIRD ) ) )
        {
            void* ptr = ht_os_virtual_commit( ( u8* ) pMemBase + threadBinIdx * BIN_SZ_IN_BYTES, BIN_SZ_IN_BYTES );
            SysAtomicWrite64<sys_fence_t::REL>( pBin, BIT_NPOS >> ( 64 - footprintInBlocks ) );
            return { ptr, footprintInBlocks, ht_virt_alloc_type::BLOCK };
        }

        u64 blockStartAndLenWithinBinMask = HtCASLoopReserve( pBin, LmbdGetReservedMaskCas );
        if( INVALID_RUN_MASK != blockStartAndLenWithinBinMask )
        {
            u64     blockIdxWithinBin   = std::countr_zero( blockStartAndLenWithinBinMask );
            u64     offInBlocks         = threadBinIdx * BLOCKS_PER_BIN + blockIdxWithinBin;
            void*   pRaw                = ( u8* ) pMemBase + BLOCK_SZ_IN_BYTES * offInBlocks;

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
    u64 offInBlocks = ( ( u8* ) pAlloc - ( u8* ) pMemBase ) / BLOCK_SZ_IN_BYTES;
    u64 binIdx      = offInBlocks / BLOCKS_PER_BIN;
    u64 bitIdx      = offInBlocks % BLOCKS_PER_BIN;

    HT_ASSERT( ( 0 != blockCount ) && ( ( blockCount + bitIdx ) <= 64 ) );
    const u64 binCommittedBlocksMask = ( BIT_NPOS >> ( 64 - blockCount ) ) << bitIdx;

    atomic_u64* pBin            = GetBinAt( chunkMap, binIdx );
    // NOTE: we got exclusive ownership of the bit so we don't need to CAS, we just need to push the change
    u64 prevBinState = SysAtomicAnd64<sys_fence_t::REL>( pBin, ~binCommittedBlocksMask );

    // NOTE: We commit a full bin of virtual blocks when allocating form a full free bin the first time. We want to
    // decommit symmetrically, ie when all is free. We proceed by checking IF we basically cleared the bin. If we did,
    // we try to CAS claim the block and decommit it then mark the bin as free again before returning.
    // Else other thread got to claim some slots so exit.
    if( ( FREE_AS_A_BIRD == ( prevBinState & ~binCommittedBlocksMask ) ) && ( FREE_AS_A_BIRD ==
            SysAtomicCas64<sys_fence_t::ACQ>( pBin, LOCKED_BIN, FREE_AS_A_BIRD ) ) )
    {
        ht_os_virtual_decommit( ( u8* ) pMemBase + binIdx * BIN_SZ_IN_BYTES, BIN_SZ_IN_BYTES );
        SysAtomicWrite64<sys_fence_t::REL>( pBin, FREE_AS_A_BIRD );
    }
}

ht_virtual_allocator HtMakeVirtualAllocator( u64 maxMemInBytes )
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


u64 ht_thread_heap::OwningThreadIdx() const { return this - std::data( g_htThreadHeaps ); }

std::span<u8> ht_thread_heap::Allocate( u64 szInBytes )
{
    if( szInBytes < HT_THEAP_MIN_ALLOC_SZ_IN_BYTES )
    {
        HT_ASSERT( false && "Unimplemented path !");
        //ht_mip_allocator* pMipZone = std::ranges::find_if( mipZoneList, [ szInBytes ]( auto& alloc )
        //{
        //    return alloc.HasSpaceForAlloc( szInBytes );
        //} );
//
        //if( std::end( mipZoneList ) == pMipZone )
        //{
        //    // NOTE: this alloc will be kept in the mip allocator and freed WHEN IT is freed,
        //    // else we'd have dup keys of the first mip alloc
        //    ht_virt_alloc hZoneAlloc = g_htVirtualAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, OwningThreadIdx() );
        //    HT_ASSERT( hZoneAlloc );
        //    pMipZone = &mipZoneList.push_back( { hZoneAlloc, std::size( mipZoneList ) } );
        //}
//
        //ht_zone_alloc hAlloc = pMipZone->AllocNode( szInBytes );
        //HT_ASSERT( hAlloc );
        //allocMap.insert( std::bit_cast<ht_halloc>( hAlloc ) );
//
        //return { ( u8* ) HtGetAllocPtr( hAlloc ), HtGetAllocSize( hAlloc ) };
    }
    // TODO: do we serve exactly the request or round to nearest pow2 ?
    u64 dedicatedTheapAllocSzInBytes = std::max( szInBytes, BLOCK_SZ_IN_BYTES );

    ht_virt_alloc hAlloc = g_htVirtualAllocator.AllocVirtualBlock( dedicatedTheapAllocSzInBytes, OwningThreadIdx() );
    HT_ASSERT( hAlloc );
    allocMap.insert( std::bit_cast<ht_halloc>( hAlloc ) );

    return { ( u8* ) HtGetAllocPtr( hAlloc ), dedicatedTheapAllocSzInBytes };
}

void ht_thread_heap::Free( void* ptr )
{
    const ht_halloc* pHandle = allocMap.find( ptr );
    if( std::end( allocMap ) == pHandle ) return;

    ht_halloc hAlloc = *pHandle;
    allocMap.erase( pHandle );

    u64 selfType = std::bit_cast<u64>( hAlloc ) >> 63;
    if( ht_halloc_self_type::VIRTUAL == selfType )
    {
       return g_htVirtualAllocator.FreeVirtualBlock( std::bit_cast<ht_virt_alloc>( hAlloc ), OwningThreadIdx() );
    }

    if( ht_halloc_self_type::MIP_ZONE == selfType )
    {
        HT_ASSERT( false && "Unimplemented path !");
        //ht_zone_alloc hZoneAlloc = std::bit_cast<ht_zone_alloc>( hAlloc );
        //ht_mip_allocator& mipZone = mipZoneList[ hZoneAlloc.zoneIdx ];
        //mipZone.FreeNode( hZoneAlloc );
        //if( mipZone.IsEmpty() ) g_htVirtualAllocator.FreeVirtualBlock( mipZone.hVirtAlloc, OwningThreadIdx() );
        //mipZone = {};
        //return;
    }

    HT_ASSERT( false && "Wrong data has infiltrated the allocator !!!!" );
}

inline std::span<ht_thread_heap> HtMakeThreadHeaps( u64 threadCount )
{
    // NOTE: otherwise C++ adds 8 bytes top of our alloc
    static_assert( std::is_trivially_destructible<ht_thread_heap>::value );
    void* mem = ht_os_virtual_alloc( sizeof( ht_thread_heap ) * threadCount );
    return { new ( mem ) ht_thread_heap[ threadCount ], threadCount };
}


void HtMakeAllocator( u64 maxMemSz, u64 threadCount )
{
    g_htVirtualAllocator    = HtMakeVirtualAllocator( maxMemSz );
    g_htThreadHeaps         = HtMakeThreadHeaps( threadCount );
}

ht_thread_heap *const GetThreadHeap( u64 threadIdx ) { return &g_htThreadHeaps[ threadIdx ]; }
