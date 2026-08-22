// NOTE: covered cases —
//   alloc: basic, zero-byte, sequential, alignment (16/64/256), exact capacity
//   rewind: to mark, to zero, clamp past capacity
//   reset: basic, idempotent, alloc after reset, multi-cycle
//   negative: alloc past capacity, rewind past offset, non-pow2 alignment
//
// NOTE: covered cases — ST_ is single threaded, threadIdx is only the bin scan start offset
//   packing: round trips, top user address, equality, address past the user range
//   chunk map: GetBinAt order + out of range, HtCASLoopReserve flip / read / bail
//   huge list: link, unlink middle, unlink down to the sentinel
//   make: chunk rounding, layout, empty initial state
//   alloc: single, multi, mixed, holes, full bins, thread offset, negatives
//   free: bit clearing, address to bin, reuse, null alloc
//   dedicated: threshold, header layout, side by side with blocks

#include "test_common.h"

#include <ht_memory.h>
// NOTE: included, not linked — GetBinAt and friends are file local. CMake drops the TU.
#include <ht_allocator.cpp>

#include <Windows.h>

// NOTE: longjmp interception globals — extern'd in ht_error.h under HT_TESTS
jmp_buf gHtAssertJmpbuf;
i32     gHtAssertFired = 0;

// ============================================================================
// static_arena
// ============================================================================

static constexpr u64 STATIC_CAP = 256;

MU_TEST( StaticArenaAllocBasic )
{
    static_arena<STATIC_CAP> a = {};
    void* p = a.Alloc( 32, 8 );
    mu_check( nullptr != p );
    mu_check( ( u8* ) p >= a.mem && ( u8* ) p < ( a.mem + STATIC_CAP ) );
    mu_check( 32 == a.offset );
}

MU_TEST( StaticArenaAllocZeroBytes )
{
    static_arena<STATIC_CAP> a = {};
    void* p = a.Alloc( 0, 1 );
    mu_check( nullptr != p );
    mu_check( 0 == a.offset );
}

MU_TEST( StaticArenaAllocAlignment )
{
    static_arena<STATIC_CAP> a = {};
    a.Alloc( 1, 1 );
    void* p = a.Alloc( 8, 64 );
    mu_check( 0 == ( (u64)p & 63 ) );
}

MU_TEST( StaticArenaAllocSequential )
{
    static_arena<STATIC_CAP> a = {};
    void* p1 = a.Alloc( 16, 8 );
    void* p2 = a.Alloc( 16, 8 );
    mu_check( ( ( u8* ) p1 + 16 ) == ( u8* )p2 );
    mu_check( 32 == a.offset );
}

MU_TEST( StaticArenaAllocExactCapacity )
{
    static_arena<STATIC_CAP> a = {};
    void* p = a.Alloc( STATIC_CAP, 1 );
    mu_check( nullptr != p );
    mu_check( STATIC_CAP == a.offset );
}

MU_TEST( StaticArenaRewind )
{
    static_arena<STATIC_CAP> a = {};
    a.Alloc( 64, 8 );
    u64 mark = a.offset;
    a.Alloc( 64, 8 );
    a.Rewind( mark );
    mu_check( mark == a.offset );
}

MU_TEST( StaticArenaRewindToZero )
{
    static_arena<STATIC_CAP> a = {};
    a.Alloc( 64, 8 );
    a.Rewind( 0 );
    mu_check( 0 == a.offset );
}

MU_TEST( StaticArenaRewindClamp )
{
    // NOTE: mark > SZ_IN_BYTES clamps to SZ_IN_BYTES
    static_arena<64> a = {};
    a.Rewind( 99999 );
    mu_check( 64 == a.offset );
}

MU_TEST( StaticArenaReset )
{
    static_arena<STATIC_CAP> a = {};
    a.Alloc( 128, 8 );
    a.Reset();
    mu_check( 0 == a.offset );
}

MU_TEST( StaticArenaResetThenAlloc )
{
    static_arena<STATIC_CAP> a = {};
    a.Alloc( 128, 8 );
    a.Reset();
    void* p = a.Alloc( 32, 8 );
    mu_check( nullptr != p );
    mu_check( 32 == a.offset );
}

// ============================================================================
// static_arena — negative
// ============================================================================
MU_TEST( StaticArenaAllocPastCapacity )
{
    static_arena<64> a = {};
    MU_ASSERT_FIRES( a.Alloc( 65, 1 ) );
}

MU_TEST( StaticArenaAllocNonPow2Align )
{
    static_arena<STATIC_CAP> a = {};
    MU_ASSERT_FIRES( a.Alloc( 8, 3 ) );
}

MU_TEST_SUITE( SuiteStaticArena )
{
    MU_RUN_TEST( StaticArenaAllocBasic );
    MU_RUN_TEST( StaticArenaAllocZeroBytes );
    MU_RUN_TEST( StaticArenaAllocAlignment );
    MU_RUN_TEST( StaticArenaAllocSequential );
    MU_RUN_TEST( StaticArenaAllocExactCapacity );
    MU_RUN_TEST( StaticArenaRewind );
    MU_RUN_TEST( StaticArenaRewindToZero );
    MU_RUN_TEST( StaticArenaRewindClamp );
    MU_RUN_TEST( StaticArenaReset );
    MU_RUN_TEST( StaticArenaResetThenAlloc );
    MU_RUN_TEST( StaticArenaAllocPastCapacity );
    MU_RUN_TEST( StaticArenaAllocNonPow2Align );
}

// ============================================================================
// dynamic_arena
// ============================================================================

static constexpr u64 DYN_CAP = 4096;
static u8            gDynBuf[ DYN_CAP ];

MU_TEST( DynamicArenaAllocBasic )
{
    dynamic_arena a = { gDynBuf, DYN_CAP };
    void* p = a.Alloc( 32, 8 );
    mu_check( nullptr != p );
    mu_check( (u8*)p >= gDynBuf && (u8*)p < ( gDynBuf + DYN_CAP ) );
    mu_check( 32 == a.offset );
}

MU_TEST( DynamicArenaAllocZeroBytes )
{
    dynamic_arena a = { gDynBuf, DYN_CAP };
    a.Alloc( 0, 1 );
    mu_check( 0 == a.offset );
}

MU_TEST( DynamicArenaAllocAlignment )
{
    dynamic_arena a = { gDynBuf, DYN_CAP };
    a.Alloc( 1, 1 );
    void* p = a.Alloc( 8, 64 );
    mu_check( 0 == ( (u64)p & 63 ) );
}

