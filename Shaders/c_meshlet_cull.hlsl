#include "ht_renderer_types.h"

#include "ht_hlsl_lang.h"
#include "ht_hlsl_math.h"
#include "ht_unpacking.h"
#include "ht_culling.h"


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

    const bool isLatePass       = bool( pushBlock.isLatePass );
    const bool enableCulling    = bool( pushBlock.enableCulling );

    meshlet_cull_wok_item   mltWorkItem = BufferLoad<meshlet_cull_wok_item>( pushBlock.expandedMltsIdx, mltId );
    gpu_instance            currInst    = BufferLoad<gpu_instance>( pushBlock.instDescIdx, mltWorkItem.instId );
    gpu_meshlet             currMlt     = device_ptr<gpu_meshlet>( gGlobData.mltAddr )[ mltWorkItem.globMltId ];

    bool visible = true;
    if( enableCulling )
    {
        float4x4 toWorld = f4x3_To_f4x4_Affine( currInst.toWorld );

        // NOTE: we use camIdx here bc we'll have a debug camera
        view_data           cam     = BufferLoad<view_data>( pushBlock.viewBuffIdx, pushBlock.camIdx );
        Texture2D<float4>   hizTex  = gTexture2D_float4[ pushBlock.hizTexIdx ];
        SamplerState        quadMin = samplers[ pushBlock.hizSamplerIdx ];

        visibility_res mltVisRes = TestVisibility( currMlt.aabbMin, currMlt.aabbMax, toWorld, cam, hizTex, quadMin, isLatePass );

        if( !isLatePass )
        {
            bool mltIsOccluded  = !mltVisRes.notOccluded;
            u32 occSlotIdx      = HTWaveReserveGlobalSlot( mltIsOccluded, pushBlock.occludedMltCountIdx );
            if( mltIsOccluded )
            {
                BufferStore<meshlet_cull_wok_item>( pushBlock.occludedMltBuffIdx, mltWorkItem, occSlotIdx );
            }
        }

        visible = mltVisRes.inFrustum && mltVisRes.notOccluded;
    }

    u32 slotIdx = HTWaveReserveGlobalSlot( visible, pushBlock.drawCountIdx );

    if( visible )
    {
        draw_meshlet_command drawMltCmd = ( draw_meshlet_command ) 0;

        drawMltCmd.indexCount     = ( u32 ) ( ( currMlt.packed8_12_12_VtxCount_Lod_01_IdxCount >> 8 ) & ( ( 1u << 12 ) - 1 ) );
        drawMltCmd.instanceCount  = 1;
        drawMltCmd.firstIndex     = currMlt.idxOffset + mltWorkItem.idxOffset;
        // NOTE: bc we use bitstreams for pos encoding we can't use an "item" based offset,
        // so we need to manually compute it in the shader
        drawMltCmd.vertexOffset   = 0;
        drawMltCmd.firstInstance  = 0;

        draw_meshlet_cmd_data drawMltData = ( draw_meshlet_cmd_data ) 0;

        drawMltData.globalInstId        = mltWorkItem.instId;
        drawMltData.globalMltId         = mltWorkItem.globMltId;
        drawMltData.vtxAttrOffset       = mltWorkItem.vtxAttrsOffset;
        drawMltData.vtxPosOffsetInBits  = mltWorkItem.vtxPosOffsetInBytes * 8u;

    	BufferStore<draw_meshlet_command>( pushBlock.drawCmsIdx, drawMltCmd, slotIdx );
    	BufferStore<draw_meshlet_cmd_data>( pushBlock.drawDataIdx, drawMltData, slotIdx );
    }
}