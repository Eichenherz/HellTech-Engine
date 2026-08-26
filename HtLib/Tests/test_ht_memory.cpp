// NOTE: covered cases —
//   alloc: basic, zero-byte, sequential, alignment (16/64/256), exact capacity
//   rewind: to mark, to zero, clamp past capacity
//   reset: basic, idempotent, alloc after reset, multi-cycle
//   negative: alloc past capacity, rewind past offset, non-pow2 alignment
//
// NOTE: covered cases — ST_ is single threaded, threadIdx is only the bin scan start offset
//   gate: the OS address space has to fit the packed word, a failure here stops the run cold
//   packing: round trips, top user address, equality, address past the user range, unaligned address
//   chunk map: GetBinAt order + out of range, HtCASLoopReserve flip / read / bail
//   make: chunk rounding, layout, empty initial state
//   alloc: single, multi, mixed, holes, full bins, thread offset, denials, negatives
//   free: bit clearing, address to bin, reuse, null alloc
//   dedicated: threshold, page count, side by side with blocks

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
// platform gate — every suite below it is meaningless if this one does not hold
// ============================================================================

MU_TEST( ST_GatePlatformFitsThePackedWord )
{
    SYSTEM_INFO sysInfo = {};
    GetSystemInfo( &sysInfo );

    // NOTE: the packed word cannot hold a fatter address than this
    mu_check( ( u64 ) sysInfo.lpMaximumApplicationAddress <= OS_USER_MAX_ADDR );
    mu_check( OS_PAGE_SIZE_IN_BYTES == sysInfo.dwPageSize );
    mu_check( 0 == ( BLOCK_SZ_IN_BYTES % sysInfo.dwAllocationGranularity ) );
}

MU_TEST_SUITE( ST_SuitePlatformGate )
{
    MU_RUN_TEST( ST_GatePlatformFitsThePackedWord );
}

// ============================================================================
// ht_virt_alloc packing
// ============================================================================

// NOTE: never dereferenced, the packing only cares that it is HT_INTERNAL_ALIGNMENT aligned
static void* const   gPackDummy               = ( void* ) HT_INTERNAL_ALIGNMENT;
// NOTE: the top user address rounded down to an address the packed word can actually hold
static constexpr u64 OS_USER_MAX_ALIGNED_ADDR = OS_USER_MAX_ADDR & ~( HT_INTERNAL_ALIGNMENT - 1 );

MU_TEST( ST_PackBlockRoundTrip )
{
    ht_virt_alloc alloc = { gPackDummy, 7, ht_virt_alloc_type::BLOCK };

    mu_check( gPackDummy == HtGetAllocPtr( alloc ) );
    mu_check( ht_virt_alloc_type::BLOCK == alloc.type );
    mu_check( 7 == alloc.metadata );
}

MU_TEST( ST_PackDedicatedRoundTrip )
{
    ht_virt_alloc alloc = { gPackDummy, 1234, ht_virt_alloc_type::DEDICATED };

    mu_check( gPackDummy == HtGetAllocPtr( alloc ) );
    mu_check( ht_virt_alloc_type::DEDICATED == alloc.type );
    mu_check( 1234 == alloc.metadata );
}

MU_TEST( ST_PackHoldsTheWholePayload )
{
    // NOTE: the payload field is a full 32 bits wide, nothing of it may fall off
    ht_virt_alloc alloc = { gPackDummy, 0xFFFFFFFF, ht_virt_alloc_type::DEDICATED };

    mu_check( gPackDummy == HtGetAllocPtr( alloc ) );
    mu_check( ht_virt_alloc_type::DEDICATED == alloc.type );
    mu_check( 0xFFFFFFFF == alloc.metadata );
}

MU_TEST( ST_PackHoldsTheTopUserAddress )
{
    ht_virt_alloc alloc = { ( void* ) OS_USER_MAX_ALIGNED_ADDR, 1, ht_virt_alloc_type::BLOCK };

    mu_check( ( void* ) OS_USER_MAX_ALIGNED_ADDR == HtGetAllocPtr( alloc ) );
    mu_check( 1 == alloc.metadata );
}

