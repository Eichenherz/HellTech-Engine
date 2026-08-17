#include "ht_renderer_types.h"

#include "ht_hlsl_lang.h"
#include "ht_hlsl_math.h"
#include "ht_culling.h"


[[vk::push_constant]]
culling_params pushBlock;


[shader( "compute" )]
[numthreads( 64, 1, 1 )]
void DrawCullCsMain( u32x3 globalDispatchID : SV_DispatchThreadID )
{
	u32 tID = globalDispatchID.x;

    const bool isLatePass       = bool( pushBlock.isLatePass );
    const bool enableCulling    = bool( pushBlock.enableCulling );

	u32 instCount = !isLatePass ? pushBlock.instCount : BufferLoad<u32>( pushBlock.occludedInstCounterIdx, 0 );
	if( tID >= instCount ) return;

    u32 instID = !isLatePass ? tID : BufferLoad<u32>( pushBlock.occludedInstBuffIdx, tID );

	gpu_instance    currentInst     = BufferLoad<gpu_instance>( pushBlock.instDescIdx, instID );
	gpu_mesh        currentMesh     = BufferLoad<gpu_mesh>( pushBlock.meshDescIdx, currentInst.meshIdx );
     // NOTE: we use camIdx here bc we'll have a debug camera
    view_data       cam             = BufferLoad<view_data>( pushBlock.viewBuffIdx, pushBlock.camIdx );

    bool visible = true;
    if( enableCulling )
    {
        Texture2D<float4>   hizTex  = gTexture2D_float4[ pushBlock.hizTexIdx ];
        SamplerState        quadMin = samplers[ pushBlock.hizSamplerIdx ];
        float4x4            toWorld = f4x3_To_f4x4_Affine( currentInst.toWorld );

        visibility_res instVisRes = TestVisibility( currentMesh.aabbMin, currentMesh.aabbMax, toWorld, cam,
            hizTex, quadMin, isLatePass );

        if( !isLatePass )
        {
            bool instIsOccluded = !instVisRes.notOccluded;
            u32 occSlotIdx      = HTWaveReserveGlobalSlot( instIsOccluded, pushBlock.occludedInstCounterIdx );
            if( instIsOccluded )
            {
                BufferStore<u32>( pushBlock.occludedInstBuffIdx, instID, occSlotIdx );
            }
        }

        visible = instVisRes.inFrustum && instVisRes.notOccluded;
    }

    u32 slotIdx = HTWaveReserveGlobalSlot( visible, pushBlock.visibleItemsCountIdx );

	if( visible )
	{
        float   distSq    = SqDistCamToAabbInObjSpace( currentInst.toWorld, currentMesh.aabbMin, currentMesh.aabbMax,
                                                        currentInst.scale, cam.worldPos );
        float   threshold = distSq * cam.lodTarget * cam.lodTarget;
        float3  errSqLod3 = currentMesh.lod4Err.yzw * currentMesh.lod4Err.yzw;
        u32x4   lods4     = UnpackLODMeshletCount( currentMesh );

        u32x2   lodOffsetCount = SelectOffsetAndCountFromLod4( errSqLod3, threshold, lods4 );

        u32     lodMltOffst = bool( pushBlock.enableLod ) ? lodOffsetCount.x : 0;
        u32     lodMltCount = bool( pushBlock.enableLod ) ? lodOffsetCount.y : lods4.x;

		visible_instance thisInst;
		thisInst.instId					= instID;
		thisInst.meshletOffset			= currentMesh.meshletOffset + lodMltOffst;
		thisInst.meshletCount			= lodMltCount;
		thisInst.vtxPosOffsetInBytes    = currentMesh.vtxPosOffsetInBytes;
		thisInst.vtxAttrsOffset			= currentMesh.vtxAttrsOffset;
		thisInst.idxOffset				= currentMesh.idxOffset;
		BufferStore<visible_instance>( pushBlock.visibleItemsIdx, thisInst, slotIdx );
	}
}