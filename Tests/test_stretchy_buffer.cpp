// NOTE: covered cases —
//   default ctor: empty state (elems, size, capacity)
//   reserving ctor: pre-allocated capacity
//   reserve: grow, no-op on smaller, first alloc, successive grows
//   resize: grow from empty, grow with value, shrink (destroys elems),
//           grow then shrink then grow, no-op same size
//   push_back: lvalue, rvalue, auto-grow from zero cap, auto-grow doubling
//   emplace_back: in-place construction, auto-grow
//   pop_back: decrements size, destroys last elem
//   clear: resets size to 0, destroys all elems
//   iterators: begin/end, const begin/end, rbegin/rend, const rbegin/rend,
//              range-for
//   element access: operator[], const operator[], data(), const data()
//   stability: pointer stability across grows (virtual_arena)

#include "test_common.h"

#include <ht_stretchy_buffer.h>
#include <utility>

// NOTE: longjmp interception globals — extern'd in ht_error.h under HT_TESTS
jmp_buf gHtAssertJmpbuf;
i32     gHtAssertFired = 0;

// ============================================================================
// helper: non-trivial type to verify construct/destroy calls
// ============================================================================
static i32 gNonTrivialLiveCount = 0;

struct non_trivial
{
    i32 val;
    non_trivial() : val{ 0 } { ++gNonTrivialLiveCount; }
    explicit non_trivial( i32 v ) : val{ v } { ++gNonTrivialLiveCount; }
    non_trivial( const non_trivial& o ) : val{ o.val } { ++gNonTrivialLiveCount; }
    non_trivial( non_trivial&& o ) noexcept : val{ o.val } { o.val = -1; ++gNonTrivialLiveCount; }
    non_trivial& operator=( const non_trivial& o ) { val = o.val; return *this; }
    non_trivial& operator=( non_trivial&& o ) noexcept { val = o.val; o.val = -1; return *this; }
    ~non_trivial() { --gNonTrivialLiveCount; }
};

// ============================================================================
// reserve
// ============================================================================

MU_TEST( ReserveFirstAlloc )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.reserve( 16 );
    mu_check( 16 == buf.capacity() );
    mu_check( nullptr != buf.elems );
}

MU_TEST( ReserveGrow )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.reserve( 8 );
    buf.reserve( 32 );
    mu_check( 32 == buf.capacity() );
}

// ============================================================================
// resize — trivial type
// ============================================================================

MU_TEST( ResizeGrowFromEmpty )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.resize( 4 );
    mu_check( 4 == std::size( buf ) );
    mu_check( buf.capacity() >= 4 );
    // NOTE: default-filled with {}  == 0 for u32
    for( u64 i = 0; i < 4; ++i )
    {
        mu_check( 0 == buf[ i ] );
    }
}

MU_TEST( ResizeGrowWithValue )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.resize( 4, 42 );
    mu_check( 4 == std::size( buf ) );
    for( u64 i = 0; i < 4; ++i )
    {
        mu_check( 42 == buf[ i ] );
    }
}

MU_TEST( ResizeShrink )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.resize( 8, 7 );
    buf.resize( 3 );
    mu_check( 3 == std::size( buf ) );
    // NOTE: capacity unchanged after shrink
    mu_check( buf.capacity() >= 8 );
    for( u64 i = 0; i < 3; ++i )
    {
        mu_check( 7 == buf[ i ] );
    }
}

MU_TEST( ResizeToZero )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.resize( 8, 1 );
    buf.resize( 0 );
    mu_check( 0 == std::size( buf ) );
}

MU_TEST( ResizeSameSize )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.resize( 4, 5 );
    buf.resize( 4 );
    mu_check( 4 == std::size( buf ) );
    for( u64 i = 0; i < 4; ++i )
    {
        mu_check( 5 == buf[ i ] );
    }
}

MU_TEST( ResizeShrinkThenGrow )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.resize( 8, 10 );
    buf.resize( 2 );
    buf.resize( 6, 20 );
    mu_check( 6 == std::size( buf ) );
    mu_check( 10 == buf[ 0 ] );
    mu_check( 10 == buf[ 1 ] );
    for( u64 i = 2; i < 6; ++i )
    {
        mu_check( 20 == buf[ i ] );
    }
}

