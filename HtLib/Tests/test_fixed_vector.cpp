// NOTE: covered cases — span ctor
//   element qualification: span<T>, span<const T>, fixed-extent span<T, E>
//   copy-init: implicit conversion from span<T> in a single user-defined conversion
//              ( the HellPack meshlet case: .indices = std::span{ p, n } )
//   sources: raw ptr + count, subspan at a nonzero offset, std::array, std::vector
//   contents: order and values preserved, size() reports the span's size not N
//   sizes: empty span, exactly N
//   non-scalar T
//   negative: span larger than N fires HT_ASSERT
//
// NOTE: covered cases — from_range ctor
//   sources: std::vector, std::array, std::span, transform_view, iota_view
//   widening: u8 range into a u32 container ( the HellPack mltTempIndices case )
//   list-init: fv = { std::from_range, r } picks this over the initializer_list ctor
//   reassignment: elemCount comes from the fresh temporary, it does not accumulate
//   sizes: empty range, exactly N
//   non-scalar T built by the view
//   negative: range larger than N fires HT_ASSERT, direct and through a view

#include "test_common.h"

#include <ht_fixed_vector.h>

#include <array>
#include <ranges>
#include <span>
#include <vector>

// NOTE: longjmp interception globals — extern'd in ht_error.h under HT_TESTS
jmp_buf gHtAssertJmpbuf;
i32     gHtAssertFired = 0;

struct test_entry
{
    u32 a;
    u32 b;
};

static constexpr u64 CAP = 8;

// NOTE: the comma in fixed_vector<u8, CAP> would split the MU_ASSERT_FIRES macro args
using fixed_vector_u8  = fixed_vector<u8, CAP>;
using fixed_vector_u32 = fixed_vector<u32, CAP>;

// ============================================================================
// element qualification
// ============================================================================

MU_TEST( SpanCtorFromConstSpan )
{
    const u8 src[] = { 1, 2, 3, 4 };
    std::span<const u8> s = src;

    fixed_vector<u8, CAP> fv = s;

    mu_check( 4 == fv.size() );
    mu_check( 1 == fv[ 0 ] );
    mu_check( 2 == fv[ 1 ] );
    mu_check( 3 == fv[ 2 ] );
    mu_check( 4 == fv[ 3 ] );
}

MU_TEST( SpanCtorFromMutableSpan )
{
    u8 src[] = { 5, 6, 7 };
    std::span<u8> s = src;

    fixed_vector<u8, CAP> fv = s;

    mu_check( 3 == fv.size() );
    mu_check( 5 == fv[ 0 ] );
    mu_check( 6 == fv[ 1 ] );
    mu_check( 7 == fv[ 2 ] );
}

MU_TEST( SpanCtorFromFixedExtentSpan )
{
    std::array<u8, 3> src = { 9, 8, 7 };
    std::span<u8, 3> s = src;

    fixed_vector<u8, CAP> fv = s;

    mu_check( 3 == fv.size() );
    mu_check( 9 == fv[ 0 ] );
    mu_check( 8 == fv[ 1 ] );
    mu_check( 7 == fv[ 2 ] );
}

// ============================================================================
// copy-init through an implicit conversion — the HellPack meshlet case
// ============================================================================

MU_TEST( SpanCtorCopyInitFromPtrAndCount )
{
    std::vector<u8> tris = { 0, 1, 2, 3, 4, 5, 6 };

    // NOTE: exactly how __hp_meshlet::indices is built in HellPack
    fixed_vector<u8, CAP> fv = std::span{ &tris[ 0 ] + 1, u64( 3 ) };

    mu_check( 3 == fv.size() );
    mu_check( 1 == fv[ 0 ] );
    mu_check( 2 == fv[ 1 ] );
    mu_check( 3 == fv[ 2 ] );
}

MU_TEST( SpanCtorFromSubspanAtOffset )
{
    std::vector<u8> buff = { 10, 11, 12, 13, 14, 15 };
    std::span<const u8> whole = buff;

    fixed_vector<u8, CAP> fv = whole.subspan( 4, 2 );

    mu_check( 2 == fv.size() );
    mu_check( 14 == fv[ 0 ] );
    mu_check( 15 == fv[ 1 ] );
}

MU_TEST( SpanCtorFromVector )
{
    std::vector<u32> src = { 100, 200, 300 };

    fixed_vector<u32, CAP> fv = std::span{ src };

    mu_check( 3 == fv.size() );
    mu_check( 100 == fv[ 0 ] );
    mu_check( 200 == fv[ 1 ] );
    mu_check( 300 == fv[ 2 ] );
}

// ============================================================================
// sizes
// ============================================================================

MU_TEST( SpanCtorEmptySpan )
{
    std::span<const u8> s = {};

    fixed_vector<u8, CAP> fv = s;

    mu_check( 0 == fv.size() );
    mu_check( fv.begin() == fv.end() );
}

