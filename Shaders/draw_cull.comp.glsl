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


layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer mesh_ref{ 
	mesh meshes[]; 
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

layout( binding = 5 ) writeonly buffer dbg_draw_cmd{
	draw_command dbgDrawCmd[];
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

// TODO: 
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

	if( !OCCLUSION_CULLING && drawVisibility[ di ] == 0 ) return;

	instance_desc currentInst = inst_desc_ref( bdas.instDescAddr ).instDescs[ di ];
	mesh currentMesh = mesh_ref( bdas.meshDescAddr ).meshes[ currentInst.meshIdx ];

	// TODO: fix culling 
	vec4 center = vec4( currentInst.pos + RotateQuat( currentMesh.center * currentInst.scale, currentInst.rot ), 1 );
	float radius = currentMesh.radius * currentInst.scale;
	
	// TODO: move to view space ?
	bool visible = center.z - cullInfo.zNear > -radius;// && ( cullInfo.drawDistance - center.z > -radius );
	visible = visible && dot( cullInfo.planes[ 0 ], center ) > -radius;
	visible = visible && dot( cullInfo.planes[ 1 ], center ) > -radius;
	visible = visible && dot( cullInfo.planes[ 2 ], center ) > -radius;
	visible = visible && dot( cullInfo.planes[ 3 ], center ) > -radius;

	if( visible && OCCLUSION_CULLING ){
		
		vec3 viewSpaceCenter = ( cam.view * center ).xyz;
		if( viewSpaceCenter.z > radius + cullInfo.zNear ){
			vec4 aabb = ProjectedSphereToAABB( viewSpaceCenter, radius,
											   cullInfo.projWidth / cullInfo.zNear,
											   cullInfo.projHeight / cullInfo.zNear );

			float width = abs( aabb.z - aabb.x ) * cullInfo.pyramidWidthPixels;
			float height = abs( aabb.w - aabb.y ) * cullInfo.pyramidHeightPixels;
			float mipLevel = floor( log2( max( width, height ) ) );
			// NOTE: sampler does clamping 
			float depth = textureLod( minQuadFormDepthPyramid, ( aabb.xy + aabb.zw ) * 0.5, mipLevel ).x;
			float closestDepthValOnSphere = cullInfo.zNear / ( viewSpaceCenter.z - radius );

			visible = visible && ( closestDepthValOnSphere > depth );
		}
	}

	float lodLevel = log2( max( 1, distance( center.xyz, cam.camPos ) - radius ) );
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

	if( visible && ( !OCCLUSION_CULLING || drawVisibility[ di ] == 0 ) ){
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
		//mesh bndVolMesh = mesh_ref( bdas.meshDescAddr ).meshes[ currentDraw.bndVolMeshIdx ];
		//
		//uint dbgDrawCallIdx = atomicAdd( dbgDrawCallCount, 1 );
		//
		//dbgDrawCmd[ dbgDrawCallIdx ].drawIdx = di;
		//dbgDrawCmd[ dbgDrawCallIdx ].indexCount = bndVolMesh.lods[ 0 ].indexCount;
		//dbgDrawCmd[ dbgDrawCallIdx ].firstIndex = bndVolMesh.lods[ 0 ].indexOffset;
		//dbgDrawCmd[ dbgDrawCallIdx ].vertexOffset = bndVolMesh.vertexOffset;
		//dbgDrawCmd[ dbgDrawCallIdx ].instanceCount = 1;
		//dbgDrawCmd[ dbgDrawCallIdx ].firstInstance = 0;
	#endif
	}

	if( OCCLUSION_CULLING ) drawVisibility[ di ] = visible ? 1 : 0;
}