MU_TEST( DynamicArenaAllocSequential )
{
    dynamic_arena a = { gDynBuf, DYN_CAP };
    void* p1 = a.Alloc( 16, 8 );
    void* p2 = a.Alloc( 16, 8 );
    mu_check( ( (u8*)p1 + 16 ) == (u8*)p2 );
}

MU_TEST( DynamicArenaAllocExactCapacity )
{
    dynamic_arena a = { gDynBuf, DYN_CAP };
    void* p = a.Alloc( DYN_CAP, 1 );
    mu_check( nullptr != p );
    mu_check( DYN_CAP == a.offset );
}

MU_TEST( DynamicArenaRewind )
{
    dynamic_arena a = { gDynBuf, DYN_CAP };
    a.Alloc( 64, 8 );
    u64 mark = a.offset;
    a.Alloc( 128, 8 );
    a.Rewind( mark );
    mu_check( mark == a.offset );
}

MU_TEST( DynamicArenaRewindToZero )
{
    dynamic_arena a = { gDynBuf, DYN_CAP };
    a.Alloc( 128, 8 );
    a.Rewind( 0 );
    mu_check( 0 == a.offset );
}

MU_TEST( DynamicArenaRewindClamp )
{
    dynamic_arena a = { gDynBuf, DYN_CAP };
    a.Rewind( 99999 );
    mu_check( DYN_CAP == a.offset );
}

MU_TEST( DynamicArenaReset )
{
    dynamic_arena a = { gDynBuf, DYN_CAP };
    a.Alloc( 256, 8 );
    a.Reset();
    mu_check( 0 == a.offset );
}

MU_TEST( DynamicArenaResetThenAlloc )
{
    dynamic_arena a = { gDynBuf, DYN_CAP };
    a.Alloc( 256, 8 );
    a.Reset();
    void* p = a.Alloc( 32, 8 );
    mu_check( nullptr != p );
    mu_check( 32 == a.offset );
}

// ============================================================================
// dynamic_arena — negative
// ============================================================================
MU_TEST( DynamicArenaAllocPastCapacity )
{
    dynamic_arena a = { gDynBuf, DYN_CAP };
    MU_ASSERT_FIRES( a.Alloc( DYN_CAP + 1, 1 ) );
}

MU_TEST( DynamicArenaAllocNonPow2Align )
{
    dynamic_arena a = { gDynBuf, DYN_CAP };
    MU_ASSERT_FIRES( a.Alloc( 8, 3 ) );
}

MU_TEST_SUITE( SuiteDynamicArena )
{
    MU_RUN_TEST( DynamicArenaAllocBasic );
    MU_RUN_TEST( DynamicArenaAllocZeroBytes );
    MU_RUN_TEST( DynamicArenaAllocAlignment );
    MU_RUN_TEST( DynamicArenaAllocSequential );
    MU_RUN_TEST( DynamicArenaAllocExactCapacity );
    MU_RUN_TEST( DynamicArenaRewind );
    MU_RUN_TEST( DynamicArenaRewindToZero );
    MU_RUN_TEST( DynamicArenaRewindClamp );
    MU_RUN_TEST( DynamicArenaReset );
    MU_RUN_TEST( DynamicArenaResetThenAlloc );
    MU_RUN_TEST( DynamicArenaAllocPastCapacity );
    MU_RUN_TEST( DynamicArenaAllocNonPow2Align );
}

// ============================================================================
// ht_virt_alloc packing
// ============================================================================

static u8 gPackDummy[ 8 ] = {};

// NOTE: gate for every suite below, the packed word cannot hold a fatter address
static bool HtOsUserAddrFitsPackedWord()
{
    SYSTEM_INFO sysInfo = {};
    GetSystemInfo( &sysInfo );

    return ( u64 ) sysInfo.lpMaximumApplicationAddress <= OS_USER_MAX_ADDR;
}

MU_TEST( ST_PackFitsTheOsUserAddressSpace )
{
    SYSTEM_INFO sysInfo = {};
    GetSystemInfo( &sysInfo );

    mu_check( ( u64 ) sysInfo.lpMaximumApplicationAddress <= OS_USER_MAX_ADDR );
    mu_check( OS_PAGE_SIZE_IN_BYTES == sysInfo.dwPageSize );
    mu_check( 0 == ( BLOCK_SZ_IN_BYTES % sysInfo.dwAllocationGranularity ) );
}

MU_TEST( ST_PackBlockRoundTrip )
{
    ht_alloc_metadata meta = { .type = ht_virt_alloc_type::BLOCK, .blockCount = 7 };
    auto[ pPacked, unpacked ] = HtUnpackVirtualAllocation( ht_virt_alloc( gPackDummy, meta ) );

    mu_check( gPackDummy == ( u8* ) pPacked );
    mu_check( ht_virt_alloc_type::BLOCK == unpacked.type );
    mu_check( 7 == unpacked.blockCount );
}

MU_TEST( ST_PackDedicatedRoundTrip )
{
    ht_alloc_metadata meta = { .type = ht_virt_alloc_type::DEDICATED };
    auto[ pPacked, unpacked ] = HtUnpackVirtualAllocation( ht_virt_alloc( gPackDummy, meta ) );

    mu_check( gPackDummy == ( u8* ) pPacked );
    mu_check( ht_virt_alloc_type::DEDICATED == unpacked.type );
    mu_check( 0 == unpacked.blockCount );
}

MU_TEST( ST_PackHoldsTheBiggestBlockCount )
{
    // NOTE: blockCount is 7 bits and a run can be a whole bin wide
    ht_alloc_metadata meta = { .type = ht_virt_alloc_type::BLOCK, .blockCount = ( u16 ) BLOCKS_PER_BIN };
    auto[ pPacked, unpacked ] = HtUnpackVirtualAllocation( ht_virt_alloc( gPackDummy, meta ) );

    mu_check( gPackDummy == ( u8* ) pPacked );
    mu_check( BLOCKS_PER_BIN == unpacked.blockCount );
}

MU_TEST( ST_PackHoldsTheTopUserAddress )
{
    ht_alloc_metadata meta = { .type = ht_virt_alloc_type::BLOCK, .blockCount = 1 };
    auto[ pPacked, unpacked ] = HtUnpackVirtualAllocation( ht_virt_alloc( ( void* ) OS_USER_MAX_ADDR, meta ) );

    mu_check( ( void* ) OS_USER_MAX_ADDR == pPacked );
    mu_check( 1 == unpacked.blockCount );
}

