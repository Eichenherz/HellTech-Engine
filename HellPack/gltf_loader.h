#ifndef __GLTF_LOADER_H__
#define __GLTF_LOADER_H__

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NOEXCEPTION
#include <tiny_gltf.h>

#include <iostream>
#include <span>
#include <ranges>

#include <ankerl/unordered_dense.h>

#include "ht_core_types.h"
#include "ht_error.h"
#include "ht_math.h"


#include "hp_types_internal.h"

constexpr u64 DEFAULT_SAMPLER_IDX = 0;


struct gltf_raw_attr_stream
{
	const u8*	data;
	u64			elemCount;
	u64			componentCount;
	u64			componentByteSize;
	u64			strideBytes;

	constexpr u64 size() const noexcept
	{
		return elemCount;
	}
};

template<typename T>
struct gltf_typed_attr_stream : gltf_raw_attr_stream
{
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

template<typename T>
concept TinyGltfTextureInfoConcept = requires( T a ) {
	{ a.index } -> std::convertible_to<i32>;
	{ a.texCoord } -> std::convertible_to<i32>;
};

struct gltf_texture
{
	i32 imageIdx	= -1;
	i32 samplerIdx	= -1;
};

// NOTE: gltf matrices are col maj, right-handed
inline DirectX::XMMATRIX GetMatrix( std::span<const double> mIn )
{
	using namespace DirectX;
	XMMATRIX m = {};
	m.r[ 0 ] = XMVectorSet( ( float ) mIn[ 0 ], ( float ) mIn[ 4 ], ( float ) mIn[ 8 ], ( float ) mIn[ 12 ] );
	m.r[ 1 ] = XMVectorSet( ( float ) mIn[ 1 ], ( float ) mIn[ 5 ], ( float ) mIn[ 9 ], ( float ) mIn[ 13 ] );
	m.r[ 2 ] = XMVectorSet( ( float ) mIn[ 2 ], ( float ) mIn[ 6 ], ( float ) mIn[ 10 ], ( float ) mIn[ 14 ] );
	m.r[ 3 ] = XMVectorSet( ( float ) mIn[ 3 ], ( float ) mIn[ 7 ], ( float ) mIn[ 11 ], ( float ) mIn[ 15 ] );
	return m;
}

inline packed_trs GetTrsFromNode( const tinygltf::Node& node )
{
	using namespace DirectX;

	XMVECTOR xmT = XMVectorSet( 0.0f, 0.0f, 0.0f, 0.0f );
	XMVECTOR xmR = XMVectorSet( 0.0f, 0.0f, 0.0f, 1.0f );
	XMVECTOR xmS = XMVectorSet( 1.0f, 1.0f, 1.0f, 0.0f );
	if( std::size( node.matrix ) == 16 )
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
		if( std::size( node.translation ) == 3 )
		{
			xmT = XMVectorSet(
				( float ) node.translation[ 0 ],
				( float ) node.translation[ 1 ],
				( float ) node.translation[ 2 ],
				0.0f
			);
		}
		if( std::size( node.rotation ) == 4 )
		{
			xmR = XMVectorSet(
				( float ) node.rotation[ 0 ],
				( float ) node.rotation[ 1 ],
				( float ) node.rotation[ 2 ],
				( float ) node.rotation[ 3 ]
			);
		}
		if( std::size( node.scale ) == 3 )
		{
			xmS = XMVectorSet(
				( float ) node.scale[ 0 ],
				( float ) node.scale[ 1 ],
				( float ) node.scale[ 2 ],
				0.0f
			);
		}
	}

	return { 
		.t = DX_XMStoreFloat3( xmT ), 
		.r = DX_XMStoreFloat4( xmR ), 
		.s = DX_XMStoreFloat3( xmS ) 
	};
}

