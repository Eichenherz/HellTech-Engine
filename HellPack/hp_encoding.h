#pragma once

#ifndef __HP_ENCODING_H__
#define __HP_ENCODING_H__

#include <ht_core_types.h>

#include <meshoptimizer.h>

#include <ht_math.h>
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
	float absLen = std::fabsf( n.x ) + std::fabsf( n.y ) + std::fabsf( n.z );
	float absNorm = ( absLen == 0.0f ) ? 0.0f : 1.0f / absLen;
	float nx = n.x * absNorm;
	float ny = n.y * absNorm;

	// NOTE: Reflect the folds of the lower hemisphere over the diagonals
	float octaX = ( n.z >= 0.f ) ? nx : ( 1.0f - std::fabsf( ny ) ) * SignNonZero( nx );
	float octaY = ( n.z >= 0.f ) ? ny : ( 1.0f - std::fabsf( nx ) ) * SignNonZero( ny );

	return { octaX, octaY };
}
// NOTE: Rune Stubbe's version : https://twitter.com/Stubbesaurus/status/937994790553227264
inline float3 DecodeOctaNormal( float2 octa )
{
	using namespace DirectX;

	float3  n = { octa.x, octa.y, 1.0f - std::fabsf( octa.x ) - std::fabsf( octa.y ) };
	float   t = std::fmaxf( -n.z, 0.0f );
	n.x += ( n.x >= 0.0f ) ? t : -t;
	n.y += ( n.y >= 0.0f ) ? t : -t;

	return DX_XMStoreFloat3( XMVector3Normalize( DX_XMLoadFloat3( n ) ) );
}

// NOTE: https://zeux.io/2026/04/30/quantizing-tangent-frames/
// NOTE: from `Building an Orthonormal Basis, Revisited`
inline void BuildBasisFromNormalDuffFrisvad( float3 n, float3& oTan, float3& oBitan )
{
	float sign  = SignNonZero( n.z );
	float a     = -1.0f / ( sign + n.z );
	float b     = n.x * n.y * a;
	oTan        = { 1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x };
	oBitan	    = { b, sign + n.y * n.y * a, -n.y };

}
// NOTE: must use reconstructed normal not original
inline float EncodeTanToAngle( float3 decodedNormal, float3 t )
{
	using namespace DirectX;

	float3 tanRef, bitanRef; BuildBasisFromNormalDuffFrisvad( decodedNormal, tanRef, bitanRef );

	float cosA  = t.x * tanRef.x + t.y * tanRef.y + t.z * tanRef.z;
	float sinA  = t.x * bitanRef.x  + t.y * bitanRef.y  + t.z * bitanRef.z;
	float angle = std::atan2f( sinA, cosA );  // [-π, π]

	return angle * XM_1DIVPI; // [-1, 1] for snorm
}

static_assert( ( 2 * BIT_DEPTH_OCT_N + BIT_DEPTH_TAN_A + BIT_DEPTH_BTAN_S ) == BitCount<u32>() );

inline oct11x2s_a9_s1 EncodeTanFrame( float3 n, float3 t, float bs )
{
	float2  octaNormal      = EncodeOctaNormal( n );
	// NOTE: no it's not redundant, we need to encode the tan based on this
	float3  decodedNormal   = DecodeOctaNormal( octaNormal );
	float   tanAngle        = EncodeTanToAngle( decodedNormal, t );

	u32     nx              = SnormBits<BIT_DEPTH_OCT_N>( octaNormal.x );
	u32     ny              = SnormBits<BIT_DEPTH_OCT_N>( octaNormal.y );
	u32     ta              = SnormBits<BIT_DEPTH_TAN_A>( tanAngle );
	u32     bh              = ( std::signbit( bs ) ? 1u : 0u );

	return nx | ( ny << BIT_DEPTH_OCT_N )
		 | ( ta << ( 2 * BIT_DEPTH_OCT_N ) )
		 | ( bh << ( 2 * BIT_DEPTH_OCT_N + BIT_DEPTH_TAN_A ) );
}

// NOTE: https://graphics.stanford.edu/~seander/bithacks.html#VariableSignExtend
inline i32 RestoreSign( u32 comp, u32 bitDepth )
{
	i32 bitmask = 1u << ( bitDepth - 1 );
	return i32( ( comp ^ bitmask ) - bitmask );
}

struct mlt_quantized_grid
{
	float3	quantAabbMin;
	float3	quantAabbMax;
	u32x3	bitDepthPerAxis;
    u32	    gridResInBits;
    u32	    gridStep;
    float	gridQuantMaxErr;
};

