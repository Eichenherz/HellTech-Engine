#pragma once

#ifndef __GLTF_LOADER_H__
#define __GLTF_LOADER_H__

#include <span>
#include <ranges>

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


// NOTE: can't be replaced by a span, this handles interleaved data as well
template<TRIVIAL_T T>
struct gltf_attr_stream
{
	std::span<const u8>	bytes				= {};
	u64					count				= 0;
	u64					byteOffset			= 0;
	u64					strideInBytes		= sizeof( T );

	gltf_attr_stream() = default;

	gltf_attr_stream( std::span<const u8> bytes, u64 count, u64 byteOffset, u64 strideInBytes )
		: bytes{ bytes }, count{ count }, byteOffset{ byteOffset }, strideInBytes{ strideInBytes } {}

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
		const u64 bytesSize = accessor.count ? ( accessor.count - 1 ) * stride + elemSize : 0;

		this->bytes		    = { ( ( const u8* ) buff->data ) + baseOffset, bytesSize };
		this->count		    = accessor.count;
		this->byteOffset    = baseOffset;
		this->strideInBytes = stride;
	}

	constexpr u64 size() const { return count; }
};

template<typename To = void, TRIVIAL_T From>
auto GltfTypedView( const gltf_attr_stream<From>& stream )
{
	using elem_t = std::conditional_t<std::is_void_v<To>, From, To>;
	HT_ASSERT( stream.strideInBytes >= sizeof( elem_t ) );
	return stream.bytes
        | std::views::stride( stream.strideInBytes )
		| std::views::transform( HtReinterpretAs<elem_t> );
}

template<TRIVIAL_T T>
gltf_attr_stream<T> GltfPatchAttrStream( const gltf_attr_stream<T>& stream, std::span<const u8> buffer )
{
	HT_ASSERT( stream.byteOffset + std::size( stream.bytes ) <= std::size( buffer ) );
	return {
		{ std::data( buffer ) + stream.byteOffset, std::size( stream.bytes ) },
		stream.count,
		stream.byteOffset,
		stream.strideInBytes
	};
}

template<TRIVIAL_T T>
gltf_attr_stream<T> CgltfGetDeferredAttrStream( const cgltf_primitive& prim, cgltf_attribute_type type, i32 attrIdx )
{
	const cgltf_accessor* pAccessor = cgltf_find_accessor( &prim, type, attrIdx );
	return pAccessor ? gltf_attr_stream<T>{ *pAccessor } : gltf_attr_stream<T>{};
}

inline auto GtlfGetIdx32View( const gltf_attr_stream<u8>& idxStream )
{
    using pfn_idx32 = u32( * )( const u8& );

    auto emptyView = std::span<const u8>{} | std::views::stride( 1 ) | std::views::transform( pfn_idx32{} );

    if( 0 == std::size( idxStream ) ) return emptyView;

    switch( idxStream.strideInBytes )
    {
        case sizeof( u8 ): return idxStream.bytes | std::views::stride( idxStream.strideInBytes )
            | std::views::transform( pfn_idx32( []( const u8& b ) -> u32 { return HtReinterpretAs<u8>( b ); } ) );
        case sizeof( u16 ): return idxStream.bytes | std::views::stride( idxStream.strideInBytes )
            | std::views::transform( pfn_idx32( []( const u8& b ) -> u32 { return HtReinterpretAs<u16>( b ); } ) );
        case sizeof( u32 ): return idxStream.bytes | std::views::stride( idxStream.strideInBytes )
            | std::views::transform( pfn_idx32( []( const u8& b ) -> u32 { return HtReinterpretAs<u32>( b ); } ) );
    }

    HT_ASSERT( 0 && "Wrong stream type" );
    return emptyView;
}

struct raw_mesh_desc
{
	hpk_mesh_name			    name;
    u64                         meshHash;
	gltf_attr_stream<float3>	pos;
	gltf_attr_stream<float3>	normals;
	gltf_attr_stream<u8>		indices;
    aabb_t<float3>              aabb;
	raw_mesh_topology_t			topology;
};

inline raw_mesh_desc GltfPatchRawMeshDesc( const raw_mesh_desc& in, std::span<const u8> bin )
{
    return {
        .name       = in.name,
        .meshHash   = in.meshHash,
        .pos        = GltfPatchAttrStream( in.pos, bin ),
        .normals    = GltfPatchAttrStream( in.normals, bin ),
        .indices    = GltfPatchAttrStream( in.indices, bin ),
        .aabb       = in.aabb,
        .topology   = in.topology
    };
}