inline alpha_mode GltfAlphaModeToEnum( std::string_view gltfAlphaMode )
{
	if( gltfAlphaMode == "MASK" )  return ALPHA_MODE_MASK;
	if( gltfAlphaMode == "BLEND" ) return ALPHA_MODE_BLEND;
	return ALPHA_MODE_OPAQUE;
}
inline sampler_filter_mode_flags GltfFilterToFlags( i32 gltfFilter )
{
	switch( gltfFilter )
	{
	case TINYGLTF_TEXTURE_FILTER_NEAREST:                return FILTER_NEAREST;
	case TINYGLTF_TEXTURE_FILTER_LINEAR:                 return FILTER_LINEAR;
	case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST: return FILTER_NEAREST_MIPMAP_NEAREST;
	case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:  return FILTER_LINEAR_MIPMAP_NEAREST;
	case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:  return FILTER_NEAREST_MIPMAP_LINEAR;
	case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:   return FILTER_LINEAR_MIPMAP_LINEAR;
	default:                                             return FILTER_LINEAR;
	}
}
inline sampler_wrap_mode_flags GltfWrapToFlags( i32 gltfWrap )
{
	switch( gltfWrap )
	{
	case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:   return WRAP_CLAMP_TO_EDGE;
	case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT: return WRAP_MIRRORED_REPEAT;
	case TINYGLTF_TEXTURE_WRAP_REPEAT:          return WRAP_REPEAT;
	default:                                    return WRAP_CLAMP_TO_EDGE;
	}
}
inline image_metadata GetGltfTextureMetadata( const tinygltf::Image& img )
{
	image_metadata out = {
		.width	= ( u16 ) img.width,
		.height = ( u16 ) img.height,
	};

	switch( img.component )
	{
	case 1: out.component	= image_channels_t::R; break;
	case 2: out.component	= image_channels_t::RG; break;
	case 3: out.component	= image_channels_t::RGB; break;
	case 4: out.component	= image_channels_t::RGBA; break;
	default: out.component	= image_channels_t::UNKNOWN;
	}

	switch( img.bits )
	{
	case 8:  out.bits = image_bit_depth_t::B8; break;
	case 16: out.bits = image_bit_depth_t::B16; break;
	case 32: out.bits = image_bit_depth_t::B32; break;
	default: out.bits = image_bit_depth_t::UNKNOWN;
	}

	switch( img.pixel_type )
	{
	case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:		out.pixelType = image_pixel_type::UBYTE; break;
	case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:	out.pixelType = image_pixel_type::USHORT; break;
	case TINYGLTF_COMPONENT_TYPE_FLOAT:				out.pixelType = image_pixel_type::FLOAT32; break;
	default:										out.pixelType = image_pixel_type::UNKNOWN;
	}

	return out;
}

struct gltf_loader
{
	tinygltf::Model model;

	gltf_loader( std::string_view filePath )
	{
		std::string err, warn;
		if( tinygltf::TinyGLTF loader; !loader.LoadASCIIFromFile(
			&model, &err, &warn, std::string{ filePath }, tinygltf::SectionCheck::REQUIRE_ALL ) )
		{
			std::cout<< std::format("TinyGLTF LoadASCIIFromFile error: {}\nwarn: {}\n", err, warn );
			if( !loader.LoadBinaryFromFile(
				&model, &err, &warn, std::string{ filePath }, tinygltf::SectionCheck::REQUIRE_ALL ) )
			{
				std::cout<< std::format("TinyGLTF LoadBinaryFromFile error: {}\n warn: {}\n", err, warn );
				abort();
			}
		}

		HT_ASSERT( 1 == std::size( model.scenes ) );
		std::cout << "Successfully loaded the file.\n";
	}

	std::vector<u32> GetIndexBufferFromStream( const tinygltf::Accessor& idxAccessor ) const
	{
		HT_ASSERT( TINYGLTF_TYPE_SCALAR == idxAccessor.type );

		gltf_raw_attr_stream rawIdxStream = GetRawAttributeStream( idxAccessor );

		std::vector<u32> normalized( std::size( rawIdxStream ) );

		auto CasterLambda = [] ( auto v ) { return ( u32 ) v; }; 

		switch( idxAccessor.componentType )
		{
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
		{
			gltf_typed_attr_stream<u8> typedIdxStream = { rawIdxStream };
			std::ranges::copy( typedIdxStream | std::views::transform( CasterLambda ), std::begin( normalized ) );
			break;
		}
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
		{
			gltf_typed_attr_stream<u16> typedIdxStream = { rawIdxStream };
			std::ranges::copy( typedIdxStream | std::views::transform( CasterLambda ), std::begin( normalized ) );
			break;
		}
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
		{
			gltf_typed_attr_stream<u32> typedIdxStream = { rawIdxStream };
			std::ranges::copy( typedIdxStream, std::begin( normalized ) );
			break;
		}
		}
		return normalized;
	}

