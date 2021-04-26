#version 460


#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive : require

#define GLOBAL_RESOURCES

#include "..\r_data_structs.h"
#include "glsl_func_lib.h"

layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer vtx_ref{
	vertex vertices[];
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

layout( location = 0 ) out vec3 oNormal;
layout( location = 1 ) out vec3 oTan;
layout( location = 2 ) out vec3 oFragWorldPos;
layout( location = 3 ) out vec2 oUv;
layout( location = 4 ) out uint oMtlIdx;

vec2 SignNonZero( vec2 e )
{
	return vec2( ( ( e.x >= 0.0 ) ? 1.0 : -1.0 ), ( ( e.y >= 0.0 ) ? 1.0 : -1.0 ) );
}
float Snorm8ToFloat( uint8_t x )
{
	return float( x ) / 127.0 - 1.0;
}
vec3 DecodeOctaNormal( vec2 octa )
{
	vec3 n = vec3( octa, 1.0 - abs( octa.x ) - abs( octa.y ) );
	
	n.xy = ( n.z < 0 ) ? ( SignNonZero( n.xy ) - SignNonZero( n.xy ) * abs( n.yx ) ) : n.xy;
	// NOTE: Rune Stubbe's version
	//float t = max( -n.z, 0.0 );                     
	//n.x += ( n.x > 0.0 ) ? -t : t;                     
	//n.y += ( n.y > 0.0 ) ? -t : t;                   
	
	return normalize( n );
	//vec2 t = vec2( octa.x + octa.y, octa.x - octa.y );
	//return normalize( vec3( t, 2.0 - abs( t.x ) - abs( t.y ) ) );
}
vec2 EncodeOctaNormal( vec3 n )
{
	// NOTE: Project the sphere onto the octahedron, and then onto the xy plane
	vec2 octa = n.xy * ( 1.0 / ( abs( n.x ) + abs( n.y ) + abs( n.z ) ) );
	// NOTE: Reflect the folds of the lower hemisphere over the diagonals
	return ( n.z < 0.0 ) ? ( SignNonZero( octa ) - abs( octa.yx ) * SignNonZero( octa ) ) : octa;
	//vec2 t = n.xy * ( 1.0 / ( abs( n.x ) + abs( n.y ) + abs( n.z ) ) );
	//return vec2( t.x + t.y, t.x - t.y );
}

vec3 DecodeTanFromAngle( vec3 n, float tanAngle )
{
	// NOTE: inspired by Doom Eternal
	vec3 tanRef = ( abs( n.x ) > abs( n.z ) ) ?
		vec3( -n.y, n.x, 0.0 ) :
		vec3( 0.0, -n.z, n.y );

	tanAngle *= PI;
	return tanRef * cos( tanAngle ) + cross( n, tanRef ) * sin( tanAngle );
}

void main() 
{
	vertex vtx = vtx_ref( g.addr + g.vtxOffset ).vertices[ gl_VertexIndex ];
	vec3 pos = vec3( vtx.px, vtx.py, vtx.pz );
	vec3 normal = vec3( vtx.nx, vtx.ny, vtx.nz );

	//vec3 norm = normal;
	vec3 norm = DecodeOctaNormal( EncodeOctaNormal( normal ) );
	vec2 octaNormal = vec2( vtx.octaNormalX, vtx.octaNormalY );
	//vec3 norm = DecodeOctaNormal( octaNormal );
	vec2 texCoord = vec2( vtx.tu, vtx.tv );

	uint di = drawCmd[ gl_DrawIDARB ].drawIdx;
	draw_data args = drawArgs[ di ];
	mesh currentMesh = mesh_ref( g.addr + g.meshesOffset ).meshes[ args.meshIdx ];

	vec3 worldPos = RotateQuat( pos * args.scale, args.rot ) + args.pos;
	gl_Position = cam.proj * cam.view * vec4( worldPos, 1.0 );

	vec3 n = normalize( norm );
	vec3 t = DecodeTanFromAngle( norm, vtx.tAngle );
	t = normalize( RotateQuat( t, args.rot ) );
	n = normalize( RotateQuat( n, args.rot ) );

	oNormal = n;
	oTan = t;
	oFragWorldPos = worldPos;
	oUv = texCoord;
	oMtlIdx = vtx.mi;
}