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

	float3 ZERO = float3( 0.0f, 0.0f, 0.0f );

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
	float2 minXY;
	float2 maxXY;
	float maxZ;
};

screenspace_aabb ProjectAabbToScreenSpace( float3 aabbMin, float3 aabbMax, float4x4 mvp )
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
		aabbMin + aabbSize };

	float2 minXY = float2( 1.0f, 1.0f );
	float2 maxXY = float2( 0.0f, 0.0f );
	float maxZ = 0.0f;

	[unroll]
	for( uint i = 0; i < 8; ++i )
	{
		float4 clipPos = mul( float4( aabbCorners[ i ], 1.0f ), mvp );
		clipPos.xyz = clipPos.xyz / clipPos.w;
		clipPos.xy = clamp( clipPos.xy, -1.0f, 1.0f );
		clipPos.xy = clipPos.xy * float2( 0.5f, -0.5f ) + float2( 0.5f, 0.5f );

		minXY = min( clipPos.xy, minXY );
		maxXY = max( clipPos.xy, maxXY );
		maxZ = max( maxZ, clipPos.z );
	}

	screenspace_aabb res = { minXY, maxXY, maxZ };
	return res;
}

bool ScreenSpaceAabbVsHiZ( in screenspace_aabb ssAabb, in Texture2D<float4> hizTex, in SamplerState quadMin )
{
	uint3 widthHeightMipCount;
	hizTex.GetDimensions( 0, widthHeightMipCount.x, widthHeightMipCount.y, widthHeightMipCount.z );
	
	float2 size = abs( ssAabb.maxXY - ssAabb.minXY ) * float2( widthHeightMipCount.xy );
	float maxMipLevel = float( widthHeightMipCount.z ) - 1.0f;
				
	float chosenMipLevel = min( floor( log2( max( size.x, size.y ) ) ), maxMipLevel );
			
	float2 uv = ( ssAabb.maxXY + ssAabb.minXY ) * 0.5f;
	
	float sampledDepth = hizTex.SampleLevel( quadMin, uv, chosenMipLevel ).x;
	
	return ( sampledDepth <= ssAabb.maxZ );
}

#endif // !__CULLING_H__
