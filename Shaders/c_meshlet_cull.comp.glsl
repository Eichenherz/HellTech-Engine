#version 460


#extension GL_KHR_shader_subgroup_basic: require
#extension GL_KHR_shader_subgroup_arithmetic: require
#extension GL_KHR_shader_subgroup_ballot: require


#extension GL_GOOGLE_include_directive: require

#define BINDLESS
#include "..\r_data_structs.h"

layout( push_constant, scalar ) uniform block{
	uint64_t	instDescAddr;
	uint64_t	meshletsAddr;
	uint64_t	inMeshletsIdAddr;
	uint64_t	inMeshletsCountAddr;
	uint64_t	outMeshletsIdAddr;
	uint64_t	outMeshletsCountAddr;
	uint64_t	atomicWorkgrCounterAddr;
	uint64_t	dispatchCmdAddr;
	uint64_t	dbgDrawCmdsAddr;
	uint	    hizBuffIdx;
	uint	    hizSamplerIdx;
	uint		camIdx;
};


layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer meshlet_desc_ref{ 
	meshlet meshlets[]; 
};
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer inst_desc_ref{
	instance_desc instDescs[];
};


layout( buffer_reference, buffer_reference_align = 8 ) readonly buffer u64_ref{ 
	uint64_t meshletIdBuff[]; 
};
layout( buffer_reference, buffer_reference_align = 4 ) readonly buffer u32_ref{ 
	uint totalMeshletCount; 
};

layout( buffer_reference, buffer_reference_align = 4 ) writeonly buffer dispatch_indirect_ref{
	dispatch_command dispatchCmd;
};
layout( buffer_reference, scalar, buffer_reference_align = 4 ) writeonly buffer draw_cmd_ref{
	draw_indirect dbgBBoxDrawCmd[];
};

// TODO: pack into u64
struct meshlet_info
{
	uint dataOffset;
	uint16_t instId;
	uint8_t vtxCount;
	uint8_t triCount;
};
layout( buffer_reference, buffer_reference_align = 4 ) writeonly buffer mlet_info_ref{ 
	meshlet_info visibleMeshlets[];
};
layout( buffer_reference, buffer_reference_align = 4 ) coherent buffer coherent_counter_ref{
	uint coherentCounter;
};


shared uint workgrAtomicCounterShared = {};

const uint meshletsPerWorkgr = 32;