MU_TEST( ST_PackHoldsRealPointers )
{
    ht_alloc_metadata meta = { .type = ht_virt_alloc_type::BLOCK, .blockCount = 1 };
    u64 onStack = 0;
    void* pOsMem = ht_os_virtual_alloc( OS_PAGE_SIZE_IN_BYTES );

    mu_check( ( void* ) &onStack == HtUnpackVirtualAllocation( ht_virt_alloc( &onStack, meta ) ).ptr );
    mu_check( pOsMem == HtUnpackVirtualAllocation( ht_virt_alloc( pOsMem, meta ) ).ptr );

    ht_os_virtual_release( pOsMem );
}

MU_TEST( ST_PackDefaultIsTheFreeSentinel )
{
    // NOTE: FreeVirtualBlock bails out on this
    ht_alloc_metadata meta = { .type = ht_virt_alloc_type::BLOCK, .blockCount = 1 };

    mu_check( ht_virt_alloc{} == ht_virt_alloc{} );
    mu_check( !( ht_virt_alloc( gPackDummy, meta ) == ht_virt_alloc{} ) );
}

MU_TEST( ST_PackEqualityCoversTheMetadata )
{
    ht_alloc_metadata oneBlock = { .type = ht_virt_alloc_type::BLOCK, .blockCount = 1 };
    ht_alloc_metadata twoBlocks = { .type = ht_virt_alloc_type::BLOCK, .blockCount = 2 };

    mu_check( ht_virt_alloc( gPackDummy, oneBlock ) == ht_virt_alloc( gPackDummy, oneBlock ) );
    mu_check( !( ht_virt_alloc( gPackDummy, oneBlock ) == ht_virt_alloc( gPackDummy, twoBlocks ) ) );
}

// ============================================================================
// ht_virt_alloc packing — negative
// ============================================================================
MU_TEST( ST_PackAddressAboveTheUserRangeFires )
{
    ht_alloc_metadata meta = {};
    ht_virt_alloc probe = {};
    MU_ASSERT_FIRES( probe = ht_virt_alloc( ( void* ) ( 1ull << OS_USER_ADDR_BIT_WIDTH ), meta ) );
}

MU_TEST_SUITE( ST_SuiteVirtAllocPacking )
{
    MU_RUN_TEST( ST_PackFitsTheOsUserAddressSpace );
    MU_RUN_TEST( ST_PackBlockRoundTrip );
    MU_RUN_TEST( ST_PackDedicatedRoundTrip );
    MU_RUN_TEST( ST_PackHoldsTheBiggestBlockCount );
    MU_RUN_TEST( ST_PackHoldsTheTopUserAddress );
    MU_RUN_TEST( ST_PackHoldsRealPointers );
    MU_RUN_TEST( ST_PackDefaultIsTheFreeSentinel );
    MU_RUN_TEST( ST_PackEqualityCoversTheMetadata );
    MU_RUN_TEST( ST_PackAddressAboveTheUserRangeFires );
}

// ============================================================================
// chunk map addressing and the CAS reserve loop
// ============================================================================

// NOTE: 2 chunks is the smallest pool where the thread start offset is observable. A reserve is
// address space only, the tests hand every block they commit back.
static ht_virtual_allocator gPool = HtMakeAllocator( 2 * CHUNK_SZ_IN_BYTES );

static void ScrubPool()
{
    u64 binCount = std::size( gPool.chunkMap ) * BINS_PER_CHUNK;
    for( u64 binIdx = 0; binIdx < binCount; ++binIdx )
    {
        *GetBinAt( gPool.chunkMap, binIdx ) = 0;
    }

    gPool.circularList.pNext = &gPool.circularList;
    gPool.circularList.pPrev = &gPool.circularList;
}

MU_TEST( ST_GetBinAtWalksChunksThenBins )
{
    mu_check( &gPool.chunkMap[ 0 ].blockBins[ 0 ] == GetBinAt( gPool.chunkMap, 0 ) );
    mu_check( &gPool.chunkMap[ 0 ].blockBins[ BINS_PER_CHUNK - 1 ] == GetBinAt( gPool.chunkMap, BINS_PER_CHUNK - 1 ) );
    mu_check( &gPool.chunkMap[ 1 ].blockBins[ 0 ] == GetBinAt( gPool.chunkMap, BINS_PER_CHUNK ) );
    mu_check( &gPool.chunkMap[ 1 ].blockBins[ 1 ] == GetBinAt( gPool.chunkMap, BINS_PER_CHUNK + 1 ) );
}

MU_TEST( ST_GetBinAtPastTheMapFires )
{
    volatile u64* pBin = nullptr;
    MU_ASSERT_FIRES( pBin = GetBinAt( gPool.chunkMap, std::size( gPool.chunkMap ) * BINS_PER_CHUNK ) );
}

MU_TEST( ST_CasLoopReserveFlipsTheMaskIn )
{
    atomic_u64 bin = 0b0011ull;
    u64 reserved = HtCASLoopReserve( &bin, []( u64 ) { return 0b1100ull; }, 0 );

    mu_check( 0b1100ull == reserved );
    mu_check( 0b1111ull == bin );
}

MU_TEST( ST_CasLoopReserveFeedsTheLiveBinToTheLambda )
{
    atomic_u64 bin = 0b0110ull;
    u64 seenByLambda = 0;
    u64 reserved = HtCASLoopReserve( &bin, [ &seenByLambda ]( u64 binVal ) { seenByLambda = binVal; return 0b1ull; }, 0 );

    mu_check( 0b0110ull == seenByLambda );
    mu_check( 0b1ull == reserved );
    mu_check( 0b0111ull == bin );
}

MU_TEST( ST_CasLoopReserveBailsOnTheInvalidMask )
{
    atomic_u64 bin = 0b1010ull;
    u64 reserved = HtCASLoopReserve( &bin, []( u64 ) { return 0ull; }, 0 );

    mu_check( 0 == reserved );
    mu_check( 0b1010ull == bin );
}

MU_TEST_SUITE( ST_SuiteChunkMap )
{
    MU_RUN_TEST( ST_GetBinAtWalksChunksThenBins );
    MU_RUN_TEST( ST_GetBinAtPastTheMapFires );
    MU_RUN_TEST( ST_CasLoopReserveFlipsTheMaskIn );
    MU_RUN_TEST( ST_CasLoopReserveFeedsTheLiveBinToTheLambda );
    MU_RUN_TEST( ST_CasLoopReserveBailsOnTheInvalidMask );
}

