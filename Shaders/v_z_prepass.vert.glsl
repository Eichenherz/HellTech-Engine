#version 460

#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive : require

// TODO: pass cam data diffrently
#include "..\r_data_structs.h"

layout( push_constant ) uniform block{
	uint64_t vtxAddr;
	uint64_t transfAddr;
	//uint64_t drawCmdAddr;
	uint64_t camDataAddr;
};

layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer vtx_ref{
	vertex vertices[];
};
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer inst_desc_ref{
	instance_desc instDescs[];
};
//layout( buffer_reference, buffer_reference_align = 4 ) readonly buffer draw_cmd_ref{
//	draw_command drawCmd[];
//};
layout( buffer_reference, buffer_reference_align = 16 ) readonly buffer cam_data_ref{
	global_data camera;
};


void main()
{
	uint instId = uint( gl_VertexIndex & uint16_t( -1 ) );
	uint vertexId = uint( gl_VertexIndex >> 16 );

	// TODO: pos only
	// TODO: trans only
	vertex vtx = vtx_ref( vtxAddr ).vertices[ vertexId ];
	instance_desc thisInst = inst_desc_ref( transfAddr ).instDescs[ instId ];
	global_data cam = cam_data_ref( camDataAddr ).camera;

	gl_Position = cam.proj * cam.mainView * thisInst.localToWorld * vec4( vtx.px, vtx.py, vtx.pz, 1.0f );

	//vertex vtx = vtx_ref( vtxAddr ).vertices[ gl_VertexIndex ];
	//vec3 pos = vec3( vtx.px, vtx.py, vtx.pz );
	//
	//uint di = draw_cmd_ref( drawCmdAddr ).drawCmd[ gl_DrawIDARB ].drawIdx;
	//instance_desc inst = inst_desc_ref( transfAddr ).instDescs[ di ];
	//global_data cam = cam_data_ref( camDataAddr ).camera;
	//
	//
	//gl_Position = cam.proj * cam.activeView * inst.localToWorld * vec4( pos, 1.0f );
}