	struct __gltf_node
	{
		packed_trs	parentTRS;
		i32			nodeIdx = -1;
	};
	// NOTE: gltf hierarchy is a forest not a graph
	std::vector<raw_node> ProcessNodes() const
	{
		const std::vector<tinygltf::Node>& nodes = model.nodes;

		std::vector<__gltf_node> nodeStack;

		std::vector<raw_node> flatNodes;
		flatNodes.reserve( std::size( nodes ) );

		for( i32 rootNodeIdx : model.scenes[ 0 ].nodes )
		{
			nodeStack.push_back( { .parentTRS = IDENTITY_TRS, .nodeIdx = rootNodeIdx } );
		}

		while( std::size( nodeStack ) > 0 )
		{
			__gltf_node curr = nodeStack.back();
			nodeStack.pop_back();

			const tinygltf::Node& currentNode = nodes[ curr.nodeIdx ];

			packed_trs currentTrs = GetTrsFromNode( currentNode );

			packed_trs trs = XMComposePackedTRS( curr.parentTRS, currentTrs );
			flatNodes.push_back( { trs, currentNode.mesh } );

			for( i32 childNodeIdx : currentNode.children )
			{
				nodeStack.push_back( { .parentTRS = trs, .nodeIdx = childNodeIdx } );
			}
		}

		return flatNodes;
	}

