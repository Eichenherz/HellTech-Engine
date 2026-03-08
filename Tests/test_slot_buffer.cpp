// NOTE: covered cases —
//   push: appends when freelist empty, returns valid handle, value accessible
//   remove + reuse: removed slot goes to freelist, next push reuses it
//   LIFO freelist: multiple removes, pushes come back in reverse order
//   fill-remove-refill: push N, remove all, push N — all slots reused
//   memcpy to plain buffer: full memcpy of backing data, then compare every entry
//   negative: operator[] with out-of-range handle fires HT_ASSERT

#include "test_common.h"

#include <ht_slot_buffer.h>
#include <cstring>

// NOTE: longjmp interception globals — extern'd in ht_error.h under HT_TESTS
jmp_buf gHtAssertJmpbuf;
i32     gHtAssertFired = 0;

struct test_entry
{
    u32 a;
    u32 b;
};

// ============================================================================
// push + access
// ============================================================================

MU_TEST( PushAndAccess )
{
    slot_buffer<test_entry> sb = {};
    slot_buffer<test_entry>::hndl32 h = sb.PushEntry( { 10, 20 } );
    mu_check( 10 == sb[ h ].a );
    mu_check( 20 == sb[ h ].b );
}

MU_TEST( PushMultipleSequential )
{
    slot_buffer<test_entry> sb = {};
    slot_buffer<test_entry>::hndl32 h0 = sb.PushEntry( { 1, 2 } );
    slot_buffer<test_entry>::hndl32 h1 = sb.PushEntry( { 3, 4 } );
    slot_buffer<test_entry>::hndl32 h2 = sb.PushEntry( { 5, 6 } );

    mu_check( 0 == h0.slotIdx );
    mu_check( 1 == h1.slotIdx );
    mu_check( 2 == h2.slotIdx );

    mu_check( 1 == sb[ h0 ].a );
    mu_check( 3 == sb[ h1 ].a );
    mu_check( 5 == sb[ h2 ].a );
}

// ============================================================================
// remove + freelist reuse
// ============================================================================

MU_TEST( RemoveThenReuse )
{
    slot_buffer<test_entry> sb = {};
    slot_buffer<test_entry>::hndl32 h0 = sb.PushEntry( { 100, 200 } );
    slot_buffer<test_entry>::hndl32 h1 = sb.PushEntry( { 300, 400 } );

    u32 removedIdx = h0.slotIdx;
    sb.RemoveEntry( h0 );

    // NOTE: next push should reuse the freed slot
    slot_buffer<test_entry>::hndl32 h2 = sb.PushEntry( { 500, 600 } );
    mu_check( removedIdx == h2.slotIdx );
    mu_check( 500 == sb[ h2 ].a );
    mu_check( 600 == sb[ h2 ].b );

    // NOTE: h1 should be untouched
    mu_check( 300 == sb[ h1 ].a );
    mu_check( 400 == sb[ h1 ].b );
}

MU_TEST( RemoveMultipleLIFO )
{
    slot_buffer<test_entry> sb = {};
    slot_buffer<test_entry>::hndl32 h0 = sb.PushEntry( { 1, 0 } );
    slot_buffer<test_entry>::hndl32 h1 = sb.PushEntry( { 2, 0 } );
    slot_buffer<test_entry>::hndl32 h2 = sb.PushEntry( { 3, 0 } );

    // NOTE: remove in order 0, 1 — freelist head should be 1 -> 0 -> sentinel
    sb.RemoveEntry( h0 );
    sb.RemoveEntry( h1 );

    // NOTE: LIFO — first push gets slot 1, second gets slot 0
    slot_buffer<test_entry>::hndl32 r0 = sb.PushEntry( { 10, 0 } );
    slot_buffer<test_entry>::hndl32 r1 = sb.PushEntry( { 20, 0 } );

    mu_check( h1.slotIdx == r0.slotIdx );
    mu_check( h0.slotIdx == r1.slotIdx );

    mu_check( 10 == sb[ r0 ].a );
    mu_check( 20 == sb[ r1 ].a );
    // NOTE: h2 still alive
    mu_check( 3 == sb[ h2 ].a );
}

// ============================================================================
// fill, remove all, refill
// ============================================================================

