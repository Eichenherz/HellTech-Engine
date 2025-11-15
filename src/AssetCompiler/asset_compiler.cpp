#include "asset_compiler.h"

#include <meshoptimizer.h>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>
#include "spng.h"

#include "r_data_structs.h"
#include <ranges>
#include <vector>

#include <span>
#include <string_view>

#include <string.h>

#include "ht_error.h"
#include "ht_utils.h"
#include "core_types.h"	


#include "ht_math.h"
#include "encoding.h"

#include <DirectXCollision.h>

#include <ankerl/unordered_dense.h>

#include "texture_compressor.h"
#include "bcn_compressor.h"

#include <System/sys_file.h>
#include <filesystem>
namespace stdfs = std::filesystem;

struct alignas( 16 ) packed_trs
{
	vec3 t;
	float pad0;
	vec4 r;
	vec3 s;
	float pad1;
};

inline packed_trs XM_CALLCONV XMComposePackedTRS( packed_trs a, packed_trs b )
{
	using namespace DirectX;

	XMVECTOR aT = XMLoadFloat3( &a.t );
	XMVECTOR aR = XMLoadFloat4( &a.r );
	XMVECTOR aS = XMLoadFloat3( &a.s );

	XMVECTOR bT = XMLoadFloat3( &b.t );
	XMVECTOR bR = XMLoadFloat4( &b.r );
	XMVECTOR bS = XMLoadFloat3( &b.s );

	vec3 outT;
	XMStoreFloat3( &outT, XMVectorAdd( aT, bT ) );
	vec4 outR;
	XMStoreFloat4( &outR, XMQuaternionMultiply( aR, bR ) ); // World = Parent * Local
	vec3 outS;
	XMStoreFloat3( &outS, XMVectorMultiply( aS, bS ) );

	return { .t = outT, .r = outR, .s = outS };
}

struct gltf_node
{
	packed_trs transform;
	i32 meshIdx;
};

enum class index_type : u8
{
	U8,
	U16,
	U32
};

struct gltf_index_span
{
	index_type type;
	union
	{
		std::span<const u8>  u8Data;
		std::span<const u16> u16Data;
		std::span<const u32> u32Data;
	};

	gltf_index_span( std::span<const u8> span ) : type{ index_type::U8 }, u8Data{ span } {}
	gltf_index_span( std::span<const u16> span ) : type{ index_type::U16 }, u16Data{ span } {}
	gltf_index_span( std::span<const u32> span ) : type{ index_type::U32 }, u32Data{ span } {}
};

constexpr bool LEFT_HANDED = true;
static_assert( LEFT_HANDED );

struct raw_mesh
{
	std::string name;
	std::vector<DirectX::XMFLOAT3> pos;
	std::vector<DirectX::XMFLOAT3> normals;
	std::vector<DirectX::XMFLOAT3> tans;
	std::vector<DirectX::XMFLOAT2> uvs;
	std::vector<u32> indices;
	i32 materialIdx;
};

// TODO: we'll need to adjust the normal and tan stuff if we use !LEFT_HANDED
struct gltf_mesh_view
{
	std::string name;
	gltf_index_span indexStream;
	std::span<const DirectX::XMFLOAT3> posStream;
	std::span<const DirectX::XMFLOAT3> normStream;
	std::span<const DirectX::XMFLOAT4> tanStream;
	std::span<const DirectX::XMFLOAT2> uvStream;
	i32 materialIdx;

	inline std::vector<u32> NormalizeIndexBuffer() const
	{
		std::vector<u32> normalized;
		switch( indexStream.type )
		{
		case index_type::U8:
		{
			normalized.reserve( std::size( indexStream.u8Data ) );
			for( u8 idx : indexStream.u8Data )
			{
				normalized.push_back( u32( idx ) );
			}
			break;
		}
		case index_type::U16:
		{
			normalized.reserve( std::size( indexStream.u16Data ) );
			for( u16 idx : indexStream.u16Data )
			{
				normalized.push_back( u32( idx ) );
			}
			break;
		}
		case index_type::U32:
		{
			normalized.reserve( std::size( indexStream.u32Data ) );
			for( u32 idx : indexStream.u32Data )
			{
				normalized.push_back( idx );
			}
			break;
		}
		}
		return normalized;
	}

	inline raw_mesh GetRawMesh() const
	{
		const u64 streamSize = std::size( posStream );
		HT_ASSERT( ( streamSize == std::size( normStream ) ) &&
				   ( streamSize == std::size( tanStream ) ) &&
				   ( streamSize == std::size( uvStream ) ) );

		std::vector<DirectX::XMFLOAT3> tans;
		tans.reserve( std::size( tanStream ) );

		for( DirectX::XMFLOAT4 tanW : tanStream )
		{
			tans.push_back( { tanW.x * tanW.w, tanW.y * tanW.w, tanW.z * tanW.w } );
		}
		std::vector<u32> indices = NormalizeIndexBuffer();

		raw_mesh mesh = {
			.name = std::move( name ),
			.pos = { std::cbegin( posStream ), std::cend( posStream ) },
			.normals = { std::cbegin( normStream ), std::cend( normStream ) },
			.tans = std::move( tans ),
			.uvs = { std::cbegin( uvStream ), std::cend( uvStream ) },
			.indices = std::move( indices ),
			.materialIdx = materialIdx
		};

		return mesh;
	}
};

template<typename T>
concept TinyGltfTextureInfoConcept = requires( T a ) {
	{ a.index } -> std::convertible_to<int>;
	{ a.texCoord } -> std::convertible_to<int>;
};

enum class gltf_img_components : u8
{
	UNKNOWN = 0,
	R = 1,
	RG = 2,
	RGB = 3,
	RGBA = 4
};

enum class gltf_img_bit_depth : u8
{
	UNKNOWN = 0,
	B8  = 8,
	B16 = 16,
	B32 = 32
};

enum class gltf_img_pixel_type : u8
{
	UNKNOWN = 0,
	UBYTE,
	USHORT,
	FLOAT32
};

struct gltf_image_metadata
{
	i32 width;
	i32 height;
    gltf_img_components component;
    gltf_img_bit_depth  bits;
    gltf_img_pixel_type pixelType;
};

struct gltf_texture
{
	i32 imageIdx = -1;
	i32 samplerIdx = -1;
};

struct gltf_image
{
	std::span<const u8> data;
	gltf_image_metadata metadata;
};

struct gltf_processor
{
	tinygltf::Model model;

	gltf_processor( std::string_view filePath )
	{
		std::string err, warn;
		if( tinygltf::TinyGLTF loader; !loader.LoadASCIIFromFile(
			&model, &err, &warn, std::string{ filePath }, tinygltf::SectionCheck::REQUIRE_ALL ) )
		{
			std::cout<< std::format("TinyGLTF LoadASCIIFromFile error: {}\n warn: {}\n", err, warn );
			if( !loader.LoadBinaryFromFile(
				&model, &err, &warn, std::string{ filePath }, tinygltf::SectionCheck::REQUIRE_ALL ) )
			{
				std::cout<< std::format(""
							"TinyGLTF LoadBinaryFromFile error: {}\n warn: {}\n", err, warn );
				abort();
			}
		}

		HT_ASSERT( 1 == std::size( model.scenes ) );
	}

	std::vector<gltf_node> ProcessNodes() const
	{
		const std::vector<tinygltf::Node>& nodes = model.nodes;

		struct queued_node
		{
			packed_trs parentTRS;
			u32 nodeIdx;
		};
		std::vector<queued_node> nodeQueue;
		// NOTE: we need this bc our tree is flattened
		std::vector<bool> visited( std::size( nodes ), false );

		std::vector<gltf_node> flatNodes;
		flatNodes.reserve( std::size( nodes ) );

		for( u32 nodeIdx = 0; nodeIdx < std::size( nodes ); ++nodeIdx )
		{
			if( visited[ nodeIdx ] ) continue;

			const tinygltf::Node& n = nodes[ nodeIdx ];

			packed_trs pkTrs = GetTrsFromNode( n );

			nodeQueue.push_back( { pkTrs, nodeIdx } );
			while( std::size( nodeQueue ) > 0 )
			{
				queued_node curr = nodeQueue.back();
				nodeQueue.pop_back();

				const tinygltf::Node& currentNode = nodes[ curr.nodeIdx ];
				packed_trs currentTrs = GetTrsFromNode( currentNode );

				packed_trs parentTrs = XMComposePackedTRS( curr.parentTRS, currentTrs );
				flatNodes.push_back( { parentTrs, currentNode.mesh } );
				visited[ curr.nodeIdx ] = true;

				for( u32 childNodeIdx : currentNode.children )
				{
					nodeQueue.push_back( { parentTrs, childNodeIdx } );
				}
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
				const i32 materialIdx = primMesh.material;

				HT_ASSERT( -1 != primMesh.indices );

				const gltf_attr_stream idxStream = GetAttributeStream( model.accessors[ primMesh.indices ] );
				const gltf_attr_stream posStream = GetAttributeStream( GetAccessorByName( "POSITION", primMesh ) );
				const gltf_attr_stream normStream = GetAttributeStream( GetAccessorByName( "NORMAL", primMesh ) );
				const gltf_attr_stream tanStream = GetAttributeStream( GetAccessorByName( "TANGENT", primMesh ) );
				const gltf_attr_stream uvStream = GetAttributeStream( GetAccessorByName( "TEXCOORD_0", primMesh ) );

				// NOTE: the cast to span operator will ASSERT our type matches the size and elem count
				gltf_mesh_view currentMesh = {
					.name = m.name.c_str(),
					.indexStream = ( gltf_index_span ) idxStream,
					.posStream = ( std::span<const DirectX::XMFLOAT3> ) posStream,
					.normStream = ( std::span<const DirectX::XMFLOAT3> ) normStream,
					.tanStream = ( std::span<const DirectX::XMFLOAT4> ) tanStream,
					.uvStream = ( std::span<const DirectX::XMFLOAT2> ) uvStream,
					.materialIdx = materialIdx
				};
				raw_mesh mesh = currentMesh.GetRawMesh();
				meshesOut.emplace_back( mesh );
			}
		}

		return meshesOut;
	}

	std::vector<sampler_config> ProcessSamplers() const
	{
		std::vector<sampler_config> samplersOut;
		samplersOut.reserve( std::size( model.samplers ) );
		for( const tinygltf::Sampler& sampler : model.samplers )
		{
			sampler_config samplerConfig = {
				.filterModeS = GltfFilterToFlags( sampler.minFilter ),
				.filterModeT = GltfFilterToFlags( sampler.magFilter ),
				.wrapModeS = GltfWrapToFlags( sampler.wrapS ),
				.wrapModeT = GltfWrapToFlags( sampler.wrapT )
			};
			samplersOut.push_back( samplerConfig );
		}
		if( std::size( model.samplers ) == 0 )
		{
			samplersOut.push_back( DEFAULT_SAMPLER );
		}

		return samplersOut;
	}

