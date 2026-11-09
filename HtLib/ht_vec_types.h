#pragma once

#ifndef HELLTECH_HT_VEC_TYPES_H
#define HELLTECH_HT_VEC_TYPES_H

#ifdef __cplusplus

#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>

#include <bit>
#include <cmath>

#include <ht_core_types.h>
#include <ht_macros.h>

#if defined( __clang__ )

using u8x4		= u8 __attribute__( ( ext_vector_type( 4 ) ) );

using i16x2		= i16 __attribute__( ( ext_vector_type( 2 ) ) );
using i16x4		= i16 __attribute__( ( ext_vector_type( 4 ) ) );

using u16x2		= u16 __attribute__( ( ext_vector_type( 2 ) ) );
using u16x4		= u16 __attribute__( ( ext_vector_type( 4 ) ) );

using i32x2		= i32 __attribute__( ( ext_vector_type( 2 ) ) );
using i32x3	= i32 __attribute__( ( ext_vector_type( 3 ) ) );

using u32x2		= u32 __attribute__( ( ext_vector_type( 2 ) ) );
using u32x3	= u32 __attribute__( ( ext_vector_type( 3 ) ) );
using u32x4		= u32 __attribute__( ( ext_vector_type( 4 ) ) );

using fp16x2	= u16 __attribute__( ( ext_vector_type( 2 ) ) );

using float2	= float __attribute__( ( ext_vector_type( 2 ) ) );
using float3	= float __attribute__( ( ext_vector_type( 3 ) ) );
using float4	= float __attribute__( ( ext_vector_type( 4 ) ) );

using quat4		= float4;

using bool32x2	= i32 __attribute__( ( ext_vector_type( 2 ) ) );
using bool32x3	= i32 __attribute__( ( ext_vector_type( 3 ) ) );
using bool32x4	= i32 __attribute__( ( ext_vector_type( 4 ) ) );

using float3x3 	= float __attribute__( ( matrix_type( 3, 3 ) ) );
using float4x3 	= float __attribute__( ( matrix_type( 4, 3 ) ) );
using float4x4 	= float __attribute__( ( matrix_type( 4, 4 ) ) );

template<typename T>
struct ht_proxy { T v; };

// NOTE: clang's vec3 IS 16 bytes aligned; so we need these types for STORING data to files or CPU <-> GPU
template<typename T, typename V>
struct pkd_vec3
{
	T x, y, z;

	constexpr pkd_vec3() = default;
	constexpr pkd_vec3( T x, T y, T z ) : x{ x }, y{ y }, z{ z } {}
	constexpr pkd_vec3( V v ) : x{ v.x }, y{ v.y }, z{ v.z } {}
	constexpr operator V() const { return { x, y, z }; }
};

using pkd_i32x3 = pkd_vec3<i32, i32x3>;
using pkd_u32x3 = pkd_vec3<u32, u32x3>;
using pkd_fp32x3 = pkd_vec3<float, float3>;

template<typename T, typename V>
struct pkd_vec4
{
	T x, y, z, w;

	constexpr pkd_vec4() = default;
	constexpr pkd_vec4( T x, T y, T z, T w ) : x{ x }, y{ y }, z{ z }, w{ w } {}
	constexpr pkd_vec4( V v ) : x{ v.x }, y{ v.y }, z{ v.z }, w{ v.w } {}
	constexpr operator V() const { return { x, y, z, w }; }
};

using pkd_fp32x4 = pkd_vec4<float, float4>;

static_assert( TRIVIAL_T<u8x4> );
static_assert( TRIVIAL_T<i16x2> );
static_assert( TRIVIAL_T<u16x2> );
static_assert( TRIVIAL_T<u16x4> );
static_assert( TRIVIAL_T<i32x2> );
static_assert( TRIVIAL_T<i32x3> );
static_assert( TRIVIAL_T<u32x2> );
static_assert( TRIVIAL_T<u32x3> );
static_assert( TRIVIAL_T<u32x4> );
static_assert( TRIVIAL_T<fp16x2> );
static_assert( TRIVIAL_T<float2> );
static_assert( TRIVIAL_T<float3> );
static_assert( TRIVIAL_T<float4> );
static_assert( TRIVIAL_T<bool32x2> );
static_assert( TRIVIAL_T<bool32x3> );
static_assert( TRIVIAL_T<bool32x4> );
static_assert( TRIVIAL_T<ht_proxy<float3x3>> );
static_assert( TRIVIAL_T<ht_proxy<float4x3>> );
static_assert( TRIVIAL_T<ht_proxy<float4x4>> );

