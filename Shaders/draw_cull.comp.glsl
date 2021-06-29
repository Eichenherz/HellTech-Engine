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
layout( binding = 4 ) uniform sampler2D minQuadFormDepthPyramid;


#if GLSL_DBG

layout( binding = 5, scalar ) writeonly buffer dbg_draw_cmd{
	draw_indirect dbgDrawCmd[];
};
layout( binding = 6 ) buffer dbg_draw_cmd_count{
	uint dbgDrawCallCount;
};
#endif


// TODO: would a u64 be better here ?
shared uint meshletCullDispatchCounterLDS;

// NOTE: https://research.nvidia.com/publication/2d-polyhedral-bounds-clipped-perspective-projected-3d-sphere && niagara renderer by zeux
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

vec4 SignFlip( vec4 e )
{
	return vec4( ( e.x >= 0.0 ) ? 1.0 : -1.0, 
				 ( e.y >= 0.0 ) ? 1.0 : -1.0, 
				 ( e.z >= 0.0 ) ? 1.0 : -1.0,
				 ( e.w >= 0.0 ) ? 1.0 : -1.0 );
}

void main()
{
	uint di = gl_GlobalInvocationID.x;

	if( di == 0 ){
		drawCallCount = 0;
	#if GLSL_DBG
		dbgDrawCallCount = 0;
	#endif
	}

	if( di >= cullInfo.drawCallsCount ) return;

	//if( !OCCLUSION_CULLING && drawVisibility[ di ] == 0 ) return;

	instance_desc currentInst = inst_desc_ref( bdas.instDescAddr ).instDescs[ di ];
	mesh_desc currentMesh = mesh_desc_ref( bdas.meshDescAddr ).meshes[ currentInst.meshIdx ];

	// NOTE: transform to clip space
	// NOTE: full model transfrom because BB might not be centered
	vec4 center = vec4( currentInst.pos + RotateQuat( currentMesh.center * currentInst.scale, currentInst.rot ), 1 );
	vec4 extent = vec4( RotateQuat( currentMesh.extent * currentInst.scale, currentInst.rot ), 0 ); // dir
	//center = vec4( RotateQuat( center.xyz, cam.viewQuat ), 1 ) + cam.viewMove;
	//extent = vec4( RotateQuat( extent.xyz, cam.viewQuat ), 0 );
	center = cam.view * center;
	extent = cam.view * extent;
	center = cam.proj * center;
	extent = cam.proj * extent;

	bool visible = true;
	//bool visible = center.z - cullInfo.zNear > -extent.z;// && ( cullInfo.drawDistance - center.z > -extent.z );
	//visible = visible && center.z * cullData.frustum[ 1 ] - abs( center.x ) * cullData.frustum[ 0 ] > -radius;
	//visible = visible && center.z * cullData.frustum[ 3 ] - abs( center.y ) * cullData.frustum[ 2 ] > -radius;

	//visible = visible && dot( cullInfo.planes[ 0 ], center ) > -dot( abs( cullInfo.planes[ 0 ] ), extent );
	//visible = visible && dot( cullInfo.planes[ 1 ], center ) > -dot( abs( cullInfo.planes[ 1 ] ), extent );
	//visible = visible && dot( cullInfo.planes[ 2 ], center ) > -dot( abs( cullInfo.planes[ 2 ] ), extent );
	//visible = visible && dot( cullInfo.planes[ 3 ], center ) > -dot( abs( cullInfo.planes[ 3 ] ), extent );

	//visible = visible && ( dot( cullInfo.planes[ 0 ], extent * SignFlip( cullInfo.planes[ 0 ] ) + center ) > -cullInfo.planes[ 0 ].w );
	//visible = visible && ( dot( cullInfo.planes[ 1 ], extent * SignFlip( cullInfo.planes[ 1 ] ) + center ) > -cullInfo.planes[ 1 ].w );
	//visible = visible && ( dot( cullInfo.planes[ 2 ], extent * SignFlip( cullInfo.planes[ 2 ] ) + center ) > -cullInfo.planes[ 2 ].w );
	//visible = visible && ( dot( cullInfo.planes[ 3 ], extent * SignFlip( cullInfo.planes[ 3 ] ) + center ) > -cullInfo.planes[ 3 ].w );

	visible = visible && ( abs( center.x ) < extent.x + center.w );
	visible = visible && ( abs( center.y ) < extent.y + center.w );
	

	// DON'T use yet
	//if( visible && OCCLUSION_CULLING )
	//{
	//	vec3 viewSpaceCenter = ( cam.view * center ).xyz;
	//	if( viewSpaceCenter.z > radius + cullInfo.zNear )
	//	{
	//		vec4 aabb = ProjectedSphereToAABB( viewSpaceCenter, radius,
	//										   cullInfo.projWidth / cullInfo.zNear,
	//										   cullInfo.projHeight / cullInfo.zNear );
	//
	//		float width = abs( aabb.z - aabb.x ) * cullInfo.pyramidWidthPixels;
	//		float height = abs( aabb.w - aabb.y ) * cullInfo.pyramidHeightPixels;
	//		float mipLevel = floor( log2( max( width, height ) ) );
	//		// NOTE: sampler does clamping 
	//		float depth = textureLod( minQuadFormDepthPyramid, ( aabb.xy + aabb.zw ) * 0.5, mipLevel ).x;
	//		float closestDepthValOnSphere = cullInfo.zNear / ( viewSpaceCenter.z - radius );
	//
	//		visible = visible && ( closestDepthValOnSphere > depth );
	//	}
	//}

	float lodLevel = log2( max( 1, distance( center.xyz, cam.camPos ) - length( extent ) ) );
	uint lodIdx = clamp( uint( lodLevel ), 0, currentMesh.lodCount - 1 );
	mesh_lod lod = currentMesh.lods[ 0 ];

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

	//if( visible && ( !OCCLUSION_CULLING || drawVisibility[ di ] == 0 ) ){
	if( visible ){
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
		dbgDrawCmd[ dbgDrawCallIdx ].vertexCount = 24;
		dbgDrawCmd[ dbgDrawCallIdx ].instanceCount = 1;
		dbgDrawCmd[ dbgDrawCallIdx ].firstInstance = 0;
	#endif
	}

	//if( OCCLUSION_CULLING ) drawVisibility[ di ] = visible ? 1 : 0;
}