// ============================================================================
// huge alloc list
// ============================================================================

MU_TEST( ST_HugeListLinkPushesInFront )
{
    copyable_srwlock lock = {};
    ht_huge_alloc sentinel = { .pNext = &sentinel, .pPrev = &sentinel };
    ht_huge_alloc first = {};
    ht_huge_alloc second = {};

    HtHugeAllocLinkWithLock( lock, &sentinel, &first, 128 );
    HtHugeAllocLinkWithLock( lock, &sentinel, &second, 256 );

    mu_check( &second == sentinel.pNext );
    mu_check( &first == second.pNext );
    mu_check( &sentinel == first.pNext );

    mu_check( &first == sentinel.pPrev );
    mu_check( &second == first.pPrev );
    mu_check( &sentinel == second.pPrev );

    mu_check( 128 == first.szInBytes );
    mu_check( 256 == second.szInBytes );
}

MU_TEST( ST_HugeListUnlinkTakesTheMiddleOut )
{
    copyable_srwlock lock = {};
    ht_huge_alloc sentinel = { .pNext = &sentinel, .pPrev = &sentinel };
    ht_huge_alloc first = {};
    ht_huge_alloc second = {};
    ht_huge_alloc third = {};

    HtHugeAllocLinkWithLock( lock, &sentinel, &first, 128 );
    HtHugeAllocLinkWithLock( lock, &sentinel, &second, 256 );
    HtHugeAllocLinkWithLock( lock, &sentinel, &third, 512 );

    mu_check( 256 == HtHugeAllocUnlinkWithLock( lock, &second ) );
    mu_check( &third == sentinel.pNext );
    mu_check( &first == third.pNext );
    mu_check( &third == first.pPrev );
    mu_check( &sentinel == first.pNext );
    mu_check( &first == sentinel.pPrev );
}

MU_TEST( ST_HugeListUnlinkDownToTheSentinel )
{
    copyable_srwlock lock = {};
    ht_huge_alloc sentinel = { .pNext = &sentinel, .pPrev = &sentinel };
    ht_huge_alloc first = {};
    ht_huge_alloc second = {};

    HtHugeAllocLinkWithLock( lock, &sentinel, &first, 128 );
    HtHugeAllocLinkWithLock( lock, &sentinel, &second, 256 );

    // NOTE: head first, then the last one standing
    mu_check( 256 == HtHugeAllocUnlinkWithLock( lock, &second ) );
    mu_check( &first == sentinel.pNext );
    mu_check( &first == sentinel.pPrev );

    mu_check( 128 == HtHugeAllocUnlinkWithLock( lock, &first ) );
    mu_check( &sentinel == sentinel.pNext );
    mu_check( &sentinel == sentinel.pPrev );
}

MU_TEST_SUITE( ST_SuiteHugeList )
{
    MU_RUN_TEST( ST_HugeListLinkPushesInFront );
    MU_RUN_TEST( ST_HugeListUnlinkTakesTheMiddleOut );
    MU_RUN_TEST( ST_HugeListUnlinkDownToTheSentinel );
}

// ============================================================================
// HtMakeAllocator
// ============================================================================

MU_TEST( ST_MakeRoundsUpToWholeChunks )
{
    ht_virtual_allocator oneByte = HtMakeAllocator( 1 );
    ht_virtual_allocator oneChunk = HtMakeAllocator( CHUNK_SZ_IN_BYTES );
    ht_virtual_allocator onePastAChunk = HtMakeAllocator( CHUNK_SZ_IN_BYTES + 1 );

    mu_check( 1 == std::size( oneByte.chunkMap ) );
    mu_check( CHUNK_SZ_IN_BYTES == oneByte.reservedInBytes );
    mu_check( 1 == std::size( oneChunk.chunkMap ) );
    mu_check( CHUNK_SZ_IN_BYTES == oneChunk.reservedInBytes );
    mu_check( 2 == std::size( onePastAChunk.chunkMap ) );
    mu_check( ( 2 * CHUNK_SZ_IN_BYTES ) == onePastAChunk.reservedInBytes );
}

MU_TEST( ST_MakeHandsBackAnEmptyAllocator )
{
    ht_virtual_allocator fresh = HtMakeAllocator( CHUNK_SZ_IN_BYTES );
    u64 binCount = std::size( fresh.chunkMap ) * BINS_PER_CHUNK;
    for( u64 binIdx = 0; binIdx < binCount; ++binIdx )
    {
        mu_check( 0 == *GetBinAt( fresh.chunkMap, binIdx ) );
    }

    // NOTE: the branchless unlink dies if the sentinel is not self linked
    mu_check( &fresh.circularList == fresh.circularList.pNext );
    mu_check( &fresh.circularList == fresh.circularList.pPrev );
    mu_check( 0 == fresh.circularList.szInBytes );
}

MU_TEST( ST_MakePutsTheBlockRegionPastTheChunkMap )
{
    ht_virtual_allocator fresh = HtMakeAllocator( CHUNK_SZ_IN_BYTES );
    u64 mapSzInBytes = std::size( fresh.chunkMap ) * sizeof( ht_virtual_chunk );

    mu_check( nullptr != std::data( fresh.chunkMap ) );
    mu_check( ( ( u8* ) std::data( fresh.chunkMap ) + FwdAlignPot( mapSzInBytes, BLOCK_SZ_IN_BYTES ) ) == fresh.pMemBase );
    mu_check( 0 == ( ( u64 ) fresh.pMemBase % OS_PAGE_SIZE_IN_BYTES ) );
}

MU_TEST_SUITE( ST_SuiteMakeAllocator )
{
    MU_RUN_TEST( ST_MakeRoundsUpToWholeChunks );
    MU_RUN_TEST( ST_MakeHandsBackAnEmptyAllocator );
    MU_RUN_TEST( ST_MakePutsTheBlockRegionPastTheChunkMap );
}

// ============================================================================
// AllocVirtualBlock
// ============================================================================

