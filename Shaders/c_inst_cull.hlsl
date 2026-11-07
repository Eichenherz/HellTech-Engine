#include "ht_renderer_types.h"

#include "ht_hlsl_lang.h"
#include "ht_hlsl_math.h"
#include "ht_culling.h"


[[vk::push_constant]]
culling_pass_params pushBlock;


[shader( "compute" )]
[numthreads( 64, 1, 1 )]
void InstCullCsMain( u32x3 globalDispatchID : SV_DispatchThreadID )
{
	u32 tID = globalDispatchID.x;

    const bool isLatePass       = bool( pushBlock.isLatePass );
    const bool enableCulling    = bool( pushBlock.enableInstCull );
    const bool enableLod        = bool( pushBlock.enableMeshLod );

	u32 instCount = !isLatePass ? pushBlock.instCount : device_ptr<u32>( pushBlock.occludedInstCounterAddr )[ 0 ];
	if( tID >= instCount ) return;

    u32 instID = !isLatePass ? tID : device_ptr<u32>( pushBlock.occludedInstAddr )[ tID ];

	gpu_instance        currentInst = device_ptr<gpu_instance>( g_GlobalData.instAddr )[ instID ];
	RHNDL32( gpu_mesh ) hMesh       = device_ptr<RHNDL32( gpu_mesh )>( g_GlobalData.meshRefAddr )[ currentInst.meshIdx ];
	gpu_mesh            meshDesc    = device_ptr<gpu_mesh>( g_GlobalData.meshDescAddr )[ hMesh.idx ];
     // NOTE: we use camIdx here bc we'll have a debug camera
    view_data           cam         = device_ptr<view_data>( g_GlobalData.viewsAddr )[ g_GlobalData.mainCamIdx ];

    bool visible = true;
    if( enableCulling )
    {
        Texture2D<float4>   hizTex  = gTexture2D_float4[ pushBlock.hizTexIdx ];
        SamplerState        quadMin = samplers[ pushBlock.hizSamplerIdx ];
        float4x4            toWorld = f4x3_To_f4x4_Affine( currentInst.toWorld );

        visibility_res instVisRes = TestVisibility( meshDesc.aabbMin, meshDesc.aabbMax, toWorld, cam,
            hizTex, quadMin, isLatePass );

        if( !isLatePass )
        {
            const bool instIsOccluded = !instVisRes.notOccluded;

            u32 occSlotIdx = HTWaveReserveGlobalSlot( instIsOccluded, device_ptr<u32>( pushBlock.occludedInstCounterAddr ) );
            if( instIsOccluded )
            {
                device_ptr<u32>( pushBlock.occludedInstAddr ).Store( occSlotIdx, instID );
            }
        }

        visible = instVisRes.inFrustum && instVisRes.notOccluded;
    }

    u32 slotIdx = HTWaveReserveGlobalSlot( visible, device_ptr<u32>( pushBlock.visibleInstCounterAddr ) );

	if( visible )
	{
        float   distSq      = SqDistCamToAabbInObjSpace( currentInst.toWorld, meshDesc.aabbMin, meshDesc.aabbMax,
                                                        currentInst.scale, cam.worldPos );
        float   threshold   = distSq * cam.lodTarget * cam.lodTarget;
        float3  errSqLod13  = meshDesc.lod4Err.yzw * meshDesc.lod4Err.yzw;

        u32x2   availableDesiredLodsIdx = ComputeDesiredAndAvailableLODs( errSqLod13, threshold,
            GPUMeshGetLodMask( meshDesc ) );

        //if( 0 != ( availableDesiredLodsIdx.x - availableDesiredLodsIdx.y ) )
        //{
        //    device_ptr<u32>( g_GlobalData.lodRequestsAddr ).Store( occSlotIdx, instID );
        //}

        u32 lodIdx = enableLod ? availableDesiredLodsIdx.x : 0;
        gpu_mesh_lod selectedLod = meshDesc.lods[ lodIdx ];
		visible_instance visInst = {
            selectedLod.payloadStartInBytes,
		    instID,
            lodIdx,
            selectedLod.firstMlt,
            selectedLod.mltCount
		};
		device_ptr<visible_instance>( pushBlock.visibleInstAddr ).Store( slotIdx, visInst );
	}
}