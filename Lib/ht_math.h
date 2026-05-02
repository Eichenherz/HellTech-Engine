#ifndef __HT_MATH_H__
#define __HT_MATH_H__

#include "ht_vec_types.h"
#include "ht_core_types.h"
#include <span>
#include <math.h>

#include <DirectXPackedVector.h>

namespace DXPacked = DirectX::PackedVector;

constexpr float HT_ALMOST_HALF_PI = 0.995f * DirectX::XM_PIDIV2;

inline float2 fminf( float2 a, float2 b )
{
	return { fminf( a.x,b.x ), fminf( a.y,b.y ) };
}
inline float3 fminf( float3 a, float3 b )
{
	return { fminf( a.x,b.x ), fminf( a.y,b.y ), fminf( a.z,b.z ) };
}
inline float4 fminf( float4 a, float4 b )
{
	return { fminf( a.x,b.x ), fminf( a.y,b.y ), fminf( a.z,b.z ), fminf( a.w,b.w ) };
}

inline float2 fmaxf( float2 a, float2 b )
{
	return { fmaxf( a.x,b.x ), fmaxf( a.y,b.y ) };
}
inline float3 fmaxf( float3 a, float3 b )
{
	return { fmaxf( a.x,b.x ), fmaxf( a.y,b.y ), fmaxf( a.z,b.z ) };
}
inline float4 fmaxf( float4 a, float4 b )
{
	return { fmaxf( a.x,b.x ), fmaxf( a.y,b.y ), fmaxf( a.z,b.z ), fmaxf( a.w,b.w ) };
}

inline constexpr float3 CrossProd( float3 a, float3 b )
{
	return  { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y  *b.x };
}

