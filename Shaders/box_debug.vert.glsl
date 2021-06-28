#version 460

#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive : require


#define GLOBAL_RESOURCES

#define GLSL_DBG 0

#include "../r_data_structs.h"

//#if GLSL_DBG
//layout( push_constant ) uniform block{
//	uint freeCam;
//};
//#endif

layout( binding = 0, scalar ) readonly buffer draw_cmd{
	draw_indirect drawCmd[];
};
//layout( binding = 1, scalar ) readonly buffer mesh_dsc{
//	mesh_desc meshes[];
//};
//layout( binding = 2 ) readonly buffer inst_desc{
//	instance_desc instDescs[];
//};
layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer mesh_desc_ref{
	mesh_desc meshes[];
};
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer inst_desc_ref{
	instance_desc instDescs[];
};



layout( location = 0 ) out vec3 oCol;

vec3 RotateQuat( vec3 v, vec4 q )
{
	vec3 t = 2.0 * cross( q.xyz, v );
	return v + q.w * t + cross( q.xyz, t );
}

const vec4 BOX[] = {
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

void main()
{
	uint di = drawCmd[ gl_DrawIDARB ].drawIdx;
	instance_desc inst = inst_desc_ref( bdas.instDescAddr ).instDescs[ di ];
	mesh_desc m = mesh_desc_ref( bdas.meshDescAddr ).meshes[ inst.meshIdx ];
	//instance_desc inst = instDescs[ di ];
	//mesh_desc m = meshes[ inst.meshIdx ];

	vec4 vtx = BOX[ gl_VertexIndex ];
	vec3 pos = vtx.xyz * m.extent + m.center;
	vec3 worldPos = RotateQuat( pos * inst.scale, inst.rot ) + inst.pos;
	vec4 outScreenPos = cam.view * vec4( worldPos, 1 );
//#if GLSL_DBG
//	outScreenPos = bool( freeCam ) ? ( cam.dbgCam * outScreenPos ) : outScreenPos;
//#endif
	gl_Position = cam.proj * outScreenPos;
	oCol = vec3( 255, 255, 0 );
}