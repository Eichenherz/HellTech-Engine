#version 460

#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive : require


#include "../r_data_structs.h"

layout( push_constant ) uniform block{
	mat4		viewProj;
	vec4		col;
	uint64_t	cmdAddr;
	uint64_t	transfAddr;
	uint64_t	meshletAddr;
};

layout( buffer_reference, scalar, buffer_reference_align = 8 ) readonly buffer draw_indir_ref{
	draw_indirect drawCmd[];
};
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer inst_desc_ref{
	instance_desc instDescs[];
};
layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer meshlet_desc_ref{ 
	meshlet meshlets[]; 
};
//layout( binding = 0, scalar ) readonly buffer draw_indir{
//	draw_indirect drawCmd[];
//};
//layout( binding = 1, std430 ) readonly buffer inst_desc{
//	instance_desc instDescs[];
//};
//layout( binding = 2, scalar ) readonly buffer meshlet_desc{ 
//	meshlet meshlets[]; 
//};


const vec4 BOX_TRIANGLES[] = {
	vec4( -1.0,1.0,1.0,1.0 ),
	vec4( -1.0,-1.0,1.0,1.0 ),
	vec4( -1.0,-1.0,-1.0,1.0 ),
	vec4( 1.0,1.0,1.0,1.0 ),
	vec4( 1.0,-1.0,1.0,1.0 ),
	vec4( -1.0,-1.0,1.0,1.0 ),
	vec4( 1.0,1.0,-1.0,1.0 ),
	vec4( 1.0,-1.0,-1.0,1.0 ),
	vec4( 1.0,-1.0,1.0,1.0 ),
	vec4( -1.0,1.0,-1.0,1.0 ),
	vec4( -1.0,-1.0,-1.0,1.0 ),
	vec4( 1.0,-1.0,-1.0,1.0 ),
	vec4( -1.0,-1.0,1.0,1.0 ),
	vec4( 1.0,-1.0,1.0,1.0 ),
	vec4( 1.0,-1.0,-1.0,1.0 ),
	vec4( 1.0,1.0,1.0,1.0 ),
	vec4( -1.0,1.0,1.0,1.0 ),
	vec4( -1.0,1.0,-1.0,1.0 ),
	vec4( -1.0,1.0,-1.0,1.0 ),
	vec4( -1.0,1.0,1.0,1.0 ),
	vec4( -1.0,-1.0,-1.0,1.0 ),
	vec4( -1.0,1.0,1.0,1.0 ),
	vec4( 1.0,1.0,1.0,1.0 ),
	vec4( -1.0,-1.0,1.0,1.0 ),
	vec4( 1.0,1.0,1.0,1.0 ),
	vec4( 1.0,1.0,-1.0,1.0 ),
	vec4( 1.0,-1.0,1.0,1.0 ),
	vec4( 1.0,1.0,-1.0,1.0 ),
	vec4( -1.0,1.0,-1.0,1.0 ),
	vec4( 1.0,-1.0,-1.0,1.0 ),
	vec4( -1.0,-1.0,-1.0,1.0 ),
	vec4( -1.0,-1.0,1.0,1.0 ),
	vec4( 1.0,-1.0,-1.0,1.0 ),
	vec4( 1.0,1.0,-1.0,1.0 ),
	vec4( 1.0,1.0,1.0,1.0 ),
	vec4( -1.0,1.0,-1.0,1.0 )
};

const vec4 BOX_CORNERS[] = {
	vec4( -1.0,1.0,1.0,1.0 ),
	vec4( -1.0,-1.0,1.0,1.0 ),
	vec4( -1.0,-1.0,-1.0,1.0 ),
	vec4( 1.0,1.0,1.0,1.0 ),
	vec4( 1.0,-1.0,1.0,1.0 ),
	vec4( 1.0,1.0,-1.0,1.0 ),
	vec4( 1.0,-1.0,-1.0,1.0 ),
	vec4( -1.0,1.0,-1.0,1.0 )
};

const vec4 BOX_EDGES[] = {
	vec4( -1.0, 1.0,-1.0,1.0 ),
	vec4(  1.0, 1.0,-1.0,1.0 ),

	vec4(  1.0, 1.0,-1.0,1.0 ),
	vec4(  1.0,-1.0,-1.0,1.0 ),

	vec4(  1.0,-1.0,-1.0,1.0 ),
	vec4( -1.0,-1.0,-1.0,1.0 ),

	vec4( -1.0,-1.0,-1.0,1.0 ),
	vec4( -1.0, 1.0,-1.0,1.0 ),

	vec4( -1.0, 1.0, 1.0,1.0 ),
	vec4(  1.0, 1.0, 1.0,1.0 ),

	vec4(  1.0, 1.0, 1.0,1.0 ),
	vec4(  1.0,-1.0, 1.0,1.0 ),

	vec4(  1.0,-1.0, 1.0,1.0 ),
	vec4( -1.0,-1.0, 1.0,1.0 ),

	vec4( -1.0,-1.0, 1.0,1.0 ),
	vec4( -1.0, 1.0, 1.0,1.0 ),

	vec4( -1.0, 1.0,-1.0,1.0 ),
	vec4( -1.0, 1.0, 1.0,1.0 ),

	vec4(  1.0, 1.0,-1.0,1.0 ),
	vec4(  1.0, 1.0, 1.0,1.0 ),

	vec4(  1.0,-1.0,-1.0,1.0 ),
	vec4(  1.0,-1.0, 1.0,1.0 ),

	vec4( -1.0,-1.0,-1.0,1.0 ),
	vec4( -1.0,-1.0, 1.0,1.0 )
};


layout( location = 0 ) out vec3 oCol;
void main()
{
	vec4 vtx = BOX_EDGES[ gl_VertexIndex ];

	//uint64_t di = drawCmd[ gl_DrawIDARB ].drawIdx;
	uint64_t di = draw_indir_ref( cmdAddr ).drawCmd[ gl_DrawIDARB ].drawIdx;
	uint ii = uint( di & uint( -1 ) );
	uint mi = uint( di >> 32 );

	//instance_desc inst = instDescs[ ii ];
	//meshlet m = meshlets[ mi ];
	
	instance_desc inst = inst_desc_ref( transfAddr ).instDescs[ ii ];
	meshlet m = meshlet_desc_ref( meshletAddr ).meshlets[ mi ];

	vec3 pos = vtx.xyz * m.extent + m.center;
	
	gl_Position = viewProj * inst.localToWorld * vec4( pos, 1 );
	oCol = col.xyz;
}