MU_TEST( ST_PackHoldsRealPointers )
{
    // NOTE: only OS handed out memory qualifies, it is the only thing granularity aligned
    void* pOsMem = ht_os_virtual_alloc( OS_PAGE_SIZE_IN_BYTES );

    mu_check( pOsMem == HtGetAllocPtr( ht_virt_alloc( pOsMem, 1, ht_virt_alloc_type::BLOCK ) ) );

    ht_os_virtual_release( pOsMem );
}

MU_TEST( ST_PackDefaultIsTheFreeSentinel )
{
    // NOTE: FreeVirtualBlock bails out on this
    mu_check( ht_virt_alloc{} == ht_virt_alloc{} );
    mu_check( !( ht_virt_alloc( gPackDummy, 1, ht_virt_alloc_type::BLOCK ) == ht_virt_alloc{} ) );
}

MU_TEST( ST_PackEqualityCoversTheMetadata )
{
    ht_virt_alloc oneBlock = { gPackDummy, 1, ht_virt_alloc_type::BLOCK };
    ht_virt_alloc twoBlocks = { gPackDummy, 2, ht_virt_alloc_type::BLOCK };
    ht_virt_alloc dedicated = { gPackDummy, 1, ht_virt_alloc_type::DEDICATED };

    mu_check( oneBlock == ht_virt_alloc( gPackDummy, 1, ht_virt_alloc_type::BLOCK ) );
    mu_check( !( oneBlock == twoBlocks ) );
    // NOTE: the type bit is part of the word too
    mu_check( !( oneBlock == dedicated ) );
}

// ============================================================================
// ht_virt_alloc packing — negative
// ============================================================================
MU_TEST( ST_PackAddressAboveTheUserRangeFires )
{
    ht_virt_alloc probe = {};
    MU_ASSERT_FIRES(
        probe = ht_virt_alloc( ( void* ) ( 1ull << OS_USER_ADDR_BIT_WIDTH ), 1, ht_virt_alloc_type::BLOCK ) );
}

MU_TEST( ST_PackUnalignedAddressFires )
{
    // NOTE: the low bits are metadata, an address that uses them cannot survive the round trip
    ht_virt_alloc probe = {};
    MU_ASSERT_FIRES(
        probe = ht_virt_alloc( ( void* ) ( HT_INTERNAL_ALIGNMENT + 1 ), 1, ht_virt_alloc_type::BLOCK ) );
    MU_ASSERT_FIRES(
        probe = ht_virt_alloc( ( void* ) OS_PAGE_SIZE_IN_BYTES, 1, ht_virt_alloc_type::BLOCK ) );
}

MU_TEST( ST_PackPayloadPastItsFieldFires )
{
    // NOTE: only 32 bits ride along, anything above them would eat into the address
    ht_virt_alloc probe = {};
    MU_ASSERT_FIRES( probe = ht_virt_alloc( gPackDummy, 1ull << 32, ht_virt_alloc_type::BLOCK ) );
}

MU_TEST_SUITE( ST_SuiteVirtAllocPacking )
{
    MU_RUN_TEST( ST_PackBlockRoundTrip );
    MU_RUN_TEST( ST_PackDedicatedRoundTrip );
    MU_RUN_TEST( ST_PackHoldsTheWholePayload );
    MU_RUN_TEST( ST_PackHoldsTheTopUserAddress );
    MU_RUN_TEST( ST_PackHoldsRealPointers );
    MU_RUN_TEST( ST_PackDefaultIsTheFreeSentinel );
    MU_RUN_TEST( ST_PackEqualityCoversTheMetadata );
    MU_RUN_TEST( ST_PackAddressAboveTheUserRangeFires );
    MU_RUN_TEST( ST_PackUnalignedAddressFires );
    MU_RUN_TEST( ST_PackPayloadPastItsFieldFires );
}

// ============================================================================
// chunk map addressing and the CAS reserve loop
// ============================================================================

// NOTE: 2 chunks is the smallest pool where the thread start offset is observable. A reserve is
// address space only, the tests hand every block they commit back.
static ht_virtual_allocator htAllocator = HtMakeAllocator( 2 * CHUNK_SZ_IN_BYTES );

