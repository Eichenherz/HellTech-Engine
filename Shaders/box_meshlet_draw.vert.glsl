#version 460

#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive : require


#include "../r_data_structs.h"

layout( push_constant ) uniform block{
	mat4		viewProj;
	vec4		col;
	uint64_t	cmdAddr;
	uint64_t	transfAddr;
	uint64_t	meshlet_w_coneAddr;
};

layout( buffer_reference, scalar, buffer_reference_align = 8 ) readonly buffer draw_indir_ref{
	draw_indirect drawCmd[];
};
layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer inst_desc_ref{
	instance_desc instDescs[];
};
layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer meshlet_w_cone_desc_ref{ 
	meshlet_w_cone meshlet_w_cones[]; 
};
layout( buffer_reference, buffer_reference_align = 4 ) readonly buffer compacted_args_ref{
	compacted_draw_args compactedDrawArgs[];
};


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
	//meshlet_w_cone m = meshlet_w_cones[ mi ];
	
	instance_desc inst = inst_desc_ref( transfAddr ).instDescs[ ii ];
	meshlet_w_cone m = meshlet_w_cone_desc_ref( meshlet_w_coneAddr ).meshlet_w_cones[ mi ];

	vec3 pos = vtx.xyz * m.extent + m.center;
	
	gl_Position = viewProj * inst.localToWorld * vec4( pos, 1 );
	oCol = col.xyz;
}