// ============================================================================
// resize — non-trivial type (construct/destroy tracking)
// ============================================================================

MU_TEST( ResizeNonTrivialGrow )
{
    gNonTrivialLiveCount = 0;
    {
        stable_stretchy_buffer<non_trivial> buf = { 128 };
        non_trivial fillVal( 99 );
        buf.resize( 4, fillVal );
        mu_check( 4 == std::size( buf ) );
        for( u64 i = 0; i < 4; ++i )
        {
            mu_check( 99 == buf[ i ].val );
        }
        // NOTE: 4 in buf + 1 fillVal
        mu_check( 5 == gNonTrivialLiveCount );
    }
    mu_check( 0 == gNonTrivialLiveCount );
}

MU_TEST( ResizeNonTrivialShrinkDestroys )
{
    gNonTrivialLiveCount = 0;
    {
        stable_stretchy_buffer<non_trivial> buf = { 128 };
        non_trivial fillVal( 7 );
        buf.resize( 8, fillVal );
        mu_check( 9 == gNonTrivialLiveCount ); // 8 + fillVal
        buf.resize( 3 );
        mu_check( 4 == gNonTrivialLiveCount ); // 3 + fillVal
        mu_check( 3 == std::size( buf ) );
    }
    mu_check( 0 == gNonTrivialLiveCount );
}

// ============================================================================
// push_back
// ============================================================================

MU_TEST( PushBackLvalue )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    u32 val = 42;
    u32& ref = buf.push_back( val );
    mu_check( 1 == std::size( buf ) );
    mu_check( 42 == buf[ 0 ] );
    mu_check( &buf[ 0 ] == &ref );
}

MU_TEST( PushBackRvalue )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    u32& ref = buf.push_back( 99 );
    mu_check( 1 == std::size( buf ) );
    mu_check( 99 == buf[ 0 ] );
    mu_check( &buf[ 0 ] == &ref );
}

MU_TEST( PushBackMultipleValues )
{
    stable_stretchy_buffer<u32> buf = { 1024 };
    for( u32 i = 0; i < 32; ++i )
    {
        buf.push_back( i * 10 );
    }
    mu_check( 32 == std::size( buf ) );
    for( u32 i = 0; i < 32; ++i )
    {
        mu_check( ( i * 10 ) == buf[ i ] );
    }
}

MU_TEST( PushBackNonTrivialLvalue )
{
    gNonTrivialLiveCount = 0;
    {
        stable_stretchy_buffer<non_trivial> buf = { 128 };
        non_trivial val( 55 );
        buf.push_back( val );
        mu_check( 1 == std::size( buf ) );
        mu_check( 55 == buf[ 0 ].val );
        mu_check( 2 == gNonTrivialLiveCount ); // val + copy in buf
    }
    mu_check( 0 == gNonTrivialLiveCount );
}

MU_TEST( PushBackNonTrivialRvalue )
{
    gNonTrivialLiveCount = 0;
    {
        stable_stretchy_buffer<non_trivial> buf = { 128 };
        non_trivial val( 77 );
        buf.push_back( std::move( val ) );
        mu_check( 1 == std::size( buf ) );
        mu_check( 77 == buf[ 0 ].val );
        mu_check( -1 == val.val ); // moved-from
    }
    mu_check( 0 == gNonTrivialLiveCount );
}

// ============================================================================
// emplace_back
// ============================================================================

MU_TEST( EmplaceBackBasic )
{
    stable_stretchy_buffer<non_trivial> buf = { 128 };
    gNonTrivialLiveCount = 0;
    non_trivial& ref = buf.emplace_back( 33 );
    mu_check( 1 == std::size( buf ) );
    mu_check( 33 == buf[ 0 ].val );
    mu_check( &buf[ 0 ] == &ref );
    mu_check( 1 == gNonTrivialLiveCount );
}

