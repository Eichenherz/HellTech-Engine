#include "asset_compiler.h"

// TODO: make into program/dll
#include <meshoptimizer.h>
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include "spng.h"
#include "lz4.h"


#include "r_data_structs.h"
#include <cmath>
#include <vector>
#include <span>
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
// TODO: use angle between normals ?
inline float EncodeTanToAngle( vec3 n, vec3 t )
{
	using namespace DirectX;

	// NOTE: inspired by Doom Eternal
	vec3 tanRef = ( std::abs( n.x ) > std::abs( n.z ) ) ?
		vec3{ -n.y, n.x, 0.0f } :
		vec3{ 0.0f, -n.z, n.y };

	float tanRefAngle = XMVectorGetX( 
		XMVector3AngleBetweenVectors( XMLoadFloat3( &t ), XMLoadFloat3( &tanRef ) ) );
	return XMScalarModAngle( tanRefAngle ) * XM_1DIVPI;
}

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
// TODO: use own mem
// TODO: more ?
// TODO: better tex processing
// NOTE: assume model has pre-baked textures and merged primitives
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
				texProcessingCache[ texDscIdx ] = std::size( compressedImgs );
				raw_texture_info raw = CgltfDecodeTexture( *pbrBaseCol, pBin, texBin );

				const u8* imgSrc = std::data( texBin ) + raw.offset;
				u8* texBinOut = std::data( texBin ) + raw.offset;
				// NOTE: compress in-place
				CompressToBc1_SIMD( imgSrc, raw.width, raw.height, texBinOut );

				u64 bcByteCount = GetBCTexByteCount( raw.width, raw.height, bc1BytesPerBlock );
				texBin.resize( raw.offset + bcByteCount );

				compressedImgs.push_back(
					{ 0,{ raw.offset,bcByteCount }, raw.width, raw.height, TEXTURE_FORMAT_BC1_RGB_SRGB, TEXTURE_TYPE_2D } );
			}
			materials[ mi ].baseColIdx = texProcessingCache[ texDscIdx ];
		}
		
		if( const cgltf_texture* metalRoughMap = pbrMetallicRoughness.metallic_roughness_texture.texture )
		{
			u64 texDscIdx = u64( metalRoughMap - data->textures );
			if( texProcessingCache[ texDscIdx ] == -1 )
			{
				texProcessingCache[ texDscIdx ] = std::size( compressedImgs );
				raw_texture_info raw = CgltfDecodeTexture( *metalRoughMap, pBin, texBin );

				const u8* imgSrc = std::data( texBin ) + raw.offset;
				u8* texBinOut = std::data( texBin ) + raw.offset;
				// NOTE: compress in-place
				CompressMetalRoughMapToBc5_SIMD( imgSrc, raw.width, raw.height, texBinOut );

				u64 bcByteCount = GetBCTexByteCount( raw.width, raw.height, bc5BytesPerBlock );
				texBin.resize( raw.offset + bcByteCount );

				compressedImgs.push_back(
					{ 0,{ raw.offset,bcByteCount }, raw.width, raw.height, TEXTURE_FORMAT_BC5_UNORM, TEXTURE_TYPE_2D } );
			}
			materials[ mi ].occRoughMetalIdx = texProcessingCache[ texDscIdx ];
		}
		if( const cgltf_texture* normalMap = mtrl.normal_texture.texture )
		{
			u64 texDscIdx = u64( normalMap - data->textures );
			if( texProcessingCache[ texDscIdx ] == -1 )
			{
				texProcessingCache[ texDscIdx ] = std::size( compressedImgs );
				raw_texture_info raw = CgltfDecodeTexture( *normalMap, pBin, texBin );

				const u8* imgSrc = std::data( texBin ) + raw.offset;
				u8* texBinOut = std::data( texBin ) + raw.offset;
				// NOTE: compress in-place
				CompressNormalMapToBc5_SIMD( imgSrc, raw.width, raw.height, texBinOut );

				u64 bcByteCount = GetBCTexByteCount( raw.width, raw.height, bc5BytesPerBlock );
				texBin.resize( raw.offset + bcByteCount );

				compressedImgs.push_back(
					{ 0, { raw.offset,bcByteCount }, raw.width, raw.height, TEXTURE_FORMAT_BC5_UNORM, TEXTURE_TYPE_2D } );
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

		ACOMPL_ERR( mesh.primitives_count - 1 );

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

constexpr u64 MAX_VTX = 128;
constexpr u64 MAX_TRIS = 256;
constexpr float CONE_WEIGHT = 0.8f;
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

		mesh_desc& meshOut = meshDescs[ std::size( meshDescs ) - 1 ];
		meshOut.vertexCount = vtxRange.size;
		meshOut.vertexOffset = vtxRange.offset;
		meshOut.lodCount = std::size( idxLods );
		for( u64 l = 0; l < std::size( idxLods ); ++l )
		{
			meshOut.lods[ l ].indexCount = idxLods[ l ].size;
			meshOut.lods[ l ].indexOffset = idxLods[ l ].offset;
			meshOut.lods[ l ].meshletCount = meshletLods[ l ].size;
			meshOut.lods[ l ].meshletOffset = meshletLods[ l ].offset;
		}

		meshOut.aabbMin = { -m.aabbMin[ 0 ], m.aabbMin[ 1 ], m.aabbMin[ 2 ] };
		meshOut.aabbMax = { -m.aabbMax[ 0 ], m.aabbMax[ 1 ], m.aabbMax[ 2 ] };
		
		{
			XMVECTOR xmm0 = XMLoadFloat3( &meshOut.aabbMin );
			XMVECTOR xmm1 = XMLoadFloat3( &meshOut.aabbMax );
			XMVECTOR center = XMVectorScale( XMVectorAdd( xmm1, xmm0 ), 0.5f );
			XMVECTOR extent = XMVectorAbs( XMVectorScale( XMVectorSubtract( xmm1, xmm0 ), 0.5f ) );
			XMStoreFloat3( &meshOut.center, center );
			XMStoreFloat3( &meshOut.extent, extent );
			//XMStoreFloat3( &out.aabbMin, XMVectorAdd( center, extent ) );
			//XMStoreFloat3( &out.aabbMax, XMVectorSubtract( center, extent ) );
		}

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
	
	std::vector<u8> outData( fileFooter.originalSize + sizeof( fileFooter ) );
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