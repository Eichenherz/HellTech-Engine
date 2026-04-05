#include "r_data_structs2.h"

// NOTE: taken from Lottes 2016, "Advanced Techniques and Optimization of HDR Color Pipelines"
float TonemapLottesCurve( float hdrLum )
{
	const float a = 1.6f;
	const float d = 0.977f;
	const float hdrMax = 8.0f;
	const float midIn = 0.18f;
	const float midOut = 0.267f;

	const float b =
        ( -pow( midIn, a ) + pow( hdrMax, a ) * midOut ) /
        ( ( pow( hdrMax, a * d ) - pow( midIn, a * d ) ) * midOut );
	const float c =
        ( pow( hdrMax, a * d ) * pow( midIn, a ) - pow( hdrMax, a ) * pow( midIn, a * d ) * midOut ) /
        ( ( pow( hdrMax, a * d ) - pow( midIn, a * d ) ) * midOut );

	return pow( hdrLum, a ) / ( pow( hdrLum, a * d ) * b + c );
}

float LumTonemapAcesFilmCurve( float x )
{
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;
	return clamp( ( x * ( a * x + b ) ) / ( x * ( c * x + d ) + e ), 0.0, 1.0 );
}

float3 RgbTonemapAcesFilmCurve( float3 x )
{
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;
	return clamp( ( x * ( a * x + b ) ) / ( x * ( c * x + d ) + e ), 0.0, 1.0 );
}

// NOTE: taken from https://github.com/CesiumGS/cesium/tree/master/Source/Shaders/Builtin/Functions
// NOTE: vals taken from http://www.brucelindbloom.com/index.html?Math.html
// NOTE: x is luminance
float3 RgbToYxy( float3 rgb )
{
	const float3x3 RGB2XYZ = transpose( float3x3(
        0.4497288f, 0.3162486f, 0.1844926f,
        0.2446525f, 0.6720283f, 0.0833192f,
        0.0251848f, 0.1411824f, 0.9224628f ) );


	float3 xyz = mul( rgb, RGB2XYZ );
	float3 Yxy;
	Yxy.x = xyz.y;
	float temp = dot( float3( 1.0f, 1.0f, 1.0f ), xyz );
	Yxy.yz = xyz.xy / temp;
	return Yxy;
}

float3 YxyToRgb( float3 Yxy )
{
	const float3x3 XYZ2RGB = transpose( float3x3(
        2.9515373, -1.2894116, -0.4738445,
        -1.0851093, 1.9908566, 0.0372026,
        0.0854934, -0.2694964, 1.0912975 ) );

	float3 xyz;
	xyz.x = Yxy.x * Yxy.y / Yxy.z;
	xyz.y = Yxy.x;
	xyz.z = Yxy.x * (1.0f - Yxy.y - Yxy.z) / Yxy.z;

	return mul( xyz, XYZ2RGB );
}

// NOTE: taken from https://gamedev.stackexchange.com/questions/92015/optimized-linear-to-srgb-glsl
float4 LinearToSrgb( float4 linearRgb )
{
	bool3 cutoff = linearRgb.rgb <= float3( 0.0031308f, 0.0031308f, 0.0031308f );
	float3 toPowInv2Dot4 = pow( linearRgb.rgb, float3( 1.0f / 2.4f, 1.0f / 2.4f, 1.0f / 2.4f ) );
	float3 higher = float3( 1.055f, 1.055f, 1.055f ) * toPowInv2Dot4 + float3( -0.055f, -0.055f, -0.055f );
	float3 lower = linearRgb.rgb * float3( 12.92f, 12.92f, 12.92f );

	return float4( lerp( higher, lower, cutoff ), linearRgb.a );
}

[[vk::push_constant]]
struct {
	uint hdrColSrcIdx;
	uint sdrColDstIdx;
	uint avgLumIdx;
} pushBlock;

[shader("compute")]
[numthreads(16,16,1)]
void TonemappingGammaCsMain( uint3 globalThreadDispatchID : SV_DispatchThreadID )
{
	Texture2D<float4> hdrColSrc = gTexture2D_float4[ pushBlock.hdrColSrcIdx ];
	
	uint2 hdrColSrcSize;
	// NOTE: both imgs should have the same res, asserted on in cpp
	hdrColSrc.GetDimensions( hdrColSrcSize.x, hdrColSrcSize.y );
	if( ( globalThreadDispatchID.x > hdrColSrcSize.x ) || ( globalThreadDispatchID.y > hdrColSrcSize.y ) )
	{
		return;
	}
	
	float3 rgb = hdrColSrc.Load( int3( globalThreadDispatchID.xy, 0 ) ).rgb;
	float3 yxy = RgbToYxy( rgb );
    
	float avgLum = storageBuffers[ pushBlock.avgLumIdx ].Load<float>(0);
	float lum = yxy.x / ( 9.6f * avgLum + 0.0001f );
    
	yxy.x = TonemapLottesCurve( lum );
    //yxy.x = LumTonemapAcesFilmCurve( lum );
    
	rgb = YxyToRgb( yxy );

	float4 srgbMappedTexel = LinearToSrgb( float4( rgb, 1 ) );
	RWTexture2D<float4> dstLevelTex = gRWTexture2D_float4[ pushBlock.sdrColDstIdx ];
	dstLevelTex[ globalThreadDispatchID.xy ] = srgbMappedTexel;
}