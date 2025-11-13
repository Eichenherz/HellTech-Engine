#ifndef __ENCODING_H__
#define __ENCODING_H__

#include "ht_math.h"
// TODO: use DirectXMath ?
// TODO: fast ?
inline float SignNonZero( float e )
{
	return ( e >= 0.0f ) ? 1.0f : -1.0f;
}
inline DirectX::XMFLOAT2 OctaNormalEncode( DirectX::XMFLOAT3 n )
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
inline float EncodeTanToAngle( DirectX::XMFLOAT3 n, DirectX::XMFLOAT3 t )
{
	using namespace DirectX;

	// NOTE: inspired by Doom Eternal
	XMFLOAT3 tanRef = ( std::abs( n.x ) > std::abs( n.z ) ) ? XMFLOAT3{ -n.y, n.x, 0.0f } : XMFLOAT3{ 0.0f, -n.z, n.y };

	float tanRefAngle = XMVectorGetX( XMVector3AngleBetweenVectors( XMLoadFloat3( &t ), XMLoadFloat3( &tanRef ) ) );
	return XMScalarModAngle( tanRefAngle ) * XM_1DIVPI;
}

#endif // !__ENCODING_H__
