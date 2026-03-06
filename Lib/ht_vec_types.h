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

using float2 = DirectX::XMFLOAT2;
using float3 = DirectX::XMFLOAT3;
using float4 = DirectX::XMFLOAT4;

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

using float4x4 = DirectX::XMFLOAT4X4A;

#endif // !__HT_VEC_TYPES_H__
