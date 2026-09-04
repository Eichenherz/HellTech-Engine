// NOTE: covered cases — ST_ is single threaded, threadIdx is only the bin scan start offset
//   gate: the OS page and reserve granularity have to match what the layout constants assume
//   layout: chunk region is whole chunks, bins stop exactly where the dedicated bump starts
//   chunk map: GetBinAt order + out of range, HtCASLoopReserve flip / read / bail
//   make: layout, empty initial state, map out of the reserve
//   alloc: single, multi, mixed, holes, full bins, thread offset, denials, negatives
//   free: bit clearing, address to bin, reuse, null alloc
//   dedicated: the border, rounding, monotonic bump, side by side with blocks
//   accounting: one bin charged per fresh bin, dedicated charged by size

#include "test_common.h"

#include <array>

#include <ht_memory.h>
// NOTE: included, not linked — GetBinAt and friends are file local. CMake drops the TU.
#include <ht_allocator.cpp>

#include <Windows.h>

// NOTE: longjmp interception globals — extern'd in ht_error.h under HT_TESTS
jmp_buf gHtAssertJmpbuf;
i32     gHtAssertFired = 0;

// ============================================================================
// fixture
// ============================================================================

// NOTE: 2 chunks is the smallest map where the thread start offset is observable. The map is a
// plain std::array so the tests never depend on the 8 GB one HtMakeVirtualAllocator hands out.
// The reserve is the real one though: the dedicated bump starts at the far side of it.
static constexpr u64 TEST_CHUNK_COUNT   = 2;
static constexpr u64 TEST_BIN_COUNT     = TEST_CHUNK_COUNT * BINS_PER_CHUNK;
static constexpr u64 TEST_MAP_SZ_IN_BYTES = TEST_CHUNK_COUNT * CHUNK_SZ_IN_BYTES;

static std::array<ht_virtual_chunk, TEST_CHUNK_COUNT> gTestChunkMap = {};

static ht_virtual_allocator MakeTestAllocator()
{
    return {
        .pMemBase           = ( u8* ) ht_os_virtual_reserve( MAX_RESERVE_SZ_IN_BYTES ),
        .reservedInBytes    = MAX_RESERVE_SZ_IN_BYTES,
        .chunkMap           = gTestChunkMap
    };
}

static ht_virtual_allocator htAllocator = MakeTestAllocator();

static void ScrubPool()
{
    for( u64 binIdx = 0; binIdx < TEST_BIN_COUNT; ++binIdx )
    {
        *GetBinAt( htAllocator.chunkMap, binIdx ) = FREE_AS_A_BIRD;
    }
    htAllocator.committedInBytes     = 0;
    htAllocator.dedicatedAllocOffset = CHUNK_REGION_CAP_IN_BYTES;
}

static bool IsNullAlloc( ht_virt_alloc alloc )
{
    return ( nullptr == std::data( alloc ) ) && ( 0 == std::size( alloc ) );
}

static u64 AllocSzInBlocks( ht_virt_alloc alloc )
{
    return std::size( alloc ) / BLOCK_SZ_IN_BYTES;
}

// ============================================================================
// platform gate — every suite below it is meaningless if this one does not hold
// ============================================================================

MU_TEST( ST_GatePlatformMatchesTheLayoutConstants )
{
    SYSTEM_INFO sysInfo = {};
    GetSystemInfo( &sysInfo );

    mu_check( ( u64 ) sysInfo.lpMaximumApplicationAddress <= OS_USER_MAX_ADDR );
    mu_check( OS_COMMIT_PAGE_SIZE_IN_BYTES == sysInfo.dwPageSize );
    // NOTE: the reserve base only lands on HT_INTERNAL_ALIGNMENT because the OS hands it out there
    mu_check( OS_RESERVE_PAGE_SIZE_IN_BYTES == sysInfo.dwAllocationGranularity );
    mu_check( HT_INTERNAL_ALIGNMENT == sysInfo.dwAllocationGranularity );
    mu_check( 0 == ( BLOCK_SZ_IN_BYTES % sysInfo.dwAllocationGranularity ) );
}

MU_TEST_SUITE( ST_SuitePlatformGate )
{
    MU_RUN_TEST( ST_GatePlatformMatchesTheLayoutConstants );
}

// ============================================================================
// region layout — the block path and the dedicated bump must not overlap
// ============================================================================

