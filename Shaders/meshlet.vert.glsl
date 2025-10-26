#version 460

#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive: require

#define BINDLESS
#include "..\r_data_structs.h"

layout( push_constant ) uniform block{
	uint64_t vtxAddr;
	uint64_t transfAddr;
	uint64_t meshletsVtxAddr;
	uint64_t compactedDrawsAddr;
	uint64_t camIdx;
};

layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer vtx_ref{
	vertex vertices[];
};
layout( buffer_reference, buffer_reference_align = 4 ) readonly buffer mlet_vtx_ref{
	uint mletVerts[];
};
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer inst_desc_ref{
	instance_desc instDescs[];
};

layout( buffer_reference, buffer_reference_align = 4 ) readonly buffer compacted_args_ref{
	compacted_draw_args compactedDrawArgs[];
};

uint Hash( uint a )
{
   a = ( a + 0x7ed55d16 ) + ( a << 12 );
   a = ( a ^ 0xc761c23c ) ^ ( a >> 19 );
   a = ( a + 0x165667b1 ) + ( a << 5 );
   a = ( a + 0xd3a2646c ) ^ ( a << 9 );
   a = ( a + 0xfd7046c5 ) + ( a << 3 );
   a = ( a ^ 0xb55a4f09 ) ^ ( a >> 16 );

   return a;
}


layout( location = 0 ) out vec3 oCol;
void main() 
{
	//uint mletVtx = mlet_vtx_ref( meshletsVtxAddr ).mletVerts[ gl_VertexIndex ];
	vertex vtx = vtx_ref( vtxAddr ).vertices[ gl_VertexIndex ];
	vec3 pos = vec3( vtx.px, vtx.py, vtx.pz );

	uint di = compacted_args_ref( compactedDrawsAddr ).compactedDrawArgs[ gl_DrawIDARB ].nodeIdx;
	instance_desc inst = inst_desc_ref( transfAddr ).instDescs[ di ];
	
	global_data cam = ssbos[uint(camIdx)].g;
	gl_Position = cam.proj * cam.activeView * inst.localToWorld * vec4( pos, 1 );

	uint mhash = Hash( gl_VertexIndex );
	vec3 mcolor = vec3( float( mhash & 255 ), float( ( mhash >> 8 ) & 255 ), float( ( mhash >> 16 ) & 255 ) ) / 255.0f;
	oCol = mcolor;
}