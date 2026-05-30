#pragma once

#ifndef __HELLTECH_HT_UNPACKING_H__
#define __HELLTECH_HT_UNPACKING_H__

#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"
#include "ht_hlsl_math.h"

// NOTE: https://graphics.stanford.edu/~seander/bithacks.html#VariableSignExtend
i32 RestoreSign( u32 comp, u32 bitDepth )
{
    i32 bitmask = 1u << ( bitDepth - 1 );
    return i32( ( comp ^ bitmask ) - bitmask );
}

struct unpacked_tbn
{
    float octNx;
    float octNy;
    float tanAngle;
    float bitanSign;
};

unpacked_tbn UnpackTBN( oct11x2s_a9_s1 enc )
{
    const u32 OCT_MASK = ( 1u << BIT_DEPTH_OCT_N ) - 1u;
    const u32 TAN_MASK = ( 1u << BIT_DEPTH_TAN_A ) - 1u;

    float ox = RestoreSign( enc & OCT_MASK, BIT_DEPTH_OCT_N );
    float oy = RestoreSign( ( enc >>  BIT_DEPTH_OCT_N ) & OCT_MASK, BIT_DEPTH_OCT_N );
    float ta = RestoreSign( ( enc >> ( 2 * BIT_DEPTH_OCT_N ) ) & TAN_MASK, BIT_DEPTH_TAN_A );
    float bs = ( ( enc >> 31 ) & 1u)  ? -1.0f : 1.0f;

    unpacked_tbn result = { ox, oy, ta, bs };
    return result;
}

void BuildBasisFromNormalDuffFrisvad( in float3 n, out float3 tan, out float3 bitan )
{
    float sign = ( n.z >= 0.0f ) ? 1.0f : -1.0f;
    float a = -1.0f / ( sign + n.z );
    float b = n.x * n.y * a;
    tan = float3( 1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x );
    bitan = float3( b, sign + n.y * n.y * a, -n.y );
}

// NOTE: Rune Stubbe's version : https://twitter.com/Stubbesaurus/status/937994790553227264
float3 DecodeOctaNormal( float2 octa )
{
    float3 n = float3( octa, 1.0f - abs( octa.x ) - abs( octa.y ) );
    float2 t = float2( max( -n.z, 0.0f ), max( -n.z, 0.0f ) );
    n.xy += select( n.xy < 0.0f, -t, t );

    return normalize( n );
}

float3 DecodeTanFromAngle( float3 n, float tanAngle )
{
    float3 tanRef, bitanRef; BuildBasisFromNormalDuffFrisvad( n, tanRef, bitanRef );
    float sn, cs; sincos( tanAngle * PI, sn, cs );
    return tanRef * cs + bitanRef * sn;
}

u32x3 FetchTriangleFromMegaBuff( u64 globalIdxInBytes )
{
    device_ptr<u32> triBuff = { gGlobData.triAddr };
    u64 lo = triBuff[ globalIdxInBytes >> 2 ];
    u64 hi = triBuff[ ( globalIdxInBytes >> 2 ) + 1 ];
    u64 shift = ( globalIdxInBytes & 3 ) * 8;
    u64 raw = ( ( hi << 32 ) | lo ) >> shift;
    return unpack_u8u32( u32( raw & 0xFFFFFF ) ).xyz;
}

u32 UnpackFromBitstream( in device_ptr<u32>  packedPosMegaBuff, in u32 bitDepth, in u32 globalOffsetInBits )
{
    u32 bucketIdx = globalOffsetInBits >> 5;
    u32 bitIdxInBucket = globalOffsetInBits & 31;

    u32 w0 = packedPosMegaBuff[ bucketIdx ];
    u32 w1 = packedPosMegaBuff[ bucketIdx + 1 ];

    u32 lo = w0 >> bitIdxInBucket;

    u32 shiftAmount = 32u - bitIdxInBucket;
    u32 hi = ( 32u == shiftAmount ) ? 0u : ( w1 << shiftAmount );

    return ( lo | hi ) & ( ( 1u << bitDepth ) - 1 );
}

float3 DecodeVertexFromMegaBuff( in gpu_meshlet mlt, in u32 globalOffsetInBits, in u32 vtxId )
{
    device_ptr<u32> posBuff = { gGlobData.vtxPosAddr };

    u32 bitsPerVtx = mlt.xBitDepth + mlt.yBitDepth + mlt.zBitDepth;
    u32 vtxBase    = globalOffsetInBits + mlt.vtxPosOffsetBits + vtxId * bitsPerVtx;

    i32 x = UnpackFromBitstream( posBuff, mlt.xBitDepth, vtxBase );
    i32 y = UnpackFromBitstream( posBuff, mlt.yBitDepth, vtxBase + mlt.xBitDepth );
    i32 z = UnpackFromBitstream( posBuff, mlt.zBitDepth, vtxBase + mlt.xBitDepth + mlt.yBitDepth );

    float3 exp = -float3( mlt.gridBitResolution, mlt.gridBitResolution, mlt.gridBitResolution );
    return ldexp( float3( i32x3( x, y, z ) + mlt.aabbMin ), exp );
}

void DecodeMeshletCullingBoundsFromRound( in gpu_meshlet mlt, out float3 min, out float3 max )
{
    float gridScale = 1.0f / float( 1u << mlt.gridBitResolution );
    min = float3( mlt.aabbMin ) * gridScale - gridScale;
    max = float3( mlt.aabbMax ) * gridScale + gridScale;
}

#endif //!__HELLTECH_HT_UNPACKING_H__