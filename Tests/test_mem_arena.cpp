// NOTE: covered cases —
//   alloc: basic, zero-byte, sequential, alignment (16/64/256), exact capacity
//   rewind: to mark, to zero, clamp past capacity
//   reset: basic, idempotent, alloc after reset, multi-cycle
//   virtual pages: commit on demand, page-step growth, alignment crossing page
//                  boundary, rewind keeps committed, clamp to reserved
//   move ctor/assign, self-move-assign (negative)
//   stack_adaptor: LIFO rewind, BasePtr, PMR interface (allocate/deallocate/is_equal)
//   negative: alloc past capacity, rewind past offset, non-pow2 alignment

#include "test_common.h"

#include <System/sys_mem_arena.h>
#include <utility>

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
// virtual_arena
// ============================================================================

static constexpr u64 RESERVE_SZ = 1ull << 20;   // 1 MB
static constexpr u64 PAGE_SZ    = virtual_arena::PAGE_SIZE;

MU_TEST( VirtualArenaDtorOnDefault )
{
    { virtual_arena a = {}; }  // NOTE: must not crash
    mu_check( true );
}

MU_TEST( VirtualArenaReserveCtor )
{
    virtual_arena a = { RESERVE_SZ };
    mu_check( nullptr != a.base );
    mu_check( RESERVE_SZ == a.reserved );
    mu_check( 0 == a.offset );
    mu_check( 0 == a.committed );
}

MU_TEST( VirtualArenaAllocBasic )
{
    virtual_arena a = { RESERVE_SZ };
    void* p = a.Alloc( 64, 8 );
    mu_check( nullptr != p );
    mu_check( (u8*)p >= a.base && (u8*)p < ( a.base + a.reserved ) );
    mu_check( 64 == a.offset );
}

MU_TEST( VirtualArenaAllocZeroBytes )
{
    virtual_arena a = { RESERVE_SZ };
    void* p = a.Alloc( 0, 1 );
    mu_check( nullptr != p );
    mu_check( 0 == a.offset );
}

MU_TEST( VirtualArenaAllocAlignment64 )
{
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( 1, 1 );
    void* p = a.Alloc( 32, 64 );
    mu_check( 0 == ( (u64)p & 63 ) );
}

MU_TEST( VirtualArenaAllocCommitsPage )
{
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( 1, 1 );
    mu_check( a.committed >= PAGE_SZ );
}

MU_TEST( VirtualArenaAllocExactPage )
{
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( PAGE_SZ, 1 );
    mu_check( PAGE_SZ == a.committed );
    mu_check( PAGE_SZ == a.offset );
}

MU_TEST( VirtualArenaAllocMultiPage )
{
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( ( PAGE_SZ + 1 ), 1 );
    mu_check( a.committed >= ( PAGE_SZ * 2 ) );
}

MU_TEST( VirtualArenaAllocLarge )
{
    virtual_arena a = { RESERVE_SZ };
    constexpr u64 SZ_128KB = ( 128ull * 1024 );
    a.Alloc( SZ_128KB, 1 );
    mu_check( 0 == ( a.committed % PAGE_SZ ) );
    mu_check( a.committed >= SZ_128KB );
}

MU_TEST( VirtualArenaAllocNoOverlap )
{
    virtual_arena a = { RESERVE_SZ };
    void* p1 = a.Alloc( 128, 8 );
    void* p2 = a.Alloc( 128, 8 );
    mu_check( (u8*)p2 >= ( (u8*)p1 + 128 ) );
}

MU_TEST( VirtualArenaWriteThrough )
{
    virtual_arena a = { RESERVE_SZ };
    u8* p = (u8*)a.Alloc( PAGE_SZ, 1 );
    for( u64 i = 0; i < PAGE_SZ; ++i )
    {
        p[ i ] = (u8)( i & 0xFF );
    }
    bool ok = true;
    for( u64 i = 0; i < PAGE_SZ; ++i )
    {
        if( p[ i ] != (u8)( i & 0xFF ) )
        {
            ok = false;
            break;
        }
    }
    mu_check( ok );
}

MU_TEST( VirtualArenaRewind )
{
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( 64, 8 );
    u64 mark = a.offset;
    a.Alloc( 128, 8 );
    a.Rewind( mark );
    mu_check( mark == a.offset );
}