	std::vector<material_info> ProcessMaterials() const
	{
		std::vector<material_info> materialsOut;
		materialsOut.reserve( std::size( model.materials ) );
		for( const tinygltf::Material& material : model.materials )
		{
			const tinygltf::PbrMetallicRoughness& pbrInfo = material.pbrMetallicRoughness;

			material_info metadata = {
				.baseColFactor = {
					( float ) pbrInfo.baseColorFactor[ 0 ],
					( float ) pbrInfo.baseColorFactor[ 1 ],
					( float ) pbrInfo.baseColorFactor[ 2 ],
					( float ) pbrInfo.baseColorFactor[ 3 ]
				},
				.metallicFactor = ( float ) pbrInfo.metallicFactor,
				.roughnessFactor = ( float ) pbrInfo.roughnessFactor,
				.alphaCutoff = ( float ) material.alphaCutoff,
				.emissiveFactor = {
					( float ) material.emissiveFactor[ 0 ],
					( float ) material.emissiveFactor[ 1 ],
					( float ) material.emissiveFactor[ 2 ]
				},
				.alphaMode = GltfAlphaModeToEnum( material.alphaMode )
			};

			ankerl::unordered_dense::set<i32> samplers;
			const gltf_texture normalTex = ProcessTexture( material.normalTexture );
			samplers.insert( normalTex.samplerIdx );
			metadata.normalIdx = normalTex.imageIdx;

			const gltf_texture pbrBaseCol = ProcessTexture( pbrInfo.baseColorTexture );
			samplers.insert( pbrBaseCol.samplerIdx );
			metadata.baseColorIdx = pbrBaseCol.imageIdx;

			const gltf_texture metallicRoughness = ProcessTexture( pbrInfo.metallicRoughnessTexture );
			samplers.insert( metallicRoughness.samplerIdx );
			metadata.metallicRoughnessIdx = metallicRoughness.imageIdx;

			const gltf_texture occlusionTex = ProcessTexture( material.occlusionTexture );
			samplers.insert( occlusionTex.samplerIdx );
			metadata.occlusionIdx = occlusionTex.imageIdx;

			const gltf_texture emissiveTex = ProcessTexture( material.emissiveTexture );
			samplers.insert( emissiveTex.samplerIdx );
			metadata.emissiveIdx = emissiveTex.imageIdx;

			// NOTE: we will have -1 and possibly others so at most size 2
			HT_ASSERT( std::size( samplers ) <= 2 );

			auto it = std::ranges::find_if( samplers,
				[]( i32 x ){ return x != -1; });
			metadata.samplerIdx = ( it != std::cend( samplers ) ) ? *it : -1;

			materialsOut.emplace_back( metadata );
		}

		return materialsOut;
	}

	std::vector<gltf_image> ProcessImages() const
	{
		std::vector<gltf_image> imgOut;
		imgOut.reserve( std::size( model.images ) );
		for( const tinygltf::Image& img : model.images )
		{
			HT_ASSERT( std::size( img.image ) );
			imgOut.push_back( {
				.data = std::span<const u8>{ std::data( img.image ), std::size( img.image ) },
				.metadata = GetGltfTextureMetadata( img )
			} );
		}

		return imgOut;
	}

	template<TinyGltfTextureInfoConcept TexInfo>
	inline gltf_texture ProcessTexture( const TexInfo& texInfo ) const
	{
		gltf_texture texOut = {};
		if( -1 != texInfo.index )
		{
			// NOTE: bc we use TEXCOORD_0
			HT_ASSERT( 0 == texInfo.texCoord );
			const tinygltf::Texture& tex = model.textures[ texInfo.index ];
			texOut = {
				.imageIdx = tex.source,
				.samplerIdx = tex.sampler
			};
		}

		return texOut;
	}
	// UTILS:
	static inline packed_trs GetTrsFromNode( const tinygltf::Node& node )
	{
		using namespace DirectX;

		XMVECTOR xmT = XMVectorSet( 0, 0, 0, 0 );
		XMVECTOR xmR = XMVectorSet( 0, 0, 0, 1 );
		XMVECTOR xmS = XMVectorSet( 1, 1, 1, 0 );
		if( std::size( node.matrix ) == 16 )
		{
			XMMATRIX m = GetMatrix( node.matrix );
			if( !XMMatrixDecompose( &xmS, &xmR, &xmT, m ) )
			{
				xmT = XMVectorSet( 0, 0, 0, 0 );
				xmR = XMVectorSet( 0, 0, 0, 1 );
				xmS = XMVectorSet( 1, 1, 1, 0 );
			}

		}
		else
		{
			if( std::size( node.translation ) == 3 )
			{
				xmT = XMVectorSet(
					(float)node.translation[0],
					(float)node.translation[1],
					(float)node.translation[2],
					0.0f
				);
			}
			if( std::size( node.rotation ) == 4 )
			{
				xmR = XMVectorSet(
					(float)node.rotation[0],
					(float)node.rotation[1],
					(float)node.rotation[2],
					(float)node.rotation[3]
				);
			}
			if( std::size( node.scale ) == 3 )
			{
				xmS = XMVectorSet(
					(float)node.scale[0],
					(float)node.scale[1],
					(float)node.scale[2],
					0.0f
				);
			}
		}

		vec3 t;
		vec4 r;
		vec3 s;

		XMStoreFloat3( &t, xmT );
		XMStoreFloat4( &r, xmR );
		XMStoreFloat3( &s, xmS );
		return { .t = t, .r = r, .s = s };
	}
	// NOTE: gltf matrices are col maj, right-handed
	static inline DirectX::XMMATRIX GetMatrix( const std::vector<double>& mIn )
	{
		using namespace DirectX;
		XMMATRIX m = XMMatrixSet(
			(float)mIn[0],  (float)mIn[1],  (float)mIn[2],  (float)mIn[3],
			(float)mIn[4],  (float)mIn[5],  (float)mIn[6],  (float)mIn[7],
			(float)mIn[8],  (float)mIn[9],  (float)mIn[10], (float)mIn[11],
			(float)mIn[12], (float)mIn[13], (float)mIn[14], (float)mIn[15]
		);

		return XMMatrixTranspose( m );
	}

	inline const tinygltf::Accessor& GetAccessorByName( std::string_view name, const tinygltf::Primitive& primMesh ) const
	{
		const auto it = primMesh.attributes.find( std::string{ name } );
		HT_ASSERT( std::cend( primMesh.attributes ) != it );
		return model.accessors[ it->second ];
	}

	struct gltf_attr_stream
	{
		const u8* data;
		u64 elemCount;
		u64 component;
		u64 componentByteSize;

		template<typename T>
		explicit operator std::span<T>() const
		{
			HT_ASSERT( sizeof( T ) == ( component * componentByteSize ) );
			return std::span<T>( ( T* ) data, elemCount );
		}

		explicit operator gltf_index_span() const
		{
			switch( componentByteSize )
			{
				case sizeof( u8 ) : return { std::span<const u8>{ data, elemCount } };
				case sizeof( u16 ) : return { std::span<const u16>{ ( const u16* ) data, elemCount } };
				case sizeof( u32 ) : return { std::span<const u32>{ ( const u32* ) data, elemCount } };

				default: HT_ASSERT( false );
			}
		}
	};
	// NOTE: according to the spec https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes
	// we could just fix these attribute's size
	inline gltf_attr_stream GetAttributeStream( const tinygltf::Accessor& accessor ) const
	{
		const i32 viewIdx = accessor.bufferView;
		HT_ASSERT( -1 != viewIdx );
		const tinygltf::BufferView& view = model.bufferViews[ viewIdx ];

		HT_ASSERT( -1 != view.buffer );
		const tinygltf::Buffer& buff = model.buffers[ view.buffer ];
		const u8* streamView = ( const u8* ) ( std::data( buff.data ) + view.byteOffset + accessor.byteOffset );

		const u64 numComponents = tinygltf::GetNumComponentsInType( accessor.type );
		const u64 componentSizeInBytes = tinygltf::GetComponentSizeInBytes( accessor.componentType );
		HT_ASSERT( view.byteStride == ( numComponents * componentSizeInBytes ) );

		return { streamView, accessor.count, numComponents, componentSizeInBytes };
	}

	static inline alpha_mode GltfAlphaModeToEnum( std::string_view gltfAlphaMode )
	{
		if( gltfAlphaMode == "MASK" )  return ALPHA_MODE_MASK;
		if( gltfAlphaMode == "BLEND" ) return ALPHA_MODE_BLEND;
		return ALPHA_MODE_OPAQUE;
	}

	static inline sampler_filter_mode_flags GltfFilterToFlags( int gltfFilter )
	{
		switch( gltfFilter )
		{
			case TINYGLTF_TEXTURE_FILTER_NEAREST:                 return FILTER_NEAREST;
			case TINYGLTF_TEXTURE_FILTER_LINEAR:                  return FILTER_LINEAR;
			case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST: return FILTER_NEAREST_MIPMAP_NEAREST;
			case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:  return FILTER_LINEAR_MIPMAP_NEAREST;
			case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:  return FILTER_NEAREST_MIPMAP_LINEAR;
			case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:   return FILTER_LINEAR_MIPMAP_LINEAR;
			default:                                             return FILTER_LINEAR;
		}
	}

	static inline sampler_wrap_mode_flags GltfWrapToFlags( int gltfWrap )
	{
		switch( gltfWrap )
		{
			case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:   return WRAP_CLAMP_TO_EDGE;
			case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT: return WRAP_MIRRORED_REPEAT;
			case TINYGLTF_TEXTURE_WRAP_REPEAT:          return WRAP_REPEAT;
			default:                                    return WRAP_CLAMP_TO_EDGE;
		}
	}

	static inline gltf_image_metadata GetGltfTextureMetadata( const tinygltf::Image& img )
	{
		gltf_image_metadata out{
			.width = img.width,
			.height = img.height,
			//.component = ,
			//.bits = ,
			//.pixelType =
		};

		switch( img.component )
		{
			case 1: out.component = gltf_img_components::R; break;
			case 2: out.component = gltf_img_components::RG; break;
			case 3: out.component = gltf_img_components::RGB; break;
			case 4: out.component = gltf_img_components::RGBA; break;
			default: out.component = gltf_img_components::UNKNOWN;
		}

		switch( img.bits )
		{
			case 8:  out.bits = gltf_img_bit_depth::B8; break;
			case 16: out.bits = gltf_img_bit_depth::B16; break;
			case 32: out.bits = gltf_img_bit_depth::B32; break;
			default: out.bits = gltf_img_bit_depth::UNKNOWN;
		}

		switch( img.pixel_type )
		{
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  out.pixelType = gltf_img_pixel_type::UBYTE; break;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: out.pixelType = gltf_img_pixel_type::USHORT; break;
			case TINYGLTF_COMPONENT_TYPE_FLOAT:          out.pixelType = gltf_img_pixel_type::FLOAT32; break;
			default: out.pixelType = gltf_img_pixel_type::UNKNOWN;
		}

		return out;
	}
};

