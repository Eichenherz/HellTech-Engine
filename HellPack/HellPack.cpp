#include <meshoptimizer.h>

#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;

#include <atomic>
#include <thread>

#include <span>
#include <ranges>
#include <format>

#include <ankerl/unordered_dense.h>

#include <dds.h>

#include "ht_core_types.h"
#include "ht_error.h"

#include "zip_pack.h"


#include "ht_gfx_types.h"
#include <hell_pack.h>
#include <ht_serialization.h>
#include "ht_math.h"

#include "hp_encoding.h"
#include "hp_bcn_compression.h"

#include "gltf_loader.h"

#include "hp_types_internal.h"
#include "ht_vec_types.h"

#include <ht_macros.h>

#include <range_utils.h>

constexpr u32x3 CanonicallySortTriangleIndices( u32x3 t )
{
	if( t.x > t.y ) std::swap( t.x, t.y );
	if( t.y > t.z ) std::swap( t.y, t.z );
	if( t.x > t.y ) std::swap( t.x, t.y );
	return t;
}

bool LexicalLessThan( float3 a, float3 b )
{
	return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
}

template<typename TriIdx, typename PrimIdx>
inline std::vector<TriIdx> PermuteTrianglesByPrimitiveRemap(
	const std::vector<TriIdx>&	oldIdx,
	const std::vector<PrimIdx>& primitiveIndices
) {
	u64 triangleCount = std::size( primitiveIndices );
	HP_ASSERT( ( triangleCount * 3 ) == std::size( oldIdx ) );

	std::vector<TriIdx> newIdx( std::size( oldIdx ) );
	for( u64 ti = 0; ti < triangleCount; ++ti )
	{
		u64 oldTi = primitiveIndices[ ti ];
		u64 src = 3ull * oldTi;
		u64 dst = 3ull * ti;

		newIdx[ dst + 0 ] = oldIdx[ src + 0 ];
		newIdx[ dst + 1 ] = oldIdx[ src + 1 ];
		newIdx[ dst + 2 ] = oldIdx[ src + 2 ];
	}

	return newIdx;
}

template<typename Idx>
inline std::vector<Idx> BuildVertexRemapFromPermutedIndices( const std::vector<Idx>& permutedIndices, u64 vtxCount )
{
	constexpr Idx invalidIdx = Idx{ INVALID_IDX };

	HP_ASSERT( invalidIdx >= vtxCount );

	std::vector<Idx> remap( vtxCount, invalidIdx );
	u32 next = 0;

	for( Idx idx : permutedIndices )
	{
		Idx oldV = idx;
		if( invalidIdx == remap[ oldV ] )
		{
			remap[ oldV ] = next++;
		}
	}

	return remap;
}

void ValidateAndNormalizeRawMesh( raw_mesh& rawMesh )
{
	HT_ASSERT( std::size( rawMesh.pos ) > 0 );
	HT_ASSERT( std::size( rawMesh.indices ) != 0 );
	HT_ASSERT( rawMesh.materialIdx <= i32( u16( -1 ) ) );
	HT_ASSERT( ( std::size( rawMesh.indices ) % 3 ) == 0 );

	//DeduplicateTriangles( rawMesh );

	if( std::size( rawMesh.tans ) == 0 )
	{
		// NOTE: this only works because we reindex later on
		std::vector<float4> tangents( std::size( rawMesh.indices ) );
		meshopt_generateTangents( &tangents[ 0 ].x, &rawMesh.indices[ 0 ], std::size( rawMesh.indices ),
			&rawMesh.pos[ 0 ].x, std::size( rawMesh.pos ), sizeof( rawMesh.pos[ 0 ] ),
			&rawMesh.normals[ 0 ].x, sizeof( rawMesh.normals[ 0 ] ),
			&rawMesh.uvs[ 0 ].x, sizeof( rawMesh.uvs[ 0 ] ) );
		rawMesh.tans = MOV( tangents );
		// TODO: fix tan dedupe or smth
	}

	HT_ASSERT( ( std::size( rawMesh.pos ) == std::size( rawMesh.normals ) )
		//&& ( std::size( rawMesh.pos ) == std::size( rawMesh.tans ) )
		&& ( std::size( rawMesh.pos ) == std::size( rawMesh.uvs ) )
	);

	aabb_t<float3> meshAabb = ComputeAabb( rawMesh.pos );

	float3 ext = meshAabb.max - meshAabb.min;
	HT_ASSERT( std::isfinite( ext.x ) && std::isfinite( ext.y ) && std::isfinite( ext.z ) );
	HT_ASSERT( std::max( { ext.x, ext.y, ext.z } ) > 0.0f );

	rawMesh.aabb = meshAabb;
}

struct meshlet_config
{
	float   coneWeight		= 0.8f;
	u16		maxVertices		= RASTER_MAX_VTX_PER_MLT;
	u16		maxTriangles	= RASTER_MAX_TRIS_PER_MLT;
};