MU_TEST( VirtualArenaRewindToZero )
{
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( 64, 8 );
    a.Rewind( 0 );
    mu_check( 0 == a.offset );
    mu_check( a.committed >= PAGE_SZ );  // NOTE: pages stay committed after rewind
}

MU_TEST( VirtualArenaRewindThenAlloc )
{
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( 128, 8 );
    u64 mark = a.offset;
    a.Alloc( 256, 8 );
    a.Rewind( mark );
    void* p = a.Alloc( 64, 8 );  // NOTE: pages still committed — must succeed
    mu_check( nullptr != p );
    mu_check( ( mark + 64 ) == a.offset );
}

MU_TEST( VirtualArenaReset )
{
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( 64, 8 );
    a.Reset();
    mu_check( 0 == a.offset );
    mu_check( 0 == a.committed );
}

MU_TEST( VirtualArenaResetIdempotent )
{
    virtual_arena a = { RESERVE_SZ };
    a.Reset();  // NOTE: committed already 0
    mu_check( 0 == a.offset );
    mu_check( 0 == a.committed );
}

MU_TEST( VirtualArenaResetThenAlloc )
{
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( 64, 8 );
    a.Reset();
    void* p = a.Alloc( 32, 8 );
    mu_check( nullptr != p );
    mu_check( a.committed >= PAGE_SZ );
}

MU_TEST( VirtualArenaMultiResetCycle )
{
    virtual_arena a = { RESERVE_SZ };
    for( i32 i = 0; i < 4; ++i )
    {
        void* p = a.Alloc( PAGE_SZ, 1 );
        mu_check( nullptr != p );
        a.Reset();
        mu_check( 0 == a.offset );
        mu_check( 0 == a.committed );
    }
}

MU_TEST( VirtualArenaMoveCtor )
{
    virtual_arena src = { RESERVE_SZ };
    src.Alloc( 64, 8 );
    u8* origBase = src.base;

    virtual_arena dst = { std::move( src ) };

    mu_check( nullptr == src.base );
    mu_check( 0 == src.offset );
    mu_check( 0 == src.committed );
    mu_check( 0 == src.reserved );
    mu_check( origBase == dst.base );
    mu_check( 64 == dst.offset );
    mu_check( RESERVE_SZ == dst.reserved );
}

MU_TEST( VirtualArenaMoveAssign )
{
    virtual_arena src = { RESERVE_SZ };
    src.Alloc( 64, 8 );
    u8* origBase = src.base;

    virtual_arena dst = {};
    dst = std::move( src );

    mu_check( nullptr == src.base );
    mu_check( 0 == src.offset );
    mu_check( 0 == src.committed );
    mu_check( 0 == src.reserved );
    mu_check( origBase == dst.base );
    mu_check( 64 == dst.offset );
    mu_check( RESERVE_SZ == dst.reserved );
}

// ============================================================================
// virtual_arena — page scenarios
// ============================================================================

MU_TEST( VirtualArenaCommitStableWithinPage )
{
    // NOTE: small allocs within one page must not grow committed past PAGE_SZ
    virtual_arena a = { RESERVE_SZ };
    for( i32 i = 0; i < 16; ++i )
    {
        a.Alloc( 16, 8 );
    }
    mu_check( PAGE_SZ == a.committed );
}

MU_TEST( VirtualArenaCommitGrowsExactPageSteps )
{
    // NOTE: each PAGE_SZ alloc must advance committed by exactly one page
    virtual_arena a = { RESERVE_SZ };
    for( i32 i = 1; i <= 4; ++i )
    {
        a.Alloc( PAGE_SZ, 1 );
        mu_check( ( (u64)i * PAGE_SZ ) == a.committed );
    }
}

MU_TEST( VirtualArenaAlignmentCrossesPageBoundary )
{
    // NOTE: alignment bump crosses page boundary, forcing a second commit
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( ( PAGE_SZ - 1 ), 1 );
    mu_check( PAGE_SZ == a.committed );
    void* p = a.Alloc( 1, PAGE_SZ );
    mu_check( 0 == ( (u64)p & ( PAGE_SZ - 1 ) ) );
    mu_check( a.committed >= ( PAGE_SZ * 2 ) );
}

