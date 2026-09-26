#pragma once

#ifndef __HT_HASH_H__
#define __HT_HASH_H__

#include <ht_core_types.h>
#include <string_view>

constexpr u32 PcgHash32( u32 input )
{
    u32 state = input * 747796405u + 2891336453u;
    u32 word = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
    return ( word >> 22u ) ^ word;
}

constexpr u64 SplitmixHash64( u64 input )
{
    input = ( input ^ ( input >> 30 ) ) * 0xbf58476d1ce4e5b9ull;
    input = ( input ^ ( input >> 27 ) ) * 0x94d049bb133111ebull;
    return input ^ ( input >> 31 );
}

// NOTE: from https://github.com/bryc/code/blob/master/jshash/PRNGs.md#splitmix32
constexpr u32 SplitmixHash32( u32 input )
{
    input += 0x9e3779b9;
    input = ( input ^ ( input >> 16 ) ) * 0x85ebca6b;
    input = ( input ^ ( input >> 13 ) ) * 0xc2b2ae35;
    return input ^ ( input >> 16 );
}

// NOTE: doesn't produce good hash patterns, it's a great remap tho
constexpr u64 FibRemap( u64 input, u64 bitWidthOfDstRangePow2 )
{
    // NOTE: 0x9E3779B97F4A7C15 == 2 ^ 64 / fibGoldenRatio
    // NOTE: almost like a modulo + we take as many bits as needed for out range
    return ( input * 0x9E3779B97F4A7C15ull ) >> ( 64 - bitWidthOfDstRangePow2 );
}

// PMF ~ 1 / k^2 on [ min, max ].  a = 1.0f/ min, ab = a - 1.0f / ( max + 1 )
constexpr u32 PowDistroCDF( u32 h, float a, float ab, u32 max )
{
    float u = float( h >> 8 ) * 0x1p-24f;
    u32 k = u32( 1.0f / ( a - u * ab ) );
    return ( k > max ) ? max : k;
}

consteval u32 MurmurHash32( std::string_view s )
{
    u32 seed = 0x9E3779B9u;

    for( char c : s )
    {
        u32 k = u8( c );

        k *= 0xcc9e2d51u;
        k = ( k << 15 ) | ( k >> 17 ) ; // 32-bit left rotation by 15
        k *= 0x1b873593u;

        seed ^= k;
    }

    return seed;
}

consteval u64 MurmurHash64( std::string_view s )
{
    u64 seed = 0x9E3779B97F4A7C15ull;

    for( char c : s )
    {
        u64 k = u8( c );

        k *= 0x87c37b91114253d5ull;
        k = ( k << 31 ) | ( k >> 33 ) ; // 64-bit left rotation by 31
        k *= 0x4cf5ad432745937full;

        seed ^= k;
    }

    return seed;
}

#endif //!__HT_HASH_H__