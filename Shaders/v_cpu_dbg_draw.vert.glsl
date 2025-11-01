#version 460

#extension GL_GOOGLE_include_directive : require

#define BINDLESS
#include "..\r_data_structs.h"

layout( push_constant, scalar ) uniform block {
	mat4 transf;
	uint64_t vtxAddr;
	uint64_t viewAddr;
	uint viewIdx;
	uint color;
};

layout( buffer_reference ) readonly buffer dbg_vtx_ref{
	vec3 dbgVertices[];
};

layout( buffer_reference, buffer_reference_align = 16 ) readonly buffer view_ref{
	view_data views[];
};

vec3 AabbTransfromVertex( vec3 pos, vec3 minBox, vec3 maxBox )
{
	vec3 center = ( maxBox + minBox ) * 0.5f;
	vec3 scale = abs( maxBox - minBox ) * 0.5f;

	return pos * scale + center;
}

layout( location = 0 ) out vec3 oCol;
void main()
{
	vec3 pos = dbg_vtx_ref( vtxAddr ).dbgVertices[ gl_VertexIndex ];

	view_data viewData = view_ref( viewAddr ).views[ viewIdx ];

	gl_Position = viewData.mainViewProj * transf * vec4( pos, 1.0f );
	oCol = unpackUnorm4x8( color ).zyx;
}