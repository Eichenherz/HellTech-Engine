#version 460

#extension GL_KHR_shader_subgroup_basic: require
#extension GL_KHR_shader_subgroup_arithmetic: require
#extension GL_KHR_shader_subgroup_ballot: require
#extension GL_KHR_shader_subgroup_shuffle: require

#extension GL_GOOGLE_include_directive: require

#define BINDLESS
#include "..\r_data_structs.h"


#extension GL_EXT_debug_printf : enable


layout( push_constant, scalar ) uniform block{
	uint64_t	instDescAddr;
	uint64_t	meshDescAddr;
	uint64_t    visInstAddr;
	uint64_t    atomicWorkgrCounterAddr;
	uint64_t    drawCounterAddr;
	uint64_t    dispatchCmdAddr;
	uint	    hizBuffIdx;
	uint	    hizSamplerIdx;
	uint		instCount;
	uint		camIdx;
};


layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer mesh_desc_ref{ 
	mesh_desc meshes[]; 
};
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer inst_desc_ref{
	instance_desc instDescs[];
};

// TODO: strike down
struct expandee_info
{
	uint instId;
	uint expOffset;
	uint expCount;
};

layout( buffer_reference, buffer_reference_align = 4 ) writeonly buffer vis_inst_ref{
	expandee_info visibleInsts[];
};

layout( buffer_reference, buffer_reference_align = 4 ) coherent buffer coherent_counter_ref{
	uint coherentCounter;
};

layout( buffer_reference, buffer_reference_align = 4 ) writeonly buffer dispatch_indirect_ref{
	dispatch_command dispatchCmd;
};
layout( buffer_reference, buffer_reference_align = 4 ) writeonly buffer draw_cmd_ref{
	draw_command drawCmd[];
};


shared uint workgrAtomicCounterShared = {};

layout( local_size_x_id = 0 ) in;

