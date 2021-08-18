#version 460

#extension GL_KHR_shader_subgroup_basic: require
#extension GL_KHR_shader_subgroup_arithmetic: require
#extension GL_KHR_shader_subgroup_ballot: require
#extension GL_KHR_shader_subgroup_shuffle: require

#extension GL_GOOGLE_include_directive: require

#define GLOBAL_RESOURCES

#include "..\r_data_structs.h"

#define WAVE_OPS 0
#define GLSL_DBG 1

#extension GL_EXT_debug_printf : enable


layout( local_size_x_id = 0 ) in;
layout( constant_id = 1 ) const bool OCCLUSION_CULLING = false;


layout( local_size_x = 64, local_size_y = 1, local_size_z = 1 ) in;


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
layout( binding = 1 ) buffer draw_cmd_count{
	uint drawCallCount;
};
layout( binding = 2 ) buffer draw_visibility_buffer{
	uint drawVisibility[];
};
layout( binding = 3 ) buffer dispatch_cmd{
	dispatch_command dispatchCmd[];
};
layout( binding = 4 ) buffer mlet_inst_idx{
	uint mletInstIdx[];
};
layout( binding = 5 ) buffer mlet_dispatch_count{
	uint mletDispatchCount;
};
layout( binding = 6 ) uniform sampler2D minQuadDepthPyramid;


#if GLSL_DBG
layout( binding = 7, scalar ) writeonly buffer dbg_draw_cmd{
	draw_indirect dbgDrawCmd[];
};
layout( binding = 8 ) buffer dbg_draw_cmd_count{
	uint dbgDrawCallCount;
};
layout( binding = 9 ) writeonly buffer dbg_occlusion_data{
	occlusion_debug occDbgBuff[];
};
layout( binding = 10 ) writeonly buffer screen_boxes_buff{
	dbg_vertex screenspaceBoxBuff[];
};
#endif


// TODO: would a u64 be better here ?
shared uint meshletCullDispatchCounterLDS;

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

vec3 RotateQuat( vec3 v, vec4 q )
{
	vec3 t = 2.0 * cross( q.xyz, v );
	return v + q.w * t + cross( q.xyz, t );
}

