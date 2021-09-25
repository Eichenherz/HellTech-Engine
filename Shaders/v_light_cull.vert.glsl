#version 460

#extension GL_GOOGLE_include_directive: require

#include "..\r_data_structs.h"

// TODO: pass matrices differently
layout( push_constant ) uniform block{
	mat4		projView;
	uint64_t	geomAddr;
	uint64_t	lightsAddr;
};


layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer light_ref{
	light_data lights[];
};
layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer geom_ref{
	vec3 pos[];
};

layout( location = 0 ) out flat uint lightId;
void main()
{
	uint vertexIdx = uint( gl_VertexIndex & uint16_t( -1 ) );
	uint lightIdx = uint( gl_VertexIndex >> 16 );

	vec3 pos = geom_ref( geomAddr ).pos[ vertexIdx ];
	light_data light = light_ref( lightsAddr ).lights[ lightIdx ];

	gl_Position = projView * vec4( pos * light.radius + light.pos, 1.0f );
	lightId = lightIdx;
}