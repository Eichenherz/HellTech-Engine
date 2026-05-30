#pragma once

#ifndef __HELLTECH_HT_HLSL_MATH_H__
#define __HELLTECH_HT_HLSL_MATH_H__

static const float INV_PI = 0.31830988618f;
static const float PI = 3.14159265359f;

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

float Min4_f32( float v0, float v1, float v2, float v3 )
{
    float2 min2 = min( float2( v0, v1 ), float2( v2, v3 ) );
    return min( min2.x, min2.y );
}

float Min8_f32( float v0, float v1, float v2, float v3, float v4, float v5, float v6, float v7 )
{
    return min( Min4_f32( v0, v1, v2, v3 ), Min4_f32( v4, v5, v6, v7 ) );
}

float2 Min4_f32x2( float2 v0, float2 v1, float2 v2, float2 v3 )
{
    return min( min( v0, v1 ), min( v2, v3 ) );
}
float2 Max4_f32x2( float2 v0, float2 v1, float2 v2, float2 v3 )
{
    return max( max( v0, v1 ), max( v2, v3 ) );
}

float4x4 f4x3_To_f4x4_Affine( float4x3 transf )
{
    return float4x4(
        float4( transf[ 0 ], 0.0f ),
        float4( transf[ 1 ], 0.0f ),
        float4( transf[ 2 ], 0.0f ),
        float4( transf[ 3 ], 1.0f )
    );
}

#endif //!__HELLTECH_HT_HLSL_MATH_H__