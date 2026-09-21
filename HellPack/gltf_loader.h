#pragma once

#ifndef __GLTF_LOADER_H__
#define __GLTF_LOADER_H__

#include <algorithm>
#include <iostream>
#include <span>
#include <ranges>
#include <numeric>

#include <ankerl/unordered_dense.h>

#include <ht_core_types.h>
#include <ht_error.h>
#include <ht_math.h>
#include <range_utils.h>

#include <ht_fixed_string.h>
#include <ht_array.h>

#include "gltf_helper.h"
#include "hp_types_internal.h"

#include <cgltf.h>

constexpr u64 DEFAULT_SAMPLER_IDX = 0;

#define HT_CGLTF_SPAN( ptr ) std::span{ ( ptr ), ( ptr ## _count ) }


template<TRIVIAL_T T>
struct gltf_strided_elem
{
	const u8*	data		= nullptr;
	u64			strideBytes	= 0;

	HT_FORCEINLINE const T& operator()( u64 i ) const { return ( const T& ) data[ i * strideBytes ]; }
};

template<TRIVIAL_T T>
using gltf_stream_view = std::ranges::transform_view<std::ranges::iota_view<u64, u64>, gltf_strided_elem<T>>;

// NOTE: can't be replaced by a span, this handles interleaved data as well
template<TRIVIAL_T T>
struct gltf_attr_stream
{
	gltf_stream_view<T>	view				= {};
	u64					byteOffset			= 0;
	u64					strideInBytes		= 0;

	gltf_attr_stream() = default;

	gltf_attr_stream( const gltf_stream_view<T>& view, u64 byteOffset, u64 strideInBytes )
		: view{ view }, byteOffset{ byteOffset }, strideInBytes{ strideInBytes } {}

	gltf_attr_stream( const cgltf_accessor& accessor )
	{
		HT_ASSERT( !accessor.is_sparse );

		const cgltf_buffer_view* view = accessor.buffer_view;
		HT_ASSERT( view && view->buffer );

		const cgltf_buffer* buff = view->buffer;

		const u64 numComponents         = cgltf_num_components( accessor.type );
		const u64 componentSizeInBytes  = cgltf_component_size( accessor.component_type );
		const u64 elemSize              = numComponents * componentSizeInBytes;

		// NOTE: gltf byteStride==0 means tightly packed ( effective stride == elemSize )
		const u64 stride = ( view->stride == 0 ) ? elemSize : view->stride;

		// NOTE: If a stride is provided, it must be >= element size
		HT_ASSERT( stride >= elemSize );

		const u64 baseOffset = view->offset + accessor.offset;
		HT_ASSERT( baseOffset < buff->size );

		// NOTE: a u8 stream may alias any element type as raw bytes ( deferred index streams )
		HT_ASSERT( ( sizeof( T ) == elemSize ) || ( 1 == sizeof( T ) ) );

		// NOTE: buff->data is null until cgltf_load_buffers / Bind; the view then just carries the offset
		this->view				= gltf_stream_view<T>{
		    std::ranges::iota_view<u64, u64>{ 0, accessor.count },
		    { ( ( const u8* ) buff->data ) + baseOffset, stride } };
		this->byteOffset		= baseOffset;
		this->strideInBytes		= stride;
	}

	auto begin() const { return std::ranges::begin( view ); }
	auto end() const { return std::ranges::end( view ); }
	constexpr u64 size() const { return std::ranges::size( view ); }
};

template<TRIVIAL_T T>
gltf_attr_stream<T> GltfPatchAttrStream( const gltf_attr_stream<T>& stream, std::span<const u8> buffer )
{
	HT_ASSERT( stream.byteOffset + ( std::size( stream ) - 1 ) * stream.strideInBytes + sizeof( T ) <= std::size( buffer ) );
	return {
		gltf_stream_view<T>{
			std::ranges::iota_view<u64, u64>{ 0, std::size( stream ) },
			{ std::data( buffer ) + stream.byteOffset, stream.strideInBytes } },
		stream.byteOffset,
		stream.strideInBytes
	};
}

