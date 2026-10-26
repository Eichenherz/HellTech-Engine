#pragma once

#ifndef HELLTECH_HPK_GLTF_LOADER_H
#define HELLTECH_HPK_GLTF_LOADER_H

#include <span>
#include <ranges>

#include <ht_core_types.h>
#include <ht_error.h>
#include <range_utils.h>

#include <ht_fixed_string.h>
#include <System/sys_file.h>

#include "hpk_gltf_types.h"
#include "gltf_helper.h"
#include "hp_types_internal.h"

inline bool HpkSkipPrimitive( const hpk_gltf_primitive& prim )
{
    return hpk_gltf_prim_mode::TRIANGLES != prim.mode;
}

inline raw_mesh HpkProcessGltfPrim(
    const hpk_gltf_data&        data,
    const hpk_gltf_primitive&   prim,
    const hpk_mesh_name&        hpkMeshKeyStr,
    virtual_arena&              arena
) {
    // NOTE: gltf mandates that the pos stream be present
    const hpk_gltf_accessor& posAcc     = data.accessors[ prim.attributes.POSITION ];
    const hpk_gltf_accessor* pIdxAcc    = HpkGetGltfAccessor( data, prim.indices );
    const hpk_gltf_accessor* pNrmAcc    = HpkGetGltfAccessor( data, prim.attributes.NORMAL );

    return {
        .name    = hpkMeshKeyStr,
        .pos     = { arena, std::from_range, hpk_strided_view<float3>{ data, posAcc } },
        .norm    = pNrmAcc ? borrowed_array<float3>{ arena, std::from_range, hpk_strided_view<float3>{ data, *pNrmAcc } }
            : borrowed_array<float3>{},
        .idx     = pIdxAcc ? HpkGltfUnpackIndices( data, *pIdxAcc, arena ) : borrowed_array<u32>{},
        .aabbMin = { posAcc.min[ 0 ], posAcc.min[ 1 ], posAcc.min[ 2 ] },
        .aabbMax = { posAcc.max[ 0 ], posAcc.max[ 1 ], posAcc.max[ 2 ] },
        .topo    = raw_mesh_topology_t::MESH
    };
}

inline auto HpkGltfProcessDrawablesHierarchy(
    const hpk_gltf_data&        data,
    std::string_view            originFileName,
    virtual_arena&              tmpArena,
    virtual_array<raw_node>&    flatNodes,
    auto&&                      PfnVisitPrimFunc
) {
    auto tmpScope = scoped_arena{ tmpArena };

    std::span meshSeenBitset = ArenaNewArray<u64>( tmpArena, ( std::size( data.meshes ) + 63 ) / 64 );
    std::ranges::fill( meshSeenBitset, 0 );

    auto LmbdVisitNode = [ & ]( this auto&& PfnSelf, const hpk_gltf_node& node, packed_trs parentTrs ) -> void
    {
        if( !node.extensions.KHR_node_visibility.visible ) return;
        if( ~0u != node.skin ) return;
        if( ( ~0u != node.mesh )
            && ( caldera_prim_purpose_t::NONE != data.meshes[ node.mesh ].extensions.CALDERA_mesh_purpose.purpose )
         ) return;

        packed_trs trs = GltfComposePackedTRS( parentTrs, GetTrsFromNode( node ) );

        if( const u32 meshIdx = node.mesh; ~0u != meshIdx )
        {
            hpk_gltf_mesh mesh = data.meshes[ meshIdx ];

            hpk_gltf_caldera_shared_mesh sharedMesh = mesh.extensions.CALDERA_shared_mesh;
            bool isShared = !IsStructZero( sharedMesh );

            bool processPrims = !isShared && !( meshSeenBitset[ meshIdx / 64 ] & ( 1ull << ( meshIdx & 63 ) ) );

            bool isNodeDrawable = !node.extensions.CALDERA_node_place_holder.placeholder;
            u64 instCount = isNodeDrawable ? HpkGltfGetExtGpuInstCount( data, node ) : 0;

            for( const auto[ primIdx, prim ] : mesh.primitives | std::views::enumerate )
            {
                if( HpkSkipPrimitive( prim ) ) continue;

                auto primKeyStr = !isShared
                    ? hpk_mesh_name{
                        "{}_Mesh_{}_Prim_{}_{:.48}", originFileName, meshIdx, primIdx,
                        std::size( mesh.name ) ? mesh.name : "NONE"
                    }
                    : hpk_mesh_name{
                        "{}_Mesh_{}_Prim_{}_{:.48}", sharedMesh.parentFile, sharedMesh.meshIdx, primIdx,
                        sharedMesh.meshName
                    };

                if( processPrims )
                {
                    PfnVisitPrimFunc( data, prim, primKeyStr );
                }

                const hpk_gltf_accessor& posAcc = data.accessors[ prim.attributes.POSITION ];
                float3a aabbMin = { posAcc.min[ 0 ], posAcc.min[ 1 ], posAcc.min[ 2 ] };
                float3a aabbMax = { posAcc.max[ 0 ], posAcc.max[ 1 ], posAcc.max[ 2 ] };

                for( u64 ii = 0; ii < instCount; ++ii )
                {
                    flatNodes.push_back( {
                        .toWorld    = GltfComposePackedTRS( trs, HpkGltfGetTRSFromExtGpuInst( data, node, ii ) ),
                        .aabbCenter = ( aabbMax + aabbMin ) * 0.5f,
                        .aabbExtent = ( aabbMax - aabbMin ) * 0.5f,
                        .meshHash   = HpkHashMeshKey( primKeyStr ),
                    } );
                }
            }

            meshSeenBitset[ meshIdx / 64 ] |= 1ull << ( meshIdx & 63 );
        }

        for( u32 childIdx : node.children )
        {
            PfnSelf( data.nodes[ childIdx ], trs );
        }
    };

    for( u32 rootIdx : data.scenes[ data.scene ].nodes )
    {
        LmbdVisitNode( data.nodes[ rootIdx ], IDENTITY_TRS );
    }

    //return std::tuple{ flatNodes };
}

#endif // !HELLTECH_HPK_GLTF_LOADER_H
