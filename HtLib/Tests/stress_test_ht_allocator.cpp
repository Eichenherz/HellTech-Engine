#include <ht_core_types.h>
#include <ht_utils.h>
#include <ht_error.h>
#include <System/sys_thread.h>
#include <System/sys_timer.h>
// NOTE: we included this just bc we wanna not deal with shit rn
#include "../Lib/ht_renderer_types.h"
#include <ht_math.h>
#include <immintrin.h>
#include <ht_memory.h>

#include <ht_fixed_vector.h>

constexpr u64 NUM_THREADS           = 8;
constexpr u64 THREAD_IDX_BIT_WIDTH  = std::bit_width( NUM_THREADS );

static ht_virtual_allocator* pHtAllocator = nullptr;

u32 HwRandSeed32()
{
    u32 seed = 0;
    for( u64 tryIdx = 0; tryIdx < 4; ++tryIdx )
    {
        if( 1 == _rdseed32_step( &seed ) ) break;
    }
    return seed;
}

// NOTE: we need to size it as
// liveBlocks = NUM_THREADS * THREAD_WORK_ITEMS * meanFootprintInBlocks
// poolBlocks = chunkCount * BINS_PER_CHUNK * BLOCKS_PER_BIN
// if the pool is too empty the CAS will not conflict
// if pool is too full we're gonna hammer on the OOM recycling path
// NOTE: we need to draw from a distro bc uniform will have us alloc
// larger stuff more often which gets us to "TOO FULL"
constexpr u64 MAX_POOL_SIZE     = 12 * MB;
constexpr u64 THREAD_WORK_ITEMS = 64;

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

u32 ThreadMainLoop( void* pData )
{
    HT_ASSERT( pHtAllocator );

    u64 threadIdx = ( u64 ) pData;

    u32 randSeq = HwRandSeed32();

    fixed_vector<tagged_alloc, THREAD_WORK_ITEMS> allocs = {};

    for( ;; )
    {
        if( EXIT_SIGNAL == SysAtomicRead64<sys_split_barrier_t::NO_FENCE>( &exitSignal ) ) break;
        threadStats[ threadIdx ].iters++;

        randSeq = PcgHash( randSeq );
        if( u64 allocSz = std::size( allocs ); THREAD_WORK_ITEMS == allocSz )
        {
            u32          itemIdx = randSeq % allocSz;
            std::swap( allocs[ itemIdx ], allocs[ allocSz - 1 ] );

            const tagged_alloc victim = allocs.pop_back();
            auto[ pMem, meta ] = HtUnpackVirtualAllocation( victim.alloc );
            // NOTE: every block carries the stamp, so a stranger landing anywhere in the run trips
            for( u64 blockIdx = 0; blockIdx < meta.blockCount; ++blockIdx )
            {
                HT_ASSERT( victim.stamp == *( stamp_t* )( ( u8* ) pMem + blockIdx * BLOCK_SZ_IN_BYTES ) );
            }
            // NOTE: intentionally use thread 0 to force contention
            pHtAllocator->FreeVirtualBlock( victim.alloc, 0 );
            threadStats[ threadIdx ].frees++;
        }
        else
        {
            u64 footprint = PowDistroCDF( randSeq, 1.0f, 1.0f - 1.0f / 65.0f, 64 ) * BLOCK_SZ_IN_BYTES;
            // NOTE: intentionally use thread 0 to force contention
            ht_virt_alloc alloc = pHtAllocator->AllocVirtualBlock( footprint, 0 );

            if( ht_virt_alloc{} == alloc )
            {
                if( std::size( allocs ) )
                {
                    const tagged_alloc victim = allocs.pop_back();

                    auto[ pMem, meta ] = HtUnpackVirtualAllocation( victim.alloc );
                    for( u64 blockIdx = 0; blockIdx < meta.blockCount; ++blockIdx )
                    {
                        HT_ASSERT( victim.stamp == *( stamp_t* )( ( u8* ) pMem + blockIdx * BLOCK_SZ_IN_BYTES ) );
                    }
                    pHtAllocator->FreeVirtualBlock( victim.alloc, 0 );
                    threadStats[ threadIdx ].frees++;
                }
                threadStats[ threadIdx ].oom++;
                continue;
            }

            stamp_t stamp = { .loopCount = threadStats[ threadIdx ].iters, .threadIdx = threadIdx };
            auto[ pMem, meta ] = HtUnpackVirtualAllocation( alloc );
            for( u64 blockIdx = 0; blockIdx < meta.blockCount; ++blockIdx )
            {
                *( stamp_t* )( ( u8* ) pMem + blockIdx * BLOCK_SZ_IN_BYTES ) = stamp;
            }
            allocs.push_back( { .alloc = alloc, .stamp = stamp } );
            threadStats[ threadIdx ].allocs++;
        }
    }

    SysAtomicAdd64<sys_split_barrier_t::NO_FENCE>( &threadCounter, 1 );
    return 0;
}

constexpr u64 RUNTIME_SECS = 100;

i32 main()
{
    ht_virtual_allocator htAllocator = HtMakeAllocator( MAX_POOL_SIZE );
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
                SysAtomicWrite64<sys_split_barrier_t::NO_FENCE>( &exitSignal, EXIT_SIGNAL );
                switchWait = true;
            }
        }
        else
        {
            if( NUM_THREADS == SysAtomicRead64<sys_split_barrier_t::NO_FENCE>( &threadCounter ) ) break;
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

    return 0;
}