struct meshlet_info
{
	vec3	aabbMin;
	vec3	aabbMax;

	vec3    coneAxis;
	vec3    coneApex;

	int8_t	coneX, coneY, coneZ, coneCutoff;

	uint    vertexOffset;
	uint    triangleOffset;
	uint8_t vertexCount;
	uint8_t triangleCount;
};

template<typename Vec>
struct aabb_t
{
	Vec min;
	Vec max;
};

aabb_t<DirectX::XMFLOAT3> ComputeMeshletAabb(
	const meshopt_Meshlet& m,
	std::span<const DirectX::XMFLOAT3> vertices,
	std::span<const u32> mletVtx
) {
	using namespace DirectX;

	constexpr u64 laneCount = sizeof( __m256 ) / sizeof( float );
	constexpr u64 batchSize = 2 * laneCount;

	XMFLOAT3 min = vertices[ 0 ];
	XMFLOAT3 max = vertices[ 0 ];

	const u64 batches = m.vertex_count / batchSize;
	u64 vi = 0;
	for( ; vi < batches; vi += batchSize )
	{
		alignas( sizeof( __m256 ) ) float xBatch[ batchSize ];
		alignas( sizeof( __m256 ) ) float yBatch[ batchSize ];
		alignas( sizeof( __m256 ) ) float zBatch[ batchSize ];
		//[[unroll]]
		for( u64 i = 0; i < batchSize; ++i )
		{
			const XMFLOAT3 pos = vertices[ mletVtx[ vi + m.vertex_offset + i ] ];
			xBatch[ i ] = pos.x;
			yBatch[ i ] = pos.y;
			zBatch[ i ] = pos.z;
		}

		__m256 laneX0_f32x8 = _mm256_load_ps( xBatch );
		__m256 laneX1_f32x8 = _mm256_load_ps( xBatch + laneCount );

		float minX = MinF32x8_SIMD( laneX0_f32x8, laneX1_f32x8 );
		float maxX = MaxF32x8_SIMD( laneX0_f32x8, laneX1_f32x8 );

		__m256 laneY0_f32x8 = _mm256_load_ps( yBatch );
		__m256 laneY1_f32x8 = _mm256_load_ps( yBatch + laneCount );

		float minY = MinF32x8_SIMD( laneY0_f32x8, laneY1_f32x8 );
		float maxY = MaxF32x8_SIMD( laneY0_f32x8, laneY1_f32x8 );

		__m256 laneZ0_f32x8 = _mm256_load_ps( zBatch );
		__m256 laneZ1_f32x8 = _mm256_load_ps( zBatch + laneCount );

		float minZ = MinF32x8_SIMD( laneZ0_f32x8, laneZ1_f32x8 );
		float maxZ = MaxF32x8_SIMD( laneZ0_f32x8, laneZ1_f32x8 );

		min.x = std::min( minX, min.x );
		max.x = std::max( maxX, max.x );

		min.y = std::min( minY, min.y );
		max.y = std::max( maxY, max.y );

		min.z = std::min( minZ, min.z );
		max.z = std::max( maxZ, max.z );
	}

	for( ; vi < m.vertex_count; ++vi )
	{
		const XMFLOAT3 pos = vertices[ mletVtx[ vi + m.vertex_offset ] ];
		XMVECTOR xmMin = XMVectorMin( XMLoadFloat3( &pos ), XMLoadFloat3( &min ) );
		XMVECTOR xmMax = XMVectorMin( XMLoadFloat3( &pos ), XMLoadFloat3( &max ) );

		XMStoreFloat3( &min, xmMin );
		XMStoreFloat3( &max, xmMax );
	}
	return { .min = min, .max = max };
}

aabb_t<DirectX::XMFLOAT2> ComputeTexBounds( std::span<const DirectX::XMFLOAT2> uvs )
{
	using namespace DirectX;

	constexpr u64 laneCount = sizeof( __m256 ) / sizeof( float );
	constexpr u64 batchSize = 2 * laneCount;

	XMFLOAT2 min = uvs[ 0 ];
	XMFLOAT2 max = uvs[ 0 ];

	const u64 batches = std::size( uvs ) / batchSize;
	u64 uvi = 0;
	for( ; uvi < batches; uvi += batchSize )
	{
		alignas( sizeof( __m256 ) ) float uBatch[ batchSize ];
		alignas( sizeof( __m256 ) ) float vBatch[ batchSize ];
		//[[unroll]]
		for( u64 i = 0; i < batchSize; ++i )
		{
			const XMFLOAT2 texCoord = uvs[ uvi + i ];
			uBatch[ i ] = texCoord.x;
			vBatch[ i ] = texCoord.y;
		}

		__m256 laneX0_f32x8 = _mm256_load_ps( uBatch );
		__m256 laneX1_f32x8 = _mm256_load_ps( uBatch + laneCount );

		float minX = MinF32x8_SIMD( laneX0_f32x8, laneX1_f32x8 );
		float maxX = MaxF32x8_SIMD( laneX0_f32x8, laneX1_f32x8 );

		__m256 laneY0_f32x8 = _mm256_load_ps( vBatch );
		__m256 laneY1_f32x8 = _mm256_load_ps( vBatch + laneCount );

		float minY = MinF32x8_SIMD( laneY0_f32x8, laneY1_f32x8 );
		float maxY = MaxF32x8_SIMD( laneY0_f32x8, laneY1_f32x8 );

		min.x = std::min( minX, min.x );
		max.x = std::max( maxX, max.x );

		min.y = std::min( minY, min.y );
		max.y = std::max( maxY, max.y );
	}

	for( ; uvi < std::size( uvs ); ++uvi )
	{
		const XMFLOAT2 texCoord = uvs[ uvi ];
		XMVECTOR xmMin = XMVectorMin( XMLoadFloat2( &texCoord ), XMLoadFloat2( &min ) );
		XMVECTOR xmMax = XMVectorMin( XMLoadFloat2( &texCoord ), XMLoadFloat2( &max ) );

		XMStoreFloat2( &min, xmMin );
		XMStoreFloat2( &max, xmMax );
	}
	return { .min = min, .max = max };
}

struct meshlets
{
	std::vector<meshlet_info> desc;
	std::vector<u32> vertices;
	std::vector<u8> triangles;

	inline u64 GetDataSizeInBytes() const
	{
		u64 szInBytes = SizeInBytes( vertices );
		szInBytes += SizeInBytes( triangles );

		return szInBytes;
	}
};

using snorm8x4 = u32;

struct optimized_mesh
{
	std::vector<DirectX::XMFLOAT3>     pos;
	std::vector<snorm8x4>              tanFrames;
	std::vector<DirectX::XMFLOAT2>     uvs;
	std::vector<u32>                   indices;
	
	vec3	                           aabbMin;
	vec3	                           aabbMax;

	u64                                materialIdx;

	inline u64 GetDataSizeInBytes() const
	{
		u64 szInBytes = SizeInBytes(pos);
		szInBytes += SizeInBytes( tanFrames );
		szInBytes += SizeInBytes( uvs );
		szInBytes += SizeInBytes( indices );

		return szInBytes;
	}
};


constexpr u64 MAX_VTX = 128;
constexpr u64 MAX_TRIS = 256;
constexpr float CONE_WEIGHT = 0.8f;
// TODO: add hierarchical lod
struct mesh_pipeline
{
	raw_mesh& rawMesh;

	mesh_pipeline( raw_mesh& rawMesh ) : rawMesh{ rawMesh } {}

	void ReindexAndOptimizeMesh()
	{
		meshopt_Stream attrStreams[] = {
			{ .data = std::data( rawMesh.pos ), .size = std::size( rawMesh.pos ), .stride = sizeof( rawMesh.pos[ 0 ] ) },
			{ .data = std::data( rawMesh.normals ), .size = std::size( rawMesh.normals ), .stride = sizeof( rawMesh.normals[ 0 ] ) },
			{ .data = std::data( rawMesh.tans ), .size = std::size( rawMesh.tans ), .stride = sizeof( rawMesh.tans[ 0 ] ) },
			{ .data = std::data( rawMesh.uvs ), .size = std::size( rawMesh.uvs ), .stride = sizeof( rawMesh.uvs[ 0 ] ) },
		};
		std::vector<u32>& indices = rawMesh.indices;

		const u64 vtxCount = std::size( rawMesh.pos );
		const u64 idxCount = std::size( indices );

		std::vector<u32> remap( vtxCount );
		u64 newVtxCount = meshopt_generateVertexRemapMulti(
			std::data( remap ), std::data( indices ), idxCount, vtxCount, attrStreams, std::size( attrStreams ) );

		HT_ASSERT( newVtxCount <= vtxCount );
		if( newVtxCount != vtxCount )
		{
			meshopt_remapIndexBuffer( std::data( indices ), std::data( indices ), idxCount, std::data( remap ) );
			meshopt_remapVertexBuffer( std::data( rawMesh.pos ), std::data( rawMesh.pos ), vtxCount, 
				sizeof( rawMesh.pos[ 0 ] ), std::data( remap ) );
			rawMesh.pos.resize( newVtxCount );
			meshopt_remapVertexBuffer( std::data( rawMesh.normals ), std::data( rawMesh.normals ), vtxCount, 
				sizeof( rawMesh.normals[ 0 ] ), std::data( remap ) );
			rawMesh.normals.resize( newVtxCount );
			meshopt_remapVertexBuffer( std::data( rawMesh.tans ), std::data( rawMesh.tans ), vtxCount, 
				sizeof( rawMesh.tans[ 0 ] ), std::data( remap ) );
			rawMesh.tans.resize( newVtxCount );
			meshopt_remapVertexBuffer( std::data( rawMesh.uvs ), std::data( rawMesh.uvs ), vtxCount, 
				sizeof( rawMesh.uvs[ 0 ] ), std::data( remap ) );
			rawMesh.uvs.resize( newVtxCount );
		}

		meshopt_optimizeVertexCache( std::data( indices ), std::data( indices ), idxCount, newVtxCount );

		meshopt_optimizeVertexFetch( std::data( rawMesh.pos ), std::data( indices ), idxCount, std::data( rawMesh.pos ), 
			newVtxCount, sizeof( rawMesh.pos[ 0 ] ) );
		meshopt_optimizeVertexFetch( std::data( rawMesh.normals ), std::data( indices ), idxCount, std::data( rawMesh.normals ), 
			newVtxCount, sizeof( rawMesh.normals[ 0 ] ) );
		meshopt_optimizeVertexFetch( std::data( rawMesh.tans ), std::data( indices ), idxCount, std::data( rawMesh.tans ), 
			newVtxCount, sizeof( rawMesh.tans[ 0 ] ) );
		meshopt_optimizeVertexFetch( std::data( rawMesh.uvs ), std::data( indices ), idxCount, std::data( rawMesh.uvs ), 
			newVtxCount, sizeof( rawMesh.uvs[ 0 ] ) );
	}

