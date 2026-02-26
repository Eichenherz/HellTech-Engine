#ifndef __HP_MATH_H__
#define __HP_MATH_H__

#ifdef __clang__
// NOTE: clang-cl on VS issue
#undef __clang__
#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>
#define __clang__

#elif _MSC_VER >= 1916

#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>

#endif

#include <cmath>

struct float2 { float x, y; };
struct float3 { float x, y, z; };
struct float4 { float x, y, z, w; };

constexpr bool operator==( const float2& a, const float2& b )
{
	return a.x == b.x && a.y == b.y;
}
constexpr bool operator!=( const float2& a, const float2& b )
{
	return !( a == b );
}

constexpr bool operator==( const float3& a, const float3& b )
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}
constexpr bool operator!=( const float3& a, const float3& b )
{
	return !( a == b );
}

constexpr bool operator==( const float4& a, const float4& b )
{
	return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}
constexpr bool operator!=( const float4& a, const float4& b )
{
	return !( a == b );
}

static_assert( std::is_trivially_copyable_v<float2> );
static_assert( std::is_trivially_copyable_v<float3> );
static_assert( std::is_trivially_copyable_v<float4> );
static_assert( sizeof( float2 ) == sizeof( DirectX::XMFLOAT2 ) );
static_assert( sizeof( float3 ) == sizeof( DirectX::XMFLOAT3 ) );
static_assert( sizeof( float4 ) == sizeof( DirectX::XMFLOAT4 ) );

__forceinline DirectX::XMFLOAT2 ToDX( const float2& v ) noexcept { return std::bit_cast< DirectX::XMFLOAT2 >( v ); }
__forceinline DirectX::XMFLOAT3 ToDX( const float3& v ) noexcept { return std::bit_cast< DirectX::XMFLOAT3 >( v ); }
__forceinline DirectX::XMFLOAT4 ToDX( const float4& v ) noexcept { return std::bit_cast< DirectX::XMFLOAT4 >( v ); }

__forceinline float2 FromDX( const DirectX::XMFLOAT2& v ) noexcept { return std::bit_cast< float2 >( v ); }
__forceinline float3 FromDX( const DirectX::XMFLOAT3& v ) noexcept { return std::bit_cast< float3 >( v ); }
__forceinline float4 FromDX( const DirectX::XMFLOAT4& v ) noexcept { return std::bit_cast< float4 >( v ); }

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

struct u16x3
{
	u16 x, y, z;
};

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

	HP_ASSERT( meshletCount );
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

__forceinline float3 XM_CALLCONV DX_XMStoreFloat3( DirectX::XMVECTOR v )
{
	DirectX::XMFLOAT3 out;
	DirectX::XMStoreFloat3( &out, v );
	return FromDX( out );
}

__forceinline float4 XM_CALLCONV DX_XMStoreFloat4( DirectX::XMVECTOR v )
{
	DirectX::XMFLOAT4 out;
	DirectX::XMStoreFloat4( &out, v );
	return FromDX( out );
}

inline aabb_t<float3> TransformAABB( 
	const float3& min, 
	const float3& max, 
	const float3& t, 
	const float4& r, 
	const float3& s 
) {
	using namespace DirectX;

	DirectX::XMFLOAT3 dxMin = ToDX( min );
	DirectX::XMFLOAT3 dxMax = ToDX( max );
	DirectX::XMFLOAT3 dxT = ToDX( t );
	DirectX::XMFLOAT4 dxR = ToDX( r );
	DirectX::XMFLOAT3 dxS = ToDX( s );

	XMVECTOR xmMin = XMLoadFloat3( &dxMin );
	XMVECTOR xmMax = XMLoadFloat3( &dxMax );

	XMVECTOR xmCenter = XMVectorScale( XMVectorAdd( xmMax, xmMin ), 0.5f );
	XMVECTOR xmExtent = XMVectorScale( XMVectorSubtract( xmMax, xmMin ), 0.5f );

	XMMATRIX xmTRS = XMMatrixAffineTransformation(
		XMLoadFloat3( &dxS ), XMVectorZero(), XMLoadFloat4( &dxR ), XMLoadFloat3( &dxT ) );

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

inline u64 AlignUp( u64 x, u64 a ) { return ( x + ( a - 1 ) ) & ~( a - 1 ); }

#endif // !__HP_MATH_H__