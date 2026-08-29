#pragma once

#ifndef __HT_UTILS_H__
#define __HT_UTILS_H__

#include <new>
#include <bit>
#include <immintrin.h>

#include <ht_core_types.h>
#include <ht_error.h>

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
        k = ( k << 15 ) | ( k >> 17 ) ; // 32-bit left rotation by 15
        k *= 0x1b873593u;

        seed ^= k;
    }

    return seed;
}

constexpr u64 BIT_NPOS = ~u64{ 0 };
constexpr u32 BIT_NPOS_32 = ~u32{ 0 };

// NOTE: mask 0b0010'0111 + 1 -> 0b0010'1000 & ~mask -> 0b0000'1000
constexpr u64 FirstUnsetMask64( u64 mask )
{
    return ~mask & ( mask + 1 );
}

// NOTE: mask 0b0011'0100, -mask 0b1100'1100, AND -> 0b0000'0100
// NOTE: 1 bit run case; 0 for an empty mask
constexpr u64 FirstSetMask64( u64 mask )
{
    return mask & ( 0ull - mask );
}

// NOTE: bit_floor is 1 << Log2Floor( mask ), ie lzcnt/ bsr
// mask 0b0011'0100, top 1 sits at idx 5, so 1 << 5 -> 0b0010'0000
// 0 for an empty mask
constexpr u64 LastSetMask64( u64 mask )
{
    return std::bit_floor( mask );
}

// NOTE: 0b11 * lowest bit ( 0b0000'0010 ) -> 0b0000'0110, mul by a pow2 == shl by its idx
constexpr u64 First2BitRunMask64( u64 mask )
{
    return 0b11ull * FirstSetMask64( mask &= mask >> 1 );
}

// NOTE: 0b111 * 0b0000'1000 -> 0b0011'1000
constexpr u64 First3BitRunMask64( u64 mask )
{
    u64 mask1 = mask & ( mask >> 1 );
    u64 mask2 = mask1 & ( mask >> 2 );
    return 0b111ull * FirstSetMask64( mask2 );
}

constexpr u64 First4BitRunMask64( u64 mask )
{
    mask &= mask >> 1;
    mask &= mask >> 2;
    return 0b1111ull * FirstSetMask64( mask );
}

constexpr u64 FindSmall14RunMask64( u64 mask, u64 runLen ) // NOTE: same as 1-4 from above
{
    HT_ASSERT( ( runLen >= 1 ) && ( runLen <= 4 ) );

    u64 shift0  = runLen >> 1;
    u64 shift1  = ( runLen - shift0 ) >> 1;
    mask        &= mask >> shift0;
    mask        &= mask >> shift1;
    return ( ( 1ull << runLen ) - 1 ) * FirstSetMask64( mask );
}
// NOTE: hacker's delight https://github.com/hcs0/Hackers-Delight/blob/master/ffstr1.c.txt ffstr12
constexpr u64 FindNBitRunMask64( u64 mask, u64 runLen )
{
    HT_ASSERT( runLen > 0 && runLen <= 64 );

    u64 folded  = mask;
    u64 foldLen = runLen;
    // NOTE: log2( bit_width( u64 ) ) == 6
    [[ unroll ]]
    for( u64 foldIdx = 0; foldIdx < 6; ++foldIdx ) //while( foldLen > 1 )
    {
        u64 foldShift   = foldLen >> 1;
        folded          &= folded >> foldShift;
        foldLen         -= foldShift;
    }
    return ( BIT_NPOS >> ( 64 - runLen ) ) * FirstSetMask64( folded );
}

constexpr u64 Log2Floor( u64 num ) { return std::bit_width( num ) - 1; }
constexpr u64 Log2Ceil( u64 num ) { return std::bit_width( num ); }
constexpr u64 NextPow2( u64 num ) { return 1ull << Log2Ceil( num ); }

inline u32 HwRandSeed32()
{
    u32 seed = 0;
    for( u64 tryIdx = 0; tryIdx < 4; ++tryIdx )
    {
        if( 1 == _rdseed32_step( &seed ) ) break;
    }
    return seed;
}

// NOTE: bmp 1 == taken, 0 == free
// NOTE: start as in 0th bit of the run
constexpr u64 FindRunStartInBitmap( std::span<const u64> bmp, u64 inRunLen )
{
    HT_ASSERT( ( std::size( bmp ) >= inRunLen ) && ( 0 < inRunLen ) );

    u64 runLen      = 0;
    u64 runStart    = 0;

    for( u64 qwordIndex = 0; qwordIndex < std::size( bmp ); ++qwordIndex )
    {
        u64 freeMask = ~bmp[ qwordIndex ];   // NOTE: easier alog; 1 == free
        u64 firstBitIdx = qwordIndex * 64;

        /*
        freeMask   = 0b0011'0011      runs at bits 0-1 and 4-5

        lsbFreeBit = 0b0000'0001      BLSI, isolates lowest set bit
        endRunMark = 0b0011'0100      +1 carried through bits 0,1, stopped at bit 2

        countr_zero( freeMask )   = 0   run starts here
        countr_zero( endRunMark ) = 2   run ends here (exclusive)
        length = 2 - 0 = 2

        freeMask &= endRunMark  ->  0b0011'0000    run 1 gone
        */
        while( 0 != freeMask )
        {
            u64 lsbFreeMask     = freeMask & ( 0ull - freeMask );
            u64 endMarkMask     = freeMask + lsbFreeMask;
            u64 localRunStart   = std::countr_zero( freeMask );
            u64 localRunLength  = std::countr_zero( endMarkMask ) - localRunStart;

            const bool canGlueToMainRun = ( 0 == localRunStart ) && ( 0 != runLen );
            runStart    = canGlueToMainRun ? runStart                : firstBitIdx + localRunStart;
            runLen      = canGlueToMainRun ? runLen + localRunLength : localRunLength;

            if( runLen >= inRunLen ) return runStart;

            freeMask &= endMarkMask;
        }
        // NOTE: we didn't find a run long enough,
        // and we didn't reach the end of current qword ( means it has holes ); so we reset
        runLen = ( ( runStart + runLen ) == ( firstBitIdx + 64 ) ) ? runLen : 0;
    }

    return BIT_NPOS;
}

#endif // !__HT_UTILS_H__
