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
        ( 1 - yy - zz ) * s.x,  ( xy - wz ) * s.y,		( xz + wy ) * s.z,		t.x,
        ( xy + wz ) * s.x,    	( 1 - xx - zz ) * s.y,  ( yz - wx ) * s.z,		t.y,
        ( xz - wy ) * s.x,    	( yz + wx ) * s.y,		( 1 - xx - yy ) * s.z,  t.z,
        0,						0,						0,						1
    );
}

#endif //!__HELLTECH_HT_HLSL_MATH_H__