template<CONTIGUOUS_RANGE_T R>
inline meshopt_Stream MeshoptMakeStream( const R& range )
{
	HT_ASSERT( 0 != std::size( range ) );
	return {
		.data	= std::data( range ),
		.size	= sizeof( range[ 0 ] ),
		.stride = sizeof( range[ 0 ] )
	};
}

template<CONTIGUOUS_RANGE_T R>
inline void MeshoptRemapAttributeBufferInplace( R& attrRange, u64 attrElemCount, std::span<const u32> remap )
{
	HT_ASSERT( 0 != std::size( attrRange ) );
	meshopt_remapVertexBuffer( std::data( attrRange ),std::data( attrRange ),
		attrElemCount, sizeof( attrRange[ 0 ] ), std::data( remap ) );
}

// TODO: no inplace remap !
// NOTE: no cache optimization, buildMeshlets doesn't need it ( only buildMeshletsScan does )
// NOTE: no fetch optimization meshlets emit their own contiguous vertex slices into the global VB,
// which already gives optimal fetch locality
void MeshoptReindexAndOptimizeMesh( raw_mesh& rawMesh )
{
	meshopt_Stream attrStreams[] = {
		MeshoptMakeStream( rawMesh.pos ),
		MeshoptMakeStream( rawMesh.normals ),
		MeshoptMakeStream( rawMesh.tans ),
		MeshoptMakeStream( rawMesh.uvs )
	};
	std::vector<u32>& indices = rawMesh.indices;

	const u64 vtxCount = std::size( rawMesh.pos );
	const u64 idxCount = std::size( indices );

	std::vector<u32> remap( vtxCount );
	u64 newVtxCount = meshopt_generateVertexRemapMulti( std::data( remap ), std::data( indices ),
		idxCount, vtxCount, attrStreams, std::size( attrStreams ) );

	HT_ASSERT( newVtxCount <= vtxCount );
	meshopt_remapIndexBuffer( std::data( indices ), std::data( indices ), idxCount,
		std::data( remap ) );

	MeshoptRemapAttributeBufferInplace( rawMesh.pos, vtxCount, remap );
	MeshoptRemapAttributeBufferInplace( rawMesh.normals, vtxCount, remap );
	MeshoptRemapAttributeBufferInplace( rawMesh.tans, vtxCount, remap );
	MeshoptRemapAttributeBufferInplace( rawMesh.uvs, vtxCount, remap );
}

struct __meshopt_lod
{
	std::vector<u32>	indices;
	float				error;
};

inline std::vector<u8> MeshoptGenerateVtxUVLocksFromSimplification(
	u32						vtxCount,
	std::span<const u32>	remap,
	std::span<const float2> texCoords
) {
	HT_ASSERT( ( vtxCount <= std::size( remap ) ) && ( vtxCount <= std::size( texCoords ) ) );

	std::vector<u8> locks( vtxCount, 0 );
	for( u32 i = 0; i < vtxCount; ++i )
	{
		u32 r = remap[ i ];

		if( r != i && ( ( texCoords[ r ].x != texCoords[ i ].x ) || ( texCoords[ r ].y != texCoords[ i ].y ) ) )
		{
			locks[ i ] |= meshopt_SimplifyVertex_Protect;
		}
	}

	return locks;
}

std::vector<__meshopt_lod> MeshoptGenerateLODChain( const raw_mesh& rawMesh, u64 howManySimplifications )
{
	std::vector<__meshopt_lod> lodChain = {};
	lodChain.emplace_back( rawMesh.indices, 0.0f ); // NOTE: src is lod0 and has 0 error

	const u64 vtxCount = std::size( rawMesh.pos );

	std::vector<u32> remap( vtxCount );
	meshopt_generatePositionRemap( &remap[ 0 ], &rawMesh.pos[ 0 ].x, vtxCount, sizeof( rawMesh.pos[ 0 ] ) );

	// NOTE: protect from UV seams
	std::vector<u8> locks = MeshoptGenerateVtxUVLocksFromSimplification( ( u32 ) vtxCount, remap, rawMesh.uvs );

	constexpr float normalsWeight = 0.9f;
	constexpr float attrWeights[] = { normalsWeight, normalsWeight, normalsWeight };
	constexpr u32	options = meshopt_SimplifyErrorAbsolute | meshopt_SimplifyPermissive | meshopt_SimplifyPrune;

	float cumulativeErr = 0.0f;

	for( u64 li = 0; li < howManySimplifications; ++li )
	{
		const u32* pSrcLodLevel = &lodChain.back().indices[ 0 ];
		u64 srcIdxCount = std::size( lodChain.back().indices );

		float simplificationTarget = ( 0 == li ) ? ( 1.0f / 1.4f ) : 0.5f;
		u64 targetIdxCount = u64( simplificationTarget * ( float ) srcIdxCount );

		std::vector<u32> lod( srcIdxCount );
		float lodError = 0.0f;
		lod.resize( meshopt_simplifyWithAttributes( &lod[ 0 ], pSrcLodLevel, srcIdxCount,
			&rawMesh.pos[ 0 ].x, vtxCount, sizeof( rawMesh.pos[ 0 ] ),
			&rawMesh.normals[ 0 ].x, sizeof( rawMesh.normals[ 0 ] ),
			attrWeights, std::size( attrWeights ), /* vertex_lock= */ &locks[ 0 ],
			targetIdxCount, FLT_MAX, options, &lodError ) );

		if( ( std::size( lod ) >= srcIdxCount ) || ( 0 == std::size( lod ) ) ) break;

		cumulativeErr += lodError;

		lodChain.emplace_back( MOV( lod ), cumulativeErr );
	}

	return lodChain;
}