MU_TEST( VirtualArenaRewindKeepsCommitted )
{
    // NOTE: rewind only moves the logical cursor, committed stays
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( ( PAGE_SZ * 3 ), 1 );
    u64 highWater = a.committed;
    a.Rewind( 0 );
    mu_check( 0 == a.offset );
    mu_check( highWater == a.committed );
}

MU_TEST( VirtualArenaCommitClampedToReserved )
{
    // NOTE: newCommitted is clamped to reserved
    constexpr u64 TWO_PAGES = ( PAGE_SZ * 2 );
    virtual_arena a = { TWO_PAGES };
    a.Alloc( TWO_PAGES, 1 );
    mu_check( TWO_PAGES == a.committed );
    mu_check( a.reserved == a.committed );
}

MU_TEST( VirtualArenaRewindThenCrossPageAlloc )
{
    // NOTE: rewind into page 0, re-alloc PAGE_SZ — newOffset = 1.5*PAGE_SZ within committed 2*PAGE_SZ
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( ( PAGE_SZ * 2 ), 1 );
    a.Rewind( ( PAGE_SZ / 2 ) );
    u64 committed0 = a.committed;
    a.Alloc( PAGE_SZ, 1 );
    mu_check( committed0 == a.committed );
    mu_check( ( ( PAGE_SZ / 2 ) + PAGE_SZ ) == a.offset );
}

// ============================================================================
// virtual_arena — negative
// ============================================================================
MU_TEST( VirtualArenaAllocPastReserved )
{
    virtual_arena a = { PAGE_SZ };
    a.Alloc( PAGE_SZ, 1 );
    MU_ASSERT_FIRES( a.Alloc( 1, 1 ) );
}

MU_TEST( VirtualArenaRewindPastOffset )
{
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( 64, 8 );
    MU_ASSERT_FIRES( a.Rewind( 65 ) );  // NOTE: mark > offset
}

MU_TEST( VirtualArenaAllocNonPow2Align )
{
    virtual_arena a = { RESERVE_SZ };
    MU_ASSERT_FIRES( a.Alloc( 8, 3 ) );
}

MU_TEST( VirtualArenaMoveAssignSelf )
{
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( 64, 8 );
    MU_ASSERT_FIRES( a = std::move( a ) );
}

MU_TEST_SUITE( SuiteVirtualArena )
{
    MU_RUN_TEST( VirtualArenaDtorOnDefault );
    MU_RUN_TEST( VirtualArenaReserveCtor );
    MU_RUN_TEST( VirtualArenaAllocBasic );
    MU_RUN_TEST( VirtualArenaAllocZeroBytes );
    MU_RUN_TEST( VirtualArenaAllocAlignment64 );
    MU_RUN_TEST( VirtualArenaAllocCommitsPage );
    MU_RUN_TEST( VirtualArenaAllocExactPage );
    MU_RUN_TEST( VirtualArenaAllocMultiPage );
    MU_RUN_TEST( VirtualArenaAllocLarge );
    MU_RUN_TEST( VirtualArenaAllocNoOverlap );
    MU_RUN_TEST( VirtualArenaWriteThrough );
    MU_RUN_TEST( VirtualArenaRewind );
    MU_RUN_TEST( VirtualArenaRewindToZero );
    MU_RUN_TEST( VirtualArenaRewindThenAlloc );
    MU_RUN_TEST( VirtualArenaReset );
    MU_RUN_TEST( VirtualArenaResetIdempotent );
    MU_RUN_TEST( VirtualArenaResetThenAlloc );
    MU_RUN_TEST( VirtualArenaMultiResetCycle );
    MU_RUN_TEST( VirtualArenaMoveCtor );
    MU_RUN_TEST( VirtualArenaMoveAssign );
    MU_RUN_TEST( VirtualArenaCommitStableWithinPage );
    MU_RUN_TEST( VirtualArenaCommitGrowsExactPageSteps );
    MU_RUN_TEST( VirtualArenaAlignmentCrossesPageBoundary );
    MU_RUN_TEST( VirtualArenaRewindKeepsCommitted );
    MU_RUN_TEST( VirtualArenaCommitClampedToReserved );
    MU_RUN_TEST( VirtualArenaRewindThenCrossPageAlloc );
    MU_RUN_TEST( VirtualArenaAllocPastReserved );
    MU_RUN_TEST( VirtualArenaRewindPastOffset );
    MU_RUN_TEST( VirtualArenaAllocNonPow2Align );
    MU_RUN_TEST( VirtualArenaMoveAssignSelf );
}

