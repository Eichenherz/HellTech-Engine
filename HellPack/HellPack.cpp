#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;

#include <atomic>
#include <thread>
#include <barrier>
#include <print>
#include <span>
#include <ranges>
#include <format>

#include <ankerl/unordered_dense.h>

#include <dds.h>

#include <ht_core_types.h>
#include <ht_error.h>

#include "zip_pack.h"


#include <ht_gfx_types.h>
#include <hell_pack.h>
#include <ht_serialization.h>
#include <ht_math.h>
#include <ht_ring_buffer.h>
#include <System/sys_file.h>

#include "hp_encoding.h"
#include "hp_bcn_compression.h"

#include "gltf_loader.h"

#include "hp_types_internal.h"
#include <ht_vec_types.h>

#include <ht_macros.h>

#include <range_utils.h>

#include "hpk_meshopt_pipeline.h"

static const u64 g_ThreadCount = std::thread::hardware_concurrency();

thread_local static virtual_arena g_ThreadArena[ 2 ] = { { 4 * GB }, { 4 * GB } };
thread_local static virtual_arena g_ThreadExtLibArena = { 8 * GB };

inline void* MeshoptScratchAlloc( size_t szInBytes ) { return g_ThreadExtLibArena.Alloc( szInBytes, 16 ); }
inline void MeshoptScratchFree( void* ) {}

inline void* CgltfArenaAlloc( void*, cgltf_size szInBytes ) { return g_ThreadExtLibArena.Alloc( szInBytes, 16 ); }
inline void CgltfArenaFree( void*, void* ) {}

inline cgltf_result
HtCgltfFileRead( const cgltf_memory_options*, const cgltf_file_options*, const char*, cgltf_size*, void** )
{
    HT_ASSERT( 0 && "glb only, no external files" );
    return cgltf_result_io_error;
}
inline void HtCgltfFileRelease( const cgltf_memory_options*, const cgltf_file_options*, void*, cgltf_size )
{
    HT_ASSERT( 0 && "glb only, no external files" );
}



constexpr u32x3 CanonicallySortTriangleIndices( u32x3 t )
{
	if( t.x > t.y ) std::swap( t.x, t.y );
	if( t.y > t.z ) std::swap( t.y, t.z );
	if( t.x > t.y ) std::swap( t.x, t.y );
	return t;
}

