#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

[[vk::push_constant]]
multi_pass_downsampler_params pushBlock;


[shader( "compute" )]
[numthreads( 32, 32, 1 )]
void Pow2DownsamplerCsMain( u32x3 globalThreadDispatchID : SV_DispatchThreadID )
{
	u32x2 pos = globalThreadDispatchID.xy;
	if( any( pos >= pushBlock.dstSize ) ) return;

    Texture2D<float> srcLevelTex = gTexture2D_float[ pushBlock.inImgIdx ];

    float minDepth = 1.0;   // max for reverse-Z
    if( pushBlock.isMip0FromNonPot ) // NOTE: uniform per dispatch
    {
        // NOTE; bc it's non pot we have to reduce more
        u32x2 srcFloor = ( pos * pushBlock.srcSize ) / pushBlock.dstSize;
        u32x2 srcCeil = ( ( pos + 1 ) * pushBlock.srcSize + pushBlock.dstSize - 1 ) / pushBlock.dstSize;

        SamplerState pointSampler = samplers[ pushBlock.pointSamplerIdx ];

        for( u32 sy = srcFloor.y; sy < srcCeil.y; sy += 2 )
        {
           for( u32 sx = srcFloor.x; sx < srcCeil.x; sx += 2 )
           {
                float2 uv = ( float2( sx, sy ) + float2( 1.0, 1.0 ) ) / float2( pushBlock.srcSize );
                float4 four = srcLevelTex.Gather( pointSampler, uv );
                minDepth = min( minDepth, min( min( four.x, four.y ), min( four.z, four.w ) ) );
           }
        }
    }
    else
    {
        float2 uv = ( float2( pos ) + 0.5f ) / float2( pushBlock.dstSize );
    	SamplerState quadMinSampler = samplers[ pushBlock.reductionSamplerIdx ];
    	minDepth = srcLevelTex.SampleLevel( quadMinSampler, uv, pushBlock.inImgLod );
    }

    RWTexture2D<float> dstLevelTex = gRWTexture2D_float[ pushBlock.outImgIdx ];
	dstLevelTex[ pos ] = minDepth;
}
