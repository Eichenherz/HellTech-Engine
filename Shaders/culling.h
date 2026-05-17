#ifndef __CULLING_H__
#define __CULLING_H__


struct frustum_culling_result
{
	bool visible;
	bool intersectsZNear;
};

// NOTE: Gribb-Hartmann method
frustum_culling_result FrustumCulling( float3 aabbMin, float3 aabbMax, float4x4 mvp )
{
	float4x4 transpMvp = transpose( mvp );
	float4 xPlanePos = transpMvp[ 3 ] + transpMvp[ 0 ];
	float4 yPlanePos = transpMvp[ 3 ] + transpMvp[ 1 ];
	float4 xPlaneNeg = transpMvp[ 3 ] - transpMvp[ 0 ];
	float4 yPlaneNeg = transpMvp[ 3 ] - transpMvp[ 1 ];

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

struct screenspace_aabb
{
	float2	minXY;
	float2	maxXY;
	float	maxZ;
};

screenspace_aabb ProjectAabbToScreenSpace( in float3 aabbMin, in float3 aabbMax, in float4x4 mvp )
{
	float3 aabbSize = aabbMax - aabbMin;
	float3 aabbCorners[] = { 
		aabbMin,
		aabbMin + float3( aabbSize.x, 0.0f, 0.0f ),
		aabbMin + float3( 0.0f, aabbSize.y, 0.0f ),
		aabbMin + float3( 0.0f, 0.0f, aabbSize.z ),
		aabbMin + float3( aabbSize.xy, 0.0f ),
		aabbMin + float3( 0.0f, aabbSize.yz ),
		aabbMin + float3( aabbSize.x, 0.0f, aabbSize.z ),
		aabbMin + aabbSize
	};

	float2	minXY = float2( 1.0f, 1.0f );
	float2	maxXY = float2( 0.0f, 0.0f );
	float	maxZ  = 0.0f;

	[unroll]
	for( u32 ci = 0; ci < 8; ++ci )
	{
		float4 clipPos = mul( float4( aabbCorners[ ci ], 1.0f ), mvp );
		clipPos.xyz = clipPos.xyz / clipPos.w;
		clipPos.xy = clamp( clipPos.xy, -1.0f, 1.0f );
		clipPos.xy = clipPos.xy * float2( 0.5f, -0.5f ) + 0.5f;

		minXY = min( clipPos.xy, minXY );
		maxXY = max( clipPos.xy, maxXY );
		maxZ  = max( maxZ, clipPos.z );
	}

	screenspace_aabb res = { minXY, maxXY, maxZ };
	return res;
}

bool ScreenSpaceAabbVsHiZ( in screenspace_aabb ssAabb, in Texture2D<float4> hizTex, in SamplerState quadMin )
{
	u32x3 widthHeightMipCount;
	hizTex.GetDimensions( 0, widthHeightMipCount.x, widthHeightMipCount.y, widthHeightMipCount.z );
	
	float2 size = abs( ssAabb.maxXY - ssAabb.minXY ) * float2( widthHeightMipCount.xy );
	float maxMipLevel = float( widthHeightMipCount.z ) - 1.0f;
				
	float chosenMipLevel = min( floor( log2( max( size.x, size.y ) ) ), maxMipLevel );
			
	float2 uv = ( ssAabb.maxXY + ssAabb.minXY ) * 0.5f;
	
	float sampledDepth = hizTex.SampleLevel( quadMin, uv, chosenMipLevel ).x;
	// NOTE: bc we use reverse Z
	return ( sampledDepth <= ssAabb.maxZ );
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
	in Texture2D<float4>    hizTex,
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
		float4x4 view = isLatePass ? cam.mainView : cam.prevView;
		screenspace_aabb ssAabb = ProjectAabbToScreenSpace( aabbMin, aabbMax, mul( toWorld, mul( view, cam.proj ) ) );
		notOccluded = ScreenSpaceAabbVsHiZ( ssAabb, hizTex, quadMin );
	}

	visibility_res res = { inFrustum, notOccluded };
	return res;
}

#endif // !__CULLING_H__