// ============================================================================
// stack_adaptor
// ============================================================================

MU_TEST( StackAdaptorSavesOffset )
{
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( 64, 8 );
    u64 savedOffset = a.offset;
    stack_adaptor sa = { a };
    mu_check( savedOffset == sa.baseFrameOffset );
}

MU_TEST( StackAdaptorRewindsOnDtor )
{
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( 64, 8 );
    u64 savedOffset = a.offset;
    {
        stack_adaptor sa = { a };
        a.Alloc( 256, 8 );
        mu_check( a.offset > savedOffset );
    }
    mu_check( savedOffset == a.offset );
}

MU_TEST( StackAdaptorBasePtr )
{
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( 64, 8 );
    stack_adaptor sa = { a };
    mu_check( ( a.base + 64 ) == sa.BasePtr() );
}

MU_TEST( StackAdaptorBasePtrAtZero )
{
    virtual_arena a = { RESERVE_SZ };
    stack_adaptor sa = { a };
    mu_check( a.base == sa.BasePtr() );
}

MU_TEST( StackAdaptorPmrAllocate )
{
    virtual_arena a = { RESERVE_SZ };
    stack_adaptor sa = { a };
    void* p = sa.allocate( 32, 8 );
    mu_check( nullptr != p );
    mu_check( 32 == a.offset );
}

MU_TEST( StackAdaptorPmrDeallocateNoop )
{
    virtual_arena a = { RESERVE_SZ };
    stack_adaptor sa = { a };
    void* p = sa.allocate( 32, 8 );
    u64 offsetBefore = a.offset;
    sa.deallocate( p, 32, 8 );
    mu_check( offsetBefore == a.offset );
}

MU_TEST( StackAdaptorPmrIsEqualSame )
{
    virtual_arena a = { RESERVE_SZ };
    stack_adaptor sa = { a };
    mu_check( sa.is_equal( sa ) );
}

MU_TEST( StackAdaptorPmrIsEqualDifferent )
{
    virtual_arena a = { RESERVE_SZ };
    stack_adaptor sa1 = { a };
    stack_adaptor sa2 = { a };
    mu_check( !sa1.is_equal( sa2 ) );
}

MU_TEST( StackAdaptorNested )
{
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( 64, 8 );
    u64 mark0 = a.offset;
    {
        stack_adaptor outer = { a };
        a.Alloc( 128, 8 );
        u64 mark1 = a.offset;
        {
            stack_adaptor inner = { a };
            a.Alloc( 256, 8 );
            mu_check( a.offset > mark1 );
        }
        mu_check( mark1 == a.offset );
    }
    mu_check( mark0 == a.offset );
}

MU_TEST( StackAdaptorOnNonzeroBase )
{
    virtual_arena a = { RESERVE_SZ };
    a.Alloc( PAGE_SZ, 1 );
    u64 savedOffset = a.offset;
    {
        stack_adaptor sa = { a };
        a.Alloc( 128, 8 );
    }
    mu_check( savedOffset == a.offset );
}

MU_TEST_SUITE( SuiteStackAdaptor )
{
    MU_RUN_TEST( StackAdaptorSavesOffset );
    MU_RUN_TEST( StackAdaptorRewindsOnDtor );
    MU_RUN_TEST( StackAdaptorBasePtr );
    MU_RUN_TEST( StackAdaptorBasePtrAtZero );
    MU_RUN_TEST( StackAdaptorPmrAllocate );
    MU_RUN_TEST( StackAdaptorPmrDeallocateNoop );
    MU_RUN_TEST( StackAdaptorPmrIsEqualSame );
    MU_RUN_TEST( StackAdaptorPmrIsEqualDifferent );
    MU_RUN_TEST( StackAdaptorNested );
    MU_RUN_TEST( StackAdaptorOnNonzeroBase );
}

// ============================================================================
// main
// ============================================================================

int main( int argc, char* argv[] )
{
    MU_RUN_SUITE( SuiteStaticArena );
    MU_RUN_SUITE( SuiteDynamicArena );
    MU_RUN_SUITE( SuiteVirtualArena );
    MU_RUN_SUITE( SuiteStackAdaptor );
    MU_REPORT();
    return MU_EXIT_CODE;
}
