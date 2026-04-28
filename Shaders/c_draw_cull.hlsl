#include "ht_renderer_types.h"

#include "ht_hlsl_lang.h"
#include "ht_hlsl_math.h"

#include "culling.h"

[[vk::push_constant]]
culling_params pushBlock;


[numthreads(32, 1, 1)]
[shader("compute")]
void DrawCullCsMain( u32x3 globalDispatchID : SV_DispatchThreadID, u32 groupFlatIdx : SV_GroupIndex )
{
	u32 instId = globalDispatchID.x;
	if( instId >= pushBlock.instCount )
	{
		return;
	}

	bool instanceIsOccluded = false;
	if( bool( pushBlock.isLatePass ) )
	{
		instanceIsOccluded = BufferLoad<uint>( pushBlock.visInstCacheIdx, instId );
		if( !instanceIsOccluded ) return;
	}

	gpu_instance currentInst = BufferLoad<gpu_instance>( pushBlock.instDescIdx, instId );
	float4x4 toWorld = TrsToFloat4x4( currentInst.toWorld.t, currentInst.toWorld.r, currentInst.toWorld.s );

	gpu_mesh currentMesh = BufferLoad<gpu_mesh>( pushBlock.meshDescIdx, currentInst.meshIdx );
		
	float3 aabbMin = currentMesh.minAabb;
	float3 aabbMax = currentMesh.maxAabb;
		
	view_data cam = BufferLoad<view_data>( pushBlock.camIdx );

	bool testOcclusion = !bool( pushBlock.isLatePass ) ? true : instanceIsOccluded;
	bool visible = false;
	//if( !bool( pushBlock.isLatePass ) )
	//{
	//	// NOTE: 1st pass runs frustum culling with current instTransform and current cam
	//
	//	float4x4 mvp = mul( toWorld, mul( cam.mainView, cam.proj ) );
	//	frustum_culling_result frustumCullRes = FrustumCulling( aabbMin, aabbMax, mvp );
	//	testOcclusion = testOcclusion && !frustumCullRes.intersectsZNear;
	//	// NOTE: we might be visible but if we intersect the znear we skip occlusion
	//	visible = frustumCullRes.visible;
	//}
	
	//if( visible && testOcclusion )
	//{
	//	// NOTE: 1st pass uses prev instTransform prevCam and prev HZB
	//	float4x4 view = bool( pushBlock.isLatePass ) ? cam.mainView : cam.prevView;
	//	float4x4 mvpOcclusion = mul( toWorld, mul( view, cam.proj ) );
	//	screenspace_aabb ssAabb = ProjectAabbToScreenSpace( aabbMin, aabbMax, mvpOcclusion );
	//
	//	Texture2D<float4> hizTex = gTexture2D_float4[ pushBlock.hizTexIdx ];
	//	SamplerState quadMin = samplers[ pushBlock.hizSamplerIdx ];
	//
	//	visible = ScreenSpaceAabbVsHiZ( ssAabb, hizTex, quadMin );
	//}
	visible = true;
	if( !bool( pushBlock.isLatePass ) )
	{
		BufferStore<u32>( pushBlock.visInstCacheIdx, visible ? 1 : 0, globalDispatchID.x );
	}

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