MU_TEST( FillRemoveAllRefill )
{
    slot_buffer<test_entry> sb = {};
    constexpr u32 N = 16;
    slot_buffer<test_entry>::hndl32 handles[ N ];

    for( u32 i = 0; i < N; ++i )
    {
        handles[ i ] = sb.PushEntry( { i, i * 10 } );
    }

    // NOTE: remove all
    for( u32 i = 0; i < N; ++i )
    {
        sb.RemoveEntry( handles[ i ] );
    }

    // NOTE: refill — all pushes should reuse freed slots, no new slots appended
    u64 sizeAfterRemoval = std::size( sb );
    for( u32 i = 0; i < N; ++i )
    {
        handles[ i ] = sb.PushEntry( { i + 100, i + 200 } );
    }
    mu_check( std::size( sb ) == sizeAfterRemoval );

    for( u32 i = 0; i < N; ++i )
    {
        mu_check( ( i + 100 ) == sb[ handles[ i ] ].a );
        mu_check( ( i + 200 ) == sb[ handles[ i ] ].b );
    }
}

// ============================================================================
// memcpy to plain buffer — byte-for-byte copy, compare every slot
// ============================================================================

MU_TEST( MemcpyToPlainBuffer )
{
    slot_buffer<test_entry> sb = {};
    slot_buffer<test_entry>::hndl32 h0 = sb.PushEntry( { 11, 22 } );
    slot_buffer<test_entry>::hndl32 h1 = sb.PushEntry( { 33, 44 } );
    slot_buffer<test_entry>::hndl32 h2 = sb.PushEntry( { 55, 66 } );

    // NOTE: remove middle entry so the buffer has a mix of live + dead slots
    sb.RemoveEntry( h1 );

    u64 totalEntries = std::size( sb );
    u64 byteCount = totalEntries * sizeof( test_entry );
    test_entry* plainBuf = ( test_entry* ) std::malloc( byteCount );
    std::memcpy( plainBuf, std::data( sb.items ), byteCount );

    // NOTE: every byte must match — live entries, dead entries, freelist guts, all of it
    mu_check( 0 == std::memcmp( plainBuf, std::data( sb.items ), byteCount ) );

    // NOTE: sanity-check individual live entries through the copy
    mu_check( 11 == plainBuf[ h0.slotIdx ].a );
    mu_check( 22 == plainBuf[ h0.slotIdx ].b );
    mu_check( 55 == plainBuf[ h2.slotIdx ].a );
    mu_check( 66 == plainBuf[ h2.slotIdx ].b );

    std::free( plainBuf );
}

// ============================================================================
// negative: out-of-range handle
// ============================================================================

MU_TEST( AccessOutOfRange )
{
    slot_buffer<test_entry> sb = {};
    sb.PushEntry( { 1, 2 } );

    slot_buffer<test_entry>::hndl32 bad = { .slotIdx = 999 };
    MU_ASSERT_FIRES( sb[ bad ] );
}

// ============================================================================
// suites
// ============================================================================

MU_TEST_SUITE( SuiteSlotBufferPush )
{
    MU_RUN_TEST( PushAndAccess );
    MU_RUN_TEST( PushMultipleSequential );
}

MU_TEST_SUITE( SuiteSlotBufferFreelist )
{
    MU_RUN_TEST( RemoveThenReuse );
    MU_RUN_TEST( RemoveMultipleLIFO );
    MU_RUN_TEST( FillRemoveAllRefill );
}

MU_TEST_SUITE( SuiteSlotBufferMemcpy )
{
    MU_RUN_TEST( MemcpyToPlainBuffer );
}

MU_TEST_SUITE( SuiteSlotBufferNegative )
{
    MU_RUN_TEST( AccessOutOfRange );
}

// ============================================================================
// main
// ============================================================================

int main( int argc, char* argv[] )
{
    MU_RUN_SUITE( SuiteSlotBufferPush );
    MU_RUN_SUITE( SuiteSlotBufferFreelist );
    MU_RUN_SUITE( SuiteSlotBufferMemcpy );
    MU_RUN_SUITE( SuiteSlotBufferNegative );
    MU_REPORT();
    return MU_EXIT_CODE;
}