MU_TEST( ST_LayoutChunkRegionIsWholeChunks )
{
    // NOTE: a partial chunk would push the bins past the cap the dedicated bump starts at
    mu_check( 0 != CHUNK_REGION_ELEM_COUNT );
    mu_check( 0 == ( CHUNK_REGION_CAP_IN_BYTES % CHUNK_SZ_IN_BYTES ) );
    mu_check( ( CHUNK_REGION_ELEM_COUNT * CHUNK_SZ_IN_BYTES ) == CHUNK_REGION_CAP_IN_BYTES );
    mu_check( ( CHUNK_REGION_CAP_IN_BYTES + DEDICATED_REGION_CAP_IN_BYTES ) == MAX_RESERVE_SZ_IN_BYTES );
}

MU_TEST( ST_LayoutBinsStopWhereTheDedicatedBumpStarts )
{
    // NOTE: the highest byte the bin scan can address, one past it is the first dedicated byte
    u64 binRegionSzInBytes = CHUNK_REGION_ELEM_COUNT * BINS_PER_CHUNK * BIN_SZ_IN_BYTES;

    mu_check( binRegionSzInBytes == CHUNK_REGION_CAP_IN_BYTES );
    mu_check( 0 == ( CHUNK_REGION_CAP_IN_BYTES % HT_INTERNAL_ALIGNMENT ) );
}

MU_TEST( ST_LayoutDedicatedBumpStartsAtTheBorder )
{
    ht_virtual_allocator fresh = {};
    mu_check( CHUNK_REGION_CAP_IN_BYTES == fresh.dedicatedAllocOffset );
    mu_check( 0 == fresh.committedInBytes );
}

MU_TEST_SUITE( ST_SuiteRegionLayout )
{
    MU_RUN_TEST( ST_LayoutChunkRegionIsWholeChunks );
    MU_RUN_TEST( ST_LayoutBinsStopWhereTheDedicatedBumpStarts );
    MU_RUN_TEST( ST_LayoutDedicatedBumpStartsAtTheBorder );
}

// ============================================================================
// chunk map addressing and the CAS reserve loop
// ============================================================================

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
    MU_ASSERT_FIRES( pBin = GetBinAt( htAllocator.chunkMap, TEST_BIN_COUNT ) );
}

MU_TEST( ST_CasLoopReserveFlipsTheMaskIn )
{
    atomic_u64 bin = 0b0011ull;
    u64 reserved = HtCASLoopReserve( &bin, []( u64 ) { return 0b1100ull; } );

    mu_check( 0b1100ull == reserved );
    mu_check( 0b1111ull == bin );
}

MU_TEST( ST_CasLoopReserveFeedsTheLiveBinToTheLambda )
{
    atomic_u64 bin = 0b0110ull;
    u64 seenByLambda = 0;
    u64 reserved = HtCASLoopReserve( &bin, [ &seenByLambda ]( u64 binVal ) { seenByLambda = binVal; return 0b1ull; } );

    mu_check( 0b0110ull == seenByLambda );
    mu_check( 0b1ull == reserved );
    mu_check( 0b0111ull == bin );
}

MU_TEST( ST_CasLoopReserveBailsOnTheInvalidMask )
{
    atomic_u64 bin = 0b1010ull;
    u64 reserved = HtCASLoopReserve( &bin, []( u64 ) { return 0ull; } );

    mu_check( 0 == reserved );
    mu_check( 0b1010ull == bin );
}

MU_TEST( ST_CasLoopReserveBailsOnAFreeBin )
{
    // NOTE: a zero bin may only be left through the commit CAS, the reserve loop has to refuse it
    atomic_u64 bin = FREE_AS_A_BIRD;
    u64 reserved = HtCASLoopReserve( &bin, []( u64 ) { return 0b1ull; } );

    mu_check( INVALID_RUN_MASK == reserved );
    mu_check( FREE_AS_A_BIRD == bin );
}

MU_TEST_SUITE( ST_SuiteChunkMap )
{
    MU_RUN_TEST( ST_GetBinAtWalksChunksThenBins );
    MU_RUN_TEST( ST_GetBinAtPastTheMapFires );
    MU_RUN_TEST( ST_CasLoopReserveFlipsTheMaskIn );
    MU_RUN_TEST( ST_CasLoopReserveFeedsTheLiveBinToTheLambda );
    MU_RUN_TEST( ST_CasLoopReserveBailsOnTheInvalidMask );
    MU_RUN_TEST( ST_CasLoopReserveBailsOnAFreeBin );
}

// ============================================================================
// HtMakeVirtualAllocator
// ============================================================================

MU_TEST( ST_MakeHandsBackTheConstantLayout )
{
    ht_virtual_allocator fresh = HtMakeVirtualAllocator();

    mu_check( CHUNK_REGION_ELEM_COUNT == std::size( fresh.chunkMap ) );
    mu_check( MAX_RESERVE_SZ_IN_BYTES == fresh.reservedInBytes );
    mu_check( CHUNK_REGION_CAP_IN_BYTES == fresh.dedicatedAllocOffset );
    mu_check( 0 == ( ( u64 ) fresh.pMemBase % HT_INTERNAL_ALIGNMENT ) );

    ht_os_virtual_release( fresh.pMemBase );
}

