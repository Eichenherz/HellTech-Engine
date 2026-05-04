#ifndef __GLTF_LOADER_H__
#define __GLTF_LOADER_H__

#define STB_IMAGE_IMPLEMENTATION
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <iostream>
#include <span>
#include <ranges>

#include <ankerl/unordered_dense.h>

#include "ht_core_types.h"
#include "ht_error.h"
#include "ht_math.h"


#include "hp_types_internal.h"

constexpr u64 DEFAULT_SAMPLER_IDX = 0;

#define HT_CGLTF_SPAN( ptr ) std::span{ ( ptr ), ( ptr ## _count ) }


// NOTE: can't be replaced by a span, this handles interleaved data as well
template<TRIVIAL_T T>
struct gltf_typed_attr_stream
{
	const u8*	data				= nullptr;
	u64			elemCount			= 0;
	u64			componentCount		= 0;
	u64			componentByteSize	= 0;
	u64			strideBytes			= 0;

	gltf_typed_attr_stream() = default;

	gltf_typed_attr_stream( const cgltf_accessor& accessor )
	{
		HT_ASSERT( !accessor.is_sparse );

		const cgltf_buffer_view* view = accessor.buffer_view;
		HT_ASSERT( view );
		HT_ASSERT( view->buffer );

		const cgltf_buffer* buff = view->buffer;

		const u64 numComponents = cgltf_num_components( accessor.type );
		const u64 componentSizeInBytes = cgltf_component_size( accessor.component_type );
		const u64 elemSize = numComponents * componentSizeInBytes;

		// NOTE: gltf byteStride==0 means tightly packed ( effective stride == elemSize )
		const u64 strideInBytes = ( view->stride == 0 ) ? elemSize : view->stride;

		// NOTE: If a stride is provided, it must be >= element size
		HT_ASSERT( strideInBytes >= elemSize );

		const u64 baseOffset = view->offset + accessor.offset;
		HT_ASSERT( baseOffset < buff->size );

		this->data				= ( ( const u8* ) buff->data ) + baseOffset;
		this->elemCount			= accessor.count;
		this->componentCount	= numComponents;
		this->componentByteSize	= componentSizeInBytes;
		this->strideBytes		= strideInBytes;
	}

	constexpr u64 size() const noexcept
	{
		return elemCount;
	}

	struct iterator
	{
		// NOTE: don't change these names !
		using iterator_category = std::input_iterator_tag;
		using value_type		= T;
		using difference_type	= std::ptrdiff_t;
		using pointer			= void;
		using reference			= T;

		const u8*	ptr;
		u64			strideBytes;

		T operator*() const
		{
			T out;
			std::memcpy( &out, ptr, sizeof( T ) );
			return out;
		}

		iterator& operator++()
		{
			ptr += strideBytes;
			return *this;
		}

		iterator operator++( int )
		{
			iterator tmp = *this;
			++( *this );
			return tmp;
		}

		bool operator==( const iterator& rhs ) const { return ptr == rhs.ptr; }
		bool operator!=( const iterator& rhs ) const { return ptr != rhs.ptr; }
	};

	iterator begin() const
	{
		if( 0 == elemCount ) return { nullptr, strideBytes };

		HT_ASSERT( sizeof( T ) == ( componentCount * componentByteSize ) );
		HT_ASSERT( strideBytes >= sizeof( T ) );
		return { data, strideBytes };
	}

	iterator end() const
	{
		if( 0 == elemCount ) return { nullptr, strideBytes };

		HT_ASSERT( sizeof( T ) == ( componentCount * componentByteSize ) );
		HT_ASSERT( strideBytes >= sizeof( T ) );
		return { data + elemCount * strideBytes, strideBytes };
	}
};