	meshlets MakeMeshlets( u64 vertexCount, u64 triangleCount, float coneWeight )
	{
		using namespace DirectX;

		std::vector<meshopt_Meshlet> meshlets;
		std::vector<u32> mletVtx;
		std::vector<u8> mletTris;

		const std::span<u32> indices = rawMesh.indices;
		const std::span<DirectX::XMFLOAT3> pos = rawMesh.pos;

		const u64 maxMeshletCount = meshopt_buildMeshletsBound( std::size( indices ), vertexCount, triangleCount );
		meshlets.resize( maxMeshletCount );
		mletVtx.resize( maxMeshletCount * vertexCount );
		mletTris.resize( maxMeshletCount * triangleCount * 3 );

		const u64 meshletCount = meshopt_buildMeshlets( std::data( meshlets ), std::data( mletVtx ), std::data( mletTris ),
				std::data( indices ), std::size( indices ), &pos[ 0 ].x, std::size( pos ), sizeof( pos[ 0 ] ),
				vertexCount, triangleCount, coneWeight );

		const meshopt_Meshlet& last = meshlets[ meshletCount - 1 ];

		meshlets.resize( meshletCount );
		mletVtx.resize( ( u64 ) last.vertex_offset + last.vertex_count );
		mletTris.resize( ( u64 ) last.triangle_offset + ( ( ( u64 ) last.triangle_count * 3 + 3 ) & ~3 ) );

		std::vector<meshlet_info> mletsDesc;
		mletsDesc.reserve( meshletCount );
		for( const meshopt_Meshlet& m : meshlets )
		{
			meshopt_optimizeMeshlet( 
				&mletVtx[ m.vertex_offset ], &mletTris[ m.triangle_offset ], m.triangle_count, m.vertex_count );

			const meshopt_Bounds bounds = meshopt_computeMeshletBounds( std::data( mletVtx ), std::data( mletTris ), 
				m.triangle_count, &pos[ 0 ].x, std::size( pos ), sizeof( pos[ 0 ] ) );

			const aabb_t<DirectX::XMFLOAT3> aabb = ComputeMeshletAabb( m, pos, mletVtx );
			
			const meshlet_info mlet = {
				.aabbMin = aabb.min,
				.aabbMax = aabb.max,
				.coneAxis = { bounds.cone_axis[ 0 ], bounds.cone_axis[ 1 ], bounds.cone_axis[ 2 ] },
				.coneApex = { bounds.cone_apex[ 0 ], bounds.cone_apex[ 1 ], bounds.cone_apex[ 2 ] },
				.coneX = bounds.cone_axis_s8[ 0 ],
				.coneY = bounds.cone_axis_s8[ 1 ],
				.coneZ = bounds.cone_axis_s8[ 2 ],
				.coneCutoff = bounds.cone_cutoff_s8,
				.vertexOffset = m.vertex_offset,
				.triangleOffset = m.triangle_offset,
				.vertexCount = ( u8 ) m.vertex_count,
				.triangleCount = ( u8 ) m.triangle_count
			};
			
			mletsDesc.emplace_back( mlet );
		}

		return { std::move( mletsDesc ), std::move( mletVtx ), std::move( mletTris ) };
	}

	optimized_mesh AssembleMesh( const meshlets& mlets )
	{
		using namespace DirectX;

		XMFLOAT3 meshAabbMin = mlets.desc[ 0 ].aabbMin;
		XMFLOAT3 meshAabbMax = mlets.desc[ 0 ].aabbMax;

		for( const meshlet_info& m : mlets.desc )
		{
			XMVECTOR xmMin = XMVectorMin( XMLoadFloat3( &m.aabbMin ), XMLoadFloat3( &meshAabbMin ) );
			XMVECTOR xmMax = XMVectorMin( XMLoadFloat3( &m.aabbMax ), XMLoadFloat3( &meshAabbMax ) );

			XMStoreFloat3( &meshAabbMin, xmMin );
			XMStoreFloat3( &meshAabbMax, xmMax );
		}

		const aabb_t<DirectX::XMFLOAT2> texRect = ComputeTexBounds( rawMesh.uvs );

		const u64 streamSize = std::size( rawMesh.pos );

		std::vector<snorm8x4> tanFrames;
		tanFrames.reserve( streamSize );

		for( u64 i = 0; i < streamSize; ++i )
		{
			XMFLOAT3 n = rawMesh.normals[ i ];
			n.x = -n.x;
			XMFLOAT3 t = rawMesh.tans[ i ];

			tanFrames.push_back( EncodeTanFrame( n, t ) );
		}

		optimized_mesh mesh = {
			.pos = std::move( rawMesh.pos ),
			.tanFrames = std::move( tanFrames),
			.uvs = std::move(rawMesh.uvs),
			.indices = std::move(rawMesh.indices),
			
			.aabbMin = meshAabbMin,
			.aabbMax = meshAabbMax,
			
			.materialIdx = rawMesh.materialIdx
		};

		return mesh;
	}

	inline snorm8x4 EncodeTanFrame( DirectX::XMFLOAT3 n, DirectX::XMFLOAT3 t )
	{
		DirectX::XMFLOAT2 octaNormal = OctaNormalEncode( n );
		float tanAngle = EncodeTanToAngle( n, t );

		i8 snormNx = meshopt_quantizeSnorm( octaNormal.x, 8 );
		i8 snormNy = meshopt_quantizeSnorm( octaNormal.y, 8 );
		i8 snormTanAngle = meshopt_quantizeSnorm( tanAngle, 8 );

		u32 bitsSnormNx = *( u8* ) &snormNx;
		u32 bitsSnormNy = *( u8* ) &snormNy;
		u32 bitsSnormTanAngle = *( u8* ) &snormTanAngle;

		snorm8x4 packedTanFrame = bitsSnormNx | ( bitsSnormNy << 8 ) | ( bitsSnormTanAngle << 16 );
		return packedTanFrame;
	}
};

#include <vfspp/VFS.h>

using virtual_fs = vfspp::MultiThreadedVirtualFileSystem;

inline std::shared_ptr<virtual_fs> MountZipFS( std::string_view parentDir, std::string_view archiveName )
{
	HT_ASSERT( stdfs::is_directory( parentDir ) );
	std::shared_ptr<virtual_fs> vfs = std::make_shared<virtual_fs>();
	HT_ASSERT( vfs->CreateFileSystem<vfspp::ZipFileSystem>( std::data( parentDir ), std::data( archiveName ) ) );
	
	return vfs;
}

inline std::array<compression_batch, ( u64 ) material_map_type::COUNT>
AssembleTextureJobBatches( const std::vector<material_info>& materials, const std::vector<gltf_image>& imgs )
{
	std::array<compression_batch, ( u64 ) material_map_type::COUNT> batches = {};
	for( u64 i = 0; i < ( u64 ) material_map_type::COUNT; ++i )
	{
		batches[ i ] = compression_batch{ material_map_type( i ) };
	}

	ankerl::unordered_dense::set<u32> textureSet;
	textureSet.reserve( std::size( imgs ) );
	for( const material_info& m : materials )
	{
		if( u32 idx = m.baseColorIdx; textureSet.find( idx ) == std::cend( textureSet ) )
		{
			textureSet.insert( idx );
			batches[ ( u64 ) material_map_type::BASE_COLOR ].Append( { imgs[ idx ].data } );
		}
		if( u32 idx = m.metallicRoughnessIdx; textureSet.find( idx ) == std::cend( textureSet ) )
		{
			textureSet.insert( idx );
			batches[ ( u64 ) material_map_type::METALLIC_ROUGHNESS ].Append( { imgs[ idx ].data } );
		}
		if( u32 idx = m.normalIdx; textureSet.find( idx ) == std::cend( textureSet ) )
		{
			textureSet.insert( idx );
			batches[ ( u64 ) material_map_type::NORMALS ].Append( { imgs[ idx ].data } );
		}
		if( u32 idx = m.occlusionIdx; textureSet.find( idx ) == std::cend( textureSet ) )
		{
			textureSet.insert( idx );
			batches[ ( u64 ) material_map_type::OCCLUSION ].Append( { imgs[ idx ].data } );
		}
		if( u32 idx = m.emissiveIdx; textureSet.find( idx ) == std::cend( textureSet ) )
		{
			textureSet.insert( idx );
			batches[ ( u64 ) material_map_type::EMISSIVE ].Append( { imgs[ idx ].data } );
		}
	}

	return batches;
}

struct meshbin_desc
{
	range posByteRange;
	range tanFrameByteRange;
	range uvByteRange;
	range idxByteRange;
	range mletsVtxRange;
	range mletsTrisRange;
};

struct mesh_desc2
{
	virtual_path    meshbinPath;
	meshbin_desc    meshbinDesc;
	range			mletDescRange;
	vec3			aabbMin;
	vec3			aabbMax;
	u64				materialIdx;
};

//struct texture_entry
//{
//	virtual_path path;
//	texture_metadata metadata;
//};
struct material_desc
{
	virtual_path baseColorPath;
	virtual_path mroPath; // NOTE: Metallic Roughness Occlusion
	virtual_path normalPath;
	virtual_path emissivePath;

	vec3 baseColFactor;
	//float pad0;
	float metallicFactor;
	float roughnessFactor;
	float alphaCutoff;
	//float pad1;
	vec3 emissiveFactor;
	//float pad2;
	u32 samplerIdx;

	alpha_mode alphaMode;
};

struct mesh_entry
{
	std::span<meshlet_info> mlets;

	std::span<u8>			pos;
	std::span<u8>			tanFrames;
	std::span<u8>			uvs;
	std::span<u8>			idx;
	std::span<u8>			mletsVtx;
	std::span<u8>			mletsTris;

	vec3	                aabbMin;
	vec3	                aabbMax;

	u64                     materialIdx;
};

struct mesh_entry_internal
{
	std::vector<meshlet_info> mlets;

	virtual_path    meshbinPath = {};
	meshbin_desc    meshbinDesc;

	vec3	                           aabbMin;
	vec3	                           aabbMax;

	u64                                materialIdx;
}; 

struct mesh_serialzier
{
	optimized_mesh& meshIn;
	meshlets& mletsIn;