MU_TEST( ST_AllocSingleBlockTakesTheFirstBit )
{
    u8* pBase = ( u8* ) gPool.pMemBase;

    ht_virt_alloc alloc = gPool.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    auto[ pBlock, meta ] = HtUnpackVirtualAllocation( alloc );

    mu_check( pBase == ( u8* ) pBlock );
    mu_check( ht_virt_alloc_type::BLOCK == meta.type );
    mu_check( 1 == meta.blockCount );
    mu_check( 0b1ull == *GetBinAt( gPool.chunkMap, 0 ) );

    gPool.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_AllocSingleBlocksAreContiguous )
{
    u8* pBase = ( u8* ) gPool.pMemBase;

    ht_virt_alloc first = gPool.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc second = gPool.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc third = gPool.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );

    mu_check( pBase == ( u8* ) HtUnpackVirtualAllocation( first ).ptr );
    mu_check( ( pBase + BLOCK_SZ_IN_BYTES ) == ( u8* ) HtUnpackVirtualAllocation( second ).ptr );
    mu_check( ( pBase + 2 * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtUnpackVirtualAllocation( third ).ptr );
    mu_check( 0b111ull == *GetBinAt( gPool.chunkMap, 0 ) );

    gPool.FreeVirtualBlock( first, 0 );
    gPool.FreeVirtualBlock( second, 0 );
    gPool.FreeVirtualBlock( third, 0 );
}

MU_TEST( ST_AllocMultiBlockRoundsPartialsUp )
{
    ht_virt_alloc onePastABlock = gPool.AllocVirtualBlock( BLOCK_SZ_IN_BYTES + 1, 0 );
    mu_check( 2 == HtUnpackVirtualAllocation( onePastABlock ).meta.blockCount );
    mu_check( 0b11ull == *GetBinAt( gPool.chunkMap, 0 ) );

    ht_virt_alloc oneShortOfThree = gPool.AllocVirtualBlock( 3 * BLOCK_SZ_IN_BYTES - 1, 0 );
    mu_check( 3 == HtUnpackVirtualAllocation( oneShortOfThree ).meta.blockCount );
    mu_check( 0b11111ull == *GetBinAt( gPool.chunkMap, 0 ) );

    gPool.FreeVirtualBlock( onePastABlock, 0 );
    gPool.FreeVirtualBlock( oneShortOfThree, 0 );
}

MU_TEST( ST_AllocMultiBlockTakesAWholeBin )
{
    // NOTE: the biggest request the block path still serves
    u8* pBase = ( u8* ) gPool.pMemBase;

    ht_virt_alloc alloc = gPool.AllocVirtualBlock( BLOCKS_PER_BIN * BLOCK_SZ_IN_BYTES, 0 );
    auto[ pBlock, meta ] = HtUnpackVirtualAllocation( alloc );

    mu_check( ht_virt_alloc_type::BLOCK == meta.type );
    mu_check( BLOCKS_PER_BIN == meta.blockCount );
    mu_check( pBase == ( u8* ) pBlock );
    mu_check( BIT_NPOS == *GetBinAt( gPool.chunkMap, 0 ) );
    mu_check( 0 == *GetBinAt( gPool.chunkMap, 1 ) );

    gPool.FreeVirtualBlock( alloc, 0 );
    mu_check( 0 == *GetBinAt( gPool.chunkMap, 0 ) );
}