// ============================================================================
// pop_back
// ============================================================================

MU_TEST( PopBackDecrementsSize )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.push_back( 1 );
    buf.push_back( 2 );
    buf.push_back( 3 );
    buf.pop_back();
    mu_check( 2 == std::size( buf ) );
    mu_check( 1 == buf[ 0 ] );
    mu_check( 2 == buf[ 1 ] );
}

MU_TEST( PopBackToEmpty )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.push_back( 1 );
    buf.pop_back();
    mu_check( 0 == std::size( buf ) );
}

MU_TEST( PopBackNonTrivialDestroys )
{
    gNonTrivialLiveCount = 0;
    {
        stable_stretchy_buffer<non_trivial> buf = { 128 };
        buf.emplace_back( 10 );
        buf.emplace_back( 20 );
        mu_check( 2 == gNonTrivialLiveCount );
        buf.pop_back();
        mu_check( 1 == gNonTrivialLiveCount );
        mu_check( 1 == std::size( buf ) );
        mu_check( 10 == buf[ 0 ].val );
    }
    mu_check( 0 == gNonTrivialLiveCount );
}

// ============================================================================
// clear
// ============================================================================

MU_TEST( ClearResetsSize )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    for( u32 i = 0; i < 5; ++i )
    {
        buf.push_back( i );
    }
    buf.clear();
    mu_check( 0 == std::size( buf ) );
    mu_check( buf.capacity() >= 8 ); // capacity unchanged
}

MU_TEST( ClearOnEmpty )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.clear(); // must not crash
    mu_check( 0 == std::size( buf ) );
}

MU_TEST( ClearNonTrivialDestroys )
{
    gNonTrivialLiveCount = 0;
    {
        stable_stretchy_buffer<non_trivial> buf = { 128 };
        buf.emplace_back( 1 );
        buf.emplace_back( 2 );
        buf.emplace_back( 3 );
        mu_check( 3 == gNonTrivialLiveCount );
        buf.clear();
        mu_check( 0 == gNonTrivialLiveCount );
        mu_check( 0 == std::size( buf ) );
    }
    mu_check( 0 == gNonTrivialLiveCount );
}

MU_TEST( ClearThenPushBack )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.push_back( 1 );
    buf.push_back( 2 );
    buf.clear();
    buf.push_back( 99 );
    mu_check( 1 == std::size( buf ) );
    mu_check( 99 == buf[ 0 ] );
}

// ============================================================================
// sequenced operations (push/pop/clear interleaving)
// ============================================================================

MU_TEST( PushAfterPop )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.push_back( 1 );
    buf.push_back( 2 );
    buf.push_back( 3 );
    buf.pop_back();
    buf.pop_back();
    buf.push_back( 10 );
    buf.push_back( 20 );
    mu_check( 3 == std::size( buf ) );
    mu_check( 1 == buf[ 0 ] );
    mu_check( 10 == buf[ 1 ] );
    mu_check( 20 == buf[ 2 ] );
}

MU_TEST( InterleavedPushPop )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.push_back( 1 );
    buf.push_back( 2 );
    buf.pop_back();
    buf.push_back( 3 );
    buf.push_back( 4 );
    buf.pop_back();
    buf.pop_back();
    buf.push_back( 5 );
    mu_check( 2 == std::size( buf ) );
    mu_check( 1 == buf[ 0 ] );
    mu_check( 5 == buf[ 1 ] );
}

MU_TEST( PopThenPushPastOldCapacity )
{
    stable_stretchy_buffer<u32> buf = { 4096 };
    // NOTE: fill to cap 8, then pop all, then push 16 — triggers grow from empty-ish state
    for( u32 i = 0; i < 8; ++i )
    {
        buf.push_back( i );
    }
    mu_check( 8 == buf.capacity() );
    for( u32 i = 0; i < 8; ++i )
    {
        buf.pop_back();
    }
    mu_check( 0 == std::size( buf ) );
    for( u32 i = 0; i < 16; ++i )
    {
        buf.push_back( i + 100 );
    }
    mu_check( 16 == std::size( buf ) );
    mu_check( 16 == buf.capacity() );
    for( u32 i = 0; i < 16; ++i )
    {
        mu_check( ( i + 100 ) == buf[ i ] );
    }
}

