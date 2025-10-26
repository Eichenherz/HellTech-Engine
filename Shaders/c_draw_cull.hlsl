#include "r_data_structs2.h"

#include "culling.h"

[[vk::push_constant]]
culling_params pushBlock;

bool ScreenSpaceAabbVsHiZ( in screenspace_aabb ssAabb, in Texture2D<float4> hizTex, in SamplerState quadMin )
{
	uint3 widthHeightMipCount;
	hizTex.GetDimensions( 0, widthHeightMipCount.x, widthHeightMipCount.y, widthHeightMipCount.z );
	
	float2 size = abs( ssAabb.maxXY - ssAabb.minXY ) * float2( widthHeightMipCount.xy );
	float maxMipLevel = float( widthHeightMipCount.z ) - 1.0f;
				
	float chosenMipLevel = min( floor( log2( max( size.x, size.y ) ) ), maxMipLevel );
			
	float2 uv = ( ssAabb.maxXY + ssAabb.minXY ) * 0.5f;
	
	float sampledDepth = hizTex.SampleLevel( quadMin, uv, chosenMipLevel ).x;
	
	return ( sampledDepth <= ssAabb.maxZ );
}

bool GetInstanceVisFromCache( uint cacheBuffIdx, uint instID )
{
	uint bucketId = instID / 32;
	uint entryID = instID & 31;
	
	uint visBucket = BufferLoad<uint>( cacheBuffIdx, bucketId );
	
	return visBucket & entryID;
}

void SetInstanceVisFromCache( uint cacheBuffIdx, uint instID )
{
	uint bucketId = instID / 32;
	uint entryID = instID & 31;
	
	uint old;
	// NOTE: bc we can't use ResourceHeap yet we gotta do this BS
	storageBuffers[ cacheBuffIdx ].InterlockedOr( bucketId, entryID, old );
}

groupshared uint ldsGroupOffset;
groupshared uint ldsGroupCounter;

[numthreads(32, 1, 1)]
[shader("compute")]
void DrawCullCsMain( uint3 globalDispatchID : SV_DispatchThreadID, uint groupFlatIdx : SV_GroupIndex ) 
{
	if( groupFlatIdx == 0 )
	{
		ldsGroupOffset = 0;
	}
	GroupMemoryBarrierWithGroupSync();
	
	bool isValidInstanceIdx = globalDispatchID.x < pushBlock.instCount;
	bool testVisibility = true;
	if( isValidInstanceIdx && !pushBlock.isLatePass )
	{
		testVisibility = GetInstanceVisFromCache( pushBlock.visInstCacheIdx, globalDispatchID.x );
	}
	
	bool visible = false;
	instance_desc currentInst;
	mesh_desc currentMesh;
	if( isValidInstanceIdx && testVisibility )
	{
		currentInst = BufferLoad<instance_desc>( pushBlock.instDescIdx, globalDispatchID.x );
		currentMesh = BufferLoad<mesh_desc>( pushBlock.meshDescIdx, currentInst.meshIdx );
		
		float3 center = currentMesh.center;
		float3 extent = currentMesh.extent;
		
		float3 aabbMin = center - extent;
		float3 aabbMax = center + extent;
		
		global_data cam = BufferLoad<global_data>( pushBlock.camIdx );
		float4x4 mvp = mul( currentInst.localToWorld, mul( cam.mainView, cam.proj ) );
			
		frustum_culling_result frustumCullRes = FrustumCulling( aabbMin, aabbMax, mvp );
		
		visible = frustumCullRes.visible;
		if( pushBlock.isLatePass && ( visible && !frustumCullRes.intersectsZNear ) )
		{
			screenspace_aabb ssAabb = ProjectAabbToScreenSapce( aabbMin, aabbMax, mvp );
				
			Texture2D<float4> hizTex = gTexture2D_float4[ pushBlock.hizTexIdx ];
			SamplerState quadMin = samplers[ pushBlock.hizSamplerIdx ];
			
			visible = ScreenSpaceAabbVsHiZ( ssAabb, hizTex, quadMin );
			if( visible )
			{
				SetInstanceVisFromCache( pushBlock.visInstCacheIdx, globalDispatchID.x );
			}
		}
	}
	
	uint lanesVisible = WaveActiveCountBits( visible );
	uint offsetForWave = 0;
	if( lanesVisible > 0 )
	{
		if( WaveIsFirstLane() )
		{
			InterlockedAdd( ldsGroupOffset, lanesVisible, offsetForWave );
		}
		// NOTE: assuming IDs match for WaveIsFirstLane and WaveReadLaneFirst
		offsetForWave = WaveReadLaneFirst( offsetForWave );
	}

	GroupMemoryBarrierWithGroupSync();
	if( groupFlatIdx == 0 )
	{
		uint offsetForGroup;
		storageBuffers[ pushBlock.drawCounterIdx ].InterlockedAdd( 0, ldsGroupOffset, offsetForGroup );
		ldsGroupOffset = offsetForGroup;
	}

	DeviceMemoryBarrier();
	GroupMemoryBarrierWithGroupSync();
	uint activeLaneOffset = WavePrefixCountBits( lanesVisible );
	uint slotIdx = WaveReadLaneFirst( offsetForWave + ldsGroupOffset ) + activeLaneOffset;
	if( visible )
	{
		// TODO: use meshlet lodding
		mesh_lod lod = currentMesh.lods[ 0 ];
		draw_command cmd;
		cmd.drawIdx = globalDispatchID.x; // NOTE: instance ID !!!
		cmd.indexCount = lod.indexCount;
		cmd.instanceCount = 1;
		cmd.firstIndex = lod.indexOffset;
		cmd.vertexOffset = currentMesh.vertexOffset;
		cmd.firstInstance = 0;
		storageBuffers[ pushBlock.drawBuffIdx ].Store( slotIdx * sizeof( draw_command ), cmd );
	}

	if( groupFlatIdx == 0 )
	{
		storageBuffers[ pushBlock.atomicWgCounterIdx ].InterlockedAdd( 0, 1, ldsGroupCounter );
	}

	GroupMemoryBarrierWithGroupSync();
	if( ldsGroupCounter != ( WORK_GROUP_COUNT.x - 1 ) )
	{
		return;
	}

	if( groupFlatIdx == 0 )
	{
		//uint mletsExpDispatch = ( BufferLoad<uint>( pushBlock.drawCounterIdx ) + 3 ) / 4;
		//storageBuffers[ pushBlock.dispatchCmdIdx ].Store( 0, dispatch_command{ mletsExpDispatch, 1, 1 } );
		// NOTE: reset atomicCounter
		storageBuffers[ pushBlock.atomicWgCounterIdx ].Store( 0, 0 );
	}
}