template<TRIVIAL_T T>
using mlt_attr_vector = fixed_vector<T, RASTER_MAX_VTX_PER_MLT>;

using mlt_idx_vector = fixed_vector<u8, RASTER_MAX_TRIS_PER_MLT * 3>;

template<TRIVIAL_T T>
inline mlt_attr_vector<T> GetMeshletLocalAttrStream(
	std::span<const T>		meshAttrStream,
	std::span<const u32>	mltVtx,
	u64						mltVtxOffset,
	u64						mltVtxCount
){
	mlt_attr_vector<T> localStream;
	localStream.resize( mltVtxCount );

	for( u64 vi = 0; vi < std::size( localStream ); ++vi )
	{
		localStream[ vi ] = meshAttrStream[ mltVtx[ vi + mltVtxOffset ] ];
	}

	return localStream;
}

struct __hp_meshlet
{
	mlt_attr_vector<float3>	pos		= {};
	mlt_attr_vector<float3>	norm	= {};
	mlt_attr_vector<float4>	tan 	= {};
	mlt_attr_vector<float2>	uvs 	= {};
	mlt_idx_vector			indices	= {};
	mlt_idx_vector			idxLod	= {};

	float					lodError;
	u16 					vtxCount;
};

// TODO: if we get meshlet weirdness we'd prolly need to protect some attrs during simplification
std::vector<__hp_meshlet> MeshoptMakeHpMeshletsWithLod(
	std::span<const float3> pos,
	std::span<const float3> norm,
	std::span<const float4> tan,
	std::span<const float2> uvs,
	std::span<const u32>	indices,
	float					parentMeshLodErr,
	meshlet_config			cfg
) {
	const u64 indexCount = std::size( indices );
	
	const u64 maxMeshletCount = meshopt_buildMeshletsBound( indexCount, cfg.maxVertices, cfg.maxTriangles );
	std::vector<meshopt_Meshlet> meshlets( maxMeshletCount );
	std::vector<u32> mltVtx( indexCount );
	std::vector<u8> mltTris( indexCount );

	u64 meshletCount = meshopt_buildMeshlets( &meshlets[ 0 ], &mltVtx[ 0 ], &mltTris[ 0 ], &indices[ 0 ],
		std::size( indices ), &pos[ 0 ].x, std::size( pos ), sizeof( pos[ 0 ] ),
		cfg.maxVertices, cfg.maxTriangles, cfg.coneWeight );

	HT_ASSERT( meshletCount < MAX_MESHLETS_PER_MESH );

	const meshopt_Meshlet& last = meshlets[ meshletCount - 1 ];

	meshlets.resize( meshletCount );
	mltVtx.resize( ( u64 ) last.vertex_offset + last.vertex_count );
	mltTris.resize( ( u64 ) last.triangle_offset + ( u64 ) last.triangle_count * 3 );


	std::vector<__hp_meshlet> outMlts = {};
	outMlts.reserve( std::size( meshlets ) );

	fixed_vector<u32, RASTER_MAX_TRIS_PER_MLT * 3> mltTempIndices32 = {}; // NOTE: bc we can't have simplify on u8
	fixed_vector<u32, RASTER_MAX_TRIS_PER_MLT * 3> mltTempLod = {};

	constexpr float normalsWeight = 0.9f;
	constexpr float attrWeights[] = { normalsWeight, normalsWeight, normalsWeight };

	for( u64 mi = 0; mi < std::size( meshlets ); ++mi )
	{
		const meshopt_Meshlet& m = meshlets[ mi ];
		HT_ASSERT( ( m.vertex_count <= u32( RASTER_MAX_VTX_PER_MLT ) ) && ( m.triangle_count <= u32( RASTER_MAX_TRIS_PER_MLT ) ) );

		meshopt_optimizeMeshlet( &mltVtx[ m.vertex_offset ], &mltTris[ m.triangle_offset ], m.triangle_count, m.vertex_count );

		mlt_attr_vector<float3>	localPos		= GetMeshletLocalAttrStream( pos, mltVtx, m.vertex_offset, m.vertex_count );
		mlt_attr_vector<float3>	localNorm		= GetMeshletLocalAttrStream( norm, mltVtx, m.vertex_offset, m.vertex_count );
		mlt_attr_vector<float2>	localUVs		= GetMeshletLocalAttrStream( uvs, mltVtx, m.vertex_offset, m.vertex_count );
		mlt_idx_vector			localIndices	= std::span{ &mltTris[ m.triangle_offset ], m.triangle_count * 3 };
		u64						localIdxCount	= std::size( localIndices );

		mltTempLod.resize( RASTER_MAX_TRIS_PER_MLT * 3 );
		mltTempIndices32 = { std::from_range, localIndices | std::views::transform( HtCastTo<u32> ) };
		float lodError = 0.0f;

		constexpr u32 simplifierOptions = meshopt_SimplifyLockBorder | meshopt_SimplifyErrorAbsolute | meshopt_SimplifyPermissive;
		// NOTE: for mesh-shaders it might be worth it to reorder LOD1's vertices/ triangles to come first in the buffer
		mltTempLod.resize( meshopt_simplifyWithAttributes( &mltTempLod[ 0 ], &mltTempIndices32[ 0 ], localIdxCount,
			&localPos[ 0 ].x, std::size( localPos ), sizeof( localPos[ 0 ] ),
			&localNorm[ 0 ].x, sizeof( localNorm[ 0 ] ), attrWeights,
			std::size( attrWeights ), nullptr,
			u64( ( float ) localIdxCount * 0.5f ), FLT_MAX, simplifierOptions, &lodError ) );

		mlt_idx_vector lodMltIndices = {};
		if( std::size( mltTempLod ) < localIdxCount )
		{
			lodMltIndices = { std::from_range, mltTempLod | std::views::transform( HtCastTo<u8> ) };
		}

		outMlts.push_back( {
			.pos		= MOV( localPos ),
			.norm		= MOV( localNorm ),
			.tan		= GetMeshletLocalAttrStream( tan, mltVtx, m.vertex_offset, m.vertex_count ),
			.uvs		= MOV( localUVs ),
			.indices	= MOV( localIndices ),
			.idxLod		= MOV( lodMltIndices ),
			.lodError	= ( std::size( mltTempLod ) < localIdxCount ) ? parentMeshLodErr + lodError : FLT_MAX,
			.vtxCount	= ( u16 ) m.vertex_count
		} );
	}

	return outMlts;
}

