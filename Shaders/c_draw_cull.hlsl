#include "ht_renderer_types.h"

#include "ht_hlsl_lang.h"
#include "ht_hlsl_math.h"

#include "culling.h"

[[vk::push_constant]]
culling_params pushBlock;


[shader( "compute" )]
[numthreads( 64, 1, 1 )]
void DrawCullCsMain( u32x3 globalDispatchID : SV_DispatchThreadID )
{
	u32 tID = globalDispatchID.x;

    const bool isLatePass = bool( pushBlock.isLatePass );
    const bool toggleCulling = bool( pushBlock.toggleCulling );

	u32 instCount = !isLatePass ? pushBlock.instCount : BufferLoad<u32>( pushBlock.occludedInstCounterIdx, 0 );
	if( tID >= instCount ) return;

    u32 instID = !isLatePass ? tID : BufferLoad<u32>( pushBlock.occludedInstBuffIdx, tID );

	gpu_instance currentInst = BufferLoad<gpu_instance>( pushBlock.instDescIdx, instID );
	gpu_mesh currentMesh = BufferLoad<gpu_mesh>( pushBlock.meshDescIdx, currentInst.meshIdx );

    bool visible = true;
    if( toggleCulling )
    {
        float4x4 toWorld = float4x4(
            float4( currentInst.toWorld[ 0 ], 0.0f ),
            float4( currentInst.toWorld[ 1 ], 0.0f ),
            float4( currentInst.toWorld[ 2 ], 0.0f ),
            float4( currentInst.toWorld[ 3 ], 1.0f )
        );

        // NOTE: we use camIdx here bc we'll have a debug camera
        view_data cam = BufferLoad<view_data>( pushBlock.viewBuffIdx, pushBlock.camIdx );
        Texture2D<float4> hizTex = gTexture2D_float4[ pushBlock.hizTexIdx ];
        SamplerState quadMin = samplers[ pushBlock.hizSamplerIdx ];

        visibility_res instVisRes = TestVisibility( currentMesh.aabbMin, currentMesh.aabbMax, toWorld, cam,
            hizTex, quadMin, isLatePass );

        if( !isLatePass )
        {
            bool instIsOccluded = !instVisRes.notOccluded;
            u32 occSlotIdx = HTWaveReserveGlobalSlot( instIsOccluded, pushBlock.occludedInstCounterIdx );
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
		visible_instance thisInst;
		thisInst.instId						= instID;
		thisInst.meshletOffset				= currentMesh.meshletOffset;
		thisInst.meshletCount				= currentMesh.meshletCount;
		thisInst.vtxPosOffsetInBytes		= currentMesh.vtxPosOffsetInBytes;
		thisInst.vtxAttrsOffset				= currentMesh.vtxAttrsOffset;
		thisInst.triOffset					= currentMesh.triOffset;
		BufferStore<visible_instance>( pushBlock.visibleItemsIdx, thisInst, slotIdx );
	}
}