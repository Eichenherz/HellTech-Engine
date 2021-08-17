#version 460

layout( push_constant ) uniform block{
	mat4 projView;
};

struct dbg_vertex
{
	vec4 pos;
	vec3 col;
};

layout( binding = 0 ) readonly buffer dbg_vtx_buff{
	dbg_vertex dbgGeomBuff[];
};

layout( location = 0 ) out vec3 oCol;

void main()
{
	dbg_vertex v = dbgGeomBuff[ gl_VertexIndex ];
	gl_Position = projView * v.pos;
	oCol = v.col.xyz;
}