#pragma once

#ifndef __HT_VEC_TYPES_H__
#define __HT_VEC_TYPES_H__

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

using u32x2		= DirectX::XMUINT2;
using u32x3		= DirectX::XMUINT3;

using float2	= DirectX::XMFLOAT2;
using float3	= DirectX::XMFLOAT3;
using float4	= DirectX::XMFLOAT4;

using float3x3 	= DirectX::XMFLOAT3X3;
using float4x3 	= DirectX::XMFLOAT4X3;
using float4x4 	= DirectX::XMFLOAT4X4A;

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

constexpr float2 operator-( float2 v )
{
	return { -v.x, -v.y };
}

constexpr float3 operator-( float3 v )
{
	return { -v.x, -v.y, -v.z };
}

constexpr float4 operator-( float4 v )
{
	return { -v.x, -v.y, -v.z, -v.w };
}


__forceinline float3 XM_CALLCONV DX_XMStoreFloat3( DirectX::XMVECTOR v )
{
	DirectX::XMFLOAT3 out;
	DirectX::XMStoreFloat3( &out, v );
	return out;
}

__forceinline float4 XM_CALLCONV DX_XMStoreFloat4( DirectX::XMVECTOR v )
{
	DirectX::XMFLOAT4 out;
	DirectX::XMStoreFloat4( &out, v );
	return out;
}

__forceinline float4x3 XM_CALLCONV DX_XMStoreFloat4x3( DirectX::XMMATRIX m )
{
	DirectX::XMFLOAT4X3 out;
	DirectX::XMStoreFloat4x3( &out, m );
	return out;
}

__forceinline float4x4 XM_CALLCONV DX_XMStoreFloat4x4A( DirectX::XMMATRIX m )
{
	DirectX::XMFLOAT4X4A out;
	DirectX::XMStoreFloat4x4A( &out, m );
	return out;
}

__forceinline DirectX::XMVECTOR XM_CALLCONV DX_XMLoadFloat3( float3 v )
{
	return DirectX::XMLoadFloat3( &v );
}

__forceinline DirectX::XMVECTOR XM_CALLCONV DX_XMLoadFloat4( float4 v )
{
	return DirectX::XMLoadFloat4( &v );
}

#endif // !__HT_VEC_TYPES_H__
