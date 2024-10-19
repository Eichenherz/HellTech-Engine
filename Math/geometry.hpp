#pragma once

#include <vector>
#include <assert.h>
#include <array>
#include <span>

#include "core_types.h"
#include "r_data_structs.h"
// NOTE: clang-cl on VS issue
#ifdef __clang__
#undef __clang__
#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>
#define __clang__

#elif _MSC_VER >= 1916

#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>

#endif


/*
// NOTE: corners are stored in this way:
//	   3---------7
//	  /|        /|
//	 / |       / |
//	1---------5  |
//	|  2- - - |- 6
//	| /       | /
//	|/        |/
//	0---------4
*/
constexpr u8 boxLineIndices[] = {
	0,1,	0,2,	1,3,	2,3,
	0,4,	1,5,	2,6,	3,7,
	4,5,	4,6,	5,7,	6,7
};
// NOTE: counter-clockwise
constexpr u8 boxTrisIndices[] = {
	0, 1, 2,    // side 1
	2, 1, 3,
	4, 0, 6,    // side 2
	6, 0, 2,
	7, 5, 6,    // side 3
	6, 5, 4,
	3, 1, 7,    // side 4
	7, 1, 5,
	4, 5, 0,    // side 5
	0, 5, 1,
	3, 7, 2,    // side 6
	2, 7, 6,
};

constexpr bool worldLeftHanded = 1;

constexpr u64 boxLineVertexCount = 24u;
constexpr u64 boxTrisVertexCount = 36u;


inline constexpr void ReverseTriangleWinding( u32* indices, u64 count )
{
	assert( count % 3 == 0 );
	for( u64 t = 0; t < count; t += 3 ) std::swap( indices[ t ], indices[ t + 2 ] );
}
// TODO: improve ?
void GenerateIcosphere( std::vector<DirectX::XMFLOAT3>& vtxData, std::vector<u32>& idxData, u64 numIters )
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

	if constexpr( worldLeftHanded ) ReverseTriangleWinding( triangles, std::size( triangles ) );

	std::vector<XMFLOAT3> vtxCache;
	std::vector<u32> idxCache;

	vtxCache = { std::begin( vertices ), std::end( vertices ) };
	idxData = { std::begin( triangles ),std::end( triangles ) };

	//vtxCache.reserve( ICOSAHEDRON_VTX_NUM * std::exp2( numIters ) );
	idxCache.reserve( size_t( 3u * ICOSAHEDRON_FACE_NUM * exp2( 2u * numIters ) ) );
	idxData.reserve( size_t( 3u * ICOSAHEDRON_FACE_NUM * exp2( 2u * numIters ) ) );


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

			u32 idxOffset = ( u32 ) std::size( vtxCache ) - 1u;

			vtxCache.push_back( m01 );
			vtxCache.push_back( m12 );
			vtxCache.push_back( m20 );

			if constexpr( !worldLeftHanded )
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
// TODO: remove ?
void GenerateBoxCube( std::vector<DirectX::XMFLOAT4>& vtx, std::vector<u32>& idx )
{
	using namespace DirectX;

	constexpr float w = 0.5f;
	constexpr float h = 0.5f;
	constexpr float t = 0.5f;

	XMFLOAT3 c0 = { w, h,-t };
	XMFLOAT3 c1 = { -w, h,-t };
	XMFLOAT3 c2 = { -w,-h,-t };
	XMFLOAT3 c3 = { w,-h,-t };

	XMFLOAT3 c4 = { w, h, t };
	XMFLOAT3 c5 = { -w, h, t };
	XMFLOAT3 c6 = { -w,-h, t };
	XMFLOAT3 c7 = { w,-h, t };

	constexpr XMFLOAT4 col = {};

	XMFLOAT4 vertices[] = {
		// Bottom
		{c0.x,c0.y,c0.z,1.0f},
		{c1.x,c1.y,c1.z,1.0f},
		{c2.x,c2.y,c2.z,1.0f},
		{c3.x,c3.y,c3.z,1.0f},
		// Left					
		{c7.x,c7.y,c7.z,1.0f },
		{c4.x,c4.y,c4.z,1.0f },
		{c0.x,c0.y,c0.z,1.0f },
		{c3.x,c3.y,c3.z,1.0f },
		// Front			
		{c4.x,c4.y,c4.z,1.0f },
		{c5.x,c5.y,c5.z,1.0f },
		{c1.x,c1.y,c1.z,1.0f },
		{c0.x,c0.y,c0.z,1.0f },
		// Back				
		{c6.x,c6.y,c6.z,1.0f },
		{c7.x,c7.y,c7.z,1.0f },
		{c3.x,c3.y,c3.z,1.0f },
		{c2.x,c2.y,c2.z,1.0f },
		// Right			
		{c5.x,c5.y,c5.z,1.0f },
		{c6.x,c6.y,c6.z,1.0f },
		{c2.x,c2.y,c2.z,1.0f },
		{c1.x,c1.y,c1.z,1.0f },
		// Top				
		{c7.x,c7.y,c7.z,1.0f },
		{c6.x,c6.y,c6.z,1.0f },
		{c5.x,c5.y,c5.z,1.0f },
		{c4.x,c4.y,c4.z,1.0f }
	};

	u32 indices[] = {
			0, 1, 3,        1, 2, 3,        // Bottom	
			4, 5, 7,        5, 6, 7,        // Left
			8, 9, 11,       9, 10, 11,      // Front
			12, 13, 15,     13, 14, 15,     // Back
			16, 17, 19,     17, 18, 19,	    // Right
			20, 21, 23,     21, 22, 23	    // Top
	};

	if constexpr( worldLeftHanded ) ReverseTriangleWinding( indices, std::size( indices ) );

	vtx.insert( std::end( vtx ), std::begin( vertices ), std::end( vertices ) );
	idx.insert( std::end( idx ), std::begin( indices ), std::end( indices ) );
}


