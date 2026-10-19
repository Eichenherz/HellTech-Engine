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

inline raw_mesh HpkMakeRawMeshFromGltf(
    const hpk_gltf_data&        gltfData,
    const hpk_gltf_mesh&        gltfMesh,
    const hpk_gltf_primitive&   gltfPrim,
    const hpk_mesh_name&        hpkMeshKeyStr,
    virtual_arena&              arena
) {
    // NOTE: gltf mandates that the pos stream be present
    const hpk_gltf_accessor posAcc = *HpkGetGltfAccessor( gltfData, gltfPrim.attributes.POSITION );
    HT_ASSERT( ( 3 == std::size( posAcc.min ) ) && ( 3 == std::size( posAcc.max ) ) );

    const hpk_gltf_accessor* pIdxAcc = HpkGetGltfAccessor( gltfData, gltfPrim.indices );
    const hpk_gltf_accessor* pNormAcc = HpkGetGltfAccessor( gltfData, gltfPrim.attributes.NORMAL );

    return {
        .name		= hpk_mesh_name{ "{:.96}", std::size( gltfMesh.name ) ? gltfMesh.name : "NONE" },
        .pos		= borrowed_array<float3>{ arena, std::from_range, hpk_strided_view<float3>{ gltfData, posAcc } },
        .norm       = pNormAcc ? borrowed_array<float3>{ arena, std::from_range,
            hpk_strided_view<float3>{ gltfData, *pNormAcc } } : borrowed_array<float3>{},
        .idxBuff    = pIdxAcc ? HpkGltfUnpackIndices( gltfData, *pIdxAcc, arena ) : borrowed_array<u32>{},
        .aabbMin    = { posAcc.min[ 0 ], posAcc.min[ 1 ], posAcc.min[ 2 ] },
        .aabbMax    = { posAcc.max[ 0 ], posAcc.max[ 1 ], posAcc.max[ 2 ] },
        .hash       = HpkHashMeshKey( hpkMeshKeyStr ),
        .topology   = HpkGltfPrimTopology( gltfPrim.mode )
    };
}

