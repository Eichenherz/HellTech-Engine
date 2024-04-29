#include "../r_data.h"

// TODO: spv intrins ?
#define SPV_BUILTIN_NUM_WORKGROUPS 24
[[ vk::ext_decorate(SPV_BUILTIN_NUM_WORKGROUPS) ]] uint3 gl_NumWorkGroups;

// TODO: vk_bindless_utils.hlsli
template<typename T>
T UnifomLoadFromBuffer( in uint hlslBufferIdx, in uint entryIdx )
{
	return bufferTable[ hlslBufferIdx ].Load<T>( entryIdx * sizeof( T ) );
}
template<typename T>
T NonUnifomLoadFromBuffer( in uint hlslBufferIdx, in uint entryIdx )
{
	return bufferTable[ NonUniformResourceIndex( hlslBufferIdx ) ].Load<T>( entryIdx * sizeof( T ) );
}
template<typename T>
void UnifomStoreInBuffer( in uint hlslBufferIdx, in uint entryIdx, in T entry )
{
	uavBufferTable[ hlslBufferIdx ].Store<T>( entryIdx * sizeof( T ), entry );
}
template<typename T>
void NonUnifomStoreInBuffer( in uint hlslBufferIdx, in uint entryIdx, in T entry )
{
	uavBufferTable[ NonUniformResourceIndex( hlslBufferIdx ) ].Store<T>( entryIdx * sizeof( T ), entry );
}

// NOTE: culling inspired by Nabla
// https://github.com/Devsh-Graphics-Programming/Nabla/blob/master/include/nbl/builtin/glsl/utils/culling.glsl
void FrustumTestAABB( 
	in float3	boxMin, 
	in float3	boxMax, 
	in float4x4 trMvp, 
	inout bool	isVisible, 
	inout bool	crossesNearZ 
){
	float4 xPlanePos = trMvp[ 3 ] + trMvp[ 0 ];
	float4 yPlanePos = trMvp[ 3 ] + trMvp[ 1 ];
	float4 xPlaneNeg = trMvp[ 3 ] - trMvp[ 0 ];
	float4 yPlaneNeg = trMvp[ 3 ] - trMvp[ 1 ];
		
	isVisible = true;
	isVisible = isVisible && ( dot( lerp( boxMax, boxMin, trMvp[ 3 ].xyz < ( float3 ) 0.0f ), trMvp[ 3 ].xyz ) > -trMvp[ 3 ].w );
	isVisible = isVisible && ( dot( lerp( boxMax, boxMin, xPlanePos.xyz < ( float3 ) 0.0f ), xPlanePos.xyz ) > -xPlanePos.w );
	isVisible = isVisible && ( dot( lerp( boxMax, boxMin, yPlanePos.xyz < ( float3 ) 0.0f ), yPlanePos.xyz ) > -yPlanePos.w );
	isVisible = isVisible && ( dot( lerp( boxMax, boxMin, xPlaneNeg.xyz < ( float3 ) 0.0f ), xPlaneNeg.xyz ) > -xPlaneNeg.w );
	isVisible = isVisible && ( dot( lerp( boxMax, boxMin, yPlaneNeg.xyz < ( float3 ) 0.0f ), yPlaneNeg.xyz ) > -yPlaneNeg.w );

	float minW = dot( lerp( boxMax, boxMin, trMvp[ 3 ].xyz >= (float3) 0.0f ), trMvp[ 3 ].xyz ) + trMvp[ 3 ].w;
	crossesNearZ = minW <= 0.0f;
}

bool HizOcclusionTestAABB( 
	in float3 boxMin, 
	in float3 boxMax, 
	in float4x4 mvp,
	in Texture2D hiz,
	in SamplerState minQuad
){
	float3 boxSize = boxMax - boxMin;
	float3 boxCorners[] = {
		boxMin,
		boxMin + float3( boxSize.x, 0, 0 ),
		boxMin + float3( 0, boxSize.y, 0 ),
		boxMin + float3( 0, 0, boxSize.z ),
		boxMin + float3( boxSize.xy, 0 ),
		boxMin + float3( 0, boxSize.yz ),
		boxMin + float3( boxSize.x, 0, boxSize.z ),
		boxMin + boxSize
	};
			
	float2 minXY = 1.0f;
	float2 maxXY = 0.0f;
	// NOTE: inverted ops bc of inv depth buffer
	float maxZ = 0.0f;

	[ unroll ]
	for( uint i = 0; i < 8; ++i )
	{
		float4 clipPos = mul( float4( boxCorners[ i ], 1.0f ), mvp );
		clipPos.xyz = clipPos.xyz / clipPos.w;
		clipPos.xy = clamp( clipPos.xy, -1.0f, 1.0f );
		clipPos.xy = clipPos.xy * float2( 0.5f, -0.5f ) + float2( 0.5f, 0.5f );
 			
		minXY = min( clipPos.xy, minXY );
		maxXY = max( clipPos.xy, maxXY );
		maxZ = saturate( max( maxZ, clipPos.z ) );
	}
	
	float2 hizDim = 0.0f;
	float hizMipCount = 0.0f;
	hiz.GetDimensions( 0, hizDim.x, hizDim.y, hizMipCount );

	float2 size = abs( maxXY - minXY ) * hizDim;
	float mipLevel = min( floor( log2( max( size.x, size.y ) ) ), hizMipCount - 1.0f );

	float sampledDepth = hiz.SampleLevel( minQuad, ( maxXY + minXY ) * 0.5f, mipLevel ).x;
	
	//float zNear = cam.proj[3][2];
	//visible = visible && ( sampledDepth * minW <= zNear );	
	return ( sampledDepth <= maxZ );
}



