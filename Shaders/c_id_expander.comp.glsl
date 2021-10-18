#version 460

#extension GL_KHR_shader_subgroup_basic: require
#extension GL_KHR_shader_subgroup_arithmetic: require
#extension GL_KHR_shader_subgroup_ballot: require

#extension GL_GOOGLE_include_directive: require

#include "..\r_data_structs.h"

#extension GL_EXT_debug_printf : enable

// TODO: rename stuff
layout( push_constant ) uniform block{
	uint64_t	visInstAddr;
	uint64_t	visInstCountAddr;
	uint64_t    expandeeAddr;
	uint64_t    expandeeCountAddr;
	uint64_t    atomicWorkgrCounterAddr;
	uint64_t    dispatchCmdAddr;
};

// TODO: try pack into uint64_t
// TODO: must have vtx offset too
struct expandee_info
{
	uint instId;
	uint expOffset;
	uint expCount;
};

layout( buffer_reference, buffer_reference_align = 4 ) readonly buffer vis_inst_ref{
	expandee_info visibleInsts[];
};
layout( buffer_reference, buffer_reference_align = 8 ) writeonly buffer expandee_ref{
	uint64_t expandeeIdBuff[];
};
layout( buffer_reference, buffer_reference_align = 4 ) readonly buffer uint_ref{
	uint count;
};
layout( buffer_reference, buffer_reference_align = 4 ) coherent buffer coherent_counter_ref{
	uint coherentCounter;
};
layout( buffer_reference, buffer_reference_align = 4 ) writeonly buffer dispatch_indirect_ref{
	dispatch_command dispatchCmd;
};


//layout( binding = 0 ) readonly buffer visible_clusters{
//	expandee_info visibleClusters[];
//};
//layout( binding = 1 ) readonly buffer visible_clusters_cnt{
//	uint visibleClustersCount;
//};
//layout( binding = 2 ) writeonly buffer expandee_list{
//	uint64_t expandeeIdBuff[];
//};
//layout( binding = 3 ) coherent buffer expandee_list_cnt{
//	uint expandeeCount;
//};
//
//layout( binding = 4 ) coherent buffer atomic_cnt{
//	uint workgrAtomicCounter;
//};
//
//layout( binding = 5 ) buffer disptach_indirect{
//	dispatch_command dispatchCmd;
//};

// TODO: push const or spec const ?
const uint clustersPerWorkgr = 4;
const uint destWorkgrSize = 32;

shared uint expandeeOffsetLDS = {};
shared uint workgrAtomicCounterShared = {};


layout( local_size_x = 32, local_size_y = 1, local_size_z = 1 ) in;
void main()
{
	uint workGrIdx = gl_WorkGroupID.x;
	
	if( gl_LocalInvocationID.x == 0 )
	{
		uint perWorkgrExpCount = 0;
		for( uint ii = 0; ii < clustersPerWorkgr; ++ii )
		{
			uint clusterIdx = workGrIdx * clustersPerWorkgr + ii;

			//if( clusterIdx >= visibleClustersCount ) break;
			if( clusterIdx >= uint_ref( visInstCountAddr ).count ) break;

			//perWorkgrExpCount += visibleClusters[ clusterIdx ].expCount;
			perWorkgrExpCount += vis_inst_ref( visInstAddr ).visibleInsts[ clusterIdx ].expCount;
		}

		//expandeeOffsetLDS = atomicAdd( expandeeCount, perWorkgrExpCount );
		expandeeOffsetLDS = atomicAdd( coherent_counter_ref( expandeeCountAddr ).coherentCounter, perWorkgrExpCount );
	}


	barrier();
	memoryBarrier();
	for( uint ii = 0; ii < clustersPerWorkgr; ++ii )
	{
		uint clusterIdx;
		if( subgroupElect() )
		{
			clusterIdx = workGrIdx * clustersPerWorkgr + ii;
		}
		
		clusterIdx = subgroupBroadcastFirst( clusterIdx );

		//if( clusterIdx >= visibleClustersCount ) break;
		if( clusterIdx >= uint_ref( visInstCountAddr ).count ) break;
		
		//uint parentInstId = visibleClusters[ clusterIdx ].instId;
		//uint expandeeIdxOffset = visibleClusters[ clusterIdx ].expOffset;
		//uint thisExpandeeCount = visibleClusters[ clusterIdx ].expCount;

		uint parentInstId = vis_inst_ref( visInstAddr ).visibleInsts[ clusterIdx ].instId;
		uint expandeeIdxOffset = vis_inst_ref( visInstAddr ).visibleInsts[ clusterIdx ].expOffset;
		uint thisExpandeeCount = vis_inst_ref( visInstAddr ).visibleInsts[ clusterIdx ].expCount;

		for( uint msi = 0; msi < thisExpandeeCount; msi += gl_WorkGroupSize.x )
		{
			uint slotIdx = msi + gl_LocalInvocationID.x;
			// TODO: wavefront select ?
			if( slotIdx < thisExpandeeCount )
			{
				expandee_ref( expandeeAddr ).expandeeIdBuff[ slotIdx + expandeeOffsetLDS ] = 
						uint64_t( parentInstId ) | ( uint64_t( expandeeIdxOffset + slotIdx ) << 32 );
			}
		}
		
		barrier();
		groupMemoryBarrier();
		if( gl_LocalInvocationID.x == 0 ) expandeeOffsetLDS += thisExpandeeCount;
		
	}

	//if( gl_LocalInvocationID.x == 0 ) workgrAtomicCounterShared = atomicAdd( workgrAtomicCounter, 1 );
	if( gl_LocalInvocationID.x == 0 ) 
		workgrAtomicCounterShared = atomicAdd( coherent_counter_ref( atomicWorkgrCounterAddr ).coherentCounter, 1 );

	barrier();
	memoryBarrier();
	if( ( gl_LocalInvocationID.x == 0 ) && ( workgrAtomicCounterShared == gl_NumWorkGroups.x - 1 ) )
	{
		//uint expandeeCullDispatch = ( expandeeCount + destWorkgrSize - 1 ) / destWorkgrSize;
		uint expandeeCullDispatch = ( coherent_counter_ref( expandeeCountAddr ).coherentCounter + destWorkgrSize - 1 ) / destWorkgrSize;

		//dispatchCmd = dispatch_command( expandeeCullDispatch, 1, 1 );
		dispatch_indirect_ref( dispatchCmdAddr ).dispatchCmd = dispatch_command( expandeeCullDispatch, 1, 1 );
		
		//workgrAtomicCounter = 0;
		coherent_counter_ref( atomicWorkgrCounterAddr ).coherentCounter = 0;
	}
}