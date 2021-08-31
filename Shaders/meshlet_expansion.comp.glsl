#version 460

#extension GL_KHR_shader_subgroup_basic: require
#extension GL_KHR_shader_subgroup_arithmetic: require
#extension GL_KHR_shader_subgroup_ballot: require

#extension GL_GOOGLE_include_directive: require


#include "..\r_data_structs.h"

#extension GL_EXT_debug_printf : enable

struct inst_chunk
{
	uint instID;
	uint mletOffset;
	uint mletCount;
};
layout( binding = 0 ) readonly buffer visible_insts{
	inst_chunk visibleInstsChunks[];
};
layout( binding = 1 ) readonly buffer visible_insts_cnt{
	uint visibleInstsCount;
};

struct meshlet_id
{
	uint instID;
	uint meshletID;
};
layout( binding = 2 ) writeonly buffer meshlet_list{
	meshlet_id visibleMeshlets[];
};
layout( binding = 3 ) buffer meshlet_list_cnt{
	uint meshletCount;
};

const uint instsPerWorkgr = 4;

shared uint visibleMletsOffsetLDS = {};

layout( local_size_x = 128, local_size_y = 1, local_size_z = 1 ) in;
void main()
{
	uint workGrIdx = gl_WorkGroupID.x;
	
	// NOTE: inside workgr so we can sync
	if( gl_LocalInvocationID.x == 0 )
	{
		uint workgrAccumulator = 0;
		[[ unroll ]] 
		for( uint ii = 0; ii < instsPerWorkgr; ++ii )
		{
			uint instIdx = workGrIdx * instsPerWorkgr + ii;

			if( instIdx >= visibleInstsCount ) break;

			workgrAccumulator += visibleInstsChunks[ instIdx ].mletCount;
		}

		visibleMletsOffsetLDS = atomicAdd( meshletCount, workgrAccumulator );
	}
	barrier();
	groupMemoryBarrier();


	[[ unroll ]] 
	for( uint ii = 0; ii < instsPerWorkgr; ++ii )
	{
		uint instIdx = workGrIdx * instsPerWorkgr + ii;
	
		if( instIdx >= visibleInstsCount ) return;
	
		uint mletsIndexOffset = visibleInstsChunks[ instIdx ].mletOffset;
	
		uint mletsCount = visibleInstsChunks[ instIdx ].mletCount;
	
		//uint meshletIdx = gl_SubgroupID * gl_SubgroupSize + gl_SubgroupInvocationID;
		//if( meshletIdx < mletsCount )
		//{
		//	uint slotIdx = visibleMletsOffsetLDS + meshletIdx;
		//	visibleMeshlets[ slotIdx ] = meshlet_id( instIdx, mletsIndexOffset + meshletIdx );
		//}
	
		for( uint msi = 0; msi < mletsCount; msi += gl_SubgroupSize )
		{
			uint slotIdx = msi + gl_SubgroupInvocationID;
			// TODO: wavefront select ?
			if( slotIdx < mletsCount )
			{
				visibleMeshlets[ slotIdx + visibleMletsOffsetLDS ] = meshlet_id( instIdx, mletsIndexOffset + slotIdx );
			}
		}
		
		// TODO: atomicAdd ?
		if( gl_LocalInvocationID.x == 0 ) visibleMletsOffsetLDS += mletsCount;
		barrier();
		groupMemoryBarrier();
	}
}