#pragma once

#ifndef __HT_MATH_H__
#define __HT_MATH_H__

#include <ht_vec_types.h>
#include <ht_gfx_types.h>

#include <ht_core_types.h>
#include <ht_error.h>

#include <bit>
#include <span>

#include <DirectXPackedVector.h>
namespace DXPacked = DirectX::PackedVector;

constexpr float HT_ALMOST_HALF_PI = 0.995f * DirectX::XM_PIDIV2;

inline const DXPacked::XMCOLOR HT_WHITE		= { 255u, 255u, 255u, 1 };
inline const DXPacked::XMCOLOR HT_BLACK		= { 0u, 0u, 0u, 1 };
inline const DXPacked::XMCOLOR HT_GRAY		= { 0x80u, 0x80u, 0x80u, 1 };
inline const DXPacked::XMCOLOR HT_LIGHTGRAY	= { 0xD3u, 0xD3u, 0xD3u, 1 };
inline const DXPacked::XMCOLOR HT_RED		= { 255u, 0u, 0u, 1 };
inline const DXPacked::XMCOLOR HT_GREEN		= { 0u, 255u, 0u, 1 };
inline const DXPacked::XMCOLOR HT_BLUE		= { 0u, 0u, 255u, 1 };
inline const DXPacked::XMCOLOR HT_YELLOW	= { 255u, 255u, 0u, 1 };
inline const DXPacked::XMCOLOR HT_CYAN		= { 0u, 255u, 255u, 1 };
inline const DXPacked::XMCOLOR HT_MAGENTA	= { 255u, 0u, 255u, 1 };

inline float4 DXPackedXMColorToFloat4( DXPacked::XMCOLOR col )
{
	constexpr float RANGE = 1.0f / 255.0f;
	return {
		col.r * RANGE,
		col.g * RANGE,
		col.b * RANGE,
		col.a * RANGE,
	};
}

constexpr float FSignOf( float x ) { return x < 0.0f ? -1.0f : 1.0f; }

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

inline aabb_t<float3> TransformAABB( 
	const float3& min, 
	const float3& max, 
	const float3& t, 
	const float4& r, 
	const float3& s 
) {
	using namespace DirectX;

	XMVECTOR xmMin = DX_XMLoadFloat3( min );
	XMVECTOR xmMax = DX_XMLoadFloat3( max );

	XMVECTOR xmCenter = XMVectorScale( XMVectorAdd( xmMax, xmMin ), 0.5f );
	XMVECTOR xmExtent = XMVectorScale( XMVectorSubtract( xmMax, xmMin ), 0.5f );

	XMMATRIX xmTRS = XMMatrixAffineTransformation( DX_XMLoadFloat3( s ), XMVectorZero(),
	    DX_XMLoadFloat4( r ), XMLoadFloat3( &t ) );

	XMVECTOR xmNewCenter = XMVector3Transform( xmCenter, xmTRS );
	XMVECTOR xmNewExtent = XMVector3Transform( xmExtent, xmTRS );

	XMVECTOR xmNewMin = XMVectorAdd( xmNewCenter, xmNewExtent );
	XMVECTOR xmNewMax = XMVectorSubtract( xmNewCenter, xmNewExtent );

	return { .min = DX_XMStoreFloat3( xmNewMin ), .max = DX_XMStoreFloat3( xmNewMax ) };
}

HT_FORCEINLINE aabb_t<float3> TransformAABB(
	const aabb_t<float3>&   aabb,
	const float3&			t, 
	const float4&			r, 
	const float3&			s 
) {
	return TransformAABB( aabb.min, aabb.max, t, r, s );
}