void main()
{
	uint di = gl_GlobalInvocationID.x;

	if( di == 0 )
	{
		drawCallCount = 0;
		mletDispatchCount = 0;
	#if GLSL_DBG
		dbgDrawCallCount = 0;
	#endif
	}

	if( di >= cullInfo.drawCallsCount ) return;

#if GLSL_DBG
	//screenspaceBoxBuff[ 8 * di + 0 ] = dbg_vertex( vec4( 0 ), vec4( 0 ) );
	//screenspaceBoxBuff[ 8 * di + 1 ] = dbg_vertex( vec4( 0 ), vec4( 0 ) );
	//screenspaceBoxBuff[ 8 * di + 2 ] = dbg_vertex( vec4( 0 ), vec4( 0 ) );
	//screenspaceBoxBuff[ 8 * di + 3 ] = dbg_vertex( vec4( 0 ), vec4( 0 ) );
	//screenspaceBoxBuff[ 8 * di + 4 ] = dbg_vertex( vec4( 0 ), vec4( 0 ) );
	//screenspaceBoxBuff[ 8 * di + 5 ] = dbg_vertex( vec4( 0 ), vec4( 0 ) );
	//screenspaceBoxBuff[ 8 * di + 6 ] = dbg_vertex( vec4( 0 ), vec4( 0 ) );
	//screenspaceBoxBuff[ 8 * di + 7 ] = dbg_vertex( vec4( 0 ), vec4( 0 ) );
#endif

	instance_desc currentInst = inst_desc_ref( bdas.instDescAddr ).instDescs[ di ];
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
	vec3 localCamPos = ( inverse( currentInst.localToWorld ) * vec4( cam.camPos, 1 ) ).xyz;
	bool camInsideAabb = all( greaterThanEqual( localCamPos, boxMin ) ) && all( lessThanEqual( localCamPos, boxMax ) );
	if( visible && !camInsideAabb && OCCLUSION_CULLING )
	{
		// TODO: use this perspZ or compute per min/max Bound ? 
		float perspZ = dot( mix( boxMax, boxMin, lessThan( transpMvp[ 3 ].xyz, vec3( 0.0f ) ) ), transpMvp[ 3 ].xyz ) + transpMvp[ 3 ].w;
		float xPosBound = dot( mix( boxMax, boxMin, lessThan( xPlanePos.xyz, vec3( 0.0f ) ) ), xPlanePos.xyz ) + xPlanePos.w;
		float yPosBound = dot( mix( boxMax, boxMin, lessThan( yPlanePos.xyz, vec3( 0.0f ) ) ), yPlanePos.xyz ) + yPlanePos.w;
		float xNegBound = dot( mix( boxMax, boxMin, lessThan( xPlaneNeg.xyz, vec3( 0.0f ) ) ), xPlaneNeg.xyz ) + xPlaneNeg.w;
		float yNegBound = dot( mix( boxMax, boxMin, lessThan( yPlaneNeg.xyz, vec3( 0.0f ) ) ), yPlaneNeg.xyz ) + yPlaneNeg.w;
		
		xPosBound = clamp( xPosBound / perspZ, -1.0f, 1.0f ) * 0.5f + 0.5f;
		yPosBound = clamp( yPosBound / perspZ, -1.0f, 1.0f ) * -0.5f + 0.5f;
		xNegBound = clamp( xNegBound / perspZ, -1.0f, 1.0f ) * 0.5f + 0.5f;
		yNegBound = clamp( yNegBound / perspZ, -1.0f, 1.0f ) * -0.5f + 0.5f;
		
		vec2 screenMin = vec2( min( xPosBound, xNegBound ), min( yPosBound, yNegBound ) );
		vec2 screenMax = vec2( max( xPosBound, xNegBound ), max( yPosBound, yNegBound ) );

		vec2 screenBoxSize = abs( screenMax - screenMin ) * textureSize( minQuadDepthPyramid, 0 ).xy;
		
		float mipLevel = floor( log2( max( screenBoxSize.x, screenBoxSize.y ) ) );
		 
		float depth = textureLod( minQuadDepthPyramid, ( screenMax + screenMin ) * 0.5f, mipLevel ).x;
		visible = visible && ( 1.0f / perspZ >= depth );

	#if GLSL_DBG
		occDbgBuff[ di ].mvp = transpose( transpMvp );
		//occDbgBuff[ di ].minZCorner = vec4( boxCorners[ 4 ], 1 );
		//occDbgBuff[ di ].minXCorner = vec4( boxCorners[ 0 ], 1 );
		//occDbgBuff[ di ].minYCorner = vec4( boxCorners[ 1 ], 1 );
		//occDbgBuff[ di ].maxXCorner = vec4( boxCorners[ 2 ], 1 );
		//occDbgBuff[ di ].maxYCorner = vec4( boxCorners[ 3 ], 1 );
		
		occDbgBuff[ di ].ndcMin = screenMin;
		occDbgBuff[ di ].ndcMax = screenMax;
		
		occDbgBuff[ di ].xPosBound = xPosBound;
		occDbgBuff[ di ].yPosBound = yPosBound;
		occDbgBuff[ di ].xNegBound = xNegBound;
		occDbgBuff[ di ].yNegBound = yNegBound;
		occDbgBuff[ di ].zNearBound = 1.0f / perspZ;
		
		occDbgBuff[ di ].mipLevel = mipLevel;
		
		occDbgBuff[ di ].depth = depth;
		
		//vec4 screenBoxCorners[ 4 ] = {
		//	vec4( minXY, 0, 1 ),
		//	vec4( minXY.x, maxXY.y, 0, 1 ),
		//	vec4( maxXY.x, minXY.y, 0, 1 ),
		//	vec4( maxXY, 0, 1 )
		//};
		//
		//screenspaceBoxBuff[ 8 * di + 0 ] = dbg_vertex( screenBoxCorners[ 0 ], vec4( 255, 0, 0, 1 ) );
		//screenspaceBoxBuff[ 8 * di + 1 ] = dbg_vertex( screenBoxCorners[ 1 ], vec4( 255, 0, 0, 1 ) );
		//					
		//screenspaceBoxBuff[ 8 * di + 2 ] = dbg_vertex( screenBoxCorners[ 1 ], vec4( 255, 0, 0, 1 ) );
		//screenspaceBoxBuff[ 8 * di + 3 ] = dbg_vertex( screenBoxCorners[ 3 ], vec4( 255, 0, 0, 1 ) );
		//					
		//screenspaceBoxBuff[ 8 * di + 4 ] = dbg_vertex( screenBoxCorners[ 3 ], vec4( 255, 0, 0, 1 ) );
		//screenspaceBoxBuff[ 8 * di + 5 ] = dbg_vertex( screenBoxCorners[ 2 ], vec4( 255, 0, 0, 1 ) );
		//					
		//screenspaceBoxBuff[ 8 * di + 6 ] = dbg_vertex( screenBoxCorners[ 2 ], vec4( 255, 0, 0, 1 ) );
		//screenspaceBoxBuff[ 8 * di + 7 ] = dbg_vertex( screenBoxCorners[ 0 ], vec4( 255, 0, 0, 1 ) );
	#endif	
	}

	// TODO: must compute LOD based on AABB
	float lodLevel = log2( max( 1, distance( center.xyz, cam.camPos ) - length( extent ) ) );
	uint lodIdx = clamp( uint( lodLevel ), 0, currentMesh.lodCount - 1 );
	mesh_lod lod = currentMesh.lods[ 0 ];

	// TODO: should pass meshletOffset + count too ?
	// TODO: go surfin'
	// TODO: add to dispatch cound directly
	if( visible )
	{
		uint mletIdxOffset = atomicAdd( mletDispatchCount, lod.meshletCount );
		for( uint i = 0; i < lod.meshletCount; ++i )
		{
			mletInstIdx[ mletIdxOffset + i ] = di;
		}
	}

#if WAVE_OPS
	uint mletsCount = subgroupAdd( visible ? lod.meshletCount : 0 );
	memoryBarrierShared();

	if( gl_LocalInvocationID.x == 0 ) meshletCullDispatchCounterLDS = 0;
	// TODO: should get lowest active ?
	if( gl_SubgroupInvocationID == 0 ) meshletCullDispatchCounterLDS += mletsCount;

	if( gl_LocalInvocationID.x == ( gl_WorkGroupSize.x - 1 ) ) {
		// TODO: / 256.0f just once in the last glob invoc ?
		//drawCallGrIdx = atomicAdd( drawCallCount, ceil( float( meshletCullDispatchCounterLDS ) / 256.0f ); );
	}
#endif

	if( visible )
	{
	#if !WAVE_OPS
		uint drawCallIdx = atomicAdd( drawCallCount, 1 );
	#endif

		drawCmd[ drawCallIdx ].drawIdx = di;
		drawCmd[ drawCallIdx ].indexCount = lod.indexCount;
		drawCmd[ drawCallIdx ].firstIndex = lod.indexOffset;
		drawCmd[ drawCallIdx ].vertexOffset = currentMesh.vertexOffset;
		drawCmd[ drawCallIdx ].instanceCount = 1;
		drawCmd[ drawCallIdx ].firstInstance = 0;

	#if GLSL_DBG
		//uint dbgDrawCallIdx = atomicAdd( dbgDrawCallCount, 1 );
		//// TODO: box vertex count const
		//dbgDrawCmd[ dbgDrawCallIdx ].drawIdx = di;
		//dbgDrawCmd[ dbgDrawCallIdx ].firstVertex = 0;
		//dbgDrawCmd[ dbgDrawCallIdx ].vertexCount = 36;
		//dbgDrawCmd[ dbgDrawCallIdx ].instanceCount = 1;
		//dbgDrawCmd[ dbgDrawCallIdx ].firstInstance = 0;
	#endif
	}

	if( di == 0 )
	{
		uint xDispatches = ( mletDispatchCount + gl_WorkGroupSize.x - 1 ) / gl_WorkGroupSize.x;
		dispatchCmd[ 0 ] = dispatch_command( xDispatches, 1, 1 );
	}
}