constexpr float DotProd( float2 a, float2 b )
{
	return a.x * b.x + a.y * b.y;
}
constexpr float DotProd( float3 a, float3 b )
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
constexpr float DotProd( float4 a, float4 b )
{
	return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

constexpr float FSignOf( float x )
{
	return x < 0.0f ? -1.0f : 1.0f;
}

// AABB

template<typename Vec>
struct aabb_t
{
	Vec min;
	Vec max;
};

inline aabb_t<float3> ComputeAabb( std::span<const float3> vertices )
{
	float3 min = vertices[ 0 ];
	float3 max = vertices[ 0 ];

	for( u64 vi = 1; vi < std::size( vertices ); ++vi )
	{
		min.x = std::min( min.x, vertices[ vi ].x );
		min.y = std::min( min.y, vertices[ vi ].y );
		min.z = std::min( min.z, vertices[ vi ].z );

		max.x = std::max( max.x, vertices[ vi ].x );
		max.y = std::max( max.y, vertices[ vi ].y );
		max.z = std::max( max.z, vertices[ vi ].z );
	}
	return { .min = min, .max = max };
}

inline aabb_t<float2> ComputeAabb( std::span<const float2> vertices )
{
	float2 min = vertices[ 0 ];
	float2 max = vertices[ 0 ];

	for( u64 vi = 1; vi < std::size( vertices ); ++vi )
	{
		min.x = std::min( min.x, vertices[ vi ].x );
		min.y = std::min( min.y, vertices[ vi ].y );

		max.x = std::max( max.x, vertices[ vi ].x );
		max.y = std::max( max.y, vertices[ vi ].y );
	}
	return { .min = min, .max = max };
}

inline aabb_t<float3> MergeAabbPair( const aabb_t<float3>& a, const aabb_t<float3>& b )
{
	return {
		.min = {
			std::min( a.min.x, b.min.x ),
			std::min( a.min.y, b.min.y ),
			std::min( a.min.z, b.min.z ),
		},
		.max = {
			std::max( a.max.x, b.max.x ),
			std::max( a.max.y, b.max.y ),
			std::max( a.max.z, b.max.z ),
		}
	};
}

inline aabb_t<float3> MergeAabbsMultiple( const std::ranges::forward_range auto& aabbs )
{
	aabb_t<float3> out = {
		.min = { FLT_MAX, FLT_MAX, FLT_MAX },
		.max = { -FLT_MAX, -FLT_MAX, -FLT_MAX },
	};

	for( const aabb_t<float3>& box : aabbs )
	{
		out = MergeAabbPair( out, { .min = box.min, .max = box.max } );
	}

	return out;
}

inline aabb_t<float3> MergeAabbs( const std::ranges::forward_range auto& aabbs )
{
	const u64 meshletCount = std::size( aabbs );

	HT_ASSERT( meshletCount );
	if( 1 == meshletCount )
	{
		return { .min = aabbs[ 0 ].min, .max = aabbs[ 0 ].max };
	}

	if( 2 == meshletCount )
	{
		return MergeAabbPair(
			{ .min = aabbs[ 0 ].min, .max = aabbs[ 0 ].max },
			{ .min = aabbs[ 1 ].min, .max = aabbs[ 1 ].max }
		);
	}

	return MergeAabbsMultiple( aabbs );
}

inline aabb_t<float3> TransformAABB( 
	const float3& min, 
	const float3& max, 
	const float3& t, 
	const float4& r, 
	const float3& s 
) {
	using namespace DirectX;

	XMVECTOR xmMin = XMLoadFloat3( &min );
	XMVECTOR xmMax = XMLoadFloat3( &max );

	XMVECTOR xmCenter = XMVectorScale( XMVectorAdd( xmMax, xmMin ), 0.5f );
	XMVECTOR xmExtent = XMVectorScale( XMVectorSubtract( xmMax, xmMin ), 0.5f );

	XMMATRIX xmTRS = XMMatrixAffineTransformation(
		XMLoadFloat3( &s ), XMVectorZero(), XMLoadFloat4( &r ), XMLoadFloat3( &t ) );

	XMVECTOR xmNewCenter = XMVector3Transform( xmCenter, xmTRS );
	XMVECTOR xmNewExtent = XMVector3Transform( xmExtent, xmTRS );

	XMVECTOR xmNewMin = XMVectorAdd( xmNewCenter, xmNewExtent );
	XMVECTOR xmNewMax = XMVectorSubtract( xmNewCenter, xmNewExtent );

	return { .min = DX_XMStoreFloat3( xmNewMin ), .max = DX_XMStoreFloat3( xmNewMax ) };
}

__forceinline aabb_t<float3> TransformAABB( 
	const aabb_t<float3>&   aabb,
	const float3&			t, 
	const float4&			r, 
	const float3&			s 
) {
	return TransformAABB( aabb.min, aabb.max, t, r, s );
}

constexpr std::array<float3, 8u> GenerateBoxWithBounds( float3 boxMin, float3 boxMax )
{
	std::array<float3, 8u> boxCorners = {};
	boxCorners[ 0 ] = { boxMax.x, boxMax.y, boxMax.z };
	boxCorners[ 1 ] = { boxMax.x, boxMin.y, boxMax.z };
	boxCorners[ 2 ] = { boxMax.x, boxMax.y, boxMin.z };
	boxCorners[ 3 ] = { boxMax.x, boxMin.y, boxMin.z };
	boxCorners[ 4 ] = { boxMin.x, boxMax.y, boxMax.z };
	boxCorners[ 5 ] = { boxMin.x, boxMin.y, boxMax.z };
	boxCorners[ 6 ] = { boxMin.x, boxMax.y, boxMin.z };
	boxCorners[ 7 ] = { boxMin.x, boxMin.y, boxMin.z };

	return boxCorners;
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
		XMFLOAT4& outCorner = boxCorners[ ci ];
		XMVECTOR transformedCorner = XMVector4Transform( XMLoadFloat4( &outCorner ), transf );
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

inline u64 FloorPowOf2( u64 size )
{
	// NOTE: use Hacker's Delight for bit-trickery
	constexpr u64 ONE_LEFT_MOST = u64( 1ULL << ( sizeof( u64 ) * 8 - 1 ) );
	return ( size ) ? ONE_LEFT_MOST >> __lzcnt64( size ) : 0;
}
inline u32 GetImgMipCount( u32 width, u32 height, u32 mipLevels )
{
	HT_ASSERT( width && height );
	// NOTE: floor( log2 () ) == bit_width -1
	return std::min( ( u32 ) std::bit_width( std::max( width, height ) ) - 1, mipLevels );
}

struct sincos
{
	float sin;
	float cos;
};

__forceinline sincos DX_XMScalarSinCos( float rads )
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


#endif // !__HT_MATH_H__