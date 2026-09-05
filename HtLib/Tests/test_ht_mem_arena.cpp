// NOTE: covered cases —
//   alloc: basic, zero-byte, sequential, alignment (16/64/256), exact capacity
//   stretch: last alloc, zero, exact capacity, then alloc, not-last denial, past-capacity denial
//   rewind: to mark, to zero, then alloc
//   scope: single, nested
//   helpers: ArenaNew, ArenaMake, ArenaNewArray
//   negative: alloc past capacity, rewind past capacity, non-pow2 alignment, empty backing

#include "test_common.h"

#include <ht_mem_arena.h>

// NOTE: longjmp interception globals — extern'd in ht_error.h under HT_TESTS
jmp_buf gHtAssertJmpbuf;
i32     gHtAssertFired = 0;

// ============================================================================
// linear_arena
// ============================================================================

static constexpr u64        ARENA_CAP = 4096;
// NOTE: the offset checks only hold when the base is already aligned to what they ask for
alignas( 256 ) static u8    gArenaBuf[ ARENA_CAP ];

struct arena_probe
{
    u32 x;
    u64 y;
};

MU_TEST( LinearArenaAllocBasic )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    void* p = a.Alloc( 32, 8 );
    mu_check( nullptr != p );
    mu_check( ( u8* ) p >= gArenaBuf && ( u8* ) p < ( gArenaBuf + ARENA_CAP ) );
    mu_check( ( 32 + HT_ASAN_BORDER ) == a.offsetInBytes );
}

MU_TEST( LinearArenaAllocZeroBytes )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    void* p = a.Alloc( 0, 1 );
    mu_check( ( void* ) gArenaBuf == p );
    mu_check( HT_ASAN_BORDER == a.offsetInBytes );
}

MU_TEST( LinearArenaAllocAlignment16 )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    a.Alloc( 1, 1 );
    void* p = a.Alloc( 8, 16 );
    mu_check( 0 == ( ( u64 ) p & 15 ) );
    mu_check( ( ( ( u8* ) p - gArenaBuf ) + 8 + HT_ASAN_BORDER ) == a.offsetInBytes );
}

MU_TEST( LinearArenaAllocAlignment64 )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    a.Alloc( 1, 1 );
    void* p = a.Alloc( 8, 64 );
    mu_check( 0 == ( ( u64 ) p & 63 ) );
    mu_check( ( ( ( u8* ) p - gArenaBuf ) + 8 + HT_ASAN_BORDER ) == a.offsetInBytes );
}

MU_TEST( LinearArenaAllocAlignment256 )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    a.Alloc( 1, 1 );
    void* p = a.Alloc( 8, 256 );
    mu_check( 0 == ( ( u64 ) p & 255 ) );
    mu_check( ( ( ( u8* ) p - gArenaBuf ) + 8 + HT_ASAN_BORDER ) == a.offsetInBytes );
}

MU_TEST( LinearArenaAllocSequential )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    void* p1 = a.Alloc( 16, 8 );
    void* p2 = a.Alloc( 16, 8 );
    mu_check( ( ( u8* ) p1 + 16 + HT_ASAN_BORDER ) == ( u8* ) p2 );
    mu_check( ( 32 + 2 * HT_ASAN_BORDER ) == a.offsetInBytes );
}

MU_TEST( LinearArenaAllocExactCapacity )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    void* p = a.Alloc( ARENA_CAP - HT_ASAN_BORDER, 1 );
    mu_check( ( void* ) gArenaBuf == p );
    mu_check( ARENA_CAP == a.offsetInBytes );
}

MU_TEST( LinearArenaRewind )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    a.Alloc( 64, 8 );
    u64 mark = a.offsetInBytes;
    a.Alloc( 128, 8 );
    a.Rewind( mark );
    mu_check( mark == a.offsetInBytes );
}

MU_TEST( LinearArenaRewindToZero )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    a.Alloc( 128, 8 );
    a.Rewind( 0 );
    mu_check( 0 == a.offsetInBytes );
}

MU_TEST( LinearArenaRewindThenAlloc )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    a.Alloc( 256, 8 );
    a.Rewind( 0 );
    void* p = a.Alloc( 32, 8 );
    mu_check( ( void* ) gArenaBuf == p );
    mu_check( ( 32 + HT_ASAN_BORDER ) == a.offsetInBytes );
}

