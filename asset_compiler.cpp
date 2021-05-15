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

// TODO: texture processing separately
// TODO: rethink samplers
// TODO: use own mem
// TODO: more ?
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
	std::vector<image_metadata> rawRbga8Imgs( data->textures_count, image_metadata{} );
	std::vector<material_data> materials( data->materials_count );

	for( u64 ti = 0; ti < data->textures_count; ++ti )
	{
		const cgltf_texture& t = data->textures[ ti ];
		u64 imgOffset = t.image->buffer_view->offset;
		u64 imgSize = t.image->buffer_view->size;

		std::string_view mimeType = { t.image->mime_type };
		if( mimeType == "image/png"sv )
		{
			png_decoder dcd( pBin + imgOffset, imgSize );
			u64 imageByteCount = PngGetDecodedImageByteCount( dcd );
			u64 widthHeight = PngGetDecodedImageSize( dcd );
			u64 texBinDataOffset = std::size( texBin );
			u8* textureBinData = std::data( texBin ) + texBinDataOffset;
			texBin.resize( std::size( texBin ) + imageByteCount );
			PngDecodeImageFromMem( dcd, textureBinData, imageByteCount );

			rawRbga8Imgs[ ti ].texBinRange = { texBinDataOffset,imageByteCount };
			rawRbga8Imgs[ ti ].width = widthHeight & u32( -1 );
			rawRbga8Imgs[ ti ].height = widthHeight >> 32;
		}
	}

	for( u64 mi = 0; mi < data->materials_count; ++mi )
	{
		const cgltf_material& mtrl = data->materials[ mi ];
		assert( mtrl.has_pbr_metallic_roughness );

		const cgltf_pbr_metallic_roughness& pbrMetallicRoughness = mtrl.pbr_metallic_roughness;
		if( const cgltf_texture* pbrBaseCol = pbrMetallicRoughness.base_color_texture.texture )
		{
			u64 texDscIdx = u64( pbrBaseCol - data->textures );
			image_metadata& texInfo = rawRbga8Imgs[ texDscIdx ];

			const u8* imgSrc = std::data( texBin ) + texInfo.texBinRange.offset;
			u8* texBinOut = std::data( texBin ) + texInfo.texBinRange.offset;
			u64 width = texInfo.width;
			u64 height = texInfo.height;
			u64 bc1ColByteCount = GetCompressedTextureByteCount( width, height, bc1BytesPerBlock );
			texInfo.texBinRange.size = bc1ColByteCount;
			texInfo.format = TEXTURE_FORMAT_BC1_RGB_SRGB;
			CompressToBc1_SIMD( imgSrc, width, height, texBinOut );

			vec3 baseColFactor = { 
				pbrMetallicRoughness.base_color_factor[ 0 ],
				pbrMetallicRoughness.base_color_factor[ 1 ],
				pbrMetallicRoughness.base_color_factor[ 2 ] };
			materials[ mi ].baseColFactor = baseColFactor;
			materials[ mi ].baseColIdx = texDscIdx;
		}
		
		if( const cgltf_texture* metalRoughMap = pbrMetallicRoughness.metallic_roughness_texture.texture )
		{
			u64 texDscIdx = u64( metalRoughMap - data->textures );
			image_metadata& texInfo = rawRbga8Imgs[ texDscIdx ];
			
			const u8* imgSrc = std::data( texBin ) + texInfo.texBinRange.offset;
			u8* texBinOut = std::data( texBin ) + texInfo.texBinRange.offset;
			u64 width = texInfo.width;
			u64 height = texInfo.height;
			u64 bc5ColByteCount = GetCompressedTextureByteCount( width, height, bc5BytesPerBlock );
			texInfo.texBinRange.size = bc5ColByteCount;
			texInfo.format = TEXTURE_FORMAT_BC5_UNORM;
			CompressMetalRoughMapToBc5_SIMD( imgSrc, width, height, texBinOut );

			materials[ mi ].metallicFactor = pbrMetallicRoughness.metallic_factor;
			materials[ mi ].roughnessFactor = pbrMetallicRoughness.roughness_factor;
			materials[ mi ].normalMapIdx = texDscIdx;
		}
		if( const cgltf_texture* normalMap = mtrl.normal_texture.texture )
		{
			u64 texDscIdx = u64( normalMap - data->textures );
			image_metadata& texInfo = rawRbga8Imgs[ texDscIdx ];
			
			const u8* imgSrc = std::data( texBin ) + texInfo.texBinRange.offset;
			u8* texBinOut = std::data( texBin ) + texInfo.texBinRange.offset;
			u64 width = texInfo.width;
			u64 height = texInfo.height;
			u64 bc5ColByteCount = GetCompressedTextureByteCount( width, height, bc5BytesPerBlock );
			texInfo.texBinRange.size = bc5ColByteCount;
			texInfo.format = TEXTURE_FORMAT_BC5_UNORM;
			CompressNormalMapToBc5_SIMD( imgSrc, width, height, texBinOut );

			materials[ mi ].occRoughMetalIdx = texDscIdx;
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
	imgDescs = std::move( rawRbga8Imgs );
	mtrlDescs = std::move( materials );
}

// TODO: mesh triangulate ?
inline u64 MeshoptReindexMesh( 
	const std::span<u32>	indicesIn, 
	std::span<u32>			indicesOut, 
	std::span<vertex>		vertices 
){
	std::vector<u32> remap( std::size( vertices ) );
	u64 newVtxCount = meshopt_generateVertexRemap( std::data( remap ),
												   std::data( indicesIn ),
												   std::size( indicesIn ),
												   std::data( vertices ),
												   std::size( vertices ),
												   sizeof( vertices[ 0 ] ) );
	assert( newVtxCount <= std::size( vertices ) );
	if( newVtxCount == std::size( vertices ) ) return newVtxCount;

	meshopt_remapIndexBuffer( std::data( indicesOut ), std::data( indicesIn ), std::size( indicesIn ), std::data( remap ) );
	meshopt_remapVertexBuffer( std::data( vertices ), 
							   std::data( vertices ), 
							   std::size( vertices ), 
							   sizeof( vertices[ 0 ] ), 
							   std::data( remap ) );
	return newVtxCount;
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

// TODO: spans
inline void MeshoptOptimizeMesh(
	const vertex*	vertices,
	const u32*		indices,
	u64				vtxCount,
	u64				idxCount,
	vertex*			verticesOut,
	u32*			indicesOut
){
	meshopt_optimizeVertexCache( indicesOut, indices, idxCount, vtxCount );
	meshopt_optimizeOverdraw( indicesOut, indices, idxCount, &vertices[ 0 ].px, vtxCount, sizeof( vertices[ 0 ] ), 1.05f );
	meshopt_optimizeVertexFetch( verticesOut, indices, idxCount, vertices, vtxCount, sizeof( vertices[ 0 ] ) );
}

// TODO: no indicesOffset
inline void MeshoptMakeMeshLods(
	const std::span<vertex> verticesView,
	const std::span<u32> indicesView,
	u64				indicesOutOffset,
	u32*			indicesOut,
	std::vector<mesh_lod>& outMeshLods
){
	constexpr float ERROR_THRESHOLD = 1e-2f;
	constexpr float reductionFactor = 0.85f;

	std::vector<mesh_lod> meshLods( std::size( outMeshLods ) );
	std::memcpy( indicesOut + indicesOutOffset, std::data( indicesView ), std::size( indicesView ) * sizeof( indicesView[ 0 ] ) );
	meshLods[ 0 ].indexCount = std::size( indicesView );
	meshLods[ 0 ].indexOffset = indicesOutOffset;

	u64 meshLodsCount = 1;
	for( ; meshLodsCount < std::size( meshLods ); ++meshLodsCount )
	{
		const mesh_lod& prevLod = meshLods[ meshLodsCount - 1 ];
		const u32* prevIndices = indicesOut + prevLod.indexOffset;
		u32* nextIndices = indicesOut + prevLod.indexOffset + prevLod.indexCount;

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
	}

	meshLods.resize( meshLodsCount );
	outMeshLods = std::move( meshLods );
}
// TODO: world handedness
// TODO: remove mtlIndex from vertex
// TODO: use u16 idx
// TODO: quantize pos + uvs
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
			// NOTE: for simplicitly we do it in here
			firstVertex[ i ].mi = m.mtlIdx;
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
		for( u64 i = 0; i < m.idxRange.size; ++i ) indices[ m.idxRange.offset + i ] += vtxOffset;
		
		// NOTE: optimize and lod
		constexpr u64 lodMaxCount = 4;
		u64 idxOffset = std::size( indices );
		indices.resize( idxOffset + m.idxRange.size * lodMaxCount );
		MeshoptReindexMesh( { const_cast<u32*>( std::data( importedIndices ) + m.idxRange.offset ), m.idxRange.offset },
							{ std::data( indices ) + idxOffset, m.idxRange.offset },
							{ std::data( vertices ) + vtxOffset,vtxAttrCount } );
		MeshoptOptimizeMesh( firstVertex,
							 std::data( indices ) + idxOffset,
							 vtxAttrCount,
							 m.idxRange.size,
							 firstVertex,
							 std::data( indices ) + idxOffset );

		
		std::vector<mesh_lod> meshLods( lodMaxCount );
		MeshoptMakeMeshLods( { std::data( vertices ) + vtxOffset,vtxAttrCount },
							 { const_cast<u32*>( std::data( importedIndices ) + m.idxRange.offset ), m.idxRange.offset },
							 idxOffset,
							 std::data( indices ),
							 meshLods );

		meshDescs.push_back( {} );
		binary_mesh_desc& mesh = meshDescs[ std::size( meshDescs ) - 1 ];
		mesh.vtxRange = { vtxOffset, u32( std::size( vertices ) - vtxOffset ) };
		mesh.lodCount = std::size( meshLods );
		for( u64 l = 0; l < std::size( meshLods ); ++l )
		{
			mesh.lodRanges[ l ].offset = meshLods[ l ].indexOffset;
			mesh.lodRanges[ l ].size = meshLods[ l ].indexCount;
		}
		mesh.aabbMinMax[ 0 ] = m.aabbMin[ 0 ];
		mesh.aabbMinMax[ 1 ] = m.aabbMin[ 1 ];
		mesh.aabbMinMax[ 2 ] = m.aabbMin[ 2 ];
		mesh.aabbMinMax[ 3 ] = m.aabbMax[ 0 ];
		mesh.aabbMinMax[ 4 ] = m.aabbMax[ 1 ];
		mesh.aabbMinMax[ 5 ] = m.aabbMax[ 2 ];
		mesh.materialIndex = m.mtlIdx;
	}
}

