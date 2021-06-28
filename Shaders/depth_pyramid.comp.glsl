// NOTE: code here is HEAVILY inspired by AMD's FidelityFX-SPD

#version 460

#extension GL_GOOGLE_include_directive: require

#extension GL_KHR_shader_subgroup_arithmetic: require
#extension GL_KHR_shader_subgroup_ballot: require
#extension GL_KHR_shader_subgroup_quad: require
#extension GL_KHR_shader_subgroup_shuffle: require


#include "..\r_data_structs.h"

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout( binding = 0 ) uniform texture2D depthSrc;
layout( binding = 1 ) uniform sampler reduceMin4;
layout( binding = 2, r32f ) uniform coherent image2D depthMips[ 12 ];
layout( binding = 3 ) coherent buffer global_atomic_counte
{
	uint globalAtomicCounter;
};

layout(push_constant) uniform block
{
	downsample_info dsInfo;
};

shared vec4 intermediateLDS[16][16];
shared uint ldsCounter;


uint ABfe(uint src,uint off,uint bits){return bitfieldExtract(src,int(off),int(bits));}
// Proxy for V_BFI_B32 where the 'mask' is set as 'bits', 'mask=(1<<bits)-1', and 'bits' needs to be an immediate.
uint ABfiM(uint src,uint ins,uint bits){return bitfieldInsert(src,ins,0,int(bits));}

uvec2 ARmpRed8x8(uint a)
{
	return uvec2( ABfiM( ABfe(a,2u,3u), a,			 1u ),
				ABfiM( ABfe(a,3u,3u), ABfe(a,1u,2u), 2u ));
}


vec4 MinReduce4( vec4 v0, vec4 v1, vec4 v2, vec4 v3 ) 
{  
	return min( min( v0, v1 ), min( v2, v3 ) ); 
}

vec4 QuadWaveMinReduce( vec4 t )
{
	vec4 t1 = subgroupQuadSwapHorizontal( t );
	vec4 t2 = subgroupQuadSwapVertical( t );
	vec4 t3 = subgroupQuadSwapDiagonal( t );
	return MinReduce4( t, t1, t2, t3 );
}

// NOTE: last thread group uses 5th mip to compute the rest
vec4 LoadReduceQuadFromMip5( ivec2 base )
{
	vec4 t0 = imageLoad( depthMips[5], base + ivec2( 0, 0 ) );
    vec4 t1 = imageLoad( depthMips[5], base + ivec2( 0, 1 ) );
    vec4 t2 = imageLoad( depthMips[5], base + ivec2( 1, 0 ) );
    vec4 t3 = imageLoad( depthMips[5], base + ivec2( 1, 1 ) );
    return MinReduce4( t0, t1, t2, t3 );
}

//  __64x1_TO_8x8_MAPPING__
//	00 01 08 09 10 11 18 19 
//	02 03 0a 0b 12 13 1a 1b
//	04 05 0c 0d 14 15 1c 1d
//	06 07 0e 0f 16 17 1e 1f 
//	20 21 28 29 30 31 38 39 
//	22 23 2a 2b 32 33 3a 3b
//	24 25 2c 2d 34 35 3c 3d
//	26 27 2e 2f 36 37 3e 3f 

// TODO: faster faster
uint DecodeMorton1By1( uint x )
{
	// NOTE: "delete" all odd-indexed bits
	x &= 0x55555555;                  // x = -f-e -d-c -b-a -9-8 -7-6 -5-4 -3-2 -1-0
	x = (x ^ (x >>  1)) & 0x33333333; // x = --fe --dc --ba --98 --76 --54 --32 --10
	x = (x ^ (x >>  2)) & 0x0f0f0f0f; // x = ---- fedc ---- ba98 ---- 7654 ---- 3210
	x = (x ^ (x >>  4)) & 0x00ff00ff; // x = ---- ---- fedc ba98 ---- ---- 7654 3210
	x = (x ^ (x >>  8)) & 0x0000ffff; // x = ---- ---- ---- ---- fedc ba98 7654 3210
	return x;
}