MU_TEST( SpanCtorExactCapacity )
{
    std::array<u8, CAP> src = { 0, 1, 2, 3, 4, 5, 6, 7 };

    fixed_vector<u8, CAP> fv = std::span<const u8>{ src };

    mu_check( CAP == fv.size() );
    mu_check( fv.capacity() == fv.size() );
    for( u64 i = 0; i < CAP; ++i )
    {
        mu_check( u8( i ) == fv[ i ] );
    }
}

MU_TEST( SpanCtorSizeIsSpanSizeNotCapacity )
{
    const u32 src[] = { 42, 43 };

    fixed_vector<u32, CAP> fv = std::span<const u32>{ src };

    mu_check( 2 == fv.size() );
    mu_check( CAP == fv.capacity() );
    // NOTE: past-the-size reads must stay out of bounds
    MU_ASSERT_FIRES( fv[ 2 ] );
}

// ============================================================================
// iteration order + non-scalar T
// ============================================================================

MU_TEST( SpanCtorPreservesOrder )
{
    const u32 src[] = { 7, 3, 9, 1 };

    fixed_vector<u32, CAP> fv = std::span<const u32>{ src };

    u64 i = 0;
    for( u32 v : fv )
    {
        mu_check( src[ i ] == v );
        ++i;
    }
    mu_check( 4 == i );
}

MU_TEST( SpanCtorStructElems )
{
    const test_entry src[] = { { 1, 2 }, { 3, 4 } };

    fixed_vector<test_entry, CAP> fv = std::span<const test_entry>{ src };

    mu_check( 2 == fv.size() );
    mu_check( 1 == fv[ 0 ].a && 2 == fv[ 0 ].b );
    mu_check( 3 == fv[ 1 ].a && 4 == fv[ 1 ].b );
}

MU_TEST( SpanCtorDataIsACopy )
{
    u8 src[] = { 1, 2, 3 };

    fixed_vector<u8, CAP> fv = std::span<u8>{ src };
    src[ 1 ] = 99;

    mu_check( 2 == fv[ 1 ] );
    mu_check( std::data( src ) != fv.data() );
}

// ============================================================================
// negative
// ============================================================================

MU_TEST( SpanCtorOverflowFires )
{
    const u8 src[ CAP + 1 ] = {};

    MU_ASSERT_FIRES( fixed_vector_u8( std::span<const u8>{ src } ) );
}

MU_TEST( SpanCtorFarOverflowFires )
{
    std::vector<u8> src( CAP * 4, 0 );

    MU_ASSERT_FIRES( fixed_vector_u8( std::span<const u8>{ src } ) );
}

// ============================================================================
// from_range ctor
// ============================================================================

MU_TEST( FromRangeVector )
{
    std::vector<u8> src = { 1, 2, 3, 4 };

    fixed_vector<u8, CAP> fv{ std::from_range, src };

    mu_check( 4 == fv.size() );
    mu_check( 1 == fv[ 0 ] );
    mu_check( 4 == fv[ 3 ] );
}

MU_TEST( FromRangeArray )
{
    std::array<u32, 3> src = { 10, 20, 30 };

    fixed_vector<u32, CAP> fv{ std::from_range, src };

    mu_check( 3 == fv.size() );
    mu_check( 10 == fv[ 0 ] );
    mu_check( 20 == fv[ 1 ] );
    mu_check( 30 == fv[ 2 ] );
}

MU_TEST( FromRangeSpan )
{
    const u8 buff[] = { 7, 8, 9 };
    std::span<const u8> src = buff;

    fixed_vector<u8, CAP> fv{ std::from_range, src };

    mu_check( 3 == fv.size() );
    mu_check( 7 == fv[ 0 ] );
    mu_check( 9 == fv[ 2 ] );
}

// NOTE: exactly how mltTempIndices is widened in HellPack — u8 tri indices to u32
MU_TEST( FromRangeWideningTransformView )
{
    std::vector<u8> tris = { 0, 1, 2, 3, 4, 5 };
    std::span<u8> mltLocalIndices = { &tris[ 0 ] + 3, u64( 3 ) };

    fixed_vector<u32, CAP> fv = { std::from_range,
        mltLocalIndices | std::views::transform( []( u8 i ) { return u32( i ); } ) };

    mu_check( 3 == fv.size() );
    mu_check( 3u == fv[ 0 ] );
    mu_check( 4u == fv[ 1 ] );
    mu_check( 5u == fv[ 2 ] );
}

// NOTE: reassigning must not accumulate — elemCount comes from the fresh temporary
MU_TEST( FromRangeReassignResetsCount )
{
    const u8 first[]  = { 1, 2, 3, 4, 5 };
    const u8 second[] = { 9, 9 };

    fixed_vector<u8, CAP> fv = { std::from_range, std::span<const u8>{ first } };
    mu_check( 5 == fv.size() );

    fv = { std::from_range, std::span<const u8>{ second } };

    mu_check( 2 == fv.size() );
    mu_check( 9 == fv[ 0 ] );
    mu_check( 9 == fv[ 1 ] );
    MU_ASSERT_FIRES( fv[ 2 ] );
}

