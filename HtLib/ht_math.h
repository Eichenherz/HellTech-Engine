#pragma once

#ifndef __HT_MATH_H__
#define __HT_MATH_H__

#include <ht_vec_types.h>
#include <ht_gfx_types.h>

#include <ht_core_types.h>
#include <ht_error.h>

#include <bit>
#include <span>
#include <numbers>

#include <DirectXPackedVector.h>
namespace DXPacked = DirectX::PackedVector;

constexpr float HT_ALMOST_HALF_PI = 0.995f * std::numbers::pi_v<float> * 0.5f;

namespace ht
{
	constexpr float to_rads( float degs ) { return degs * ( std::numbers::pi_v<float> / 180.0f ); }

	constexpr float mod_angle( float rads )
	{
		constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;
		return rads - TWO_PI * std::round( rads * ( 1.0f / TWO_PI ) );
	}

	constexpr quat4 quat_mul( quat4 q1, quat4 q2 )
	{
		float3a xyz = q2.w * q1.xyz + q1.w * q2.xyz + cross( q2.xyz, q1.xyz );
		return { xyz.x, xyz.y, xyz.z, q2.w * q1.w - dot( q2.xyz, q1.xyz ) };
	}

	constexpr float3a vec_rot( float3a v, quat4 q )
	{
		float3a t = 2.0f * cross( q.xyz, v );
		return v + q.w * t + cross( q.xyz, t );
	}
	constexpr float3 vec_rot( float3 v, quat4 q )
	{
	    float3a r = vec_rot( float3a{ v.x, v.y, v.z }, q );
	    return { r.x, r.y, r.z };
	}
}

constexpr u8x4 HT_WHITE		= { 255, 255, 255, 255 };
constexpr u8x4 HT_BLACK		= { 0, 0, 0, 255 };
constexpr u8x4 HT_GRAY		= { 0x80, 0x80, 0x80, 255 };
constexpr u8x4 HT_LIGHTGRAY	= { 0xD3, 0xD3, 0xD3, 255 };
constexpr u8x4 HT_RED		= { 255, 0, 0, 255 };
constexpr u8x4 HT_GREEN		= { 0, 255, 0, 255 };
constexpr u8x4 HT_BLUE		= { 0, 0, 255, 255 };
constexpr u8x4 HT_YELLOW	= { 255, 255, 0, 255 };
constexpr u8x4 HT_CYAN		= { 0, 255, 255, 255 };
constexpr u8x4 HT_MAGENTA	= { 255, 0, 255, 255 };

constexpr float FSignOf( float x ) { return x < 0.0f ? -1.0f : 1.0f; }
constexpr float Unorm8ToF32( u8 unorm )
{
    constexpr float INV_RANGE = 1.0f / 255.0f;
    return float( unorm ) * INV_RANGE;
}

constexpr float4 Unorm8ToF32( u8x4 unorm )
{
    constexpr float INV_RANGE = 1.0f / 255.0f;
    return ht::vec_cast<float4>( unorm ) * INV_RANGE;
}

// AABB

template<typename vec_t>
struct aabb_t
{
	vec_t min;
	vec_t max;
};

inline float3 AabbCenter( const aabb_t<float3> aabb ) { return ( aabb.max + aabb.min ) * 0.5f; }
inline float3 AabbHalfExtent( const aabb_t<float3> aabb ) { return ( aabb.max - aabb.min ) * 0.5f; }

inline aabb_t<float3> ComputeAabb( std::span<const float3> vertices )
{
	float3 min = vertices[ 0 ];
	float3 max = vertices[ 0 ];

	for( float3 vtx : vertices )
	{
		min = ht::min( min, vtx );
		max = ht::max( max, vtx );
	}
	return { .min = min, .max = max };
}

HT_FORCEINLINE aabb_t<float3> MergeAabbPair( const aabb_t<float3>& a, const aabb_t<float3>& b )
{
	return { .min = ht::min( a.min, b.min ), .max = ht::max( a.max, b.max ) };
}

#include <immintrin.h>

