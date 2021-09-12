#version 460


#extension GL_EXT_control_flow_attributes : require
#extension GL_EXT_samplerless_texture_functions : require

#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_KHR_shader_subgroup_ballot : require
#extension GL_KHR_shader_subgroup_shuffle : require

#extension GL_GOOGLE_include_directive : require

#include "..\r_data_structs.h"

layout( binding = 0 ) uniform texture2D hdrColSrc;
layout( binding = 1, rgba8 ) writeonly uniform coherent image2D sdrColDst;
layout( binding = 2 ) buffer avg_luminance
{
	float avgLum;
};


// TODO: pre compute
 // NOTE: taken from Lottes 2016, "Advanced Techniques and Optimization of HDR Color Pipelines"
float TonemapLottesCurve( float hdrLum )
{
    const float a = 1.6;
    const float d = 0.977;
    const float hdrMax = 8.0;
    const float midIn = 0.18;
    const float midOut = 0.267;

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

vec3 RgbTonemapAcesFilmCurve( vec3 x )
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
vec3 RgbToYxy( vec3 rgb )
{
    const mat3 RGB2XYZ = transpose( mat3(
        0.4497288, 0.3162486, 0.1844926,
        0.2446525, 0.6720283, 0.0833192,
        0.0251848, 0.1411824, 0.9224628 ) );


    vec3 xyz = RGB2XYZ * rgb;
    vec3 Yxy;
    Yxy.x = xyz.y;
    float temp = dot( vec3( 1.0 ), xyz );
    Yxy.yz = xyz.xy / temp;
    return Yxy;
}

vec3 YxyToRgb( vec3 Yxy )
{
    const mat3 XYZ2RGB = transpose( mat3(
        2.9515373, -1.2894116, -0.4738445,
        -1.0851093, 1.9908566, 0.0372026,
        0.0854934, -0.2694964, 1.0912975 ) );

    vec3 xyz;
    xyz.x = Yxy.x * Yxy.y / Yxy.z;
    xyz.y = Yxy.x;
    xyz.z = Yxy.x * ( 1.0 - Yxy.y - Yxy.z ) / Yxy.z;

    return XYZ2RGB * xyz;
}

// NOTE: taken from https://gamedev.stackexchange.com/questions/92015/optimized-linear-to-srgb-glsl
vec4 LinearToSrgb( vec4 rgb )
{
    bvec4 cutoff = lessThanEqual( rgb, vec4( 0.0031308 ) );
    vec4 higher = vec4( 1.055 ) * pow( rgb, vec4( 1.0 / 2.4 ) ) - vec4( 0.055 );
    vec4 lower = rgb * vec4( 12.92 );

    return mix( higher, lower, cutoff );
}

// NOTE: inspired by https://bruop.github.io/tonemapping/
layout( local_size_x = 16, local_size_y = 16, local_size_z = 1 ) in;
void main()
{
	// NOTE: both imgs should have the same res, asserted on in cpp
	uvec2 hdrColSrcSize = textureSize( hdrColSrc, 0 ).xy;
	if( gl_GlobalInvocationID.x > hdrColSrcSize.x || gl_GlobalInvocationID.y > hdrColSrcSize.y ) return;

	vec3 rgb = texelFetch( hdrColSrc, ivec2( gl_GlobalInvocationID.xy ), 0 ).rgb;
    vec3 yxy = RgbToYxy( rgb );
    
    float lum = yxy.x / ( 9.6 * avgLum + 0.0001 );
    
    yxy.x = TonemapLottesCurve( lum );
    //yxy.x = LumTonemapAcesFilmCurve( lum );
    
    rgb = YxyToRgb( yxy );

    imageStore( sdrColDst, ivec2( gl_GlobalInvocationID.xy ), LinearToSrgb( vec4( rgb, 1 ) ) );
    //imageStore( sdrColDst, ivec2( gl_GlobalInvocationID.xy ), vec4( rgb, 1 ) );
}