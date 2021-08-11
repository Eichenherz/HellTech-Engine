#version 460

#extension GL_KHR_shader_subgroup_basic: require
#extension GL_KHR_shader_subgroup_arithmetic: require
#extension GL_KHR_shader_subgroup_ballot: require
#extension GL_KHR_shader_subgroup_shuffle: require
// TODO: add to general ?
#extension GL_EXT_samplerless_texture_functions: require

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
layout( binding = 6 ) uniform texture2D depthPyramid;
layout( binding = 7 ) uniform sampler minQuadSampler;

#if GLSL_DBG

layout( binding = 8, scalar ) writeonly buffer dbg_draw_cmd{
	draw_indirect dbgDrawCmd[];
};
layout( binding = 9 ) buffer dbg_draw_cmd_count{
	uint dbgDrawCallCount;
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

	// TODO: 
	vec3 camToCenterLocalDist = ( inverse( currentInst.localToWorld ) * vec4( cam.camPos, 1.0f ) ).xyz - center;
	bool camInsideLocalAABB = all( greaterThanEqual( extent - abs( camToCenterLocalDist ), vec3( 0.0f ) ) );
	if( visible && !camInsideLocalAABB && OCCLUSION_CULLING )
	{
		mat4 mvp = transpose( transpMvp );

		vec2 ndcMin = vec2( 1.0f );
		vec2 ndcMax = vec2( 0.0f );
		float minDepth = 1.0f;

		// TODO: clamp ?
		[[ unroll ]] for( uint i = 0; i < 8; ++i )
		{
			vec4 clipCorner = mvp * vec4( mix( boxMax, boxMin, bvec3( i & 1, i & 2, i & 4 ) ), 1 );
			clipCorner /= clipCorner.w;

			vec2 uvCorner = clipCorner.xy * vec2( 0.5f, -0.5f ) + vec2( 0.5f );
			ndcMin = min( ndcMin, uvCorner );
			ndcMax = max( ndcMax, uvCorner );
			minDepth = min( minDepth, clipCorner.z );
		}
		
		vec2 boxSize = abs( ndcMax - ndcMin ) * textureSize( depthPyramid, 0 ).xy;
		
		float mipLevel = floor( log2( max( boxSize.x, boxSize.y ) ) );
		// NOTE: sampler does clamping 
		float depth = textureLod( sampler2D( depthPyramid, minQuadSampler ), ( ndcMax.xy + ndcMin.xy ) * 0.5f, mipLevel ).x;
		visible = visible && ( minDepth >= depth );
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
		uint dbgDrawCallIdx = atomicAdd( dbgDrawCallCount, 1 );
		// TODO: box vertex count const
		dbgDrawCmd[ dbgDrawCallIdx ].drawIdx = di;
		dbgDrawCmd[ dbgDrawCallIdx ].firstVertex = 0;
		dbgDrawCmd[ dbgDrawCallIdx ].vertexCount = 36;
		dbgDrawCmd[ dbgDrawCallIdx ].instanceCount = 1;
		dbgDrawCmd[ dbgDrawCallIdx ].firstInstance = 0;
	#endif
	}

	if( di == 0 )
	{
		uint xDispatches = ( mletDispatchCount + gl_WorkGroupSize.x - 1 ) / gl_WorkGroupSize.x;
		dispatchCmd[ 0 ] = dispatch_command( xDispatches, 1, 1 );
	}
}