inline std::vector<u32> GetNormalizedIndexBufferFromStream( const cgltf_accessor* idxAccessor )
{
	if( !idxAccessor ) return {};

	HT_ASSERT( cgltf_type_scalar == idxAccessor->type );

	auto CasterLambda = [] ( auto v ) { return ( u32 ) v; };

	switch( idxAccessor->component_type )
	{
		case cgltf_component_type_r_8u:
		{
			gltf_typed_attr_stream<u8> typedIdxStream = { *idxAccessor };
			return { std::from_range, typedIdxStream | std::views::transform( CasterLambda ) };
		}
		case cgltf_component_type_r_16u:
		{
			gltf_typed_attr_stream<u16> typedIdxStream = { *idxAccessor };
			return { std::from_range, typedIdxStream | std::views::transform( CasterLambda ) };
		}
		case cgltf_component_type_r_32u:
		{
			gltf_typed_attr_stream<u32> typedIdxStream = { *idxAccessor };
			return { std::from_range, typedIdxStream | std::views::transform( CasterLambda ) };
		}
		default: HT_ASSERT( 0 && "Wrong stream type" );
	}

	return {};
}

// GLTF stores matrices column-major: mIn[col*4 + row]. DirectXMath uses row-major row-vector
// convention, so M_dx = M_gltf^T — each GLTF column becomes a DirectXMath row.
// The old code read GLTF rows into DX rows (same values, wrong layout), which put translation
// in the last column of each row instead of row 3, so XMMatrixDecompose extracted T = {0,0,0}.
inline DirectX::XMMATRIX GetMatrix( std::span<const cgltf_float> mIn )
{
	using namespace DirectX;
	XMMATRIX m = {};
	m.r[ 0 ] = XMVectorSet( mIn[ 0 ],  mIn[ 1 ],  mIn[ 2 ],  mIn[ 3 ]  );
	m.r[ 1 ] = XMVectorSet( mIn[ 4 ],  mIn[ 5 ],  mIn[ 6 ],  mIn[ 7 ]  );
	m.r[ 2 ] = XMVectorSet( mIn[ 8 ],  mIn[ 9 ],  mIn[ 10 ], mIn[ 11 ] );
	m.r[ 3 ] = XMVectorSet( mIn[ 12 ], mIn[ 13 ], mIn[ 14 ], mIn[ 15 ] );
	return m;
}

// NOTE: GLTF quaternions follow the standard convention q*v*q^{-1} (i.e. XMVector3InverseRotate),
// NOT DirectXMath's XMVector3Rotate which computes conj(q)*v*q — the opposite rotation.
// Using the wrong one mirrors child translations across rotated parent frames.
inline packed_trs XM_CALLCONV GltfComposePackedTRS( packed_trs parent, packed_trs child )
{
	using namespace DirectX;

	XMVECTOR parentT = DX_XMLoadFloat3( parent.t );
	XMVECTOR parentR = DX_XMLoadFloat4( parent.r );
	XMVECTOR parentS = DX_XMLoadFloat3( parent.s );

	XMVECTOR childT = DX_XMLoadFloat3( child.t );
	XMVECTOR childR = DX_XMLoadFloat4( child.r );
	XMVECTOR childS = DX_XMLoadFloat3( child.s );

	float3 outS = DX_XMStoreFloat3( XMVectorMultiply( parentS, childS ) );
	float4 outR = DX_XMStoreFloat4( XMQuaternionMultiply( childR, parentR ) );
	XMVECTOR transfT = XMVector3InverseRotate( XMVectorMultiply( childT, parentS ), parentR );
	float3 outT = DX_XMStoreFloat3( XMVectorAdd( parentT, transfT ) );

	return { .t = outT, .r = outR, .s = outS };
}

inline packed_trs GetTrsFromNode( const cgltf_node& node )
{
	using namespace DirectX;

	XMVECTOR xmT = XMVectorSet( 0.0f, 0.0f, 0.0f, 0.0f );
	XMVECTOR xmR = XMVectorSet( 0.0f, 0.0f, 0.0f, 1.0f );
	XMVECTOR xmS = XMVectorSet( 1.0f, 1.0f, 1.0f, 0.0f );
	if( node.has_matrix )
	{
		XMMATRIX m = GetMatrix( node.matrix );
		if( !XMMatrixDecompose( &xmS, &xmR, &xmT, m ) )
		{
			xmT = XMVectorSet( 0.0f, 0.0f, 0.0f, 0.0f );
			xmR = XMVectorSet( 0.0f, 0.0f, 0.0f, 1.0f );
			xmS = XMVectorSet( 1.0f, 1.0f, 1.0f, 0.0f );
		}
	}
	else
	{
		if( node.has_translation )
		{
			xmT = XMVectorSet( node.translation[ 0 ], node.translation[ 1 ], node.translation[ 2 ], 0.0f );
		}
		if( node.has_rotation )
		{
			xmR = XMVectorSet(
				node.rotation[ 0 ],
				node.rotation[ 1 ],
				node.rotation[ 2 ],
				node.rotation[ 3 ]
			);
		}
		if( node.has_scale )
		{
			xmS = XMVectorSet( node.scale[ 0 ], node.scale[ 1 ], node.scale[ 2 ], 0.0f );
		}
	}

	return {  .t = DX_XMStoreFloat3( xmT ),  .r = DX_XMStoreFloat4( xmR ),  .s = DX_XMStoreFloat3( xmS ) };
}

