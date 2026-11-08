#include "ht_renderer_types.h"

#include "ht_hlsl_lang.h"
#include "ht_hlsl_math.h"
#include "ht_culling.h"


[[vk::push_constant]]
culling_pass_params pushBlock;


[shader( "compute" )]
[numthreads( 128, 1, 1 )]
void MeshletCullCsMain( u32x3 globalDispatchID : SV_DispatchThreadID )
{
    u32 mltId = globalDispatchID.x;

    u32 mltCount = device_ptr<u32>( pushBlock.mltToTestCounterAddr )[ 0 ];
    if( mltId >= mltCount ) return;

    const bool isLatePass       = bool( pushBlock.isLatePass );
    const bool enableCulling    = bool( pushBlock.enableMltCull );
    const bool enableLod        = bool( pushBlock.enableMltLod );

    meshlet_cull_wok_item   mltWorkItem = device_ptr<meshlet_cull_wok_item>( pushBlock.mltToTestAddr )[ mltId ];
    gpu_instance            currInst    = device_ptr<gpu_instance>( g_GlobalData.instAddr )[ mltWorkItem.instId ];
    gpu_meshlet             currMlt     = device_ptr<gpu_meshlet>( g_GlobalData.mltAddr )[ mltWorkItem.globMltId ];
     // NOTE: we use camIdx here bc we'll have a debug camera
    view_data               cam         = device_ptr<view_data>( g_GlobalData.viewsAddr )[ g_GlobalData.mainCamIdx ];

    bool visible = true;
    if( enableCulling )
    {
        float4x4 toWorld = f4x3_To_f4x4_Affine( currInst.toWorld );

        Texture2D<float4>   hizTex  = gTexture2D_float4[ pushBlock.hizTexIdx ];
        SamplerState        quadMin = samplers[ pushBlock.hizSamplerIdx ];

        visibility_res mltVisRes = TestVisibility( currMlt.aabbMin, currMlt.aabbMax, toWorld, cam, hizTex, quadMin, isLatePass );

        if( !isLatePass )
        {
            bool mltIsOccluded = !mltVisRes.notOccluded;
            u32 occSlotIdx = HTWaveReserveGlobalSlot( mltIsOccluded, device_ptr<u32>( pushBlock.occludedMltCounterAddr ) );
            if( mltIsOccluded )
            {
                device_ptr<meshlet_cull_wok_item>( pushBlock.occludedMltAddr ).Store( occSlotIdx, mltWorkItem );
            }
        }

        visible = mltVisRes.inFrustum && mltVisRes.notOccluded;
    }

    u32 slotIdx = HTWaveReserveGlobalSlot( visible, device_ptr<u32>( pushBlock.drawCounterAddr ) );

    if( visible )
    {
        float distSq = SqDistCamToAabbInObjSpace( currInst.toWorld, currMlt.aabbMin, currMlt.aabbMax, currInst.scale,
                                                  cam.worldPos );

        float threshold = distSq * cam.lodTarget * cam.lodTarget;
        float lodErrSqq = currMlt.lodError * currMlt.lodError;

        u32x2   lod01IdxCounts  = UnpackMeshletVtxAndLodIdxCount( currMlt ).yz;
        bool    lodSwitch       = enableLod && ( lodErrSqq < threshold );
        u32     lodIdxOffset    = lodSwitch ? lod01IdxCounts.x : 0;
        u32     lodIdxCount     = lodSwitch ? lod01IdxCounts.y : lod01IdxCounts.x;

        u32x2   mltVtxStreamSizes   = GPUMeshletComputeVtxStreamSizes( currMlt );
        u64     localIdxBuffOffset  = currMlt.offsetInPayload + mltVtxStreamSizes.x + mltVtxStreamSizes.y;
        draw_meshlet_command drawMltCmd = {
            lodIdxCount,
            1,
            mltWorkItem.meshPayloadStartInBytes + localIdxBuffOffset + lodIdxOffset,
            // NOTE: bc we use bitstreams for pos encoding we can't use an "item" based offset,
            // so we need to manually compute it in the shader
            0,
            0
        };

        draw_meshlet_cmd_data drawMltData = {
            mltWorkItem.meshPayloadStartInBytes,
            mltWorkItem.instId,
            mltWorkItem.lodIdx,
            mltWorkItem.globMltId,
            0
        };

    	device_ptr<draw_meshlet_command>( pushBlock.drawCmdsAddr ).Store( slotIdx, drawMltCmd );
    	device_ptr<draw_meshlet_cmd_data>( pushBlock.drawDataAddr ).Store( slotIdx, drawMltData );
    }
}