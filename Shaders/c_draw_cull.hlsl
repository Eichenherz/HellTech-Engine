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
	u32 instId = globalDispatchID.x;
	if( instId >= pushBlock.instCount )
	{
		return;
	}

    const bool isLatePass = bool( pushBlock.isLatePass );

	if( isLatePass )
	{
	    bool instIsOccluded = GetBitAtIdx( pushBlock.occludedInstCacheIdx, instId );
		if( !instIsOccluded ) return;
	}

    // NOTE: we use camIdx here bc we'll have a debug camera
    view_data cam = BufferLoad<view_data>( pushBlock.viewBuffIdx, pushBlock.camIdx );
    Texture2D<float4> hizTex = gTexture2D_float4[ pushBlock.hizTexIdx ];
    SamplerState quadMin = samplers[ pushBlock.hizSamplerIdx ];

	gpu_instance currentInst = BufferLoad<gpu_instance>( pushBlock.instDescIdx, instId );
	gpu_mesh currentMesh = BufferLoad<gpu_mesh>( pushBlock.meshDescIdx, currentInst.meshIdx );

	float4x4 toWorld = float4x4(
        float4( currentInst.toWorld[ 0 ], 0.0f ),
        float4( currentInst.toWorld[ 1 ], 0.0f ),
        float4( currentInst.toWorld[ 2 ], 0.0f ),
        float4( currentInst.toWorld[ 3 ], 1.0f )
	);

	float3 aabbMin = currentMesh.minAabb;
	float3 aabbMax = currentMesh.maxAabb;

//#ifdef 1
// TODO: ifdef dbg ? do it on the CPU side ?
    u32 dbgSlot = HTWaveReserveGlobalSlot( true, pushBlock.dbgInstCountIdx );
	dbg_aabb_instance aabbInst = { toWorld, float4( 1.0f, 0.0f, 0.0f, 0.0f ), aabbMin, aabbMax };
	BufferStore<dbg_aabb_instance>( pushBlock.dbgInstBuffIdx, aabbInst, dbgSlot );
//#endif

    visibility_res instVisRes = TestVisibility( aabbMin, aabbMax, toWorld, cam, hizTex, quadMin, isLatePass );

	if( !isLatePass && !instVisRes.notOccluded )
	{
	    SetBitAtIdx( pushBlock.occludedInstCacheIdx, instId );
	}

    bool visible = instVisRes.inFrustum && instVisRes.notOccluded;

    u32 slotIdx = HTWaveReserveGlobalSlot( visible, pushBlock.visibleItemsCountIdx );

	if( visible )
	{
		visible_instance thisInst = {
		    instId,
		    currentMesh.meshletOffset,
		    currentMesh.meshletCount,
			currentMesh.vtxOffset,
			currentMesh.triOffset
		};
		BufferStore<visible_instance>( pushBlock.visibleItemsIdx, thisInst, slotIdx );
	}
}