MU_TEST( ST_MakeHandsBackAnEmptyAllocator )
{
    ht_virtual_allocator fresh = HtMakeVirtualAllocator();
    u64 binCount = std::size( fresh.chunkMap ) * BINS_PER_CHUNK;
    for( u64 binIdx = 0; binIdx < binCount; ++binIdx )
    {
        mu_check( FREE_AS_A_BIRD == *GetBinAt( fresh.chunkMap, binIdx ) );
    }

    ht_os_virtual_release( fresh.pMemBase );
}

MU_TEST( ST_MakeKeepsTheChunkMapOutOfTheReserve )
{
    // NOTE: the map is its own OS allocation, it must not eat into the reserve
    ht_virtual_allocator fresh = HtMakeVirtualAllocator();
    u8* pMapBase = ( u8* ) std::data( fresh.chunkMap );
    u8* pBlockBase = fresh.pMemBase;

    mu_check( nullptr != pMapBase );
    mu_check( nullptr != pBlockBase );
    mu_check( ( pMapBase + std::size( fresh.chunkMap ) * sizeof( ht_virtual_chunk ) ) <= pBlockBase
        || ( pBlockBase + fresh.reservedInBytes ) <= pMapBase );

    ht_os_virtual_release( fresh.pMemBase );
}

MU_TEST_SUITE( ST_SuiteMakeAllocator )
{
    MU_RUN_TEST( ST_MakeHandsBackTheConstantLayout );
    MU_RUN_TEST( ST_MakeHandsBackAnEmptyAllocator );
    MU_RUN_TEST( ST_MakeKeepsTheChunkMapOutOfTheReserve );
}

// ============================================================================
// AllocVirtualBlock
// ============================================================================

MU_TEST( ST_AllocSingleBlocksAreContiguous )
{
    u8* pBase = htAllocator.pMemBase;

    ht_virt_alloc first = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc second = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc third = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );

    mu_check( pBase == std::data( first ) );
    mu_check( ( pBase + BLOCK_SZ_IN_BYTES ) == std::data( second ) );
    mu_check( ( pBase + 2 * BLOCK_SZ_IN_BYTES ) == std::data( third ) );
    mu_check( 1 == AllocSzInBlocks( first ) );
    mu_check( 0b111ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( first, 0 );
    htAllocator.FreeVirtualBlock( second, 0 );
    htAllocator.FreeVirtualBlock( third, 0 );
}

