#version 460

#extension GL_KHR_shader_subgroup_basic: require
#extension GL_KHR_shader_subgroup_arithmetic: require
#extension GL_KHR_shader_subgroup_ballot: require
#extension GL_KHR_shader_subgroup_vote: require

#extension GL_GOOGLE_include_directive: require

#include "..\r_data_structs.h"

layout( push_constant ) uniform block{
	uint64_t meshletDataAddr;
	uint64_t visMeshletsAddr;
	uint64_t visMeshletsCountAddr;
	uint64_t mergedIdxBuffAddr;
	uint64_t mergedIdxCountAddr;
	uint64_t drawCmdsAddr;
	uint64_t drawCmdCountAddr;
	uint64_t atomicWorkgrCounterAddr;
};

layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer mlet_data_ref{ 
	uint meshletData[]; 
};

struct meshlet_info
{
	uint dataOffset;
	uint16_t instId;
	uint8_t vtxCount;
	uint8_t triCount;
};
layout( buffer_reference, buffer_reference_align = 4 ) readonly buffer mlet_info_ref{ 
	meshlet_info visibleMeshlets[];
};
layout( buffer_reference, buffer_reference_align = 4 ) readonly buffer u32_ref{ 
	uint visibleMeshletsCount;
};
layout( buffer_reference, buffer_reference_align = 4 ) writeonly buffer index_id_ref{
	uint mergedIdxBuff[];
};
layout( buffer_reference, buffer_reference_align = 4 ) coherent buffer coherent_counter_ref{
	uint coherentCounter;
};
layout( buffer_reference, buffer_reference_align = 4 ) writeonly buffer draw_cmd_ref{
	draw_command drawCmd;
};


const uint meshletsPerWorkgr = 32;

shared uint idxBuffOffsetLDS = {};
shared uint workgrAtomicCounterShared = {};

// TODO: increase size ?
layout( local_size_x = 32, local_size_y = 1, local_size_z = 1 ) in;
void main()
{
	uint workGrIdx = gl_WorkGroupID.x;
	
	if( gl_LocalInvocationID.x == 0 )
	{
		uint perWorkgrCount = 0;
		for( uint mi = 0; mi < meshletsPerWorkgr; ++mi )
		{
			uint meshletIdx = workGrIdx * meshletsPerWorkgr + mi;

			if( meshletIdx >= u32_ref( visMeshletsCountAddr ).visibleMeshletsCount ) break;

			perWorkgrCount += uint( mlet_info_ref( visMeshletsAddr ).visibleMeshlets[ meshletIdx ].triCount * 3 );
		}

		idxBuffOffsetLDS = atomicAdd( coherent_counter_ref( mergedIdxCountAddr ).coherentCounter, perWorkgrCount );
	}


	barrier();
	memoryBarrier();
	for( uint mi = 0; mi < meshletsPerWorkgr; ++mi )
	{
		uint meshletIdx;
		if( subgroupElect() )
		{
			meshletIdx = workGrIdx * meshletsPerWorkgr + mi;
		}
		
		meshletIdx = subgroupBroadcastFirst( meshletIdx );

		if( meshletIdx >= u32_ref( visMeshletsCountAddr ).visibleMeshletsCount ) break;

		uint16_t parentInstId = mlet_info_ref( visMeshletsAddr ).visibleMeshlets[ meshletIdx ].instId;
		uint dataOffset = mlet_info_ref( visMeshletsAddr ).visibleMeshlets[ meshletIdx ].dataOffset;
		uint vtxOffset = dataOffset;
		uint idxOffset = vtxOffset + uint( mlet_info_ref( visMeshletsAddr ).visibleMeshlets[ meshletIdx ].vtxCount );
		// NOTE: want all the indices
		uint thisIdxCount = uint( mlet_info_ref( visMeshletsAddr ).visibleMeshlets[ meshletIdx ].triCount * 3 );
		 
		for( uint i = 0; i < thisIdxCount; i += gl_WorkGroupSize.x )
		{
			uint slotIdx = i + gl_LocalInvocationID.x; 

			// TODO:
			//bool execMask = subgroupAll( slotIdx < thisIdxCount );

			if( slotIdx < thisIdxCount )
			{
				uint idxGroupBin = slotIdx / 4;
				uint meshletIdxGroup = mlet_data_ref( meshletDataAddr ).meshletData[ idxOffset + idxGroupBin ];

				uint idx = ( meshletIdxGroup >> ( ( slotIdx % 4 ) * 8 ) ) & 0xff;

				uint vertexId = mlet_data_ref( meshletDataAddr ).meshletData[ vtxOffset + idx ];
				index_id_ref( mergedIdxBuffAddr ).mergedIdxBuff[ slotIdx + idxBuffOffsetLDS ] = 
					uint( parentInstId ) | ( uint( vertexId ) << 16 );
			}
		}
		
		barrier();
		groupMemoryBarrier();
		if( gl_LocalInvocationID.x == 0 ) idxBuffOffsetLDS += thisIdxCount;	
	}

	//if( gl_LocalInvocationID.x == 0 ) workgrAtomicCounterShared = atomicAdd( workgrAtomicCounter, 1 );
	if( gl_LocalInvocationID.x == 0 ) 
		workgrAtomicCounterShared = atomicAdd( coherent_counter_ref( atomicWorkgrCounterAddr ).coherentCounter, 1 );

	barrier();
	memoryBarrier();
	if( ( gl_LocalInvocationID.x == 0 ) && ( workgrAtomicCounterShared == gl_NumWorkGroups.x - 1 ) )
	{
		draw_cmd_ref( drawCmdsAddr ).drawCmd.drawIdx = -1; // Don't use
		// TODO: atomicAdd 0 here ?
		draw_cmd_ref( drawCmdsAddr ).drawCmd.indexCount = coherent_counter_ref( mergedIdxCountAddr ).coherentCounter; 
		draw_cmd_ref( drawCmdsAddr ).drawCmd.instanceCount = 1;
		draw_cmd_ref( drawCmdsAddr ).drawCmd.firstIndex = 0;
		draw_cmd_ref( drawCmdsAddr ).drawCmd.vertexOffset = 0; // Pass some offset ?
		draw_cmd_ref( drawCmdsAddr ).drawCmd.firstInstance = 0;

		coherent_counter_ref( drawCmdCountAddr ).coherentCounter = 1;

		coherent_counter_ref( atomicWorkgrCounterAddr ).coherentCounter = 0;
	}
}