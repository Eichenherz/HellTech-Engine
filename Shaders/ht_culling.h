#pragma once

#ifndef __HT_CULLING_H__
#define __HT_CULLING_H__

#include "ht_hlsl_math.h"

// NOTE: frustum and HZB cull algo https://github.com/zeux/niagara/blob/master/src/shaders/drawcull.comp.glsl
// NOTE: bounds projection https://zeux.io/2023/01/12/approximate-projected-bounds/

struct frustum_culling_result
{
	bool visible;
	bool intersectsZNear;
};

// NOTE: Gribb-Hartmann method
frustum_culling_result FrustumCulling( in float3 aabbMin, in float3 aabbMax, in float4x4 mvp )
{
	float4x4	transpMvp = transpose( mvp );
	float4 		xPlanePos = transpMvp[ 3 ] + transpMvp[ 0 ];
	float4 		yPlanePos = transpMvp[ 3 ] + transpMvp[ 1 ];
	float4 		xPlaneNeg = transpMvp[ 3 ] - transpMvp[ 0 ];
	float4 		yPlaneNeg = transpMvp[ 3 ] - transpMvp[ 1 ];

	static const float3 ZERO = float3( 0.0f, 0.0f, 0.0f );

	bool visible = true;
	visible = visible &&
		( dot( lerp( aabbMax, aabbMin, float3( transpMvp[ 3 ].xyz < ZERO ) ), transpMvp[ 3 ].xyz ) > -transpMvp[ 3 ].w );
	visible = visible && ( dot( lerp( aabbMax, aabbMin, float3( xPlanePos.xyz < ZERO ) ), xPlanePos.xyz ) > -xPlanePos.w );
	visible = visible && ( dot( lerp( aabbMax, aabbMin, float3( yPlanePos.xyz < ZERO ) ), yPlanePos.xyz ) > -yPlanePos.w );
	visible = visible && ( dot( lerp( aabbMax, aabbMin, float3( xPlaneNeg.xyz < ZERO ) ), xPlaneNeg.xyz ) > -xPlaneNeg.w );
	visible = visible && ( dot( lerp( aabbMax, aabbMin, float3( yPlaneNeg.xyz < ZERO ) ), yPlaneNeg.xyz ) > -yPlaneNeg.w );

	float minW = dot( lerp( aabbMax, aabbMin, float3( transpMvp[ 3 ].xyz >= ZERO ) ), transpMvp[ 3 ].xyz ) + transpMvp[ 3 ].w;
	bool intersectsZNear = minW <= 0.0f;

	frustum_culling_result res = { visible, intersectsZNear };
	return res;
}

// NOTE: from zeux
bool ProjectBoundsToScreenSpace(
	in 	float3			boundsMin,
	in 	float3			boundsMax,
	in 	float4x4		mvp,
	in 	float			zNear,
	out float4			outUVBox,
	out float			outMaxZ
) {
	float3 extent		= boundsMax - boundsMin;
	float4 projExtentX 	= mul( float4( extent.x, 0.0f, 0.0f, 0.0f ), mvp );
	float4 projExtentY 	= mul( float4( 0.0f, extent.y, 0.0f, 0.0f ), mvp );
	float4 projExtentZ 	= mul( float4( 0.0f, 0.0f, extent.z, 0.0f ), mvp );

	float4 clip0 = mul( float4( boundsMin, 1.0f ), mvp );
	float4 clip1 = clip0 + projExtentZ;
	float4 clip2 = clip0 + projExtentY;
	float4 clip3 = clip2 + projExtentZ;
	float4 clip4 = clip0 + projExtentX;
	float4 clip5 = clip4 + projExtentZ;
	float4 clip6 = clip4 + projExtentY;
	float4 clip7 = clip6 + projExtentZ;

	// NOTE: near plane rejection - before persp divide, we “early” out if the box intersects or is behind zNear
	float minW = Min8_f32( clip0.w, clip1.w, clip2.w, clip3.w, clip4.w, clip5.w, clip6.w, clip7.w );
	if( minW < zNear )
	{
		outUVBox = 0.0f;
		outMaxZ  = 0.0f;
		return false;
	}

	float2 boxMin = min(
		Min4_f32x2( clip0.xy / clip0.w, clip1.xy / clip1.w, clip2.xy / clip2.w, clip3.xy / clip3.w ),
		Min4_f32x2( clip4.xy / clip4.w, clip5.xy / clip5.w, clip6.xy / clip6.w, clip7.xy / clip7.w ) );
	float2 boxMax = max(
		Max4_f32x2( clip0.xy / clip0.w, clip1.xy / clip1.w, clip2.xy / clip2.w, clip3.xy / clip3.w ),
		Max4_f32x2( clip4.xy / clip4.w, clip5.xy / clip5.w, clip6.xy / clip6.w, clip7.xy / clip7.w ) );

	// NOTE: clip space to uv space
	outUVBox = float4( boxMin, boxMax ).xwzy * float4( 0.5f, -0.5f, 0.5f, -0.5f ) +  0.5f;
	// Largest NDC z = NDC z of corner with smallest clip.w; Rev Z infinite-far matrix: ndc.z = zNear / clip.w
	outMaxZ = zNear / minW;

	return true;
}

