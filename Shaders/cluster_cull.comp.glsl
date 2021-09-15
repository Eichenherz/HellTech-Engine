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
layout( binding = 2, scalar ) writeonly buffer draw_cmd{
	draw_command drawCmd[];
};
layout( binding = 3 ) buffer draw_cmd_count{
	uint drawCallCount;
};

//struct expandee_info
//{
//	uint instId;
//	uint expOffset;
//	uint expCount;
//};
//layout( binding = 4 ) writeonly buffer triangle_ids{
//	expandee_info visibleMeshlets[];
//};
struct meshlet_info
{
	uint triOffset;
	uint vtxOffset;
	uint16_t instId;
	uint16_t idxCount;
};
layout( binding = 4 ) writeonly buffer triangle_ids{
	meshlet_info visibleMeshlets[];
};

layout( binding = 5 ) uniform sampler2D minQuadDepthPyramid;
layout( binding = 6 ) coherent buffer atomic_cnt{
	uint workgrAtomicCounter;
};
layout( binding = 7 ) buffer disptach_indirect{
	dispatch_command dispatchCmd;
};

layout( binding = 8, scalar ) writeonly buffer draw_indir{
	draw_indirect dbgDrawCmd[];
};

shared uint workgrAtomicCounterShared = {};


layout( local_size_x = 1, local_size_y = 1, local_size_z = 1 ) in;
void main()
{
	uint globalIdx = gl_GlobalInvocationID.x;

	if( globalIdx >= totalMeshletCount ) return;

	uint64_t mid = meshletIdBuff[ globalIdx ];
	uint parentInstId = uint( mid & uint( -1 ) );
	uint meshletIdx = uint( mid >> 32 );

	instance_desc currentInst = inst_desc_ref( bdas.instDescAddr ).instDescs[ parentInstId ];
	meshlet thisMeshlet = meshlet_desc_ref( bdas.meshletsAddr ).meshlets[ meshletIdx ];

	vec3 center = thisMeshlet.center;
	vec3 extent = abs( thisMeshlet.extent );
	
	vec3 boxMin = ( center - extent ).xyz;
	vec3 boxMax = ( center + extent ).xyz;
	
	// NOTE: frustum culling inspired by Nabla
	// https://github.com/Devsh-Graphics-Programming/Nabla/blob/master/include/nbl/builtin/glsl/utils/culling.glsl
	mat4 transpMvp = transpose( cam.proj * cam.mainView * currentInst.localToWorld );
	vec4 xPlanePos = transpMvp[ 3 ] + transpMvp[ 0 ];
	vec4 yPlanePos = transpMvp[ 3 ] + transpMvp[ 1 ];
	vec4 xPlaneNeg = transpMvp[ 3 ] - transpMvp[ 0 ];
	vec4 yPlaneNeg = transpMvp[ 3 ] - transpMvp[ 1 ];
	
	bool visible = true;
	visible = visible &&
		( dot( mix( boxMax, boxMin, lessThan( transpMvp[ 3 ].xyz, vec3( 0.0f ) ) ), transpMvp[ 3 ].xyz ) > -transpMvp[ 3 ].w );
	visible = visible && ( dot( mix( boxMax, boxMin, lessThan( xPlanePos.xyz, vec3( 0.0f ) ) ), xPlanePos.xyz ) > -xPlanePos.w );
	visible = visible && ( dot( mix( boxMax, boxMin, lessThan( yPlanePos.xyz, vec3( 0.0f ) ) ), yPlanePos.xyz ) > -yPlanePos.w );
	visible = visible && ( dot( mix( boxMax, boxMin, lessThan( xPlaneNeg.xyz, vec3( 0.0f ) ) ), xPlaneNeg.xyz ) > -xPlaneNeg.w );
	visible = visible && ( dot( mix( boxMax, boxMin, lessThan( yPlaneNeg.xyz, vec3( 0.0f ) ) ), yPlaneNeg.xyz ) > -yPlaneNeg.w );


	// TODO: in what space ?
	// NOTE: cone culling 
	//vec4 coneAxis = vec4( int( thisMeshlet.coneX ) / 127.0f, int( thisMeshlet.coneY ) / 127.0f, int( thisMeshlet.coneZ ) / 127.0f, 0.0f );
	//coneAxis = normalize( currentInst.localToWorld * coneAxis );
	//float coneCutoff = int( thisMeshlet.coneCutoff ) / 127.0f;
	//visible = visible && dot( cam.camViewDir, coneAxis.xyz ) < coneCutoff;

	

	vec3 localCamPos = ( inverse( currentInst.localToWorld ) * vec4( cam.worldPos, 1 ) ).xyz;
	bool camInsideAabb = all( greaterThanEqual( localCamPos, boxMin ) ) && all( lessThanEqual( localCamPos, boxMax ) );
	//if( visible && !camInsideAabb )
	if( false )
	{
		// TODO: use this perspZ or compute per min/max Bound ? 
		float perspZ = dot( mix( boxMax, boxMin, lessThan( transpMvp[ 3 ].xyz, vec3( 0.0f ) ) ), transpMvp[ 3 ].xyz ) + transpMvp[ 3 ].w;
		
		
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
		float minZ = 0.0f;

		mat4 mvp = transpose( transpMvp );
		
        [[unroll]]
        for( int i = 0; i < 8; ++i )
        {
            vec4 clipPos = mvp * vec4( boxCorners[ i ], 1 );
 		
            clipPos.xyz = clipPos.xyz / clipPos.w;

            clipPos.xy = clamp( clipPos.xy, -1, 1 );
            clipPos.xy = clipPos.xy * vec2( 0.5, -0.5 ) + vec2( 0.5, 0.5 );
 		
            minXY = min( clipPos.xy, minXY );
            maxXY = max( clipPos.xy, maxXY );
        }
		
		vec2 size = abs( maxXY - minXY ) * textureSize( minQuadDepthPyramid, 0 ).xy;
		float depthPyramidMaxMip = textureQueryLevels( minQuadDepthPyramid ) - 1.0f;
		float mipLevel = min( floor( log2( max( size.x, size.y ) ) ), depthPyramidMaxMip );
		
		float sampledDepth = textureLod( minQuadDepthPyramid, ( maxXY + minXY ) * 0.5f, mipLevel ).x;
		visible = visible && ( sampledDepth * perspZ <= 1.0f );	
	}

	//bool rangePassFilter = ( meshletIdx < 64 ) && ( meshletIdx > 12 );
	//visible = visible && rangePassFilter;
	//visible = visible && ( meshletIdx == 23 );
	//visible = ( meshletIdx == 23 );

	//uvec4 ballotVisible = subgroupBallot( visible );
	//uint subgrActiveInvocationsCount = subgroupBallotBitCount( ballotVisible );
	//
	//if( subgrActiveInvocationsCount == 0 ) return;
	//// TODO: shared atomics + global atomics ?
	//uint subgrSlotOffset = subgroupElect() ? atomicAdd( drawCallCount, subgrActiveInvocationsCount ) : 0;
	//
	//uint subgrActiveIdx = subgroupBallotExclusiveBitCount( ballotVisible );
	//uint slotIdx = subgroupBroadcastFirst( subgrSlotOffset  ) + subgrActiveIdx;

	if( visible )
	{
		uint slotIdx = atomicAdd( drawCallCount, 1 );

		//visibleMeshlets[ slotIdx ].instId = parentInstId;
		//visibleMeshlets[ slotIdx ].expOffset = thisMeshlet.triBufOffset;
		//// NOTE: want all the indices
		//visibleMeshlets[ slotIdx ].expCount = uint( thisMeshlet.triangleCount ) * 3;

		
		visibleMeshlets[ slotIdx ].triOffset = thisMeshlet.triBufOffset;
		visibleMeshlets[ slotIdx ].vtxOffset = thisMeshlet.vtxBufOffset;
		visibleMeshlets[ slotIdx ].instId = uint16_t( parentInstId );
		// NOTE: want all the indices
		visibleMeshlets[ slotIdx ].idxCount = uint16_t( uint( thisMeshlet.triangleCount ) * 3 );

		drawCmd[ slotIdx ].drawIdx = parentInstId;
		drawCmd[ slotIdx ].indexCount = uint( thisMeshlet.triangleCount ) * 3;
		drawCmd[ slotIdx ].instanceCount = 1;
		drawCmd[ slotIdx ].firstIndex = thisMeshlet.triBufOffset;
		drawCmd[ slotIdx ].vertexOffset = thisMeshlet.vtxBufOffset;
		drawCmd[ slotIdx ].firstInstance = 0;

		dbgDrawCmd[ slotIdx ].drawIdx = mid;
		dbgDrawCmd[ slotIdx ].firstVertex = 0;
		dbgDrawCmd[ slotIdx ].vertexCount = 24;
		dbgDrawCmd[ slotIdx ].instanceCount = 1;
		dbgDrawCmd[ slotIdx ].firstInstance = 0;
	}				

	if( gl_LocalInvocationID.x == 0 ) workgrAtomicCounterShared = atomicAdd( workgrAtomicCounter, 1 );


	barrier();
	memoryBarrier();
	if( ( gl_LocalInvocationID.x == 0 ) && ( workgrAtomicCounterShared == gl_NumWorkGroups.x - 1 ) )
	{
		// TODO: pass as spec consts or push consts ? 
		uint trisExpDispatch = ( drawCallCount + 7 ) / 8;
		dispatchCmd = dispatch_command( trisExpDispatch, 1, 1 );
		// NOTE: reset atomicCounter
		workgrAtomicCounter = 0;
	}
}