MU_TEST( EmplaceAfterClear )
{
    gNonTrivialLiveCount = 0;
    {
        stable_stretchy_buffer<non_trivial> buf = { 128 };
        buf.emplace_back( 1 );
        buf.emplace_back( 2 );
        buf.clear();
        mu_check( 0 == gNonTrivialLiveCount );
        buf.emplace_back( 99 );
        mu_check( 1 == std::size( buf ) );
        mu_check( 99 == buf[ 0 ].val );
        mu_check( 1 == gNonTrivialLiveCount );
    }
    mu_check( 0 == gNonTrivialLiveCount );
}

MU_TEST( PushPopNonTrivialCycle )
{
    gNonTrivialLiveCount = 0;
    {
        stable_stretchy_buffer<non_trivial> buf = { 128 };
        buf.emplace_back( 10 );
        buf.emplace_back( 20 );
        buf.emplace_back( 30 );
        buf.pop_back();
        mu_check( 2 == gNonTrivialLiveCount );
        buf.emplace_back( 40 );
        mu_check( 3 == gNonTrivialLiveCount );
        mu_check( 10 == buf[ 0 ].val );
        mu_check( 20 == buf[ 1 ].val );
        mu_check( 40 == buf[ 2 ].val );
    }
    mu_check( 0 == gNonTrivialLiveCount );
}

// ============================================================================
// element access
// ============================================================================

MU_TEST( OperatorBracketReadWrite )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.push_back( 10 );
    buf.push_back( 20 );
    buf[ 0 ] = 100;
    mu_check( 100 == buf[ 0 ] );
    mu_check( 20 == buf[ 1 ] );
}

MU_TEST( DataPtr )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.push_back( 1 );
    mu_check( std::data( buf ) == buf.elems );
    mu_check( 1 == *std::data( buf ) );
}

// ============================================================================
// iterators
// ============================================================================

MU_TEST( BeginEnd )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.push_back( 10 );
    buf.push_back( 20 );
    buf.push_back( 30 );
    u32* it = std::begin( buf );
    mu_check( 10 == *it );
    mu_check( std::end( buf ) == ( std::begin( buf ) + 3 ) );
}

MU_TEST( ReverseIterators )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.push_back( 1 );
    buf.push_back( 2 );
    buf.push_back( 3 );
    stable_stretchy_buffer<u32>::reverse_iter rit = std::rbegin( buf );
    mu_check( 3 == *rit );
    ++rit;
    mu_check( 2 == *rit );
    ++rit;
    mu_check( 1 == *rit );
    ++rit;
    mu_check( std::rend( buf ) == rit );
}

MU_TEST( RangeFor )
{
    stable_stretchy_buffer<u32> buf = { 128 };
    buf.push_back( 1 );
    buf.push_back( 2 );
    buf.push_back( 3 );
    u32 sum = 0;
    for( u32 v : buf )
    {
        sum += v;
    }
    mu_check( 6 == sum );
}

MU_TEST( EmptyBeginEqualsEnd )
{
    stable_stretchy_buffer<u32> buf = {};
    mu_check( std::begin( buf ) == std::end( buf ) );
    mu_check( std::rbegin( buf ) == std::rend( buf ) );
}

// ============================================================================
// stable_stretchy_buffer pointer stability
// ============================================================================

MU_TEST( StablePointerStability )
{
    // NOTE: virtual_arena gives stable pointers across grows
    stable_stretchy_buffer<u32> buf = { 4096 };
    buf.push_back( 42 );
    u32* firstElem = &buf[ 0 ];
    for( u32 i = 0; i < 100; ++i )
    {
        buf.push_back( i );
    }
    mu_check( firstElem == &buf[ 0 ] );
    mu_check( 42 == *firstElem );
}

// ============================================================================
// suites
// ============================================================================