namespace ht
{
	constexpr bool all( auto mask ) { return 0 != __builtin_reduce_and( mask ); }
	constexpr bool any( auto mask ) { return 0 != __builtin_reduce_or( mask ); }

	constexpr auto dot( auto a, auto b )
	{
		auto m = a * b;
		if constexpr( 2 == __builtin_vectorelements( decltype( m ) ) ) return m.x + m.y;
		else if constexpr( 3 == __builtin_vectorelements( decltype( m ) ) ) return m.x + m.y + m.z;
		else return m.x + m.y + m.z + m.w;
	}

	constexpr auto length( auto v ) { return __builtin_elementwise_sqrt( dot( v, v ) ); }

	constexpr auto cross( auto a, auto b )
	{
		return decltype( a ){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}

	constexpr auto min( auto a, auto b ) { return __builtin_elementwise_min( a, b ); }
	constexpr auto max( auto a, auto b ) { return __builtin_elementwise_max( a, b ); }
	constexpr auto clamp( auto v, auto lo, auto hi )
	{
	    return __builtin_elementwise_min( __builtin_elementwise_max( v, lo ), hi );
	}
	constexpr auto abs( auto v ) { return __builtin_elementwise_abs( v ); }
	constexpr auto floor( auto v ) { return __builtin_elementwise_floor( v ); }
	constexpr auto ceil( auto v ) { return __builtin_elementwise_ceil( v ); }
	constexpr auto round( auto v ) { return __builtin_elementwise_round( v ); }
	constexpr auto trunc( auto v ) { return __builtin_elementwise_trunc( v ); }
	constexpr auto sqrt( auto v ) { return __builtin_elementwise_sqrt( v ); }
	constexpr auto sin( auto v ) { return __builtin_elementwise_sin( v ); }
	constexpr auto cos( auto v ) { return __builtin_elementwise_cos( v ); }
	constexpr auto fma( auto a, auto b, auto c ) { return __builtin_elementwise_fma( a, b, c ); }
	constexpr auto copysign( auto mag, auto sgn ) { return __builtin_elementwise_copysign( mag, sgn ); }
	constexpr auto add_sat( auto a, auto b ) { return __builtin_elementwise_add_sat( a, b ); }
	constexpr auto sub_sat( auto a, auto b ) { return __builtin_elementwise_sub_sat( a, b ); }

	constexpr auto hmin( auto v ) { return __builtin_reduce_min( v ); }
	constexpr auto hmax( auto v ) { return __builtin_reduce_max( v ); }

	// NOTE: C casts on clang vecs are actually BIT casts WTF ????
	template<typename dst_vec_t>
	constexpr dst_vec_t vec_cast( auto v ) { return __builtin_convertvector( v, dst_vec_t ); }

	template<typename> struct mat_dims;
	template<u32 R, u32 C> struct mat_dims<float __attribute__( ( matrix_type( R, C ) ) )>
	{
	    static constexpr u32 ROWS = R, COLS = C;
	};

	template<typename MAT_T>
	HT_FORCEINLINE MAT_T load_col_maj( const float* p )
	{
		return __builtin_matrix_column_major_load( p, mat_dims<MAT_T>::ROWS, mat_dims<MAT_T>::COLS, mat_dims<MAT_T>::ROWS );
	}

	HT_FORCEINLINE auto mat_row( const auto& m, u32 i )
	{
		constexpr u32 COLS = mat_dims<std::remove_cvref_t<decltype( m )>>::COLS;

		float __attribute__( ( ext_vector_type( COLS ) ) ) row = {};
		for( u32 c = 0; c < COLS; ++c ) { row[ c ] = m[ i ][ c ]; }
		return row;
	}

	HT_FORCEINLINE void mat_row( auto& m, u32 i, auto row )
	{
		for( u32 c = 0; c < __builtin_vectorelements( row ); ++c ) { m[ i ][ c ] = row[ c ]; }
	}
}

#else
#error "HtLib vector types need clang ( ext_vector_type + matrix_type ). Tough luck."
#endif

namespace ht
{
	constexpr i64 dot( i32x2 a, i32x2 b ) { return i64( a.x ) * b.x + i64( a.y ) * b.y; }
	constexpr u32 dot( u16x2 a, u16x2 b ) { return a.x * b.x + a.y * b.y; }
	constexpr i32 dot( i16x2 a, i16x2 b ) { return a.x * b.x + a.y * b.y; }