template<TRIVIAL_T To, TRIVIAL_T From>
gltf_attr_stream<To> GltfRecastAttrStream( const gltf_attr_stream<From>& stream )
{
	HT_ASSERT( stream.strideInBytes >= sizeof( To ) );
	return {
		gltf_stream_view<To>{
			std::ranges::iota_view<u64, u64>{ 0, std::size( stream ) },
			{ ( const u8* ) &*std::begin( stream ), stream.strideInBytes } },
		stream.byteOffset,
		stream.strideInBytes
	};
}

template<TRIVIAL_T T>
std::vector<T> CgltfCopyAttrStream( const cgltf_primitive& prim, cgltf_attribute_type type, i32 attrIdx )
{
	const cgltf_accessor* pAccessor = cgltf_find_accessor( &prim, type, attrIdx );
	if( !pAccessor ) return {};

	return { std::from_range, gltf_attr_stream<T>{ *pAccessor } };
}

template<TRIVIAL_T T>
gltf_attr_stream<T> CgltfGetDeferredAttrStream( const cgltf_primitive& prim, cgltf_attribute_type type, i32 attrIdx )
{
	const cgltf_accessor* pAccessor = cgltf_find_accessor( &prim, type, attrIdx );
	return pAccessor ? gltf_attr_stream<T>{ *pAccessor } : gltf_attr_stream<T>{};
}

struct gltf_idx_view
{
	gltf_attr_stream<u8>	raw;
	cgltf_component_type	componentType = cgltf_component_type_invalid;

	gltf_idx_view() = default;
	gltf_idx_view( const gltf_attr_stream<u8>& raw, cgltf_component_type componentType )
		: raw{ raw }, componentType{ componentType } {}
	gltf_idx_view( const cgltf_accessor& accessor ) : raw{ accessor }, componentType{ accessor.component_type }
	{
		HT_ASSERT( cgltf_type_scalar == accessor.type );
	}
};

inline gltf_idx_view GltfPatchIdxStream( const gltf_idx_view& stream, std::span<const u8> buffer )
{
	return { GltfPatchAttrStream( stream.raw, buffer ), stream.componentType };
}

inline std::vector<u32> GetNormalizedIndexBufferFromAccessor( const cgltf_accessor* idxAccessor )
{
    if( !idxAccessor ) return {};

    HT_ASSERT( cgltf_type_scalar == idxAccessor->type );

    switch( idxAccessor->component_type )
    {
        case cgltf_component_type_r_8u:
            return { std::from_range, gltf_attr_stream<u8>{ *idxAccessor } | std::views::transform( HtCastTo<u32> ) };
        case cgltf_component_type_r_16u:
            return { std::from_range, gltf_attr_stream<u16>{ *idxAccessor } | std::views::transform( HtCastTo<u32> ) };
        case cgltf_component_type_r_32u: return { std::from_range, gltf_attr_stream<u32>{ *idxAccessor } };
    }

    HT_ASSERT( 0 && "Wrong stream type" );
    return {};
}

inline std::vector<u32> GetNormalizedIndexBufferFromStream( const gltf_idx_view& idxView )
{
    if( 0 == std::size( idxView.raw ) ) return {};

    switch( idxView.componentType )
    {
        case cgltf_component_type_r_8u:
            return { std::from_range, GltfRecastAttrStream<u8>( idxView.raw ) | std::views::transform( HtCastTo<u32> ) };
        case cgltf_component_type_r_16u:
            return { std::from_range, GltfRecastAttrStream<u16>( idxView.raw ) | std::views::transform( HtCastTo<u32> ) };
        case cgltf_component_type_r_32u:
            return { std::from_range, GltfRecastAttrStream<u32>( idxView.raw ) };
    }

    HT_ASSERT( 0 && "Wrong stream type" );
    return {};
}

template<arena_t ARENA_T>
arena_array<u32, ARENA_T> ReadNormalizedIndexBuffer( const gltf_idx_view& idxView, ARENA_T& arena )
{
    if( 0 == std::size( idxView.raw ) ) return { arena };

    switch( idxView.componentType )
    {
        case cgltf_component_type_r_8u:
            return { arena, std::from_range, GltfRecastAttrStream<u8>( idxView.raw ) | std::views::transform( HtCastTo<u32> ) };
        case cgltf_component_type_r_16u:
            return { arena, std::from_range, GltfRecastAttrStream<u16>( idxView.raw ) | std::views::transform( HtCastTo<u32> ) };
        case cgltf_component_type_r_32u:
            return { arena, std::from_range, GltfRecastAttrStream<u32>( idxView.raw ) };
    }

    HT_ASSERT( 0 && "Wrong stream type" );
    return { arena };
}

