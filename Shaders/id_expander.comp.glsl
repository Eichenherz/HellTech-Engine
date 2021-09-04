#version 460

#extension GL_GOOGLE_include_directive: require

#include "..\r_data_structs.h"

#extension GL_EXT_debug_printf : enable

// TODO: try pack into uint64_t
struct expandee_info
{
	uint instId;
	uint expOffset;
	uint expCount;
};
layout( binding = 0 ) readonly buffer visible_clusters{
	expandee_info visibleClusters[];
};
layout( binding = 1 ) readonly buffer visible_clusters_cnt{
	uint visibleClustersCount;
};
layout( binding = 2 ) writeonly buffer expandee_list{
	uint64_t expandeeIdBuff[];
};
layout( binding = 3 ) coherent buffer expandee_list_cnt{
	uint expandeeCount;
};

layout( binding = 4 ) coherent buffer atomic_cnt{
	uint workgrAtomicCounter;
};

layout( binding = 5 ) buffer disptach_indirect{
	dispatch_command dispatchCmd;
};

// TODO: push const or spec const ?
const uint clustersPerWorkgr = 4;
const uint destWorkgrSize = 256;

shared uint expandeeOffsetLDS = {};
shared uint workgrAtomicCounterShared = {};


layout( local_size_x = 32, local_size_y = 1, local_size_z = 1 ) in;
void main()
{
	uint workGrIdx = gl_WorkGroupID.x;
	
	// NOTE: inside workgr so we can order invocations
	if( gl_LocalInvocationID.x == 0 )
	{
		uint perWorkgrExpCount = 0;
		[[ unroll ]] 
		for( uint ii = 0; ii < clustersPerWorkgr; ++ii )
		{
			uint clusterIdx = workGrIdx * clustersPerWorkgr + ii;

			if( clusterIdx >= visibleClustersCount ) break;

			perWorkgrExpCount += visibleClusters[ clusterIdx ].expCount;
		}

		expandeeOffsetLDS = atomicAdd( expandeeCount, perWorkgrExpCount );
	}
	barrier();
	memoryBarrier();


	[[ unroll ]] 
	for( uint ii = 0; ii < clustersPerWorkgr; ++ii )
	{
		uint clusterIdx = workGrIdx * clustersPerWorkgr + ii;
	
		if( clusterIdx >= visibleClustersCount ) break;
		
		uint parentInstId = visibleClusters[ clusterIdx ].instId;
		uint expandeeIdxOffset = visibleClusters[ clusterIdx ].expOffset;
		uint thisExpandeeCount = visibleClusters[ clusterIdx ].expCount;
		
		for( uint msi = 0; msi < thisExpandeeCount; msi += gl_WorkGroupSize.x )
		{
			uint slotIdx = msi + gl_LocalInvocationID.x;
			// TODO: wavefront select ?
			if( slotIdx < thisExpandeeCount )
			{
				expandeeIdBuff[ slotIdx + expandeeOffsetLDS ] = 
						uint64_t( parentInstId ) | ( uint64_t( expandeeIdxOffset + slotIdx ) << 32 );
			}
		}
		
		// TODO: atomicAdd ?
		if( gl_LocalInvocationID.x == 0 ) expandeeOffsetLDS += thisExpandeeCount;
		barrier();
		groupMemoryBarrier();
	}

	if( gl_LocalInvocationID.x == 0 ) workgrAtomicCounterShared = atomicAdd( workgrAtomicCounter, 1 );
	barrier();
	memoryBarrier();


	if( ( gl_LocalInvocationID.x == 0 ) && ( workgrAtomicCounterShared == gl_NumWorkGroups.x - 1 ) )
	{
		uint expandeeCullDispatch = ( expandeeCount + destWorkgrSize - 1 ) / destWorkgrSize;
		dispatchCmd = dispatch_command( expandeeCullDispatch, 1, 1 );
		// NOTE: reset atomicCounter
		workgrAtomicCounter = 0;
	}
}