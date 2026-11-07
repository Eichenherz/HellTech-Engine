#pragma once

#ifndef HELLTECH_HT_UNPACKING_H
#define HELLTECH_HT_UNPACKING_H

#include "../HtLib/ht_renderer_types.h"
#include "ht_hlsl_lang.h"
#include "ht_hlsl_math.h"

// NOTE: https://graphics.stanford.edu/~seander/bithacks.html#VariableSignExtend
i32 RestoreSign( u32 comp, u32 bitDepth )
{
    i32 bitmask = 1u << ( bitDepth - 1 );
    return i32( ( comp ^ bitmask ) - bitmask );
}

template<u32 BIT_COUNT>
float SnormFromBits( u32 bits )
{
    const u32 MASK = ( 1u << BIT_COUNT ) - 1u;
    return float( RestoreSign( bits & MASK, BIT_COUNT ) ) / float( ( 1u << ( BIT_COUNT - 1u ) ) - 1u );
}

struct unpacked_tbn
{
    float octNx;
    float octNy;
    float tanAngle;
    float btnSign;
};

unpacked_tbn UnpackTBN( oct11x2s_a9_s1 enc )
{
    float ox = SnormFromBits<BIT_DEPTH_OCT_N>( enc );
    float oy = SnormFromBits<BIT_DEPTH_OCT_N>( enc >> BIT_DEPTH_OCT_N );
    float ta = SnormFromBits<BIT_DEPTH_TAN_A>( enc >> ( 2 * BIT_DEPTH_OCT_N ) );
    float bs = ( ( enc >> 31 ) & 1u )  ? -1.0f : 1.0f;

    unpacked_tbn result = { ox, oy, ta, bs };
    return result;
}

void BuildBasisFromNormalDuffFrisvad( in float3 n, out float3 tan, out float3 bitan )
{
    float sign = ( n.z >= 0.0f ) ? 1.0f : -1.0f;
    float a = -1.0f / ( sign + n.z );
    float b = n.x * n.y * a;
    tan     = float3( 1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x );
    bitan   = float3( b, sign + n.y * n.y * a, -n.y );
}

// NOTE: Rune Stubbe's version : https://twitter.com/Stubbesaurus/status/937994790553227264
float3 DecodeOctaNormal( float2 octa )
{
    float3 n = float3( octa, 1.0f - abs( octa.x ) - abs( octa.y ) );
    float2 t = float2( max( -n.z, 0.0f ), max( -n.z, 0.0f ) );
    n.xy += select( n.xy < 0.0f, t, -t );

    return normalize( n );
}

float3 DecodeTanFromAngle( float3 n, float tanAngle )
{
    float3 tanRef, bitanRef; BuildBasisFromNormalDuffFrisvad( n, tanRef, bitanRef );
    float sn, cs; sincos( tanAngle * PI, sn, cs );
    return tanRef * cs + bitanRef * sn;
}

u32x3 FetchTriangleIndicesFromMegaBuff( in device_ptr<u32> localPayload, u64 triStartInBytes )
{
    u64 lo      = localPayload[ triStartInBytes >> 2 ];
    u64 hi      = localPayload[ ( triStartInBytes >> 2 ) + 1 ];
    u64 shift   = ( triStartInBytes & 3 ) * 8;
    u64 raw     = ( ( hi << 32 ) | lo ) >> shift;
    return unpack_u8u32( u32( raw & 0x00FFFFFF ) ).xyz;
}

u32 UnpackFromBitstream( in device_ptr<u32> localPayload, in u32 bitDepth, in u32 bitOffsetInMltPayload )
{
    u32 bucketIdx       = bitOffsetInMltPayload >> 5;
    u32 bitIdxInBucket  = bitOffsetInMltPayload & 31;

    u32 w0 = localPayload[ bucketIdx ];
    u32 w1 = localPayload[ bucketIdx + 1 ];

    u32 lo = w0 >> bitIdxInBucket;

    u32 shiftAmount = 32u - bitIdxInBucket;
    u32 hi = ( 32u == shiftAmount ) ? 0u : ( w1 << shiftAmount );

    u32 mask = ( 32u == bitDepth ) ? 0xffffffffu : ( ( 1u << bitDepth ) - 1u );
    return ( lo | hi ) & mask;
}

float3 DecodeVertexFromMegaBuff( in gpu_meshlet mlt, in device_ptr<u32> localPayload, in u32 vtxId )
{
    u32x4 xyz_Grid_BitDepth = unpack_u8u32( mlt.packed8888_XYZ_Grid_BitDepth );

    u32 bitsPerVtx = xyz_Grid_BitDepth.x + xyz_Grid_BitDepth.y + xyz_Grid_BitDepth.z;
    u32 vtxBase    = vtxId * bitsPerVtx;

    u32 x = UnpackFromBitstream( localPayload, xyz_Grid_BitDepth.x, vtxBase );
    u32 y = UnpackFromBitstream( localPayload, xyz_Grid_BitDepth.y,
        vtxBase + xyz_Grid_BitDepth.x );
    u32 z = UnpackFromBitstream( localPayload, xyz_Grid_BitDepth.z,
        vtxBase + xyz_Grid_BitDepth.x + xyz_Grid_BitDepth.y );

    i32x3 exp = i32x3( -xyz_Grid_BitDepth.w, -xyz_Grid_BitDepth.w, -xyz_Grid_BitDepth.w );
    return ldexp( float3( x, y, z ), exp ) + mlt.aabbMin;
}

#endif //!HELLTECH_HT_UNPACKING_H