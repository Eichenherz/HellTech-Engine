#version 460


#extension GL_KHR_shader_subgroup_basic: require
#extension GL_KHR_shader_subgroup_arithmetic: require
#extension GL_KHR_shader_subgroup_ballot: require


#extension GL_GOOGLE_include_directive: require

#define GLOBAL_RESOURCES

#include "..\r_data_structs.h"


layout( buffer_reference, scalar, buffer_reference_align = 1 ) readonly buffer mlet_tri_ref{ 
	uint8_t meshletTriangles[]; 
};
layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer mlet_vert_ref{ 
	uint meshletVertIdx[]; 
};
layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer vtx_ref{
	vertex vertices[];
};
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer inst_desc_ref{
	instance_desc instDescs[];
};

// TODO: rename this stuff ?
layout( binding = 0 ) readonly buffer triangle_list{
	uint64_t triangleIdBuff[];
};
layout( binding = 1 ) readonly buffer triangle_list_cnt{
	uint totalTriangleCount;
};
layout( binding = 2 ) buffer vis_idx_count{
	uint visibleIdxCount;
};
layout( binding = 3 ) writeonly buffer index_ids{
	uint indexIds[];
};
layout( binding = 4, scalar ) buffer draw_cmd{
	draw_command drawCmd[];
};
layout( binding = 5 ) buffer draw_cmd_count{
	uint drawCallCount;
};
layout( binding = 6 ) coherent buffer atomic_cnt{
	uint workgrAtomicCounter;
};

shared uint workgrAtomicCounterShared = {};

// TODO: unpacking of data ?
// TODO: need to take care of meshlet vertex buffer offset
// NOTE: inspired by "Optimizing gfx with comp GDC"
layout( local_size_x = 256, local_size_y = 1, local_size_z = 1 ) in;
void main()
{
	uint globalIdx = gl_GlobalInvocationID.x;

	if( globalIdx >= totalTriangleCount ) return;

	uint64_t mid = triangleIdBuff[ globalIdx ];
	uint parentInstId = uint( mid & uint( -1 ) );
	uint triangleIdx = uint( mid >> 32 );

	instance_desc currentInst = inst_desc_ref( bdas.instDescAddr ).instDescs[ parentInstId ];
	
	mat4 worldViewProj = cam.proj * cam.mainView * currentInst.localToWorld;

	// TODO: branch return ? if !visible return ?

	// NOTE: a triangle data
	uint indices[ 3 ] = {};
	vec4 vertices[ 3 ] = {};

	[[ unroll ]]
	for( uint i = 0; i < 3; ++i )
	{
		uint8_t tri = mlet_tri_ref( bdas.meshletsTriAddr ).meshletTriangles[ triangleIdx + i ];
		uint idx = mlet_vert_ref( bdas.meshletsVtxAddr ).meshletVertIdx[ uint( tri ) ];
		vertex v = vtx_ref( bdas.vtxAddr ).vertices[ idx ];

		indices[ i ] = idx;
		vertices[ i ] = worldViewProj * vec4( v.px, v.py, v.pz, 1 );
	}
	

	bool visible = true;
	// TODO: do we have degenerate tris ?
	// NOTE: backface culling in homogeneous coords
	visible = visible && determinant( mat3( vertices[0].xyw, vertices[1].xyw, vertices[2].xyw ) ) <= 0.0f;

	// TODO: near plane clipping ?


	// NOTE: to NDC
	vertices[ 0 ] /= vertices[ 0 ].w;
	vertices[ 1 ] /= vertices[ 1 ].w;
	vertices[ 2 ] /= vertices[ 2 ].w;
	
	vec2 minNdc = min( vertices[0].xy, min( vertices[1].xy, vertices[2].xy ) );
	vec2 maxNdc = max( vertices[0].xy, max( vertices[1].xy, vertices[2].xy ) );

	// TODO: recheck condition
	visible = visible && !any( equal( roundEven( minNdc ), roundEven( maxNdc ) ) );

	// NOTE: frustum culling in 0,1 range
	vertices[ 0 ] = vertices[ 0 ] * 0.5f + 0.5f;
	vertices[ 1 ] = vertices[ 1 ] * 0.5f + 0.5f;
	vertices[ 2 ] = vertices[ 2 ] * 0.5f + 0.5f;

	vec2 min01 = min( vertices[0].xy, min( vertices[1].xy, vertices[2].xy ) );
	vec2 max01 = max( vertices[0].xy, max( vertices[1].xy, vertices[2].xy ) );

	visible = visible && !any( greaterThan( min01, vec2( 1.0f ) ) ) && !any( lessThan( max01, vec2( 0.0f ) ) );

	uvec4 ballotVisible = subgroupBallot( visible );
	uint activeWaveThreadCount = subgroupBallotBitCount( ballotVisible );
	
	if( activeWaveThreadCount == 0 ) return;
	// TODO: shared atomics + global atomics ?
	uint subgrSlotOffset = ( gl_SubgroupInvocationID == 0 ) ? atomicAdd( visibleIdxCount, activeWaveThreadCount * 3 ) : 0;
	
	uint visibleTriangleIdx = subgroupBallotExclusiveBitCount( ballotVisible );
	uint outSlotId = subgroupBroadcastFirst( subgrSlotOffset  ) + visibleTriangleIdx;
	
	if( visible )
	{
		indexIds[ outSlotId + 0 ] = uint( parentInstId ) | ( uint( indices[0] ) << 16 );
		indexIds[ outSlotId + 1 ] = uint( parentInstId ) | ( uint( indices[1] ) << 16 );
		indexIds[ outSlotId + 2 ] = uint( parentInstId ) | ( uint( indices[2] ) << 16 );
	}

	if( gl_LocalInvocationID.x == 0 ) workgrAtomicCounterShared = atomicAdd( workgrAtomicCounter, 1 );
	barrier();

	if( ( gl_LocalInvocationID.x == 0 ) && ( workgrAtomicCounterShared == gl_NumWorkGroups.x - 1 ) )
	{
		drawCmd[ 0 ].drawIdx = -1; // Don't use
		drawCmd[ 0 ].indexCount = visibleIdxCount;
		drawCmd[ 0 ].instanceCount = 1;
		drawCmd[ 0 ].firstIndex = 0;
		drawCmd[ 0 ].vertexOffset = 0; // Pass some offset ?
		drawCmd[ 0 ].firstInstance = 0;
		drawCallCount = 1;

		workgrAtomicCounter = 0;
	}
}