MU_TEST( ST_AllocMultiBlockRunsNeverCrossABin )
{
    // NOTE: a bin is one atomic word, bin 0 has 4 free at the top so a 5 run has to go to bin 1
    u8* pBase = ( u8* ) gPool.pMemBase;
    *GetBinAt( gPool.chunkMap, 0 ) = ~( 0xFull << 60 );

    ht_virt_alloc alloc = gPool.AllocVirtualBlock( 5 * BLOCK_SZ_IN_BYTES, 0 );

    mu_check( ( pBase + BLOCKS_PER_BIN * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtUnpackVirtualAllocation( alloc ).ptr );
    mu_check( ~( 0xFull << 60 ) == *GetBinAt( gPool.chunkMap, 0 ) );
    mu_check( 0b11111ull == *GetBinAt( gPool.chunkMap, 1 ) );

    gPool.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_AllocMixesSingleAndMultiBlockRuns )
{
    u8* pBase = ( u8* ) gPool.pMemBase;

    ht_virt_alloc single = gPool.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc triple = gPool.AllocVirtualBlock( 3 * BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc pair = gPool.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );

    mu_check( pBase == ( u8* ) HtUnpackVirtualAllocation( single ).ptr );
    mu_check( ( pBase + BLOCK_SZ_IN_BYTES ) == ( u8* ) HtUnpackVirtualAllocation( triple ).ptr );
    mu_check( ( pBase + 4 * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtUnpackVirtualAllocation( pair ).ptr );
    mu_check( 0b111111ull == *GetBinAt( gPool.chunkMap, 0 ) );

    // NOTE: the hole the middle run leaves is exactly what the next 3 run takes back
    gPool.FreeVirtualBlock( triple, 0 );
    mu_check( 0b110001ull == *GetBinAt( gPool.chunkMap, 0 ) );

    ht_virt_alloc refill = gPool.AllocVirtualBlock( 3 * BLOCK_SZ_IN_BYTES, 0 );
    mu_check( ( pBase + BLOCK_SZ_IN_BYTES ) == ( u8* ) HtUnpackVirtualAllocation( refill ).ptr );
    mu_check( 0b111111ull == *GetBinAt( gPool.chunkMap, 0 ) );

    gPool.FreeVirtualBlock( single, 0 );
    gPool.FreeVirtualBlock( refill, 0 );
    gPool.FreeVirtualBlock( pair, 0 );
    mu_check( 0 == *GetBinAt( gPool.chunkMap, 0 ) );
}

MU_TEST( ST_AllocTakesTheLowestFittingHole )
{
    // NOTE: a 2 block hole at bit 3 and a 4 block hole at bit 20
    u8* pBase = ( u8* ) gPool.pMemBase;
    *GetBinAt( gPool.chunkMap, 0 ) = ~( ( 0b11ull << 3 ) | ( 0b1111ull << 20 ) );

    ht_virt_alloc alloc = gPool.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );

    mu_check( ( pBase + 3 * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtUnpackVirtualAllocation( alloc ).ptr );
    mu_check( ~( 0b1111ull << 20 ) == *GetBinAt( gPool.chunkMap, 0 ) );

    gPool.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_AllocSkipsHolesThatAreTooSmall )
{
    u8* pBase = ( u8* ) gPool.pMemBase;
    *GetBinAt( gPool.chunkMap, 0 ) = ~( ( 0b11ull << 3 ) | ( 0b1111ull << 20 ) );

    ht_virt_alloc alloc = gPool.AllocVirtualBlock( 4 * BLOCK_SZ_IN_BYTES, 0 );

    mu_check( ( pBase + 20 * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtUnpackVirtualAllocation( alloc ).ptr );
    mu_check( ~( 0b11ull << 3 ) == *GetBinAt( gPool.chunkMap, 0 ) );

    gPool.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_AllocFitsAHoleExactly )
{
    u8* pBase = ( u8* ) gPool.pMemBase;
    *GetBinAt( gPool.chunkMap, 0 ) = ~( 0b111ull << 10 );

    ht_virt_alloc alloc = gPool.AllocVirtualBlock( 3 * BLOCK_SZ_IN_BYTES, 0 );

    mu_check( ( pBase + 10 * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtUnpackVirtualAllocation( alloc ).ptr );
    mu_check( BIT_NPOS == *GetBinAt( gPool.chunkMap, 0 ) );

    gPool.FreeVirtualBlock( alloc, 0 );
    mu_check( ~( 0b111ull << 10 ) == *GetBinAt( gPool.chunkMap, 0 ) );
}

MU_TEST( ST_AllocSkipsFullBins )
{
    u8* pBase = ( u8* ) gPool.pMemBase;
    *GetBinAt( gPool.chunkMap, 0 ) = BIT_NPOS;
    *GetBinAt( gPool.chunkMap, 1 ) = BIT_NPOS;
    *GetBinAt( gPool.chunkMap, 2 ) = BIT_NPOS;

    ht_virt_alloc alloc = gPool.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );

    mu_check( ( pBase + 3 * BLOCKS_PER_BIN * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtUnpackVirtualAllocation( alloc ).ptr );
    mu_check( 0b1ull == *GetBinAt( gPool.chunkMap, 3 ) );
    mu_check( BIT_NPOS == *GetBinAt( gPool.chunkMap, 2 ) );

    gPool.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_AllocReachesTheLastBlockOfTheReserve )
{
    u8* pBase = ( u8* ) gPool.pMemBase;
    u64 binCount = std::size( gPool.chunkMap ) * BINS_PER_CHUNK;
    for( u64 binIdx = 0; binIdx < binCount; ++binIdx )
    {
        *GetBinAt( gPool.chunkMap, binIdx ) = BIT_NPOS;
    }
    *GetBinAt( gPool.chunkMap, binCount - 1 ) = ~( 1ull << 63 );

    ht_virt_alloc alloc = gPool.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    u8* pBlock = ( u8* ) HtUnpackVirtualAllocation( alloc ).ptr;

    // NOTE: the last block has to end exactly on the end of the reserve
    mu_check( ( pBase + ( binCount * BLOCKS_PER_BIN - 1 ) * BLOCK_SZ_IN_BYTES ) == pBlock );
    mu_check( ( pBlock + BLOCK_SZ_IN_BYTES ) == ( pBase + gPool.reservedInBytes ) );

    pBlock[ BLOCK_SZ_IN_BYTES - 1 ] = 0xEE;
    mu_check( 0xEE == pBlock[ BLOCK_SZ_IN_BYTES - 1 ] );

    gPool.FreeVirtualBlock( alloc, 0 );
    mu_check( ~( 1ull << 63 ) == *GetBinAt( gPool.chunkMap, binCount - 1 ) );
}

MU_TEST( ST_AllocStartsOnTheThreadsOwnChunk )
{
    u8* pBase = ( u8* ) gPool.pMemBase;

    ht_virt_alloc alloc = gPool.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 1 );

    mu_check( ( pBase + BINS_PER_CHUNK * BLOCKS_PER_BIN * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtUnpackVirtualAllocation( alloc ).ptr );
    mu_check( 0 == *GetBinAt( gPool.chunkMap, 0 ) );
    mu_check( 0b1ull == *GetBinAt( gPool.chunkMap, BINS_PER_CHUNK ) );

    gPool.FreeVirtualBlock( alloc, 1 );
    mu_check( 0 == *GetBinAt( gPool.chunkMap, BINS_PER_CHUNK ) );
}

MU_TEST( ST_AllocWrapsPastTheLastBin )
{
    u8* pBase = ( u8* ) gPool.pMemBase;
    u64 binCount = std::size( gPool.chunkMap ) * BINS_PER_CHUNK;
    for( u64 binIdx = BINS_PER_CHUNK; binIdx < binCount; ++binIdx )
    {
        *GetBinAt( gPool.chunkMap, binIdx ) = BIT_NPOS;
    }

    ht_virt_alloc alloc = gPool.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 1 );

    mu_check( pBase == ( u8* ) HtUnpackVirtualAllocation( alloc ).ptr );
    mu_check( 0b1ull == *GetBinAt( gPool.chunkMap, 0 ) );

    gPool.FreeVirtualBlock( alloc, 1 );
}

MU_TEST( ST_AllocThreadOffsetWrapsModuloBinCount )
{
    // NOTE: 2 chunks is 16 bins, thread 2 starts a full lap in and lands back on bin 0
    u8* pBase = ( u8* ) gPool.pMemBase;

    ht_virt_alloc alloc = gPool.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 2 );

    mu_check( pBase == ( u8* ) HtUnpackVirtualAllocation( alloc ).ptr );
    mu_check( 0b1ull == *GetBinAt( gPool.chunkMap, 0 ) );

    gPool.FreeVirtualBlock( alloc, 2 );
}

MU_TEST( ST_AllocHandsBackWritableMemory )
{
    ht_virt_alloc alloc = gPool.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );
    u8* pBlock = ( u8* ) HtUnpackVirtualAllocation( alloc ).ptr;

    pBlock[ 0 ] = 0xAB;
    pBlock[ 2 * BLOCK_SZ_IN_BYTES - 1 ] = 0xCD;
    mu_check( 0xAB == pBlock[ 0 ] );
    mu_check( 0xCD == pBlock[ 2 * BLOCK_SZ_IN_BYTES - 1 ] );

    gPool.FreeVirtualBlock( alloc, 0 );
}

// ============================================================================
// AllocVirtualBlock — negative
// ============================================================================
MU_TEST( ST_AllocBelowOneBlockFires )
{
    MU_ASSERT_FIRES( gPool.AllocVirtualBlock( 0, 0 ) );
    MU_ASSERT_FIRES( gPool.AllocVirtualBlock( 1, 0 ) );
    MU_ASSERT_FIRES( gPool.AllocVirtualBlock( BLOCK_SZ_IN_BYTES - 1, 0 ) );
    mu_check( 0 == *GetBinAt( gPool.chunkMap, 0 ) );
}

MU_TEST( ST_AllocOutOfMemoryFires )
{
    u64 binCount = std::size( gPool.chunkMap ) * BINS_PER_CHUNK;
    for( u64 binIdx = 0; binIdx < binCount; ++binIdx )
    {
        *GetBinAt( gPool.chunkMap, binIdx ) = BIT_NPOS;
    }

    MU_ASSERT_FIRES( gPool.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 ) );
}

