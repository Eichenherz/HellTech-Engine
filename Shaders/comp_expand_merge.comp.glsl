#version 460


#extension GL_GOOGLE_include_directive: require

#include "..\r_data_structs.h"

layout( push_constant ) uniform block{
	uint64_t meshletTriAddr;
	uint64_t meshletVtxAddr;
};

layout( buffer_reference, scalar, buffer_reference_align = 1 ) readonly buffer mlet_tri_ref{ 
	uint8_t meshletTriangles[]; 
};
layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer mlet_vert_ref{ 
	uint meshletVertexIds[]; 
};

struct meshlet_info
{
	uint triOffset;
	uint vtxOffset;
	uint16_t instId;
	uint16_t idxCount;
};
layout( binding = 0 ) readonly buffer visible_meshlets{
	meshlet_info visibleMeshlets[];
};
layout( binding = 1 ) readonly buffer visible_meshlets_cnt{
	uint visibleMeshletsCount;
};
layout( binding = 2 ) writeonly buffer index_ids{
	uint mergedIdxBuff[];
};
layout( binding = 3 ) coherent buffer vis_idx_count{
	uint mergedIdxCount;
};
layout( binding = 4, scalar ) buffer draw_cmd{
	draw_command drawCmd;
};
layout( binding = 5 ) buffer draw_cmd_count{
	uint drawCmdCount;
};

layout( binding = 6 ) coherent buffer atomic_cnt{
	uint workgrAtomicCounter;
};


const uint meshletsPerWorkgr = 8;

shared uint idxBuffOffsetLDS = {};
shared uint workgrAtomicCounterShared = {};

// TODO: balace out meshlets sizes etc
layout( local_size_x = 256, local_size_y = 1, local_size_z = 1 ) in;
void main()
{
	uint workGrIdx = gl_WorkGroupID.x;
	
	// NOTE: inside workgr so we can order invocations
	if( gl_LocalInvocationID.x == 0 )
	{
		uint perWorkgrCount = 0;
		[[ unroll ]] 
		for( uint mi = 0; mi < meshletsPerWorkgr; ++mi )
		{
			uint meshletIdx = workGrIdx * meshletsPerWorkgr + mi;

			if( meshletIdx >= visibleMeshletsCount ) break;

			perWorkgrCount += uint( visibleMeshlets[ meshletIdx ].idxCount );
		}

		idxBuffOffsetLDS = atomicAdd( mergedIdxCount, perWorkgrCount );
	}


	barrier();
	memoryBarrier();
	[[ unroll ]] 
	for( uint mi = 0; mi < meshletsPerWorkgr; ++mi )
	{
		uint meshletIdx = workGrIdx * meshletsPerWorkgr + mi;
	
		if( meshletIdx >= visibleMeshletsCount ) break;
	
		uint16_t parentInstId = visibleMeshlets[ meshletIdx ].instId;
		uint tirangleOffset = visibleMeshlets[ meshletIdx ].triOffset;
		uint vertexOffset = visibleMeshlets[ meshletIdx ].vtxOffset;
		uint thisIdxCount = uint( visibleMeshlets[ meshletIdx ].idxCount );
		
		for( uint i = 0; i < thisIdxCount; i += gl_WorkGroupSize.x )
		{
			uint slotIdx = i + gl_LocalInvocationID.x;
			// TODO: wavefront select ?
			if( slotIdx < thisIdxCount )
			{
				uint8_t tri = mlet_tri_ref( meshletTriAddr ).meshletTriangles[ tirangleOffset + slotIdx ];
				uint vertexId = mlet_vert_ref( meshletVtxAddr ).meshletVertexIds[ uint( tri ) + vertexOffset ];
				mergedIdxBuff[ slotIdx + idxBuffOffsetLDS ] = uint( parentInstId ) | ( uint( vertexId ) << 16 );
			}
		}
		
		barrier();
		groupMemoryBarrier();
		if( gl_LocalInvocationID.x == 0 ) idxBuffOffsetLDS += thisIdxCount;
		
	}

	if( gl_LocalInvocationID.x == 0 ) workgrAtomicCounterShared = atomicAdd( workgrAtomicCounter, 1 );

	barrier();
	memoryBarrier();
	if( ( gl_LocalInvocationID.x == 0 ) && ( workgrAtomicCounterShared == gl_NumWorkGroups.x - 1 ) )
	{
		drawCmd.drawIdx = -1; // Don't use
		drawCmd.indexCount = mergedIdxCount; // TODO: atomicAdd 0 here ?
		drawCmd.instanceCount = 1;
		drawCmd.firstIndex = 0;
		drawCmd.vertexOffset = 0; // Pass some offset ?
		drawCmd.firstInstance = 0;

		drawCmdCount = 1;

		workgrAtomicCounter = 0;
	}
}