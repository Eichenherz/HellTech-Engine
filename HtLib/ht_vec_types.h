#pragma once

#ifndef __HT_VEC_TYPES_H__
#define __HT_VEC_TYPES_H__

#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>

#include <bit>
#include <cmath>

#include <ht_core_types.h>
#include <ht_macros.h>


using i32x3		= DirectX::XMINT3;
using u32x3		= DirectX::XMUINT3;
using float3	= DirectX::XMFLOAT3;

#if defined(__clang__)

using i16x2		= i16 __attribute__( ( ext_vector_type( 2 ) ) );

using u16x2		= u16 __attribute__( ( ext_vector_type( 2 ) ) );
using u16x4		= u16 __attribute__( ( ext_vector_type( 4 ) ) );

using i32x2		= i32 __attribute__( ( ext_vector_type( 2 ) ) );
using i32x3a	= i32 __attribute__( ( ext_vector_type( 3 ) ) );

using u32x2		= u32 __attribute__( ( ext_vector_type( 2 ) ) );
using u32x3a	= u32 __attribute__( ( ext_vector_type( 3 ) ) );
using u32x4		= u32 __attribute__( ( ext_vector_type( 4 ) ) );

using fp16x2	= u16 __attribute__( ( ext_vector_type( 2 ) ) );

using float2	= float __attribute__( ( ext_vector_type( 2 ) ) );
using float3a	= float __attribute__( ( ext_vector_type( 3 ) ) );
using float4	= float __attribute__( ( ext_vector_type( 4 ) ) );

using bool32x2	= i32 __attribute__( ( ext_vector_type( 2 ) ) );
using bool32x3	= i32 __attribute__( ( ext_vector_type( 3 ) ) );
using bool32x4	= i32 __attribute__( ( ext_vector_type( 4 ) ) );

using float3x3 	= float __attribute__( ( matrix_type( 3, 3 ) ) );
using float4x3 	= float __attribute__( ( matrix_type( 4, 3 ) ) );
using float4x4 	= float __attribute__( ( matrix_type( 4, 4 ) ) );

template<typename T>
struct ht_proxy { T v; };

static_assert( TRIVIAL_T<i16x2> );
static_assert( TRIVIAL_T<u16x2> );
static_assert( TRIVIAL_T<u16x4> );
static_assert( TRIVIAL_T<i32x2> );
static_assert( TRIVIAL_T<i32x3a> );
static_assert( TRIVIAL_T<u32x2> );
static_assert( TRIVIAL_T<u32x3a> );
static_assert( TRIVIAL_T<u32x4> );
static_assert( TRIVIAL_T<fp16x2> );
static_assert( TRIVIAL_T<float2> );
static_assert( TRIVIAL_T<float3a> );
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

	constexpr auto cross( auto a, auto b )
	{
		return decltype( a ){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}

	constexpr auto min( auto a, auto b ) { return __builtin_elementwise_min( a, b ); }
	constexpr auto max( auto a, auto b ) { return __builtin_elementwise_max( a, b ); }
	constexpr auto clamp( auto v, auto lo, auto hi ) { return __builtin_elementwise_min( __builtin_elementwise_max( v, lo ), hi ); }
	constexpr auto abs( auto v ) { return __builtin_elementwise_abs( v ); }
	constexpr auto floor( auto v ) { return __builtin_elementwise_floor( v ); }
	constexpr auto ceil( auto v ) { return __builtin_elementwise_ceil( v ); }
	constexpr auto round( auto v ) { return __builtin_elementwise_round( v ); }
	constexpr auto trunc( auto v ) { return __builtin_elementwise_trunc( v ); }
	constexpr auto sqrt( auto v ) { return __builtin_elementwise_sqrt( v ); }
	constexpr auto fma( auto a, auto b, auto c ) { return __builtin_elementwise_fma( a, b, c ); }
	constexpr auto copysign( auto mag, auto sgn ) { return __builtin_elementwise_copysign( mag, sgn ); }
	constexpr auto add_sat( auto a, auto b ) { return __builtin_elementwise_add_sat( a, b ); }
	constexpr auto sub_sat( auto a, auto b ) { return __builtin_elementwise_sub_sat( a, b ); }

	constexpr auto hmin( auto v ) { return __builtin_reduce_min( v ); }
	constexpr auto hmax( auto v ) { return __builtin_reduce_max( v ); }
}

#else
#error "HtLib vector types need clang ( ext_vector_type + matrix_type ). Tough luck."
#endif

template<typename T> struct ht_ext_vec3 {};
template<> struct ht_ext_vec3<float3> { using type = float3a; };
template<> struct ht_ext_vec3<i32x3>  { using type = i32x3a; };
template<> struct ht_ext_vec3<u32x3>  { using type = u32x3a; };