inline mlt_quantized_grid HpkMakeMltQuantizedGrid( aabb_t<float3> meshletAabb )
{
    float3  aabbExt         = ( meshletAabb.max - meshletAabb.min ) * 0.5f;
    u32     maxDim          = ( u32 ) std::ceilf( std::max( { aabbExt.x, aabbExt.y, aabbExt.z } ) );
    // NOTE: 23 bc floats have 23 bits for mantissa
    u32     gridResInBits   = std::min( 21, 23 - std::bit_width( maxDim ) );
	u32     gridStep        = 1u << gridResInBits;

    float   invGridFactor   = 1.0f / float( gridStep );

	float3 snappedAabbMin = hpk::floorf( meshletAabb.min * ( float ) gridStep );
    float3 snappedAabbMax = hpk::ceilf( meshletAabb.max * ( float ) gridStep );

	u32x3 mltBitDepthPerAxis = {
		( u32 ) std::bit_width<u32>( ( u32 ) std::abs( snappedAabbMax.x - snappedAabbMin.x ) ),
		( u32 ) std::bit_width<u32>( ( u32 ) std::abs( snappedAabbMax.y - snappedAabbMin.y ) ),
		( u32 ) std::bit_width<u32>( ( u32 ) std::abs( snappedAabbMax.z - snappedAabbMin.z ) )
	};
	HT_ASSERT( u32x3{} != mltBitDepthPerAxis );

	return {
		.quantAabbMin		= snappedAabbMin * invGridFactor,
		.quantAabbMax		= snappedAabbMax * invGridFactor,
		.bitDepthPerAxis	= mltBitDepthPerAxis,
	    .gridResInBits      = gridResInBits,
	    .gridStep           = gridStep,
	    .gridQuantMaxErr    = 0.5f / float( gridStep )
	};
}

// NOTE: vtx quant from https://daniilvinn.github.io/2024/05/04/omniforce-vertex-quantization.html
inline u32 QuantizeVertexPosCompWithAnchor( float comp, float quantAnchor, u32 gridBitDepth )
{
    i32 quantPos = ( i32 ) std::roundf( ( comp - quantAnchor ) * float( 1u << gridBitDepth ) );
    HT_ASSERT( quantPos >= 0 );
    return u32( quantPos );
}

inline u32x3 HpkEncodeMltVertexPosition( const mlt_quantized_grid& grid, float3 p )
{
	return {
		QuantizeVertexPosCompWithAnchor( p.x, grid.quantAabbMin.x, grid.gridResInBits ),
		QuantizeVertexPosCompWithAnchor( p.y, grid.quantAabbMin.y, grid.gridResInBits ),
		QuantizeVertexPosCompWithAnchor( p.z, grid.quantAabbMin.z, grid.gridResInBits )
	};
}

inline float DecodeVertexPosCompWithAnchor( u32 comp, float gridStep, float minBoundQuant, u32 mltBitDepth )
{
    u32 bidDepthMask = ( 1u << mltBitDepth ) - 1;
    return float( i32( comp & bidDepthMask ) ) / gridStep + minBoundQuant;
}

inline float HpkMaxQuantError( float comp, float gridQuantMaxErr )
{
    // NOTE: floats get "sparser" as they get bigger
    // so our meshGridQuantMaxErr would simply fall through one of those gaps.
    // This is inherent to how fp works
    float ulpErr = std::nextafterf( std::fabsf( comp ), FLT_MAX ) - std::fabsf( comp );
    return gridQuantMaxErr + 2.0f * ulpErr;
}

inline bool HpkDecodeVerifyQuantized( float3 pos, u32x3 encPos, const mlt_quantized_grid& grid )
{
	float decX = DecodeVertexPosCompWithAnchor(
	    encPos.x, ( float ) grid.gridStep, grid.quantAabbMin.x, grid.bitDepthPerAxis.x );
	float decY = DecodeVertexPosCompWithAnchor(
	    encPos.y, ( float ) grid.gridStep, grid.quantAabbMin.y, grid.bitDepthPerAxis.y );
	float decZ = DecodeVertexPosCompWithAnchor(
	    encPos.z, ( float ) grid.gridStep, grid.quantAabbMin.z, grid.bitDepthPerAxis.z );

	float3 quantErr     = {
	    std::fabsf( pos.x - decX ), std::fabsf( pos.y - decY ), std::fabsf( pos.z - decZ )
	};
	float3 maxQuantErr  = {
	    HpkMaxQuantError( pos.x, grid.gridQuantMaxErr ),
	    HpkMaxQuantError( pos.y, grid.gridQuantMaxErr ),
	    HpkMaxQuantError( pos.z, grid.gridQuantMaxErr )
	};

	return quantErr <= maxQuantErr;
}

#endif // !__HP_ENCODING_H__