[[vk::push_constant]]
struct{
	uint instBufferIdx;
	uint meshBufferIdx;

	uint visInstBufferIdx;
	uint drawCounterIdx;
	uint dispatchCmdBufferIdx;

	uint atomicWorkgrCounterIdx;

	uint hizTextureIdx;
	uint hizSamplerIdx;

	uint camIdx;

	uint instCount;
} pushBlock;


struct expandee_info
{
	uint instId;
	uint expOffset;
	uint expCount;
};

groupshared uint workgrAtomicCounterShared;

[numthreads(32, 1, 1)]
void CullInstancesMain( 
	uint3 globalIdx : SV_DispatchThreadID, 
	uint3 workGrIdx : SV_GroupThreadID,
	[[ vk::ext_decorate(SPV_BUILTIN_NUM_WORKGROUPS) ]] 
	uint3 gl_NumWorkGroups : gl_NumWorkGroups
){
	if( globalIdx.x < pushBlock.instCount )
	{
		instance_desc thisInst = UnifomLoadFromBuffer<instance_desc>( pushBlock.instBufferIdx, globalIdx.x );
		mesh_desc thisMesh = NonUnifomLoadFromBuffer<mesh_desc>( pushBlock.meshBufferIdx, thisInst.meshIdx );

		float3 center = thisMesh.center;
		float3 extent = thisMesh.extent;
		float3 boxMin = center - extent;
		float3 boxMax = center + extent;
		
		global_data cam = UnifomLoadFromBuffer<global_data>( pushBlock.camIdx, 0 );

		float4x4 mvp = cam.proj * cam.mainView * thisInst.localToWorld;

		bool isVisible = false;
		bool crossesNearZ = false;

		float4x4 trMvp = transpose( cam.proj * cam.mainView * thisInst.localToWorld );
		FrustumTestAABB( boxMin, boxMax, trMvp, isVisible, crossesNearZ );

		if( isVisible && !crossesNearZ )
		{
			Texture2D hiz = textureTable[ pushBlock.hizTextureIdx ];
			SamplerState minQuad = samplerTable[ pushBlock.hizSamplerIdx ];
			isVisible = HizOcclusionTestAABB( boxMin, boxMax, mvp, hiz, minQuad );
		}

		// NOTE: just for 32 sized waves
		uint ballotVisible = WaveActiveBallot( isVisible ).x;
		uint waveActiveInvocationsCount = WaveActiveCountBits( ballotVisible );

		if( waveActiveInvocationsCount > 0 )
		{
			uint waveSlotOffset = 0;
			if( WaveIsFirstLane() )
			{
				uavBufferTable[ pushBlock.drawCounterIdx ].InterlockedAdd( 0, waveActiveInvocationsCount, waveSlotOffset );
			}
				
			uint waveActiveIdx = WavePrefixCountBits( ballotVisible );
			uint slotIdx = WaveReadLaneFirst( waveSlotOffset ) + waveActiveIdx;
		
			if( isVisible )
			{
				mesh_lod lod = thisMesh.lods[ 0 ];
				expandee_info thisExpandee;
				thisExpandee.instId = globalIdx.x;
				thisExpandee.expOffset = lod.meshletOffset;
				thisExpandee.expCount = lod.meshletCount;
				UnifomStoreInBuffer( pushBlock.visInstBufferIdx, slotIdx, thisExpandee );
			}
		}

		if( workGrIdx.x == 0 )
		{
			uavBufferTable[ pushBlock.atomicWorkgrCounterIdx ].InterlockedAdd( 0, 1, workgrAtomicCounterShared );
		}

		GroupMemoryBarrierWithGroupSync();
		if( ( workGrIdx.x == 0 ) && ( workgrAtomicCounterShared == gl_NumWorkGroups.x - 1 ) )
		{
			// TODO: pass as spec consts or push consts ? 
			uint mletsExpDispatch = UnifomLoadFromBuffer<uint>( pushBlock.drawCounterIdx, 0 );
			mletsExpDispatch = ( mletsExpDispatch + 3 ) / 4;

			dispatch_command disptachCmd;
			disptachCmd.localSizeX = mletsExpDispatch;
			disptachCmd.localSizeY = 1;
			disptachCmd.localSizeZ = 1;
			UnifomStoreInBuffer( pushBlock.dispatchCmdBufferIdx, 0, disptachCmd );
			// NOTE: reset atomicCounter
			UnifomStoreInBuffer( pushBlock.atomicWorkgrCounterIdx, 0, 0u );
		}
	}
}