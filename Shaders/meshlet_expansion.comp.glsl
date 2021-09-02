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
layout( binding = 2 ) writeonly buffer meshlet_list{
	uint64_t meshletIdBuff[];
};
layout( binding = 3 ) coherent buffer meshlet_list_cnt{
	uint meshletCount;
};

layout( binding = 4 ) coherent buffer atomic_cnt{
	uint workgrAtomicCounter;
};

layout( binding = 5 ) buffer disptach_indirect{
	dispatch_command dispatchCmd;
};

const uint instsPerWorkgr = 4;

shared uint visibleMletsOffsetLDS = {};
shared uint workgrAtomicCounterShared = {};


layout( local_size_x = 128, local_size_y = 1, local_size_z = 1 ) in;
void main()
{
	uint workGrIdx = gl_WorkGroupID.x;
	
	// NOTE: inside workgr so we can order invocations
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
	
		if( instIdx >= visibleInstsCount ) break;
	
		uint mletsIndexOffset = visibleInstsChunks[ instIdx ].mletOffset;
	
		uint mletsCount = visibleInstsChunks[ instIdx ].mletCount;
		
		for( uint msi = 0; msi < mletsCount; msi += gl_WorkGroupSize.x )
		{
			uint slotIdx = msi + gl_LocalInvocationID.x;
			// TODO: wavefront select ?
			if( slotIdx < mletsCount )
			{
				meshletIdBuff[ slotIdx + visibleMletsOffsetLDS ] = uint64_t( instIdx ) | ( uint64_t( mletsIndexOffset + slotIdx ) << 32 );
			}
		}
		
		// TODO: atomicAdd ?
		if( gl_LocalInvocationID.x == 0 ) visibleMletsOffsetLDS += mletsCount;
		barrier();
		groupMemoryBarrier();
	}

	if( gl_LocalInvocationID.x == 0 ) workgrAtomicCounterShared = atomicAdd( workgrAtomicCounter, 1 );
	barrier();

	if( ( gl_LocalInvocationID.x == 0 ) && ( workgrAtomicCounterShared == gl_NumWorkGroups.x - 1 ) )
	{
		// TODO: pass as spec consts or push consts ? 
		uint mletsCullDispatch = ( meshletCount + 255 ) / 256;
		dispatchCmd = dispatch_command( mletsCullDispatch, 1, 1 );
		// NOTE: reset atomicCounter
		workgrAtomicCounter = 0;
	}
}