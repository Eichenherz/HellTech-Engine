#ifndef __VIRTUAL_CAMERA__
#define __VIRTUAL_CAMERA__

#include <Math/directx_math.hpp>
#include <algorithm>

#include "r_data_structs.h"


using namespace DirectX;

struct virtual_camera
{
	static constexpr DirectX::XMFLOAT3 fwdBasis = { 0, 0, 1 };
	static constexpr DirectX::XMFLOAT3 upBasis = { 0, 1, 0 };

	DirectX::XMMATRIX projection;

	DirectX::XMFLOAT3 worldPos = { 0,0,0 };
	// NOTE: pitch must be in [-pi/2,pi/2]
	float pitch = 0;
	float yaw = 0;

	float speed = 1.5f;

	inline void Move( DirectX::XMVECTOR camMove, DirectX::XMFLOAT2 dPos, float elapsedSecs )
	{
		XMMATRIX tRotScale = XMMatrixRotationRollPitchYaw( pitch, yaw, 0 ) * XMMatrixScaling( speed, speed, speed );
		camMove = XMVector3Transform( XMVector3Normalize( camMove ), tRotScale );

		XMVECTOR xmCamPos = XMLoadFloat3( &worldPos );
		XMVECTOR smoothNewCamPos = XMVectorLerp( xmCamPos, XMVectorAdd( xmCamPos, camMove ), 0.18f * elapsedSecs / 0.0166f );

		// TODO: thresholds
		//float moveLen = XMVectorGetX( XMVector3Length( smoothNewCamPos ) );
		XMStoreFloat3( &worldPos, smoothNewCamPos );

		yaw = XMScalarModAngle( yaw + dPos.x * elapsedSecs );
		pitch = std::clamp( pitch + dPos.y * elapsedSecs, -almostPiDiv2, almostPiDiv2 );
	}
	inline DirectX::XMVECTOR LookAt() const
	{
		return XMVector3Transform( XMLoadFloat3( &fwdBasis ), XMMatrixRotationRollPitchYaw( pitch, yaw, 0 ) );
	}
	inline DirectX::XMMATRIX ViewMatrix() const
	{
		auto lookAt = this->LookAt();
		XMVECTOR xmWorldPos = XMLoadFloat3( &worldPos );
		XMMatrixLookAtLH( xmWorldPos, XMVectorAdd( xmWorldPos, lookAt ), XMLoadFloat3( &upBasis ) );
	}
	inline DirectX::XMMATRIX FrustumMatrix() const
	{
		// NOTE: inv( A * B ) = inv B * inv A
		XMMATRIX invFrustMat = XMMatrixMultiply( ViewMatrix(), projection );
		XMVECTOR det = XMMatrixDeterminant( invFrustMat );
		return XMMatrixInverse( &det, invFrustMat );
	}

	inline camera_data GetGpuData( bool frustumFrozen ) const
	{
		XMMATRIX view = ViewMatrix();

		XMVECTOR viewDet = XMMatrixDeterminant( view );
		XMMATRIX invView = XMMatrixInverse( &viewDet, view );

		camera_data cd = {};

		XMStoreFloat3( &cd.camViewDir, XMVectorNegate( invView.r[ 2 ] ) );
		XMStoreFloat4x4A( &cd.proj, projection );
		XMStoreFloat4x4A( &cd.activeView, view );
		if( !frustumFrozen )
		{
			XMStoreFloat4x4A( &cd.mainView, view );
		}
		cd.worldPos = worldPos;

		XMStoreFloat4x4A( &cd.frustTransf, FrustumMatrix() );

		XMStoreFloat4x4A( &cd.activeProjView, XMMatrixMultiply( view, projection ) );
		XMStoreFloat4x4A( &cd.mainProjView, XMMatrixMultiply( XMLoadFloat4x4A( &cd.mainView ), projection ) );

		return cd;
	}
};

#endif // !__VIRTUAL_CAMERA__
