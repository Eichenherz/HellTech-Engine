#version 460


#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive : require

#define GLOBAL_RESOURCES

#include "..\r_data_structs.h"
#include "glsl_func_lib.h"

layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer pos_ref{
	vec3 positions[];
};
layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer norm_ref{
	vec4 normals[];
};
layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer uvm_ref{
	vec3 uvms[];
};
layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer mesh_ref{ 
	mesh meshes[]; 
};
//layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer draw_args_ref{
//	draw_data drawArgs[];
//};


layout( binding = 0 ) readonly buffer draw_args_buffer{
	draw_data drawArgs[];
};

layout( binding = 1 ) readonly buffer draw_cmd_buffer{
	draw_command drawCmd[];
};

layout( location = 0 ) out vec3 normal;
layout( location = 1 ) out vec3 tan;
layout( location = 2 ) out vec3 bitan;
layout( location = 3 ) out vec3 fragWorldPos;
layout( location = 4 ) out vec2 uv;
layout( location = 5 ) out uint mtlIdx;

float Snorm8Dequant( int x )
{
	return float( x ) / 127.0 - 1.0;
}
vec3 Snorm8OctahedronDecode( int16_t x )
{
	vec2 f = vec2( Snorm8Dequant( int( x ) & 0xff ), 
				   Snorm8Dequant( ( int( x ) >> 8 ) & 0xff ) );
	f = f * 2.0 - 1.0;

	// NOTE: https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
	vec3 n = vec3( f.x, f.y, 1.0 - abs( f.x ) - abs( f.y ) );
	float t = clamp( -n.z, 0.0, 1.0 );
	n.xy += sign( n.xy ) * t;
	n.xy += vec2( ( n.x >= 0.0 ) ? -t : t, ( n.y >= 0.0 ) ? -t : t );
	return normalize( n );
}

vec3 DecodeTanFromAngle( vec3 n, float tanAngle )
{
	// NOTE: inspired by Doom Eternal
	vec3 tanRef = ( abs( n.x ) > abs( n.z ) ) ?
		vec3( -n.y, n.x, 0.0 ) :
		vec3( 0.0, -n.z, n.y );

	tanAngle *= 3.141592654;
	return tanRef * cos( tanAngle ) + cross( n, tanRef ) * sin( tanAngle );
}

void main() 
{
	vec3 pos = pos_ref( g.addr + g.posOffset ).positions[ gl_VertexIndex ];
	//int16_t encNorm = norm_ref( g.addr + g.normOffset ).normals[ gl_VertexIndex ];
	//vec3 norm = Snorm8OctahedronDecode( encNorm );
	vec4 norm = norm_ref( g.addr + g.normOffset ).normals[ gl_VertexIndex ];
	vec3 texMatCoord = uvm_ref( g.addr + g.uvsOffset ).uvms[ gl_VertexIndex ];


	uint di = drawCmd[ gl_DrawIDARB ].drawIdx;
	draw_data args = drawArgs[ di ];
	mesh currentMesh = mesh_ref( g.addr + g.meshesOffset ).meshes[ args.meshIdx ];
	vec3 worldPos = RotateQuat( pos * args.scale, args.rot ) + args.pos;
	gl_Position = cam.proj * cam.view * vec4( worldPos, 1.0 );

	vec3 n = normalize( norm.xyz );
	vec3 t = DecodeTanFromAngle( norm.xyz, norm.w );
	vec3 b = cross( n, t );

	t = normalize( RotateQuat( t, args.rot ) );
	b = normalize( RotateQuat( b, args.rot ) );
	n = normalize( RotateQuat( n, args.rot ) );

	// NOTE: orthonormalization ?
	//t = normalize( t - dot( t, n ) * n );

	normal = n;// normalize( norm.xyz );
	tan = t;
	bitan = b;
	fragWorldPos = worldPos;
	uv = texMatCoord.xy;
	mtlIdx = uint( texMatCoord.z );
}