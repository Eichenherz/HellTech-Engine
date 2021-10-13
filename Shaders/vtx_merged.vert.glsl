#version 460


#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive : require

//#define GLOBAL_RESOURCES

#include "..\r_data_structs.h"
#include "glsl_func_lib.h"

layout( push_constant ) uniform block{
	uint64_t vtxAddr;
	uint64_t transfAddr;
	//uint64_t drawCmdAddr;
	uint64_t camDataAddr;
};

layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer vtx_ref{
	vertex vertices[];
};
//layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer mesh_ref{ 
//	mesh_desc meshes[]; 
//};
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer inst_desc_ref{
	instance_desc instDescs[];
};
layout( buffer_reference, buffer_reference_align = 16 ) readonly buffer cam_data_ref{
	global_data camera;
};

vec2 SignNonZero( vec2 e )
{
	return mix( vec2( 1.0f ), vec2( -1.0f ), lessThan( e, vec2( 0.0f ) ) );
}
vec3 DecodeOctaNormal( vec2 octa )
{
	vec3 n = vec3( octa, 1.0 - abs( octa.x ) - abs( octa.y ) );
	//vec2 signVector = SignNonZero( n.xy );
	//n.xy = ( n.z < 0 ) ? ( signVector - signVector * abs( n.yx ) ) : n.xy;

	// NOTE: Rune Stubbe's version : https://twitter.com/Stubbesaurus/status/937994790553227264
	//float t = max( -n.z, 0.0f );                     
	//n.x += ( n.x > 0.0f ) ? -t : t;                     
	//n.y += ( n.y > 0.0f ) ? -t : t;                   
	
	vec2 t = vec2( max( -n.z, 0.0f ) );
	n.xy += mix( -t, t, lessThan( n.xy, vec2( 0.0f ) ) );

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


struct vs_out
{
	vec3 n;
	vec3 t;
	vec3 worldPos;
	vec2 uv;
};

layout( location = 0 ) out vs_out vsOut;
layout( location = 4 ) out flat uint oMtlIdx;
void main() 
{
	uint instId = uint( gl_VertexIndex & uint16_t( -1 ) );
	uint vertexId = uint( gl_VertexIndex >> 16 );

	//instance_desc inst = inst_desc_ref( bdas.instDescAddr ).instDescs[ instId ];
	instance_desc inst = inst_desc_ref( transfAddr ).instDescs[ instId ];
	global_data cam = cam_data_ref( camDataAddr ).camera;
	//vertex vtx = vtx_ref( bdas.vtxAddr ).vertices[ vertexId ];
	vertex vtx = vtx_ref( vtxAddr ).vertices[ vertexId ];
	
	vec3 worldPos = RotateQuat( vec3( vtx.px, vtx.py, vtx.pz ) * inst.scale, inst.rot ) + inst.pos;
	//vec3 worldPos = ( inst.localToWorld * vec4( pos, 1 ) ).xyz;
	gl_Position = cam.proj * cam.activeView * vec4( worldPos, 1 );

	vec3 encodedTanFame = unpackSnorm4x8( vtx.snorm8octTanFrame ).xyz;
	vec3 n = DecodeOctaNormal( encodedTanFame.xy );
	vec3 t = DecodeTanFromAngle( n, encodedTanFame.z );
	t = normalize( RotateQuat( t, inst.rot ) );
	n = normalize( RotateQuat( n, inst.rot ) );


	vsOut = vs_out( n, t, worldPos, vec2( vtx.tu, vtx.tv ) );
	oMtlIdx = inst.mtrlIdx;
}