static void ScrubPool()
{
    u64 binCount = std::size( htAllocator.chunkMap ) * BINS_PER_CHUNK;
    for( u64 binIdx = 0; binIdx < binCount; ++binIdx )
    {
        *GetBinAt( htAllocator.chunkMap, binIdx ) = 0;
    }
}

MU_TEST( ST_GetBinAtWalksChunksThenBins )
{
    mu_check( &htAllocator.chunkMap[ 0 ].blockBitmap[ 0 ] == GetBinAt( htAllocator.chunkMap, 0 ) );
    mu_check( &htAllocator.chunkMap[ 0 ].blockBitmap[ BINS_PER_CHUNK - 1 ]
        == GetBinAt( htAllocator.chunkMap, BINS_PER_CHUNK - 1 ) );
    mu_check( &htAllocator.chunkMap[ 1 ].blockBitmap[ 0 ] == GetBinAt( htAllocator.chunkMap, BINS_PER_CHUNK ) );
    mu_check( &htAllocator.chunkMap[ 1 ].blockBitmap[ 1 ] == GetBinAt( htAllocator.chunkMap, BINS_PER_CHUNK + 1 ) );
}

MU_TEST( ST_GetBinAtPastTheMapFires )
{
    volatile u64* pBin = nullptr;
    MU_ASSERT_FIRES( pBin = GetBinAt( htAllocator.chunkMap, std::size( htAllocator.chunkMap ) * BINS_PER_CHUNK ) );
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
}

MU_TEST( ST_MakeKeepsTheChunkMapOutOfTheBlockRegion )
{
    // NOTE: the map is its own OS allocation now, it must not eat into the reserve
    ht_virtual_allocator fresh = HtMakeAllocator( CHUNK_SZ_IN_BYTES );
    u8* pMapBase = ( u8* ) std::data( fresh.chunkMap );
    u8* pBlockBase = ( u8* ) fresh.pMemBase;

    mu_check( nullptr != pMapBase );
    mu_check( nullptr != pBlockBase );
    mu_check( ( pMapBase + std::size( fresh.chunkMap ) * sizeof( ht_virtual_chunk ) ) <= pBlockBase
        || ( pBlockBase + fresh.reservedInBytes ) <= pMapBase );
}

MU_TEST( ST_MakeAlignsEveryBlockStart )
{
    // NOTE: base on the OS granularity plus a block sized stride is what puts every block on 64k
    ht_virtual_allocator fresh = HtMakeAllocator( CHUNK_SZ_IN_BYTES );
    u64 blockCount = std::size( fresh.chunkMap ) * BINS_PER_CHUNK * BLOCKS_PER_BIN;

    mu_check( 0 == ( ( u64 ) fresh.pMemBase % HT_INTERNAL_ALIGNMENT ) );
    for( u64 blockIdx = 0; blockIdx < blockCount; ++blockIdx )
    {
        u8* pBlock = ( u8* ) fresh.pMemBase + blockIdx * BLOCK_SZ_IN_BYTES;
        mu_check( 0 == ( ( u64 ) pBlock % HT_INTERNAL_ALIGNMENT ) );
    }
}

MU_TEST_SUITE( ST_SuiteMakeAllocator )
{
    MU_RUN_TEST( ST_MakeRoundsUpToWholeChunks );
    MU_RUN_TEST( ST_MakeHandsBackAnEmptyAllocator );
    MU_RUN_TEST( ST_MakeKeepsTheChunkMapOutOfTheBlockRegion );
    MU_RUN_TEST( ST_MakeAlignsEveryBlockStart );
}

// ============================================================================
// AllocVirtualBlock
// ============================================================================