MU_TEST( ST_AllocMultiBlockRoundsPartialsUp )
{
    ht_virt_alloc onePastABlock = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES + 1, 0 );
    mu_check( 2 == AllocSzInBlocks( onePastABlock ) );
    mu_check( 0b11ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    ht_virt_alloc oneShortOfThree = htAllocator.AllocVirtualBlock( 3 * BLOCK_SZ_IN_BYTES - 1, 0 );
    mu_check( 3 == AllocSzInBlocks( oneShortOfThree ) );
    mu_check( 0b11111ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( onePastABlock, 0 );
    htAllocator.FreeVirtualBlock( oneShortOfThree, 0 );
}

MU_TEST( ST_AllocMaxRunTakesTheTopOfTheBlockPath )
{
    // NOTE: the biggest request the block path still serves, one byte past this is dedicated
    u8* pBase = htAllocator.pMemBase;

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( MAX_BIN_ALLOC_SZ_IN_BLOCKS * BLOCK_SZ_IN_BYTES, 0 );
    mu_check( MAX_BIN_ALLOC_SZ_IN_BLOCKS == AllocSzInBlocks( alloc ) );
    mu_check( pBase == std::data( alloc ) );
    mu_check( ( BIT_NPOS >> ( 64 - MAX_BIN_ALLOC_SZ_IN_BLOCKS ) ) == *GetBinAt( htAllocator.chunkMap, 0 ) );
    mu_check( FREE_AS_A_BIRD == *GetBinAt( htAllocator.chunkMap, 1 ) );

    htAllocator.FreeVirtualBlock( alloc, 0 );
    mu_check( FREE_AS_A_BIRD == *GetBinAt( htAllocator.chunkMap, 0 ) );
}

MU_TEST( ST_AllocMultiBlockRunsNeverCrossABin )
{
    // NOTE: a bin is one atomic word, bin 0 is one short of a max run at the top so it has to go to bin 1
    u8* pBase = htAllocator.pMemBase;
    const u64 shortTailMask = ( BIT_NPOS >> ( 64 - ( MAX_BIN_ALLOC_SZ_IN_BLOCKS - 1 ) ) )
        << ( BLOCKS_PER_BIN - ( MAX_BIN_ALLOC_SZ_IN_BLOCKS - 1 ) );
    *GetBinAt( htAllocator.chunkMap, 0 ) = ~shortTailMask;

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( MAX_BIN_ALLOC_SZ_IN_BLOCKS * BLOCK_SZ_IN_BYTES, 0 );

    mu_check( ( pBase + BLOCKS_PER_BIN * BLOCK_SZ_IN_BYTES ) == std::data( alloc ) );
    mu_check( ~shortTailMask == *GetBinAt( htAllocator.chunkMap, 0 ) );
    mu_check( ( BIT_NPOS >> ( 64 - MAX_BIN_ALLOC_SZ_IN_BLOCKS ) ) == *GetBinAt( htAllocator.chunkMap, 1 ) );

    htAllocator.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_AllocMixesSingleAndMultiBlockRuns )
{
    u8* pBase = htAllocator.pMemBase;

    ht_virt_alloc single = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc triple = htAllocator.AllocVirtualBlock( 3 * BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc pair = htAllocator.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );

    mu_check( pBase == std::data( single ) );
    mu_check( ( pBase + BLOCK_SZ_IN_BYTES ) == std::data( triple ) );
    mu_check( ( pBase + 4 * BLOCK_SZ_IN_BYTES ) == std::data( pair ) );
    mu_check( 0b111111ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    // NOTE: the hole the middle run leaves is exactly what the next 3 run takes back
    htAllocator.FreeVirtualBlock( triple, 0 );
    mu_check( 0b110001ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    ht_virt_alloc refill = htAllocator.AllocVirtualBlock( 3 * BLOCK_SZ_IN_BYTES, 0 );
    mu_check( ( pBase + BLOCK_SZ_IN_BYTES ) == std::data( refill ) );
    mu_check( 0b111111ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( single, 0 );
    htAllocator.FreeVirtualBlock( refill, 0 );
    htAllocator.FreeVirtualBlock( pair, 0 );
    mu_check( FREE_AS_A_BIRD == *GetBinAt( htAllocator.chunkMap, 0 ) );
}

MU_TEST( ST_AllocTakesTheLowestFittingHole )
{
    // NOTE: a 2 block hole at bit 3 and a 4 block hole at bit 20
    u8* pBase = htAllocator.pMemBase;
    *GetBinAt( htAllocator.chunkMap, 0 ) = ~( ( 0b11ull << 3 ) | ( 0b1111ull << 20 ) );

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );

    mu_check( ( pBase + 3 * BLOCK_SZ_IN_BYTES ) == std::data( alloc ) );
    mu_check( ~( 0b1111ull << 20 ) == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_AllocSkipsHolesThatAreTooSmall )
{
    u8* pBase = htAllocator.pMemBase;
    *GetBinAt( htAllocator.chunkMap, 0 ) = ~( ( 0b11ull << 3 ) | ( 0b1111ull << 20 ) );

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( 4 * BLOCK_SZ_IN_BYTES, 0 );

    mu_check( ( pBase + 20 * BLOCK_SZ_IN_BYTES ) == std::data( alloc ) );
    mu_check( ~( 0b11ull << 3 ) == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_AllocFitsAHoleExactly )
{
    u8* pBase = htAllocator.pMemBase;
    *GetBinAt( htAllocator.chunkMap, 0 ) = ~( 0b111ull << 10 );

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( 3 * BLOCK_SZ_IN_BYTES, 0 );

    mu_check( ( pBase + 10 * BLOCK_SZ_IN_BYTES ) == std::data( alloc ) );
    mu_check( BIT_NPOS == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( alloc, 0 );
    mu_check( ~( 0b111ull << 10 ) == *GetBinAt( htAllocator.chunkMap, 0 ) );
}

MU_TEST( ST_AllocSkipsFullBins )
{
    u8* pBase = htAllocator.pMemBase;
    *GetBinAt( htAllocator.chunkMap, 0 ) = BIT_NPOS;
    *GetBinAt( htAllocator.chunkMap, 1 ) = BIT_NPOS;
    *GetBinAt( htAllocator.chunkMap, 2 ) = BIT_NPOS;

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );

    mu_check( ( pBase + 3 * BLOCKS_PER_BIN * BLOCK_SZ_IN_BYTES ) == std::data( alloc ) );
    mu_check( 0b1ull == *GetBinAt( htAllocator.chunkMap, 3 ) );
    mu_check( BIT_NPOS == *GetBinAt( htAllocator.chunkMap, 2 ) );

    htAllocator.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_AllocReachesTheLastBlockOfTheMap )
{
    // NOTE: only the bins we never touch are marked full by hand. A bin is committed when it is
    // claimed from free, so the last one is filled block by block through the allocator instead
    u8* pBase = htAllocator.pMemBase;
    for( u64 binIdx = 0; binIdx < TEST_BIN_COUNT - 1; ++binIdx )
    {
        *GetBinAt( htAllocator.chunkMap, binIdx ) = BIT_NPOS;
    }

    ht_virt_alloc alloc = {};
    for( u64 blockIdx = 0; blockIdx < BLOCKS_PER_BIN; ++blockIdx )
    {
        alloc = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    }
    u8* pBlock = std::data( alloc );

    mu_check( BIT_NPOS == *GetBinAt( htAllocator.chunkMap, TEST_BIN_COUNT - 1 ) );
    // NOTE: the last block has to end exactly on the end of the map
    mu_check( ( pBase + ( TEST_BIN_COUNT * BLOCKS_PER_BIN - 1 ) * BLOCK_SZ_IN_BYTES ) == pBlock );
    mu_check( ( pBlock + BLOCK_SZ_IN_BYTES ) == ( pBase + TEST_MAP_SZ_IN_BYTES ) );

    pBlock[ BLOCK_SZ_IN_BYTES - 1 ] = 0xEE;
    mu_check( 0xEE == pBlock[ BLOCK_SZ_IN_BYTES - 1 ] );

    htAllocator.FreeVirtualBlock( alloc, 0 );
    mu_check( ~( 1ull << 63 ) == *GetBinAt( htAllocator.chunkMap, TEST_BIN_COUNT - 1 ) );
}

MU_TEST( ST_AllocStartsOnTheThreadsOwnChunk )
{
    u8* pBase = htAllocator.pMemBase;

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 1 );

    mu_check( ( pBase + CHUNK_SZ_IN_BYTES ) == std::data( alloc ) );
    mu_check( FREE_AS_A_BIRD == *GetBinAt( htAllocator.chunkMap, 0 ) );
    mu_check( 0b1ull == *GetBinAt( htAllocator.chunkMap, BINS_PER_CHUNK ) );

    htAllocator.FreeVirtualBlock( alloc, 1 );
    mu_check( FREE_AS_A_BIRD == *GetBinAt( htAllocator.chunkMap, BINS_PER_CHUNK ) );
}

MU_TEST( ST_AllocWrapsPastTheLastBin )
{
    u8* pBase = htAllocator.pMemBase;
    for( u64 binIdx = BINS_PER_CHUNK; binIdx < TEST_BIN_COUNT; ++binIdx )
    {
        *GetBinAt( htAllocator.chunkMap, binIdx ) = BIT_NPOS;
    }

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 1 );

    mu_check( pBase == std::data( alloc ) );
    mu_check( 0b1ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( alloc, 1 );
}

MU_TEST( ST_AllocThreadOffsetWrapsModuloBinCount )
{
    // NOTE: 2 chunks is 16 bins, thread 2 starts a full lap in and lands back on bin 0
    u8* pBase = htAllocator.pMemBase;

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, TEST_CHUNK_COUNT );

    mu_check( pBase == std::data( alloc ) );
    mu_check( 0b1ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( alloc, TEST_CHUNK_COUNT );
}

MU_TEST( ST_AllocHandsBackWritableMemory )
{
    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );
    u8* pBlock = std::data( alloc );

    pBlock[ 0 ] = 0xAB;
    pBlock[ std::size( alloc ) - 1 ] = 0xCD;
    mu_check( 0xAB == pBlock[ 0 ] );
    mu_check( 0xCD == pBlock[ std::size( alloc ) - 1 ] );

    htAllocator.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_AllocOutOfMemoryReturnsTheNullAlloc )
{
    for( u64 binIdx = 0; binIdx < TEST_BIN_COUNT; ++binIdx )
    {
        *GetBinAt( htAllocator.chunkMap, binIdx ) = BIT_NPOS;
    }

    mu_check( IsNullAlloc( htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 ) ) );
    mu_check( BIT_NPOS == *GetBinAt( htAllocator.chunkMap, 0 ) );
}

MU_TEST( ST_AllocReturnsTheNullAllocWhenNoRunIsLongEnough )
{
    // NOTE: 3 free then 1 taken all the way down, room to spare but no run of 4
    u8* pBase = htAllocator.pMemBase;
    for( u64 binIdx = 0; binIdx < TEST_BIN_COUNT; ++binIdx )
    {
        *GetBinAt( htAllocator.chunkMap, binIdx ) = 0x8888888888888888ull;
    }

    ht_virt_alloc fits = htAllocator.AllocVirtualBlock( 3 * BLOCK_SZ_IN_BYTES, 0 );
    mu_check( pBase == std::data( fits ) );
    htAllocator.FreeVirtualBlock( fits, 0 );

    // NOTE: a denial leaves every bin exactly as it found it
    mu_check( IsNullAlloc( htAllocator.AllocVirtualBlock( 4 * BLOCK_SZ_IN_BYTES, 0 ) ) );
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
    mu_check( FREE_AS_A_BIRD == *GetBinAt( htAllocator.chunkMap, 0 ) );
}

MU_TEST_SUITE( ST_SuiteAllocVirtualBlock )
{
    MU_SUITE_CONFIGURE( &ScrubPool, nullptr );

    MU_RUN_TEST( ST_AllocSingleBlocksAreContiguous );
    MU_RUN_TEST( ST_AllocMultiBlockRoundsPartialsUp );
    MU_RUN_TEST( ST_AllocMaxRunTakesTheTopOfTheBlockPath );
    MU_RUN_TEST( ST_AllocMultiBlockRunsNeverCrossABin );
    MU_RUN_TEST( ST_AllocMixesSingleAndMultiBlockRuns );
    MU_RUN_TEST( ST_AllocTakesTheLowestFittingHole );
    MU_RUN_TEST( ST_AllocSkipsHolesThatAreTooSmall );
    MU_RUN_TEST( ST_AllocFitsAHoleExactly );
    MU_RUN_TEST( ST_AllocSkipsFullBins );
    MU_RUN_TEST( ST_AllocReachesTheLastBlockOfTheMap );
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
    mu_check( FREE_AS_A_BIRD == *GetBinAt( htAllocator.chunkMap, 0 ) );
}

MU_TEST( ST_FreeClearsBitsAtTheTopOfABin )
{
    // NOTE: the run mask has to land on bits 60..63 without running off the word
    u8* pBase = htAllocator.pMemBase;
    *GetBinAt( htAllocator.chunkMap, 0 ) = ~( 0xFull << 60 );

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( 4 * BLOCK_SZ_IN_BYTES, 0 );
    mu_check( ( pBase + 60 * BLOCK_SZ_IN_BYTES ) == std::data( alloc ) );
    mu_check( BIT_NPOS == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( alloc, 0 );
    mu_check( ~( 0xFull << 60 ) == *GetBinAt( htAllocator.chunkMap, 0 ) );
}

MU_TEST( ST_FreeRecoversTheBinFromTheAddress )
{
    // NOTE: free gets nothing but the span, it has to dig out both bin and bit
    u8* pBase = htAllocator.pMemBase;
    *GetBinAt( htAllocator.chunkMap, 0 ) = BIT_NPOS;
    *GetBinAt( htAllocator.chunkMap, 1 ) = BIT_NPOS;
    *GetBinAt( htAllocator.chunkMap, 2 ) = ~( 0b11ull << 40 );

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );
    mu_check( ( pBase + ( 2 * BLOCKS_PER_BIN + 40 ) * BLOCK_SZ_IN_BYTES ) == std::data( alloc ) );
    mu_check( BIT_NPOS == *GetBinAt( htAllocator.chunkMap, 2 ) );

    htAllocator.FreeVirtualBlock( alloc, 0 );
    mu_check( ~( 0b11ull << 40 ) == *GetBinAt( htAllocator.chunkMap, 2 ) );
    mu_check( BIT_NPOS == *GetBinAt( htAllocator.chunkMap, 0 ) );
    mu_check( BIT_NPOS == *GetBinAt( htAllocator.chunkMap, 1 ) );
}

MU_TEST( ST_FreeThenAllocReusesTheSameAddress )
{
    ht_virt_alloc first = htAllocator.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );
    u8* pFirst = std::data( first );
    htAllocator.FreeVirtualBlock( first, 0 );
    mu_check( FREE_AS_A_BIRD == *GetBinAt( htAllocator.chunkMap, 0 ) );

    ht_virt_alloc second = htAllocator.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );
    u8* pBlock = std::data( second );
    mu_check( pFirst == pBlock );

    // NOTE: the free decommitted it, the realloc has to have committed it back
    pBlock[ 0 ] = 0x5A;
    mu_check( 0x5A == pBlock[ 0 ] );

    htAllocator.FreeVirtualBlock( second, 0 );
}