// TODO: impose some ordering on data descriptors
// TODO: revisit
struct drak_file_header
{
	char magik[ 4 ] = "DRK";
	u32 drakVer = 0;
	u32 contentVer = 0;
};

// TODO: better more efficient copy
static drak_file_desc CompileGlbAsset( const std::vector<u8>& glbData, std::vector<u8>& drakBinData
){
	std::vector<float> meshAttrs;
	std::vector<u32> rawIndices;
	std::vector<imported_mesh> rawMeshDescs;

	std::vector<vertex> vertices;
	std::vector<u32> indices;
	std::vector<u8> texBin;
	std::vector<binary_mesh_desc> meshDescs;
	std::vector<material_data> mtrlDescs;
	std::vector<image_metadata> imgDescs;

	LoadGlbFile( glbData, meshAttrs, rawIndices, texBin, imgDescs, mtrlDescs, rawMeshDescs );
	AssembleMeshAndOptimize( meshAttrs, rawMeshDescs, rawIndices, vertices, indices, meshDescs );

	u64 totalFileDescSize = BYTE_COUNT( meshDescs ) + BYTE_COUNT( mtrlDescs ) + BYTE_COUNT( imgDescs );
	u64 totalContentSize = BYTE_COUNT( vertices ) + BYTE_COUNT( indices ) + BYTE_COUNT( texBin );
	std::vector<u8> outData;
	outData.resize( totalFileDescSize + totalContentSize );

	u8* pOutData = std::data( outData );

	std::memcpy( pOutData, std::data( meshDescs ), BYTE_COUNT( meshDescs ) );
	pOutData += BYTE_COUNT( meshDescs );
	std::memcpy( pOutData, std::data( mtrlDescs ), BYTE_COUNT( mtrlDescs ) );
	pOutData += BYTE_COUNT( mtrlDescs );
	std::memcpy( pOutData, std::data( imgDescs ), BYTE_COUNT( imgDescs ) );
	pOutData += BYTE_COUNT( imgDescs );
	std::memcpy( pOutData, std::data( vertices ), BYTE_COUNT( vertices ) );
	pOutData += BYTE_COUNT( vertices );
	std::memcpy( pOutData, std::data( indices ), BYTE_COUNT( indices ) );
	pOutData += BYTE_COUNT( indices );
	std::memcpy( pOutData, std::data( texBin ), BYTE_COUNT( texBin ) );

	drakBinData = std::move( outData );
	drak_file_desc fileDesc = {};
	fileDesc.compressedSize = totalContentSize;
	fileDesc.originalSize = totalContentSize;
	fileDesc.meshesCount = std::size( meshDescs );
	fileDesc.mtrlsCount = std::size( mtrlDescs );
	fileDesc.texCount = std::size( imgDescs );

	return fileDesc;
}

// TODO: mem efficient
// TODO: add compression
inline void SaveCompressToBinaryFile(
	const char*								filename

)
{
	
}