	u64 dstOffset = 0;

	std::vector<u8> binOut;

	mesh_serialzier( optimized_mesh& mesh, meshlets& mlets ) : meshIn{ mesh }, mletsIn{ mlets }
	{
		const u64 szInBytes = meshIn.GetDataSizeInBytes() + mletsIn.GetDataSizeInBytes();
		binOut.reserve( szInBytes );
	}

	template<typename T>
	inline range CopyRangeWithOffset( const std::vector<T>& inData )
	{
		const u64 copySizeInBytes = std::size( inData );
		std::memcpy( std::data(binOut) + dstOffset, std::data(inData), copySizeInBytes);
		const range r = { .offset = dstOffset, .size = copySizeInBytes };
		dstOffset += copySizeInBytes;

		return r;
	}

	inline meshbin_desc Serialize()
	{
		meshbin_desc mei;

		mei.posByteRange = CopyRangeWithOffset( meshIn.pos );
		mei.tanFrameByteRange = CopyRangeWithOffset( meshIn.tanFrames );
		mei.uvByteRange = CopyRangeWithOffset( meshIn.uvs );
		mei.mletsVtxRange = CopyRangeWithOffset( mletsIn.vertices );
		mei.mletsTrisRange = CopyRangeWithOffset( mletsIn.triangles );

		return mei;
	}
};

struct drk_file_footer
{
	std::vector<material_desc> materials;
	std::vector<meshbin_desc> meshes;
	std::vector<meshlet_info> meshlets;
	std::vector<sampler_config> samplers;
};

// TODO: for now we always map the whole file, we might want to map a sub-regions for very large files in the future
// NOTE: bc we do this we will be tracking "absolute" offsets
struct drk_file_writer
{
	std::string parentDir;
	std::shared_ptr<virtual_fs> zipVfs;
	drk_file_footer footer;

	drk_file_writer( std::string_view parentDirSV, std::string_view archiveName ) :
		parentDir{ parentDirSV }, zipVfs{ MountZipFS( parentDirSV, archiveName ) } {}

	std::vector<texture_entry> 
	CompressAndSaveTexutres( const std::array<compression_batch, ( u64 ) material_map_type::COUNT>& batches )
	{
		std::vector<texture_entry> textures;

		nvtt_compressor nvttCompressor;
		//for( const compression_batch& b : batches )
		//{
		//	if( std::size( b.texturesBin ) == 0 ) continue;
		//
		//	nvtt_batch nvttBatch = { b };
		//	const u64 batchSizeInBytes = nvttCompressor.GetEstimatedBatchSize( nvttBatch );
		//	
		//	nvtt_batch_handler batchOutHandler = { fileView.pView + absOffset, fileView.size };
		//	nvttCompressor.ProcessBatch( nvttBatch, batchOutHandler );
		//
		//	HT_ASSERT( std::size( nvttBatch.surfaces ) == std::size( batchOutHandler.outputRanges ) );
		//	textures.reserve( std::size( textures ) + std::size( nvttBatch.surfaces ) );
		//	for( u64 texIdx = 0; texIdx < std::size( nvttBatch.surfaces ); ++texIdx )
		//	{
		//		range r = batchOutHandler.outputRanges[ texIdx ];
		//		r.offset += absOffset;
		//		const nvtt::Surface& currentSurf = nvttBatch.surfaces[ texIdx ];
		//		const texture_rect rect = NvttGetSurafceRect( currentSurf );
		//		const texture_metadata meta = {
		//			.width = rect.width,
		//			.height = rect.height,
		//			.format = b.format,
		//			.type = MapNvttTextureType( currentSurf.type() ),
		//			.mipCount = 1,
		//			.layerCount = 1
		//		};
		//
		//		textures.push_back( { r, meta } );
		//	}
		//
		//	absOffset += batchOutHandler.offsetInBytes;  
		//}

		return textures;
	}

	std::vector<mesh_entry_internal> OptimizeAndSaveGeometry( std::vector<raw_mesh>& rawMeshes )
	{
		std::vector<mesh_entry_internal> meshesOut;
		meshesOut.reserve( std::size( rawMeshes ) );

		// TODO: multithread
		for( raw_mesh& rawMesh : rawMeshes )
		{
			const std::string filePath = std::format( "{}/mesh_{}.bin", parentDir, rawMesh.name );
			auto meshFile = zipVfs->OpenFile( filePath, vfspp::IFile::FileMode::Write );
			HT_ASSERT( !meshFile );

			mesh_pipeline pipeline = { rawMesh };
			pipeline.ReindexAndOptimizeMesh();
			meshlets mlets = pipeline.MakeMeshlets( MAX_VTX, MAX_TRIS, CONE_WEIGHT );
			optimized_mesh mesh = pipeline.AssembleMesh( mlets );

			mesh_serialzier s{ mesh, mlets };

			meshbin_desc binDesc = s.Serialize();

			meshFile->Write( s.binOut );

			mesh_entry_internal mei = {
				.mlets = std::move( mlets.desc ),
				.meshbinPath = MakeVirtPath( filePath ),
				.meshbinDesc = binDesc,
				.aabbMin = mesh.aabbMin,
				.aabbMax = mesh.aabbMax,
				.materialIdx = mesh.materialIdx
			};
			meshesOut.push_back( std::move( mei ) );
		}

		return meshesOut;
	}
};


// NOTE: assume we have a single scene
// NOTE: cameras and animations are ignored rn
void GltfConditionAssetFile( const path& filePath )
{
	gltf_processor gltf{ filePath.string() };

	std::vector<gltf_node> nodes = gltf.ProcessNodes();
	std::vector<raw_mesh> meshes = gltf.ProcessMeshes();
	std::vector<gltf_image> images = gltf.ProcessImages();
	std::vector<sampler_config> samplers = gltf.ProcessSamplers();
	std::vector<material_info> materials = gltf.ProcessMaterials();

	std::array<compression_batch, ( u64 ) material_map_type::COUNT> batches =
		AssembleTextureJobBatches( materials, images );

	drk_file_writer fileWriter{ "Assets", filePath.stem().string() + ".zip" };
	//fileWriter.OptimizeAndSaveGeometry( rawMeshes );
	//fileWriter.CompressAndSaveTexutres( batches );
	
	
}

inline gltf_sampler_filter GetSamplerFilter( i32 code )
{
	switch( code )
	{
	case 9728: return GLTF_SAMPLER_FILTER_NEAREST;
	case 9729: return GLTF_SAMPLER_FILTER_LINEAR;
	case 9984: return GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_NEAREST;
	case 9985: return GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_NEAREST;
	case 9986: return GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_LINEAR;
	case 9987: return GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_LINEAR;
	}
}
inline gltf_sampler_address_mode GetSamplerAddrMode( i32 code )
{
	switch( code )
	{
	case 10497: return GLTF_SAMPLER_ADDRESS_MODE_REPEAT;
	case 33071: return GLTF_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	case 33648: return GLTF_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	}
}

// NOTE: will assume: pos,normal,tan are vec3 and uv is vec2
struct imported_mesh
{
	range	posStreamRange;
	range	normalStreamRange;
	range	tanStreamRange;
	range	uvsStreamRange;
	range	idxRange;
	float	aabbMin[ 3 ];
	float	aabbMax[ 3 ];
	u16		mtlIdx;
};

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

// TODO: context per gltf ?
struct png_decoder
{
	spng_ctx* ctx;

	png_decoder( const u8* pngData, u64 pngSize ) : ctx{ spng_ctx_new( 0 ) }
	{
		// NOTE: ignore chunk CRC's 
		spng_set_crc_action( ctx, SPNG_CRC_USE, SPNG_CRC_USE );
		spng_set_png_buffer( ctx, pngData, pngSize );
	}
	~png_decoder() { spng_ctx_free( ctx ); }

};
inline u64 PngGetDecodedImageByteCount( const png_decoder& dcd )
{
	u64 outSize = 0;
	spng_decoded_image_size( dcd.ctx, SPNG_FMT_RGBA8, &outSize );

	return outSize;
}
inline u64 PngGetDecodedImageSize( const png_decoder& dcd )
{
	spng_ihdr ihdr = {};
	spng_get_ihdr( dcd.ctx, &ihdr );

	return u64( ihdr.width ) | ( u64( ihdr.height ) << 32 );
}
inline void PngDecodeImageFromMem( const png_decoder& dcd, u8* txBinOut, u64 txSize )
{
	spng_decode_image( dcd.ctx, txBinOut, txSize, SPNG_FMT_RGBA8, 0 );
}


inline DirectX::XMMATRIX CgltfNodeGetTransf( const cgltf_node* node )
{
	using namespace DirectX;

	XMMATRIX t = {};
	if( node->has_rotation || node->has_translation || node->has_scale )
	{
		XMVECTOR move = XMLoadFloat3( (const XMFLOAT3*) node->translation );
		XMVECTOR rot = XMLoadFloat4( (const XMFLOAT4*) node->rotation );
		XMVECTOR scale = XMLoadFloat3( (const XMFLOAT3*) node->scale );
		t = XMMatrixAffineTransformation( scale, XMVectorSet( 0, 0, 0, 1 ), rot, move );
	}
	else if( node->has_matrix )
	{
		// NOTE: gltf matrices are stored in col maj
		t = XMMatrixTranspose( XMLoadFloat4x4( (const XMFLOAT4X4*) node->matrix ) );
	}

	return t;
}
inline u64 CgltfCompTypeByteCount( cgltf_component_type compType )
{
	switch( compType )
	{
	case cgltf_component_type_r_8: case cgltf_component_type_r_8u: return 1;
	case cgltf_component_type_r_16: case cgltf_component_type_r_16u: return 2;
	case cgltf_component_type_r_32u: case cgltf_component_type_r_32f: return 4;
	case cgltf_component_type_invalid: return -1;
	}
}
inline float CgltfReadFloat( const u8* data, cgltf_component_type compType )
{
	switch( compType )
	{
	case cgltf_component_type_invalid: default: return NAN;
	case cgltf_component_type_r_8: return float( *(const i8*) data );
	case cgltf_component_type_r_8u: return float( *(const u8*) data );
	case cgltf_component_type_r_16: return float( *(const i16*) data );
	case cgltf_component_type_r_16u: return float( *(const u16*) data );
	case cgltf_component_type_r_32u: return float( *(const u32*) data );
	case cgltf_component_type_r_32f: return *(const float*) data;
	}
}

struct raw_texture_info
{
	u32				offset;
	u16				width;
	u16				height;
	texture_type	type;
};