inline alpha_mode CgltfAlphaModeToEnum( cgltf_alpha_mode gltfAlphaMode )
{
	if( cgltf_alpha_mode_mask == gltfAlphaMode )  return ALPHA_MODE_MASK;
	if( cgltf_alpha_mode_blend == gltfAlphaMode ) return ALPHA_MODE_BLEND;
	return ALPHA_MODE_OPAQUE;
}
inline sampler_filter_mode_flags CgltfFilterToFlags( cgltf_filter_type gltfFilter )
{
	switch( gltfFilter )
	{
	case cgltf_filter_type_nearest:                	return FILTER_NEAREST;
	case cgltf_filter_type_linear:                 	return FILTER_LINEAR;
	case cgltf_filter_type_nearest_mipmap_nearest: 	return FILTER_NEAREST_MIPMAP_NEAREST;
	case cgltf_filter_type_linear_mipmap_nearest:  	return FILTER_LINEAR_MIPMAP_NEAREST;
	case cgltf_filter_type_nearest_mipmap_linear:  	return FILTER_NEAREST_MIPMAP_LINEAR;
	case cgltf_filter_type_linear_mipmap_linear:   	return FILTER_LINEAR_MIPMAP_LINEAR;
	default:										break;
	}
	return FILTER_LINEAR;
}
inline sampler_wrap_mode_flags CgltfWrapToFlags( cgltf_wrap_mode gltfWrap )
{
	switch( gltfWrap )
	{
	case cgltf_wrap_mode_clamp_to_edge:   return WRAP_CLAMP_TO_EDGE;
	case cgltf_wrap_mode_mirrored_repeat: return WRAP_MIRRORED_REPEAT;
	case cgltf_wrap_mode_repeat:          return WRAP_REPEAT;
	default:                              break;
	}

	return WRAP_CLAMP_TO_EDGE;
}

template<TRIVIAL_T T>
inline std::vector<T> CgltfGetAttributeStream( const cgltf_primitive& prim, cgltf_attribute_type type, i32	attrIdx )
{
	const cgltf_accessor* pAccessor = cgltf_find_accessor( &prim, type, attrIdx );
	if( !pAccessor ) return {};

	const gltf_typed_attr_stream<T> stream = { *pAccessor };
	return { std::from_range, stream };
}

struct gltf_loader
{
	// NOTE: we don't care to free OS will do this for us !!!!!
	cgltf_data* data = NULL;

	gltf_loader( const char* filePath )
	{
		cgltf_options options = {};
		HT_ASSERT( cgltf_result_success == cgltf_parse_file( &options, filePath, &data ) );
		HT_ASSERT( cgltf_result_success == cgltf_validate( data ) );
		HT_ASSERT( cgltf_result_success == cgltf_load_buffers( &options, data, filePath ) );
		HT_ASSERT( 1 == data->scenes_count );
		std::cout << "Successfully loaded the file.\n";
	}

