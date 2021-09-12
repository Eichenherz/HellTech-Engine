#version 460

#extension GL_GOOGLE_include_directive : require

// TODO: pass cam data diffrently
#define GLOBAL_RESOURCES
#include "..\r_data_structs.h"

layout( push_constant ) uniform block{
	uint64_t vtxAddr;
	uint64_t transfAddr;
};

layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer vtx_ref{
	vertex vertices[];
};
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer inst_desc_ref{
	instance_desc instDescs[];
};

void main()
{
	uint instId = uint( gl_VertexIndex & uint16_t( -1 ) );
	uint vertexId = uint( gl_VertexIndex >> 16 );

	//vertex vtx = vtx_ref( bdas.vtxAddr ).vertices[ vertexId ];
	//instance_desc thisInst = inst_desc_ref( bdas.instDescAddr ).instDescs[ instId ];

	// TODO: pos only
	// TODO: trans only
	vertex vtx = vtx_ref( vtxAddr ).vertices[ vertexId ];
	instance_desc thisInst = inst_desc_ref( transfAddr ).instDescs[ instId ];
	
	gl_Position = cam.proj * cam.mainView * thisInst.localToWorld * vec4( vtx.px, vtx.py, vtx.pz, 1.0f );
}