// NOTE: from https://stackoverflow.com/questions/17638487/minimum-of-4-sp-values-in-m128
inline __m128 _mm_hmin_ps( __m128 v )
{
	v = _mm_min_ps( v, _mm_shuffle_ps( v, v, _MM_SHUFFLE( 2, 1, 0, 3 ) ) );
	v = _mm_min_ps( v, _mm_shuffle_ps( v, v, _MM_SHUFFLE( 1, 0, 3, 2 ) ) );
	return v;
}

inline __m128 _mm_hmax_ps( __m128 v )
{
	v = _mm_max_ps( v, _mm_shuffle_ps( v, v, _MM_SHUFFLE( 2, 1, 0, 3 ) ) );
	v = _mm_max_ps( v, _mm_shuffle_ps( v, v, _MM_SHUFFLE( 1, 0, 3, 2 ) ) );
	return v;
}

inline float MinF32x8_SIMD( __m256 a, __m256 b )
{
	__m256 laneMin_f32x8 = _mm256_min_ps( a, b );
	__m128 lo = _mm256_castps256_ps128( laneMin_f32x8 );
	__m128 hi = _mm256_extractf128_ps( laneMin_f32x8, 1 );

	__m128 laneMin_f32x4 = _mm_min_ps( lo, hi );

	__m128 min_f32x4 = _mm_hmin_ps( laneMin_f32x4 );

	return _mm_cvtss_f32( min_f32x4 );
}

inline float MaxF32x8_SIMD( __m256 a, __m256 b )
{
	__m256 laneMax_f32x8 = _mm256_max_ps( a, b );
	__m128 lo = _mm256_castps256_ps128( laneMax_f32x8 );
	__m128 hi = _mm256_extractf128_ps( laneMax_f32x8, 1 );

	__m128 laneMax_f32x4 = _mm_max_ps( lo, hi );

	__m128 max_f32x4 = _mm_hmax_ps( laneMax_f32x4 );

	return _mm_cvtss_f32( max_f32x4 );
}

HT_FORCEINLINE float2 DX_XMScalarSinCos( float rads )
{
	float sin = 0.0f, cos = 0.0f; DirectX::XMScalarSinCos( &sin, &cos, rads );
	return { sin, cos };
}

inline float3x3 RotMatFromPitchYawRoll( float3a pitchYawRoll )
{
    DirectX::XMVECTOR sin, cos; DirectX::XMVectorSinCos( &sin, &cos, ( DirectX::XMVECTOR ) pitchYawRoll.xyzz );

    auto[ sp, sy, sr ]  = ( ( float4 ) sin ).xyz;
    auto[ cp, cy, cr ]  = ( ( float4 ) cos ).xyz;

    float srsp = sr * sp;
    float crsp = cr * sp;

    float3x3 m = {};
    ht::mat_row( m, 0, float3a{ cr * cy + srsp * sy,    sr * cp,    srsp * cy - cr * sy } );
    ht::mat_row( m, 1, float3a{ crsp * sy - sr * cy,    cr * cp,    sr * sy + crsp * cy } );
    ht::mat_row( m, 2, float3a{ cp * sy,                -sp,        cp * cy } );
    return m;
}

HT_FORCEINLINE float3x3 RotMatFromPitchYawRoll( float3 pitchYawRoll )
{
    return RotMatFromPitchYawRoll( float3a{ pitchYawRoll.x, pitchYawRoll.y, pitchYawRoll.z } );
}

HT_FORCEINLINE float3x3 RotMatFromPitchYawRoll( float pitch, float yaw, float roll )
{
    return RotMatFromPitchYawRoll( float3a{ pitch, yaw, roll } );
}

inline float3x3 RotMatFromQuat( float4 q )
{
    float3a q2 = q.xyz * 2.0f;

    auto[ xx, yy, zz ]  = q.xyz * q2;
    auto[ xy, xz, yz ]  = q.xxy * q2.yzz;
    auto[ wx, wy, wz ]  = q.w * q2;

    float3x3 m = {};
    ht::mat_row( m, 0, float3a{ 1.0f - yy - zz,    xy + wz,            xz - wy } );
    ht::mat_row( m, 1, float3a{ xy - wz,           1.0f - xx - zz,     yz + wx } );
    ht::mat_row( m, 2, float3a{ xz + wy,           yz - wx,            1.0f - xx - yy } );
    return m;
}

