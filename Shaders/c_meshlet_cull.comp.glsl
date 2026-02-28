#version 460


#extension GL_KHR_shader_subgroup_basic: require
#extension GL_KHR_shader_subgroup_arithmetic: require
#extension GL_KHR_shader_subgroup_ballot: require


#extension GL_GOOGLE_include_directive: require

#define BINDLESS
#include "..\r_data_structs.h"

layout( push_constant, scalar ) uniform block{
	uint64_t	instDescAddr;
	uint64_t	meshlet_w_conesAddr;
	uint64_t	inMeshletsIdAddr;
	uint64_t	inMeshletsCountAddr;
	uint64_t	compactedDrawAddr;
	uint64_t	drawCmdsAddr;
	uint64_t	drawCountAddr;
	uint64_t	dbgDrawCmdsAddr;
	uint	    hizBuffIdx;
	uint	    hizSamplerIdx;
	uint		camIdx;
};


layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer meshlet_w_cone_desc_ref{ 
	meshlet_w_cone meshlet_w_cones[]; 
};
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer inst_desc_ref{
	instance_desc instDescs[];
};


layout( buffer_reference, buffer_reference_align = 8 ) readonly buffer u64_ref{ 
	uint64_t meshlet_w_coneIdBuff[]; 
};
layout( buffer_reference, buffer_reference_align = 4 ) readonly buffer u32_ref{ 
	uint totalMeshletCount; 
};

layout( buffer_reference, buffer_reference_align = 4 ) writeonly buffer compacted_args_ref{
	compacted_draw_args compactedDrawArgs[];
};
layout( buffer_reference, scalar, buffer_reference_align = 8 ) writeonly buffer draw_ref{
	draw_indirect dbgBBoxDrawCmd[];
};
layout( buffer_reference, buffer_reference_align = 4 ) writeonly buffer draw_cmd_ref{
	draw_command drawCmds[];
};

layout( buffer_reference, buffer_reference_align = 4 ) coherent buffer coherent_counter_ref{
	uint coherentCounter;
};


shared uint workgrAtomicCounterShared = {};

const uint meshlet_w_conesPerWorkgr = 32;

// NOTE: meshlet_w_cone occlusion culling bug
// NOTE: hack fix: workgr == 32 ( and in id_expander dstWorkGrSize )
layout( local_size_x = 32, local_size_y = 1, local_size_z = 1 ) in;
void main()
{
	uint globalIdx = gl_GlobalInvocationID.x;

	if( globalIdx < u32_ref( inMeshletsCountAddr ).totalMeshletCount )
	{
		uint64_t mid = u64_ref( inMeshletsIdAddr ).meshlet_w_coneIdBuff[ globalIdx ];
		uint parentInstId = uint( mid & uint( -1 ) );
		uint meshlet_w_coneIdx = uint( mid >> 32 );

		instance_desc parentInst = inst_desc_ref( instDescAddr ).instDescs[ parentInstId ];
		meshlet_w_cone thisMeshlet = meshlet_w_cone_desc_ref( meshlet_w_conesAddr ).meshlet_w_cones[ meshlet_w_coneIdx ];

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

		if( false && visible && !intersectsNearZ )
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
		uvec4 ballotVisible = subgroupBallot( visible );
		uint subgrActiveInvocationsCount = subgroupBallotBitCount( ballotVisible );
		if( subgrActiveInvocationsCount > 0 ) 
		{
			// TODO: shared atomics + global atomics ?
			uint subgrSlotOffset = subgroupElect() ? 
				atomicAdd( coherent_counter_ref( drawCountAddr ).coherentCounter, subgrActiveInvocationsCount ) : 0;
			uint subgrActiveIdx = subgroupBallotExclusiveBitCount( ballotVisible );
			uint slotIdx = subgroupBroadcastFirst( subgrSlotOffset  ) + subgrActiveIdx;

			if( visible )
			{	
				compacted_args_ref( compactedDrawAddr ).compactedDrawArgs[ slotIdx ].nodeIdx = parentInstId; 
				// NOTE: will add more as we progress
				compacted_args_ref( drawCmdsAddr ).compactedDrawArgs[ slotIdx ].meshlet_w_coneIdx = uint(mid); 

				uint vtxOffset = thisMeshlet.dataOffset;
				uint firstIdx = vtxOffset + uint( thisMeshlet.vertexCount );

				draw_cmd_ref( drawCmdsAddr ).drawCmds[ slotIdx ].indexCount = thisMeshlet.triangleCount; 
				draw_cmd_ref( drawCmdsAddr ).drawCmds[ slotIdx ].instanceCount = 1;
				draw_cmd_ref( drawCmdsAddr ).drawCmds[ slotIdx ].firstIndex = firstIdx;
				draw_cmd_ref( drawCmdsAddr ).drawCmds[ slotIdx ].vertexOffset = int( vtxOffset );
				draw_cmd_ref( drawCmdsAddr ).drawCmds[ slotIdx ].firstInstance = 0;

				draw_ref( dbgDrawCmdsAddr ).dbgBBoxDrawCmd[ slotIdx ].drawIdx = mid;
				draw_ref( dbgDrawCmdsAddr ).dbgBBoxDrawCmd[ slotIdx ].firstVertex = 0;
				draw_ref( dbgDrawCmdsAddr ).dbgBBoxDrawCmd[ slotIdx ].vertexCount = 24;
				draw_ref( dbgDrawCmdsAddr ).dbgBBoxDrawCmd[ slotIdx ].instanceCount = 1;
				draw_ref( dbgDrawCmdsAddr ).dbgBBoxDrawCmd[ slotIdx ].firstInstance = 0;
			}
		}
	}
}