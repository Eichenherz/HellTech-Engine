#version 460

#extension GL_GOOGLE_include_directive : require

#include "..\r_data_structs.h"

layout( location = 0 ) in vec3 normal;
layout( location = 1 ) in vec3 tan;
layout( location = 2 ) in vec3 bitan;
layout( location = 3 ) in vec3 worldPos;
layout( location = 4 ) in vec2 uv;
layout( location = 5 ) in flat uint mtlIdx;

layout( location = 0 ) out vec4 oCol;

void main()
{
	vec3 normalCol = normal * 0.5 + 0.5;
	normalCol *= 0.001;
	oCol = vec4( normalCol, 1.0 );
}