// ============================================================================
// linear_arena — TryStretchAlloc
// ============================================================================

MU_TEST( LinearArenaStretchLastAlloc )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    void* p = a.Alloc( 64, 8 );
    u64 newSize = a.TryStretchAlloc( { ( u8* ) p, 64 }, 64 );
    mu_check( 128 == newSize );
    mu_check( ( 128 + 2 * HT_ASAN_BORDER ) == a.offsetInBytes );
}

MU_TEST( LinearArenaStretchZero )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    void* p = a.Alloc( 64, 8 );
    u64 newSize = a.TryStretchAlloc( { ( u8* ) p, 64 }, 0 );
    mu_check( 64 == newSize );
    mu_check( ( 64 + HT_ASAN_BORDER ) == a.offsetInBytes );
}

MU_TEST( LinearArenaStretchExactCapacity )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    void* p = a.Alloc( 64, 8 );
    u64 newSize = a.TryStretchAlloc( { ( u8* ) p, 64 }, ARENA_CAP - 64 - 2 * HT_ASAN_BORDER );
    mu_check( ( ARENA_CAP - 2 * HT_ASAN_BORDER ) == newSize );
    mu_check( ARENA_CAP == a.offsetInBytes );
}

MU_TEST( LinearArenaStretchThenAlloc )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    void* p = a.Alloc( 64, 8 );
    a.TryStretchAlloc( { ( u8* ) p, 64 }, 64 );
    void* q = a.Alloc( 16, 8 );
    mu_check( ( ( u8* ) p + 128 + 2 * HT_ASAN_BORDER ) == ( u8* ) q );
    mu_check( ( 144 + 3 * HT_ASAN_BORDER ) == a.offsetInBytes );
}

MU_TEST( LinearArenaStretchNotLastDenied )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    void* p1 = a.Alloc( 64, 8 );
    a.Alloc( 32, 8 );
    u64 newSize = a.TryStretchAlloc( { ( u8* ) p1, 64 }, 8 );
    mu_check( ~0ull == newSize );
    mu_check( ( 96 + 2 * HT_ASAN_BORDER ) == a.offsetInBytes );
}

MU_TEST( LinearArenaStretchOneBytePastCapacityDenied )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    void* p = a.Alloc( 64, 8 );
    u64 newSize = a.TryStretchAlloc( { ( u8* ) p, 64 }, ARENA_CAP - 64 - 2 * HT_ASAN_BORDER + 1 );
    mu_check( ~0ull == newSize );
    mu_check( ( 64 + HT_ASAN_BORDER ) == a.offsetInBytes );
}

MU_TEST( LinearArenaStretchPastCapacityDenied )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    void* p = a.Alloc( 64, 8 );
    u64 newSize = a.TryStretchAlloc( { ( u8* ) p, 64 }, ARENA_CAP );
    mu_check( ~0ull == newSize );
    mu_check( ( 64 + HT_ASAN_BORDER ) == a.offsetInBytes );
}

// ============================================================================
// ht_mem_scope
// ============================================================================

MU_TEST( MemScopeRewindsOnExit )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    a.Alloc( 32, 8 );
    u64 mark = a.offsetInBytes;
    {
        ht_mem_scope scope{ a };
        a.Alloc( 128, 8 );
        mu_check( mark < a.offsetInBytes );
    }
    mu_check( mark == a.offsetInBytes );
}

MU_TEST( MemScopeNested )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    {
        ht_mem_scope outer{ a };
        a.Alloc( 64, 8 );
        u64 mid = a.offsetInBytes;
        {
            ht_mem_scope inner{ a };
            a.Alloc( 64, 8 );
            mu_check( mid < a.offsetInBytes );
        }
        mu_check( mid == a.offsetInBytes );
    }
    mu_check( 0 == a.offsetInBytes );
}

// ============================================================================
// arena helpers
// ============================================================================

MU_TEST( ArenaNewSingle )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    u64* p = ArenaNew<u64>( a );
    mu_check( ( void* ) gArenaBuf == ( void* ) p );
    mu_check( ( sizeof( u64 ) + HT_ASAN_BORDER ) == a.offsetInBytes );
}

