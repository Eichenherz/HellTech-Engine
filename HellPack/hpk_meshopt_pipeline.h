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

#endif //!__HPK_MESHOPT_PIPELINE_H__