MU_TEST( ST_AllocSingleBlockTakesTheFirstBit )
{
    u8* pBase = ( u8* ) htAllocator.pMemBase;

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    mu_check( pBase == ( u8* ) HtGetAllocPtr( alloc ) );
    mu_check( ht_virt_alloc_type::BLOCK == alloc.type );
    mu_check( 1 == alloc.metadata );
    mu_check( 0b1ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_AllocSingleBlocksAreContiguous )
{
    u8* pBase = ( u8* ) htAllocator.pMemBase;

    ht_virt_alloc first = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc second = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc third = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );

    mu_check( pBase == ( u8* ) HtGetAllocPtr( first ) );
    mu_check( ( pBase + BLOCK_SZ_IN_BYTES ) == ( u8* ) HtGetAllocPtr( second ) );
    mu_check( ( pBase + 2 * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtGetAllocPtr( third ) );
    mu_check( 0b111ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( first, 0 );
    htAllocator.FreeVirtualBlock( second, 0 );
    htAllocator.FreeVirtualBlock( third, 0 );
}

MU_TEST( ST_AllocMultiBlockRoundsPartialsUp )
{
    ht_virt_alloc onePastABlock = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES + 1, 0 );
    mu_check( 2 == onePastABlock.metadata );
    mu_check( 0b11ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    ht_virt_alloc oneShortOfThree = htAllocator.AllocVirtualBlock( 3 * BLOCK_SZ_IN_BYTES - 1, 0 );
    mu_check( 3 == oneShortOfThree.metadata );
    mu_check( 0b11111ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( onePastABlock, 0 );
    htAllocator.FreeVirtualBlock( oneShortOfThree, 0 );
}

MU_TEST( ST_AllocMultiBlockTakesAWholeBin )
{
    // NOTE: the biggest request the block path still serves
    u8* pBase = ( u8* ) htAllocator.pMemBase;

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( BLOCKS_PER_BIN * BLOCK_SZ_IN_BYTES, 0 );
    mu_check( ht_virt_alloc_type::BLOCK == alloc.type );
    mu_check( BLOCKS_PER_BIN == alloc.metadata );
    mu_check( pBase == ( u8* ) HtGetAllocPtr( alloc ) );
    mu_check( BIT_NPOS == *GetBinAt( htAllocator.chunkMap, 0 ) );
    mu_check( 0 == *GetBinAt( htAllocator.chunkMap, 1 ) );

    htAllocator.FreeVirtualBlock( alloc, 0 );
    mu_check( 0 == *GetBinAt( htAllocator.chunkMap, 0 ) );
}

MU_TEST( ST_AllocMultiBlockRunsNeverCrossABin )
{
    // NOTE: a bin is one atomic word, bin 0 has 4 free at the top so a 5 run has to go to bin 1
    u8* pBase = ( u8* ) htAllocator.pMemBase;
    *GetBinAt( htAllocator.chunkMap, 0 ) = ~( 0xFull << 60 );

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( 5 * BLOCK_SZ_IN_BYTES, 0 );

    mu_check( ( pBase + BLOCKS_PER_BIN * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtGetAllocPtr( alloc ) );
    mu_check( ~( 0xFull << 60 ) == *GetBinAt( htAllocator.chunkMap, 0 ) );
    mu_check( 0b11111ull == *GetBinAt( htAllocator.chunkMap, 1 ) );

    htAllocator.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_AllocMixesSingleAndMultiBlockRuns )
{
    u8* pBase = ( u8* ) htAllocator.pMemBase;

    ht_virt_alloc single = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc triple = htAllocator.AllocVirtualBlock( 3 * BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc pair = htAllocator.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );

    mu_check( pBase == ( u8* ) HtGetAllocPtr( single ) );
    mu_check( ( pBase + BLOCK_SZ_IN_BYTES ) == ( u8* ) HtGetAllocPtr( triple ) );
    mu_check( ( pBase + 4 * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtGetAllocPtr( pair ) );
    mu_check( 0b111111ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    // NOTE: the hole the middle run leaves is exactly what the next 3 run takes back
    htAllocator.FreeVirtualBlock( triple, 0 );
    mu_check( 0b110001ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    ht_virt_alloc refill = htAllocator.AllocVirtualBlock( 3 * BLOCK_SZ_IN_BYTES, 0 );
    mu_check( ( pBase + BLOCK_SZ_IN_BYTES ) == ( u8* ) HtGetAllocPtr( refill ) );
    mu_check( 0b111111ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( single, 0 );
    htAllocator.FreeVirtualBlock( refill, 0 );
    htAllocator.FreeVirtualBlock( pair, 0 );
    mu_check( 0 == *GetBinAt( htAllocator.chunkMap, 0 ) );
}

MU_TEST( ST_AllocTakesTheLowestFittingHole )
{
    // NOTE: a 2 block hole at bit 3 and a 4 block hole at bit 20
    u8* pBase = ( u8* ) htAllocator.pMemBase;
    *GetBinAt( htAllocator.chunkMap, 0 ) = ~( ( 0b11ull << 3 ) | ( 0b1111ull << 20 ) );

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );

    mu_check( ( pBase + 3 * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtGetAllocPtr( alloc ) );
    mu_check( ~( 0b1111ull << 20 ) == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_AllocSkipsHolesThatAreTooSmall )
{
    u8* pBase = ( u8* ) htAllocator.pMemBase;
    *GetBinAt( htAllocator.chunkMap, 0 ) = ~( ( 0b11ull << 3 ) | ( 0b1111ull << 20 ) );

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( 4 * BLOCK_SZ_IN_BYTES, 0 );

    mu_check( ( pBase + 20 * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtGetAllocPtr( alloc ) );
    mu_check( ~( 0b11ull << 3 ) == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_AllocFitsAHoleExactly )
{
    u8* pBase = ( u8* ) htAllocator.pMemBase;
    *GetBinAt( htAllocator.chunkMap, 0 ) = ~( 0b111ull << 10 );

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( 3 * BLOCK_SZ_IN_BYTES, 0 );

    mu_check( ( pBase + 10 * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtGetAllocPtr( alloc ) );
    mu_check( BIT_NPOS == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( alloc, 0 );
    mu_check( ~( 0b111ull << 10 ) == *GetBinAt( htAllocator.chunkMap, 0 ) );
}

MU_TEST( ST_AllocSkipsFullBins )
{
    u8* pBase = ( u8* ) htAllocator.pMemBase;
    *GetBinAt( htAllocator.chunkMap, 0 ) = BIT_NPOS;
    *GetBinAt( htAllocator.chunkMap, 1 ) = BIT_NPOS;
    *GetBinAt( htAllocator.chunkMap, 2 ) = BIT_NPOS;

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );

    mu_check( ( pBase + 3 * BLOCKS_PER_BIN * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtGetAllocPtr( alloc ) );
    mu_check( 0b1ull == *GetBinAt( htAllocator.chunkMap, 3 ) );
    mu_check( BIT_NPOS == *GetBinAt( htAllocator.chunkMap, 2 ) );

    htAllocator.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_AllocReachesTheLastBlockOfTheReserve )
{
    u8* pBase = ( u8* ) htAllocator.pMemBase;
    u64 binCount = std::size( htAllocator.chunkMap ) * BINS_PER_CHUNK;
    for( u64 binIdx = 0; binIdx < binCount; ++binIdx )
    {
        *GetBinAt( htAllocator.chunkMap, binIdx ) = BIT_NPOS;
    }
    *GetBinAt( htAllocator.chunkMap, binCount - 1 ) = ~( 1ull << 63 );

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    u8* pBlock = ( u8* ) HtGetAllocPtr( alloc );

    // NOTE: the last block has to end exactly on the end of the reserve
    mu_check( ( pBase + ( binCount * BLOCKS_PER_BIN - 1 ) * BLOCK_SZ_IN_BYTES ) == pBlock );
    mu_check( ( pBlock + BLOCK_SZ_IN_BYTES ) == ( pBase + htAllocator.reservedInBytes ) );

    pBlock[ BLOCK_SZ_IN_BYTES - 1 ] = 0xEE;
    mu_check( 0xEE == pBlock[ BLOCK_SZ_IN_BYTES - 1 ] );

    htAllocator.FreeVirtualBlock( alloc, 0 );
    mu_check( ~( 1ull << 63 ) == *GetBinAt( htAllocator.chunkMap, binCount - 1 ) );
}

MU_TEST( ST_AllocStartsOnTheThreadsOwnChunk )
{
    u8* pBase = ( u8* ) htAllocator.pMemBase;

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 1 );

    mu_check( ( pBase + BINS_PER_CHUNK * BLOCKS_PER_BIN * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtGetAllocPtr( alloc ) );
    mu_check( 0 == *GetBinAt( htAllocator.chunkMap, 0 ) );
    mu_check( 0b1ull == *GetBinAt( htAllocator.chunkMap, BINS_PER_CHUNK ) );

    htAllocator.FreeVirtualBlock( alloc, 1 );
    mu_check( 0 == *GetBinAt( htAllocator.chunkMap, BINS_PER_CHUNK ) );
}

MU_TEST( ST_AllocWrapsPastTheLastBin )
{
    u8* pBase = ( u8* ) htAllocator.pMemBase;
    u64 binCount = std::size( htAllocator.chunkMap ) * BINS_PER_CHUNK;
    for( u64 binIdx = BINS_PER_CHUNK; binIdx < binCount; ++binIdx )
    {
        *GetBinAt( htAllocator.chunkMap, binIdx ) = BIT_NPOS;
    }

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 1 );

    mu_check( pBase == ( u8* ) HtGetAllocPtr( alloc ) );
    mu_check( 0b1ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( alloc, 1 );
}

MU_TEST( ST_AllocThreadOffsetWrapsModuloBinCount )
{
    // NOTE: 2 chunks is 16 bins, thread 2 starts a full lap in and lands back on bin 0
    u8* pBase = ( u8* ) htAllocator.pMemBase;

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 2 );

    mu_check( pBase == ( u8* ) HtGetAllocPtr( alloc ) );
    mu_check( 0b1ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( alloc, 2 );
}

MU_TEST( ST_AllocHandsBackWritableMemory )
{
    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );
    u8* pBlock = ( u8* ) HtGetAllocPtr( alloc );

    pBlock[ 0 ] = 0xAB;
    pBlock[ 2 * BLOCK_SZ_IN_BYTES - 1 ] = 0xCD;
    mu_check( 0xAB == pBlock[ 0 ] );
    mu_check( 0xCD == pBlock[ 2 * BLOCK_SZ_IN_BYTES - 1 ] );

    htAllocator.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_AllocOutOfMemoryReturnsTheNullAlloc )
{
    u64 binCount = std::size( htAllocator.chunkMap ) * BINS_PER_CHUNK;
    for( u64 binIdx = 0; binIdx < binCount; ++binIdx )
    {
        *GetBinAt( htAllocator.chunkMap, binIdx ) = BIT_NPOS;
    }

    mu_check( ht_virt_alloc{} == htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 ) );
    mu_check( BIT_NPOS == *GetBinAt( htAllocator.chunkMap, 0 ) );
}

MU_TEST( ST_AllocReturnsTheNullAllocWhenNoRunIsLongEnough )
{
    // NOTE: 3 free then 1 taken all the way down, room to spare but no run of 4
    u8* pBase = ( u8* ) htAllocator.pMemBase;
    u64 binCount = std::size( htAllocator.chunkMap ) * BINS_PER_CHUNK;
    for( u64 binIdx = 0; binIdx < binCount; ++binIdx )
    {
        *GetBinAt( htAllocator.chunkMap, binIdx ) = 0x8888888888888888ull;
    }

    ht_virt_alloc fits = htAllocator.AllocVirtualBlock( 3 * BLOCK_SZ_IN_BYTES, 0 );
    mu_check( pBase == ( u8* ) HtGetAllocPtr( fits ) );
    htAllocator.FreeVirtualBlock( fits, 0 );

    // NOTE: a denial leaves every bin exactly as it found it
    mu_check( ht_virt_alloc{} == htAllocator.AllocVirtualBlock( 4 * BLOCK_SZ_IN_BYTES, 0 ) );
    mu_check( 0x8888888888888888ull == *GetBinAt( htAllocator.chunkMap, 0 ) );
}

// ============================================================================
// AllocVirtualBlock — negative
// ============================================================================
MU_TEST( ST_AllocBelowOneBlockFires )
{
    MU_ASSERT_FIRES( htAllocator.AllocVirtualBlock( 0, 0 ) );
    MU_ASSERT_FIRES( htAllocator.AllocVirtualBlock( 1, 0 ) );
    MU_ASSERT_FIRES( htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES - 1, 0 ) );
    mu_check( 0 == *GetBinAt( htAllocator.chunkMap, 0 ) );
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
    MU_RUN_TEST( ST_AllocOutOfMemoryReturnsTheNullAlloc );
    MU_RUN_TEST( ST_AllocReturnsTheNullAllocWhenNoRunIsLongEnough );
    MU_RUN_TEST( ST_AllocBelowOneBlockFires );
}

// ============================================================================
// FreeVirtualBlock
// ============================================================================

MU_TEST( ST_FreeClearsOnlyItsOwnBits )
{
    ht_virt_alloc head = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc mid = htAllocator.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc tail = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    mu_check( 0b1111ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( mid, 0 );
    mu_check( 0b1001ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( head, 0 );
    htAllocator.FreeVirtualBlock( tail, 0 );
    mu_check( 0 == *GetBinAt( htAllocator.chunkMap, 0 ) );
}

MU_TEST( ST_FreeClearsBitsAtTheTopOfABin )
{
    // NOTE: the run mask has to land on bits 60..63 without running off the word
    u8* pBase = ( u8* ) htAllocator.pMemBase;
    *GetBinAt( htAllocator.chunkMap, 0 ) = ~( 0xFull << 60 );

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( 4 * BLOCK_SZ_IN_BYTES, 0 );
    mu_check( ( pBase + 60 * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtGetAllocPtr( alloc ) );
    mu_check( BIT_NPOS == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( alloc, 0 );
    mu_check( ~( 0xFull << 60 ) == *GetBinAt( htAllocator.chunkMap, 0 ) );
}

MU_TEST( ST_FreeRecoversTheBinFromTheAddress )
{
    // NOTE: free gets nothing but the pointer, it has to dig out both bin and bit
    u8* pBase = ( u8* ) htAllocator.pMemBase;
    *GetBinAt( htAllocator.chunkMap, 0 ) = BIT_NPOS;
    *GetBinAt( htAllocator.chunkMap, 1 ) = BIT_NPOS;
    *GetBinAt( htAllocator.chunkMap, 2 ) = ~( 0b11ull << 40 );

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );
    mu_check( ( pBase + ( 2 * BLOCKS_PER_BIN + 40 ) * BLOCK_SZ_IN_BYTES ) == ( u8* ) HtGetAllocPtr( alloc ) );
    mu_check( BIT_NPOS == *GetBinAt( htAllocator.chunkMap, 2 ) );

    htAllocator.FreeVirtualBlock( alloc, 0 );
    mu_check( ~( 0b11ull << 40 ) == *GetBinAt( htAllocator.chunkMap, 2 ) );
    mu_check( BIT_NPOS == *GetBinAt( htAllocator.chunkMap, 0 ) );
    mu_check( BIT_NPOS == *GetBinAt( htAllocator.chunkMap, 1 ) );
}

MU_TEST( ST_FreeThenAllocReusesTheSameAddress )
{
    ht_virt_alloc first = htAllocator.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );
    void* pFirst = HtGetAllocPtr( first );
    htAllocator.FreeVirtualBlock( first, 0 );
    mu_check( 0 == *GetBinAt( htAllocator.chunkMap, 0 ) );

    ht_virt_alloc second = htAllocator.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );
    u8* pBlock = ( u8* ) HtGetAllocPtr( second );
    mu_check( pFirst == ( void* ) pBlock );

    // NOTE: the free decommitted it, the realloc has to have committed it back
    pBlock[ 0 ] = 0x5A;
    mu_check( 0x5A == pBlock[ 0 ] );

    htAllocator.FreeVirtualBlock( second, 0 );
}

MU_TEST( ST_FreeOfTheNullAllocIsANoOp )
{
    *GetBinAt( htAllocator.chunkMap, 0 ) = 0b1010ull;

    htAllocator.FreeVirtualBlock( ht_virt_alloc{}, 0 );
    mu_check( 0b1010ull == *GetBinAt( htAllocator.chunkMap, 0 ) );
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
static constexpr u64 DEDICATED_SZ_IN_PAGES =
    ( DEDICATED_SZ_IN_BYTES + HT_DEDICATED_ALIGNMENT - 1 ) / HT_DEDICATED_ALIGNMENT;

MU_TEST( ST_DedicatedStartsOneBytePastAWholeBin )
{
    u8* pBase = ( u8* ) htAllocator.pMemBase;

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( DEDICATED_SZ_IN_BYTES, 0 );
    u8* pHuge = ( u8* ) HtGetAllocPtr( alloc );
    htAllocator.FreeVirtualBlock( alloc, 0 );

    mu_check( ht_virt_alloc_type::DEDICATED == alloc.type );
    // NOTE: it comes straight from the OS, so it never shows up in the bins or in the reserve
    mu_check( ( pHuge < pBase ) || ( pHuge >= ( pBase + htAllocator.reservedInBytes ) ) );
    mu_check( 0 == *GetBinAt( htAllocator.chunkMap, 0 ) );
}

MU_TEST( ST_DedicatedCarriesItsSizeInPages )
{
    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( DEDICATED_SZ_IN_BYTES, 0 );
    u8* pBytes = ( u8* ) HtGetAllocPtr( alloc );

    // NOTE: the request is rounded up to whole dedicated pages, that count is the whole payload
    mu_check( DEDICATED_SZ_IN_PAGES == alloc.metadata );
    // NOTE: only the packing alignment is checked, the OS only promises granularity on its own
    mu_check( 0 == ( ( u64 ) pBytes % HT_INTERNAL_ALIGNMENT ) );

    pBytes[ 0 ] = 0x12;
    pBytes[ DEDICATED_SZ_IN_BYTES - 1 ] = 0x34;
    mu_check( 0x12 == pBytes[ 0 ] );
    mu_check( 0x34 == pBytes[ DEDICATED_SZ_IN_BYTES - 1 ] );

    htAllocator.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_DedicatedAndBlocksLiveSideBySide )
{
    u8* pBase = ( u8* ) htAllocator.pMemBase;

    ht_virt_alloc single = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc pair = htAllocator.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc huge = htAllocator.AllocVirtualBlock( DEDICATED_SZ_IN_BYTES, 0 );

    // NOTE: the huge one takes no bits, so the blocks around it stay packed
    mu_check( ht_virt_alloc_type::DEDICATED == huge.type );
    mu_check( 0b111ull == *GetBinAt( htAllocator.chunkMap, 0 ) );
    htAllocator.FreeVirtualBlock( huge, 0 );

    mu_check( pBase == ( u8* ) HtGetAllocPtr( single ) );
    mu_check( ( pBase + BLOCK_SZ_IN_BYTES ) == ( u8* ) HtGetAllocPtr( pair ) );
    mu_check( 0b111ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( single, 0 );
    htAllocator.FreeVirtualBlock( pair, 0 );
    mu_check( 0 == *GetBinAt( htAllocator.chunkMap, 0 ) );
}

MU_TEST_SUITE( ST_SuiteDedicatedAlloc )
{
    MU_SUITE_CONFIGURE( &ScrubPool, nullptr );

    MU_RUN_TEST( ST_DedicatedStartsOneBytePastAWholeBin );
    MU_RUN_TEST( ST_DedicatedCarriesItsSizeInPages );
    MU_RUN_TEST( ST_DedicatedAndBlocksLiveSideBySide );
}

// ============================================================================
// main
// ============================================================================

i32 main()
{
    MU_RUN_SUITE( SuiteStaticArena );
    MU_RUN_SUITE( SuiteDynamicArena );

    // NOTE: the packed word cannot survive an OS that hands out fatter addresses, everything
    // past the gate would fail for that one reason so we report what we have and walk away
    i32 failsBeforeTheGate = minunit_fail;
    MU_RUN_SUITE( ST_SuitePlatformGate );
    [[ unlikely ]]
    if( failsBeforeTheGate != minunit_fail )
    {
        MU_REPORT();
        return MU_EXIT_CODE;
    }

    MU_RUN_SUITE( ST_SuiteVirtAllocPacking );
    MU_RUN_SUITE( ST_SuiteChunkMap );
    MU_RUN_SUITE( ST_SuiteMakeAllocator );
    MU_RUN_SUITE( ST_SuiteAllocVirtualBlock );
    MU_RUN_SUITE( ST_SuiteFreeVirtualBlock );
    MU_RUN_SUITE( ST_SuiteDedicatedAlloc );

    MU_REPORT();
    return MU_EXIT_CODE;
}