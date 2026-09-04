#include <ht_core_types.h>

#include <ht_math.h>
#include <ht_utils.h>

#include <ht_error.h>
#include <System/sys_thread.h>
#include <System/sys_timer.h>

#include <ht_memory.h>

#include <array>
#include <iostream>

constexpr u64 NUM_THREADS           = 8;
constexpr u64 THREAD_IDX_BIT_WIDTH  = std::bit_width( NUM_THREADS );

static ht_virtual_allocator* pHtAllocator = nullptr;

// NOTE: we need to size it as
// liveBlocks = NUM_THREADS * THREAD_WORK_ITEMS * meanFootprintInBlocks
// poolBlocks = chunkCount * BINS_PER_CHUNK * BLOCKS_PER_BIN
// if the pool is too empty the CAS will not conflict
// if pool is too full we're gonna hammer on the OOM recycling path
// NOTE: we need to draw from a distro bc uniform will have us alloc
// larger stuff more often which gets us to "TOO FULL"
// NOTE: 1 / k^2 over [ 1, 4 ] means a mean footprint of ~1.46 blocks, so
// 8 * 64 * 1.46 = ~749 live blocks against a 1024 block pool, ~73% full
constexpr u64 MAX_POOL_SIZE     = 64 * MB;
constexpr u64 THREAD_WORK_ITEMS = 64;

// NOTE: the chunk map is ours, not the one HtMakeVirtualAllocator hands out. Its 8 GB of bins
// would leave the pool at a fraction of a percent full and nothing would ever contend.
constexpr u64 STRESS_CHUNK_COUNT = MAX_POOL_SIZE / CHUNK_SZ_IN_BYTES;
static_assert( 0 != STRESS_CHUNK_COUNT );
static_assert( ( STRESS_CHUNK_COUNT * CHUNK_SZ_IN_BYTES ) == MAX_POOL_SIZE );

static std::array<ht_virtual_chunk, STRESS_CHUNK_COUNT> stressChunkMap = {};

// NOTE: the block path caps out here, anything past it comes straight from the OS as a dedicated
// alloc and takes no bits, which is not what this test is hammering
constexpr u64   MAX_FOOTPRINT_IN_BLOCKS = MAX_BIN_ALLOC_SZ_IN_BLOCKS;
constexpr float POW_DISTRO_A            = 1.0f;
constexpr float POW_DISTRO_AB           = 1.0f - 1.0f / ( MAX_FOOTPRINT_IN_BLOCKS + 1.0f );

struct stamp_t
{
    u64 loopCount   : 64 - THREAD_IDX_BIT_WIDTH;
    u64 threadIdx   : THREAD_IDX_BIT_WIDTH;

    bool operator==( const stamp_t& ) const = default;
};

struct tagged_alloc
{
    ht_virt_alloc   alloc;
    stamp_t         stamp;
};

struct alignas( 64 ) thread_stats_t
{
    u64 iters;
    u32 allocs;
    u32 frees;
    u32 oom;
};

static thread_stats_t threadStats[ NUM_THREADS ] = {};

alignas( 64 ) static atomic_u64 threadCounter   = 0;
alignas( 64 ) static atomic_u64 exitSignal      = 0;

constexpr u64 EXIT_SIGNAL = 1;

static bool IsNullAlloc( ht_virt_alloc alloc )
{
    return ( nullptr == std::data( alloc ) ) && ( 0 == std::size( alloc ) );
}

