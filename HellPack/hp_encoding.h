#pragma once

#ifndef __HP_ENCODING_H__
#define __HP_ENCODING_H__

#include <ht_core_types.h>

#include "ht_renderer_types.h"

inline float SignNonZero( float e )
{
	return ( e >= 0.0f ) ? 1.0f : -1.0f;
}

template<u32 BIT_COUNT>
constexpr u32 SnormBits( float v )
{
	constexpr u32 MASK = ( 1u << BIT_COUNT ) - 1;
	return std::bit_cast<u32>( meshopt_quantizeSnorm( v, BIT_COUNT ) ) & MASK;
}

inline float2 EncodeOctaNormal( float3 n )
{
	// NOTE: Project the sphere onto the octahedron, and then onto the xy plane
	float absLen = std::fabs( n.x ) + std::fabs( n.y ) + std::fabs( n.z );
	float absNorm = ( absLen == 0.0f ) ? 0.0f : 1.0f / absLen;
	float nx = n.x * absNorm;
	float ny = n.y * absNorm;

	// NOTE: Reflect the folds of the lower hemisphere over the diagonals
	float octaX = ( n.z >= 0.f ) ? nx : ( 1.0f - std::fabs( ny ) ) * SignNonZero( nx );
	float octaY = ( n.z >= 0.f ) ? ny : ( 1.0f - std::fabs( nx ) ) * SignNonZero( ny );

	return { octaX, octaY };
}
// NOTE: Rune Stubbe's version : https://twitter.com/Stubbesaurus/status/937994790553227264
inline float3 DecodeOctaNormal( float2 octa )
{
	using namespace DirectX;

	float3 n = { octa.x, octa.y, 1.0f - std::abs( octa.x ) - std::abs( octa.y ) };
	float t = std::max( -n.z, 0.0f );
	n.x += ( n.x >= 0.0f ) ? t : -t;
	n.y += ( n.y >= 0.0f ) ? t : -t;

	return DX_XMStoreFloat3( XMVector3Normalize( DX_XMLoadFloat3( n ) ) );
}

// NOTE: https://zeux.io/2026/04/30/quantizing-tangent-frames/
// NOTE: from `Building an Orthonormal Basis, Revisited`
struct tan_bitan
{
	float3 tan;
	float3 bitan;
};

inline tan_bitan BuildBasisFromNormalDuffFrisvad( float3 n  )
{
	float sign = SignNonZero( n.z );
	float a = -1.0f / ( sign + n.z );
	float b = n.x * n.y * a;
	return {
		.tan	= { 1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x },
		.bitan	= { b, sign + n.y * n.y * a, -n.y }
	};

}
// NOTE: must use reconstructed normal not original
inline float EncodeTanToAngle( float3 decodedNormal, float3 t )
{
	using namespace DirectX;

	auto[ tanRef, bitanRef ] = BuildBasisFromNormalDuffFrisvad( decodedNormal );

	float cosA  = t.x * tanRef.x + t.y * tanRef.y + t.z * tanRef.z;
	float sinA  = t.x * bitanRef.x  + t.y * bitanRef.y  + t.z * bitanRef.z;
	float angle = std::atan2f( sinA, cosA );  // [-π, π]

	return angle * XM_1DIVPI; // [-1, 1] for snorm
}

#include <meshoptimizer.h>

constexpr u32 BIT_DEPTH_OCT_N = 11;
constexpr u32 BIT_DEPTH_TAN_A = 9;
constexpr u32 BIT_DEPTH_BTAN_S = 1;

static_assert( ( 2 * BIT_DEPTH_OCT_N + BIT_DEPTH_TAN_A + BIT_DEPTH_BTAN_S ) == BitCount<u32>() );

inline oct10x2s_a11_s1 EncodeTanFrame( float3 n, float3 t, float bs )
{
	float2 octaNormal = EncodeOctaNormal( n );
	// NOTE: no it's not redundant, we need to encode the tan based on this
	float3 decodedNormal = DecodeOctaNormal( octaNormal );
	float tanAngle = EncodeTanToAngle( decodedNormal, t );

	u32 nx = SnormBits<BIT_DEPTH_OCT_N>( octaNormal.x );
	u32 ny = SnormBits<BIT_DEPTH_OCT_N>( octaNormal.y );
	u32 ta = SnormBits<BIT_DEPTH_TAN_A>( tanAngle );
	u32 bh = ( std::signbit( bs ) ? 1u : 0u );

	return nx | ( ny << BIT_DEPTH_OCT_N )
		 | ( ta << ( 2 * BIT_DEPTH_OCT_N ) )
		 | ( bh << ( 2 * BIT_DEPTH_OCT_N + BIT_DEPTH_TAN_A ) );
}

// NOTE: from // NOTE: vtx quant from https://daniilvinn.github.io/2024/05/04/omniforce-vertex-quantization.html
inline u32 QuantizeVertexPosComp( float comp, u32 vtxBitrate, u32 mltBitrate )
{
	// Quantize with multiplication by pow( 2, precision ) with further rounding
	i32 v = ( i32 ) std::round( comp * float( 1u << vtxBitrate ) );
	return ( v < 0 ) ? ( ( 1u << mltBitrate ) + v ) : v; // https://graphics.stanford.edu/~seander/bithacks.html#VariableSignExtend
}

#endif // !__HP_ENCODING_H__