inline raw_mesh_desc CgltfParseRawMeshDesc(
    const cgltf_mesh&       parentMesh,
    const cgltf_primitive&  primitive,
    std::string_view        originFileName,
    u64                     meshIdx,
    u64                     primIdx
) {
    // TODO: how to export more stuff ?
    // NOTE: gltf guarantees that all present attr streams have the same element count
    const cgltf_accessor* pPos  = cgltf_find_accessor( &primitive, cgltf_attribute_type_position, 0 );
    const cgltf_accessor* pNorm = cgltf_find_accessor( &primitive, cgltf_attribute_type_normal, 0 );

    hpk_mesh_name name = { "{}_{:.60}_{}_Primitive_{}", originFileName,
        parentMesh.name ? parentMesh.name : "Mesh", meshIdx, primIdx
    };

    return {
        .name		= name,
        .meshHash   = HpkHashMeshName( name ),
        // NOTE: gltf mandates that the pos stream be present
        .pos		= gltf_attr_stream<float3>{ *pPos },
        .normals 	= pNorm             ? gltf_attr_stream<float3>{ *pNorm }    : gltf_attr_stream<float3>{},
        .indices	= primitive.indices ? gltf_attr_stream<u8>{ *primitive.indices } : gltf_attr_stream<u8>{},
        .aabb       = CgltfGetPosStreamBounds( primitive ),
        .topology   = CgltfPrimitiveTypeToTopology( primitive.type )
    };
}

struct gltf_loader
{
    cgltf_data* data = nullptr;

};

inline const cgltf_data* CgltfLoadMetadataFromRawBytes( std::span<const u8> rawBytes, const cgltf_options& options )
{
    cgltf_data* data = nullptr;
    HT_ASSERT( cgltf_result_success == cgltf_parse( &options, std::data( rawBytes ), std::size( rawBytes ), &data ) );
    HT_ASSERT( cgltf_result_success == cgltf_validate( data ) );
    //HT_ASSERT( cgltf_result_success == cgltf_load_buffers( &options, data, nullptr ) );
    HT_ASSERT( 1 == data->scenes_count );
    return data;
}

struct parsed_gltf
{
    std::vector<raw_node>       nodes;
    std::vector<raw_mesh_desc>  meshDesc;
};

inline parsed_gltf CgltfProcessDrawablesHierarchy( const cgltf_data* data, std::string_view originFileName )
{
    std::vector<raw_node> flatNodes;
    flatNodes.reserve( data->nodes_count );

    ankerl::unordered_dense::map<const cgltf_primitive*, raw_mesh_desc> rawMeshDescMap;
    rawMeshDescMap.reserve( data->meshes_count * 4 ); // NOTE: this is just a best guess

    auto LmbdVisitNode = [ & ]( this auto&& PfnSelf, const cgltf_node& node, packed_trs parentTrs ) -> void
    {
        if( CgltfIsNodeHidden( node ) ) return;

        packed_trs trs = GltfComposePackedTRS( parentTrs, GetTrsFromNode( node ) );

        if( node.mesh )
        {
            u64 instCount = node.has_mesh_gpu_instancing ? node.mesh_gpu_instancing.attributes[ 0 ].data->count : 1;

            const cgltf_mesh& m = *node.mesh;
            for( const cgltf_primitive* pPrim = m.primitives; pPrim < ( m.primitives + m.primitives_count ); ++pPrim )
            {
                auto iterMeshDesc = rawMeshDescMap.find( pPrim );
                if( std::end( rawMeshDescMap ) == iterMeshDesc )
                {
                    raw_mesh_desc desc = CgltfParseRawMeshDesc( m, *pPrim, originFileName,
                        &m - data->meshes, pPrim - m.primitives );
                    iterMeshDesc = rawMeshDescMap.emplace( pPrim, desc ).first;
                }

                for( u64 ii = 0; ii < instCount; ++ii )
                {
                    packed_trs instTrs = node.has_mesh_gpu_instancing ?
                        GltfComposePackedTRS( trs, GltfGetTRSFromExtGpuInst( node, ii ) ) : trs;
                    flatNodes.push_back( {
                        .toWorld    = instTrs,
                        .aabb       = iterMeshDesc->second.aabb,
                        .meshHash   = HpkHashMeshName( iterMeshDesc->second.name )
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

    return {
        .nodes      = MOV( flatNodes ),
        .meshDesc   = { std::from_range, rawMeshDescMap | std::views::values }
    };
}

/*
struct [[ depracated ]] gltf_loader
{
	cgltf_data* data = nullptr;

    // TODO: make ctors explicit
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

#endif // !__GLTF_LOADER_H__