void GenerateNext4Mips( uint x, uint y, uint mipIdx, ivec2 workGroupID )
{
	// MIP "2":
	if( dsInfo.mips <= mipIdx ) return;
	barrier();
	
	vec4 t = QuadWaveMinReduce( intermediateLDS[ x ][ y ] );
	if( ( gl_LocalInvocationIndex % 4 ) == 0 ){
		imageStore( depthMips[mipIdx], workGroupID * 8 + ivec2(x/2, y/2), t );
		uint ldsX = x + (y/2) % 2;
		intermediateLDS[ ldsX ][ y ] = t;
	}

	// MIP "3":
	mipIdx = mipIdx + 1;
	if( dsInfo.mips <= mipIdx ) return;
	barrier();

	if( gl_LocalInvocationIndex < 64 ){
		uint loadLdsX = x * 2 + y % 2;
		uint loadLdsY = y * 2;
		vec4 t = QuadWaveMinReduce( intermediateLDS[ loadLdsX ][ loadLdsY ] );

		if( ( gl_LocalInvocationIndex % 4 ) == 0 ){
			imageStore( depthMips[mipIdx], workGroupID * 4 + ivec2(x/2, y/2), t );
			uint storeLdsX = x * 2 + y/2;
			uint storeLdsY = y * 2;
			intermediateLDS[ storeLdsX ][ storeLdsY ] = t;
		}
	}

	// MIP "4":
	mipIdx = mipIdx + 1;
	if( dsInfo.mips <= mipIdx ) return;
	barrier();

	if( gl_LocalInvocationIndex < 16 ){
		uint loadLdsX = x * 4 + y;
		uint loadLdsY = y * 4;
		vec4 t = QuadWaveMinReduce( intermediateLDS[ loadLdsX ][ loadLdsY ] );

		if( ( gl_LocalInvocationIndex % 4 ) == 0 ){
			imageStore( depthMips[mipIdx], workGroupID * 2 + ivec2(x/2, y/2), t );
			uint storeLdsX = x / 2 + y;
			intermediateLDS[ storeLdsX ][ 0 ] = t;
		}
	}

	// MIP "5":
	mipIdx = mipIdx + 1;
	if( dsInfo.mips <= mipIdx ) return;
	barrier();

	if( gl_LocalInvocationIndex < 4 ){
		vec4 t = QuadWaveMinReduce( intermediateLDS[ gl_LocalInvocationIndex ][ 0 ] );

		if( ( gl_LocalInvocationIndex % 4 ) == 0 ) imageStore( depthMips[mipIdx], workGroupID, t );
	}
}