std::vector<packed_vtx_attr> HpkMeshletPackVtxAttributes( const __hp_meshlet& mlt )
{
	std::vector<packed_vtx_attr> packedVtxAttrs( mlt.vtxCount );
	for( u64 vai = 0; vai < mlt.vtxCount; ++vai )
	{
		float3 n	= mlt.norm[ vai ];
		float4 t	= mlt.tan[ vai ];
		float2 uv	= mlt.uvs[ vai ];

		packedVtxAttrs[ vai ] = {
			.encodedTBN = EncodeTanFrame( n, { t.x, t.y, t.z }, t.w ),
			.encodedUVs = { meshopt_quantizeHalf( uv.x ), meshopt_quantizeHalf( uv.y ) }
		};
	}

	return packedVtxAttrs;
}

struct mlt_quantized_grid
{
	static constexpr u32	gridResolutionInBits	= 21;
	static constexpr u32	gridStep				= 1u << gridResolutionInBits;
	static constexpr float	meshGridQuantMaxErr		= 0.5f / gridStep; // NOTE: 1/2 bc we round !

	float3	quantAabbMin;
	float3	quantAabbMax;
	u32x3	bitDepthPerAxis;
	i32x3	anchor;
};

mlt_quantized_grid HpkMakeMltQuantizedGrid( aabb_t<float3> meshletAabb )
{
	constexpr u32 gridStep = mlt_quantized_grid::gridStep;

	i32 minMltX = ( i32 ) std::floor( meshletAabb.min.x * gridStep );
	i32 minMltY = ( i32 ) std::floor( meshletAabb.min.y * gridStep );
	i32 minMltZ = ( i32 ) std::floor( meshletAabb.min.z * gridStep );

	i32 maxMltX = ( i32 ) std::ceil( meshletAabb.max.x * gridStep );
	i32 maxMltY = ( i32 ) std::ceil( meshletAabb.max.y * gridStep );
	i32 maxMltZ = ( i32 ) std::ceil( meshletAabb.max.z * gridStep );

	u32x3 mltBitDepthPerAxis = {
		( u32 ) std::bit_width<u32>( ( u32 ) std::abs( maxMltX - minMltX ) ),
		( u32 ) std::bit_width<u32>( ( u32 ) std::abs( maxMltY - minMltY ) ),
		( u32 ) std::bit_width<u32>( ( u32 ) std::abs( maxMltZ - minMltZ ) )
	};
	HT_ASSERT( u32x3{} != mltBitDepthPerAxis );

	return {
		.quantAabbMin		= { float( minMltX ) / gridStep, float( minMltY ) / gridStep, float( minMltZ ) / gridStep },
		.quantAabbMax		= { float( maxMltX ) / gridStep, float( maxMltY ) / gridStep, float( maxMltZ ) / gridStep },
		.bitDepthPerAxis	= mltBitDepthPerAxis,
		.anchor				= { minMltX, minMltY, minMltZ }
	};
}

