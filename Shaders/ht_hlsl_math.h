#pragma once

#ifndef __HELLTECH_HT_HLSL_MATH_H__
#define __HELLTECH_HT_HLSL_MATH_H__

float4x4 TrsToFloat4x4( float3 t, float4 q, float3 s )
{
    float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
    float xx = q.x * x2, xy = q.x * y2, xz = q.x * z2;
    float yy = q.y * y2, yz = q.y * z2, zz = q.z * z2;
    float wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;

    return float4x4(
        ( 1 - yy - zz ) * s.x,  ( xy + wz ) * s.x,      ( xz - wy ) * s.x,      0,
        ( xy - wz ) * s.y,      ( 1 - xx - zz ) * s.y,  ( yz + wx ) * s.y,      0,
        ( xz + wy ) * s.z,      ( yz - wx ) * s.z,      ( 1 - xx - yy ) * s.z,  0,
        t.x,                     t.y,                     t.z,                    1
    );
}

float2 ClipSpaceToScreenSpace( float4 clipPos, float2 screenRes )
{
    float3 ndcPos = clipPos.xyz / clipPos.w;
    return ( ndcPos.xy * 0.5f + 0.5f ) * screenRes;
}

float3 ComputeAffineBarycentricsFromScreenSapce( float2 pixelCenter, float2 s0, float2 s1, float2 s2 )
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