void main()
{
	// TODO: reset globalCounter in shader ?
	//if( ( gl_GlobalInvocationID.x == 0 ) && ( gl_GlobalInvocationID.y == 0 ) ) globalAtomicCounter = 0;
	// NOTE: map 64x1 to 8x8 for all texture chunks
	uint localInvMod = gl_LocalInvocationIndex % 64;
	
	uint x = DecodeMorton1By1( localInvMod ) + 8 * ( ( gl_LocalInvocationIndex >> 6 ) % 2 );
	uint y = DecodeMorton1By1( localInvMod >> 1 ) + 8 * ( gl_LocalInvocationIndex >> 7 );

	// MIP 0:
	uvec2 srcPos = gl_WorkGroupID.xy * 64 + uvec2( x * 2, y * 2 );
	uvec2 dstPos = gl_WorkGroupID.xy * 32 + uvec2( x, y );
	vec4 t0 = texture( sampler2D( depthSrc, reduceMin4 ), srcPos * dsInfo.invRes + dsInfo.invRes );
	imageStore( depthMips[0], ivec2( dstPos ), t0 );

	srcPos = gl_WorkGroupID.xy * 64 + uvec2( x * 2 + 32, y * 2 );
	dstPos = gl_WorkGroupID.xy * 32 + uvec2( x + 16, y );
	vec4 t1 = texture( sampler2D( depthSrc, reduceMin4 ), srcPos * dsInfo.invRes + dsInfo.invRes );
	imageStore( depthMips[0], ivec2( dstPos ), t1 );

	srcPos = gl_WorkGroupID.xy * 64 + uvec2( x * 2, y * 2 + 32 );
	dstPos = gl_WorkGroupID.xy * 32 + uvec2( x, y + 16 );
	vec4 t2 = texture( sampler2D( depthSrc, reduceMin4 ), srcPos * dsInfo.invRes + dsInfo.invRes );
	imageStore( depthMips[0], ivec2( dstPos ), t2 );

	srcPos = gl_WorkGroupID.xy * 64 + uvec2( x * 2 + 32, y * 2 + 32 );
	dstPos = gl_WorkGroupID.xy * 32 + uvec2( x + 16, y + 16 );
	vec4 t3 = texture( sampler2D( depthSrc, reduceMin4 ), srcPos * dsInfo.invRes + dsInfo.invRes );
	imageStore( depthMips[0], ivec2( dstPos ), t3 );


	if( dsInfo.mips <= 1 ) return;

	// MIP 1:
	t0 = QuadWaveMinReduce( t0 );
	t1 = QuadWaveMinReduce( t1 );
	t2 = QuadWaveMinReduce( t2 );
	t3 = QuadWaveMinReduce( t3 );

	if( (gl_LocalInvocationIndex % 4) == 0 ){
		uvec2 subTile00 = uvec2( x/2 ,y/2 );
		uvec2 subTile10 = uvec2( x/2 + 8, y/2 );
		uvec2 subTile01 = uvec2( x/2, y/2 + 8 );
		uvec2 subTile11 = uvec2( x/2 + 8, y/2 + 8 );

		imageStore( depthMips[1], ivec2( gl_WorkGroupID.xy * 16 + subTile00 ), t0 );
		imageStore( depthMips[1], ivec2( gl_WorkGroupID.xy * 16 + subTile10 ), t1 );
		imageStore( depthMips[1], ivec2( gl_WorkGroupID.xy * 16 + subTile01 ), t2 );
		imageStore( depthMips[1], ivec2( gl_WorkGroupID.xy * 16 + subTile11 ), t3 );

		intermediateLDS[ subTile00.x ][ subTile00.y ] = t0;
		intermediateLDS[ subTile10.x ][ subTile10.y ] = t1;
		intermediateLDS[ subTile01.x ][ subTile01.y ] = t2;
		intermediateLDS[ subTile11.x ][ subTile11.y ] = t3;
	}

	GenerateNext4Mips( x, y, 2, ivec2( gl_WorkGroupID.xy ) );

	if( dsInfo.mips <= 6 ) return;

	// NOTE: only for last active workGroup will downsample remaining 64x64 texels
    if( gl_LocalInvocationIndex == 0 ) ldsCounter = atomicAdd( globalAtomicCounter, 1 );
    barrier();
    if( ldsCounter != ( dsInfo.workGroupCount - 1 ) ) return;

	globalAtomicCounter = 0;

	// MIP 6:
	srcPos = uvec2( x * 4, y * 4 );
	dstPos = uvec2( x * 2, y * 2 );
	t0 = LoadReduceQuadFromMip5( ivec2( srcPos ) );
	imageStore( depthMips[ 6 ], ivec2( dstPos ), t0 );

	srcPos = uvec2( x * 4 + 2, y * 4 );
	dstPos = uvec2( x * 2 + 1, y * 2 );
	t1 = LoadReduceQuadFromMip5( ivec2( srcPos ) );
	imageStore( depthMips[ 6 ], ivec2( dstPos ), t1 );

	srcPos = uvec2( x * 4, y * 4 + 2 );
	dstPos = uvec2( x * 2, y * 2 + 1 );
	t2 = LoadReduceQuadFromMip5( ivec2( srcPos ) );
	imageStore( depthMips[ 6 ], ivec2( dstPos ), t2 );

	srcPos = uvec2( x * 4 + 2, y * 4 + 2 );
	dstPos = uvec2( x * 2 + 1, y * 2 + 1 );
	t3 = LoadReduceQuadFromMip5( ivec2( srcPos ) );
	imageStore( depthMips[ 6 ], ivec2( dstPos ), t3 );

	// MIP 7:
	if( dsInfo.mips <= 7 ) return;

	vec4 t = MinReduce4( t0, t1, t2, t3 );
	imageStore( depthMips[ 7 ], ivec2( x, y ), t );
	intermediateLDS[ x ][ y ] = t;

	GenerateNext4Mips( x, y, 8, ivec2(0,0) );
}