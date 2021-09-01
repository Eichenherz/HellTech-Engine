#version 460

#extension GL_GOOGLE_include_directive: require

#define GLOBAL_RESOURCES

#include "..\r_data_structs.h"


layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer meshlet_desc_ref{ 
	meshlet meshlets[]; 
};
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer inst_desc_ref{
	instance_desc instDescs[];
};

// TODO: rename
struct meshlet_id
{
	uint instID;
	uint meshletID;
};
layout( binding = 0 ) readonly buffer meshlet_list{
	meshlet_id visibleMeshlets[];
};
layout( binding = 1 ) readonly buffer meshlet_list_cnt{
	uint totalMeshletCount;
};
layout( binding = 2, scalar ) writeonly buffer dbg_draw_cmd{
	draw_command dbgDrawCmd[];
};
layout( binding = 3 ) buffer dbg_draw_cmd_count{
	uint dbgDrawCallCount;
};

layout( local_size_x = 256, local_size_y = 1, local_size_z = 1 ) in;
void main()
{
	uint globalIdx = gl_GlobalInvocationID.x;

	if( globalIdx >= totalMeshletCount ) return;

	meshlet_id mid = visibleMeshlets[ globalIdx ];

	instance_desc currentInst = inst_desc_ref( bdas.instDescAddr ).instDescs[ mid.instID ];
	meshlet currentMeshlet = meshlet_desc_ref( bdas.meshletsAddr ).meshlets[ mid.meshletID ];

	uint slotIdx = atomicAdd( dbgDrawCallCount, 1 );
	dbgDrawCmd[ slotIdx ].drawIdx = mid.instID;
	dbgDrawCmd[ slotIdx ].indexCount = uint( currentMeshlet.triangleCount ) * 3;
	dbgDrawCmd[ slotIdx ].instanceCount = 1;
	dbgDrawCmd[ slotIdx ].firstIndex = currentMeshlet.triBufOffset;
	dbgDrawCmd[ slotIdx ].vertexOffset = currentMeshlet.vtxBufOffset;
	dbgDrawCmd[ slotIdx ].firstInstance = 0;
}