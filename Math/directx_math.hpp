#ifndef __DIRECTX_MATH__
#define __DIRECTX_MATH__

// TODO: no clang ?
#ifdef __clang__
// NOTE: clang-cl on VS issue
#undef __clang__
#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>
#define __clang__

#elif _MSC_VER >= 1916

#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>

constexpr float almostPiDiv2 = 0.995f * DirectX::XM_PIDIV2;

// TODO: fromalize world coord
template<bool LH = true>
inline DirectX::XMMATRIX PerspectiveReverseZInfiniteZFar( float fovYRads, float aspectRatioWH, float zNear )
{
	float sinFov;
	float cosFov;

	DirectX::XMScalarSinCos( &sinFov, &cosFov, fovYRads * 0.5f );

	float h = cosFov / sinFov;
	float w = h / aspectRatioWH;

	return {
		DirectX::XMVectorSet( w, 0, 0, 0 ),
		DirectX::XMVectorSet( 0, h, 0, 0 ),
		DirectX::XMVectorSet( 0, 0, 0, zNear ),
		LH ? DirectX::XMVectorSet( 0, 0, 1, 0 ) : DirectX::XMVectorSet( 0, 0, -1, 0 )
	};
}


#endif // !__clang__

#endif // !__DIRECTX_MATH__