    constexpr float3 normalize( float3 v )
	{
	    float len = std::sqrt( ht::dot( v, v ) );
	    return ( len > 0.0f ) ? v / len : float3{};
	}

	constexpr float3 mul( float3 v, float3x3 m )
	{
		float3 r0 = { m[ 0 ][ 0 ], m[ 0 ][ 1 ], m[ 0 ][ 2 ] };
		float3 r1 = { m[ 1 ][ 0 ], m[ 1 ][ 1 ], m[ 1 ][ 2 ] };
		float3 r2 = { m[ 2 ][ 0 ], m[ 2 ][ 1 ], m[ 2 ][ 2 ] };
		return ht::fma( v.xxx, r0, ht::fma( v.yyy, r1, v.zzz * r2 ) );
	}
}

HT_FORCEINLINE pkd_fp32x3 XM_CALLCONV DX_XMStoreFloat3( DirectX::XMVECTOR v )
{
	DirectX::XMFLOAT3 out = {};
	DirectX::XMStoreFloat3( &out, v );
	return std::bit_cast<pkd_fp32x3>( out );
}

HT_FORCEINLINE float4 XM_CALLCONV DX_XMStoreFloat4( DirectX::XMVECTOR v )
{
	DirectX::XMFLOAT4 out = {};
	DirectX::XMStoreFloat4( &out, v );
	return { out.x, out.y, out.z, out.w };
}

HT_FORCEINLINE float4x3 XM_CALLCONV DX_XMStoreFloat4x3( DirectX::XMMATRIX m )
{
	DirectX::XMFLOAT4X3 out = {};
	DirectX::XMStoreFloat4x3( &out, m );
	return ( const float4x3& ) out;
}

HT_FORCEINLINE float4x4 XM_CALLCONV DX_XMStoreFloat4x4A( DirectX::XMMATRIX m )
{
	DirectX::XMFLOAT4X4A out = {};
	DirectX::XMStoreFloat4x4A( &out, m );
	return ( const float4x4& ) out;
}

HT_FORCEINLINE DirectX::XMMATRIX XM_CALLCONV DX_XMLoadFloat4x4A( float4x4 m )
{
	return DirectX::XMLoadFloat4x4( ( const DirectX::XMFLOAT4X4* ) &m );
}

HT_FORCEINLINE DirectX::XMVECTOR XM_CALLCONV DX_XMLoadFloat3( pkd_fp32x3 v )
{
	return DirectX::XMLoadFloat3( ( const DirectX::XMFLOAT3* ) &v );
}

HT_FORCEINLINE DirectX::XMVECTOR XM_CALLCONV DX_XMLoadFloat4( float4 v )
{
	DirectX::XMFLOAT4 xm = { v.x, v.y, v.z, v.w };
	return DirectX::XMLoadFloat4( &xm );
}

#else

#include <ht_core_types.h>

typedef int16_t2    i16x2;
typedef int16_t4    i16x4;

typedef uint16_t2   u16x2;
typedef uint16_t4   u16x4;

typedef int2        i32x2;
typedef int3        i32x3;
typedef int4        i32x4;

typedef uint2       u32x2;
typedef uint3       u32x3;
typedef uint4       u32x4;

typedef float4      quat4;

typedef int2        b32x2;
typedef int3        b32x3;
typedef int4        b32x4;

typedef int3        pkd_i32x3;
typedef uint3       pkd_u32x3;
typedef float3      pkd_fp32x3;
typedef float4      pkd_fp32x4;

#endif

#endif // !HELLTECH_HT_VEC_TYPES_H
