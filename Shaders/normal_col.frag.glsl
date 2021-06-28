#version 460

#extension GL_GOOGLE_include_directive : require

#include "..\r_data_structs.h"

layout( location = 0 ) in vec3 col;
layout( location = 0 ) out vec4 oCol;

void main()
{
	//vec3 normalCol = normal * 0.5 + 0.5;
	//normalCol *= 0.001;
	//oCol = vec4( col * 0.0000001, 1.0 );
	oCol = vec4( col, 1.0 );
}