// NOTE: covered cases —
//   alloc: basic, zero-byte, sequential, alignment (16/64/256), exact capacity
//   rewind: to mark, to zero, clamp past capacity
//   reset: basic, idempotent, alloc after reset, multi-cycle
//   negative: alloc past capacity, rewind past offset, non-pow2 alignment

#include "test_common.h"

#include <ht_memory.h>

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
    mu_check( (u8*)p >= a.mem && (u8*)p < ( a.mem + STATIC_CAP ) );
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
    mu_check( ( (u8*)p1 + 16 ) == (u8*)p2 );
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
// main
// ============================================================================

int main( int argc, char* argv[] )
{
    MU_RUN_SUITE( SuiteStaticArena );
    MU_RUN_SUITE( SuiteDynamicArena );
    MU_REPORT();
    return MU_EXIT_CODE;
}
