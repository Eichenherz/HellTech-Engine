#version 460

#extension GL_KHR_shader_subgroup_basic: require
#extension GL_KHR_shader_subgroup_arithmetic: require
#extension GL_KHR_shader_subgroup_ballot: require
#extension GL_KHR_shader_subgroup_shuffle: require
//#extension GL_KHR_memory_scope_semantics: require


#extension GL_GOOGLE_include_directive: require

#define GLOBAL_RESOURCES

#include "..\r_data_structs.h"


#define GLSL_DBG 1

#extension GL_EXT_debug_printf : enable


layout( push_constant, scalar ) uniform block{
	cull_info cullInfo;
};


layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer mesh_desc_ref{ 
	mesh_desc meshes[]; 
};
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer inst_desc_ref{
	instance_desc instDescs[];
};


layout( binding = 0 ) writeonly buffer draw_cmd{
	draw_command drawCmd[];
};
layout( binding = 1 ) coherent buffer draw_cmd_count{
	uint drawCallCount;
};
layout( binding = 2 ) buffer draw_visibility_buffer{
	uint drawVisibility[];
};

// TODO: strike down
struct inst_chunk
{
	uint instID;
	uint mletOffset;
	uint mletCount;
};

layout( binding = 3 ) writeonly buffer visible_insts{
	inst_chunk visibleInstsChunks[];
};

layout( binding = 4 ) uniform sampler2D minQuadDepthPyramid;

layout( binding = 5 ) coherent buffer atomic_cnt{
	uint workgrAtomicCounter;
};
layout( binding = 6 ) buffer disptach_indirect{
	dispatch_command dispatchCmd;
};

#if GLSL_DBG
//layout( binding = 7, scalar ) writeonly buffer dbg_draw_cmd{
//	draw_indirect dbgDrawCmd[];
//};
//layout( binding = 8 ) buffer dbg_draw_cmd_count{
//	uint dbgDrawCallCount;
//};
#endif


// NOTE: https://research.nvidia.com/publication/2d-polyhedral-bounds-clipped-perspective-projected-3d-sphere 
// && niagara renderer by zeux
vec4 ProjectedSphereToAABB( vec3 viewSpaceCenter, float r, float perspDividedWidth, float perspDividedHeight )
{
	vec2 cXZ = viewSpaceCenter.xz;
	vec2 vXZ = vec2( sqrt( dot( cXZ, cXZ ) - r * r ), r );
	vec2 minX = mat2( vXZ.x, vXZ.y, -vXZ.y, vXZ.x ) * cXZ;
	vec2 maxX = mat2( vXZ.x, -vXZ.y, vXZ.y, vXZ.x ) * cXZ;

	vec2 cYZ = viewSpaceCenter.yz;
	vec2 vYZ = vec2( sqrt( dot( cYZ, cYZ ) - r * r ), r );
	vec2 minY = mat2( vYZ.x, -vYZ.y, vYZ.y, vYZ.x ) * cYZ;
	vec2 maxY = mat2( vYZ.x, vYZ.y, -vYZ.y, vYZ.x ) * cYZ;

	// NOTE: quick and dirty projection
	vec4 aabb = vec4( ( minX.x / minX.y ) * perspDividedWidth,
					  ( minY.x / minY.y ) * perspDividedHeight,
					  ( maxX.x / maxX.y ) * perspDividedWidth,
					  ( maxY.x / maxY.y ) * perspDividedHeight );

	// NOTE: from NDC to texture UV space 
	aabb = aabb.xyzw * vec4( 0.5, -0.5, 0.5, -0.5 ) + vec4( 0.5, 0.5, 0.5, 0.5 );

	return aabb;
}


shared uint workgrAtomicCounterShared = {};


layout( local_size_x_id = 0 ) in;
layout( constant_id = 1 ) const bool OCCLUSION_CULLING = false;

