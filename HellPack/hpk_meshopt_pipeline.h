#pragma once

#ifndef __HPK_MESHOPT_PIPELINE_H__
#define __HPK_MESHOPT_PIPELINE_H__

#include <ht_core_types.h>
#include <ht_vec_types.h>
#include <meshoptimizer.h>

#include <ranges>
#include <span>
#include <vector>

#include <ht_mem_arena.h>
#include <ht_array.h>
#include <range_utils.h>

#include "hp_types_internal.h"

template<CONTIGUOUS_RANGE_T R>
meshopt_Stream MeshoptMakeStream( const R& range )
{
    HT_ASSERT( 0 != std::size( range ) );
    return { .data = std::data( range ), .size = sizeof( range[ 0 ] ), .stride = sizeof( range[ 0 ] ) };
}

template<CONTIGUOUS_RANGE_T R>
void MeshoptRemapAttributeBufferInplace( R& attrRange, u64 attrElemCount, std::span<const u32> remap )
{
    HT_ASSERT( 0 != std::size( attrRange ) );
    meshopt_remapVertexBuffer( std::data( attrRange ),std::data( attrRange ),
        attrElemCount, sizeof( attrRange[ 0 ] ), std::data( remap ) );
}

// NOTE: no cache optimization, buildMeshlets doesn't need it ( only buildMeshletsScan does )
// NOTE: no fetch optimization meshlets emit their own contiguous vertex slices into the global VB,
// which already gives optimal fetch locality
static void MeshoptReindexAndOptimizeMesh(
    arena_array<float3, virtual_arena>&     pos,
    arena_array<float3, virtual_arena>&     normals,
    //arena_array<float4, virtual_arena>&   tans,
    //arena_array<float2, virtual_arena>&   uvs,
    arena_array<u32, virtual_arena>&        indices,
    virtual_arena&                          virtualArena
) {
    HT_ASSERT( std::size( pos ) == std::size( normals ) );

    scoped_arena scopedArena = { virtualArena };

    meshopt_Stream attrStreams[] = {
        MeshoptMakeStream( pos ),
        MeshoptMakeStream( normals ),
        //MeshoptMakeStream( tans ),
        //MeshoptMakeStream( uvs )
    };

    u64 vtxCount = std::size( pos );
    u64 idxCount = std::size( indices );

    borrowed_array<u32> remap = ArenaNewArray<u32>( scopedArena, vtxCount );
    u64 newVtxCount = meshopt_generateVertexRemapMulti( std::data( remap ), std::data( indices ),
        idxCount, vtxCount, attrStreams, std::size( attrStreams ) );

    HT_ASSERT( newVtxCount <= vtxCount );
    meshopt_remapIndexBuffer( std::data( indices ), std::data( indices ), idxCount,
        std::data( remap ) );

    MeshoptRemapAttributeBufferInplace( pos, vtxCount, remap );
    MeshoptRemapAttributeBufferInplace( normals, vtxCount, remap );
    //MeshoptRemapAttributeBufferInplace( tans, vtxCount, remap );
    //MeshoptRemapAttributeBufferInplace( uvs, vtxCount, remap );

    pos.resize( newVtxCount );
    normals.resize( newVtxCount );
    //tans.resize( newVtxCount );
    //uvs.resize( newVtxCount );
}

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

struct meshlet_config
{
    //float   coneWeight		= 0.8f;
    float	fillWeight		= 0.5f;
    u16		maxVertices		= RASTER_MAX_VTX_PER_MLT;
    u16		minTriangles	= RASTER_MAX_TRIS_PER_MLT / 4;
    u16		maxTriangles	= RASTER_MAX_TRIS_PER_MLT;
};

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
        //const u64 maxMltCount = meshopt_buildMeshletsBound( srcIdxCount, cfg.maxVertices, cfg.maxTriangles );
        const u64 maxMltCount = meshopt_buildMeshletsBound( srcIdxCount, cfg.maxVertices, cfg.minTriangles );
        hpk_virt_array<meshopt_Meshlet> meshlets    = { scratch, maxMltCount };
        hpk_virt_array<u32>             mltVtx      = { scratch, srcIdxCount };
        hpk_virt_array<u8>              mltTris     = { scratch, srcIdxCount };

        //u64 meshletCount = meshopt_buildMeshlets( &meshlets[ 0 ], &mltVtx[ 0 ], &mltTris[ 0 ], &srcIdxBuff[ 0 ],
        //    srcIdxCount, &pos[ 0 ].x, std::size( pos ),
        //    sizeof( pos[ 0 ] ), cfg.maxVertices, cfg.maxTriangles,
        //    cfg.coneWeight );
        u64 meshletCount = meshopt_buildMeshletsSpatial( &meshlets[ 0 ], &mltVtx[ 0 ], &mltTris[ 0 ], &srcIdxBuff[ 0 ],
            srcIdxCount, &pos[ 0 ].x, std::size( pos ),
            sizeof( pos[ 0 ] ), cfg.maxVertices, cfg.minTriangles, cfg.maxTriangles,
            cfg.fillWeight );

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

        hpk_virt_array<u32> lod = { scratch, srcIdxCount };
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

#endif //!__HPK_MESHOPT_PIPELINE_H__