#include "ht_renderer_types.h"

#include "ht_hlsl_lang.h"
#include "ht_hlsl_math.h"

#include "culling.h"

[[vk::push_constant]]
culling_params pushBlock;


[numthreads(64, 1, 1)]
[shader("compute")]
void DrawCullCsMain( u32x3 globalDispatchID : SV_DispatchThreadID )
{
	u32 instId = globalDispatchID.x;
	if( instId >= pushBlock.instCount )
	{
		return;
	}

	if( bool( pushBlock.isLatePass ) )
	{
		bool instIsOccluded = BufferLoad<u32>( pushBlock.occludedInstCacheIdx, instId );
		if( !instIsOccluded ) return;
	}

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
	// NOTE: we use camIdx here bc we'll have a debug camera
	view_data cam = BufferLoad<view_data>( pushBlock.viewBuffIdx, pushBlock.camIdx );

//#ifdef 1
	// TODO: ifdef dbg ?
	u32 waveDbgOffset = WaveActiveCountBits( true );
    u32 waveDbgBase = 0;
    if( WaveIsFirstLane() )
    {
        waveDbgBase = BufferAtomicAdd( pushBlock.dbgInstCountIdx, waveDbgOffset );
    }
    waveDbgBase = WaveReadLaneFirst( waveDbgBase );

    u32 dbgSlot = waveDbgBase + WavePrefixCountBits( true );

	dbg_aabb_instance aabbInst = { toWorld, float4( 1.0f, 0.0f, 0.0f, 0.0f ), aabbMin, aabbMax };
	BufferStore<dbg_aabb_instance>( pushBlock.dbgInstBuffIdx, aabbInst, dbgSlot );
//#endif

    bool testOcclusion = true;
	bool inFrustum = true;

	if( !bool( pushBlock.isLatePass ) )
	{
		// NOTE: 1st pass runs frustum culling with current instTransform and current cam

		float4x4 mvp = mul( toWorld, cam.mainViewProj );
		frustum_culling_result frustumCullRes = FrustumCulling( aabbMin, aabbMax, mvp );
		// NOTE: we might be visible but if we intersect the znear we skip occlusion
		testOcclusion = testOcclusion && !frustumCullRes.intersectsZNear;
		inFrustum = frustumCullRes.visible;
	}

    bool instNotOccluded = true;
	if( inFrustum && testOcclusion )
	{
		// NOTE: 1st pass uses prev instTransform prevCam and prev HZB
		float4x4 view = bool( pushBlock.isLatePass ) ? cam.mainView : cam.prevView;
		float4x4 mvpOcclusion = mul( toWorld, mul( view, cam.proj ) );
		screenspace_aabb ssAabb = ProjectAabbToScreenSpace( aabbMin, aabbMax, mvpOcclusion );

		Texture2D<float4> hizTex = gTexture2D_float4[ pushBlock.hizTexIdx ];
		SamplerState quadMin = samplers[ pushBlock.hizSamplerIdx ];

		instNotOccluded = ScreenSpaceAabbVsHiZ( ssAabb, hizTex, quadMin );
	}

	if( !bool( pushBlock.isLatePass ) )
	{
		BufferStore<u32>( pushBlock.occludedInstCacheIdx, !instNotOccluded, globalDispatchID.x );
	}

    bool visible = inFrustum && instNotOccluded;

	u32 lanesVisible = WaveActiveCountBits( visible );
	u32 offsetForWave = 0;
	if( lanesVisible > 0 )
	{
		if( WaveIsFirstLane() )
		{
			offsetForWave = BufferAtomicAdd( pushBlock.visibleItemsCountIdx, lanesVisible );
		}
	}
	u32 laneOffset = WavePrefixCountBits( visible );

	u32 slotIdx = WaveReadLaneFirst( offsetForWave ) + laneOffset;
	if( visible )
	{
		visible_instance thisInst = { instId, currentMesh.meshletOffset, currentMesh.meshletCount,
			currentMesh.vtxOffset, currentMesh.triOffset };
		BufferStore<visible_instance>( pushBlock.visibleItemsIdx, thisInst, slotIdx );
	}
}