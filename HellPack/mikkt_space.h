#ifndef __MIKKT_SPACE_H__
#define __MIKKT_SPACE_H__

#include "core_types.h"
#include "ht_math.h"
#include "ht_error.h"
#include "hp_types_internal.h"

#include <mikktspace.h>

struct mikkt_user_ctx
{
	const raw_mesh& m;
	std::span<float4> tans;
};

inline u32 corner_to_vertex_index( const raw_mesh& m, i32 face, i32 vert )
{
	// NOTE: face: [0..numFaces-1], vert: [0..2]
	u32 corner = face * 3u + vert;
	return m.indices[ corner ];
}
inline i32 get_num_faces( const SMikkTSpaceContext* ctx )
{
	auto* userData = ( mikkt_user_ctx* ) ( ctx->m_pUserData );
	return ( i32 ) std::size( userData->m.indices ) / 3;
}
inline i32 get_num_verts_of_face( const SMikkTSpaceContext*, i32 )
{
	return 3;
}
inline void get_position( const SMikkTSpaceContext* ctx, float outPos[], i32 face, i32 vert )
{
	auto* userData = ( mikkt_user_ctx* ) ( ctx->m_pUserData );
	u32 vi = corner_to_vertex_index( userData->m, face, vert );
	float3 p = userData->m.pos[ vi ];
	outPos[ 0 ] = p.x; outPos[ 1 ] = p.y; outPos[ 2 ] = p.z;
}
inline void get_normal( const SMikkTSpaceContext* ctx, float outN[], i32 face, i32 vert )
{
	auto* userData = ( mikkt_user_ctx* ) ( ctx->m_pUserData );
	u32 vi = corner_to_vertex_index( userData->m, face, vert );
	float3 n = userData->m.normals[ vi ];
	outN[ 0 ] = n.x; outN[ 1 ] = n.y; outN[ 2 ] = n.z;
}
inline void get_texcoord( const SMikkTSpaceContext* ctx, float outUV[], i32 face, i32 vert )
{
	auto* userData = ( mikkt_user_ctx* ) ( ctx->m_pUserData );
	u32 vi = corner_to_vertex_index( userData->m, face, vert );
	float2 uv = userData->m.uvs[ vi ];
	outUV[ 0 ] = uv.x; outUV[ 1 ] = uv.y;
}
inline void set_tspace_basic(
	const SMikkTSpaceContext* ctx,
	const float tangent[],
	float sign,
	i32 face,
	i32 vert
) {
	auto* userData = ( mikkt_user_ctx* ) ( ctx->m_pUserData );
	u32 vi = corner_to_vertex_index( userData->m, face, vert );

	// MikkTSpace may call this multiple times for the same vi (shared vertex across faces).
	// If your mesh is "regular" and you want the final averaged tangent, overwriting is fine:
	// the algorithm’s internal accumulation decides the final tangent per corner/vertex.
	//
	// If you later discover mismatches at seams, it means your indices were NOT split.
	userData->tans[ vi ] = { tangent[ 0 ], tangent[ 1 ], tangent[ 2 ], sign };
}

inline auto ComputeMikkTSpaceTangentsInplace( const raw_mesh& rawMesh )
{
	std::vector<float4> tans( std::size( rawMesh.pos ), float4{ 0,0,0,1 } );

	SMikkTSpaceInterface iface = {
		.m_getNumFaces = get_num_faces,
		.m_getNumVerticesOfFace = get_num_verts_of_face,
		.m_getPosition = get_position,
		.m_getNormal = get_normal,
		.m_getTexCoord = get_texcoord,
		.m_setTSpaceBasic = set_tspace_basic,
	};

	mikkt_user_ctx userData = { .m = rawMesh, .tans = tans };

	SMikkTSpaceContext ctx = { .m_pInterface = &iface, .m_pUserData = &userData };

	// NOTE: Returns 1 on success, 0 on failure (degenerates etc.)
	HT_ASSERT( 1 == genTangSpaceDefault( &ctx ) );

	return tans;
}

#endif // !__MIKKT_SPACE_H__