inline std::array<DirectX::XMFLOAT4, 8> XM_CALLCONV
TrnasformBoxVertices(
	DirectX::XMMATRIX		transf,
	DirectX::XMFLOAT3		boxMin,
	DirectX::XMFLOAT3		boxMax
) {
	using namespace DirectX;

	std::array<XMFLOAT4, 8> boxCorners = {};
	for( u64 ci = 0; ci < 8; ++ci )
	{
		XMFLOAT4 outCorner = { boxMax.x, boxMax.y, boxMax.z, 1.0f };
		XMVECTOR trnasformedCorner = XMVector4Transform( XMLoadFloat4( &outCorner ), transf );
		XMStoreFloat4( &boxCorners[ ci ], trnasformedCorner );
	}
	return boxCorners;
}

// TODO: draw them filled ? 
// TODO: rethink ?

// TODO: remove ?
inline void WriteBoundingBoxLines(
	const DirectX::XMFLOAT4X4A& mat,
	const DirectX::XMFLOAT3& min,
	const DirectX::XMFLOAT3& max,
	const DirectX::XMFLOAT4& col,
	dbg_vertex* thisLineRange
) {
	using namespace DirectX;

	std::array<XMFLOAT4, 8> boxVertices = TrnasformBoxVertices( XMLoadFloat4x4A( &mat ), min, max );

	for( u64 ii = 0; ii < boxLineVertexCount; ++ii )
	{
		thisLineRange[ ii ] = { boxVertices[ boxLineIndices[ ii ] ],col };
	}
}

struct box_bounds
{
	DirectX::XMFLOAT3 min;
	DirectX::XMFLOAT3 max;
};

struct entities_data
{
	std::vector<DirectX::XMFLOAT4X4A> transforms;
	std::vector<box_bounds> instAabbs;
};

inline std::vector<dbg_vertex>
ComputeSceneDebugBoundingBoxes(
	DirectX::XMMATRIX frustDrawMat,
	const entities_data& entities
) {
	using namespace DirectX;

	assert( std::size( entities.instAabbs ) == std::size( entities.transforms ) );
	assert( boxLineVertexCount == std::size( boxLineIndices ) );

	u64 entitiesCount = std::size( entities.transforms );
	std::vector<dbg_vertex> dbgGeom;
	// NOTE: + boxLineVertexCount for frustum
	dbgGeom.resize( entitiesCount * boxLineVertexCount + boxLineVertexCount );

	for( u64 i = 0; i < entitiesCount; ++i )
	{
		const box_bounds& aabb = entities.instAabbs[ i ];
		const XMFLOAT4X4A& mat = entities.transforms[ i ];

		std::array<XMFLOAT4, 8> boxVertices = TrnasformBoxVertices( XMLoadFloat4x4A( &mat ), aabb.min, aabb.max );

		dbg_vertex* boxLineRange = std::data( dbgGeom ) + i * boxLineVertexCount;
		constexpr XMFLOAT4 col = { 255.0f,255.0f,0.0f,1.0f };
		for( u64 ii = 0; ii < boxLineVertexCount; ++ii )
		{
			boxLineRange[ ii ] = { boxVertices[ boxLineIndices[ ii ] ],col };
		}
	}

	u64 frustBoxOffset = std::size( entities.instAabbs ) * boxLineVertexCount;

	std::array<XMFLOAT4, 8> boxVertices = TrnasformBoxVertices( frustDrawMat, { -1.0f,-1.0f,-1.0f }, { 1.0f,1.0f,1.0f } );

	dbg_vertex* boxLineRange = std::data( dbgGeom ) + frustBoxOffset;
	constexpr XMFLOAT4 frustCol = { 0.0f, 255.0f, 255.0f, 1.0f };
	for( u64 i = 0; i < boxLineVertexCount; ++i )
	{
		boxLineRange[ i ] = { boxVertices[ boxLineIndices[ i ] ],frustCol };
	}

	return dbgGeom;
}