inline auto HpkGltfProcessDrawablesHierarchy(
    const hpk_gltf_data&    data,
    std::string_view        originFileName,
    virtual_arena&          outArena,
    virtual_arena&          tmpArena,
    auto&&                  PfnVisitPrimFunc
) {
    auto tmpScope = scoped_arena{ tmpArena };

    std::span meshSeenBitset = ArenaNewArray<u64>( tmpArena, ( std::size( data.meshes ) + 63 ) / 64 );

    //auto flatNodes = arena_array<raw_node, virtual_arena>{ outArena };
    auto LmbdVisitNode = [ & ]( this auto&& PfnSelf, const hpk_gltf_node& node, packed_trs parentTrs ) -> void
    {
        if( !node.extensions.KHR_node_visibility.visible ) return;

        packed_trs trs = IDENTITY_TRS;//GltfComposePackedTRS( parentTrs, GetTrsFromNode( node ) );

        if( const u32 meshIdx = node.mesh; ~0u != meshIdx )
        {
            hpk_gltf_mesh mesh = data.meshes[ meshIdx ];

            for( const auto[ primIdx, prim ] : mesh.primitives | std::views::enumerate )
            {
                if( caldera_prim_purpose_t::NONE != prim.extensions.CALDERA_primitive_purpose.purpose ) continue;

                hpk_gltf_caldera_shared_mesh calderaShared = prim.extensions.CALDERA_shared_mesh;
                bool isShared = !IsStructZero( calderaShared );

                auto meshKeyStr = !isShared
                    ? hpk_mesh_name{ "{}_Mesh_{}_Prim_{}", originFileName, meshIdx, primIdx }
                    : hpk_mesh_name{ "{}_Mesh_{}_Prim_{}",
                        calderaShared.parentFile, calderaShared.meshIdx, calderaShared.primIdx
                    };

                if( !isShared && ( meshSeenBitset[ meshIdx >> 6 ] & ( 1ull << ( meshIdx & 63 ) ) ) )
                {
                    for( u32 lodChainRootIdx : node.extensions.MSFT_lod.ids )
                    {

                    }
                    PfnVisitPrimFunc( mesh, prim, meshKeyStr );
                }

                if( node.extensions.CALDERA_node_place_holder.placeholder ) continue;

                const hpk_gltf_accessor posAcc = *HpkGetGltfAccessor( data, prim.attributes.POSITION );
                float3a aabbMin = { posAcc.min[ 0 ], posAcc.min[ 1 ], posAcc.min[ 2 ] };
                float3a aabbMax = { posAcc.max[ 0 ], posAcc.max[ 1 ], posAcc.max[ 2 ] };

                u64 instCount = HpkGltfGetExtGpuInstCount( data, node );
                for( u64 ii = 0; ii < instCount; ++ii )
                {
                    //flatNodes.push_back( {
                    //    .toWorld    = GltfComposePackedTRS( trs, HpkGltfGetTRSFromExtGpuInst( data, node, ii ) ),
                    //    .aabbCenter = ( aabbMax + aabbMin ) * 0.5f,
                    //    .aabbExtent = ( aabbMax - aabbMin ) * 0.5f,
                    //    .meshHash   = HpkHashMeshKey( meshKeyStr ),
                    //} );
                }
            }

            meshSeenBitset[ meshIdx >> 6 ] |= 1ull << ( meshIdx & 63 );
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

/*
struct gltf_loader
{
	cgltf_data* data = nullptr;

	// TODO: explicitly enforce the 0th sampler is default convention
	std::vector<sampler_config> ProcessSamplers() const
	{
		std::vector<sampler_config> samplersOut;
		samplersOut.push_back( DEFAULT_SAMPLER );

		samplersOut.reserve( data->samplers_count );
		for( const cgltf_sampler& sampler : HT_CGLTF_SPAN( data->samplers ) )
		{
			samplersOut.push_back( {
				.filterModeS	= CgltfFilterToFlags( sampler.min_filter ),
				.filterModeT	= CgltfFilterToFlags( sampler.mag_filter ),
				.wrapModeS		= CgltfWrapToFlags( sampler.wrap_s ),
				.wrapModeT		= CgltfWrapToFlags( sampler.wrap_t )
			} );
		}

		return samplersOut;
	}

	std::vector<raw_material_info> ProcessMaterials() const
	{
		std::vector<raw_material_info> materialsOut;
		materialsOut.reserve( data->materials_count );

		ankerl::unordered_dense::set<i32> samplers;
		for( u64 mi = 0; mi < data->materials_count; ++mi )
		{
			samplers.clear();

			const cgltf_material& material = data->materials[ mi ];
			HT_ASSERT( material.has_pbr_metallic_roughness );
			// TODO: more materials ????
			const cgltf_pbr_metallic_roughness& pbrInfo = material.pbr_metallic_roughness;

			raw_material_info metadata = {
				.name				= { "{}_{}", material.name ? material.name : "mtrl", mi },
				.baseColFactor		= {
					pbrInfo.base_color_factor[ 0 ],
					pbrInfo.base_color_factor[ 1 ],
					pbrInfo.base_color_factor[ 2 ],
					pbrInfo.base_color_factor[ 3 ]
			    },
				.metallicFactor		= pbrInfo.metallic_factor,
				.roughnessFactor	= pbrInfo.roughness_factor,
				.alphaCutoff		= material.alpha_cutoff,
				.emissiveFactor		= {
					material.emissive_factor[ 0 ],
					material.emissive_factor[ 1 ],
					material.emissive_factor[ 2 ]
			    },
				.alphaMode			= CgltfAlphaModeToEnum( material.alpha_mode )
			};

			const gltf_texture pbrBaseCol = ProcessTexture( pbrInfo.base_color_texture );
			metadata.baseColorIdx = pbrBaseCol.imageIdx;
			samplers.insert( pbrBaseCol.samplerIdx );

			const gltf_texture normalTex = ProcessTexture( material.normal_texture );
			metadata.normalIdx = normalTex.imageIdx;
			samplers.insert( normalTex.samplerIdx );

			const gltf_texture metallicRoughness = ProcessTexture( pbrInfo.metallic_roughness_texture );
			metadata.metallicRoughnessIdx = metallicRoughness.imageIdx;
			samplers.insert( metallicRoughness.samplerIdx );

			const gltf_texture occlusionTex = ProcessTexture( material.occlusion_texture );
			metadata.occlusionIdx = occlusionTex.imageIdx;
			samplers.insert( occlusionTex.samplerIdx );

			const gltf_texture emissiveTex = ProcessTexture( material.emissive_texture );
			metadata.emissiveIdx = emissiveTex.imageIdx;
			samplers.insert( emissiveTex.samplerIdx );

			// NOTE: will enforce all textures in a material to use the same sampler, 
			// if there's none, we'll use the default one

			// NOTE: will have -1/DEFAULT and possibly other samplers, so at most size 2
			HT_ASSERT( std::size( samplers ) <= 2 );
			auto it = std::ranges::find_if( samplers, []( i32 x ){ return x != -1; } );
			metadata.samplerIdx = ( it != std::cend( samplers ) ) ? *it : ( u16 ) DEFAULT_SAMPLER_IDX;

			materialsOut.emplace_back( metadata );
		}

		return materialsOut;
	}

	std::vector<raw_image_view> ProcessImages() const
	{
		std::vector<raw_image_view> imgOut;
		//imgOut.reserve( data->images_count );
		//for( const cgltf_image& img : HT_CGLTF_SPAN( data->images ) )
		//{
		//	HT_ASSERT( img.buffer_view );
		//	HT_ASSERT( img.buffer_view->data );
//
		//	const u8* pRawData = ( const u8* ) img.buffer_view->data + img.buffer_view->offset;
//
		//	imgOut.push_back( {
		//		.data		= { pRawData, img.buffer_view->size },
		//		.metadata	= GetGltfTextureMetadata( img )
		//	} );
		//}

		return imgOut;
	}

	struct gltf_texture
	{
		i32 imageIdx	= -1;
		i32 samplerIdx	= -1;
	};
	gltf_texture ProcessTexture( const cgltf_texture_view& texView ) const
	{
		// NOTE: bc we use TEXCOORD_0
		HT_ASSERT( 0 == texView.texcoord );
		if( nullptr == texView.texture ) return {};

		return {
			.imageIdx	= ( i32 ) cgltf_image_index( data, texView.texture->image ),
			.samplerIdx = ( i32 ) cgltf_sampler_index( data, texView.texture->sampler )
		};
	}
};
*/

#endif // !HELLTECH_HPK_GLTF_LOADER_H