u32x3 HpkEncodeMltVertexPosition( const mlt_quantized_grid& grid, float3 p )
{
	return {
		QuantizeVertexPosCompWithAnchor( p.x, grid.anchor.x, grid.gridResolutionInBits ),
		QuantizeVertexPosCompWithAnchor( p.y, grid.anchor.y, grid.gridResolutionInBits ),
		QuantizeVertexPosCompWithAnchor( p.z, grid.anchor.z, grid.gridResolutionInBits )
	};
}

bool HpkDecodeVerifyQuantized( float3 pos, u32x3 encPos, const mlt_quantized_grid& grid )
{
	float decX = DecodeVertexPosCompWithAnchor( encPos.x, grid.gridStep, grid.anchor.x, grid.bitDepthPerAxis.x );
	float decY = DecodeVertexPosCompWithAnchor( encPos.y, grid.gridStep, grid.anchor.y, grid.bitDepthPerAxis.y );
	float decZ = DecodeVertexPosCompWithAnchor( encPos.z, grid.gridStep, grid.anchor.z, grid.bitDepthPerAxis.z );

	float3 quantErr = { std::fabsf( pos.x - decX ), std::fabsf( pos.y - decY ), std::fabsf( pos.z - decZ ) };
	float3 maxQuantErr = { grid.meshGridQuantMaxErr, grid.meshGridQuantMaxErr, grid.meshGridQuantMaxErr };

	return quantErr <= maxQuantErr;
}

constexpr bool validatePosEncoding = true;

constexpr u64 LODS_PER_MESHLET = 2;

// NOTE: vtx quant from https://daniilvinn.github.io/2024/05/04/omniforce-vertex-quantization.html
void HpkQuantizeAndAppendLODLevel(
	const std::vector<__hp_meshlet>&	meshoptMeshlets,
	bit_stream&							vtxPosBitstream,
	std::vector<packed_vtx_attr>&		verticesAttrs,
	std::vector<u8>&					indices,
	std::vector<gpu_meshlet>&			meshlets
) {
	const u64 mltCount = std::size( meshoptMeshlets );
	// NOTE: reserve max cap
	verticesAttrs.reserve( std::size( verticesAttrs ) + mltCount * RASTER_MAX_VTX_PER_MLT );
	indices.reserve( std::size( indices ) + mltCount * RASTER_MLT_MAX_INDEX * LODS_PER_MESHLET );
	meshlets.reserve( std::size( meshlets ) + mltCount );

	for( u64 mi = 0; mi < mltCount; ++mi )
	{
		const __hp_meshlet& m = meshoptMeshlets[ mi ];
		const aabb_t<float3> meshletAabb = ComputeAabb( m.pos );

		mlt_quantized_grid mltEncodingGrid = HpkMakeMltQuantizedGrid( meshletAabb );

		u32 packed8888_XYZ_Grid_BitDepth = mltEncodingGrid.bitDepthPerAxis.x
			| ( mltEncodingGrid.bitDepthPerAxis.y << 8 )
			| ( mltEncodingGrid.bitDepthPerAxis.z << 16 )
			| ( mltEncodingGrid.gridResolutionInBits << 24 );

		u32 packed8_12_12_VtxCount_Lod_01_IdxCount = u32( m.vtxCount )
			| ( u32( std::size( m.indices ) ) << 8 )
			| ( u32( std::size( m.idxLod ) ) << 20 );

		HT_ASSERT( (  m.vtxCount < 256 ) &&
			( std::size( m.idxLod ) < RASTER_MLT_MAX_INDEX ) &&
			( std::size( m.indices ) < RASTER_MLT_MAX_INDEX ) );

		meshlets.push_back( {
			.aabbMin								= mltEncodingGrid.quantAabbMin,
			.aabbMax								= mltEncodingGrid.quantAabbMax,
			.vtxPosOffsetBits						= ( u32 ) vtxPosBitstream.cursorInBits,
			.vtxAttrsOffset							= ( u32 ) std::size( verticesAttrs ),
			.idxOffset								= ( u32 ) std::size( indices ),
			.packed8888_XYZ_Grid_BitDepth			= packed8888_XYZ_Grid_BitDepth,
			.packed8_12_12_VtxCount_Lod_01_IdxCount	= packed8_12_12_VtxCount_Lod_01_IdxCount,
			.lodError								= m.lodError
		} );

		for( float3 p : m.pos )
		{
			u32x3 enc = HpkEncodeMltVertexPosition( mltEncodingGrid, p );

			vtxPosBitstream.AppendBits( enc.x, mltEncodingGrid.bitDepthPerAxis.x );
			vtxPosBitstream.AppendBits( enc.y, mltEncodingGrid.bitDepthPerAxis.y );
			vtxPosBitstream.AppendBits( enc.z, mltEncodingGrid.bitDepthPerAxis.z );

			if constexpr( validatePosEncoding )
			{
				HT_ASSERT( HpkDecodeVerifyQuantized( p, enc, mltEncodingGrid ) );
			}
		}

		verticesAttrs.append_range( HpkMeshletPackVtxAttributes( m ) );
		indices.append_range( m.indices );
		if( std::size( m.idxLod ) )
		{
			indices.append_range( m.idxLod );
		}

	}
}