MU_TEST( ArenaMakeForwardsArgs )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    arena_probe* p = ArenaMake<arena_probe>( a, 7u, 9ull );
    mu_check( 7 == p->x );
    mu_check( 9 == p->y );
    mu_check( 0 == ( ( u64 ) p & ( alignof( arena_probe ) - 1 ) ) );
    mu_check( ( sizeof( arena_probe ) + HT_ASAN_BORDER ) == a.offsetInBytes );
}

MU_TEST( ArenaNewArrayNoCookie )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    std::span<u32> arr = ArenaNewArray<u32>( a, 16 );
    mu_check( 16 == std::size( arr ) );
    mu_check( ( void* ) gArenaBuf == ( void* ) std::data( arr ) );
    mu_check( ( 16 * sizeof( u32 ) + HT_ASAN_BORDER ) == a.offsetInBytes );

    arr[ 0 ]  = 5;
    arr[ 15 ] = 7;
    mu_check( 5 == arr[ 0 ] );
    mu_check( 7 == arr[ 15 ] );
}

// ============================================================================
// linear_arena — negative
// ============================================================================
MU_TEST( LinearArenaAllocPastCapacityFires )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    MU_ASSERT_FIRES( a.Alloc( ARENA_CAP + 1, 1 ) );
}

MU_TEST( LinearArenaAllocNonPow2AlignFires )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    MU_ASSERT_FIRES( a.Alloc( 8, 3 ) );
}

MU_TEST( LinearArenaRewindPastCapacityFires )
{
    linear_arena a = { gArenaBuf, ARENA_CAP };
    MU_ASSERT_FIRES( a.Rewind( ARENA_CAP + 1 ) );
}

MU_TEST( LinearArenaEmptyBackingFires )
{
    linear_arena probe = {};
    MU_ASSERT_FIRES( probe = linear_arena( nullptr, ARENA_CAP ) );
    MU_ASSERT_FIRES( probe = linear_arena( gArenaBuf, 0 ) );
}

MU_TEST_SUITE( SuiteLinearArena )
{
    MU_RUN_TEST( LinearArenaAllocBasic );
    MU_RUN_TEST( LinearArenaAllocZeroBytes );
    MU_RUN_TEST( LinearArenaAllocAlignment16 );
    MU_RUN_TEST( LinearArenaAllocAlignment64 );
    MU_RUN_TEST( LinearArenaAllocAlignment256 );
    MU_RUN_TEST( LinearArenaAllocSequential );
    MU_RUN_TEST( LinearArenaAllocExactCapacity );
    MU_RUN_TEST( LinearArenaRewind );
    MU_RUN_TEST( LinearArenaRewindToZero );
    MU_RUN_TEST( LinearArenaRewindThenAlloc );

    MU_RUN_TEST( LinearArenaStretchLastAlloc );
    MU_RUN_TEST( LinearArenaStretchZero );
    MU_RUN_TEST( LinearArenaStretchExactCapacity );
    MU_RUN_TEST( LinearArenaStretchThenAlloc );
    MU_RUN_TEST( LinearArenaStretchNotLastDenied );
    MU_RUN_TEST( LinearArenaStretchOneBytePastCapacityDenied );
    MU_RUN_TEST( LinearArenaStretchPastCapacityDenied );

    MU_RUN_TEST( MemScopeRewindsOnExit );
    MU_RUN_TEST( MemScopeNested );

    MU_RUN_TEST( ArenaNewSingle );
    MU_RUN_TEST( ArenaMakeForwardsArgs );
    MU_RUN_TEST( ArenaNewArrayNoCookie );

    MU_RUN_TEST( LinearArenaAllocPastCapacityFires );
    MU_RUN_TEST( LinearArenaAllocNonPow2AlignFires );
    MU_RUN_TEST( LinearArenaRewindPastCapacityFires );
    MU_RUN_TEST( LinearArenaEmptyBackingFires );
}

// ============================================================================
// main
// ============================================================================

i32 main()
{
    MU_RUN_SUITE( SuiteLinearArena );

    MU_REPORT();
    return MU_EXIT_CODE;
}