inline float4x4 MatLookToLH( float3a eyePos, float3a eyeDir, float3a upDir )
{
    float3a r2          = eyeDir / std::sqrt( ht::dot( eyeDir, eyeDir ) );
    float3a upXr2       = ht::cross( upDir, r2 );
    float3a r0          = upXr2 / std::sqrt( ht::dot( upXr2, upXr2 ) );
    float3a r1          = ht::cross( r2, r0 );
    float3a negEyePos   = -eyePos;

    float d0 = ht::dot( r0, negEyePos );
    float d1 = ht::dot( r1, negEyePos );
    float d2 = ht::dot( r2, negEyePos );

    float4x4 m = {};
    ht::mat_row( m, 0, float4{ r0.x, r1.x, r2.x, 0.0f } );
    ht::mat_row( m, 1, float4{ r0.y, r1.y, r2.y, 0.0f } );
    ht::mat_row( m, 2, float4{ r0.z, r1.z, r2.z, 0.0f } );
    ht::mat_row( m, 3, float4{ d0,   d1,   d2,   1.0f } );
    return m;
}

inline float4x4 MatLookAtLH( float3 eyePos, float3 focusPos, float3 upDir )
{
    float3a eye     = { eyePos.x, eyePos.y, eyePos.z };
    float3a focus   = { focusPos.x, focusPos.y, focusPos.z };
    return MatLookToLH( eye, focus - eye, float3a{ upDir.x, upDir.y, upDir.z } );
}

inline float4x4 MatLookAtRH( float3 eyePos, float3 focusPos, float3 upDir )
{
    float3a eye     = { eyePos.x, eyePos.y, eyePos.z };
    float3a focus   = { focusPos.x, focusPos.y, focusPos.z };
    return MatLookToLH( eye, eye - focus, float3a{ upDir.x, upDir.y, upDir.z } );
}

// NOTE: this is useful when we want to draw the frozen frustum
inline DirectX::XMMATRIX XM_CALLCONV FrustumMatrixFromViewProj( DirectX::XMMATRIX viewXProj )
{
	using namespace DirectX;

	// NOTE: inv( A * B ) = inv B * inv A
	XMMATRIX invFrustMat = viewXProj;
	XMVECTOR det = XMMatrixDeterminant( invFrustMat );
	HT_ASSERT( XMVectorGetX( det ) != 0 );
	XMMATRIX frustMat = XMMatrixInverse( &det, invFrustMat );
	return frustMat;
}

inline float4x4 PerspRevZInfFarFromFovAndAspectRatio( float fovYRads, float aspectRatioWH, float zNear, bool isRH )
{
	auto[ sinFov, cosFov ] = DX_XMScalarSinCos( fovYRads * 0.5f );

	float h     = cosFov / sinFov;
	float w     = h / aspectRatioWH;
    float m23   = isRH ? -1.0f : 1.0f;

	float4x4 proj = {};
    ht::mat_row( proj, 0, float4{ w, 0, 0, 0 } );
    ht::mat_row( proj, 1, float4{ 0, h, 0, 0 } );
    ht::mat_row( proj, 2, float4{ 0, 0, 0, m23 } );
    ht::mat_row( proj, 3, float4{ 0, 0, zNear, 0 } );

	return proj;
}

constexpr packed_trs IDENTITY_TRS = {
	.t = { 0.0f, 0.0f, 0.0f },
	.r = { 0.0f, 0.0f, 0.0f, 1.0f },
	.s = { 1.0f, 1.0f, 1.0f }
};

inline float4x3 TrsToFloat4x3RowMaj( float3 t, float4 q, float3 s )
{
    float3x3 rot = RotMatFromQuat( q );

    float4x3 m = {};
    ht::mat_row( m, 0, ht::mat_row( rot, 0 ) * s.x );
    ht::mat_row( m, 1, ht::mat_row( rot, 1 ) * s.y );
    ht::mat_row( m, 2, ht::mat_row( rot, 2 ) * s.z );
    ht::mat_row( m, 3, float3a{ t.x, t.y, t.z } );
    return m;
}

inline float4x3 TrsToFloat4x3RowMaj( const packed_trs& trs ) { return TrsToFloat4x3RowMaj( trs.t, trs.r, trs.s ); }

#endif // !__HT_MATH_H__