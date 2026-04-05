#include "r_data_structs2.h"

#include "culling.h"

[[vk::push_constant]]
culling_params pushBlock;


[numthreads(32, 1, 1)]
[shader("compute")]
void DrawCullCsMain( uint3 globalDispatchID : SV_DispatchThreadID, uint groupFlatIdx : SV_GroupIndex ) 
{
	if( globalDispatchID.x >= pushBlock.instCount )
	{
		return;
	}

	bool instanceIsOccluded = false;
	if( pushBlock.isLatePass )
	{
		instanceIsOccluded = BufferLoad<uint>( pushBlock.visInstCacheIdx, globalDispatchID.x );
	}
	
	if( pushBlock.isLatePass && !instanceIsOccluded )
	{
		return;
	}

	instance_desc currentInst = BufferLoad<instance_desc>( pushBlock.instDescIdx, globalDispatchID.x );
	gpu_mesh currentMesh = BufferLoad<gpu_mesh>( pushBlock.meshDescIdx, currentInst.meshIdx );
		
	float3 aabbMin = currentMesh.minAabb;
	float3 aabbMax = currentMesh.maxAabb;
		
	global_data cam = BufferLoad<global_data>( pushBlock.camIdx );
	
	bool testOcclusion = !pushBlock.isLatePass ? true : instanceIsOccluded;;
	bool visible = false;
	if( !pushBlock.isLatePass )
	{
		// NOTE: 1st pass runs frustum culling with current instTransform and current cam
		float4x4 mvp = mul( currentInst.localToWorld, mul( cam.mainView, cam.proj ) );
		frustum_culling_result frustumCullRes = FrustumCulling( aabbMin, aabbMax, mvp );
		testOcclusion &&= !frustumCullRes.intersectsZNear;
		// NOTE: we might be visible but if we intersect the znear we skip occlusion
		visible = frustumCullRes.visible;
	}
	
	if( visible && testOcclusion )
	{
		// NOTE: 1st pass uses prev instTransform prevCam and prev HZB
		float4x4 view = pushBlock.isLatePass ? cam.mainView : cam.prevView;
		float4x4 mvpOcclusion = mul( currentInst.localToWorld, mul( view, cam.proj ) );
		screenspace_aabb ssAabb = ProjectAabbToScreenSapce( aabbMin, aabbMax, mvpOcclusion );
				
		Texture2D<float4> hizTex = gTexture2D_float4[ pushBlock.hizTexIdx ];
		SamplerState quadMin = samplers[ pushBlock.hizSamplerIdx ];
			
		visible = ScreenSpaceAabbVsHiZ( ssAabb, hizTex, quadMin );
		
	}
	
	if( !pushBlock.isLatePass )
	{
		BufferStore<uint>( pushBlock.visInstCacheIdx, visible ? 1 : 0, globalDispatchID.x );
	}

	uint lanesVisible = WaveActiveCountBits( visible );
	uint offsetForWave = 0;
	if( lanesVisible > 0 )
	{
		if( WaveIsFirstLane() )
		{
			offsetForWave = BufferAtomicAdd( pushBlock.visibleItemsCount, lanesVisible );
		}
	}

	uint slotIdx = WaveReadLaneFirst( offsetForWave );
	if( visible )
	{
		// NOTE: bc wer'e lazy, we're gonna use the same struct 
		BufferStore<gpu_mesh>( pushBlock.visibleItems, currentMesh, slotIdx );
	}
}