// NOTE: meshlet occlusion culling bug
// NOTE: hack fix: workgr == 32 ( and in id_expander dstWorkGrSize )
// NOTE: idiot fix: disable occlusion
// TODO: investigate further
layout( local_size_x = 32, local_size_y = 1, local_size_z = 1 ) in;
void main()
{
	uint globalIdx = gl_GlobalInvocationID.x;

	if( globalIdx < u32_ref( inMeshletsCountAddr ).totalMeshletCount )
	{
		uint64_t mid = u64_ref( inMeshletsIdAddr ).meshletIdBuff[ globalIdx ];
		uint parentInstId = uint( mid & uint( -1 ) );
		uint meshletIdx = uint( mid >> 32 );

		instance_desc parentInst = inst_desc_ref( instDescAddr ).instDescs[ parentInstId ];
		meshlet thisMeshlet = meshlet_desc_ref( meshletsAddr ).meshlets[ meshletIdx ];

		vec3 center = thisMeshlet.center;
		vec3 extent = thisMeshlet.extent;
		
		vec3 boxMin = center - extent;
		vec3 boxMax = center + extent;
		
		global_data cam = ssbos[camIdx].g;

		// NOTE: frustum culling inspired by Nabla
		// https://github.com/Devsh-Graphics-Programming/Nabla/blob/master/include/nbl/builtin/glsl/utils/culling.glsl
		mat4 trsMvp = transpose( cam.proj * cam.mainView * parentInst.localToWorld );
		vec4 xPlanePos = trsMvp[ 3 ] + trsMvp[ 0 ];
		vec4 yPlanePos = trsMvp[ 3 ] + trsMvp[ 1 ];
		vec4 xPlaneNeg = trsMvp[ 3 ] - trsMvp[ 0 ];
		vec4 yPlaneNeg = trsMvp[ 3 ] - trsMvp[ 1 ];
		
		bool visible = true;
		visible = visible &&( dot( mix( boxMax, boxMin, lessThan( trsMvp[ 3 ].xyz, vec3( 0.0f ) ) ), trsMvp[ 3 ].xyz ) > -trsMvp[ 3 ].w );
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
				boxMax };
			
		    vec2 minXY = vec2( 1 );
		    vec2 maxXY = {};
			float maxZ = 0.0f;

			mat4 mvp = transpose( trsMvp );
			
		    [[unroll]]
		    for( int i = 0; i < 8; ++i )
		    {
		        vec4 clipPos = mvp * vec4( boxCorners[ i ], 1 );
 			
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
			visible = visible && ( sampledDepth <= maxZ );	
		}
		//visible = true;
		uvec4 ballotVisible = subgroupBallot( visible );
		uint subgrActiveInvocationsCount = subgroupBallotBitCount( ballotVisible );
		if( subgrActiveInvocationsCount > 0 ) 
		{
			// TODO: shared atomics + global atomics ?
			//uint subgrSlotOffset = subgroupElect() ? atomicAdd( drawCallCount, subgrActiveInvocationsCount ) : 0;
			uint subgrSlotOffset = subgroupElect() ? 
				atomicAdd( coherent_counter_ref( outMeshletsCountAddr ).coherentCounter, subgrActiveInvocationsCount ) : 0;
			uint subgrActiveIdx = subgroupBallotExclusiveBitCount( ballotVisible );
			uint slotIdx = subgroupBroadcastFirst( subgrSlotOffset  ) + subgrActiveIdx;

			if( visible )
			{
				//uint slotIdx = atomicAdd( drawCallCount, 1 );
				
				mlet_info_ref( outMeshletsIdAddr ).visibleMeshlets[ slotIdx ].dataOffset = thisMeshlet.dataOffset;
				mlet_info_ref( outMeshletsIdAddr ).visibleMeshlets[ slotIdx ].instId = uint16_t( parentInstId );
				// NOTE: want all the indices
				mlet_info_ref( outMeshletsIdAddr ).visibleMeshlets[ slotIdx ].vtxCount = thisMeshlet.vertexCount;
				mlet_info_ref( outMeshletsIdAddr ).visibleMeshlets[ slotIdx ].triCount = thisMeshlet.triangleCount;

				draw_cmd_ref( dbgDrawCmdsAddr ).dbgBBoxDrawCmd[ slotIdx ].drawIdx = mid;
				draw_cmd_ref( dbgDrawCmdsAddr ).dbgBBoxDrawCmd[ slotIdx ].firstVertex = 0;
				draw_cmd_ref( dbgDrawCmdsAddr ).dbgBBoxDrawCmd[ slotIdx ].vertexCount = 24;
				draw_cmd_ref( dbgDrawCmdsAddr ).dbgBBoxDrawCmd[ slotIdx ].instanceCount = 1;
				draw_cmd_ref( dbgDrawCmdsAddr ).dbgBBoxDrawCmd[ slotIdx ].firstInstance = 0;
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
		//uint trisExpDispatch = ( drawCallCount + meshletsPerWorkgr - 1 ) / meshletsPerWorkgr;
		uint trisExpDispatch = 
			( coherent_counter_ref( outMeshletsCountAddr ).coherentCounter + meshletsPerWorkgr - 1 ) / meshletsPerWorkgr;

		dispatch_indirect_ref( dispatchCmdAddr ).dispatchCmd = dispatch_command( trisExpDispatch, 1, 1 );
		
		//workgrAtomicCounter = 0;
		coherent_counter_ref( atomicWorkgrCounterAddr ).coherentCounter = 0;
	}
}