MU_TEST( FromRangeIotaView )
{
    fixed_vector<u32, CAP> fv = { std::from_range, std::views::iota( 2u, 6u ) };

    mu_check( 4 == fv.size() );
    mu_check( 2u == fv[ 0 ] );
    mu_check( 3u == fv[ 1 ] );
    mu_check( 4u == fv[ 2 ] );
    mu_check( 5u == fv[ 3 ] );
}

MU_TEST( FromRangeStructElems )
{
    const u32 src[] = { 1, 2, 3 };

    fixed_vector<test_entry, CAP> fv = { std::from_range,
        std::span<const u32>{ src } | std::views::transform( []( u32 v ) { return test_entry{ v, v * 2 }; } ) };

    mu_check( 3 == fv.size() );
    mu_check( 1 == fv[ 0 ].a && 2 == fv[ 0 ].b );
    mu_check( 3 == fv[ 2 ].a && 6 == fv[ 2 ].b );
}

MU_TEST( FromRangeEmpty )
{
    std::vector<u8> src = {};

    fixed_vector<u8, CAP> fv{ std::from_range, src };

    mu_check( 0 == fv.size() );
    mu_check( fv.begin() == fv.end() );
}

MU_TEST( FromRangeExactCapacity )
{
    std::vector<u8> src = { 0, 1, 2, 3, 4, 5, 6, 7 };

    fixed_vector<u8, CAP> fv{ std::from_range, src };

    mu_check( CAP == fv.size() );
    for( u64 i = 0; i < CAP; ++i )
    {
        mu_check( u8( i ) == fv[ i ] );
    }
}

MU_TEST( FromRangeDataIsACopy )
{
    std::vector<u8> src = { 1, 2, 3 };

    fixed_vector<u8, CAP> fv{ std::from_range, src };
    src[ 1 ] = 99;

    mu_check( 2 == fv[ 1 ] );
    mu_check( std::data( src ) != fv.data() );
}

MU_TEST( FromRangeOverflowFires )
{
    std::vector<u8> src( CAP + 1, 0 );

    MU_ASSERT_FIRES( fixed_vector_u8( std::from_range, src ) );
}

MU_TEST( FromRangeOverflowThroughViewFires )
{
    std::vector<u8> src( CAP * 2, 0 );

    MU_ASSERT_FIRES( fixed_vector_u32( std::from_range,
        src | std::views::transform( []( u8 i ) { return u32( i ); } ) ) );
}

// ============================================================================
// suite
// ============================================================================

MU_TEST_SUITE( SuiteSpanCtor )
{
    MU_RUN_TEST( SpanCtorFromConstSpan );
    MU_RUN_TEST( SpanCtorFromMutableSpan );
    MU_RUN_TEST( SpanCtorFromFixedExtentSpan );
    MU_RUN_TEST( SpanCtorCopyInitFromPtrAndCount );
    MU_RUN_TEST( SpanCtorFromSubspanAtOffset );
    MU_RUN_TEST( SpanCtorFromVector );
    MU_RUN_TEST( SpanCtorEmptySpan );
    MU_RUN_TEST( SpanCtorExactCapacity );
    MU_RUN_TEST( SpanCtorSizeIsSpanSizeNotCapacity );
    MU_RUN_TEST( SpanCtorPreservesOrder );
    MU_RUN_TEST( SpanCtorStructElems );
    MU_RUN_TEST( SpanCtorDataIsACopy );
    MU_RUN_TEST( SpanCtorOverflowFires );
    MU_RUN_TEST( SpanCtorFarOverflowFires );
}

MU_TEST_SUITE( SuiteFromRangeCtor )
{
    MU_RUN_TEST( FromRangeVector );
    MU_RUN_TEST( FromRangeArray );
    MU_RUN_TEST( FromRangeSpan );
    MU_RUN_TEST( FromRangeWideningTransformView );
    MU_RUN_TEST( FromRangeReassignResetsCount );
    MU_RUN_TEST( FromRangeIotaView );
    MU_RUN_TEST( FromRangeStructElems );
    MU_RUN_TEST( FromRangeEmpty );
    MU_RUN_TEST( FromRangeExactCapacity );
    MU_RUN_TEST( FromRangeDataIsACopy );
    MU_RUN_TEST( FromRangeOverflowFires );
    MU_RUN_TEST( FromRangeOverflowThroughViewFires );
}

// ============================================================================
// main
// ============================================================================

int main( int argc, char* argv[] )
{
    MU_RUN_SUITE( SuiteSpanCtor );
    MU_RUN_SUITE( SuiteFromRangeCtor );
    MU_REPORT();
    return MU_EXIT_CODE;
}