inline raw_texture_info CgltfDecodeTexture( const cgltf_texture& t, const u8* pBin, std::vector<u8>& texBin )
{
	using namespace std;
	u64 imgOffset = t.image->buffer_view->offset;
	u64 imgSize = t.image->buffer_view->size;

	std::string_view mimeType = { t.image->mime_type };
	if( mimeType == "image/png"sv )
	{
		png_decoder dcd( pBin + imgOffset, imgSize );
		u64 imageByteCount = PngGetDecodedImageByteCount( dcd );
		u64 widthHeight = PngGetDecodedImageSize( dcd );
		u64 texBinDataOffset = std::size( texBin );
		texBin.resize( std::size( texBin ) + imageByteCount );
		PngDecodeImageFromMem( dcd, std::data( texBin ) + texBinDataOffset, imageByteCount );

		return { (u32)texBinDataOffset,u16( widthHeight & u32( -1 ) ), u16( widthHeight >> 32 ) };
	}
	assert( 0 );
}


// TODO: rethink samplers
static void
LoadGlbFile(
	const std::vector<u8>&			glbData,
	std::vector<float>&				meshAttrs,
	std::vector<u32>&				indices,
	std::vector<u8>&				texBin,
	std::vector<image_metadata>&	imgDescs, 
	std::vector<material_data>&		mtrlDescs,
	std::vector<imported_mesh>&		meshDescs
){
	using namespace DirectX;
	using namespace std;

	cgltf_options options = { .type = cgltf_file_type_glb };
	cgltf_data* data = 0;
	cgltf_parse( &options, std::data( glbData ), std::size( glbData ), &data );
	cgltf_validate( data );

	const u8* pBin = (const u8*) data->bin;

	std::vector<DirectX::XMFLOAT4X4> nodeTransf( data->nodes_count );
	for( u64 n = 0; n < data->nodes_count; ++n )
	{
		const cgltf_node* node = data->nodes + n;
	
		XMMATRIX t = CgltfNodeGetTransf( node );
	
		for( const cgltf_node* parent = node->parent; 
			 parent; 
			 parent = parent->parent )
		{
			t = XMMatrixMultiply( t, CgltfNodeGetTransf( parent ) );
		}
		XMStoreFloat4x4( &nodeTransf[ n ], t );
	}

	std::vector<float> attrStreams;
	std::vector<imported_mesh> rawMeshes;
	std::vector<image_metadata> compressedImgs;
	std::vector<material_data> materials( data->materials_count );

	std::vector<i32> texProcessingCache( data->textures_count, -1 );

	for( u64 mi = 0; mi < data->materials_count; ++mi )
	{
		const cgltf_material& mtrl = data->materials[ mi ];
		assert( mtrl.has_pbr_metallic_roughness );

		const cgltf_pbr_metallic_roughness& pbrMetallicRoughness = mtrl.pbr_metallic_roughness;
		materials[ mi ].baseColFactor.x = pbrMetallicRoughness.base_color_factor[ 0 ];
		materials[ mi ].baseColFactor.y = pbrMetallicRoughness.base_color_factor[ 1 ];
		materials[ mi ].baseColFactor.z = pbrMetallicRoughness.base_color_factor[ 2 ];
		materials[ mi ].metallicFactor = pbrMetallicRoughness.metallic_factor;
		materials[ mi ].roughnessFactor = pbrMetallicRoughness.roughness_factor;

		if( const cgltf_texture* pbrBaseCol = pbrMetallicRoughness.base_color_texture.texture )
		{
			u64 texDscIdx = u64( pbrBaseCol - data->textures );
			if( texProcessingCache[ texDscIdx ] == -1 )
			{
				texProcessingCache[ texDscIdx ] = ( i32 ) std::size( compressedImgs );
				raw_texture_info raw = CgltfDecodeTexture( *pbrBaseCol, pBin, texBin );

				const u8* imgSrc = std::data( texBin ) + raw.offset;
				u8* texBinOut = std::data( texBin ) + raw.offset;
				// NOTE: compress in-place
				CompressToBc1_SIMD( imgSrc, raw.width, raw.height, texBinOut );

				u64 bcByteCount = GetBCTexByteCount( raw.width, raw.height, bc1BytesPerBlock );
				texBin.resize( raw.offset + bcByteCount );

				//compressedImgs.push_back(
				//	{ 0,{ raw.offset,bcByteCount }, raw.width, raw.height, TEXTURE_FORMAT_BC1_RGB_SRGB, TEXTURE_TYPE_2D } );
			}
			materials[ mi ].baseColIdx = texProcessingCache[ texDscIdx ];
		}
		
		if( const cgltf_texture* metalRoughMap = pbrMetallicRoughness.metallic_roughness_texture.texture )
		{
			u64 texDscIdx = u64( metalRoughMap - data->textures );
			if( texProcessingCache[ texDscIdx ] == -1 )
			{
				texProcessingCache[ texDscIdx ] = ( i32 ) std::size( compressedImgs );
				raw_texture_info raw = CgltfDecodeTexture( *metalRoughMap, pBin, texBin );

				const u8* imgSrc = std::data( texBin ) + raw.offset;
				u8* texBinOut = std::data( texBin ) + raw.offset;
				// NOTE: compress in-place
				CompressMetalRoughMapToBc5_SIMD( imgSrc, raw.width, raw.height, texBinOut );

				u64 bcByteCount = GetBCTexByteCount( raw.width, raw.height, bc5BytesPerBlock );
				texBin.resize( raw.offset + bcByteCount );

				//compressedImgs.push_back(
				//	{ 0,{ raw.offset,bcByteCount }, raw.width, raw.height, TEXTURE_FORMAT_BC5_UNORM, TEXTURE_TYPE_2D } );
			}
			materials[ mi ].occRoughMetalIdx = texProcessingCache[ texDscIdx ];
		}
		if( const cgltf_texture* normalMap = mtrl.normal_texture.texture )
		{
			u64 texDscIdx = u64( normalMap - data->textures );
			if( texProcessingCache[ texDscIdx ] == -1 )
			{
				texProcessingCache[ texDscIdx ] = ( i32 ) std::size( compressedImgs );
				raw_texture_info raw = CgltfDecodeTexture( *normalMap, pBin, texBin );

				const u8* imgSrc = std::data( texBin ) + raw.offset;
				u8* texBinOut = std::data( texBin ) + raw.offset;
				// NOTE: compress in-place
				CompressNormalMapToBc5_SIMD( imgSrc, raw.width, raw.height, texBinOut );

				u64 bcByteCount = GetBCTexByteCount( raw.width, raw.height, bc5BytesPerBlock );
				texBin.resize( raw.offset + bcByteCount );

				//compressedImgs.push_back(
				//	{ 0, { raw.offset,bcByteCount }, raw.width, raw.height, TEXTURE_FORMAT_BC5_UNORM, TEXTURE_TYPE_2D } );
			}
			materials[ mi ].normalMapIdx = texProcessingCache[ texDscIdx ];
		}
	}

	constexpr u64 NumOfFloatPerAttr = 11;
	constexpr u64 NumOfOnlyCareAttrs = 4;

	for( u64 m = 0; m < data->meshes_count; ++m )
	{
		const cgltf_mesh& mesh = data->meshes[ m ];
		const cgltf_primitive& prim = mesh.primitives[ 0 ];

		mesh.primitives_count - 1;

		rawMeshes.push_back( {} );
		imported_mesh& rawMesh = rawMeshes[ std::size( rawMeshes ) - 1 ];

		u16 mtlIdx = u16( prim.material - data->materials );
		rawMesh.mtlIdx = mtlIdx;

		// NOTE: attrs must have the same count 
		u64 primVtxCount = prim.attributes[ 0 ].data->count;
		for( u64 a = 0; a < prim.attributes_count; ++a ) assert( primVtxCount == prim.attributes[ a ].data->count );

		attrStreams.reserve( std::size( attrStreams ) + NumOfFloatPerAttr * NumOfOnlyCareAttrs * primVtxCount );

		for( u64 a = 0; a < prim.attributes_count; ++a )
		{
			const cgltf_attribute& vtxAttr = prim.attributes[ a ];
			if( vtxAttr.type == cgltf_attribute_type_invalid ) continue;


			if( vtxAttr.type == cgltf_attribute_type_position )
			{
				assert( vtxAttr.data->has_min && vtxAttr.data->has_min );

				rawMesh.aabbMin[ 0 ] = vtxAttr.data->min[ 0 ];
				rawMesh.aabbMin[ 1 ] = vtxAttr.data->min[ 1 ];
				rawMesh.aabbMin[ 2 ] = vtxAttr.data->min[ 2 ];

				rawMesh.aabbMax[ 0 ] = vtxAttr.data->max[ 0 ];
				rawMesh.aabbMax[ 1 ] = vtxAttr.data->max[ 1 ];
				rawMesh.aabbMax[ 2 ] = vtxAttr.data->max[ 2 ];
			}


			u64 attrNumComp = cgltf_num_components( vtxAttr.data->type );
			switch( vtxAttr.type )
			{
			case cgltf_attribute_type_position:
			assert( attrNumComp == 3 );
			rawMesh.posStreamRange = { std::size( attrStreams ), 3 * vtxAttr.data->count }; break;
			case cgltf_attribute_type_normal:
			assert( attrNumComp == 3 );
			rawMesh.normalStreamRange = { std::size( attrStreams ), 3 * vtxAttr.data->count }; break;
			case cgltf_attribute_type_tangent:
			assert( attrNumComp == 4 );
			rawMesh.tanStreamRange = { std::size( attrStreams ), 3 * vtxAttr.data->count }; break;
			case cgltf_attribute_type_texcoord:
			assert( attrNumComp == 2 );
			rawMesh.uvsStreamRange = { std::size( attrStreams ), 2 * vtxAttr.data->count }; break;
			case cgltf_attribute_type_color: case cgltf_attribute_type_joints: case cgltf_attribute_type_weights: break;
			}


			cgltf_component_type compType = vtxAttr.data->component_type;
			u64 compByteCount = CgltfCompTypeByteCount( compType );
			u64 attrStride = vtxAttr.data->stride;
			u64 attrOffset = vtxAttr.data->offset;
			u64 attrSrcOffset = vtxAttr.data->buffer_view->offset;
			switch( vtxAttr.type )
			{
			case cgltf_attribute_type_position: case cgltf_attribute_type_normal: case cgltf_attribute_type_texcoord:
			{
				for( u64 v = 0; v < primVtxCount; ++v )
				{
					const u8* attrData = pBin + attrSrcOffset + attrOffset + attrStride * v;

					for( u64 i = 0; i < attrNumComp; ++i )
						attrStreams.push_back( CgltfReadFloat( attrData + i * compByteCount, compType ) );
				}
			}break;
			case cgltf_attribute_type_tangent:
			{
				for( u64 v = 0; v < primVtxCount; ++v )
				{
					const u8* attrData = pBin + attrSrcOffset + attrOffset + attrStride * v;

					float comps[ 4 ] = {};
					for( u64 i = 0; i < attrNumComp; ++i ) comps[ i ] = CgltfReadFloat( attrData + i * compByteCount, compType );

					attrStreams.push_back( comps[ 0 ] * comps[ 3 ] );
					attrStreams.push_back( comps[ 1 ] * comps[ 3 ] );
					attrStreams.push_back( comps[ 2 ] * comps[ 3 ] );
				}
			}break;
			case cgltf_attribute_type_color: case cgltf_attribute_type_joints: case cgltf_attribute_type_weights: break;
			}

		}

		u64 idxDstOffset = std::size( indices );
		indices.resize( idxDstOffset + prim.indices->count );
		const u8* idxSrc = pBin + prim.indices->buffer_view->offset;
		u64 idxStride = prim.indices->stride;
		rawMesh.idxRange = { idxDstOffset,prim.indices->count };
		for( u64 i = 0; i < prim.indices->count; ++i )
		{
			u64 idx = cgltf_component_read_index( idxSrc + idxStride * i, prim.indices->component_type );
			indices[ i + idxDstOffset ] = u32( idx );
		}
	}

	cgltf_free( data );

	meshAttrs = std::move( attrStreams );
	meshDescs = std::move( rawMeshes );
	imgDescs = std::move( compressedImgs );
	mtrlDescs = std::move( materials );
}