struct raw_mesh_desc
{
	fixed_string<128>			name;
	gltf_attr_stream<float3>	pos;
	gltf_attr_stream<float3>	normals;
	gltf_idx_view				indices;
    aabb_t<float3>              aabb;
	raw_mesh_topology_t			topology;
};

inline raw_mesh_desc GltfPatchRawMeshDesc( const raw_mesh_desc& in, std::span<const u8> bin )
{
    return {
        .name       = in.name,
        .pos        = GltfPatchAttrStream( in.pos, bin ),
        .normals    = GltfPatchAttrStream( in.normals, bin ),
        .indices    = GltfPatchIdxStream( in.indices, bin ),
        .aabb       = in.aabb,
        .topology   = in.topology
    };
}
struct gltf_loader
{
	cgltf_data* data = nullptr;

    // TODO: make ctors explicit
    gltf_loader( std::span<const u8> rawBytes, const cgltf_options& options )
    {
        HT_ASSERT( cgltf_result_success == cgltf_parse( &options, std::data( rawBytes ), std::size( rawBytes ), &data ) );
        HT_ASSERT( cgltf_result_success == cgltf_validate( data ) );
        //HT_ASSERT( cgltf_result_success == cgltf_load_buffers( &options, data, nullptr ) );
        HT_ASSERT( 1 == data->scenes_count );
    }
    gltf_loader( std::string_view inputFilePath ) : gltf_loader{ std::data( inputFilePath ) } {}
	gltf_loader( const char* filePath )
	{
		cgltf_options options = {};
		HT_ASSERT( cgltf_result_success == cgltf_parse_file( &options, filePath, &data ) );
		HT_ASSERT( cgltf_result_success == cgltf_validate( data ) );
		HT_ASSERT( cgltf_result_success == cgltf_load_buffers( &options, data, filePath ) );
		HT_ASSERT( 1 == data->scenes_count );
		std::cout << "Successfully loaded the file.\n";
	}

	std::vector<raw_node> ProcessDrawableNodes() const // NOTE: gltf hierarchy is a forest not a graph
	{
	    // NOTE: bc we expand the gltf meshes and prims into raw_mesh which are 1:1 with gltf_prims we need the offsets
	    // to keep the node hierarchy working
	    std::vector<u64> meshPrimitiveOffsets = { std::from_range, HT_CGLTF_SPAN( data->meshes )
	        | std::views::transform( &cgltf_mesh::primitives_count ) };
	    std::exclusive_scan( std::begin( meshPrimitiveOffsets ), std::end( meshPrimitiveOffsets ),
            std::begin( meshPrimitiveOffsets ), u64( 0 ) );

		// NOTE: use set to dedup nodes
		ankerl::unordered_dense::set<raw_node> flatNodesExpanded;
		flatNodesExpanded.reserve( data->nodes_count ); // NOTE: this is just a best guess

		auto LmbdVisitNode = [ & ]( this auto&& PfnSelf, const cgltf_node& node, packed_trs parentTrs ) -> void
		{
			if( CgltfIsNodeHidden( node ) ) return;

			packed_trs trs = GltfComposePackedTRS( parentTrs, GetTrsFromNode( node ) );

			if( node.mesh )
			{
				u64 meshPrimOffset = meshPrimitiveOffsets[ cgltf_mesh_index( data, node.mesh ) ];
				u64 instCount      = node.has_mesh_gpu_instancing ? node.mesh_gpu_instancing.attributes[ 0 ].data->count : 1;

				for( u64 ii = 0; ii < instCount; ++ii )
				{
					packed_trs instTrs = node.has_mesh_gpu_instancing ?
				        GltfComposePackedTRS( trs, GltfGetTRSFromExtGpuInst( node, ii ) ) : trs;
					for( u64 pi = 0; pi < node.mesh->primitives_count; ++pi )
					{
						flatNodesExpanded.emplace( raw_node{
						    .toWorld    = instTrs,
						    .aabb       = CgltfGetPosStreamBounds( node.mesh->primitives[ pi ] ),
						    .meshIdx    = meshPrimOffset + pi
						} );
					}
				}
			}

			for( const cgltf_node* child : HT_CGLTF_SPAN( node.children ) ) PfnSelf( *child, trs );
		};

		for( const cgltf_node* root : HT_CGLTF_SPAN( data->scenes[ 0 ].nodes ) )
		{
		    LmbdVisitNode( *root, IDENTITY_TRS );
		}

		return { std::from_range, flatNodesExpanded };
	}