	std::vector<raw_mesh> ProcessMeshes() const
	{
		u64 meshPrimitiveCount = 0;
		for( const tinygltf::Mesh& m : model.meshes )
		{
			meshPrimitiveCount += std::size( m.primitives );
		}

		std::vector<raw_mesh> meshesOut;
		meshesOut.reserve( meshPrimitiveCount );
		for( const tinygltf::Mesh& m : model.meshes )
		{
			for( const tinygltf::Primitive& primMesh : m.primitives )
			{
				// NOTE: we only handle triangle geom for now
				HT_ASSERT( TINYGLTF_MODE_TRIANGLES == primMesh.mode );

				std::vector<u32> normalizedIndexBuffer;
				if( -1 != primMesh.indices )
				{
					normalizedIndexBuffer = GetIndexBufferFromStream( model.accessors[ primMesh.indices ] );
				}
				
				// NOTE: gltf mandates this stream be present
				const gltf_typed_attr_stream<float3> posStream = {
					GetRawAttributeStream( *GetAccessorByName( "POSITION", primMesh ) ) };

				// NOTE: gltf guarantees that all present attr streams have the same element count
				const u64 attrStreamCount = std::size( posStream );

				raw_mesh mesh = {
					.name			= m.name.c_str(),
					.pos			= { std::cbegin( posStream ), std::cend( posStream ) },
					.indices		= std::move( normalizedIndexBuffer ),
					.materialIdx	= std::bit_cast<u32>( primMesh.material )
				};

				if( const tinygltf::Accessor* pAccessor = GetAccessorByName( "NORMAL", primMesh ); pAccessor )
				{
					const gltf_typed_attr_stream<float3> stream = { GetRawAttributeStream( *pAccessor ) };
					HT_ASSERT( std::size( stream ) == attrStreamCount );
					mesh.normals = { std::cbegin( stream ), std::cend( stream ) };
				}
				if( const tinygltf::Accessor* pAccessor = GetAccessorByName( "TANGENT", primMesh ); pAccessor )
				{
					const gltf_typed_attr_stream<float4> stream = { GetRawAttributeStream( *pAccessor ) };
					HT_ASSERT( std::size( stream ) == attrStreamCount );
					mesh.tans = { std::cbegin( stream ), std::cend( stream ) };
				}
				if( const tinygltf::Accessor* pAccessor = GetAccessorByName( "TEXCOORD_0", primMesh ); pAccessor )
				{
					const gltf_typed_attr_stream<float2> stream = { GetRawAttributeStream( *pAccessor ) };
					HT_ASSERT( std::size( stream ) == attrStreamCount );
					mesh.uvs = { std::cbegin( stream ), std::cend( stream ) };
				}
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

		samplersOut.reserve( std::size( model.samplers ) );
		for( const tinygltf::Sampler& sampler : model.samplers )
		{
			samplersOut.push_back( {
				.filterModeS	= GltfFilterToFlags( sampler.minFilter ),
				.filterModeT	= GltfFilterToFlags( sampler.magFilter ),
				.wrapModeS		= GltfWrapToFlags( sampler.wrapS ),
				.wrapModeT		= GltfWrapToFlags( sampler.wrapT )
			} );
		}

		return samplersOut;
	}

	std::vector<raw_material_info> ProcessMaterials() const
	{
		std::vector<raw_material_info> materialsOut;
		materialsOut.reserve( std::size( model.materials ) );

		for( u64 mi = 0; mi < std::size( model.materials ); ++mi )
		{
			const tinygltf::Material& material = model.materials[ mi ];
			const tinygltf::PbrMetallicRoughness& pbrInfo = material.pbrMetallicRoughness;

			std::string materialName = ( std::size( material.name ) ) ? material.name.c_str() : std::format( "mtrl_{}", mi );

			raw_material_info metadata = {
				.name				= std::move( materialName ),
				.baseColFactor		= {
					( float ) pbrInfo.baseColorFactor[ 0 ],
					( float ) pbrInfo.baseColorFactor[ 1 ],
					( float ) pbrInfo.baseColorFactor[ 2 ],
					( float ) pbrInfo.baseColorFactor[ 3 ]
			    },
				.metallicFactor		= ( float ) pbrInfo.metallicFactor,
				.roughnessFactor	= ( float ) pbrInfo.roughnessFactor,
				.alphaCutoff		= ( float ) material.alphaCutoff,
				.emissiveFactor		= {
					( float ) material.emissiveFactor[ 0 ],
					( float ) material.emissiveFactor[ 1 ],
					( float ) material.emissiveFactor[ 2 ]
			    },
				.alphaMode			= GltfAlphaModeToEnum( material.alphaMode )
			};

			ankerl::unordered_dense::set<i32> samplers;
			{
				const gltf_texture pbrBaseCol = ProcessTexture( pbrInfo.baseColorTexture );
				metadata.baseColorIdx = pbrBaseCol.imageIdx;
				samplers.insert( pbrBaseCol.samplerIdx );

				const gltf_texture normalTex = ProcessTexture( material.normalTexture );
				metadata.normalIdx = normalTex.imageIdx;
				samplers.insert( normalTex.samplerIdx );

				const gltf_texture metallicRoughness = ProcessTexture( pbrInfo.metallicRoughnessTexture );
				metadata.metallicRoughnessIdx = metallicRoughness.imageIdx;
				samplers.insert( metallicRoughness.samplerIdx );

				const gltf_texture occlusionTex = ProcessTexture( material.occlusionTexture );
				metadata.occlusionIdx = occlusionTex.imageIdx;
				samplers.insert( occlusionTex.samplerIdx );

				const gltf_texture emissiveTex = ProcessTexture( material.emissiveTexture );
				metadata.emissiveIdx = emissiveTex.imageIdx;
				samplers.insert( emissiveTex.samplerIdx );
			}

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
		imgOut.reserve( std::size( model.images ) );
		for( const tinygltf::Image& img : model.images )
		{
			HT_ASSERT( std::size( img.image ) );
			imgOut.push_back( {
				.data		= { std::cbegin( img.image ), std::cend( img.image ) },
				.metadata	= GetGltfTextureMetadata( img )
			} );
		}

		return imgOut;
	}

	template<TinyGltfTextureInfoConcept TexInfo>
	gltf_texture ProcessTexture( const TexInfo& texInfo ) const
	{
		if( INVALID_IDX == texInfo.index ) return {};

		// NOTE: bc we use TEXCOORD_0
		HT_ASSERT( 0 == texInfo.texCoord );
		const tinygltf::Texture& tex = model.textures[ texInfo.index ];
		return { .imageIdx = tex.source, .samplerIdx = tex.sampler };
	}

	const tinygltf::Accessor* GetAccessorByName( std::string_view name, const tinygltf::Primitive& primMesh ) const
	{
		const auto it = primMesh.attributes.find( std::string{ name } );
		if( std::cend( primMesh.attributes ) == it ) return nullptr;
		return &( model.accessors[ it->second ] );
	}

	// NOTE: according to the spec https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes
	// we could just fix these attribute's size
	gltf_raw_attr_stream GetRawAttributeStream( const tinygltf::Accessor& accessor ) const
	{
		const i32 viewIdx = accessor.bufferView;
		HT_ASSERT( INVALID_IDX != viewIdx );
		const tinygltf::BufferView& view = model.bufferViews[ viewIdx ];

		HT_ASSERT( INVALID_IDX != view.buffer );
		const tinygltf::Buffer& buff = model.buffers[ view.buffer ];

		const u64 numComponents = tinygltf::GetNumComponentsInType( accessor.type );
		const u64 componentSizeInBytes = tinygltf::GetComponentSizeInBytes( accessor.componentType );
		const u64 elemSize = numComponents * componentSizeInBytes;

		// NOTE: gltf byteStride==0 means tightly packed ( effective stride == elemSize )
		const u64 strideInBytes = ( view.byteStride == 0 ) ? elemSize : ( u64 ) view.byteStride;

		// NOTE: If a stride is provided, it must be >= element size
		HT_ASSERT( strideInBytes >= elemSize );

		const u64 baseOffset = ( u64 ) view.byteOffset + ( u64 ) accessor.byteOffset;
		HT_ASSERT( baseOffset < ( u64 ) std::size( buff.data ) );

		return { ( const u8* ) ( std::data( buff.data ) + baseOffset ), accessor.count, 
			numComponents, componentSizeInBytes, strideInBytes };
	}
};

#endif // !__GLTF_LOADER_H__
