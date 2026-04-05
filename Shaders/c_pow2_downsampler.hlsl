#include "r_data_structs2.h"

[[vk::push_constant]]
struct {
	float2 imgSize;
	uint samplerIdx;
	uint inImgIdx;
	uint inImgLod;
	uint outImgIdx;
} pushBlock;

[ shader( "compute" ) ]
[numthreads( 32, 32, 1 )]
void Pow2DownsamplerCsMain( uint3 globalThreadDispatchID : SV_DispatchThreadID )
{
	uint2 pos = globalThreadDispatchID.xy;
	float2 uv = ( float2( pos ) + 0.5f ) / pushBlock.imgSize;
	
	Texture2D<float> srcLevelTex = gTexture2D_float[ pushBlock.inImgIdx ];
	SamplerState quadMinSampler = samplers[ pushBlock.samplerIdx ];
	float depth = srcLevelTex.SampleLevel( quadMinSampler, uv, pushBlock.inImgLod );
	
	RWTexture2D<float> dstLevelTex = gRWTexture2D_float[ pushBlock.outImgIdx ];
	dstLevelTex[ pos ] = depth;
}
