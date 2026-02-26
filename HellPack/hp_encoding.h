#ifndef __HP_ENCODING_H__
#define __HP_ENCODING_H__

#include "core_types.h"
#include "hp_math.h"

inline float SignNonZero( float e )
{
	return ( e >= 0.0f ) ? 1.0f : -1.0f;
}
inline float2 OctaNormalEncode( float3 n )
{
	// NOTE: Project the sphere onto the octahedron, and then onto the xy plane
	float absLen = std::fabs( n.x ) + std::fabs( n.y ) + std::fabs( n.z );
	float absNorm = ( absLen == 0.0f ) ? 0.0f : 1.0f / absLen;
	float nx = n.x * absNorm;
	float ny = n.y * absNorm;

	// NOTE: Reflect the folds of the lower hemisphere over the diagonals
	float octaX = ( n.z < 0.f ) ? ( 1.0f - std::fabs( ny ) ) * SignNonZero( nx ) : nx;
	float octaY = ( n.z < 0.f ) ? ( 1.0f - std::fabs( nx ) ) * SignNonZero( ny ) : ny;

	return { octaX, octaY };
}
// TODO: use angle between normals ?
inline float EncodeTanToAngle( float3 n, float3 t )
{
	using namespace DirectX;

	XMFLOAT3 tan = ToDX( t );

	// NOTE: inspired by Doom Eternal
	XMFLOAT3 tanRef = ( std::abs( n.x ) > std::abs( n.z ) ) ? XMFLOAT3{ -n.y, n.x, 0.0f } : XMFLOAT3{ 0.0f, -n.z, n.y };

	float tanRefAngle = XMVectorGetX( XMVector3AngleBetweenVectors( XMLoadFloat3( &tan ), XMLoadFloat3( &tanRef ) ) );
	return XMScalarModAngle( tanRefAngle ) * XM_1DIVPI;
}

using snorm8x4 = u32;

#include <meshoptimizer.h>

inline snorm8x4 EncodeTanFrame( float3 n, float3 t )
{
	float2 octaNormal = OctaNormalEncode( n );
	float tanAngle = EncodeTanToAngle( n, t );

	i8 snormNx = meshopt_quantizeSnorm( octaNormal.x, 8 );
	i8 snormNy = meshopt_quantizeSnorm( octaNormal.y, 8 );
	i8 snormTanAngle = meshopt_quantizeSnorm( tanAngle, 8 );

	u32 bitsSnormNx = *( u8* ) &snormNx;
	u32 bitsSnormNy = *( u8* ) &snormNy;
	u32 bitsSnormTanAngle = *( u8* ) &snormTanAngle;

	snorm8x4 packedTanFrame = bitsSnormNx | ( bitsSnormNy << 8 ) | ( bitsSnormTanAngle << 16 );
	return packedTanFrame;
}

#endif // !__HP_ENCODING_H__