	std::vector<raw_mesh> ProcessPrimitives() const
	{
		std::vector<raw_mesh> meshesOut = {};
	    std::vector<raw_mesh> prims = {};

		for( u64 mi = 0; mi < data->meshes_count; ++mi )
		{
			const cgltf_mesh& m = data->meshes[ mi ];

            prims.resize( m.primitives_count );
			for( u64 pi = 0; pi < m.primitives_count; ++pi )
			{
				const cgltf_primitive& primitive = m.primitives[ pi ];

				// TODO: how to export more stuff ?
				// NOTE: gltf guarantees that all present attr streams have the same element count
				prims[ pi ] = {
					.name			= { "{}_{}_Primitive_{}", m.name ? m.name : "Mesh", mi, pi },
					// NOTE: gltf mandates that the pos stream be present
					.pos			= CgltfCopyAttrStream<float3>( primitive, cgltf_attribute_type_position, 0 ),
					.normals 		= CgltfCopyAttrStream<float3>( primitive, cgltf_attribute_type_normal, 0 ),
					.tans			= CgltfCopyAttrStream<float4>( primitive, cgltf_attribute_type_tangent, 0 ),
					.uvs			= CgltfCopyAttrStream<float2>( primitive, cgltf_attribute_type_texcoord, 0 ),
					.indices		= GetNormalizedIndexBufferFromAccessor( primitive.indices ),
					.materialIdx	= ~0u,//( u32 ) cgltf_material_index( data, primitive.material ),
				    .topology       = CgltfPrimitiveTypeToTopology( primitive.type )
				};
			}
		    meshesOut.append_range( prims );
		}

		return meshesOut;
	}

    std::vector<raw_mesh_desc> ProcessPrimitivesAttributes() const
    {
        std::vector<raw_mesh_desc> meshesOut = {};
        std::vector<raw_mesh_desc> prims = {};

        for( u64 mi = 0; mi < data->meshes_count; ++mi )
        {
            const cgltf_mesh& m = data->meshes[ mi ];

            prims.resize( m.primitives_count );
            for( u64 pi = 0; pi < m.primitives_count; ++pi )
            {
                const cgltf_primitive& primitive = m.primitives[ pi ];

                // TODO: how to export more stuff ?
                // NOTE: gltf guarantees that all present attr streams have the same element count
                const cgltf_accessor* pPos  = cgltf_find_accessor( &primitive, cgltf_attribute_type_position, 0 );
                const cgltf_accessor* pNorm = cgltf_find_accessor( &primitive, cgltf_attribute_type_normal, 0 );

                prims[ pi ] = {
                    .name				= { "{:.64}_{}_Primitive_{}", m.name ? m.name : "Mesh", mi, pi },
                    // NOTE: gltf mandates that the pos stream be present
                    .pos				= gltf_attr_stream<float3>{ *pPos },
                    .normals 			= pNorm ? gltf_attr_stream<float3>{ *pNorm } : gltf_attr_stream<float3>{},
                    .indices			= primitive.indices ? gltf_idx_view{ *primitive.indices } : gltf_idx_view{},
                    .aabb               = CgltfGetPosStreamBounds( primitive ),
                    .topology			= CgltfPrimitiveTypeToTopology( primitive.type )
                };
            }
            meshesOut.append_range( prims );
        }

        return meshesOut;
    }

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

#endif // !__GLTF_LOADER_H__