template<typename TriIdx, typename PrimIdx>
std::vector<TriIdx> PermuteTrianglesByPrimitiveRemap(
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
std::vector<Idx> BuildVertexRemapFromPermutedIndices( const std::vector<Idx>& permutedIndices, u64 vtxCount )
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

std::vector<float3> GenerateSmoothNormals( std::span<const float3> pos, std::span<const u32> indices )
{
    std::vector<float3> normals( std::size( pos ), {} );
    for( u64 triIdx = 0; triIdx < std::size( indices ); triIdx += 3 )
    {
        u32 vtx0 = indices[ triIdx + 0 ];
        u32 vtx1 = indices[ triIdx + 1 ];
        u32 vtx2 = indices[ triIdx + 2 ];

        float3 pos0 = pos[ vtx0 ];
        float3 pos1 = pos[ vtx1 ];
        float3 pos2 = pos[ vtx2 ];

        float3 faceNormal = CrossProd( pos1 - pos0, pos2 - pos0 );
        normals[ vtx0 ] += faceNormal;
        normals[ vtx1 ] += faceNormal;
        normals[ vtx2 ] += faceNormal;
    }

    std::ranges::for_each( normals, []( float3& n ) { n = Normalize( n ); } );
    return normals;
}

template<arena_t ARENA_T>
arena_array<float3, ARENA_T> GenerateSmoothNormals( std::span<const float3> pos, std::span<const u32> indices, ARENA_T& arena )
{
    arena_array<float3, ARENA_T> normals = { arena, std::from_range, std::views::repeat( float3{}, std::size( pos ) ) };
    for( u64 triIdx = 0; triIdx < std::size( indices ); triIdx += 3 )
    {
        u32 vtx0 = indices[ triIdx + 0 ];
        u32 vtx1 = indices[ triIdx + 1 ];
        u32 vtx2 = indices[ triIdx + 2 ];

        float3 pos0 = pos[ vtx0 ];
        float3 pos1 = pos[ vtx1 ];
        float3 pos2 = pos[ vtx2 ];

        float3 faceNormal = CrossProd( pos1 - pos0, pos2 - pos0 );
        normals[ vtx0 ] += faceNormal;
        normals[ vtx1 ] += faceNormal;
        normals[ vtx2 ] += faceNormal;
    }

    std::ranges::for_each( normals, []( float3& n ) { n = Normalize( n ); } );
    return normals;
}

void ValidateAndNormalizeRawMesh( raw_mesh& rawMesh )
{
	HT_ASSERT( std::size( rawMesh.pos ) > 0 );
	HT_ASSERT( std::size( rawMesh.indices ) != 0 );
	//HT_ASSERT( rawMesh.materialIdx <= i32( u16( -1 ) ) );
	HT_ASSERT( ( std::size( rawMesh.indices ) % 3 ) == 0 );

	//DeduplicateTriangles( rawMesh );

    if( std::size( rawMesh.normals ) == 0 )
    {
        rawMesh.normals = GenerateSmoothNormals( rawMesh.pos, rawMesh.indices );
    }

    /*
	if( std::size( rawMesh.tans ) == 0 )
	{
		u64 triCornerCount = std::size( rawMesh.indices );

		std::vector<float4> tangents( triCornerCount );
		meshopt_generateTangents( &tangents[ 0 ].x, &rawMesh.indices[ 0 ], triCornerCount,
			&rawMesh.pos[ 0 ].x, std::size( rawMesh.pos ),
			sizeof( rawMesh.pos[ 0 ] ), &rawMesh.normals[ 0 ].x,
			sizeof( rawMesh.normals[ 0 ] ), &rawMesh.uvs[ 0 ].x,
			sizeof( rawMesh.uvs[ 0 ] ) );

		auto LmbdDeindex = [ & ]( const auto& stream )
		{
		    return rawMesh.indices | std::views::transform( [ & ]( u32 i ) { return stream[ i ]; } );
		};

	    // NOTE: the tangents are served per corner vtx[ idxBuf[ triCornerIdx ] ]
	    // so we need to flatten the other attrs too; this is fine as we reindex later
		rawMesh.pos     = { std::from_range, LmbdDeindex( rawMesh.pos ) };
		rawMesh.normals = { std::from_range, LmbdDeindex( rawMesh.normals ) };
		rawMesh.uvs     = { std::from_range, LmbdDeindex( rawMesh.uvs ) };
		rawMesh.tans    = MOV( tangents );
		rawMesh.indices = { std::from_range, std::views::iota( 0u, ( u32 ) triCornerCount ) };
	}

	HT_ASSERT( ( std::size( rawMesh.pos ) == std::size( rawMesh.normals ) )
		&& ( std::size( rawMesh.pos ) == std::size( rawMesh.tans ) )
		&& ( std::size( rawMesh.pos ) == std::size( rawMesh.uvs ) )
	);
    */

	//float3 ext = meshAabb.max - meshAabb.min;
	//HT_ASSERT( std::isfinite( ext.x ) && std::isfinite( ext.y ) && std::isfinite( ext.z ) );
	//HT_ASSERT( std::max( { ext.x, ext.y, ext.z } ) > 0.0f );

	//rawMesh.aabb = meshAabb;
}

struct meshlet_config
{
	float   coneWeight		= 0.8f;
	u16		maxVertices		= RASTER_MAX_VTX_PER_MLT;
	u16		maxTriangles	= RASTER_MAX_TRIS_PER_MLT;
};

constexpr float LOD_MESH_LEVEL_RATIO = 0.25f;
constexpr u64   LODS_PER_MESHLET = 2; // NOTE: includes the src/lod0

template<TRIVIAL_T T>
using mlt_attr_vector = inline_array<T, RASTER_MAX_VTX_PER_MLT>;

using mlt_idx_vector = inline_array<u8, RASTER_MAX_TRIS_PER_MLT * 3>;
using mlt_idx_vector32 = inline_array<u32, RASTER_MAX_TRIS_PER_MLT * 3>;

template<TRIVIAL_T T>
mlt_attr_vector<T> GetMeshletLocalAttrStream(
	std::span<const T>		meshAttrStream,
	std::span<const u32>	mltVtx,
	u64						mltVtxOffset,
	u64						mltVtxCount
){
	return { std::from_range, mltVtx.subspan( mltVtxOffset, mltVtxCount )
		| std::views::transform( [ meshAttrStream ]( u32 vi ) { return meshAttrStream[ vi ]; } ) };
}

struct hpk_meshlet
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

constexpr float HPK_MESHOPT_NORMAL_WEIGHT   = 0.9f;
constexpr float HPK_MESHOPT_ATTR_WEIGHTS[]  = {
    HPK_MESHOPT_NORMAL_WEIGHT, HPK_MESHOPT_NORMAL_WEIGHT, HPK_MESHOPT_NORMAL_WEIGHT
};

hpk_meshlet MeshoptSimplyfyMeshlet(
    const meshopt_Meshlet&  mlt,
    std::span<u32>          mltVtx,
    std::span<u8>           mltTris,
    std::span<const float3> pos,
    std::span<const float3> norm,
    float                   parentMeshLodErr
) {
    constexpr u32 mltLodOpts = meshopt_SimplifyLockBorder | meshopt_SimplifyErrorAbsolute | meshopt_SimplifyPermissive;

    HT_ASSERT( ( mlt.vertex_count <= u32( RASTER_MAX_VTX_PER_MLT ) )
        && ( mlt.triangle_count <= u32( RASTER_MAX_TRIS_PER_MLT ) ) );

    meshopt_optimizeMeshlet( &mltVtx[ mlt.vertex_offset ], &mltTris[ mlt.triangle_offset ], mlt.triangle_count, mlt.vertex_count );

    mlt_attr_vector<float3>	localPos		= GetMeshletLocalAttrStream( pos, mltVtx, mlt.vertex_offset, mlt.vertex_count );
    mlt_attr_vector<float3>	localNorm		= GetMeshletLocalAttrStream( norm, mltVtx, mlt.vertex_offset, mlt.vertex_count );
    //mlt_attr_vector<float2>	localUVs		= GetMeshletLocalAttrStream( uvs, mltVtx, m.vertex_offset, m.vertex_count );
    mlt_idx_vector			localIdx	    = std::span{ &mltTris[ mlt.triangle_offset ], mlt.triangle_count * 3 };

    mlt_idx_vector32        mltTempLod      = { RASTER_MAX_TRIS_PER_MLT * 3, 0 };
    // NOTE: bc we can't have simplify on u8
    mlt_idx_vector32        mltTempIdx32    = { std::from_range, localIdx | std::views::transform( HtCastTo<u32> ) };
    float                   lodError        = 0.0f;

    // NOTE: for mesh-shaders it might be worth it to reorder LOD1's vertices/ triangles to come first in the buffer
    mltTempLod.resize( meshopt_simplifyWithAttributes( &mltTempLod[ 0 ], &mltTempIdx32[ 0 ],
        std::size( localIdx ), &localPos[ 0 ].x, std::size( localPos ),
        sizeof( localPos[ 0 ] ), &localNorm[ 0 ].x,
        sizeof( localNorm[ 0 ] ), HPK_MESHOPT_ATTR_WEIGHTS,
        std::size( HPK_MESHOPT_ATTR_WEIGHTS ), nullptr,
        u64( ( float ) std::size( localIdx ) * 0.5f ), FLT_MAX,
        mltLodOpts, &lodError ) );

    return {
        .pos		= MOV( localPos ),
        .norm		= MOV( localNorm ),
        //.tan		= GetMeshletLocalAttrStream( tan, mltVtx, m.vertex_offset, m.vertex_count ),
        //.uvs		= MOV( localUVs ),
        .indices	= MOV( localIdx ),
        // NOTE: the "do we have an LOD level here" is decided through FLT_MAX == lodError
        .idxLod		= { std::from_range, mltTempLod | std::views::transform( HtCastTo<u8> ) },
        .lodError	= ( std::size( mltTempLod ) < std::size( localIdx ) ) ? parentMeshLodErr + lodError : FLT_MAX,
        .vtxCount	= ( u16 ) mlt.vertex_count
    };
}

struct hpk_meshlets_w_lod
{
    arena_array<hpk_meshlet, virtual_arena>	meshlets        = {};
    float									meshLevelError  = FLT_MAX;
};

// TODO: if we get meshlet weirdness we'd prolly need to protect some attrs during simplification
std::array<hpk_meshlets_w_lod, MAX_LOD_LEVELS_COUNT> MeshoptMakeHpMeshletsWithLod(
	std::span<const float3> pos,
	std::span<const float3> normals,
	//std::span<const float4> tan,
	//std::span<const float2> uvs,
	std::span<const u32>	indices,
	float					simplificationRatio,
	meshlet_config			cfg,
	virtual_arena&          arena,
	virtual_arena&          scratchArena
) {
    HT_ASSERT( &arena != &scratchArena );

    constexpr u32 meshLodOpts = meshopt_SimplifyErrorAbsolute | meshopt_SimplifyPermissive
                                        | meshopt_SimplifyPrune | meshopt_SimplifyLockBorder;

    scoped_arena scratch = { scratchArena };

    std::array<hpk_meshlets_w_lod, MAX_LOD_LEVELS_COUNT> lodLevels = {};

    std::span<const u32>    srcIdxBuff      = indices;
    float                   parentMeshError = 0.0f;
    for( u64 lodIdx = 0; lodIdx < MAX_LOD_LEVELS_COUNT; ++lodIdx )
    {
        const u64 srcIdxCount = std::size( srcIdxBuff );
        const u64 maxMltCount = meshopt_buildMeshletsBound( srcIdxCount, cfg.maxVertices, cfg.maxTriangles );
        borrowed_array<meshopt_Meshlet> meshlets    = ArenaNewArray<meshopt_Meshlet>( scratch, maxMltCount );
        borrowed_array<u32>             mltVtx      = ArenaNewArray<u32>( scratch, srcIdxCount );
        borrowed_array<u8>              mltTris     = ArenaNewArray<u8>( scratch, srcIdxCount );

        u64 meshletCount = meshopt_buildMeshlets( &meshlets[ 0 ], &mltVtx[ 0 ], &mltTris[ 0 ], &srcIdxBuff[ 0 ],
            srcIdxCount, &pos[ 0 ].x, std::size( pos ),
            sizeof( pos[ 0 ] ), cfg.maxVertices, cfg.maxTriangles,
            cfg.coneWeight );

        HT_ASSERT( meshletCount < MAX_MESHLETS_PER_MESH );

        const meshopt_Meshlet& last = meshlets[ meshletCount - 1 ];

        meshlets.resize( meshletCount );
        mltVtx.resize( ( u64 ) last.vertex_offset + last.vertex_count );
        mltTris.resize( ( u64 ) last.triangle_offset + ( u64 ) last.triangle_count * 3 );

        lodLevels[ lodIdx ] = {
            .meshlets = { arena, std::from_range, meshlets | std::views::transform(
                [ & ]( const meshopt_Meshlet& m )
                {
                    return MeshoptSimplyfyMeshlet( m, mltVtx, mltTris, pos, normals, parentMeshError );
                } ) },
            .meshLevelError = parentMeshError
        };

        if( ( MAX_LOD_LEVELS_COUNT - 1 ) == lodIdx ) break;

        u64 targetIdxCount = u64( simplificationRatio * ( float ) srcIdxCount );

        borrowed_array<u32> lod = ArenaNewArray<u32>( scratch, srcIdxCount );
        float lodError = 0.0f;
        lod.resize( meshopt_simplifyWithAttributes( &lod[ 0 ], std::data( srcIdxBuff ),
            srcIdxCount, &pos[ 0 ].x, std::size( pos ),
            sizeof( pos[ 0 ] ), &normals[ 0 ].x,
            sizeof( normals[ 0 ] ), HPK_MESHOPT_ATTR_WEIGHTS,
            std::size( HPK_MESHOPT_ATTR_WEIGHTS ), nullptr, //&locks[ 0 ],
            targetIdxCount, FLT_MAX, meshLodOpts, &lodError ) );

        if( ( std::size( lod ) >= srcIdxCount ) || ( 0 == std::size( lod ) ) ) break;

        srcIdxBuff       = lod;
        parentMeshError += lodError;
    }

	return lodLevels;
}

std::vector<packed_vtx_attr> HpkMeshletPackVtxAttributes( const hpk_meshlet& mlt )
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
	constexpr u32   gridStep        = mlt_quantized_grid::gridStep;
    constexpr float invGridFactor   = 1.0f / float( gridStep );

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
		.quantAabbMin		= float3{ float( minMltX ), float( minMltY ), float( minMltZ ) } * invGridFactor,
		.quantAabbMax		= float3{ float( maxMltX ), float( maxMltY ), float( maxMltZ ) } * invGridFactor,
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
constexpr bool validateNormalEncoding = true;


// NOTE: vtx quant from https://daniilvinn.github.io/2024/05/04/omniforce-vertex-quantization.html
void HpkQuantizeAndAppendLODLevel(
	const std::vector<hpk_meshlet>&	meshoptMeshlets,
	bit_stream&						vtxPosBitstream,
	std::vector<oct16x2>&	        vtxNormals,
	std::vector<u8>&				indices,
	std::vector<gpu_meshlet>&		meshlets
) {
	const u64 mltCount = std::size( meshoptMeshlets );
	// NOTE: reserve max cap
	vtxNormals.reserve( std::size( vtxNormals ) + mltCount * RASTER_MAX_VTX_PER_MLT );
	indices.reserve( std::size( indices ) + mltCount * RASTER_MLT_MAX_INDEX * LODS_PER_MESHLET );
	meshlets.reserve( std::size( meshlets ) + mltCount );

	for( const hpk_meshlet& m : meshoptMeshlets )
	{
		const aabb_t<float3> meshletAabb = ComputeAabb( m.pos );

		mlt_quantized_grid mltEncodingGrid = HpkMakeMltQuantizedGrid( meshletAabb );

		u32 packed8888_XYZ_Grid_BitDepth = mltEncodingGrid.bitDepthPerAxis.x
			| ( mltEncodingGrid.bitDepthPerAxis.y << 8 )
			| ( mltEncodingGrid.bitDepthPerAxis.z << 16 )
			| ( mltEncodingGrid.gridResolutionInBits << 24 );

		u32 packed8_12_12_VtxCount_Lod_01_IdxCount = u32( m.vtxCount )
			| ( u32( std::size( m.indices ) ) << 8 )
			| ( u32( std::size( m.idxLod ) ) << 20 );

		HT_ASSERT( ( m.vtxCount < 256 ) &&
			( std::size( m.idxLod ) < RASTER_MLT_MAX_INDEX ) &&
			( std::size( m.indices ) < RASTER_MLT_MAX_INDEX ) );

		meshlets.push_back( {
			.aabbMin								= mltEncodingGrid.quantAabbMin,
			.aabbMax								= mltEncodingGrid.quantAabbMax,
			.vtxPosOffsetBits						= ( u32 ) vtxPosBitstream.cursorInBits,
			.vtxAttrsOffset							= ( u32 ) std::size( vtxNormals ),
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
				//HT_ASSERT( HpkDecodeVerifyQuantized( p, enc, mltEncodingGrid ) );
			}
		}

	    for( float3 n : m.norm )
	    {
	        float2  octN        = EncodeOctaNormal( n );
	        oct16x2 encNormal   = SnormBits<16>( octN.x ) | ( SnormBits<16>( octN.y ) << 16 );

	        vtxNormals.push_back( encNormal );

	        if constexpr( validateNormalEncoding )
	        {
	            //HT_ASSERT( HpkDecodeVerifyQuantized( p, enc, mltEncodingGrid ) );
	        }
	    }


		//verticesAttrs.append_range( HpkMeshletPackVtxAttributes( m ) );
		indices.append_range( m.indices );
		if( FLT_MAX != m.lodError )
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

constexpr bool CHECK_CORRECTNESS = true;

static void AtomicWait( const std::atomic<u64>& waitAddr, u64 waitVal )
{
    for( u64 seenVal; ( seenVal = waitAddr.load( std::memory_order_acquire ) ) < waitVal; ) waitAddr.wait( seenVal );
}

using fs_path = fixed_string<256>;

constexpr u64   GRID_SECTOR_DIM_IN_METERS   = 256;
constexpr float GRID_SCALE                  = 1.0f / float( GRID_SECTOR_DIM_IN_METERS );

i32x2 HpkBinNodeTo2DGridSector( const raw_node& node )
{
    using namespace DirectX;

    XMVECTOR localCenter = XMVectorScale( XMVectorAdd( DX_XMLoadFloat3( node.aabb.min ),
        DX_XMLoadFloat3( node.aabb.max ) ), 0.5f );
    XMVECTOR worldCenter = XMVectorAdd( XMVector3Rotate(
        XMVectorMultiply( localCenter, DX_XMLoadFloat3( node.toWorld.s ) ),
        DX_XMLoadFloat4( node.toWorld.r ) ),
        DX_XMLoadFloat3( node.toWorld.t ) );

    XMVECTOR sector = XMVectorFloor( XMVectorScale( worldCenter, GRID_SCALE ) );
    // NOTE: bc we've exported from gLTF
    return { ( i32 ) XMVectorGetX( sector ), ( i32 ) XMVectorGetZ( sector ) };
}

struct alignas( 64 ) gltf_parse_job
{
    std::vector<raw_node>       nodes;
    std::vector<raw_mesh_desc>  meshDesc;
};

auto GetDirViewOfFiles( std::string_view dir, std::string_view ext )
{
    return fs::directory_iterator{ dir } | std::views::filter( [ & ]( auto& e )
    {
        return e.path().string().ends_with( ext );
    } );
}

void HpkExitWithMsg( std::string_view msg )
{
    std::println( stderr, "{}", msg );
    std::exit( -1 );
}

static gltf_parse_job HpkParseGltfsParallel( std::string_view dir )
{
    std::vector gltfPaths = { std::from_range, GetDirViewOfFiles( dir, ".gltf" ) | std::views::transform(
    []( auto& e )
    {
        return fs_path{ e.path().string() };
    } ) };

    if( !std::size( gltfPaths ) ) HpkExitWithMsg( "No gltfs in dir\n" );

    std::vector<gltf_parse_job> parseJobs{ std::size( gltfPaths ), {} };
    std::atomic<u64>            atomicJobsCounter = 0;

    auto LmbdGltfParseJob = [ & ]()
    {
        const u64 jobCount = std::size( parseJobs );
        for( ;; )
        {
            u64 currJobIdx = atomicJobsCounter.fetch_add( 1, std::memory_order_relaxed );
            if( currJobIdx >= jobCount ) return;

            mmap_file rawGltfBytes = SysCreateMmapFile( ( const char* ) gltfPaths[ currJobIdx ],
                file_permissions_bits::READ, file_create_flags::OPEN_IF_EXISTS,
                file_access_flags::SEQUENTIAL );
            defer { SysDestroyMmapFile( &rawGltfBytes ); };

            scoped_arena fileArena = { g_ThreadArena[ 0 ] };

            cgltf_options options = {
                .type   = cgltf_file_type_gltf,
                .memory = { .alloc_func = CgltfArenaAlloc, .free_func = CgltfArenaFree },
                .file   = { .read = HtCgltfFileRead, .release = HtCgltfFileRelease }
            };
            gltf_loader gltf = { rawGltfBytes.dataView, options };
            parseJobs[ currJobIdx ] = {
                .nodes      = MOV( gltf.ProcessDrawableNodes() ),
                .meshDesc   = MOV( gltf.ProcessPrimitivesAttributes() )
            };
        }
    };

    {
        std::vector workers = { std::from_range, std::views::iota( 0ull, g_ThreadCount )
            | std::views::transform( [ & ]( u64 ) { return std::jthread{ LmbdGltfParseJob }; } ) };
    }

    std::vector<u64> meshIdxOffsets( std::size( parseJobs ) );
    {
        auto meshCounts = parseJobs | std::views::transform( []( const gltf_parse_job& j )
        {
            return std::size( j.meshDesc );
        } );
        std::exclusive_scan( std::begin( meshCounts ), std::end( meshCounts ),
            std::begin( meshIdxOffsets ), 0ull );
    }

    std::vector<raw_node> nodes;
    for( auto&&[ job, meshIdxOffset ] : std::views::zip( parseJobs, meshIdxOffsets ) )
    {
        nodes.append_range( job.nodes | std::views::transform( [ meshIdxOffset ]( const raw_node& n ) -> raw_node
        {
            return { .toWorld = n.toWorld, .aabb = n.aabb, .meshIdx = n.meshIdx + meshIdxOffset };
        } ) );
    }

    return {
        .nodes      = MOV( nodes ),
        .meshDesc   = {
            std::from_range, parseJobs | std::views::transform( &gltf_parse_job::meshDesc ) | std::views::join }
    };
}

static gltf_parse_job HpkParseGltfs( std::string_view dir )
{
    std::vector gltfPaths = { std::from_range, GetDirViewOfFiles( dir, ".gltf" ) | std::views::transform(
    []( auto& e )
    {
        return fs_path{ e.path().string() };
    } ) };

    if( !std::size( gltfPaths ) ) HpkExitWithMsg( "No gltfs in dir\n" );

    gltf_parse_job merged;
    for( const fs_path& gltfPath : gltfPaths )
    {
        mmap_file rawGltfBytes = SysCreateMmapFile( ( const char* ) gltfPath,
            file_permissions_bits::READ, file_create_flags::OPEN_IF_EXISTS,
            file_access_flags::SEQUENTIAL );
        defer { SysDestroyMmapFile( &rawGltfBytes ); };

        scoped_arena fileArena = { g_ThreadArena[ 0 ] };

        cgltf_options options = {
            .type   = cgltf_file_type_gltf,
            .memory = { .alloc_func = CgltfArenaAlloc, .free_func = CgltfArenaFree },
            .file   = { .read = HtCgltfFileRead, .release = HtCgltfFileRelease }
        };
        gltf_loader gltf = { rawGltfBytes.dataView, options };

        u64 meshIdxOffset = std::size( merged.meshDesc );
        merged.nodes.append_range( gltf.ProcessDrawableNodes() | std::views::transform(
        [ meshIdxOffset ]( const raw_node& n ) -> raw_node
        {
            return { .toWorld = n.toWorld, .aabb = n.aabb, .meshIdx = n.meshIdx + meshIdxOffset };
        } ) );
        merged.meshDesc.append_range( gltf.ProcessPrimitivesAttributes() );
    }

    return merged;
}

static void HpkProcessMeshesParallel( std::span<const raw_mesh_desc> rawMeshDescs, std::span<const u8> binData )
{
    std::atomic<u64> atomicJobsCounter = 0;

    auto LmbdProcessMeshJob = [ & ]()
    {
        const u64 jobCount = std::size( rawMeshDescs );
        for( ;; )
        {
            u64 currJobIdx = atomicJobsCounter.fetch_add( 1, std::memory_order_relaxed );
            if( currJobIdx >= jobCount ) return;

            virtual_arena& scratchArena = g_ThreadArena[ 0 ];
            virtual_arena& tempArena = g_ThreadArena[ 1 ];

            scoped_arena scopedArena0 = { scratchArena };
            scoped_arena scopedArena1 = { tempArena };

            const raw_mesh_desc& meshDesc = GltfPatchRawMeshDesc( rawMeshDescs[ currJobIdx ], binData );
            // TODO: process points
            if( raw_mesh_topology_t::POINTS == meshDesc.topology ) continue;

            arena_array<float3, virtual_arena> pos     = { scratchArena, std::from_range, meshDesc.pos };
            arena_array<u32, virtual_arena>    indices = ReadNormalizedIndexBuffer( meshDesc.indices, scratchArena );
            arena_array<float3, virtual_arena> normals = std::size( meshDesc.normals ) ?
                arena_array<float3, virtual_arena>{ scratchArena, std::from_range, meshDesc.normals }
                : GenerateSmoothNormals( pos, indices, scratchArena );

            MeshoptReindexAndOptimizeMesh( pos, normals, indices, scratchArena );

            std::array<hpk_meshlets_w_lod, MAX_LOD_LEVELS_COUNT> mltsWLod = MeshoptMakeHpMeshletsWithLod(
                pos, normals, indices, LOD_MESH_LEVEL_RATIO, {}, tempArena, scratchArena );

            bit_stream							vtxPosBitstream;
            std::vector<packed_vtx_attr>		verticesAttrs;
            std::vector<index_t>				idxBuff;
            std::vector<gpu_meshlet>			meshlets;

            HpkQuantizeAndAppendLODLevel( lodMeshlets, vtxPosBitstream, verticesAttrs, idxBuff, meshlets );

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
    };

    {
        std::vector workers = { std::from_range, std::views::iota( 0ull, g_ThreadCount )
            | std::views::transform( [ & ]( u64 ) { return std::jthread{ LmbdProcessMeshJob }; } ) };
    }
}

i32 main( i32 argc, char** argv  )
{
    std::cout << std::unitbuf;

    std::vector<std::string_view> cliArgs = { argv + 1, argv + argc };

	if( std::size( cliArgs ) < 2 ) HpkExitWithMsg( "Missing arguments\n" );

    auto isDir = std::ranges::find( cliArgs, "--dir" );

    if( std::end( cliArgs ) != isDir )
    {
        std::string_view dir = isDir[ 1 ];
        if( !fs::exists( dir ) && !fs::is_directory( dir ) ) HpkExitWithMsg( "Missing dir\n" );

        auto[ nodes, meshDescs ] = HpkParseGltfs( dir );

        std::vector<i32x2> nodeBins = { std::from_range, nodes | std::views::transform( HpkBinNodeTo2DGridSector ) };

        fs::directory_iterator dirIt{ dir };
        auto binEntry = std::ranges::find_if( dirIt, []( auto& e )
        {
            return e.path().string().ends_with( ".bin" );
        } );

        if( std::ranges::end( dirIt ) == binEntry ) HpkExitWithMsg( "No bin in dir\n" );

        fs_path binPath = { binEntry->path().string() };
        // NOTE: this is global but our hook call thread local data, so safe
        meshopt_setAllocator( MeshoptScratchAlloc, MeshoptScratchFree );

        // TODO: maybe compute the LOD count dynamically for now we'll do 4 mesh LODS at 1/4 + 2 meshlet LODs ( full + 1/2 )
        return 0;

        auto glbPathsView = fs::directory_iterator{ dir } | std::views::filter( []( auto& e )
        {
            return e.path().string().ends_with( ".glb" );
        } );

        std::vector glbPaths = { std::from_range, glbPathsView | std::views::transform( []( auto& e )
        {
            return fs_path{ e.path().string() };
        } ) };

    }

	const std::string_view gltfFilePath = argv[ 1 ];
	const std::string_view hpkFilePath  = argv[ 2 ];

	HT_ASSERT( fs::exists( gltfFilePath ) );

	gltf_loader gltf = { std::data( gltfFilePath ) };

	std::vector<raw_node>			rawNodes		= gltf.ProcessDrawableNodes();
	std::vector<raw_mesh>			rawMeshes		= gltf.ProcessPrimitives();


	ankerl::unordered_dense::map<vfs_path, hpk_mesh_asset> meshAssetMap;

	std::cout << "Processing meshes\n";
	for( raw_mesh& mesh : rawMeshes )
	{
		vfs_path assetPath = { "{}{}.mesh", HELLPACK_MESH_DIR, std::data( mesh.name ) };
		HT_ASSERT( !meshAssetMap.contains( assetPath ) );

		ValidateAndNormalizeRawMesh( mesh );
		MeshoptReindexAndOptimizeMesh( mesh );

		std::vector<hpk_lod_level> meshLods = MeshoptGenerateLODChain( mesh, MAX_LOD_LEVELS_COUNT - 1 );

		float lodErr[ MAX_LOD_LEVELS_COUNT ] = { FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
		u32 lodMltNum[ MAX_LOD_LEVELS_COUNT ] = {};

		bit_stream							vtxPosBitstream;
		std::vector<packed_vtx_attr>		verticesAttrs;
		std::vector<index_t>				indices;
		std::vector<gpu_meshlet>			meshlets;

		for( u64 li = 0; li < std::size( meshLods ); li++ )
		{
			const hpk_lod_level& lod = meshLods[ li ];
			// NOTE: the lod errors are "global" per object
			std::vector<hpk_meshlet> lodMeshlets = MeshoptMakeHpMeshletsWithLod( mesh.pos, mesh.normals, mesh.tans,
				mesh.uvs, lod.indices, lod.error, {} );

			HT_ASSERT( std::size( lodMeshlets ) < MAX_MESHLETS_PER_MESH );
			lodMltNum[ li ] = u32( std::size( lodMeshlets ) );
			lodErr[ li ] = lod.error;

			HpkQuantizeAndAppendLODLevel( lodMeshlets, vtxPosBitstream, verticesAttrs, indices, meshlets );
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
			.toWorld		= { .t = n.toWorld.t, .r = n.toWorld.r, .s = n.toWorld.s },
			.meshHash		= std::hash<vfs_path>{}( assetPath ),
			.materialIdx	= ( u16 ) mesh.materialIdx // NOTE: these should match 1:1 with ours
		} );
	}

	std::cout << "Processing meshes & nodes done ! Dumping to file.\n";

	{
		zip_writer zipArchive = { std::data( hpkFilePath ) };

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

