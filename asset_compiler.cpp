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
inline u8 FloatToSnorm8( float e )
{
	return std::round( 127.5f + e * 127.5f );
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

// TODO: mesh triangulate ?
inline u64 MeshoptReindexMesh( std::span<vertex> vtxSpan, std::span<u32> idxSpan )
{
	vertex* vertices = std::data( vtxSpan );
	u32*	indices = std::data( idxSpan );
	u64		vtxCount = std::size( vtxSpan );
	u64		idxCount = std::size( idxSpan );

	std::vector<u32> remap( vtxCount );
	u64 newVtxCount = meshopt_generateVertexRemap( std::data( remap ),
												   indices,
												   idxCount,
												   vertices,
												   vtxCount,
												   sizeof( vertices[ 0 ] ) );
	assert( newVtxCount <= vtxCount );
	if( newVtxCount == vtxCount ) return newVtxCount;

	meshopt_remapIndexBuffer( indices, indices, idxCount, std::data( remap ) );
	meshopt_remapVertexBuffer( vertices, vertices, vtxCount, sizeof( vertices[ 0 ] ), std::data( remap ) );
	return newVtxCount;
}

struct meshlets_data
{
	std::vector<meshlet> meshlets;
	std::vector<u32> vtxIndirBuf;
	std::vector<u8> triangleBuf;
};
// NOTE: fuck C++ constness thing
// TODO: store outputs
// TODO: rewrite meshopt_computeMeshletBounds to output aabbs ?
static void MeshoptMakeMeshlets(
	const std::vector<vertex>&	vertices,
	const std::vector<u32>&		lodIndices,
	binary_mesh_desc&			meshDesc,
	std::vector<meshlet>&		mletsDesc,
	std::vector<u32>&			vtxIndirBuf,
	std::vector<u8>&			triangleBuf
){
	using namespace DirectX;


	constexpr u64 MAX_VERTICES = 128;
	constexpr u64 MAX_TRIANGLES = 256;
	constexpr float CONE_WEIGHT = 0.8f;

	
	std::vector<meshopt_Meshlet> meshlets;
	std::vector<u32> mletVtx;
	std::vector<u8> mletTris;

	const std::span<vertex> vtxSpan = { 
		const_cast<vertex*>( std::data( vertices ) ) + meshDesc.vtxRange.offset,meshDesc.vtxRange.size };

	for( u64 li = 0; li < meshDesc.lodCount; ++li )
	{
		const range& lodRange = meshDesc.lodRanges[ li ];
		const std::span<u32> lodIdx = { const_cast<u32*>( std::data( lodIndices ) ) + lodRange.offset,lodRange.size };

		u64 maxMeshletCount = meshopt_buildMeshletsBound( std::size( lodIdx ), MAX_VERTICES, MAX_TRIANGLES );
		meshlets.resize( maxMeshletCount );
		mletVtx.resize( maxMeshletCount * MAX_VERTICES );
		mletTris.resize( maxMeshletCount * MAX_TRIANGLES * 3 );

		u64 meshletCount = meshopt_buildMeshlets( std::data( meshlets ), std::data( mletVtx ), std::data( mletTris ),
												  std::data( lodIdx ), std::size( lodIdx ),
												  &vtxSpan[ 0 ].px, std::size( vtxSpan ), sizeof( vtxSpan[ 0 ] ),
												  MAX_VERTICES, MAX_TRIANGLES, CONE_WEIGHT );
		meshlets.resize( meshletCount );
		mletVtx.resize( meshletCount * MAX_VERTICES );
		mletTris.resize( meshletCount * MAX_TRIANGLES * 3 );

		u64 mletOffset = std::size( mletsDesc );
		meshDesc.mletRanges[ li ] = { mletOffset,meshletCount };

		for( u64 mi = 0; mi < std::size( meshlets ); ++mi )
		{
			const meshopt_Meshlet& m = meshlets[ mi ];
			meshopt_Bounds bounds = meshopt_computeMeshletBounds( std::data( mletVtx ), std::data( mletTris ), m.triangle_count,
																  &vtxSpan[ 0 ].px, std::size( vtxSpan ), sizeof( vtxSpan[ 0 ] ) );
			// TODO: don't copy ?
			XMFLOAT3 mletVertices[ MAX_VERTICES ] = {};
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
			data.coneX = bounds.cone_axis_s8[ 0 ];
			data.coneY = bounds.cone_axis_s8[ 1 ];
			data.coneZ = bounds.cone_axis_s8[ 2 ];
			data.coneCutoff = bounds.cone_cutoff_s8;
			data.vertexCount = m.vertex_count;
			data.triangleCount = m.triangle_count;
			data.vtxBufOffset = std::size( vtxIndirBuf );
			data.triBufOffset = std::size( triangleBuf );

			mletsDesc.push_back( data );
			vtxIndirBuf.insert( std::end( vtxIndirBuf ), std::begin( mletVtx ), std::end( mletVtx ) );
			triangleBuf.insert( std::end( triangleBuf ), std::begin( mletTris ), std::end( mletTris ) );
		}
	}
}

inline void MeshoptOptimizeMesh( std::span<vertex> vtxSpan, std::span<u32> idxSpan )
{
	vertex* vertices = std::data( vtxSpan );
	u32*	indices = std::data( idxSpan );
	u64		vtxCount = std::size( vtxSpan );
	u64		idxCount = std::size( idxSpan );

	meshopt_optimizeVertexCache( indices, indices, idxCount, vtxCount );
	meshopt_optimizeOverdraw( indices, indices, idxCount, &vertices[ 0 ].px, vtxCount, sizeof( vertices[ 0 ] ), 1.05f );
	meshopt_optimizeVertexFetch( vertices, indices, idxCount, vertices, vtxCount, sizeof( vertices[ 0 ] ) );
}

// TODO: no indicesOffset
inline u64 MeshoptMakeMeshLods(
	const std::span<vertex> verticesView,
	const std::span<u32>	indicesView,
	u64						indicesOutOffset,
	u32*					indicesOut,
	std::vector<mesh_lod>&	outMeshLods
){
	constexpr float ERROR_THRESHOLD = 1e-2f;
	constexpr float reductionFactor = 0.85f;

	std::vector<mesh_lod> meshLods( std::size( outMeshLods ) );
	meshLods[ 0 ].indexCount = std::size( indicesView );
	meshLods[ 0 ].indexOffset = indicesOutOffset;

	u64 meshLodsCount = 1;
	for( ; meshLodsCount < std::size( meshLods ); ++meshLodsCount )
	{
		const mesh_lod& prevLod = meshLods[ meshLodsCount - 1 ];
		const u32* prevIndices = indicesOut + prevLod.indexOffset;
		u32 nextIndicesOffset = prevLod.indexOffset + prevLod.indexCount;
		u32* nextIndices = indicesOut + nextIndicesOffset;

		u64 nextIndicesCount = meshopt_simplify( nextIndices,
												 prevIndices,
												 prevLod.indexCount,
												 &verticesView[ 0 ].px,
												 std::size( verticesView ),
												 sizeof( verticesView[ 0 ] ),
												 float( prevLod.indexCount ) * reductionFactor,
												 ERROR_THRESHOLD );

		assert( nextIndicesCount <= prevLod.indexCount );

		meshopt_optimizeVertexCache( nextIndices, nextIndices, nextIndicesCount, std::size( verticesView ) );
		// NOTE: reached the error bound
		if( nextIndicesCount == prevLod.indexCount ) break;

		meshLods[ meshLodsCount ].indexCount = nextIndicesCount;
		meshLods[ meshLodsCount ].indexOffset = nextIndicesOffset;
	}

	meshLods.resize( meshLodsCount );
	u64 totalIndexCount = 0;
	for( const mesh_lod& l : meshLods ) totalIndexCount += l.indexCount;

	outMeshLods = std::move( meshLods );
	return totalIndexCount;
}
// TODO: world handedness
// TODO: remove mtlIndex from vertex
// TODO: use u16 idx
// TODO: quantize pos + uvs
// TODO: revisit index offsets and stuff
static void AssembleMeshAndOptimize(
	const std::vector<float>&			attrStreams,
	const std::vector<imported_mesh>&	rawMeshes,
	const std::vector<u32>&				importedIndices,
	std::vector<vertex>&				vertices,
	std::vector<u32>&					indices,
	std::vector<binary_mesh_desc>&		meshDescs
){
	meshDescs.reserve( std::size( rawMeshes ) );

	for( const imported_mesh& m : rawMeshes )
	{
		// NOTE: assemble
		assert( ( ( ( m.posStreamRange.size / 3 ) == ( m.normalStreamRange.size / 3 ) ) ==
				  ( ( m.tanStreamRange.size / 3 ) == ( m.uvsStreamRange.size / 2 ) ) ) );

		u64 vtxAttrCount = m.posStreamRange.size / 3;
		u64 vtxOffset = std::size( vertices );
		vertices.resize( vtxOffset + vtxAttrCount );

		vertex* firstVertex = &vertices[ vtxOffset ];
		for( u64 i = 0; i < vtxAttrCount; ++i )
		{
			const float* posStream = std::data( attrStreams ) + m.posStreamRange.offset;

			firstVertex[ i ].px = -posStream[ i * 3 + 0 ];
			firstVertex[ i ].py = posStream[ i * 3 + 1 ];
			firstVertex[ i ].pz = posStream[ i * 3 + 2 ];
		}
		for( u64 i = 0; i < vtxAttrCount; ++i )
		{
			const float* normalStream = std::data( attrStreams ) + m.normalStreamRange.offset;
			const float* tanStream = std::data( attrStreams ) + m.tanStreamRange.offset;

			float nx = -normalStream[ i * 3 + 0 ];
			float ny = normalStream[ i * 3 + 1 ];
			float nz = normalStream[ i * 3 + 2 ];
			float tx = tanStream[ i * 3 + 0 ];
			float ty = tanStream[ i * 3 + 1 ];
			float tz = tanStream[ i * 3 + 2 ];

			vec2 octaNormal = OctaNormalEncode( { nx,ny,nz } );
			float tanAngle = EncodeTanToAngle( { nx,ny,nz }, { tx,ty,tz } );

			firstVertex[ i ].snorm8octNx = FloatToSnorm8( octaNormal.x );
			firstVertex[ i ].snorm8octNy = FloatToSnorm8( octaNormal.y );
			firstVertex[ i ].snorm8tanAngle = FloatToSnorm8( tanAngle );
		}
		for( u64 i = 0; i < vtxAttrCount; ++i )
		{
			const float* uvsStream = std::data( attrStreams ) + m.uvsStreamRange.offset;

			firstVertex[ i ].tu = uvsStream[ i * 2 + 0 ];
			firstVertex[ i ].tv = uvsStream[ i * 2 + 1 ];
		}

		assert( sizeof( importedIndices[ 0 ] ) == sizeof( indices[ 0 ] ) );
		constexpr u64 lodMaxCount = 4;
		u64 idxOffset = std::size( indices );
		indices.resize( idxOffset + m.idxRange.size * lodMaxCount );

		for( u64 i = 0; i < m.idxRange.size; ++i )
		{
			indices[ idxOffset + i ] = importedIndices[ m.idxRange.offset + i ] + vtxOffset;
		}
		
		// NOTE: optimize and lod
		u64 newVtxCount = MeshoptReindexMesh( { std::data( vertices ) + vtxOffset,vtxAttrCount },
											  { std::data( indices ) + idxOffset, m.idxRange.size } );
		//vertices.resize( vtxOffset + newVtxCount );
		MeshoptOptimizeMesh( { firstVertex,vtxAttrCount }, { std::data( indices ) + idxOffset, m.idxRange.size } );

		std::vector<mesh_lod> meshLods( lodMaxCount );
		u64 totalIndexCount = MeshoptMakeMeshLods( 
			{ std::data( vertices ) + vtxOffset,vtxAttrCount },
			{ std::data( indices ) + m.idxRange.offset, m.idxRange.size },
			idxOffset,
			std::data( indices ),
			meshLods );
		indices.resize( idxOffset + totalIndexCount );

		meshDescs.push_back( {} );
		binary_mesh_desc& mesh = meshDescs[ std::size( meshDescs ) - 1 ];
		mesh.vtxRange = { vtxOffset, u32( std::size( vertices ) - vtxOffset ) };
		mesh.lodCount = std::size( meshLods );
		for( u64 l = 0; l < std::size( meshLods ); ++l )
		{
			mesh.lodRanges[ l ].offset = meshLods[ l ].indexOffset;
			mesh.lodRanges[ l ].size = meshLods[ l ].indexCount;
		}
		mesh.aabbMin[ 0 ] = -m.aabbMin[ 0 ];
		mesh.aabbMin[ 1 ] = m.aabbMin[ 1 ];
		mesh.aabbMin[ 2 ] = m.aabbMin[ 2 ];
		mesh.aabbMax[ 0 ] = -m.aabbMax[ 0 ];
		mesh.aabbMax[ 1 ] = m.aabbMax[ 1 ];
		mesh.aabbMax[ 2 ] = m.aabbMax[ 2 ];
		mesh.materialIndex = m.mtlIdx;
	}
}

// TODO: better more efficient copy
// TODO: better binary file design ?
void CompileGlbAssetToBinary( 
	const std::vector<u8>&	glbData, 
	std::vector<u8>&		drakAsset
){
	std::vector<float>				meshAttrs;
	std::vector<u32>				rawIndices;
	std::vector<imported_mesh>		rawMeshDescs;

	std::vector<vertex>				vertices;
	std::vector<u32>				indices;
	std::vector<u8>					texBin;
	std::vector<binary_mesh_desc>	meshDescs;
	std::vector<material_data>		mtrlDescs;
	std::vector<image_metadata>		imgDescs;

	std::vector<meshlet>			mlets;
	std::vector<u32>				mletsVtx;
	std::vector<u8>					mletsTris;

	LoadGlbFile( glbData, meshAttrs, rawIndices, texBin, imgDescs, mtrlDescs, rawMeshDescs );
	AssembleMeshAndOptimize( meshAttrs, rawMeshDescs, rawIndices, vertices, indices, meshDescs );
	for( binary_mesh_desc& m : meshDescs )
	{
		MeshoptMakeMeshlets( vertices, indices, m, mlets, mletsVtx, mletsTris );
	}

	u64 descOffset = sizeof( drak_file_header ) + sizeof( drak_file_desc );
	u64 totalFileDescSize = BYTE_COUNT( meshDescs ) + BYTE_COUNT( mtrlDescs ) + BYTE_COUNT( imgDescs );
	u64 totalContentSize = BYTE_COUNT( vertices ) + BYTE_COUNT( indices ) + BYTE_COUNT( texBin );
	totalContentSize += BYTE_COUNT( mlets ) + BYTE_COUNT( mletsVtx ) + BYTE_COUNT( mletsTris );
	std::vector<u8> outData( descOffset + totalFileDescSize + totalContentSize );

	u8* pOutData = std::data( outData ) + descOffset;

	std::memcpy( pOutData, std::data( meshDescs ), BYTE_COUNT( meshDescs ) );
	pOutData += BYTE_COUNT( meshDescs );
	std::memcpy( pOutData, std::data( mtrlDescs ), BYTE_COUNT( mtrlDescs ) );
	pOutData += BYTE_COUNT( mtrlDescs );
	std::memcpy( pOutData, std::data( imgDescs ), BYTE_COUNT( imgDescs ) );
	pOutData += BYTE_COUNT( imgDescs );

	u64 vtxOffset = 0;
	u64 idxOffset = 0;
	u64 texOffset = 0;
	u64 mletsOffset = 0;
	u64 mletsVtxOffset = 0;
	u64 mletsTrisOffset = 0;

	const u8* pDataBegin = pOutData;
	vtxOffset = pOutData - pDataBegin;
	std::memcpy( pOutData, std::data( vertices ), BYTE_COUNT( vertices ) );
	pOutData += BYTE_COUNT( vertices );

	idxOffset = pOutData - pDataBegin;
	std::memcpy( pOutData, std::data( indices ), BYTE_COUNT( indices ) );
	pOutData += BYTE_COUNT( indices );

	mletsOffset = pOutData - pDataBegin;
	std::memcpy( pOutData, std::data( mlets ), BYTE_COUNT( mlets ) );
	pOutData += BYTE_COUNT( mlets );
	mletsVtxOffset = pOutData - pDataBegin;
	std::memcpy( pOutData, std::data( mletsVtx ), BYTE_COUNT( mletsVtx ) );
	pOutData += BYTE_COUNT( mletsVtx );
	mletsTrisOffset = pOutData - pDataBegin;
	std::memcpy( pOutData, std::data( mletsTris ), BYTE_COUNT( mletsTris ) );
	pOutData += BYTE_COUNT( mletsTris );

	texOffset = pOutData - pDataBegin;
	std::memcpy( pOutData, std::data( texBin ), BYTE_COUNT( texBin ) );

	*(drak_file_header*) std::data( outData ) = {};

	drak_file_desc fileDesc = {};
	fileDesc.compressedSize = totalContentSize;
	fileDesc.originalSize = totalContentSize;
	fileDesc.meshesCount = std::size( meshDescs );
	fileDesc.mtrlsCount = std::size( mtrlDescs );
	fileDesc.texCount = std::size( imgDescs );
	fileDesc.dataOffset = descOffset + totalFileDescSize;
	fileDesc.vtxRange = { vtxOffset, BYTE_COUNT( vertices ) };
	fileDesc.idxRange = { idxOffset, BYTE_COUNT( indices ) };
	fileDesc.texRange = { texOffset, BYTE_COUNT( texBin ) };
	fileDesc.mletsRange = { mletsOffset, BYTE_COUNT( mlets ) };
	fileDesc.mletsVtxRange = { mletsVtxOffset, BYTE_COUNT( mletsVtx ) };
	fileDesc.mletsTrisRange = { mletsTrisOffset, BYTE_COUNT( mletsTris ) };
	*(drak_file_desc*) ( std::data( outData ) + sizeof( drak_file_header ) ) = fileDesc;

	drakAsset = std::move( outData );
}