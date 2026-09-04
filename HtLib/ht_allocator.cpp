#include <ht_core_types.h>

#include "ht_memory.h"
#include "ht_math.h"

#include <ranges>
#include <System/sys_sync.h>

//============================GLOBALS=============================//
static              ht_virtual_allocator    g_htVirtualAllocator    = {};
static thread_local ht_virtual_allocator*   g_pVirtualAllocator     = nullptr;
//================================================================//

//============================CONSTS==============================//
constexpr u64 LOCKED_BIN        = ~0ull; // NOTE: in order to not use another value we alias full-bin as "locked"
constexpr u64 FREE_AS_A_BIRD    = 0;
constexpr u64 INVALID_RUN_MASK  = 0;
//================================================================//

template <typename F>
concept LambdaCasBreak_T = requires( F Lmbd, u64 val ) { { Lmbd( val ) } -> std::same_as<u64>; };

template<LambdaCasBreak_T Lmbd>
u64 HtCASLoopReserve( atomic_u64* pAddr, Lmbd&& CasReserveMask )
{
    u64 originalVal = SysAtomicRead64<sys_fence_t::NONE>( pAddr );
    for( ;; )
    {
        // NOTE: We intentionally skip this state. If we get it means a free decommit
        // just happened and we missed our chance to commit it. Will move to the next bin.
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
    if( ( MAX_BIN_ALLOC_SZ_IN_BLOCKS * BLOCK_SZ_IN_BYTES ) < requestSzInBytes )
    {
        u64 allocSize = FwdAlignPot( requestSzInBytes, HT_INTERNAL_ALIGNMENT );
        u64 absOffset = SysAtomicAdd64<sys_fence_t::REL>( &dedicatedAllocOffset, allocSize );
        SysAtomicAdd64<sys_fence_t::NONE>( &committedInBytes, allocSize );
        return { ( u8* ) ht_os_virtual_commit( pMemBase + absOffset, allocSize ), allocSize };
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
            // NOTE: update before the lock is gone
            SysAtomicAdd64<sys_fence_t::NONE>( &committedInBytes, BIN_SZ_IN_BYTES );
            // NOTE: bc we use a full bin ( 64 slots ) our fresh and recently cleared states
            // are represented by the same value, 0. This forces us to do another OS commit which
            // is cheaper than a full commit but still an OS call. If this proves problematic,
            // one solution is to take the highest bit and set it after fresh so we skip
            // the re-commit if no decommit happened.
            void* ptr = ht_os_virtual_commit( pMemBase + threadBinIdx * BIN_SZ_IN_BYTES, BIN_SZ_IN_BYTES );
            SysAtomicWrite64<sys_fence_t::REL>( pBin, BIT_NPOS >> ( 64 - footprintInBlocks ) );

            return { ( u8* ) ptr, footprintInBlocks * BLOCK_SZ_IN_BYTES };
        }

        u64 blockStartAndLenWithinBinMask = HtCASLoopReserve( pBin, LmbdGetReservedMaskCas );
        if( INVALID_RUN_MASK != blockStartAndLenWithinBinMask )
        {
            u64   blockIdxWithinBin = std::countr_zero( blockStartAndLenWithinBinMask );
            u64   offInBlocks       = threadBinIdx * BLOCKS_PER_BIN + blockIdxWithinBin;
            u8*   pRaw              = pMemBase + BLOCK_SZ_IN_BYTES * offInBlocks;
            return { pRaw, footprintInBlocks * BLOCK_SZ_IN_BYTES };
        }
    }

    return INVALID_HALLOC;
}

void ht_virtual_allocator::FreeVirtualBlock( ht_virt_alloc alloc, u64 threadIdx )
{
    if( std::bit_cast<u32x4>( INVALID_HALLOC ) == std::bit_cast<u32x4>( alloc ) ) return;

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
    u64 offInBlocks = ( pAlloc - pMemBase ) / BLOCK_SZ_IN_BYTES;
    u64 binIdx      = offInBlocks / BLOCKS_PER_BIN;
    u64 bitIdx      = offInBlocks % BLOCKS_PER_BIN;

    HT_ASSERT( ( 0 != blockCount ) && ( ( blockCount + bitIdx ) <= 64 ) );
    const u64 binCommittedBlocksMask = ( BIT_NPOS >> ( 64 - blockCount ) ) << bitIdx;

    atomic_u64* pBin = GetBinAt( chunkMap, binIdx );
    // NOTE: we got exclusive ownership of the bit so we don't need to CAS, we just need to push the change
    u64 prevBinState = SysAtomicAnd64<sys_fence_t::REL>( pBin, ~binCommittedBlocksMask );

    // NOTE: We commit a full bin of virtual blocks when allocating form a full free bin the first time. We want to
    // decommit symmetrically, ie when all is free. We proceed by checking IF we basically cleared the bin. If we did,
    // we try to CAS claim the block and decommit it then mark the bin as free again before returning.
    // Else other thread got to claim some slots so exit.
    if( FREE_AS_A_BIRD != ( prevBinState & ~binCommittedBlocksMask ) ) return;

    // NOTE: decrement here to avoid losing this state and having alloc count the commit again
    SysAtomicAdd64<sys_fence_t::NONE>( &committedInBytes, -( i64 ) BIN_SZ_IN_BYTES );
    if( FREE_AS_A_BIRD == SysAtomicCas64<sys_fence_t::ACQ>( pBin, LOCKED_BIN, FREE_AS_A_BIRD ) )
    {
        ht_os_virtual_decommit( ( u8* ) pMemBase + binIdx * BIN_SZ_IN_BYTES, BIN_SZ_IN_BYTES );
        SysAtomicWrite64<sys_fence_t::REL>( pBin, FREE_AS_A_BIRD );
    }
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