struct visibility_res
{
	bool inFrustum;
	bool notOccluded;
};

visibility_res TestVisibility(
	in float3               aabbMin,
	in float3               aabbMax,
	in float4x4             toWorld,
	in view_data            cam,
	in Texture2D<float4>    hzb,
	in SamplerState         quadMin,
	in bool                 isLatePass
) {
	bool intersectsZNear = false;
	bool inFrustum = true;

	if( !isLatePass )
	{
		// NOTE: 1st pass runs frustum culling with current instTransform and current cam
		frustum_culling_result frustumCullRes = FrustumCulling( aabbMin, aabbMax, mul( toWorld, cam.mainViewProj ) );
		// NOTE: we might be visible but if we intersect the znear we skip occlusion
		intersectsZNear = frustumCullRes.intersectsZNear;
		inFrustum = frustumCullRes.visible;
	}

	bool notOccluded = true;
	if( inFrustum && !intersectsZNear )
	{
		// NOTE: 1st pass uses prev instTransform, prevView and prev HZB
		float4x4 view	= isLatePass ? cam.mainView : cam.prevView;
		float4x4 mvp	= mul( toWorld, mul( view, cam.proj ) );

		float4 uvBounds = 0.0f; float maxZ = 0.0f;
		if( ProjectBoundsToScreenSpace( aabbMin, aabbMax, mvp, cam.zNear, uvBounds, maxZ ) )
		{
			u32x3 widthHeightMipCount;
			hzb.GetDimensions( 0, widthHeightMipCount.x, widthHeightMipCount.y, widthHeightMipCount.z );

			float2 sizeTexels = float2( uvBounds.z - uvBounds.x, uvBounds.w - uvBounds.y ) * widthHeightMipCount.xy;

			// NOTE: from niagara
			// Because we only consider 2x2 pixels, we need to make sure we are sampling from a mip that reduces
			// the rectangle to 1x1 texel or smaller. Due to the rectangle being arbitrarily offset, a 1x1 rectangle
			// may cover 2x2 texel area. Using floor() here would require sampling 4 corners of AABB
			// (using bilinear fetch), which is a little slower.
			float chosenMipLevel = ceil( log2( max( sizeTexels.x, sizeTexels.y ) ) );

			float2 uv = ( uvBounds.xy + uvBounds.zw ) * 0.5f;

			float sampledDepth = hzb.SampleLevel( quadMin, uv, min( chosenMipLevel, float( widthHeightMipCount.z - 1 ) ) ).x;

			// NOTE: bc we use reverse Z
			notOccluded = ( sampledDepth <= maxZ );
		}
	}

	visibility_res res = { inFrustum, notOccluded };
	return res;
}

#endif // !__HT_CULLING_H__