// TODO: avoid template madness ?
// TODO: mesh triangulate ?
template<typename T>
u64 MeshoptReindexMesh( std::span<T> vtxSpan, std::span<u32> idxSpan )
{
	T*      vertices = std::data( vtxSpan );
	u32*	indices = std::data( idxSpan );
	u64		vtxCount = std::size( vtxSpan );
	u64		idxCount = std::size( idxSpan );

	std::vector<u32> remap( vtxCount );
	u64 newVtxCount = meshopt_generateVertexRemap( 
		std::data( remap ), indices, idxCount, vertices, vtxCount, sizeof( vertices[ 0 ] ) );

	assert( newVtxCount <= vtxCount );
	if( newVtxCount == vtxCount ) return newVtxCount;

	meshopt_remapIndexBuffer( indices, indices, idxCount, std::data( remap ) );
	meshopt_remapVertexBuffer( vertices, vertices, vtxCount, sizeof( vertices[ 0 ] ), std::data( remap ) );
	return newVtxCount;
}

template<typename T> float* GetCompX( T* );
template<> inline float* GetCompX( vertex* v ){ return &( v->px ); }
template<> inline float* GetCompX( DirectX::XMFLOAT3* v ){ return &( v->x ); }

template<typename T>
void MeshoptOptimizeMesh( std::span<T> vtxSpan, std::span<u32> idxSpan )
{
	T*      vertices = std::data( vtxSpan );
	u32*    indices = std::data( idxSpan );
	u64		vtxCount = std::size( vtxSpan );
	u64		idxCount = std::size( idxSpan );

	meshopt_optimizeVertexCache( indices, indices, idxCount, vtxCount );
	//meshopt_optimizeOverdraw( indices, indices, idxCount, &vertices[ 0 ].px, vtxCount, sizeof( vertices[ 0 ] ), 1.05f );
	meshopt_optimizeOverdraw( indices, indices, idxCount, GetCompX( &vertices[ 0 ] ), vtxCount, sizeof( vertices[ 0 ] ), 1.05f );
	meshopt_optimizeVertexFetch( vertices, indices, idxCount, vertices, vtxCount, sizeof( vertices[ 0 ] ) );
}

template u64 MeshoptReindexMesh( std::span<DirectX::XMFLOAT3> vtxSpan, std::span<u32> idxSpan );
template void MeshoptOptimizeMesh( std::span<DirectX::XMFLOAT3> vtxSpan, std::span<u32> idxSpan );


// TODO: fix C++ constness thing
// TODO: data offseting for more meshes
static void MeshoptMakeMeshlets(
	const std::span<vertex>	    vtxSpan,
	const std::vector<u32>& lodIndices,
	const std::vector<range>& lods,
	std::vector<meshlet>& mletsDesc,
	std::vector<u32>& mletsData,
	std::vector<range>& outMeshletLods
){
	using namespace DirectX;

	std::vector<u32> meshletData;

	std::vector<meshopt_Meshlet> meshlets;
	std::vector<u32> mletVtx;
	std::vector<u8> mletIdx;

	for( range lodRange : lods )
	{
		const std::span<u32> lodIdx = { const_cast< u32* >( std::data( lodIndices ) ) + lodRange.offset,lodRange.size };

		u64 maxMeshletCount = meshopt_buildMeshletsBound( std::size( lodIdx ), MAX_VTX, MAX_TRIS );
		meshlets.resize( maxMeshletCount );
		mletVtx.resize( maxMeshletCount * MAX_VTX );
		mletIdx.resize( maxMeshletCount * MAX_TRIS * 3 );

		u64 meshletCount = meshopt_buildMeshlets( std::data( meshlets ), std::data( mletVtx ), std::data( mletIdx ),
												  std::data( lodIdx ), std::size( lodIdx ),
												  &vtxSpan[ 0 ].px, std::size( vtxSpan ), sizeof( vtxSpan[ 0 ] ),
												  MAX_VTX, MAX_TRIS, CONE_WEIGHT );

		const meshopt_Meshlet& last = meshlets[ meshletCount - 1 ];

		meshlets.resize( meshletCount );
		mletVtx.resize( last.vertex_offset + last.vertex_count );
		mletIdx.resize( last.triangle_offset + ( ( last.triangle_count * 3 + 3 ) & ~3 ) );
		meshletData.reserve( std::size( meshletData ) + std::size( mletVtx ) + std::size( mletVtx ) );

		u64 mletOffset = std::size( mletsDesc );
		outMeshletLods.push_back( { mletOffset,meshletCount } );

		for( const meshopt_Meshlet& m : meshlets )
		{
			u64 dataOffset = std::size( meshletData );

			for( u64 i = 0; i < m.vertex_count; ++i )
			{
				meshletData.push_back( mletVtx[ i + m.vertex_offset ] );
			}

			const u32* indexGroups = ( const u32* ) ( std::data( mletIdx ) + m.triangle_offset );
			u64 indexGroupCount = ( m.triangle_count * 3 + 3 ) / 4;
			for( u64 i = 0; i < indexGroupCount; ++i )
			{
				meshletData.push_back( indexGroups[ i ] );
			}

			meshopt_Bounds bounds = meshopt_computeMeshletBounds(
				std::data( mletVtx ), std::data( mletIdx ), m.triangle_count,
				&vtxSpan[ 0 ].px, std::size( vtxSpan ), sizeof( vtxSpan[ 0 ] ) );

			// TODO: don't copy ?
			XMFLOAT3 mletVertices[ MAX_VTX ] = {};
			for( u64 vi = 0; vi < m.vertex_count; ++vi )
			{
				const vertex& v = vtxSpan[ mletVtx[ vi + m.vertex_offset ] ];
				mletVertices[ vi ] = { v.px,v.py,v.pz };
			}

			BoundingBox aabb = {};
			BoundingBox::CreateFromPoints( aabb, m.vertex_count, mletVertices, sizeof( mletVertices[ 0 ] ) );

			meshlet data = {};
			data.center = aabb.Center;
			data.extent = aabb.Extents;
			data.coneAxis = *( const XMFLOAT3* ) bounds.cone_axis;
			data.coneApex = *( const XMFLOAT3* ) bounds.cone_apex;
			data.coneX = bounds.cone_axis_s8[ 0 ];
			data.coneY = bounds.cone_axis_s8[ 1 ];
			data.coneZ = bounds.cone_axis_s8[ 2 ];
			data.coneCutoff = bounds.cone_cutoff_s8;
			data.vertexCount = m.vertex_count;
			data.triangleCount = m.triangle_count;
			data.dataOffset = dataOffset;
			mletsDesc.push_back( data );
		}

		mletsData.insert( std::end( mletsData ), std::begin( meshletData ), std::end( meshletData ) );
	}
}

constexpr u64 lodMaxCount = 1;
// TODO: indicesOut offset ?
inline u64 MeshoptMakeMeshLods(
	const std::span<vertex> verticesView,
	const std::span<u32>	indicesView,
	u32*					indicesOut,
	std::vector<range>&	    meshLods
){
	constexpr float ERROR_THRESHOLD = 1e-2f;
	constexpr float reductionFactor = 0.85f;

	assert( meshLods[ 0 ].size );

	u64 totalIndexCount = meshLods[ 0 ].size;
	u64 meshLodsCount = 1;
	for( ; meshLodsCount < std::size( meshLods ); ++meshLodsCount )
	{
		const range& prevLod = meshLods[ meshLodsCount - 1 ];
		const u32* prevIndices = indicesOut + prevLod.offset;
		u32 nextIndicesOffset = prevLod.offset + prevLod.size;
		u32* nextIndices = indicesOut + nextIndicesOffset;

		u64 nextIndicesCount = meshopt_simplify( nextIndices,
												 prevIndices,
												 prevLod.size,
												 &verticesView[ 0 ].px,
												 std::size( verticesView ),
												 sizeof( verticesView[ 0 ] ),
												 float( prevLod.size ) * reductionFactor,
												 ERROR_THRESHOLD );

		assert( nextIndicesCount <= prevLod.size );

		meshopt_optimizeVertexCache( nextIndices, nextIndices, nextIndicesCount, std::size( verticesView ) );
		// NOTE: reached the error bound
		if( nextIndicesCount == prevLod.size ) break;

		meshLods[ meshLodsCount ].size = nextIndicesCount;
		meshLods[ meshLodsCount ].offset = nextIndicesOffset;

		totalIndexCount += nextIndicesCount;
	}

	meshLods.resize( meshLodsCount );
	
	return totalIndexCount;
}