MU_TEST( ST_FreeOfTheNullAllocIsANoOp )
{
    *GetBinAt( htAllocator.chunkMap, 0 ) = 0b1010ull;

    htAllocator.FreeVirtualBlock( INVALID_HALLOC, 0 );
    mu_check( 0b1010ull == *GetBinAt( htAllocator.chunkMap, 0 ) );
    mu_check( 0 == htAllocator.committedInBytes );
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

// NOTE: the smallest request that walks off the block path
static constexpr u64 DEDICATED_SZ_IN_BYTES = MAX_BIN_ALLOC_SZ_IN_BLOCKS * BLOCK_SZ_IN_BYTES + 1;
static constexpr u64 DEDICATED_ALIGNED_SZ  = FwdAlignPot( DEDICATED_SZ_IN_BYTES, HT_INTERNAL_ALIGNMENT );

MU_TEST( ST_DedicatedStartsAtTheChunkBorder )
{
    // NOTE: the first dedicated byte is the first byte the bin scan can never reach
    u8* pBase = htAllocator.pMemBase;

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( DEDICATED_SZ_IN_BYTES, 0 );
    u8* pHuge = std::data( alloc );

    mu_check( ( pBase + CHUNK_REGION_CAP_IN_BYTES ) == pHuge );
    // NOTE: it lives inside the reserve, past the block region, and takes no bits
    mu_check( pHuge < ( pBase + htAllocator.reservedInBytes ) );
    mu_check( FREE_AS_A_BIRD == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_DedicatedFreeAtTheBorderTakesTheDedicatedPath )
{
    // NOTE: the alloc sits exactly on the cap, so the classify has to be >= and not >
    *GetBinAt( htAllocator.chunkMap, 0 ) = 0b1010ull;

    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( DEDICATED_SZ_IN_BYTES, 0 );
    mu_check( ( htAllocator.pMemBase + CHUNK_REGION_CAP_IN_BYTES ) == std::data( alloc ) );
    mu_check( DEDICATED_ALIGNED_SZ == htAllocator.committedInBytes );

    htAllocator.FreeVirtualBlock( alloc, 0 );

    // NOTE: a block path free would have cleared bits and dropped a whole bin off the counter
    mu_check( 0b1010ull == *GetBinAt( htAllocator.chunkMap, 0 ) );
    mu_check( 0 == htAllocator.committedInBytes );
}

MU_TEST( ST_DedicatedRoundsUpToTheInternalAlignment )
{
    ht_virt_alloc alloc = htAllocator.AllocVirtualBlock( DEDICATED_SZ_IN_BYTES, 0 );
    u8* pBytes = std::data( alloc );

    mu_check( DEDICATED_ALIGNED_SZ == std::size( alloc ) );
    mu_check( 0 == ( ( u64 ) pBytes % HT_INTERNAL_ALIGNMENT ) );

    pBytes[ 0 ] = 0x12;
    pBytes[ std::size( alloc ) - 1 ] = 0x34;
    mu_check( 0x12 == pBytes[ 0 ] );
    mu_check( 0x34 == pBytes[ std::size( alloc ) - 1 ] );

    htAllocator.FreeVirtualBlock( alloc, 0 );
}

MU_TEST( ST_DedicatedBumpNeverHandsOutTheSameRangeTwice )
{
    ht_virt_alloc first = htAllocator.AllocVirtualBlock( DEDICATED_SZ_IN_BYTES, 0 );
    ht_virt_alloc second = htAllocator.AllocVirtualBlock( DEDICATED_SZ_IN_BYTES, 0 );

    mu_check( ( std::data( first ) + std::size( first ) ) == std::data( second ) );
    mu_check( ( CHUNK_REGION_CAP_IN_BYTES + 2 * DEDICATED_ALIGNED_SZ ) == htAllocator.dedicatedAllocOffset );

    htAllocator.FreeVirtualBlock( first, 0 );
    htAllocator.FreeVirtualBlock( second, 0 );
}

MU_TEST( ST_DedicatedAndBlocksLiveSideBySide )
{
    u8* pBase = htAllocator.pMemBase;

    ht_virt_alloc single = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc pair = htAllocator.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc huge = htAllocator.AllocVirtualBlock( DEDICATED_SZ_IN_BYTES, 0 );

    // NOTE: the huge one takes no bits, so the blocks around it stay packed
    mu_check( std::data( huge ) >= ( pBase + CHUNK_REGION_CAP_IN_BYTES ) );
    mu_check( 0b111ull == *GetBinAt( htAllocator.chunkMap, 0 ) );
    htAllocator.FreeVirtualBlock( huge, 0 );

    mu_check( pBase == std::data( single ) );
    mu_check( ( pBase + BLOCK_SZ_IN_BYTES ) == std::data( pair ) );
    mu_check( 0b111ull == *GetBinAt( htAllocator.chunkMap, 0 ) );

    htAllocator.FreeVirtualBlock( single, 0 );
    htAllocator.FreeVirtualBlock( pair, 0 );
    mu_check( FREE_AS_A_BIRD == *GetBinAt( htAllocator.chunkMap, 0 ) );
}

MU_TEST_SUITE( ST_SuiteDedicatedAlloc )
{
    MU_SUITE_CONFIGURE( &ScrubPool, nullptr );

    MU_RUN_TEST( ST_DedicatedStartsAtTheChunkBorder );
    MU_RUN_TEST( ST_DedicatedFreeAtTheBorderTakesTheDedicatedPath );
    MU_RUN_TEST( ST_DedicatedRoundsUpToTheInternalAlignment );
    MU_RUN_TEST( ST_DedicatedBumpNeverHandsOutTheSameRangeTwice );
    MU_RUN_TEST( ST_DedicatedAndBlocksLiveSideBySide );
}

// ============================================================================
// committed byte accounting
// ============================================================================

MU_TEST( ST_CommitChargesOneBinPerFreshBin )
{
    // NOTE: the charge follows the os commit, not the request, so a second block in the same bin is free
    ht_virt_alloc first = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    mu_check( BIN_SZ_IN_BYTES == htAllocator.committedInBytes );

    ht_virt_alloc second = htAllocator.AllocVirtualBlock( 2 * BLOCK_SZ_IN_BYTES, 0 );
    mu_check( BIN_SZ_IN_BYTES == htAllocator.committedInBytes );

    // NOTE: the bin is still live, nothing was decommitted so nothing comes off
    htAllocator.FreeVirtualBlock( first, 0 );
    mu_check( BIN_SZ_IN_BYTES == htAllocator.committedInBytes );

    htAllocator.FreeVirtualBlock( second, 0 );
    mu_check( 0 == htAllocator.committedInBytes );
}

MU_TEST( ST_CommitChargesEveryBinItTouches )
{
    ht_virt_alloc first = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    ht_virt_alloc second = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 1 );
    mu_check( ( 2 * BIN_SZ_IN_BYTES ) == htAllocator.committedInBytes );

    htAllocator.FreeVirtualBlock( first, 0 );
    mu_check( BIN_SZ_IN_BYTES == htAllocator.committedInBytes );

    htAllocator.FreeVirtualBlock( second, 1 );
    mu_check( 0 == htAllocator.committedInBytes );
}

MU_TEST( ST_CommitChargesDedicatedBySize )
{
    ht_virt_alloc huge = htAllocator.AllocVirtualBlock( DEDICATED_SZ_IN_BYTES, 0 );
    mu_check( DEDICATED_ALIGNED_SZ == htAllocator.committedInBytes );

    ht_virt_alloc block = htAllocator.AllocVirtualBlock( BLOCK_SZ_IN_BYTES, 0 );
    mu_check( ( DEDICATED_ALIGNED_SZ + BIN_SZ_IN_BYTES ) == htAllocator.committedInBytes );

    htAllocator.FreeVirtualBlock( huge, 0 );
    mu_check( BIN_SZ_IN_BYTES == htAllocator.committedInBytes );

    htAllocator.FreeVirtualBlock( block, 0 );
    mu_check( 0 == htAllocator.committedInBytes );
}

MU_TEST_SUITE( ST_SuiteCommitAccounting )
{
    MU_SUITE_CONFIGURE( &ScrubPool, nullptr );

    MU_RUN_TEST( ST_CommitChargesOneBinPerFreshBin );
    MU_RUN_TEST( ST_CommitChargesEveryBinItTouches );
    MU_RUN_TEST( ST_CommitChargesDedicatedBySize );
}

// ============================================================================
// main
// ============================================================================

i32 main()
{
    // NOTE: the layout constants cannot survive an OS with a different granularity, everything
    // past the gate would fail for that one reason so we report what we have and walk away
    i32 failsBeforeTheGate = minunit_fail;
    MU_RUN_SUITE( ST_SuitePlatformGate );
    [[ unlikely ]]
    if( failsBeforeTheGate != minunit_fail )
    {
        MU_REPORT();
        return MU_EXIT_CODE;
    }

    MU_RUN_SUITE( ST_SuiteRegionLayout );
    MU_RUN_SUITE( ST_SuiteChunkMap );
    MU_RUN_SUITE( ST_SuiteMakeAllocator );
    MU_RUN_SUITE( ST_SuiteAllocVirtualBlock );
    MU_RUN_SUITE( ST_SuiteFreeVirtualBlock );
    MU_RUN_SUITE( ST_SuiteDedicatedAlloc );
    MU_RUN_SUITE( ST_SuiteCommitAccounting );

    MU_REPORT();
    return MU_EXIT_CODE;
}