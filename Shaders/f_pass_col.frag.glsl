#version 460

layout( location = 0 ) in vec3 col;
layout( location = 0 ) out vec4 oCol;
void main()
{
	oCol = vec4( col, 1.0 );
}