// TODO: world handedness
// TODO: use u16 idx
// TODO: quantize pos + uvs
// TODO: revisit index offsets and stuff
// TODO: expose mem ops ?
static std::pair<range, range> AssembleAndOptimizeMesh(
	const std::vector<float>& attrStreams,
	const std::vector<u32>&   importedIndices,
	const imported_mesh&      rawMesh,
	std::vector<vertex>&      vertices,
	std::vector<u32>&         indices
){
	u64 vtxAttrCount = rawMesh.posStreamRange.size / 3;
	u64 vtxOffset = std::size( vertices );
	vertices.resize( vtxOffset + vtxAttrCount );

	vertex* firstVertex = &vertices[ vtxOffset ];
	for( u64 i = 0; i < vtxAttrCount; ++i )
	{
		const float* posStream = std::data( attrStreams ) + rawMesh.posStreamRange.offset;

		firstVertex[ i ].px = -posStream[ i * 3 + 0 ];
		firstVertex[ i ].py = posStream[ i * 3 + 1 ];
		firstVertex[ i ].pz = posStream[ i * 3 + 2 ];
	}
	for( u64 i = 0; i < vtxAttrCount; ++i )
	{
		const float* normalStream = std::data( attrStreams ) + rawMesh.normalStreamRange.offset;
		const float* tanStream = std::data( attrStreams ) + rawMesh.tanStreamRange.offset;

		float nx = -normalStream[ i * 3 + 0 ];
		float ny = normalStream[ i * 3 + 1 ];
		float nz = normalStream[ i * 3 + 2 ];
		float tx = tanStream[ i * 3 + 0 ];
		float ty = tanStream[ i * 3 + 1 ];
		float tz = tanStream[ i * 3 + 2 ];

		DirectX::XMFLOAT2 octaNormal = OctaNormalEncode( { nx,ny,nz } );
		float tanAngle = EncodeTanToAngle( { nx,ny,nz }, { tx,ty,tz } );

		i8 snormNx = meshopt_quantizeSnorm( octaNormal.x, 8 );
		i8 snormNy = meshopt_quantizeSnorm( octaNormal.y, 8 );
		i8 snormTanAngle = meshopt_quantizeSnorm( tanAngle, 8 );

		u32 bitsSnormNx = *( u8* ) &snormNx;
		u32 bitsSnormNy = *( u8* ) &snormNy;
		u32 bitsSnormTanAngle = *( u8* ) &snormTanAngle;

		u32 packedTanFrame = bitsSnormNx | ( bitsSnormNy << 8 ) | ( bitsSnormTanAngle << 16 );

		firstVertex[ i ].snorm8octTanFrame = packedTanFrame;
		
	}
	for( u64 i = 0; i < vtxAttrCount; ++i )
	{
		const float* uvsStream = std::data( attrStreams ) + rawMesh.uvsStreamRange.offset;

		firstVertex[ i ].tu = uvsStream[ i * 2 + 0 ];
		firstVertex[ i ].tv = uvsStream[ i * 2 + 1 ];
	}

	u64 idxOffset = std::size( indices );
	indices.resize( idxOffset + rawMesh.idxRange.size * lodMaxCount );

	for( u64 i = 0; i < rawMesh.idxRange.size; ++i )
	{
		indices[ idxOffset + i ] = importedIndices[ rawMesh.idxRange.offset + i ] + vtxOffset;
	}

	// NOTE: optimize and lod
	u64 newVtxCount = MeshoptReindexMesh( 
		std::span<vertex>{ std::data( vertices ) + vtxOffset,vtxAttrCount }, 
		{ std::data( indices ) + idxOffset, rawMesh.idxRange.size } );
	vertices.resize( vtxOffset + newVtxCount );
	MeshoptOptimizeMesh( std::span<vertex>{ firstVertex,vtxAttrCount }, { std::data( indices ) + idxOffset, rawMesh.idxRange.size } );

	return{ { vtxOffset, u32( std::size( vertices ) - vtxOffset ) }, { idxOffset, rawMesh.idxRange.size } };
}


// TODO: more efficient copy
// TODO: better binary file design ?
void CompileGlbAssetToBinary( 
	const std::vector<u8>&	glbData, 
	std::vector<u8>&		drakAsset
){
	using namespace DirectX;

	std::vector<float>				meshAttrs;
	std::vector<u32>				rawIndices;
	std::vector<imported_mesh>		rawMeshDescs;

	std::vector<vertex>				vertices;
	std::vector<u32>				indices;
	std::vector<u8>					texBin;
	std::vector<mesh_desc>	        meshDescs;
	std::vector<material_data>		mtrlDescs;
	std::vector<image_metadata>		imgDescs;

	std::vector<meshlet>			mlets;
	std::vector<u32>				mletsData;

	LoadGlbFile( glbData, meshAttrs, rawIndices, texBin, imgDescs, mtrlDescs, rawMeshDescs );

	return;

	meshDescs.reserve( std::size( rawMeshDescs ) );
	// TODO: expose lod loop ?
	for( const imported_mesh& m : rawMeshDescs )
	{
		{
			u32 posAttrCount = m.posStreamRange.size / 3;
			u32 normalAttrCount = m.normalStreamRange.size / 3;
			u32 tanAttrCount = m.tanStreamRange.size / 3;
			u32 uvsAttrCount = m.uvsStreamRange.size / 2;
			assert( ( posAttrCount == normalAttrCount ) && ( posAttrCount == tanAttrCount ) && ( posAttrCount == uvsAttrCount ) );

			assert( sizeof( rawIndices[ 0 ] ) == sizeof( indices[ 0 ] ) );
		}

		auto[ vtxRange, idxRange ] = AssembleAndOptimizeMesh( meshAttrs, rawIndices, m, vertices, indices );

		std::vector<range> idxLods( lodMaxCount );
		idxLods[ 0 ] = idxRange;
		u64 totalIndexCount = MeshoptMakeMeshLods(
			{ std::data( vertices ) + vtxRange.offset,vtxRange.size }, { std::data( indices ) + idxRange.offset, idxRange.size },
			std::data( indices ),
			idxLods );
		indices.resize( idxRange.offset + totalIndexCount );

		const std::span<vertex> vtxSpan = { std::data( vertices ) + vtxRange.offset,vtxRange.size };
		std::vector<range> meshletLods;
		MeshoptMakeMeshlets( vtxSpan, indices, idxLods, mlets, mletsData, meshletLods );

		assert( std::size( idxLods ) == std::size( meshletLods ) );

		meshDescs.push_back( {} );

		//mesh_desc& meshOut = meshDescs[ std::size( meshDescs ) - 1 ];
		//meshOut.vertexCount = vtxRange.size;
		//meshOut.vertexOffset = vtxRange.offset;
		//meshOut.lodCount = std::size( idxLods );
		//for( u64 l = 0; l < std::size( idxLods ); ++l )
		//{
		//	meshOut.lods[ l ].indexCount = idxLods[ l ].size;
		//	meshOut.lods[ l ].indexOffset = idxLods[ l ].offset;
		//	meshOut.lods[ l ].meshletCount = meshletLods[ l ].size;
		//	meshOut.lods[ l ].meshletOffset = meshletLods[ l ].offset;
		//}
		//
		//meshOut.aabbMin = { -m.aabbMin[ 0 ], m.aabbMin[ 1 ], m.aabbMin[ 2 ] };
		//meshOut.aabbMax = { -m.aabbMax[ 0 ], m.aabbMax[ 1 ], m.aabbMax[ 2 ] };
		//
		//{
		//	XMVECTOR xmm0 = XMLoadFloat3( &meshOut.aabbMin );
		//	XMVECTOR xmm1 = XMLoadFloat3( &meshOut.aabbMax );
		//	XMVECTOR center = XMVectorScale( XMVectorAdd( xmm1, xmm0 ), 0.5f );
		//	XMVECTOR extent = XMVectorAbs( XMVectorScale( XMVectorSubtract( xmm1, xmm0 ), 0.5f ) );
		//	XMStoreFloat3( &meshOut.center, center );
		//	XMStoreFloat3( &meshOut.extent, extent );
		//	XMStoreFloat3( &out.aabbMin, XMVectorAdd( center, extent ) );
		//	XMStoreFloat3( &out.aabbMax, XMVectorSubtract( center, extent ) );
		//}

		//meshOut.materialIndex = rawMesh.mtlIdx;
	}

	// TODO: assert that none of these overflow 2gbs
	u64 totalDataSize = 
		BYTE_COUNT( meshDescs ) + BYTE_COUNT( mtrlDescs ) + BYTE_COUNT( imgDescs ) + 
		BYTE_COUNT( vertices ) + BYTE_COUNT( indices ) + BYTE_COUNT( texBin ) +
		BYTE_COUNT( mlets ) + BYTE_COUNT( mletsData );

	drak_file_footer fileFooter = {};
	fileFooter.compressedSize = totalDataSize;
	fileFooter.originalSize = totalDataSize;
	
	std::vector<u8> outData( fileFooter.originalSize + +sizeof( fileFooter ) );
	u8* pOutData = std::data( outData );
	const u8* pDataBegin = std::data( outData );

	fileFooter.meshesByteRange = { u32( pOutData - pDataBegin ),BYTE_COUNT( meshDescs ) };
	pOutData = ( u8* ) std::memcpy( pOutData, std::data( meshDescs ), BYTE_COUNT( meshDescs ) ) + BYTE_COUNT( meshDescs );

	fileFooter.mtrlsByteRange = { u32( pOutData - pDataBegin ),BYTE_COUNT( mtrlDescs ) };
	pOutData = ( u8* ) std::memcpy( pOutData, std::data( mtrlDescs ), BYTE_COUNT( mtrlDescs ) ) + BYTE_COUNT( mtrlDescs );

	fileFooter.imgsByteRange = { u32( pOutData - pDataBegin ),BYTE_COUNT( imgDescs ) };
	pOutData = ( u8* ) std::memcpy( pOutData, std::data( imgDescs ), BYTE_COUNT( imgDescs ) ) + BYTE_COUNT( imgDescs );

	fileFooter.vtxByteRange = { u32( pOutData - pDataBegin ),BYTE_COUNT( vertices ) };
	pOutData = ( u8* ) std::memcpy( pOutData, std::data( vertices ), BYTE_COUNT( vertices ) ) + BYTE_COUNT( vertices );
	
	fileFooter.idxByteRange = { u32( pOutData - pDataBegin ),BYTE_COUNT( indices ) };
	pOutData = ( u8* ) std::memcpy( pOutData, std::data( indices ), BYTE_COUNT( indices ) ) + BYTE_COUNT( indices );
	
	fileFooter.mletsByteRange = { u32( pOutData - pDataBegin ),BYTE_COUNT( mlets ) };
	pOutData = ( u8* ) std::memcpy( pOutData, std::data( mlets ), BYTE_COUNT( mlets ) ) + BYTE_COUNT( mlets );
	
	fileFooter.mletsDataByteRange = { u32( pOutData - pDataBegin ),BYTE_COUNT( mletsData ) };
	pOutData = ( u8* ) std::memcpy( pOutData, std::data( mletsData ), BYTE_COUNT( mletsData ) ) + BYTE_COUNT( mletsData );
	
	fileFooter.texBinByteRange = { u32( pOutData - pDataBegin ),BYTE_COUNT( texBin ) };
	pOutData = ( u8* ) std::memcpy( pOutData, std::data( texBin ), BYTE_COUNT( texBin ) ) + BYTE_COUNT( texBin );
	
	*( drak_file_footer*) ( std::data( outData ) + totalDataSize ) = fileFooter;

	drakAsset = std::move( outData );
}