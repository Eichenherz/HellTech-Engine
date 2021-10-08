#version 460

#extension GL_KHR_shader_subgroup_basic: require
#extension GL_KHR_shader_subgroup_arithmetic: require
#extension GL_KHR_shader_subgroup_ballot: require
#extension GL_KHR_shader_subgroup_shuffle: require

#extension GL_GOOGLE_include_directive: require

//#define GLOBAL_RESOURCES

#include "..\r_data_structs.h"


#extension GL_EXT_debug_printf : enable


layout( push_constant, scalar ) uniform block{
	uint64_t	instDescAddr;
	uint64_t	meshDescAddr;
	uint		instCount;
};


layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer mesh_desc_ref{ 
	mesh_desc meshes[]; 
};
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer inst_desc_ref{
	instance_desc instDescs[];
};

layout( binding = 0 ) readonly uniform cam_data{
	global_data cam;
};

// TODO: strike down
struct expandee_info
{
	uint instId;
	uint expOffset;
	uint expCount;
};

layout( binding = 1 ) writeonly buffer visible_insts{
	expandee_info visibleInstsChunks[];
};

layout( binding = 2 ) uniform sampler2D minQuadDepthPyramid;

layout( binding = 3 ) coherent buffer atomic_cnt{
	uint workgrAtomicCounter;
};
layout( binding = 4 ) buffer disptach_indirect{
	dispatch_command dispatchCmd;
};
layout( binding = 5 ) coherent buffer draw_cmd_count{
	uint drawCallCount;
};
layout( binding = 6 ) writeonly buffer draw_cmd{
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
		mat4 mvp = cam.proj * cam.mainView * currentInst.localToWorld;

		vec3 boxSize = boxMax - boxMin;
 		
		//vec4 clipCorners[] = { 
		//	mvp * vec4( boxMin, 1.0f ),
		//	mvp * vec4( boxMin + vec3( boxSize.x, 0, 0 ), 1.0f ),
		//	mvp * vec4( boxMin + vec3( 0, boxSize.y, 0 ), 1.0f ),
		//	mvp * vec4( boxMin + vec3( 0, 0, boxSize.z ), 1.0f ),
		//	mvp * vec4( boxMin + vec3( boxSize.xy, 0 ), 1.0f ),
		//	mvp * vec4( boxMin + vec3( 0, boxSize.yz ), 1.0f ),
		//	mvp * vec4( boxMin + vec3( boxSize.x, 0, boxSize.z ), 1.0f ),
		//	mvp * vec4( boxMin + boxSize, 1.0f ) };
		//
		//[[ unroll ]]
		//for( uint i = 0; i < 8; ++i )
		//{
		//	debugPrintfEXT( "ClipPos = %v4f", clipCorners[ i ] );
		//}

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


		//float xMin = dot( mix( boxMax, boxMin, lessThan( xPlanePos.xyz, vec3( 0.0f ) ) ), xPlanePos.xyz ) + xPlanePos.w;
		//float yMin = dot( mix( boxMax, boxMin, lessThan( yPlanePos.xyz, vec3( 0.0f ) ) ), yPlanePos.xyz ) + yPlanePos.w;
		//float xMax = dot( mix( boxMax, boxMin, lessThan( xPlaneNeg.xyz, vec3( 0.0f ) ) ), xPlaneNeg.xyz ) + xPlaneNeg.w;
		//float yMax = dot( mix( boxMax, boxMin, lessThan( yPlaneNeg.xyz, vec3( 0.0f ) ) ), yPlaneNeg.xyz ) + yPlaneNeg.w;
		//
		//float xMinNbl = dot( mix( boxMax, boxMin, greaterThanEqual( xPlanePos.xyz, vec3( 0.0f ) ) ), xPlanePos.xyz ) + xPlanePos.w;
		//float yMinNbl = dot( mix( boxMax, boxMin, greaterThanEqual( yPlanePos.xyz, vec3( 0.0f ) ) ), yPlanePos.xyz ) + yPlanePos.w;
		//float xMaxNbl = dot( mix( boxMax, boxMin, greaterThanEqual( xPlaneNeg.xyz, vec3( 0.0f ) ) ), xPlaneNeg.xyz ) + xPlaneNeg.w;
		//float yMaxNbl = dot( mix( boxMax, boxMin, greaterThanEqual( yPlaneNeg.xyz, vec3( 0.0f ) ) ), yPlaneNeg.xyz ) + yPlaneNeg.w;

		//debugPrintfEXT( "xMin = %f", xMin );
		//debugPrintfEXT( "yMin = %f", yMin );
		//debugPrintfEXT( "xMax = %f", xMax );
		//debugPrintfEXT( "yMax = %f", yMax );
		//
		//debugPrintfEXT( "xMinNbl = %f", xMinNbl );
		//debugPrintfEXT( "yMinNbl = %f", yMinNbl );
		//debugPrintfEXT( "xMaxNbl = %f", xMaxNbl );
		//debugPrintfEXT( "yMaxNbl = %f", yMaxNbl );

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
			
			vec2 size = abs( maxXY - minXY ) * textureSize( minQuadDepthPyramid, 0 ).xy;
			float depthPyramidMaxMip = textureQueryLevels( minQuadDepthPyramid ) - 1.0f;
			
			float mipLevel = min( floor( log2( max( size.x, size.y ) ) ), depthPyramidMaxMip );
			float sampledDepth = textureLod( minQuadDepthPyramid, ( maxXY + minXY ) * 0.5f, mipLevel ).x;
			//float zNear = cam.proj[3][2];
			//visible = visible && ( sampledDepth * minW <= zNear );	
			visible = visible && ( sampledDepth <= maxZ );	
		}
		//visible = true;
		// TODO: must compute LOD based on AABB's screen area
		//float lodLevel = log2( max( 1, distance( center.xyz, cam.camPos ) - length( extent ) ) );
		//uint lodIdx = clamp( uint( lodLevel ), 0, currentMesh.lodCount - 1 );
		mesh_lod lod = currentMesh.lods[ 0 ];
		
		uvec4 ballotVisible = subgroupBallot( visible );
		uint subgrActiveInvocationsCount = subgroupBallotBitCount( ballotVisible );
		if( subgrActiveInvocationsCount > 0 ) 
		{
			// TODO: shared atomics + global atomics ?
			uint subgrSlotOffset = subgroupElect() ? atomicAdd( drawCallCount, subgrActiveInvocationsCount ) : 0;
			uint subgrActiveIdx = subgroupBallotExclusiveBitCount( ballotVisible );
			uint slotIdx = subgroupBroadcastFirst( subgrSlotOffset  ) + subgrActiveIdx;
		
			if( visible )
			{
				//uint slotIdx = atomicAdd( drawCallCount, 1 );
			
				visibleInstsChunks[ slotIdx ].instId = globalIdx;
				visibleInstsChunks[ slotIdx ].expOffset = lod.meshletOffset;
				visibleInstsChunks[ slotIdx ].expCount = lod.meshletCount;
			
				drawCmd[ slotIdx ].drawIdx = globalIdx;
				drawCmd[ slotIdx ].indexCount = lod.indexCount;
				drawCmd[ slotIdx ].firstIndex = lod.indexOffset;
				drawCmd[ slotIdx ].vertexOffset = currentMesh.vertexOffset;
				drawCmd[ slotIdx ].instanceCount = 1;
				drawCmd[ slotIdx ].firstInstance = 0;
			}
		}
	}

	if( gl_LocalInvocationID.x == 0 ) workgrAtomicCounterShared = atomicAdd( workgrAtomicCounter, 1 );

	barrier();
	memoryBarrier();
	if( ( gl_LocalInvocationID.x == 0 ) && ( workgrAtomicCounterShared == gl_NumWorkGroups.x - 1 ) )
	{
		// TODO: pass as spec consts or push consts ? 
		uint mletsExpDispatch = ( drawCallCount + 3 ) / 4;
		dispatchCmd = dispatch_command( mletsExpDispatch, 1, 1 );
		// NOTE: reset atomicCounter
		workgrAtomicCounter = 0;
	}
}