using position_t = float3;

using dds_texture = std::vector<u8>;

constexpr bc_format_t DxgiToBcFormat( dds::DXGI_FORMAT dxgiFmt )
{
	using namespace dds;
	switch( dxgiFmt )
	{
	case DXGI_FORMAT_BC5_TYPELESS:
	case DXGI_FORMAT_BC5_UNORM:
	case DXGI_FORMAT_BC5_SNORM:
		return bc_format_t::BC5_RG;

	case DXGI_FORMAT_BC7_TYPELESS:
	case DXGI_FORMAT_BC7_UNORM:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		return bc_format_t::BC7_RGBA;

	default:
		HT_ASSERT( 0 && "Unimplement fmt" );
		return ( bc_format_t ) 0xFF;
	}
}

struct compression_job
{
	alignas( 8 ) vfs_path	filename;
	dds_texture				tex;
	std::span<const u8> 	src;
	dds::DXGI_FORMAT		fmt;
	u16						width;
	u16						height;

	void Execute()
	{
		bc_format_t bcnFmt = DxgiToBcFormat( fmt );
		// NOTE: these allocate memory !
		bcn_compression_result bcn = CompressRGBA8ToBCn( src, width, height, bcnFmt );

		tex.resize( sizeof( dds::Header ) + std::size( bcn.data ) );
		dds::write_header( &tex[ 0 ], fmt, width, height );
		std::memcpy( &tex[ 0 ] + sizeof( dds::Header ), &bcn.data[ 0 ], std::size( bcn.data ) );
	}
};

struct materials_jobs
{
	std::vector<material_desc>   materials;
	std::vector<compression_job> jobs;
};

materials_jobs PrepareBcnCompressionBatch(
	std::span<const raw_material_info>	rawMaterials,
	std::span<const raw_image_view>		imageViews
) {
	HT_ASSERT( std::size( imageViews ) < u16( INVALID_IDX ) );

	// NOTE: we use indices and vfs_path here bc we're deduping wrt to tinygltf's stuff which is index based
	ankerl::unordered_dense::set<u16>	jobsSet;
	std::vector<compression_job>		jobs;

	jobsSet.reserve( std::size( imageViews ) );
	jobs.reserve( std::size( imageViews ) );

	auto ProcessImageView = [ & ]( u16 idx, dds::DXGI_FORMAT fmt, const vfs_path& filename ) -> u64
	{
		if( !IsIndexValid( idx ) ) return {};
		if( std::cend( jobsSet ) == jobsSet.find( idx ) )
		{
			const raw_image_view& imgView = imageViews[ idx ];
			HT_ASSERT( std::size( imgView.data ) );

			jobsSet.emplace( idx );

			jobs.push_back( {
				.filename	= filename,
				.src		= imgView.data,
				.fmt		= fmt,
				.width		= imgView.metadata.width,
				.height		= imgView.metadata.height
			} );
		}
		
		return std::hash<vfs_path>{}( filename );
	};

	std::vector<material_desc> materials;
	materials.reserve( std::size( rawMaterials ) );
	// NOTE: GLTF conventions
	for( const raw_material_info& mtrl : rawMaterials )
	{
		//ProcessImageView( material.occlusionIdx, bc_format_t::BC7_RGBA );
		// NOTE: currently not supporting ambient occlusion which must be packed into MR
		//HT_ASSERT( !IsIndexValid( mtrl.occlusionIdx ) );

		u64 baseColorHash = ProcessImageView( mtrl.baseColorIdx, dds::DXGI_FORMAT_BC7_UNORM_SRGB, { "{}_albedo.dds", mtrl.name } );
		u64 normalHash = ProcessImageView( mtrl.normalIdx, dds::DXGI_FORMAT_BC5_UNORM, { "{}_normal.dds", mtrl.name } );
		u64 metallicRoughnessHash = ProcessImageView( mtrl.metallicRoughnessIdx, dds::DXGI_FORMAT_BC7_UNORM, { "{}_mro.dds", mtrl.name } );
		u64 emissiveHash = ProcessImageView( mtrl.emissiveIdx, dds::DXGI_FORMAT_BC7_UNORM_SRGB, { "{}_emissive.dds", mtrl.name } );

		materials.push_back( {
			.baseColorHash			= baseColorHash,
			.metallicRoughnessHash	= metallicRoughnessHash,
			.normalHash				= normalHash,
			.emissiveHash			= emissiveHash,

			.baseColFactor			= mtrl.baseColFactor,
			.emissiveFactor			= mtrl.emissiveFactor,
			.metallicFactor			= mtrl.metallicFactor,
			.roughnessFactor		= mtrl.roughnessFactor,

			.alphaCutoff			= mtrl.alphaCutoff,

			.samplerIdx				= mtrl.samplerIdx,

			.alphaMode				= mtrl.alphaMode
		} );
	}

	return { .materials = MOV( materials ), .jobs = MOV( jobs ) };
}

