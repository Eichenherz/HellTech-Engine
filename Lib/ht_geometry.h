#pragma once

#ifndef __HT_GEOMETRY_H__
#define __HT_GEOMETRY_H__

#include "ht_core_types.h"
#include "ht_vec_types.h"
#include "ht_math.h"

#include <array>

template<typename Index>
inline constexpr void ReverseTriangleWinding( Index* indices, u64 count )
{
	assert( count % 3 == 0 );
	for( u64 t = 0; t < count; t += 3 ) std::swap( indices[ t ], indices[ t + 2 ] );
}
// TODO: improve ?
inline void GenerateIcosphere( std::vector<DirectX::XMFLOAT3>& vtxData, std::vector<u32>& idxData, u64 numIters )
{
	using namespace DirectX;

	constexpr u64 ICOSAHEDRON_FACE_NUM = 20;
	constexpr u64 ICOSAHEDRON_VTX_NUM = 12;

	constexpr float X = 0.525731112119133606f;
	constexpr float Z = 0.850650808352039932f;
	constexpr float N = 0;

	constexpr XMFLOAT3 vertices[ ICOSAHEDRON_VTX_NUM ] =
	{
		{-X,N,Z}, {X,N,Z}, {-X,N,-Z}, {X,N,-Z},
		{N,Z,X}, {N,Z,-X}, {N,-Z,X}, {N,-Z,-X},
		{Z,X,N}, {-Z,X, N}, {Z,-X,N}, {-Z,-X, N}
	};

	u32 triangles[ 3 * ICOSAHEDRON_FACE_NUM ] =
	{
		0,4,1,	0,9,4,	9,5,4,	4,5,8,	4,8,1,
		8,10,1,	8,3,10,	5,3,8,	5,2,3,	2,7,3,
		7,10,3,	7,6,10,	7,11,6,	11,0,6,	0,1,6,
		6,1,10,	9,0,11,	9,11,2,	9,2,5,	7,2,11
	};

	//if constexpr( worldLeftHanded ) ReverseTriangleWinding( triangles, std::size( triangles ) );

	std::vector<XMFLOAT3> vtxCache;
	std::vector<u32> idxCache;

	vtxCache = { std::begin( vertices ), std::end( vertices ) };
	idxData = { std::begin( triangles ),std::end( triangles ) };

	//vtxCache.reserve( ICOSAHEDRON_VTX_NUM * ( 1ull << numIters ) );
	idxCache.reserve( 3 * ICOSAHEDRON_FACE_NUM * ( 1ull << ( 2 * numIters ) ) );
	idxData.reserve( 3 * ICOSAHEDRON_FACE_NUM * ( 1ull << ( 2 * numIters ) ) );


	for( u64 i = 0; i < numIters; ++i )
	{
		for( u64 t = 0; t < std::size( idxData ); t += 3 )
		{
			u32 i0 = idxData[ t ];
			u32 i1 = idxData[ t + 1 ];
			u32 i2 = idxData[ t + 2 ];

			XMVECTOR v0 = XMLoadFloat3( &vtxCache[ i0 ] );
			XMVECTOR v1 = XMLoadFloat3( &vtxCache[ i1 ] );
			XMVECTOR v2 = XMLoadFloat3( &vtxCache[ i2 ] );
			XMFLOAT3 m01, m12, m20;
			XMStoreFloat3( &m01, XMVector3Normalize( XMVectorAdd( v0, v1 ) ) );
			XMStoreFloat3( &m12, XMVector3Normalize( XMVectorAdd( v1, v2 ) ) );
			XMStoreFloat3( &m20, XMVector3Normalize( XMVectorAdd( v2, v0 ) ) );

			u32 idxOffset = ( u32 ) std::size( vtxCache ) - 1;

			vtxCache.push_back( m01 );
			vtxCache.push_back( m12 );
			vtxCache.push_back( m20 );

			if constexpr( true )//!worldLeftHanded )
			{
				idxCache.push_back( idxOffset + 1 );
				idxCache.push_back( idxOffset + 3 );
				idxCache.push_back( i0 );

				idxCache.push_back( idxOffset + 2 );
				idxCache.push_back( i2 );
				idxCache.push_back( idxOffset + 3 );

				idxCache.push_back( idxOffset + 1 );
				idxCache.push_back( idxOffset + 2 );
				idxCache.push_back( idxOffset + 3 );

				idxCache.push_back( i1 );
				idxCache.push_back( idxOffset + 2 );
				idxCache.push_back( idxOffset + 1 );
			}
			else
			{
				idxCache.push_back( i0 );
				idxCache.push_back( idxOffset + 3 );
				idxCache.push_back( idxOffset + 1 );

				idxCache.push_back( idxOffset + 3 );
				idxCache.push_back( i2 );
				idxCache.push_back( idxOffset + 2 );

				idxCache.push_back( idxOffset + 3 );
				idxCache.push_back( idxOffset + 2 );
				idxCache.push_back( idxOffset + 1 );

				idxCache.push_back( idxOffset + 1 );
				idxCache.push_back( idxOffset + 2 );
				idxCache.push_back( i1 );
			}
		}

		idxData = idxCache;
	}

	vtxData = std::move( vtxCache );
}


using box_vertices = std::array<dbg_vertex, 8u>;

constexpr box_vertices GenerateDbgBoxFromBounds( float3 boxMin, float3 boxMax )
{
	box_vertices box = {};
	box[ 0 ].pos = { boxMax.x, boxMax.y, boxMax.z };
	box[ 1 ].pos = { boxMax.x, boxMax.y, boxMin.z };
	box[ 2 ].pos = { boxMax.x, boxMin.y, boxMax.z };
	box[ 3 ].pos = { boxMax.x, boxMin.y, boxMin.z };
	box[ 4 ].pos = { boxMin.x, boxMax.y, boxMax.z };
	box[ 5 ].pos = { boxMin.x, boxMax.y, boxMin.z };
	box[ 6 ].pos = { boxMin.x, boxMin.y, boxMax.z };
	box[ 7 ].pos = { boxMin.x, boxMin.y, boxMin.z };
	return box;
}

using dbg_index_t = u16;

using box_wireframe_indices = std::array<dbg_index_t, 24>;

constexpr box_wireframe_indices GenerateBoxWireframeIndices()
{
	return {
		0,4, 1,5, 2,6, 3,7,   // X edges
		0,2, 1,3, 4,6, 5,7,   // Y edges
		0,1, 2,3, 4,5, 6,7,   // Z edges
	};
}

using box_triangle_indices = std::array<dbg_index_t, 36>;
// glTF: RH, +Y up, -Z fwd, CCW front
constexpr box_triangle_indices GenerateBoxTriIndices()
{
	return {
		// +X (0,1,2,3) outward = +X
		0,1,3,  0,3,2,
		// -X (4,5,6,7)
		4,6,7,  4,7,5,
		// +Y (0,1,4,5)
		0,4,5,  0,5,1,
		// -Y (2,3,6,7)
		2,3,7,  2,7,6,
		// +Z (0,2,4,6)
		0,2,6,  0,6,4,
		// -Z (1,3,5,7)
		1,5,7,  1,7,3,
	};
}

constexpr float3 BOX_MIN = { -0.5f, -0.5f, -0.5f };
constexpr float3 BOX_MAX = { 0.5f,  0.5f,  0.5f };

static_assert( float3{ -0.5f, -0.5f, -0.5f } == BOX_MIN );
static_assert( float3{ 0.5f,  0.5f,  0.5f } == BOX_MAX );


#endif // !__HT_GEOMETRY_H__