inline void XM_CALLCONV
TransformBoxVertices(
	DirectX::XMMATRIX	transf,
	float3				boxMin,
	float3				boxMax,
	float4*				boxCorners
) {
	using namespace DirectX;

	boxCorners[ 0 ] = { boxMax.x, boxMax.y, boxMax.z, 1.0f };
	boxCorners[ 1 ] = { boxMax.x, boxMin.y, boxMax.z, 1.0f };
	boxCorners[ 2 ] = { boxMax.x, boxMax.y, boxMin.z, 1.0f };
	boxCorners[ 3 ] = { boxMax.x, boxMin.y, boxMin.z, 1.0f };
	boxCorners[ 4 ] = { boxMin.x, boxMax.y, boxMax.z, 1.0f };
	boxCorners[ 5 ] = { boxMin.x, boxMin.y, boxMax.z, 1.0f };
	boxCorners[ 6 ] = { boxMin.x, boxMax.y, boxMin.z, 1.0f };
	boxCorners[ 7 ] = { boxMin.x, boxMin.y, boxMin.z, 1.0f };

	for( u64 ci = 0; ci < 8; ++ci )
	{
		float4& outCorner = boxCorners[ ci ];
		XMVECTOR transformedCorner = XMVector4Transform( DX_XMLoadFloat4( outCorner ), transf );
		outCorner = DX_XMStoreFloat4( transformedCorner );
	}
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

constexpr float Unorm8ToF32( u8 c )
{
	constexpr float INV_RANGE = 1.0f / 255.0f;
	return float( c ) * INV_RANGE;
}

inline u32 GetImgMipCount( u32 width, u32 height )
{
	// NOTE: 1 + floor( log2 () ) == bit_width
	return ( u32 ) std::bit_width( std::max( width, height ) );
}

struct sincos
{
	float sin;
	float cos;
};

HT_FORCEINLINE sincos DX_XMScalarSinCos( float rads )
{
	float sin;
	float cos;

	DirectX::XMScalarSinCos( &sin, &cos, rads );
	return {.sin = sin, .cos = cos };
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

inline float4x4 PerspRevZInfFarFromFovAndAspectRatioLH( float fovYRads, float aspectRatioWH, float zNear )
{
	auto[ sinFov, cosFov ] = DX_XMScalarSinCos( fovYRads * 0.5f );

	float h = cosFov / sinFov;
	float w = h / aspectRatioWH;

	DirectX::XMMATRIX proj = {};
	proj.r[ 0 ] = DirectX::XMVectorSet( w, 0, 0, 0 );
	proj.r[ 1 ] = DirectX::XMVectorSet( 0, h, 0, 0 );
	proj.r[ 2 ] = DirectX::XMVectorSet( 0, 0, 0, 1 );
	proj.r[ 3 ] = DirectX::XMVectorSet( 0, 0, zNear, 0 );

	return DX_XMStoreFloat4x4A( proj );
}

inline float4x4 PerspRevZInfFarFromFovAndAspectRatioRH( float fovYRads, float aspectRatioWH, float zNear )
{
	auto[ sinFov, cosFov ] = DX_XMScalarSinCos( fovYRads * 0.5f );

	float h = cosFov / sinFov;
	float w = h / aspectRatioWH;

	DirectX::XMMATRIX proj = {};
	proj.r[ 0 ] = DirectX::XMVectorSet( w, 0, 0, 0 );
	proj.r[ 1 ] = DirectX::XMVectorSet( 0, h, 0, 0 );
	proj.r[ 2 ] = DirectX::XMVectorSet( 0, 0, 0, -1 );
	proj.r[ 3 ] = DirectX::XMVectorSet( 0, 0, zNear, 0 );

	return DX_XMStoreFloat4x4A( proj );
}

constexpr packed_trs IDENTITY_TRS = {
	.t = { 0.0f, 0.0f, 0.0f },
	.r = { 0.0f, 0.0f, 0.0f, 1.0f },
	.s = { 1.0f, 1.0f, 1.0f }
};

inline float4x3 TrsToFloat4x3RowMaj( float3 t, float4 q, float3 s )
{
	using namespace DirectX;

	XMMATRIX m = XMMatrixRotationQuaternion( DX_XMLoadFloat4( q ) );
	m.r[ 0 ] = XMVectorScale( m.r[ 0 ], s.x );
	m.r[ 1 ] = XMVectorScale( m.r[ 1 ], s.y );
	m.r[ 2 ] = XMVectorScale( m.r[ 2 ], s.z );
	m.r[ 3 ] = XMVectorSet( t.x, t.y, t.z, 1.0f );

	return DX_XMStoreFloat4x3( m );
}

inline float4x3 TrsToFloat4x3RowMaj( const packed_trs& trs ) { return TrsToFloat4x3RowMaj( trs.t, trs.r, trs.s ); }

#endif // !__HT_MATH_H__