#include "asset_compiler.h"

// TODO: make into program/dll
#include <meshoptimizer.h>
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include "spng.h"

//#include "r_data_structs.h"
#include <cmath>
#include <vector>
#include <string_view>

#include "sys_os_api.h"
// TODO: utils file
#define ACOMPL_ERR( err )															\
do{																					\
	constexpr char DEV_ERR_STR[] = RUNTIME_ERR_LINE_FILE_STR;						\
	i32 errorRes = err;																\
	if( errorRes ){																	\
		char dbgStr[256] = {};														\
		strcat_s( dbgStr, sizeof( dbgStr ), DEV_ERR_STR );							\
		SysErrMsgBox( dbgStr );														\
		abort();																	\
	}																				\
}while( 0 )			


// TODO: write own lib
// TODO: switch to MSVC ?
#ifdef __clang__
// NOTE: clang-cl on VS issue
#undef __clang__
#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>
#define __clang__

#elif _MSC_VER >= 1916

#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>

#endif

#include <DirectXCollision.h>


#include "bcn_compressor.h"


// TODO: use DirectXMath ?
// TODO: fast ?
inline float SignNonZero( float e )
{
	return ( e >= 0.0f ) ? 1.0f : -1.0f;
}
inline vec2 OctaNormalEncode( vec3 n )
{
	// NOTE: Project the sphere onto the octahedron, and then onto the xy plane
	float absLen = std::fabs( n.x ) + std::fabs( n.y ) + std::fabs( n.z );
	float absNorm = ( absLen == 0.0f ) ? 0.0f : 1.0f / absLen;
	float nx = n.x * absNorm;
	float ny = n.y * absNorm;

	// NOTE: Reflect the folds of the lower hemisphere over the diagonals
	float octaX = ( n.z < 0.f ) ? ( 1.0f - std::fabs( ny ) ) * SignNonZero( nx ): nx;
	float octaY = ( n.z < 0.f ) ? ( 1.0f - std::fabs( nx ) ) * SignNonZero( ny ): ny;
	
	return { octaX, octaY };
}
inline float EncodeTanToAngle( vec3 n, vec3 t )
{
	using namespace DirectX;

	// NOTE: inspired by Doom Eternal
	vec3 tanRef = ( std::abs( n.x ) > std::abs( n.z ) ) ?
		vec3{ -n.y, n.x, 0.0f } :
		vec3{ 0.0f, -n.z, n.y };

	// TODO: use angle between normals ?
	float tanRefAngle = XMVectorGetX( XMVector3AngleBetweenVectors( XMLoadFloat3( &t ),
																	XMLoadFloat3( &tanRef ) ) );
	return XMScalarModAngle( tanRefAngle ) * XM_1DIVPI;
}
inline u8 FloatToSnorm8( float e )
{
	return std::round( 127.5f + e * 127.5f );
}

struct meshlets_data
{
	std::vector<meshlet> meshlets;
	std::vector<u32> vtxIndirBuf;
	std::vector<u8> triangleBuf;
};

// TODO: context per gltf ?
struct png_decoder
{
	spng_ctx* ctx;