layout( local_size_x = 64, local_size_y = 1, local_size_z = 1 ) in;
void main()
{
	uint globalIdx = gl_GlobalInvocationID.x;

	
	if( globalIdx >= cullInfo.drawCallsCount ) return;
	
	instance_desc currentInst = inst_desc_ref( bdas.instDescAddr ).instDescs[ globalIdx ];
	mesh_desc currentMesh = mesh_desc_ref( bdas.meshDescAddr ).meshes[ currentInst.meshIdx ];
	
	vec3 center = currentMesh.center;
	vec3 extent = abs( currentMesh.extent );
	
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
	
	// TODO: faster ?
	// TODO: fix Nabla occlusion
	vec3 localCamPos = ( inverse( currentInst.localToWorld ) * vec4( cam.camPos, 1 ) ).xyz;
	bool camInsideAabb = all( greaterThanEqual( localCamPos, boxMin ) ) && all( lessThanEqual( localCamPos, boxMax ) );
	if( visible && !camInsideAabb && OCCLUSION_CULLING )
	{
		// TODO: use this perspZ or compute per min/max Bound ? 
		float perspZ = dot( mix( boxMax, boxMin, lessThan( transpMvp[ 3 ].xyz, vec3( 0.0f ) ) ), transpMvp[ 3 ].xyz ) + transpMvp[ 3 ].w;
		
		float depthPyrLodCount = textureQueryLevels( minQuadDepthPyramid );
		
		// NOTE: if faulty occlusion, prolly this was uncommented 
		//float xPosBound = dot( mix( boxMax, boxMin, lessThan( xPlanePos.xyz, vec3( 0.0f ) ) ), xPlanePos.xyz ) + xPlanePos.w;
		//float yPosBound = dot( mix( boxMax, boxMin, lessThan( yPlanePos.xyz, vec3( 0.0f ) ) ), yPlanePos.xyz ) + yPlanePos.w;
		//float xNegBound = dot( mix( boxMax, boxMin, lessThan( xPlaneNeg.xyz, vec3( 0.0f ) ) ), xPlaneNeg.xyz ) + xPlaneNeg.w;
		//float yNegBound = dot( mix( boxMax, boxMin, lessThan( yPlaneNeg.xyz, vec3( 0.0f ) ) ), yPlaneNeg.xyz ) + yPlaneNeg.w;
		//			    
		//xPosBound = clamp( xPosBound / perspZ, -1.0f, 1.0f ) * 0.5f + 0.5f;
		//yPosBound = clamp( yPosBound / perspZ, -1.0f, 1.0f ) * -0.5f + 0.5f;
		//xNegBound = clamp( xNegBound / perspZ, -1.0f, 1.0f ) * 0.5f + 0.5f;
		//yNegBound = clamp( yNegBound / perspZ, -1.0f, 1.0f ) * -0.5f + 0.5f;
		//
		//vec2 screenMin = vec2( min( xPosBound, xNegBound ), min( yPosBound, yNegBound ) );
		//vec2 screenMax = vec2( max( xPosBound, xNegBound ), max( yPosBound, yNegBound ) );
		//
		//vec2 screenBoxSize = abs( screenMax - screenMin ) * textureSize( minQuadDepthPyramid, 0 ).xy;
		//float mipLevel = min( floor( log2( max( screenBoxSize.x, screenBoxSize.y ) ) ), depthPyrLodCount );
		//
		//float sampledDepth = textureLod( minQuadDepthPyramid, ( screenMax + screenMin ) * 0.5f, mipLevel ).x;
		//visible = visible && ( 1.0f / perspZ >= sampledDepth );
	
	
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
	
	
	// TODO: must compute LOD based on AABB's screen area
	//float lodLevel = log2( max( 1, distance( center.xyz, cam.camPos ) - length( extent ) ) );
	//uint lodIdx = clamp( uint( lodLevel ), 0, currentMesh.lodCount - 1 );
	mesh_lod lod = currentMesh.lods[ 0 ];
	
	uvec4 ballotVisible = subgroupBallot( visible );
	uint visibleInstCount = subgroupBallotBitCount( ballotVisible );
	
	if( visibleInstCount == 0 ) return;
	// TODO: shared atomics + global atomics ?
	uint subgrSlotOffset = ( gl_SubgroupInvocationID == 0 ) ? atomicAdd( drawCallCount, visibleInstCount ) : 0;
	
	uint visibleInstIdx = subgroupBallotExclusiveBitCount( ballotVisible );
	uint drawCallIdx = subgroupBroadcastFirst( subgrSlotOffset  ) + visibleInstIdx;
	
	if( visible )
	{
		//uint drawCallIdx = atomicAdd( drawCallCount, 1 );
	
		visibleInstsChunks[ drawCallIdx ].instID = globalIdx;
		visibleInstsChunks[ drawCallIdx ].mletOffset = lod.meshletOffset;
		visibleInstsChunks[ drawCallIdx ].mletCount = lod.meshletCount;
	
		drawCmd[ drawCallIdx ].drawIdx = globalIdx;
		drawCmd[ drawCallIdx ].indexCount = lod.indexCount;
		drawCmd[ drawCallIdx ].firstIndex = lod.indexOffset;
		drawCmd[ drawCallIdx ].vertexOffset = currentMesh.vertexOffset;
		drawCmd[ drawCallIdx ].instanceCount = 1;
		drawCmd[ drawCallIdx ].firstInstance = 0;
	}
	

	if( gl_LocalInvocationID.x == 0 ) workgrAtomicCounterShared = atomicAdd( workgrAtomicCounter, 1 );
	barrier();

	if( ( gl_LocalInvocationID.x == 0 ) && ( workgrAtomicCounterShared == gl_NumWorkGroups.x - 1 ) )
	{
		// TODO: pass as spec consts or push consts ? 
		uint mletsExpDispatch = ( drawCallCount + 3 ) / 4;
		dispatchCmd = dispatch_command( mletsExpDispatch, 1, 1 );
		// NOTE: reset atomicCounter
		workgrAtomicCounter = 0;
	}
}