layout( local_size_x = 32, local_size_y = 1, local_size_z = 1 ) in;
void main()
{
	uint globalIdx = gl_GlobalInvocationID.x;

	if( globalIdx < instCount )
	{
		instance_desc currentInst = inst_desc_ref( instDescAddr ).instDescs[ globalIdx ];
		mesh_desc currentMesh = mesh_desc_ref( meshDescAddr ).meshes[ currentInst.meshIdx ];
		
		vec3 center = currentMesh.center;
		vec3 extent = currentMesh.extent;
		
		vec3 boxMin = center - extent;
		vec3 boxMax = center + extent;
		
		// TODO: culling inspired by Nabla
		// https://github.com/Devsh-Graphics-Programming/Nabla/blob/master/include/nbl/builtin/glsl/utils/culling.glsl
		// TODO: cleanup revisit same in cluster culling

		global_data cam = ssbos[camIdx].g;
		mat4 mvp = cam.proj * cam.mainView * currentInst.localToWorld;

		vec3 boxSize = boxMax - boxMin;
 		
		mat4 trsMvp = transpose( cam.proj * cam.mainView * currentInst.localToWorld );
		vec4 xPlanePos = trsMvp[ 3 ] + trsMvp[ 0 ];
		vec4 yPlanePos = trsMvp[ 3 ] + trsMvp[ 1 ];
		vec4 xPlaneNeg = trsMvp[ 3 ] - trsMvp[ 0 ];
		vec4 yPlaneNeg = trsMvp[ 3 ] - trsMvp[ 1 ];
		
		
		bool visible = true;
		visible = visible && ( dot( mix( boxMax, boxMin, lessThan( trsMvp[ 3 ].xyz, vec3( 0.0f ) ) ), trsMvp[ 3 ].xyz ) > -trsMvp[ 3 ].w );
		visible = visible && ( dot( mix( boxMax, boxMin, lessThan( xPlanePos.xyz, vec3( 0.0f ) ) ), xPlanePos.xyz ) > -xPlanePos.w );
		visible = visible && ( dot( mix( boxMax, boxMin, lessThan( yPlanePos.xyz, vec3( 0.0f ) ) ), yPlanePos.xyz ) > -yPlanePos.w );
		visible = visible && ( dot( mix( boxMax, boxMin, lessThan( xPlaneNeg.xyz, vec3( 0.0f ) ) ), xPlaneNeg.xyz ) > -xPlaneNeg.w );
		visible = visible && ( dot( mix( boxMax, boxMin, lessThan( yPlaneNeg.xyz, vec3( 0.0f ) ) ), yPlaneNeg.xyz ) > -yPlaneNeg.w );

		float minW = dot( mix( boxMax, boxMin, greaterThanEqual( trsMvp[ 3 ].xyz, vec3( 0.0f ) ) ), trsMvp[ 3 ].xyz ) + trsMvp[ 3 ].w;
		bool intersectsNearZ = minW <= 0.0f;

		if( visible && !intersectsNearZ )
		//{}if( false )
		{
			vec3 boxSize = boxMax - boxMin;
			vec3 boxCorners[] = { 
				boxMin,
				boxMin + vec3( boxSize.x, 0, 0 ),
				boxMin + vec3( 0, boxSize.y, 0 ),
				boxMin + vec3( 0, 0, boxSize.z ),
				boxMin + vec3( boxSize.xy, 0 ),
				boxMin + vec3( 0, boxSize.yz ),
				boxMin + vec3( boxSize.x, 0, boxSize.z ),
				boxMin + boxSize };
			
		    vec2 minXY = vec2( 1 );
		    vec2 maxXY = {};
			float maxZ = 0.0f;
		    [[ unroll ]]
		    for( int i = 0; i < 8; ++i )
		    {
		        vec4 clipPos = mvp * vec4( boxCorners[ i ], 1.0f );
		        clipPos.xyz = clipPos.xyz / clipPos.w;
		        clipPos.xy = clamp( clipPos.xy, -1, 1 );
		        clipPos.xy = clipPos.xy * vec2( 0.5, -0.5 ) + vec2( 0.5, 0.5 );
 			
		        minXY = min( clipPos.xy, minXY );
		        maxXY = max( clipPos.xy, maxXY );
				maxZ = max( maxZ, clipPos.z );
		    }
			
			vec2 size = abs( maxXY - minXY ) * textureSize( sampler2D( sampledImages[ hizBuffIdx ], samplers[ hizSamplerIdx ] ), 0 ).xy;
			float depthPyramidMaxMip = textureQueryLevels( sampler2D( sampledImages[ hizBuffIdx ], samplers[ hizSamplerIdx ] ) ) - 1.0f;
			
			float mipLevel = min( floor( log2( max( size.x, size.y ) ) ), depthPyramidMaxMip );
			float sampledDepth = 
				textureLod( sampler2D( sampledImages[ hizBuffIdx ], samplers[ hizSamplerIdx ] ), ( maxXY + minXY ) * 0.5f, mipLevel ).x;
			//float zNear = cam.proj[3][2];
			//visible = visible && ( sampledDepth * minW <= zNear );	
			visible = visible && ( sampledDepth <= maxZ );	
		}
		//visible = true;
		// TODO: must compute LOD based on AABB's screen area 
		// TODO: use meshlet lodding
		
		mesh_lod lod = currentMesh.lods[ 0 ];
		
		uvec4 ballotVisible = subgroupBallot( visible );
		uint subgrActiveInvocationsCount = subgroupBallotBitCount( ballotVisible );
		if( subgrActiveInvocationsCount > 0 ) 
		{
			// TODO: shared atomics + global atomics ?
			//uint subgrSlotOffset = subgroupElect() ? atomicAdd( drawCallCount, subgrActiveInvocationsCount ) : 0;
			uint subgrSlotOffset = subgroupElect() ? 
				atomicAdd( coherent_counter_ref( drawCounterAddr ).coherentCounter, subgrActiveInvocationsCount ) : 0;
			uint subgrActiveIdx = subgroupBallotExclusiveBitCount( ballotVisible );
			uint slotIdx = subgroupBroadcastFirst( subgrSlotOffset  ) + subgrActiveIdx;
		
			if( visible )
			{
				//uint slotIdx = atomicAdd( drawCallCount, 1 );
			
				//visibleInstsChunks[ slotIdx ].instId = globalIdx;
				//visibleInstsChunks[ slotIdx ].expOffset = lod.meshletOffset;
				//visibleInstsChunks[ slotIdx ].expCount = lod.meshletCount;
				
				vis_inst_ref( visInstAddr ).visibleInsts[ slotIdx ] = expandee_info( globalIdx, lod.meshletOffset, lod.meshletCount );
				
				//drawCmd[ slotIdx ].drawIdx = globalIdx;
				//drawCmd[ slotIdx ].indexCount = lod.indexCount;
				//drawCmd[ slotIdx ].firstIndex = lod.indexOffset;
				//drawCmd[ slotIdx ].vertexOffset = currentMesh.vertexOffset;
				//drawCmd[ slotIdx ].instanceCount = 1;
				//drawCmd[ slotIdx ].firstInstance = 0;
			}
		}
	}

	//if( gl_LocalInvocationID.x == 0 ) workgrAtomicCounterShared = atomicAdd( workgrAtomicCounter, 1 );
	if( gl_LocalInvocationID.x == 0 ) 
		workgrAtomicCounterShared = atomicAdd( coherent_counter_ref( atomicWorkgrCounterAddr ).coherentCounter, 1 );

	barrier();
	memoryBarrier();
	if( ( gl_LocalInvocationID.x == 0 ) && ( workgrAtomicCounterShared == gl_NumWorkGroups.x - 1 ) )
	{
		// TODO: pass as spec consts or push consts ? 
		//uint mletsExpDispatch = ( drawCallCount + 3 ) / 4;
		uint mletsExpDispatch = ( coherent_counter_ref( drawCounterAddr ).coherentCounter + 3 ) / 4;
		//dispatchCmd = dispatch_command( mletsExpDispatch, 1, 1 );
		dispatch_indirect_ref( dispatchCmdAddr ).dispatchCmd = dispatch_command( mletsExpDispatch, 1, 1 );
		// NOTE: reset atomicCounter
		//workgrAtomicCounter = 0;
		coherent_counter_ref( atomicWorkgrCounterAddr ).coherentCounter = 0;
	}
}