MU_TEST( ST_AllocFiresWhenNoRunIsLongEnough )
{
    // NOTE: 3 free then 1 taken all the way down, room to spare but no run of 4
    u8* pBase = ( u8* ) gPool.pMemBase;
    u64 binCount = std::size( gPool.chunkMap ) * BINS_PER_CHUNK;
    for( u64 binIdx = 0; binIdx < binCount; ++binIdx )
    {
        *GetBinAt( gPool.chunkMap, binIdx ) = 0x8888888888888888ull;
    }

    ht_virt_alloc fits = gPool.AllocVirtualBlock( 3 * BLOCK_SZ_IN_BYTES, 0 );
    mu_check( pBase == ( u8* ) HtUnpackVirtualAllocation( fits ).ptr );
    gPool.FreeVirtualBlock( fits, 0 );

    MU_ASSERT_FIRES( gPool.AllocVirtualBlock( 4 * BLOCK_SZ_IN_BYTES, 0 ) );
}

MU_TEST_SUITE( ST_SuiteAllocVirtualBlock )
{
    MU_SUITE_CONFIGURE( &ScrubPool, nullptr );

    MU_RUN_TEST( ST_AllocSingleBlockTakesTheFirstBit );
    MU_RUN_TEST( ST_AllocSingleBlocksAreContiguous );
    MU_RUN_TEST( ST_AllocMultiBlockRoundsPartialsUp );
    MU_RUN_TEST( ST_AllocMultiBlockTakesAWholeBin );
    MU_RUN_TEST( ST_AllocMultiBlockRunsNeverCrossABin );
    MU_RUN_TEST( ST_AllocMixesSingleAndMultiBlockRuns );
    MU_RUN_TEST( ST_AllocTakesTheLowestFittingHole );
    MU_RUN_TEST( ST_AllocSkipsHolesThatAreTooSmall );
    MU_RUN_TEST( ST_AllocFitsAHoleExactly );
    MU_RUN_TEST( ST_AllocSkipsFullBins );
    MU_RUN_TEST( ST_AllocReachesTheLastBlockOfTheReserve );
    MU_RUN_TEST( ST_AllocStartsOnTheThreadsOwnChunk );
    MU_RUN_TEST( ST_AllocWrapsPastTheLastBin );
    MU_RUN_TEST( ST_AllocThreadOffsetWrapsModuloBinCount );
    MU_RUN_TEST( ST_AllocHandsBackWritableMemory );
    MU_RUN_TEST( ST_AllocBelowOneBlockFires );
    MU_RUN_TEST( ST_AllocOutOfMemoryFires );
    MU_RUN_TEST( ST_AllocFiresWhenNoRunIsLongEnough );
}

// ============================================================================
// FreeVirtualBlock
// ============================================================================

MU_TEST( ST_FreeClearsOnlyItsOwnBits )
{
    ht_virt_alloc head = gPool.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc mid = gPool.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc tail = gPool.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    mu_check( 0b1111ull == *GetBinAt( gPool.chunkMap, 0 ) );

    gPool.FreeVirtualBlock( mid, 0 );
    mu_check( 0b1001ull == *GetBinAt( gPool.chunkMap, 0 ) );

    gPool.FreeVirtualBlock( head, 0 );
    gPool.FreeVirtualBlock( tail, 0 );
    mu_check( 0 == *GetBinAt( gPool.chunkMap, 0 ) );
}

MU_TEST( ST_FreeClearsBitsAtTheTopOfABin )
{
    // NOTE: the run mask has to land on bits 60..63 without running off the word
    u8* pBase = ( u8* ) gPool.pMemBase;
    *GetBinAt( gPool.chunkMap, 0 ) = ~( 0xFull << 60 );

    ht_virt_alloc alloc = gPool.AllocVirtualBlock( 4 * BLOCK_SZ_IN_BYTES, 0 );
    mu_check( ( pBase + 60 * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtUnpackVirtualAllocation( alloc ).ptr );
    mu_check( BIT_NPOS == *GetBinAt( gPool.chunkMap, 0 ) );

    gPool.FreeVirtualBlock( alloc, 0 );
    mu_check( ~( 0xFull << 60 ) == *GetBinAt( gPool.chunkMap, 0 ) );
}

MU_TEST( ST_FreeRecoversTheBinFromTheAddress )
{
    // NOTE: free gets nothing but the pointer, it has to dig out both bin and bit
    u8* pBase = ( u8* ) gPool.pMemBase;
    *GetBinAt( gPool.chunkMap, 0 ) = BIT_NPOS;
    *GetBinAt( gPool.chunkMap, 1 ) = BIT_NPOS;
    *GetBinAt( gPool.chunkMap, 2 ) = ~( 0b11ull << 40 );

    ht_virt_alloc alloc = gPool.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );
    mu_check( ( pBase + ( 2 * BLOCKS_PER_BIN + 40 ) * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtUnpackVirtualAllocation( alloc ).ptr );
    mu_check( BIT_NPOS == *GetBinAt( gPool.chunkMap, 2 ) );

    gPool.FreeVirtualBlock( alloc, 0 );
    mu_check( ~( 0b11ull << 40 ) == *GetBinAt( gPool.chunkMap, 2 ) );
    mu_check( BIT_NPOS == *GetBinAt( gPool.chunkMap, 0 ) );
    mu_check( BIT_NPOS == *GetBinAt( gPool.chunkMap, 1 ) );
}

MU_TEST( ST_FreeThenAllocReusesTheSameAddress )
{
    ht_virt_alloc first = gPool.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );
    void* pFirst = HtUnpackVirtualAllocation( first ).ptr;
    gPool.FreeVirtualBlock( first, 0 );
    mu_check( 0 == *GetBinAt( gPool.chunkMap, 0 ) );

    ht_virt_alloc second = gPool.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );
    u8* pBlock = ( u8* ) HtUnpackVirtualAllocation( second ).ptr;
    mu_check( pFirst == ( void* ) pBlock );

    // NOTE: the free decommitted it, the realloc has to have committed it back
    pBlock[ 0 ] = 0x5A;
    mu_check( 0x5A == pBlock[ 0 ] );

    gPool.FreeVirtualBlock( second, 0 );
}

