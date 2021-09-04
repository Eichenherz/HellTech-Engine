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
	mesh_desc meshes[]; 
};
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer inst_desc_ref{
	instance_desc instDescs[];
};


vec2 SignNonZero( vec2 e )
{
	return vec2( ( ( e.x >= 0.0 ) ? 1.0 : -1.0 ), ( ( e.y >= 0.0 ) ? 1.0 : -1.0 ) );
}
float Snorm8ToFloat( uint8_t x )
{
	return float( uint( x ) ) * ( 1.0 / 127.0 ) - 1.0;
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
}
vec2 EncodeOctaNormal( vec3 n )
{
	// NOTE: Project the sphere onto the octahedron, and then onto the xy plane
	vec2 octa = n.xy * ( 1.0 / ( abs( n.x ) + abs( n.y ) + abs( n.z ) ) );
	// NOTE: Reflect the folds of the lower hemisphere over the diagonals
	return ( n.z < 0.0 ) ? ( SignNonZero( octa ) - abs( octa.yx ) * SignNonZero( octa ) ) : octa;
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


layout( location = 0 ) out vec3 oNormal;
layout( location = 1 ) out vec3 oTan;
layout( location = 2 ) out vec3 oFragWorldPos;
layout( location = 3 ) out vec2 oUv;
layout( location = 4 ) out flat uint oMtlIdx;
void main() 
{
	// TODO: need nonuniformEXT ?
	uint instId = uint( gl_VertexIndex & uint16_t( -1 ) );
	uint vertexId = uint( gl_VertexIndex >> 16 );

	vertex vtx = vtx_ref( bdas.vtxAddr ).vertices[ vertexId ];
	vec3 pos = vec3( vtx.px, vtx.py, vtx.pz );
	vec3 norm = DecodeOctaNormal( vec2( Snorm8ToFloat( vtx.snorm8octNx ), Snorm8ToFloat( vtx.snorm8octNy ) ) );
	vec2 texCoord = vec2( vtx.tu, vtx.tv );

	instance_desc inst = inst_desc_ref( bdas.instDescAddr ).instDescs[ instId ];
	
	vec3 worldPos = RotateQuat( pos * inst.scale, inst.rot ) + inst.pos;
	//vec3 worldPos = ( inst.localToWorld * vec4( pos, 1 ) ).xyz;
	gl_Position = cam.proj * cam.activeView * vec4( worldPos, 1 );

	vec3 n = normalize( norm );
	vec3 t = DecodeTanFromAngle( norm, Snorm8ToFloat( vtx.snorm8tanAngle ) );
	t = normalize( RotateQuat( t, inst.rot ) );
	n = normalize( RotateQuat( n, inst.rot ) );

	oNormal = n;
	oTan = t;
	oFragWorldPos = worldPos;
	oUv = texCoord;
	oMtlIdx = inst.mtrlIdx;
}