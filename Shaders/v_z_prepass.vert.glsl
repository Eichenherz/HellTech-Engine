#version 460

#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive : require
#define BINDLESS
#include "..\r_data_structs.h"

layout( push_constant ) uniform block{
	uint64_t vtxAddr;
	uint64_t transfAddr;
	uint64_t meshletsVtxAddr;
	uint64_t compactedDrawsAddr;
	uint viewDataIdx;
};

layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer vtx_ref{
	vertex vertices[];
};
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer inst_desc_ref{
	instance_desc instDescs[];
};
layout( buffer_reference, buffer_reference_align = 4 ) readonly buffer mlet_vtx_ref{
	uint mletVerts[];
};
layout( buffer_reference, buffer_reference_align = 4 ) readonly buffer compacted_args_ref{
	compacted_draw_args compactedDrawArgs[];
};


void main()
{
	//uint mletVtx = mlet_vtx_ref( meshletsVtxAddr ).mletVerts[ gl_VertexIndex ];
	vertex vtx = vtx_ref( vtxAddr ).vertices[ gl_VertexIndex ];
	
	uint di = compacted_args_ref( compactedDrawsAddr ).compactedDrawArgs[ gl_DrawIDARB ].nodeIdx;
	instance_desc thisInst = inst_desc_ref( transfAddr ).instDescs[ di ];

	view_data view = ssbos[ viewDataIdx ].views[ 0 ];//viewIdx ];

	gl_Position = view.mainViewProj * thisInst.localToWorld * vec4( vtx.px, vtx.py, vtx.pz, 1.0f );
}