template<typename T>
using ht_ext_vec3_t = typename ht_ext_vec3<T>::type;

template<typename T>
concept dx_vec3_t = requires { typename ht_ext_vec3<T>::type; };

constexpr bool32x3 operator==( const float3& a, const float3& b )
{
    return float3a{ a.x, a.y, a.z } == float3a{ b.x, b.y, b.z };
}
constexpr bool32x3 operator!=( const float3& a, const float3& b )
{
    return float3a{ a.x, a.y, a.z } != float3a{ b.x, b.y, b.z };
}
constexpr i32x3a operator<=>( const float3& a, const float3& b )
{
    float3a va = { a.x, a.y, a.z };
    float3a vb = { b.x, b.y, b.z };
    return ( va < vb ) - ( va > vb );
}

constexpr auto operator-( dx_vec3_t auto v )
{
    using V = ht_ext_vec3_t<decltype( v )>;
    V r = -V{ v.x, v.y, v.z };
    return decltype( v ){ r.x, r.y, r.z };
}
constexpr auto operator+( dx_vec3_t auto a, decltype( a ) b )
{
    using V = ht_ext_vec3_t<decltype( a )>;
    V r = V{ a.x, a.y, a.z } + V{ b.x, b.y, b.z };
    return decltype( a ){ r.x, r.y, r.z };
}
constexpr auto operator-( dx_vec3_t auto a, decltype( a ) b )
{
    using V = ht_ext_vec3_t<decltype( a )>;
    V r = V{ a.x, a.y, a.z } - V{ b.x, b.y, b.z };
    return decltype( a ){ r.x, r.y, r.z };
}
constexpr auto operator*( dx_vec3_t auto a, decltype( a.x ) scalar )
{
    using V = ht_ext_vec3_t<decltype( a )>;
    V r = V{ a.x, a.y, a.z } * scalar;
    return decltype( a ){ r.x, r.y, r.z };
}
constexpr auto& operator+=( dx_vec3_t auto& a, dx_vec3_t auto b ) { return a = a + b; }

namespace ht
{
	constexpr auto min( dx_vec3_t auto a, dx_vec3_t auto b )
	{
		using V = ht_ext_vec3_t<decltype( a )>;
		V r = ht::min( V{ a.x, a.y, a.z }, V{ b.x, b.y, b.z } );
		return decltype( a ){ r.x, r.y, r.z };
	}
	constexpr auto max( dx_vec3_t auto a, dx_vec3_t auto b )
	{
		using V = ht_ext_vec3_t<decltype( a )>;
		V r = ht::max( V{ a.x, a.y, a.z }, V{ b.x, b.y, b.z } );
		return decltype( a ){ r.x, r.y, r.z };
	}

	constexpr float dot( float3 a, float3 b ) { return ht::dot( float3a{ a.x, a.y, a.z }, float3a{ b.x, b.y, b.z } ); }
	constexpr i64 dot( i32x2 a, i32x2 b ) { return i64( a.x ) * b.x + i64( a.y ) * b.y; }
	constexpr u32 dot( u16x2 a, u16x2 b ) { return a.x * b.x + a.y * b.y; }
	constexpr i32 dot( i16x2 a, i16x2 b ) { return a.x * b.x + a.y * b.y; }

	constexpr float3 floor( float3 v ) { float3a r = ht::floor( float3a{ v.x, v.y, v.z } ); return { r.x, r.y, r.z }; }
	constexpr float3 ceil( float3 v ) { float3a r = ht::ceil( float3a{ v.x, v.y, v.z } ); return { r.x, r.y, r.z }; }
	constexpr float3 round( float3 v ) { float3a r = ht::round( float3a{ v.x, v.y, v.z } ); return { r.x, r.y, r.z }; }

	constexpr float3 normalize( float3 v )
	{
		float3a va = { v.x, v.y, v.z };
		float len = std::sqrt( ht::dot( va, va ) );
		float3a r = ( len > 0.0f ) ? va / len : float3a{};
		return { r.x, r.y, r.z };
	}
}

HT_FORCEINLINE float3 XM_CALLCONV DX_XMStoreFloat3( DirectX::XMVECTOR v )
{
	DirectX::XMFLOAT3 out = {};
	DirectX::XMStoreFloat3( &out, v );
	return out;
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

HT_FORCEINLINE DirectX::XMVECTOR XM_CALLCONV DX_XMLoadFloat3( float3 v )
{
	return DirectX::XMLoadFloat3( &v );
}

HT_FORCEINLINE DirectX::XMVECTOR XM_CALLCONV DX_XMLoadFloat4( float4 v )
{
	DirectX::XMFLOAT4 xm = { v.x, v.y, v.z, v.w };
	return DirectX::XMLoadFloat4( &xm );
}

#endif // !__HT_VEC_TYPES_H__