u32 ThreadMainLoop( void* pData )
{
    HT_ASSERT( pHtAllocator );

    u64 threadIdx = ( u64 ) pData;

    u32 randSeq = HwRandSeed32();

    std::array<tagged_alloc, THREAD_WORK_ITEMS> allocs = {};
    u64                                         allocCount = 0;

    for( ;; )
    {
        if( EXIT_SIGNAL == SysAtomicRead64<sys_fence_t::NONE>( &exitSignal ) ) break;
        threadStats[ threadIdx ].iters++;

        randSeq = PcgHash32( randSeq );
        if( THREAD_WORK_ITEMS == allocCount )
        {
            u32          itemIdx = randSeq % allocCount;
            std::swap( allocs[ itemIdx ], allocs[ allocCount - 1 ] );

            const tagged_alloc victim = allocs[ --allocCount ];
            HT_ASSERT( std::size( victim.alloc ) <= ( MAX_BIN_ALLOC_SZ_IN_BLOCKS * BLOCK_SZ_IN_BYTES ) );
            // NOTE: every block carries the stamp, so a stranger landing anywhere in the run trips
            for( u64 blockIdx = 0, blockCount = std::size( victim.alloc ) / BLOCK_SZ_IN_BYTES;
                blockIdx < blockCount; ++blockIdx )
            {
                HT_ASSERT( victim.stamp == *( stamp_t* )(
                    std::data( victim.alloc ) + blockIdx * BLOCK_SZ_IN_BYTES ) );
            }
            // NOTE: intentionally use thread 0 to force contention
            pHtAllocator->FreeVirtualBlock( victim.alloc, 0 );
            threadStats[ threadIdx ].frees++;
        }
        else
        {
            u64 footprint = PowDistroCDF(
                randSeq, POW_DISTRO_A, POW_DISTRO_AB, MAX_FOOTPRINT_IN_BLOCKS ) * BLOCK_SZ_IN_BYTES;
            // NOTE: intentionally use thread 0 to force contention
            ht_virt_alloc alloc = pHtAllocator->AllocVirtualBlock( footprint, 0 );

            if( IsNullAlloc( alloc ) )
            {
                if( 0 != allocCount )
                {
                    const tagged_alloc victim = allocs[ --allocCount ];
                    HT_ASSERT( std::size( victim.alloc ) <= ( MAX_BIN_ALLOC_SZ_IN_BLOCKS * BLOCK_SZ_IN_BYTES ) );

                    for( u64 blockIdx = 0, blockCount = std::size( victim.alloc ) / BLOCK_SZ_IN_BYTES;
                        blockIdx < blockCount; ++blockIdx )
                    {
                        HT_ASSERT( victim.stamp == *( stamp_t* )(
                            std::data( victim.alloc ) + blockIdx * BLOCK_SZ_IN_BYTES ) );
                    }
                    pHtAllocator->FreeVirtualBlock( victim.alloc, 0 );
                    threadStats[ threadIdx ].frees++;
                }
                threadStats[ threadIdx ].oom++;
                continue;
            }

            HT_ASSERT( std::size( alloc ) <= ( MAX_BIN_ALLOC_SZ_IN_BLOCKS * BLOCK_SZ_IN_BYTES ) );
            stamp_t stamp = { .loopCount = threadStats[ threadIdx ].iters, .threadIdx = threadIdx };
            for( u64 blockIdx = 0; blockIdx < ( std::size( alloc ) / BLOCK_SZ_IN_BYTES ); ++blockIdx )
            {
                *( stamp_t* )( std::data( alloc ) + blockIdx * BLOCK_SZ_IN_BYTES ) = stamp;
            }
            allocs[ allocCount++ ] = { .alloc = alloc, .stamp = stamp };
            threadStats[ threadIdx ].allocs++;
        }
    }

    // NOTE: drain the bins we're holding
    while( 0 != allocCount )
    {
        const tagged_alloc victim = allocs[ --allocCount ];
        for( u64 blockIdx = 0; blockIdx < ( std::size( victim.alloc ) / BLOCK_SZ_IN_BYTES ); ++blockIdx )
        {
            HT_ASSERT( victim.stamp == *( stamp_t* )( std::data( victim.alloc ) + blockIdx * BLOCK_SZ_IN_BYTES ) );
        }
        pHtAllocator->FreeVirtualBlock( victim.alloc, 0 );
        threadStats[ threadIdx ].frees++;
    }

    SysAtomicAdd64<sys_fence_t::NONE>( &threadCounter, 1 );
    return 0;
}

constexpr u64 RUNTIME_SECS = 100;

i32 main()
{
    ht_virtual_allocator htAllocator = {
        .pMemBase           = ( u8* ) ht_os_virtual_reserve( MAX_RESERVE_SZ_IN_BYTES ),
        .reservedInBytes    = MAX_RESERVE_SZ_IN_BYTES,
        .chunkMap           = stressChunkMap
    };
    pHtAllocator = &htAllocator;

    sys_thread threadPool[ NUM_THREADS ] = {};
    for( u64 threadIdx = 0; threadIdx < NUM_THREADS; ++threadIdx )
    {
        threadPool[ threadIdx ] = SysCreateThread( 1 * MB, ThreadMainLoop, ( void* ) threadIdx, nullptr );
    }

    const float	cpuPeriod = 1.0f / float( SysGetCpuFreq() );
    u64         startTime = SysTicks();

    for( bool switchWait = false; ; )
    {
        if( !switchWait )
        {
            if( RUNTIME_SECS <= ( SysTicks() - startTime ) * cpuPeriod )
            {
                SysAtomicWrite64<sys_fence_t::NONE>( &exitSignal, EXIT_SIGNAL );
                switchWait = true;
            }
        }
        else
        {
            if( NUM_THREADS == SysAtomicRead64<sys_fence_t::NONE>( &threadCounter ) ) break;
        }

        SysThreadSleep( 16 );
    }

    u64 totalIters  = 0;
    u64 totalAllocs = 0;
    u64 totalFrees  = 0;
    u64 totalOom    = 0;

    std::cout << "thread |      iters |     allocs |      frees |        oom\n";
    for( u64 threadIdx = 0; threadIdx < NUM_THREADS; ++threadIdx )
    {
        const thread_stats_t& stats = threadStats[ threadIdx ];

        std::cout << std::format( "{:6} | {:10} | {:10} | {:10} | {:10}\n",
            threadIdx, stats.iters, stats.allocs, stats.frees, stats.oom );

        totalIters  += stats.iters;
        totalAllocs += stats.allocs;
        totalFrees  += stats.frees;
        totalOom    += stats.oom;
    }

    std::cout << std::format( "total  | {:10} | {:10} | {:10} | {:10}\n",
        totalIters, totalAllocs, totalFrees, totalOom );

    std::cout << std::format( "committed: {} bytes, dedicated offset: {}\n",
        ( u64 ) htAllocator.committedInBytes, ( u64 ) htAllocator.dedicatedAllocOffset );

    return 0;
}