inline void WrtieOcclusionBoxes( dbg_vertex* dbgTrisBuff )
{
	using namespace DirectX;
	XMMATRIX t = XMMatrixMultiply( XMMatrixScaling( 100.0f, 60.0f, 20.0f ), XMMatrixTranslation( 20.0f, -10.0f, -60.0f ) );
	std::array<XMFLOAT4, 8> boxVertices = TrnasformBoxVertices( t, { -1.0f,-1.0f,-1.0f }, { 1.0f,1.0f,1.0f } );
	std::span<dbg_vertex> occlusionBoxSpan = { dbgTrisBuff, boxTrisVertexCount };
	assert( std::size( occlusionBoxSpan ) == std::size( boxTrisIndices ) );
	for( u64 i = 0; i < std::size( occlusionBoxSpan ); ++i )
	{
		occlusionBoxSpan[ i ] = { boxVertices[ boxTrisIndices[ i ] ],{0.000004f,0.000250f,0.000123f,1.0f} };
	}
}

constexpr double RAND_MAX_SCALE = 1.0 / double( RAND_MAX );
// TODO: remove ?
inline static std::vector<instance_desc>
SpawnRandomInstances( const std::span<mesh_desc> meshes, u64 drawCount, u64 mtrlCount, float sceneRadius )
{
	using namespace DirectX;

	std::vector<instance_desc> insts( drawCount );
	float scale = 1.0f;
	for( instance_desc& i : insts )
	{
		i.meshIdx = rand() % std::size( meshes );
		i.mtrlIdx = 0;
		i.pos.x = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		i.pos.y = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		i.pos.z = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		i.scale = scale * float( rand() * RAND_MAX_SCALE ) + 2.0f;

		XMVECTOR axis = XMVector3Normalize(
			XMVectorSet( float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
						 float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
						 float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
						 0 ) );
		float angle = XMConvertToRadians( float( rand() * RAND_MAX_SCALE ) * 90.0f );

		//XMVECTOR quat = XMQuaternionRotationNormal( axis, angle );
		XMVECTOR quat = XMQuaternionIdentity();
		XMStoreFloat4( &i.rot, quat );

		XMMATRIX scaleM = XMMatrixScaling( i.scale, i.scale, i.scale );
		XMMATRIX rotM = XMMatrixRotationQuaternion( quat );
		XMMATRIX moveM = XMMatrixTranslation( i.pos.x, i.pos.y, i.pos.z );

		XMMATRIX localToWorld = XMMatrixMultiply( scaleM, XMMatrixMultiply( rotM, moveM ) );

		XMStoreFloat4x4A( &i.localToWorld, localToWorld );
	}

	return insts;
}
inline static std::vector<DirectX::XMFLOAT4X4A> SpawmRandomTransforms( u64 instCount, float sceneRadius, float globalScale )
{
	using namespace DirectX;

	std::vector<XMFLOAT4X4A> transf( instCount );
	for( XMFLOAT4X4A& t : transf )
	{
		float posX = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		float posY = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		float posZ = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		float scale = globalScale * float( rand() * RAND_MAX_SCALE ) + 2.0f;

		XMVECTOR axis = XMVector3Normalize(
			XMVectorSet( float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
						 float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
						 float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
						 0 ) );
		float angle = XMConvertToRadians( float( rand() * RAND_MAX_SCALE ) * 90.0f );

		//XMVECTOR quat = XMQuaternionRotationNormal( axis, angle );
		XMVECTOR quat = XMQuaternionIdentity();

		XMMATRIX scaleM = XMMatrixScaling( scale, scale, scale );
		XMMATRIX rotM = XMMatrixRotationQuaternion( quat );
		XMMATRIX moveM = XMMatrixTranslation( posX, posY, posZ );

		XMMATRIX localToWorld = XMMatrixMultiply( scaleM, XMMatrixMultiply( rotM, moveM ) );

		XMStoreFloat4x4A( &t, localToWorld );
	}

	return transf;
}
inline static std::vector<light_data> SpawnRandomLights( u64 lightCount, float sceneRadius )
{
	std::vector<light_data> lights( lightCount );
	for( light_data& l : lights )
	{
		l.pos.x = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		l.pos.y = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		l.pos.z = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		l.radius = 100.0f * float( rand() * RAND_MAX_SCALE ) + 2.0f;
		l.col = { 600.0f,200.0f,100.0f };
	}

	return lights;
}