#pragma once

#ifndef __HELLTECH_HT_HLSL_MATH_H__
#define __HELLTECH_HT_HLSL_MATH_H__

float4x4 TrsToFloat4x4(float3 t, float4 q, float3 s)
{
    float3 r2 = q.xyz + q.xyz;
    float3 w2 = q.w * r2;
    float3 x2 = q.x * r2;
    float3 y2 = q.y * r2;
    float3 z2 = q.z * r2;

    float3 row0 = float3( 1.0f - ( y2.y + z2.z ) , x2.y + w2.z, x2.z - w2.y ) * s.x;
    float3 row1 = float3( x2.y - w2.z, 1.0f - ( x2.x + z2.z ), y2.z + w2.x ) * s.y;
    float3 row2 = float3( x2.z + w2.y, y2.z - w2.x, 1.0f - ( x2.x + y2.y ) ) * s.z;

    // Fill memory 0-15 in Row-Major order
    return float4x4(
        row0.x, row0.y, row0.z, 0.0f, // Indices 0-3
        row1.x, row1.y, row1.z, 0.0f, // Indices 4-7
        row2.x, row2.y, row2.z, 0.0f, // Indices 8-11
        t.x,    t.y,    t.z,    1.0f  // Indices 12-15 (Translation)
    );
}

float3 ComputeNDCBarycentrics( float2 pixelCenter, float2 s0, float2 s1, float2 s2 )
{
    float2 e1 = s1 - s0;
    float2 e2 = s2 - s0;
    float  invDet = 1.0f / ( e1.x * e2.y - e1.y * e2.x );

    float2 db1 = float2(  e2.y, -e2.x ) * invDet;
    float2 db2 = float2( -e1.y,  e1.x ) * invDet;

    float2 d  = pixelCenter - s0;
    float  b1 = dot( d, db1 );
    float  b2 = dot( d, db2 );
    float  b0 = 1.0f - b1 - b2;

   return float3( b0, b1, b2 );
}

// NOTE: Rune Stubbe's version : https://twitter.com/Stubbesaurus/status/937994790553227264
float3 DecodeOctaNormal( float2 octa )
{
    float3 n = float3( octa, 1.0 - abs( octa.x ) - abs( octa.y ) );
    float2 t = float2( max( -n.z, 0.0f ), max( -n.z, 0.0f ) );
    n.xy += select( n.xy < 0.0f, -t, t );

    return normalize( n );
}

// Hamilton product: q = a * b
float4 QuatMul( float4 a, float4 b )
{
    return float4(
        a.w * b.xyz + b.w * a.xyz + cross( a.xyz, b.xyz ),
        a.w * b.w - dot( a.xyz, b.xyz )
    );
}

// Rotate a vector by a unit quaternion
float3 QuatRot( float4 q, float3 v )
{
    float3 t = 2.0f * cross( q.xyz, v );
    return v + q.w * t + cross( q.xyz, t );
}

// Conjugate (inverse for unit quats)
float4 QuatConj( float4 q )
{
    return float4( -q.xyz, q.w );
}

#endif //!__HELLTECH_HT_HLSL_MATH_H__