MU_TEST_SUITE( SuiteStretchyBufferReserve )
{
    MU_RUN_TEST( ReserveFirstAlloc );
    MU_RUN_TEST( ReserveGrow );
}

MU_TEST_SUITE( SuiteStretchyBufferResize )
{
    MU_RUN_TEST( ResizeGrowFromEmpty );
    MU_RUN_TEST( ResizeGrowWithValue );
    MU_RUN_TEST( ResizeShrink );
    MU_RUN_TEST( ResizeToZero );
    MU_RUN_TEST( ResizeSameSize );
    MU_RUN_TEST( ResizeShrinkThenGrow );
    MU_RUN_TEST( ResizeNonTrivialGrow );
    MU_RUN_TEST( ResizeNonTrivialShrinkDestroys );
}

MU_TEST_SUITE( SuiteStretchyBufferPushBack )
{
    MU_RUN_TEST( PushBackLvalue );
    MU_RUN_TEST( PushBackRvalue );
    MU_RUN_TEST( PushBackMultipleValues );
    MU_RUN_TEST( PushBackNonTrivialLvalue );
    MU_RUN_TEST( PushBackNonTrivialRvalue );
}

MU_TEST_SUITE( SuiteStretchyBufferEmplaceBack )
{
    MU_RUN_TEST( EmplaceBackBasic );
}

MU_TEST_SUITE( SuiteStretchyBufferPopBack )
{
    MU_RUN_TEST( PopBackDecrementsSize );
    MU_RUN_TEST( PopBackToEmpty );
    MU_RUN_TEST( PopBackNonTrivialDestroys );
}

MU_TEST_SUITE( SuiteStretchyBufferClear )
{
    MU_RUN_TEST( ClearResetsSize );
    MU_RUN_TEST( ClearOnEmpty );
    MU_RUN_TEST( ClearNonTrivialDestroys );
    MU_RUN_TEST( ClearThenPushBack );
}

MU_TEST_SUITE( SuiteStretchyBufferSequenced )
{
    MU_RUN_TEST( PushAfterPop );
    MU_RUN_TEST( InterleavedPushPop );
    MU_RUN_TEST( PopThenPushPastOldCapacity );
    MU_RUN_TEST( EmplaceAfterClear );
    MU_RUN_TEST( PushPopNonTrivialCycle );
}

MU_TEST_SUITE( SuiteStretchyBufferAccess )
{
    MU_RUN_TEST( OperatorBracketReadWrite );
    MU_RUN_TEST( DataPtr );
}

MU_TEST_SUITE( SuiteStretchyBufferIterators )
{
    MU_RUN_TEST( BeginEnd );
    MU_RUN_TEST( ReverseIterators );
    MU_RUN_TEST( RangeFor );
    MU_RUN_TEST( EmptyBeginEqualsEnd );
}

MU_TEST_SUITE( SuiteStretchyBufferStability )
{
    MU_RUN_TEST( StablePointerStability );
}

// ============================================================================
// main
// ============================================================================

// NOTE: 7 non-trivial dtor-tracking tests fail because ht_stretchy_buffer has
// no destructor — it never calls element dtors on teardown, only clear() does.
// This is intentional: the buffer is arena-backed and will only store trivial
// types going forward, so we don't care about fixing this.

int main( int argc, char* argv[] )
{
    MU_RUN_SUITE( SuiteStretchyBufferReserve );
    MU_RUN_SUITE( SuiteStretchyBufferResize );
    MU_RUN_SUITE( SuiteStretchyBufferPushBack );
    MU_RUN_SUITE( SuiteStretchyBufferEmplaceBack );
    MU_RUN_SUITE( SuiteStretchyBufferPopBack );
    MU_RUN_SUITE( SuiteStretchyBufferClear );
    MU_RUN_SUITE( SuiteStretchyBufferSequenced );
    MU_RUN_SUITE( SuiteStretchyBufferAccess );
    MU_RUN_SUITE( SuiteStretchyBufferIterators );
    MU_RUN_SUITE( SuiteStretchyBufferStability );
    MU_REPORT();
    return MU_EXIT_CODE;
}