	struct __gltf_node
	{
		packed_trs	parentTRS;
		i32			nodeIdx = -1;
	};
	// NOTE: gltf hierarchy is a forest not a graph
	std::vector<raw_node> ProcessNodes() const
	{
		std::vector<__gltf_node> nodeStack;

		std::vector<raw_node> flatNodes;
		flatNodes.reserve( data->nodes_count );

		const cgltf_scene& scene = data->scenes[ 0 ];

		for( const cgltf_node* const& rootNode : HT_CGLTF_SPAN( scene.nodes ) )
		{
			nodeStack.push_back( {
				.parentTRS	= IDENTITY_TRS,
				.nodeIdx	= ( i32 ) cgltf_node_index( data, rootNode )
			} );
		}

		while( std::size( nodeStack ) )
		{
			__gltf_node curr = nodeStack.back();
			nodeStack.pop_back();

			const cgltf_node& currentNode = data->nodes[ curr.nodeIdx ];

			packed_trs currentTrs = GetTrsFromNode( currentNode );
			packed_trs trs        = GltfComposePackedTRS( curr.parentTRS, currentTrs );

			i32 meshIdx = currentNode.mesh
				? ( i32 ) cgltf_mesh_index( data, currentNode.mesh )
				: -1;
			flatNodes.push_back( { trs, meshIdx } );

			for( const cgltf_node* const& childNode : HT_CGLTF_SPAN( currentNode.children ) )
			{
				nodeStack.push_back( {
					.parentTRS	= trs,
					.nodeIdx	= ( i32 ) cgltf_node_index( data, childNode )
				} );
			}
		}

		// NOTE: expand to primitives like ProcessMeshes
		std::vector<i32> meshFirstPrimIdx( data->meshes_count );
		i32 acc = 0;
		for( u64 mi = 0; mi < data->meshes_count; ++mi )
		{
			meshFirstPrimIdx[ mi ] = acc;
			acc += ( i32 ) data->meshes[ mi ].primitives_count;
		}

		std::vector<raw_node> flatNodesExpanded;
		flatNodesExpanded.reserve( std::size( flatNodes ) );
		for( const raw_node& n : flatNodes )
		{
			if( -1 == n.meshIdx )
			{
				flatNodesExpanded.push_back( n );
				continue;
			}

			i32 primStart = meshFirstPrimIdx[ n.meshIdx ];
			i32 primCount = ( i32 ) data->meshes[ n.meshIdx ].primitives_count;
			for( i32 pi = 0; pi < primCount; ++pi )
				flatNodesExpanded.push_back( { n.toWorld, primStart + pi } );
		}

		return flatNodesExpanded;
	}

	std::vector<raw_mesh> ProcessMeshes() const
	{
		u64 meshPrimitiveCount = 0;
		for( const cgltf_mesh& m : HT_CGLTF_SPAN( data->meshes ) )
		{
			meshPrimitiveCount += m.primitives_count;
		}

		std::vector<raw_mesh> meshesOut;
		meshesOut.reserve( meshPrimitiveCount );
		for( const cgltf_mesh& m : HT_CGLTF_SPAN( data->meshes ) )
		{
			const char* meshName = m.name ? m.name : "NamelessMesh";
			u64 meshIdx = cgltf_mesh_index( data, &m );

			for( u64 pi = 0; pi < m.primitives_count; ++pi )
			{
				const cgltf_primitive& primitive = m.primitives[ pi ];

				// NOTE: we only handle triangle geom for now
				HT_ASSERT( cgltf_primitive_type_triangles == primitive.type );


				std::string name = std::format("{}_{}_Primitive_{}", meshName, meshIdx, pi );
				// NOTE: gltf guarantees that all present attr streams have the same element count
				raw_mesh mesh = {
					.name			= MOV( name ),
					// NOTE: gltf mandates this stream be present
					.pos			= CgltfGetAttributeStream<float3>( primitive, cgltf_attribute_type_position, 0 ),
					.normals 		= CgltfGetAttributeStream<float3>( primitive, cgltf_attribute_type_normal, 0 ),
					.tans			= CgltfGetAttributeStream<float4>( primitive, cgltf_attribute_type_tangent, 0 ),
					.uvs			= CgltfGetAttributeStream<float2>( primitive, cgltf_attribute_type_texcoord, 0 ),
					.indices		= GetNormalizedIndexBufferFromStream( primitive.indices ),
					.materialIdx	= ( u32 ) cgltf_material_index( data, primitive.material )
				};

				meshesOut.emplace_back( mesh );
			}
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

			std::string materialName = ( material.name ) ? material.name : std::format( "mtrl_{}", mi );

			raw_material_info metadata = {
				.name				= MOV( materialName ),
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
	inline gltf_texture ProcessTexture( const cgltf_texture_view& texView ) const
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
