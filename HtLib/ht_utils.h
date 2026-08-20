#ifndef __HT_UTILS_H__
#define __HT_UTILS_H__

#include <new>
#include <bit>

#include "ht_core_types.h"
#include "ht_error.h"

constexpr u64 GB = 1ull << 30;
constexpr u64 MB = 1ull << 20;
constexpr u64 KB = 1ull << 10;

constexpr float NS_TO_MS = 1.0e-6f;

template<typename T>
constexpr bool IsStructZero( const T& inStruct )
{
    constexpr u8 ZERO_STRUCT_MEM[ sizeof( T ) ] = {};
    i32 memCmpRes = std::memcmp( &inStruct, ZERO_STRUCT_MEM, sizeof( T ) );
    return 0 == memCmpRes;
}

template<typename T>
constexpr void ZeroStruct( T& inStruct )
{
    std::memset( &inStruct, 0, sizeof( inStruct ) );
}

constexpr bool IsPowOf2( u64 addr )
{
    return !( addr & ( addr - 1 ) );
}
constexpr u64 FwdAlignPot( u64 addr, u64 alignment )
{
    HT_ASSERT( IsPowOf2( alignment ) );
    return ( addr + ( alignment - 1 ) ) & ~( alignment - 1 );
}
// NOTE: works for any alignment, not just power-of-2 (e.g. struct strides like 44)
constexpr u64 FwdAlignGeneric( u64 addr, u64 alignment )
{
    return ( ( addr + alignment - 1 ) / alignment ) * alignment;
}

constexpr u64 CACHE_LINE_SZ = std::hardware_destructive_interference_size;

#define CACHE_ALIGN alignas( CACHE_LINE_SZ )

consteval u32 MurmurHash( std::string_view s )
{
    u32 seed = 0x9E3779B9u;

    for( char c : s )
    {
        u32 k = u8( c );

        k *= 0xcc9e2d51u;
        k = ( k << 15 ) | ( k >> 17) ; // 32-bit left rotation by 15
        k *= 0x1b873593u;

        seed ^= k;
    }

    return seed;
}

constexpr u64 BIT_NPOS = ~u64{ 0 };

constexpr u64 FirstUnsetBitIdx64( u64 mask )
{
    u64 firstSet = std::countr_one( mask );
    return ( 64 != firstSet ) ? firstSet : BIT_NPOS;
}

constexpr u64 FirstSetBitIdx64( u64 mask )
{
    u64 firstSet = std::countr_zero( mask );
    return ( 64 != firstSet ) ? firstSet : BIT_NPOS;
}

// NOTE: hacker's delight https://github.com/hcs0/Hackers-Delight/blob/master/ffstr1.c.txt ffstr12
constexpr u64 FindNBitsFreeRunStartBitIdx( u64 bin, u64 runLen )
{
    HT_ASSERT( runLen > 0 && runLen <= 64 );

    // NOTE: log2( bit_width( u64 ) ) == 6
    [[ unroll ]]
    for( u64 foldIdx = 0; foldIdx < 6; ++foldIdx ) //while( runLen > 1 )
    {
        u64 foldShift = runLen >> 1;
        bin    &= bin >> foldShift;
        runLen -= foldShift;
    }
    return FirstSetBitIdx64( bin );
}

template<UINT_T RET_T>
constexpr RET_T BitCastIrregular( UINT_T auto bin, u64 bitCount )
{
    static_assert( sizeof( bin ) <= sizeof( RET_T ) );

    RET_T contentMask = ( RET_T( 1 ) << bitCount ) - 1;
    RET_T asBits      = bin;
    // NOTE: preferably don't disable this assert
    HT_ASSERT( ( asBits & contentMask ) == asBits );
    return asBits;
}

#endif // !__HT_UTILS_H__