inline void WaitThreadPoolDone( std::vector<std::thread>& threadPool )
{
	for( auto& t : threadPool ) t.join();
}

constexpr bool CHECK_CORRECTNESS = true;

i32 main( i32 argc, char** argv  )
{
	if( argc < 3 )
	{
		std::cout << "Missing arguments\n";
		return 1;
	}

	const std::string gltfFilePath = argv[ 1 ];
	const std::string hpkFilePath = argv[ 2 ];

	HT_ASSERT( fs::exists( gltfFilePath ) );

	gltf_loader gltf = { gltfFilePath.c_str() };

	// TODO: ensure we keep the same indexing as cgltf provides !!!!
	std::vector<raw_node>			rawNodes		= gltf.ProcessDrawableNodes();
	std::vector<raw_mesh>			rawMeshes		= gltf.ProcessMeshes();
	std::vector<sampler_config>		samplers		= gltf.ProcessSamplers();
	//std::vector<raw_material_info>	rawMaterials	= gltf.ProcessMaterials();
	std::vector<raw_image_view>		imageViews		= gltf.ProcessImages();

	//auto[ materialTable, texCmpJobs ] = PrepareBcnCompressionBatch( rawMaterials, imageViews );

	std::vector<std::thread> tasks;
	std::atomic<u32> taskCounter = { 0 };

	//auto WorkerLoop = [ & ]()
	//{
	//	for( ;; )
	//	{
	//		u32 currentJobIdx = taskCounter.fetch_add( 1 );
	//		if( currentJobIdx >= std::size( texCmpJobs ) ) return;
	//
	//		texCmpJobs[ currentJobIdx ].Execute();
	//	}
	//};

	std::cout << "Processing materials async\n";

	//for( u64 ti = 0; ti < std::thread::hardware_concurrency(); ++ti ) tasks.emplace_back( WorkerLoop );

	ankerl::unordered_dense::map<vfs_path, hpk_mesh_asset> meshAssetMap;

	std::cout << "Processing meshes\n";
	for( raw_mesh& mesh : rawMeshes )
	{
		vfs_path assetPath = { "{}{}.mesh", HELLPACK_MESH_DIR, std::data( mesh.name ) };
		HT_ASSERT( !meshAssetMap.contains( assetPath ) );

		ValidateAndNormalizeRawMesh( mesh );
		MeshoptReindexAndOptimizeMesh( mesh );

		std::vector<__meshopt_lod> meshLods = MeshoptGenerateLODChain( mesh, MAX_LOD_LEVELS_COUNT - 1 );

		float lodErr[ MAX_LOD_LEVELS_COUNT ] = { FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
		u32 lodMltNum[ MAX_LOD_LEVELS_COUNT ] = {};

		bit_stream							vtxPosBitstream;
		std::vector<packed_vtx_attr>		verticesAttrs;
		std::vector<index_t>				indices;
		std::vector<gpu_meshlet>			meshlets;

		for( u64 li = 0; li < std::size( meshLods ); li++ )
		{
			const __meshopt_lod& lod = meshLods[ li ];
			// NOTE: the lod errors are "global" per object
			std::vector<__hp_meshlet> lodMeshlets = MeshoptMakeHpMeshletsWithLod( mesh.pos, mesh.normals, mesh.tans,
				mesh.uvs, lod.indices, lod.error, {} );

			HT_ASSERT( std::size( lodMeshlets ) < MAX_MESHLETS_PER_MESH );
			lodMltNum[ li ] = u32( std::size( lodMeshlets ) );
			lodErr[ li ] = lod.error;

			HpkQuantizeAndAppendLODLevel( lodMeshlets, vtxPosBitstream, verticesAttrs, indices, meshlets );
		}

		HT_ASSERT( ( 0 != std::ranges::size( vtxPosBitstream ) ) &&
			( 0 != std::ranges::size( verticesAttrs ) ) &&
			( 0 != std::ranges::size( indices ) ) &&
			( 0 != std::ranges::size( meshlets ) ) );

		HT_ASSERT( 4 == MAX_LOD_LEVELS_COUNT );
		hpk_mesh_asset meshAsset = {
			.vtxPosBitstream			= MOV( vtxPosBitstream ),
			.vertexAttrs				= MOV( verticesAttrs ),
			.indices					= MOV( indices ),
			.meshlets					= MOV( meshlets ),
			.aabb						= { mesh.aabb.min, mesh.aabb.max },
			.lodErrors					= { lodErr[ 0 ], lodErr[ 1 ], lodErr[ 2 ], lodErr[ 3 ] },
			.packed16x4_lodMltCounts	= {
				lodMltNum[ 0 ] | ( lodMltNum[ 1 ] << 16 ),
				lodMltNum[ 2 ] | ( lodMltNum[ 3 ] << 16 )
			}
		};

		meshAssetMap.emplace( assetPath, std::move( meshAsset ) );
	}


	std::vector<world_node> worldNodes;
	worldNodes.reserve( std::size( rawNodes ) );

	for( const raw_node& n : rawNodes )
	{
		if( !IsIndexValid( n.meshIdx ) ) continue;

		raw_mesh& mesh = rawMeshes[ ( u32 ) n.meshIdx ];

		vfs_path assetPath = { "{}{}.mesh", HELLPACK_MESH_DIR, std::data( mesh.name ) };

		worldNodes.push_back( {
			.toWorld		= { .t = n.toWorld.t, .pad0 = 0, .r = n.toWorld.r, .s = n.toWorld.s, .pad1 = 0 },
			.meshHash		= std::hash<vfs_path>{}( assetPath ),
			.materialIdx	= ( u16 ) mesh.materialIdx // NOTE: these should match 1:1 with ours
		} );
	}

	std::cout << "Processing meshes & nodes done ! Dumping to file.\n";

	{
		zip_writer zipArchive = { hpkFilePath.c_str() };

		HT_ASSERT( fs::exists( hpkFilePath ) );

		{
			hpk_level_asset level = { .nodes = MOV( worldNodes ) };//, .materials = MOV( materialTable ) };
			std::vector<u8> bytes = HpkSerializeAsset( level );
			zipArchive.WriteBytesToFile( { "world.lvl" }, bytes );
		}
		{
			for( auto& [ filePath, meshAsset ] : meshAssetMap )
			{
				std::vector<u8> bytes = HpkSerializeAsset( meshAsset );
				zipArchive.WriteBytesToFile( filePath, bytes );
			}
		}
		//WaitThreadPoolDone( tasks );
		//
		//std::cout << "Processing materials done ! Dumping to file.\n";
		//for( const compression_job& cmp : texCmpJobs )
		//{
		//	HT_ASSERT( std::size( cmp.tex ) );
		//	vfs_path texPath = { "{}{}", HELLPACK_TEX_DIR, std::data( cmp.filename ) };
		//	zipArchive.WriteBytesToFile( texPath, cmp.tex );
		//}
	}

	/*
	if constexpr( CHECK_CORRECTNESS )
	{
		const std::vector<u8> rawBytes = ReadFileBinary( hpkFilePath.c_str() );

		vfs_zip_mem vfsZipMem = { rawBytes };

		const auto& [ key, val ] = *std::cbegin( meshAssetMap );

		std::vector<u8> mesh0Bin( vfsZipMem.GetFileSizeInBytes( key ), 0 );
		HT_ASSERT( vfsZipMem.ReadFileToBufferNoAlloc( key, std::data( mesh0Bin ), std::size( mesh0Bin ) ) );

		const hellpack_mesh_asset hpkMeshAsset = HpkReadBinaryBlob<hellpack_mesh_asset>( mesh0Bin );

		HT_ASSERT( ByteEqual( MakeByteView( hpkMeshAsset.vertices ), MakeByteView( val.vertices ) ) );
		HT_ASSERT( ByteEqual( MakeByteView( hpkMeshAsset.triangles ), MakeByteView( val.triangles ) ) );
		HT_ASSERT( ByteEqual( MakeByteView( hpkMeshAsset.meshlets ), MakeByteView( val.meshlets ) ) );

		HT_ASSERT( hpkMeshAsset.aabbMin == val.aabb[ 0 ] );
		HT_ASSERT( hpkMeshAsset.aabbMax == val.aabb[ 1 ] );
	}
	*/
	return 0;
}