MU_TEST( ST_FreeOfTheNullAllocIsANoOp )
{
    *GetBinAt( gPool.chunkMap, 0 ) = 0b1010ull;

    gPool.FreeVirtualBlock( ht_virt_alloc{}, 0 );
    mu_check( 0b1010ull == *GetBinAt( gPool.chunkMap, 0 ) );
}

MU_TEST_SUITE( ST_SuiteFreeVirtualBlock )
{
    MU_SUITE_CONFIGURE( &ScrubPool, nullptr );

    MU_RUN_TEST( ST_FreeClearsOnlyItsOwnBits );
    MU_RUN_TEST( ST_FreeClearsBitsAtTheTopOfABin );
    MU_RUN_TEST( ST_FreeRecoversTheBinFromTheAddress );
    MU_RUN_TEST( ST_FreeThenAllocReusesTheSameAddress );
    MU_RUN_TEST( ST_FreeOfTheNullAllocIsANoOp );
}

// ============================================================================
// dedicated ( huge ) allocations
// ============================================================================

// NOTE: the smallest request that walks off the block path, freed the moment it checks out
static constexpr u64 DEDICATED_SZ_IN_BYTES = BLOCKS_PER_BIN * BLOCK_SZ_IN_BYTES + 1;

MU_TEST( ST_DedicatedStartsOneBytePastAWholeBin )
{
    u8* pBase = ( u8* ) gPool.pMemBase;

    ht_virt_alloc alloc = gPool.AllocVirtualBlock( DEDICATED_SZ_IN_BYTES, 0 );
    auto[ pHuge, meta ] = HtUnpackVirtualAllocation( alloc );
    gPool.FreeVirtualBlock( alloc, 0 );

    mu_check( ht_virt_alloc_type::DEDICATED == meta.type );
    mu_check( 0 == meta.blockCount );
    // NOTE: it comes straight from the OS, so it never shows up in the bins or in the reserve
    mu_check( ( ( u8* ) pHuge < pBase ) || ( ( u8* ) pHuge >= ( pBase + gPool.reservedInBytes ) ) );
    mu_check( 0 == *GetBinAt( gPool.chunkMap, 0 ) );
}

MU_TEST( ST_DedicatedPayloadSitsPastItsHeader )
{
    ht_virt_alloc alloc = gPool.AllocVirtualBlock( DEDICATED_SZ_IN_BYTES, 0 );
    u8* pHuge = ( u8* ) HtUnpackVirtualAllocation( alloc ).ptr;
    ht_huge_alloc* pHeader = ( ht_huge_alloc* ) pHuge - 1;

    // NOTE: the payload start is what has to be cache aligned, not the header
    mu_check( 0 == ( ( u64 ) pHuge % alignof( ht_huge_alloc ) ) );
    mu_check( ( DEDICATED_SZ_IN_BYTES + sizeof( ht_huge_alloc ) ) == pHeader->szInBytes );
    mu_check( pHeader == gPool.circularList.pNext );
    mu_check( &gPool.circularList == pHeader->pNext );

    pHuge[ 0 ] = 0x12;
    pHuge[ DEDICATED_SZ_IN_BYTES - 1 ] = 0x34;
    mu_check( 0x12 == pHuge[ 0 ] );
    mu_check( 0x34 == pHuge[ DEDICATED_SZ_IN_BYTES - 1 ] );

    gPool.FreeVirtualBlock( alloc, 0 );
    mu_check( &gPool.circularList == gPool.circularList.pNext );
}

MU_TEST( ST_DedicatedAndBlocksLiveSideBySide )
{
    u8* pBase = ( u8* ) gPool.pMemBase;

    ht_virt_alloc single = gPool.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc pair = gPool.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc huge = gPool.AllocVirtualBlock( DEDICATED_SZ_IN_BYTES, 0 );

    // NOTE: the huge one takes no bits, so the blocks around it stay packed
    mu_check( ( ( ht_huge_alloc* ) HtUnpackVirtualAllocation( huge ).ptr - 1 ) == gPool.circularList.pNext );
    mu_check( 0b111ull == *GetBinAt( gPool.chunkMap, 0 ) );
    gPool.FreeVirtualBlock( huge, 0 );

    mu_check( &gPool.circularList == gPool.circularList.pNext );
    mu_check( pBase == ( u8* ) HtUnpackVirtualAllocation( single ).ptr );
    mu_check( ( pBase + BLOCK_SZ_IN_BYTES ) == ( u8* ) HtUnpackVirtualAllocation( pair ).ptr );
    mu_check( 0b111ull == *GetBinAt( gPool.chunkMap, 0 ) );

    gPool.FreeVirtualBlock( single, 0 );
    gPool.FreeVirtualBlock( pair, 0 );
    mu_check( 0 == *GetBinAt( gPool.chunkMap, 0 ) );
}

MU_TEST_SUITE( ST_SuiteDedicatedAlloc )
{
    MU_SUITE_CONFIGURE( &ScrubPool, nullptr );

    MU_RUN_TEST( ST_DedicatedStartsOneBytePastAWholeBin );
    MU_RUN_TEST( ST_DedicatedPayloadSitsPastItsHeader );
    MU_RUN_TEST( ST_DedicatedAndBlocksLiveSideBySide );
}

// ============================================================================
// main
// ============================================================================

int main( int argc, char* argv[] )
{
    MU_RUN_SUITE( SuiteStaticArena );
    MU_RUN_SUITE( SuiteDynamicArena );

    // NOTE: the packed word cannot survive an OS that hands out fatter addresses
    if( HtOsUserAddrFitsPackedWord() )
    {
        MU_RUN_SUITE( ST_SuiteVirtAllocPacking );
        MU_RUN_SUITE( ST_SuiteChunkMap );
        MU_RUN_SUITE( ST_SuiteHugeList );
        MU_RUN_SUITE( ST_SuiteMakeAllocator );
        MU_RUN_SUITE( ST_SuiteAllocVirtualBlock );
        MU_RUN_SUITE( ST_SuiteFreeVirtualBlock );
        MU_RUN_SUITE( ST_SuiteDedicatedAlloc );
    }

    MU_REPORT();
    return MU_EXIT_CODE;
}