	png_decoder( const u8* pngData, u64 pngSize ) : ctx{ spng_ctx_new( 0 ) }
	{
		// NOTE: ignore chunk CRC's 
		ACOMPL_ERR( spng_set_crc_action( ctx, SPNG_CRC_USE, SPNG_CRC_USE ) );
		ACOMPL_ERR( spng_set_png_buffer( ctx, pngData, pngSize ) );
	}
	~png_decoder() { spng_ctx_free( ctx ); }

};
inline u64 PngGetDecodedImageByteCount( const png_decoder& dcd )
{
	u64 outSize = 0;
	ACOMPL_ERR( spng_decoded_image_size( dcd.ctx, SPNG_FMT_RGBA8, &outSize ) );

	return outSize;
}
inline u64 PngGetDecodedImageSize( const png_decoder& dcd )
{
	spng_ihdr ihdr = {};
	ACOMPL_ERR( spng_get_ihdr( dcd.ctx, &ihdr ) );
	
	return u64( ihdr.width ) | ( u64( ihdr.height ) << 32 );
}
inline void PngDecodeImageFromMem( const png_decoder& dcd, u8* txBinOut, u64 txSize )
{
	ACOMPL_ERR( spng_decode_image( dcd.ctx, txBinOut, txSize, SPNG_FMT_RGBA8, 0 ) );
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
inline u64 CountTextureBytesFromGlb( const cgltf_texture* t, const u8* pBin )
{
	using namespace std;

	u64 imgOffset = t->image->buffer_view->offset;
	u64 imgSize = t->image->buffer_view->size;

	u64 byteCount = 0;
	std::string_view mimeType = { t->image->mime_type };
	if( mimeType == "image/png"sv )
	{
		png_decoder dcd( pBin + imgOffset, imgSize );
		byteCount = PngGetDecodedImageByteCount( dcd );
	}
	else if( mimeType == "image/jpeg"sv )
	{
		assert( 0 );
	}
	else if( mimeType == "image/ktx2"sv )
	{
		assert( 0 );
	}

	return byteCount;
}
// TODO: account for jpeg, ktx, dds, etc
inline image_metadata LoadTextureFromGlb(
	const cgltf_texture*	t,
	const u8* 				pBin,
	pbr_texture_type		textureType,
	std::vector<u8>&		texBinData )
{
	using namespace std;

	u64 imgOffset = t->image->buffer_view->offset;
	u64 imgSize = t->image->buffer_view->size;

	image_metadata imgInfo = {};
	imgInfo.format = textureType;

	std::string_view mimeType = { t->image->mime_type };
	if( mimeType == "image/png"sv )
	{
		png_decoder dcd( pBin + imgOffset, imgSize );
		u64 imageByteCount = PngGetDecodedImageByteCount( dcd );
		u64 widthHeight = PngGetDecodedImageSize( dcd );
		u64 texBinDataOffset = std::size( texBinData );
		u8* textureBinData = std::data( texBinData ) + texBinDataOffset;
		texBinData.resize( std::size( texBinData ) + imageByteCount );
		PngDecodeImageFromMem( dcd, textureBinData, imageByteCount );

		imgInfo.texBinRange = { texBinDataOffset,imageByteCount };
		imgInfo.width = widthHeight & u32( -1 );
		imgInfo.height = widthHeight >> 32;
	}
	else if( mimeType == "image/jpeg"sv )
	{
		assert( 0 );
	}
	else if( mimeType == "image/ktx2"sv )
	{
		assert( 0 );
	}

	if( t->sampler )
	{
		imgInfo.samplerConfig = {
			GetSamplerFilter( t->sampler->min_filter ),
			GetSamplerFilter( t->sampler->mag_filter ),
			GetSamplerAddrMode( t->sampler->wrap_s ),
			GetSamplerAddrMode( t->sampler->wrap_t )
		};
	}

	return imgInfo;
}

// NOTE: will assume: pos,normal,tan are vec3 and uv is vec2
// TODO: meaningful names/ distictions between size and sizeInBytes
struct mesh_primitive
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

inline u64 GetMetallicPbrMaterialByteCount( const cgltf_material* material, const u8* pBin )
{
	u64 mtlSizeInBytes = 0;
	const cgltf_pbr_metallic_roughness& pbrMetallicRoughness = material->pbr_metallic_roughness;
	const cgltf_texture* pbrBaseCol = pbrMetallicRoughness.base_color_texture.texture;
	const cgltf_texture* metalRoughMap = pbrMetallicRoughness.metallic_roughness_texture.texture;
	const cgltf_texture* normalMap = material->normal_texture.texture;

	if( pbrBaseCol ) mtlSizeInBytes += CountTextureBytesFromGlb( pbrBaseCol, pBin );
	if( metalRoughMap ) mtlSizeInBytes += CountTextureBytesFromGlb( metalRoughMap, pBin );
	if( normalMap ) mtlSizeInBytes += CountTextureBytesFromGlb( normalMap, pBin );

	ACOMPL_ERR( bool( material->occlusion_texture.texture != metalRoughMap ) );

	return mtlSizeInBytes;
}
inline pbr_material LoadMetallicPbrMaterial( 
	const cgltf_material*	material, 
	const u8*				pBin,
	std::vector<u8>&		texBinData )
{
	pbr_material mtl = {};
	const cgltf_pbr_metallic_roughness& pbrMetallicRoughness = material->pbr_metallic_roughness;
	if( const cgltf_texture* pbrBaseCol = pbrMetallicRoughness.base_color_texture.texture )
	{ 
		mtl.textureMeta[ 0 ] = LoadTextureFromGlb( pbrBaseCol, pBin, PBR_TEXTURE_BASE_COLOR, texBinData );
		mtl.baseColorFactor[ 0 ] = pbrMetallicRoughness.base_color_factor[ 0 ];
		mtl.baseColorFactor[ 1 ] = pbrMetallicRoughness.base_color_factor[ 1 ];
		mtl.baseColorFactor[ 2 ] = pbrMetallicRoughness.base_color_factor[ 2 ];

		const u8* texBin = std::data( texBinData ) + mtl.textureMeta[ 0 ].texBinRange.offset;
		u8* texBinOut = std::data( texBinData ) + mtl.textureMeta[ 0 ].texBinRange.offset;
		u64 width = mtl.textureMeta[ 0 ].width;
		u64 height = mtl.textureMeta[ 0 ].height;
		u64 bc1ColByteCount = GetCompressedTextureByteCount( width, height, bc1BytesPerBlock );
		CompressToBc1_SIMD( texBin, width, height, texBinOut );
		texBinData.resize( mtl.textureMeta[ 0 ].texBinRange.offset + bc1ColByteCount );
		mtl.textureMeta[ 0 ].texBinRange.size = bc1ColByteCount;
	}
	const cgltf_texture* metalRoughMap = pbrMetallicRoughness.metallic_roughness_texture.texture;
	if( metalRoughMap )
	{
		mtl.textureMeta[ 1 ] = LoadTextureFromGlb( metalRoughMap, pBin, PBR_TEXTURE_ORM, texBinData );
		mtl.metallicFactor = pbrMetallicRoughness.metallic_factor;
		mtl.roughnessFactor = pbrMetallicRoughness.roughness_factor;
		const u8* texBin = std::data( texBinData ) + mtl.textureMeta[ 1 ].texBinRange.offset;
		u8* texBinOut = std::data( texBinData ) + mtl.textureMeta[ 1 ].texBinRange.offset;
		u64 width = mtl.textureMeta[ 1 ].width;
		u64 height = mtl.textureMeta[ 1 ].height;
		u64 bc5ColByteCount = GetCompressedTextureByteCount( width, height, bc5BytesPerBlock );
		CompressMetalRoughMapToBc5_SIMD( texBin, width, height, texBinOut );
		texBinData.resize( mtl.textureMeta[ 1 ].texBinRange.offset + bc5ColByteCount );
		mtl.textureMeta[ 1 ].texBinRange.size = bc5ColByteCount;
	}
	if( const cgltf_texture* normalMap = material->normal_texture.texture )
	{
		mtl.textureMeta[ 2 ] = LoadTextureFromGlb( normalMap, pBin, PBR_TEXTURE_NORMALS, texBinData );
		const u8* texBin = std::data( texBinData ) + mtl.textureMeta[ 2 ].texBinRange.offset;
		u8* texBinOut = std::data( texBinData ) + mtl.textureMeta[ 2 ].texBinRange.offset;
		u64 width = mtl.textureMeta[ 2 ].width;
		u64 height = mtl.textureMeta[ 2 ].height;
		u64 bc5ColByteCount = GetCompressedTextureByteCount( width, height, bc5BytesPerBlock );
		CompressNormalMapToBc5_SIMD( texBin, width, height, texBinOut );
		texBinData.resize( mtl.textureMeta[ 2 ].texBinRange.offset + bc5ColByteCount );
		mtl.textureMeta[ 2 ].texBinRange.size = bc5ColByteCount;
	}

	return mtl;
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

/*
static void readAccessor( std::vector<float>& data, const cgltf_accessor* accessor )
{
	assert( accessor->type == cgltf_type_scalar );

	data.resize( accessor->count );
	cgltf_accessor_unpack_floats( accessor, &data[ 0 ], data.size() );
}
static void readAccessor( std::vector<Attr>& data, const cgltf_accessor* accessor )
{
	size_t components = cgltf_num_components( accessor->type );

	std::vector<float> temp( accessor->count * components );
	cgltf_accessor_unpack_floats( accessor, &temp[ 0 ], temp.size() );

	data.resize( accessor->count );

	for( size_t i = 0; i < accessor->count; ++i )
	{
		for( size_t k = 0; k < components && k < 4; ++k )
			data[ i ].f[ k ] = temp[ i * components + k ];
	}
}
static void fixupIndices( std::vector<unsigned int>& indices, cgltf_primitive_type& type )
{
	if( type == cgltf_primitive_type_line_loop )
	{
		std::vector<unsigned int> result;
		result.reserve( indices.size() * 2 + 2 );

		for( size_t i = 1; i <= indices.size(); ++i )
		{
			result.push_back( indices[ i - 1 ] );
			result.push_back( indices[ i % indices.size() ] );
		}

		indices.swap( result );
		type = cgltf_primitive_type_lines;
	}
	else if( type == cgltf_primitive_type_line_strip )
	{
		std::vector<unsigned int> result;
		result.reserve( indices.size() * 2 );

		for( size_t i = 1; i < indices.size(); ++i )
		{
			result.push_back( indices[ i - 1 ] );
			result.push_back( indices[ i ] );
		}

		indices.swap( result );
		type = cgltf_primitive_type_lines;
	}
	else if( type == cgltf_primitive_type_triangle_strip )
	{
		std::vector<unsigned int> result;
		result.reserve( indices.size() * 3 );

		for( size_t i = 2; i < indices.size(); ++i )
		{
			int flip = i & 1;

			result.push_back( indices[ i - 2 + flip ] );
			result.push_back( indices[ i - 1 - flip ] );
			result.push_back( indices[ i ] );
		}

		indices.swap( result );
		type = cgltf_primitive_type_triangles;
	}
	else if( type == cgltf_primitive_type_triangle_fan )
	{
		std::vector<unsigned int> result;
		result.reserve( indices.size() * 3 );

		for( size_t i = 2; i < indices.size(); ++i )
		{
			result.push_back( indices[ 0 ] );
			result.push_back( indices[ i - 1 ] );
			result.push_back( indices[ i ] );
		}

		indices.swap( result );
		type = cgltf_primitive_type_triangles;
	}
	else if( type == cgltf_primitive_type_lines )
	{
		// glTF files don't require that line index count is divisible by 2, but it is obviously critical for scenes to render
		indices.resize( indices.size() / 2 * 2 );
	}
	else if( type == cgltf_primitive_type_triangles )
	{
		// glTF files don't require that triangle index count is divisible by 3, but it is obviously critical for scenes to render
		indices.resize( indices.size() / 3 * 3 );
	}
}
*/
// TODO: rethink samplers
// TODO: use own mem
// TODO: use gltfpack with no quant to merge meshes or do it by hand ?
// TODO: rename mesh_primitive
// TODO: use u16 idx
// TODO: quantize pos + uvs
// TODO: general
// TODO: improve, make safer ?
static void
LoadGlbFile(
	const std::vector<u8>&			glbData,
	DirectX::BoundingBox&			outAabb,
	std::vector<vertex>&			vertices,
	std::vector<u32>&				indices,
	std::vector<u8>&				textureBinData,
	std::vector<pbr_material>&		catalogue )
{
	using namespace DirectX;

	cgltf_options options = { .type = cgltf_file_type_glb };
	cgltf_data* data = 0;
	ACOMPL_ERR( cgltf_parse( &options, std::data( glbData ), std::size( glbData ), &data ) );
	ACOMPL_ERR( cgltf_validate( data ) );

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
	std::vector<range> meshes( data->meshes_count );
	std::vector<mesh_primitive> meshPrims;
	std::vector<u16> uniqueMtlIdx( data->materials_count, 0 );

	{
		u64 meshPrimsCount = 0;
		for( u64 m = 0; m < data->meshes_count; ++m ){ meshPrimsCount += data->meshes[ m ].primitives_count; }
		meshPrims.reserve( meshPrimsCount );
	}

	for( u64 m = 0; m < data->meshes_count; ++m )
	{
		const cgltf_mesh& mesh = data->meshes[ m ];

		meshes[ m ] = { std::size( meshPrims ),mesh.primitives_count };

		for( u64 p = 0; p < mesh.primitives_count; ++p )
		{
			const cgltf_primitive& prim = mesh.primitives[ p ];

			meshPrims.push_back( {} );
			mesh_primitive& meshPrim = meshPrims[ std::size( meshPrims ) - 1 ];

			if( prim.material->has_pbr_metallic_roughness )
			{
				u16 mtlIdx = u16( prim.material - data->materials );
				meshPrim.mtlIdx = mtlIdx;
				++uniqueMtlIdx[ mtlIdx ];
			}

			// NOTE: attrs must have the same count 
			u64 primVtxCount = prim.attributes[ 0 ].data->count;
			for( u64 a = 0; a < prim.attributes_count; ++a ) assert( primVtxCount == prim.attributes[ a ].data->count );

			constexpr u64 NumOfFloatPerAttr = 11;
			constexpr u64 NumOfOnlyCareAttrs = 4;
			u64 attrLocalCount = NumOfFloatPerAttr * NumOfOnlyCareAttrs * primVtxCount;
			attrStreams.reserve( std::size( attrStreams ) + attrLocalCount );

			for( u64 a = 0; a < prim.attributes_count; ++a )
			{
				const cgltf_attribute& vtxAttr = prim.attributes[ a ];
				if( vtxAttr.type == cgltf_attribute_type_invalid ) continue;
				

				if( vtxAttr.type == cgltf_attribute_type_position )
				{
					assert( vtxAttr.data->has_min && vtxAttr.data->has_min );

					meshPrim.aabbMin[ 0 ] = vtxAttr.data->min[ 0 ];
					meshPrim.aabbMin[ 1 ] = vtxAttr.data->min[ 1 ];
					meshPrim.aabbMin[ 2 ] = vtxAttr.data->min[ 2 ];

					meshPrim.aabbMax[ 0 ] = vtxAttr.data->max[ 0 ];
					meshPrim.aabbMax[ 1 ] = vtxAttr.data->max[ 1 ];
					meshPrim.aabbMax[ 2 ] = vtxAttr.data->max[ 2 ];
				}


				u64 attrNumComp = cgltf_num_components( vtxAttr.data->type );
				switch( vtxAttr.type )
				{
				case cgltf_attribute_type_position: 
					assert( attrNumComp == 3 );
					meshPrim.posStreamRange = { std::size( attrStreams ), 3 * vtxAttr.data->count }; break;
				case cgltf_attribute_type_normal: 
					assert( attrNumComp == 3 );
					meshPrim.normalStreamRange = { std::size( attrStreams ), 3 * vtxAttr.data->count }; break;
				case cgltf_attribute_type_tangent:
					assert( attrNumComp == 4 );
					meshPrim.tanStreamRange = { std::size( attrStreams ), 3 * vtxAttr.data->count }; break;
				case cgltf_attribute_type_texcoord: 
					assert( attrNumComp == 2 );
					meshPrim.uvsStreamRange = { std::size( attrStreams ), 2 * vtxAttr.data->count }; break;
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
			meshPrim.idxRange = { idxDstOffset,prim.indices->count };
			for( u64 i = 0; i < prim.indices->count; ++i )
			{
				u64 idx = cgltf_component_read_index( idxSrc + idxStride * i, prim.indices->component_type );
				indices[ i + idxDstOffset ] = u32( idx );
			}
		}
	}

	// TODO: is it worth it ?
	u64 reserveTxBinSize = 0;
	for( u16 mtlIdx = 0; mtlIdx < std::size( uniqueMtlIdx ); ++mtlIdx )
	{
		if( uniqueMtlIdx[ mtlIdx ] ) 
			reserveTxBinSize += GetMetallicPbrMaterialByteCount( &data->materials[ mtlIdx ], pBin );
	}
	textureBinData.reserve( std::size( textureBinData ) + reserveTxBinSize );

	for( u16 mtlIdx = 0; mtlIdx < std::size( uniqueMtlIdx ); ++mtlIdx )
	{
		if( uniqueMtlIdx[ mtlIdx ] ) 
			catalogue.push_back( LoadMetallicPbrMaterial( &data->materials[ mtlIdx ], pBin, textureBinData ) );
	}

	cgltf_free( data );


	// NOTE: assemble meshes
	// TODO: per mesh
	BoundingBox aabb = {};
	for( u64 p = 0; p < std::size( meshPrims ); ++p )
	{
		XMVECTOR min = XMLoadFloat3( (const XMFLOAT3*) &meshPrims[ p ].aabbMin[ 0 ] );
		XMVECTOR max = XMLoadFloat3( (const XMFLOAT3*) &meshPrims[ p ].aabbMax[ 0 ] );

		XMFLOAT3 center;
		XMFLOAT3 extent;

		XMStoreFloat3( &center, XMVectorScale( XMVectorAdd( max, min ), 0.5 ) );
		XMStoreFloat3( &extent, XMVectorScale( XMVectorSubtract( max, min ), 0.5 ) );

		BoundingBox aabbPartial = { center,extent };
		if( p == 0 ) aabb = aabbPartial;

		BoundingBox::CreateMerged( aabb, aabb, aabbPartial );
	}

	for( range r : meshes )
	{
		const mesh_primitive* firstPrim = &meshPrims[ r.offset ];
		for( u64 p = 0; p < r.size; ++p )
		{
			assert( ( ( ( firstPrim[ p ].posStreamRange.size / 3 ) == ( firstPrim[ p ].normalStreamRange.size / 3 ) ) ==
					  ( ( firstPrim[ p ].tanStreamRange.size / 3 ) == ( firstPrim[ p ].uvsStreamRange.size / 2 ) ) ) );

			u64 vtxAttrCount = firstPrim[ p ].posStreamRange.size / 3;
			u64 vtxOffset = std::size( vertices );
			vertices.resize( vtxOffset + vtxAttrCount );

			vertex* firstVertex = &vertices[ vtxOffset ];
			const float* posStream = std::data( attrStreams ) + firstPrim[ p ].posStreamRange.offset;
			const float* normalStream = std::data( attrStreams ) + firstPrim[ p ].normalStreamRange.offset;
			const float* tanStream = std::data( attrStreams ) + firstPrim[ p ].tanStreamRange.offset;
			const float* uvsStream = std::data( attrStreams ) + firstPrim[ p ].uvsStreamRange.offset;
			for( u64 i = 0; i < vtxAttrCount; ++i )
			{
				firstVertex[ i ].px = -posStream[ i * 3 + 0 ];
				firstVertex[ i ].py = posStream[ i * 3 + 1 ];
				firstVertex[ i ].pz = posStream[ i * 3 + 2 ];
				// NOTE: for simplicitly we do it in here
				firstVertex[ i ].mi = firstPrim[ p ].mtlIdx;
			}
			// TODO: cache friendlier ? merge normal and tangent stream
			for( u64 i = 0; i < vtxAttrCount; ++i )
			{
				float nx = -normalStream[ i * 3 + 0 ];
				float ny = normalStream[ i * 3 + 1 ];
				float nz = normalStream[ i * 3 + 2 ];
				float tx = tanStream[ i * 3 + 0 ];
				float ty = tanStream[ i * 3 + 1 ];
				float tz = tanStream[ i * 3 + 2 ];

				vec2 octaNormal = OctaNormalEncode( { nx,ny,nz } );
				float tanAngle = EncodeTanToAngle( { nx,ny,nz }, { tx,ty,tz } );
				//firstVertex[ i ].nx = nx;
				//firstVertex[ i ].ny = ny;
				//firstVertex[ i ].nz = nz;
				//firstVertex[ i ].tAngle = tanAngle;

				firstVertex[ i ].snorm8octNx = FloatToSnorm8( octaNormal.x );
				firstVertex[ i ].snorm8octNy = FloatToSnorm8( octaNormal.y );
				firstVertex[ i ].snorm8tanAngle = FloatToSnorm8( tanAngle );
			}
			for( u64 i = 0; i < vtxAttrCount; ++i )
			{
				firstVertex[ i ].tu = uvsStream[ i * 2 + 0 ];
				firstVertex[ i ].tv = uvsStream[ i * 2 + 1 ];
			}
			for( u64 i = 0; i < firstPrim[ p ].idxRange.size; ++i ) 
				indices[ firstPrim[ p ].idxRange.offset + i ] += vtxOffset;
		}
	}

	outAabb = aabb;
}

// TODO: mesh triangulate ?
inline void MeshoptReindexMesh( std::vector<vertex>& vertices, std::vector<u32>& indices )
{
	u64 oldVtxCount = std::size( vertices );
	u64 oldIdxCount = std::size( indices );
	

	std::vector<u32> remap( oldVtxCount );
	u64 newVtxCount = meshopt_generateVertexRemap( std::data( remap ),
												   std::data( indices ),
												   oldIdxCount,
												   std::data( vertices ),
												   oldVtxCount,
												   sizeof( vertices[ 0 ] ) );
	assert( newVtxCount <= oldVtxCount );
	if( newVtxCount == oldVtxCount ) return;

	meshopt_remapIndexBuffer( std::data( indices ), std::data( indices ), oldIdxCount, std::data( remap ) );
	meshopt_remapVertexBuffer( std::data( vertices ), std::data( vertices ), oldVtxCount, sizeof( vertices[ 0 ] ), std::data( remap ) );
	vertices.resize( newVtxCount );
}

static void MeshoptMakeLods( 
	const std::vector<vertex>&	vertices,
	u64							maxLodCount,
	std::vector<u32>&			lodIndices,
	std::vector<u32>&			idxBuffer,
	std::vector<mesh_lod>&		outMeshLods )
{
	constexpr float ERROR_THRESHOLD = 1e-2f;
	constexpr double expDecay = 0.85;

	u64 meshLodsCount = 0;
	std::vector<mesh_lod> meshLods( maxLodCount );
	for( mesh_lod& lod : meshLods )
	{
		++meshLodsCount;
		lod.indexOffset = std::size( idxBuffer );
		lod.indexCount = std::size( lodIndices );

		idxBuffer.insert( std::end( idxBuffer ), std::begin( lodIndices ), std::end( lodIndices ) );

		if( meshLodsCount < maxLodCount )
		{
			u64 nextIndicesTarget = u64( double( std::size( lodIndices ) ) * expDecay );
			u64 nextIndices = meshopt_simplify( std::data( lodIndices ),
												std::data( lodIndices ),
												std::size( lodIndices ),
												&vertices[ 0 ].px,
												std::size( vertices ),
												sizeof( vertex ),
												nextIndicesTarget,
												ERROR_THRESHOLD );

			assert( nextIndices <= std::size( lodIndices ) );

			// NOTE: reached the error bound
			if( nextIndices == std::size( lodIndices ) ) break;

			lodIndices.resize( nextIndices );
			meshopt_optimizeVertexCache( std::data( lodIndices ),
										 std::data( lodIndices ),
										 std::size( lodIndices ),
										 std::size( vertices ) );
		}
	}
	assert( meshLodsCount <= maxLodCount );
	meshLods.resize( meshLodsCount );

	outMeshLods = std::move( meshLods );
}

// TODO: meshlets
u32 MeshoptBuildMeshlets( const std::vector<vertex>& vtx, const std::vector<u32>& indices, meshlets_data& mlets )
{
	constexpr u64 MAX_VERTICES = 128;
	constexpr u64 MAX_TRIANGLES = 256;
	constexpr float CONE_WEIGHT = 0.8f;

	u64 maxMeshletCount = meshopt_buildMeshletsBound( std::size( indices ), MAX_VERTICES, MAX_TRIANGLES );
	std::vector<meshopt_Meshlet> meshlets( maxMeshletCount );
	std::vector<u32> meshletVertices( maxMeshletCount * MAX_VERTICES );
	std::vector<u8> meshletTriangles( maxMeshletCount * MAX_TRIANGLES * 3 );

	u64 meshletCount = meshopt_buildMeshlets( std::data( meshlets ),
											  std::data( meshletVertices ),
											  std::data( meshletTriangles ),
											  std::data( indices ),
											  std::size( indices ),
											  &vtx[ 0 ].px,
											  std::size( vtx ),
											  sizeof( vertex ),
											  MAX_VERTICES,
											  MAX_TRIANGLES,
											  CONE_WEIGHT );


	meshopt_Meshlet& last = meshlets[ meshletCount - 1 ];
	meshletVertices.resize( last.vertex_offset + last.vertex_count );
	meshletTriangles.resize( last.triangle_offset + ( ( last.triangle_count * 3 + 3 ) & ~3 ) );
	meshlets.resize( meshletCount );

	mlets.meshlets.reserve( std::size( mlets.meshlets ) + meshletCount );

	for( meshopt_Meshlet& m : meshlets )
	{


		meshopt_Bounds bounds = meshopt_computeMeshletBounds( &meshletVertices[ m.vertex_offset ],
															  &meshletTriangles[ m.triangle_offset ],
															  m.triangle_count,
															  &vtx[ 0 ].px,
															  std::size( vtx ),
															  sizeof( vertex ) );

		meshlet data;
		data.center = vec3( bounds.center );
		data.radius = bounds.radius;
		data.coneX = bounds.cone_axis_s8[ 0 ];
		data.coneY = bounds.cone_axis_s8[ 1 ];
		data.coneZ = bounds.cone_axis_s8[ 2 ];
		data.coneCutoff = bounds.cone_cutoff_s8;
		data.vtxBufOffset = std::size( mlets.vtxIndirBuf );
		data.triBufOffset = std::size( mlets.triangleBuf );
		data.vertexCount = m.vertex_count;
		data.triangleCount = m.triangle_count;

		mlets.meshlets.push_back( data );
	}

	mlets.vtxIndirBuf.insert( std::end( mlets.vtxIndirBuf ), std::begin( meshletVertices ), std::end( meshletVertices ) );
	mlets.triangleBuf.insert( std::end( mlets.triangleBuf ), std::begin( meshletTriangles ), std::end( meshletTriangles ) );

	return std::size( meshlets );
}

// TODO:
static void MeshoptMakeMeshlets( 
	const std::vector<vertex>& vertices, 
	const std::vector<u32>& lodIndices, 
	std::vector<mesh_lod>& meshLods,
	meshlets_data& mlets )
{
	constexpr u64 MAX_VERTICES = 128;
	constexpr u64 MAX_TRIANGLES = 256;
	constexpr float CONE_WEIGHT = 0.8f;

	for( mesh_lod& lod : meshLods )
	{
		lod.meshletOffset = std::size( mlets.meshlets );
		// TODO: use last lod for meshlet culling ?
		lod.meshletCount = MeshoptBuildMeshlets( vertices, lodIndices, mlets );
	}
}

inline void MeshoptOptimizeMesh(
	std::vector<vertex>& vertices,
	std::vector<u32>& indices )
{
	u64 vtxCount = std::size( vertices );
	u64 idxCount = std::size( indices );

	meshopt_optimizeVertexCache( std::data( indices ), std::data( indices ), idxCount, vtxCount );
	meshopt_optimizeOverdraw( std::data( indices ),
							  std::data( indices ),
							  idxCount,
							  &vertices[ 0 ].px,
							  vtxCount,
							  sizeof( vertices[ 0 ] ),
							  1.05f );
	meshopt_optimizeVertexFetch( std::data( vertices ),
								 std::data( indices ),
								 idxCount,
								 std::data( vertices ),
								 vtxCount,
								 sizeof( vertices[ 0 ] ) );
}