#include "ht_renderer_types.h"

#include "ht_hlsl_lang.h"
#include "ht_hlsl_math.h"

#include "culling.h"

[[vk::push_constant]]
meshlet_cull_params pushBlock;


[shader( "compute" )]
[numthreads( 128, 1, 1 )]
void MeshletCullCsMain( u32x3 globalDispatchID : SV_DispatchThreadID )
{
    u32 mltCount = BufferLoad<u32>( pushBlock.mltCountIdx, 0 );

    u32 mltId = globalDispatchID.x;
    if( mltId >= mltCount )
    {
    	return;
    }

    const bool isLatePass = bool( pushBlock.isLatePass );

    // NOTE: we use camIdx here bc we'll have a debug camera
    view_data cam = BufferLoad<view_data>( pushBlock.viewBuffIdx, pushBlock.camIdx );
    Texture2D<float4> hizTex = gTexture2D_float4[ pushBlock.hizTexIdx ];
    SamplerState quadMin = samplers[ pushBlock.hizSamplerIdx ];

    visible_meshlet mltToTest = BufferLoad<visible_meshlet>( pushBlock.expandedMltsIdx, mltId );
    gpu_instance currentInst = BufferLoad<gpu_instance>( pushBlock.instDescIdx, mltToTest.instId );

    float4x4 toWorld = float4x4(
        float4( currentInst.toWorld[ 0 ], 0.0f ),
        float4( currentInst.toWorld[ 1 ], 0.0f ),
        float4( currentInst.toWorld[ 2 ], 0.0f ),
        float4( currentInst.toWorld[ 3 ], 1.0f )
    );

    visibility_res mltVisRes = TestVisibility( mltToTest.minAabb, mltToTest.maxAabb, toWorld, cam, hizTex, quadMin, isLatePass );

    if( !isLatePass )
    {
        bool mltIsOccluded = !mltVisRes.notOccluded;
        u32 occSlotIdx = HTWaveReserveGlobalSlot( mltIsOccluded, pushBlock.occludedMltCountIdx );
        if( mltIsOccluded )
        {
            BufferStore<visible_meshlet>( pushBlock.occludedMltBuffIdx, mltToTest, occSlotIdx );
        }
    }

    bool visible = mltVisRes.inFrustum && mltVisRes.notOccluded;

    u32 slotIdx = HTWaveReserveGlobalSlot( visible, pushBlock.drawCountIdx );

    if( visible )
    {
        draw_meshlet_command drawMltCmd = {
            mltToTest.instId,
            mltToTest.globMltId,
            mltToTest.triCount * 3, // NOTE: * 3 bc it's an idx count
            1,
            mltToTest.globTriOffset,
            mltToTest.globVtxOffset,
            0
        };
    	BufferStore<draw_meshlet_command>( pushBlock.drawCmsIdx, drawMltCmd, slotIdx );
    }
}