#version 460


#extension GL_KHR_shader_subgroup_basic: require
#extension GL_KHR_shader_subgroup_arithmetic: require
#extension GL_KHR_shader_subgroup_ballot: require


#extension GL_GOOGLE_include_directive: require

#define GLOBAL_RESOURCES

#include "..\r_data_structs.h"


layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer meshlet_desc_ref{ 
	meshlet meshlets[]; 
};
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer inst_desc_ref{
	instance_desc instDescs[];
};


layout( binding = 0 ) readonly buffer meshlet_list{
	uint64_t meshletIdBuff[];
};
layout( binding = 1 ) readonly buffer meshlet_list_cnt{
	uint totalMeshletCount;
};
layout( binding = 2, scalar ) writeonly buffer dbg_draw_cmd{
	draw_command dbgDrawCmd[];
};
layout( binding = 3 ) buffer dbg_draw_cmd_count{
	uint dbgDrawCallCount;
};
layout( binding = 4 ) uniform sampler2D minQuadDepthPyramid;


layout( local_size_x = 256, local_size_y = 1, local_size_z = 1 ) in;
void main()
{
	uint globalIdx = gl_GlobalInvocationID.x;

	if( globalIdx >= totalMeshletCount ) return;

	uint64_t mid = meshletIdBuff[ globalIdx ];
	uint parentInstId = uint( mid & uint( -1 ) );
	uint meshletIdx = uint( mid >> 32 );

	instance_desc currentInst = inst_desc_ref( bdas.instDescAddr ).instDescs[ parentInstId ];
	meshlet currentMeshlet = meshlet_desc_ref( bdas.meshletsAddr ).meshlets[ meshletIdx ];

	vec3 center = currentMeshlet.center;
	vec3 extent = abs( currentMeshlet.extent );
	
	vec3 boxMin = ( center - extent ).xyz;
	vec3 boxMax = ( center + extent ).xyz;
	
	// NOTE: frustum culling inspired by Nabla
	// https://github.com/Devsh-Graphics-Programming/Nabla/blob/master/include/nbl/builtin/glsl/utils/culling.glsl
	mat4 transpMvp = transpose( cam.proj * cam.mainView * currentInst.localToWorld );
	vec4 xPlanePos = transpMvp[ 3 ] + transpMvp[ 0 ];
	vec4 yPlanePos = transpMvp[ 3 ] + transpMvp[ 1 ];
	vec4 xPlaneNeg = transpMvp[ 3 ] - transpMvp[ 0 ];
	vec4 yPlaneNeg = transpMvp[ 3 ] - transpMvp[ 1 ];
	
	bool visible = dot( mix( boxMax, boxMin, lessThan( transpMvp[ 3 ].xyz, vec3( 0.0f ) ) ), transpMvp[ 3 ].xyz ) > -transpMvp[ 3 ].w;
	visible = visible && ( dot( mix( boxMax, boxMin, lessThan( xPlanePos.xyz, vec3( 0.0f ) ) ), xPlanePos.xyz ) > -xPlanePos.w );
	visible = visible && ( dot( mix( boxMax, boxMin, lessThan( yPlanePos.xyz, vec3( 0.0f ) ) ), yPlanePos.xyz ) > -yPlanePos.w );
	visible = visible && ( dot( mix( boxMax, boxMin, lessThan( xPlaneNeg.xyz, vec3( 0.0f ) ) ), xPlaneNeg.xyz ) > -xPlaneNeg.w );
	visible = visible && ( dot( mix( boxMax, boxMin, lessThan( yPlaneNeg.xyz, vec3( 0.0f ) ) ), yPlaneNeg.xyz ) > -yPlaneNeg.w );

	vec3 localCamPos = ( inverse( currentInst.localToWorld ) * vec4( cam.camPos, 1 ) ).xyz;
	bool camInsideAabb = all( greaterThanEqual( localCamPos, boxMin ) ) && all( lessThanEqual( localCamPos, boxMax ) );
	if( visible && !camInsideAabb )
	{
		// TODO: use this perspZ or compute per min/max Bound ? 
		float perspZ = dot( mix( boxMax, boxMin, lessThan( transpMvp[ 3 ].xyz, vec3( 0.0f ) ) ), transpMvp[ 3 ].xyz ) + transpMvp[ 3 ].w;
		
		float depthPyrLodCount = textureQueryLevels( minQuadDepthPyramid );
		
		
		vec3 boxSize = boxMax - boxMin;
 		
        vec3 boxCorners[] = { boxMin,
                                boxMin + vec3(boxSize.x,0,0),
                                boxMin + vec3(0, boxSize.y,0),
                                boxMin + vec3(0, 0, boxSize.z),
                                boxMin + vec3(boxSize.xy,0),
                                boxMin + vec3(0, boxSize.yz),
                                boxMin + vec3(boxSize.x, 0, boxSize.z),
                                boxMin + boxSize
                             };
		
        vec2 minXY = vec2(1);
        vec2 maxXY = {};
		
		mat4 mvp = transpose( transpMvp );
		
        [[unroll]]
        for( int i = 0; i < 8; ++i )
        {
            //transform world space aaBox to NDC
            vec4 clipPos = mvp * vec4( boxCorners[ i ], 1 );
 		
            clipPos.xyz = clipPos.xyz / clipPos.w;
			//debugPrintfEXT( "ClipPos = %v4f", clipPos );
		
            clipPos.xy = clamp( clipPos.xy, -1, 1 );
            clipPos.xy = clipPos.xy * vec2( 0.5, -0.5 ) + vec2( 0.5, 0.5 );
 		
            minXY = min( clipPos.xy, minXY );
            maxXY = max( clipPos.xy, maxXY );
        }
		
		vec2 size = abs( maxXY - minXY ) * textureSize( minQuadDepthPyramid, 0 ).xy;
		float mip = min( floor( log2( max( size.x, size.y ) ) ), depthPyrLodCount );
		
		float minDepth = textureLod( minQuadDepthPyramid, ( maxXY + minXY ) * 0.5f, mip ).x;
		visible = visible && ( minDepth * perspZ <= 1.0f );	
	}

	uvec4 ballotVisible = subgroupBallot( visible );
	uint subgrActiveCount = subgroupBallotBitCount( ballotVisible );
	
	if( subgrActiveCount == 0 ) return;
	// TODO: shared atomics + global atomics ?
	uint subgrSlotOffset = ( gl_SubgroupInvocationID == 0 ) ? atomicAdd( dbgDrawCallCount, subgrActiveCount ) : 0;
	
	uint subgrActiveIdx = subgroupBallotExclusiveBitCount( ballotVisible );
	uint slotIdx = subgroupBroadcastFirst( subgrSlotOffset  ) + subgrActiveIdx;

	if( visible )
	{
		//uint slotIdx = atomicAdd( dbgDrawCallCount, 1 );
		dbgDrawCmd[ slotIdx ].drawIdx = parentInstId;
		dbgDrawCmd[ slotIdx ].indexCount = uint( currentMeshlet.triangleCount ) * 3;
		dbgDrawCmd[ slotIdx ].instanceCount = 1;
		dbgDrawCmd[ slotIdx ].firstIndex = currentMeshlet.triBufOffset;
		dbgDrawCmd[ slotIdx ].vertexOffset = currentMeshlet.vtxBufOffset;
		dbgDrawCmd[ slotIdx ].firstInstance = 0;
	}
}