#include <meshoptimizer.h>

#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;

#include <atomic>
#include <thread>

#include <span>
#include <ranges>
#include <format>

#include <ankerl/unordered_dense.h>

#include <dds.h>

#include "ht_core_types.h"
#include "ht_error.h"

#include "zip_pack.h"


#include "ht_gfx_types.h"
#include "hell_pack.h"
#include "ht_math.h"

#include "hp_encoding.h"
#include "hp_bcn_compression.h"
#include "hp_serialization.h"
#include "mikkt_space.h"

#include "gltf_loader.h"

#include "hp_types_internal.h"

template<typename TriIdx, typename PrimIdx>
inline auto PermuteTrianglesByPrimitiveRemap( const std::vector<TriIdx>& oldIdx, const std::vector<PrimIdx>& primitiveIndices )
{
	u64 triangleCount = std::size( primitiveIndices );
	HP_ASSERT( ( triangleCount * 3 ) == std::size( oldIdx ) );

	std::vector<TriIdx> newIdx( std::size( oldIdx ) );
	for( u64 ti = 0; ti < triangleCount; ++ti )
	{
		u64 oldTi = primitiveIndices[ ti ];
		u64 src = 3ull * oldTi;
		u64 dst = 3ull * ti;

		newIdx[ dst + 0 ] = oldIdx[ src + 0 ];
		newIdx[ dst + 1 ] = oldIdx[ src + 1 ];
		newIdx[ dst + 2 ] = oldIdx[ src + 2 ];
	}

	return newIdx;
}

template<typename Idx>
inline auto BuildVertexRemapFromPermutedIndices( const std::vector<Idx>& permutedIndices, u64 vtxCount )
{
	constexpr Idx invalidIdx = Idx{ INVALID_IDX };

	HP_ASSERT( invalidIdx >= vtxCount );

	std::vector<Idx> remap( vtxCount, invalidIdx );
	u32 next = 0;

	for( Idx idx : permutedIndices )
	{
		Idx oldV = idx;
		if( invalidIdx == remap[ oldV ] )
		{
			remap[ oldV ] = next++;
		}
	}

	return remap;
}

raw_mesh ValidateAndNormalizeRawMesh( const raw_mesh& inRawMesh )
{
	HT_ASSERT( std::size( inRawMesh.indices ) != 0 );
	HT_ASSERT( ( std::size( inRawMesh.indices ) % 3 ) == 0 );
	auto it = std::ranges::max_element( inRawMesh.indices );
	HT_ASSERT( *it <= u16( -1 ) );
	HT_ASSERT( inRawMesh.materialIdx <= i32( u16( -1 ) ) );

	raw_mesh outRawMesh = {
		.name = std::move( inRawMesh.name ),
		.pos = std::move( inRawMesh.pos ),
		.normals = std::move( inRawMesh.normals ),
		.tans = std::move( inRawMesh.tans ),
		.uvs = std::move( inRawMesh.uvs ),
		.indices = std::move( inRawMesh.indices ),
		.materialIdx = inRawMesh.materialIdx
	};

	if( !std::size( outRawMesh.tans ) )
	{
		outRawMesh.tans = ComputeMikkTSpaceTangentsInplace( inRawMesh );
	}

	return outRawMesh;
}

struct meshlet_config
{
	float   coneWeight = 0.8f;
	u16		maxVertices = 64;
	u16		maxTriangles = 128;
};

struct rt_cluster_config
{
	float   fillWeight = 0.5f;
	u16		maxVertices = 64;
	u16		minTriangles = 16;
	u16		maxTriangles = 64;
};

// TODO: no inplace remap !
void ReindexAndOptimizeMesh( raw_mesh& rawMesh )
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

struct __meshopt_meshlets
{
	std::vector<meshopt_Meshlet> info;
	std::vector<u32> vertices;
	std::vector<u8> triangles;
};

__meshopt_meshlets MeshoptMakeClusters( 
	std::span<const float3> pos, 
	std::span<const u32> indices, 
	meshlet_config cfg 
) { 
	const u64 indexCount = std::size( indices );
	
	const u64 maxMeshletCount = meshopt_buildMeshletsBound( indexCount, cfg.maxVertices, cfg.maxTriangles );
	std::vector<meshopt_Meshlet> meshlets( maxMeshletCount );
	std::vector<u32> mletVtx( indexCount );
	std::vector<u8> mletTris( indexCount );

	u64 meshletCount = meshopt_buildMeshlets(
		&meshlets[ 0 ], &mletVtx[ 0 ], &mletTris[ 0 ], &indices[ 0 ], std::size( indices ),
		&pos[ 0 ].x, std::size( pos ), sizeof( pos[ 0 ] ), cfg.maxVertices, cfg.maxTriangles, cfg.coneWeight );

	const meshopt_Meshlet& last = meshlets[ meshletCount - 1 ];

	meshlets.resize( meshletCount );
	mletVtx.resize( ( u64 ) last.vertex_offset + last.vertex_count );
	mletTris.resize( ( u64 ) last.triangle_offset + ( ( ( u64 ) last.triangle_count * 3 + 3 ) & ~3 ) );

	for( const meshopt_Meshlet& m : meshlets )
	{
		meshopt_optimizeMeshlet( &mletVtx[ m.vertex_offset ], &mletTris[ m.triangle_offset ],
								 m.triangle_count, m.vertex_count );
	}

	return { 
		.info = std::move( meshlets ), 
		.vertices = std::move( mletVtx ), 
		.triangles = std::move( mletTris ) 
	};
}

__meshopt_meshlets MeshoptMakeClusters( 
	std::span<const float3> pos, 
	std::span<const u32> indices, 
	rt_cluster_config cfg 
) { 
	const u64 indexCount = std::size( indices );

	// NOTE( meshoptimizer ): use minTriangles to compute worst case bound
	const u64 maxMeshletCount = meshopt_buildMeshletsBound( indexCount, cfg.maxVertices, cfg.minTriangles );
	std::vector<meshopt_Meshlet> meshlets( maxMeshletCount );
	std::vector<u32> mletVtx( indexCount );
	std::vector<u8> mletTris( indexCount );

	u64 meshletCount = meshopt_buildMeshletsSpatial(
		&meshlets[ 0 ], &mletVtx[ 0 ], &mletTris[ 0 ], &indices[ 0 ], std::size( indices ), &pos[ 0 ].x, 
		std::size( pos ), sizeof( pos[ 0 ] ), cfg.maxVertices, cfg.minTriangles, cfg.maxTriangles, cfg.fillWeight );

	const meshopt_Meshlet& last = meshlets[ meshletCount - 1 ];

	meshlets.resize( meshletCount );
	mletVtx.resize( ( u64 ) last.vertex_offset + last.vertex_count );
	mletTris.resize( ( u64 ) last.triangle_offset + ( ( ( u64 ) last.triangle_count * 3 + 3 ) & ~3 ) );

	for( const meshopt_Meshlet& m : meshlets )
	{
		meshopt_optimizeMeshlet( &mletVtx[ m.vertex_offset ], &mletTris[ m.triangle_offset ],
			m.triangle_count, m.vertex_count );
	}

	return { 
		.info = std::move( meshlets ), 
		.vertices = std::move( mletVtx ), 
		.triangles = std::move( mletTris ) 
	};
}

template<typename T> 
inline auto GetMeshletLocalAttrStream( 
	std::span<const T> meshAttrStream, 
	std::span<const u32> mletVtx, 
	u64 mletVtxOffset, 
	u64 mletVtxCount 
){
	std::vector<T> localStream( mletVtxCount );
	for( u64 vi = 0; vi < std::size( localStream ); ++vi )
	{
		localStream[ vi ] = meshAttrStream[ mletVtx[ vi + mletVtxOffset ] ];
	}

	return localStream;
}

mesh_asset HpkMakeMeshAssetFromMeshlets( const raw_mesh& rawMesh )
{
	__meshopt_meshlets meshoptMeshlets = MeshoptMakeClusters( rawMesh.pos, rawMesh.indices, meshlet_config{} );

	std::span<const float3> pos = rawMesh.pos;
	std::span<const float3> norm = rawMesh.normals;
	std::span<const float4> tan = rawMesh.tans;
	std::span<const float2> uvs = rawMesh.uvs;

	std::vector<packed_vtx> vertices;
	std::vector<u8> triangles;
	std::vector<meshlet> meshlets;

	vertices.reserve( std::size( meshoptMeshlets.vertices ) );
	triangles.reserve( std::size( meshoptMeshlets.triangles ) );
	meshlets.reserve( std::size( meshoptMeshlets.info ) );

	for( const meshopt_Meshlet& m : meshoptMeshlets.info )
	{
		std::vector<float3> mletPosStream = GetMeshletLocalAttrStream( pos, meshoptMeshlets.vertices, m.vertex_offset, m.vertex_count );

		auto mletNormStream = GetMeshletLocalAttrStream( norm, meshoptMeshlets.vertices, m.vertex_offset, m.vertex_count );
		auto mletTanStream = GetMeshletLocalAttrStream( tan, meshoptMeshlets.vertices, m.vertex_offset, m.vertex_count );
		auto mletUvStream = GetMeshletLocalAttrStream( uvs, meshoptMeshlets.vertices, m.vertex_offset, m.vertex_count );

		std::vector<packed_vtx> packedVtx( std::size( mletPosStream ) );
		for( u64 vi = 0; vi < m.vertex_count; ++vi )
		{
			float3 p = mletPosStream[ vi ];
			float3 n = mletNormStream[ vi ];
			float4 t = mletTanStream[ vi ];
			float2 uv = mletUvStream[ vi ];
			float2 octNormal = OctaNormalEncode( n );
			float tanAngle = EncodeTanToAngle( n, { t.x,t.y,t.z } );
			u8 tanSign = ( -1.0f == t.w ) ? 1 : 0;

			packedVtx[ vi ] = {
				.pos = p,
				.octNormal = octNormal, 
				.tanAngle = tanAngle, 
				.u = uv.x, .v = uv.y, 
				.tanSign = tanSign 
			};
		}

		const aabb_t<float3> aabb = ComputeAabb( mletPosStream );

		HT_ASSERT( ( m.vertex_count < u32( u8( -1 ) ) ) && ( m.triangle_count < u32( u8( -1 ) ) ) );

		meshlet outMeshlet = {
			.aabbMin = aabb.min,
			.aabbMax = aabb.max,
			.vtxOffset = ( u32 ) std::size( vertices ),
			.triOffset = ( u32 ) std::size( triangles ),
			.vtxCount = ( u8 ) m.vertex_count,
			.triCount = ( u8 ) m.triangle_count
		};
		meshlets.push_back( outMeshlet );

		auto meshletTris = std::span<const u8>{ meshoptMeshlets.triangles }.subspan( m.triangle_offset, m.triangle_count );

		std::ranges::copy( packedVtx, std::back_inserter( vertices ) );
		std::ranges::copy( meshletTris, std::back_inserter( triangles ) );
	}

	auto aabbView = meshlets | std::views::transform( 
		[] ( const meshlet& m ) { return aabb_t<float3>{ .min = m.aabbMin, .max = m.aabbMax }; } );

	aabb_t<float3> aabb = MergeAabbs( aabbView );

	return {
		.vertices = std::move( vertices ),
		.triangles = std::move( triangles ),
		.meshlets = std::move( meshlets ),
		.aabb = { aabb.min, aabb.max }
	};
}

struct rt_cluster
{
	std::vector<float3> positions;
	std::vector<vertex_attrs> packedAttrs;
	std::vector<u8> triangles;
	float3	aabbMin;
	float3	aabbMax;
};

std::vector<rt_cluster> PackClustersRaytracing( const raw_mesh& rawMesh )
{
	__meshopt_meshlets meshlets = MeshoptMakeClusters( rawMesh.pos, rawMesh.indices, rt_cluster_config{} );

	std::span<const float3> pos = rawMesh.pos;
	std::span<const float3> norm = rawMesh.normals;
	std::span<const float4> tan = rawMesh.tans;
	std::span<const float2> uvs = rawMesh.uvs;

	std::vector<rt_cluster> packedMeshlets;
	packedMeshlets.reserve( std::size( meshlets.info ) );

	for( const meshopt_Meshlet& m : meshlets.info )
	{
		auto firstTriangleIt = std::cbegin( meshlets.triangles ) + m.triangle_offset;
		std::vector<u8> triangles = { firstTriangleIt, firstTriangleIt + m.triangle_count };

		std::vector<float3> mletPosStream = GetMeshletLocalAttrStream( pos, meshlets.vertices, m.vertex_offset, m.vertex_count );

		const aabb_t<float3> aabb = ComputeAabb( mletPosStream );

		auto mletNormStream = GetMeshletLocalAttrStream( norm, meshlets.vertices, m.vertex_offset, m.vertex_count );
		auto mletTanStream = GetMeshletLocalAttrStream( tan, meshlets.vertices, m.vertex_offset, m.vertex_count );
		auto mletUvStream = GetMeshletLocalAttrStream( uvs, meshlets.vertices, m.vertex_offset, m.vertex_count );

		std::vector<vertex_attrs> packedAttrs( std::size( mletNormStream ) );
		for( u64 vi = 0; vi < m.vertex_count; ++vi )
		{
			float3 n = mletNormStream[ vi ];
			float4 t = mletTanStream[ vi ];
			float2 uv = mletUvStream[ vi ];
			float2 octNormal = OctaNormalEncode( n );
			float tanAngle = EncodeTanToAngle( n, { t.x,t.y,t.z } );
			u8 tanSign = ( -1.0f == t.w ) ? 1 : 0;

			packedAttrs[ vi ] = {
				.octNormal = octNormal, 
				.tanAngle = tanAngle, 
				.u = uv.x, .v = uv.y, 
				.tanSign = tanSign 
			};
		}

		rt_cluster rtMeshlet = {
			.positions = std::move( mletPosStream ), 
			.packedAttrs = std::move( packedAttrs ), 
			.triangles = std::move( triangles ),
			.aabbMin = aabb.min, 
			.aabbMax = aabb.max 
		};
		packedMeshlets.push_back( std::move( rtMeshlet ) );
	}

	return packedMeshlets;
}

std::vector<vertex_attrs> PackVertexAttributes( 
	std::span<const float3> normals, 
	std::span<const float4> tans, 
	std::span<const float2> uvs 
) {
	u64 vtxCount = std::size( normals );

	HT_ASSERT( ( vtxCount == std::size( tans ) ) && ( vtxCount == std::size( uvs ) ) );

	std::vector<vertex_attrs> packedAttrs;
	packedAttrs.resize( vtxCount );

	for( u64 vi = 0; vi < vtxCount; ++vi )
	{
		float3 n = normals[ vi ];
		float4 t = tans[ vi ];
		float2 uv = uvs[ vi ];
		float2 octNormal = OctaNormalEncode( n );
		float tanAngle = EncodeTanToAngle( n, { t.x,t.y,t.z } );
		u8 tanSign = ( -1.0f == t.w ) ? 1 : 0;

		packedAttrs[ vi ] = {
			.octNormal = octNormal, 
			.tanAngle = tanAngle, 
			.u = uv.x, .v = uv.y, 
			.tanSign = tanSign 
		};
	}

	return packedAttrs;
}

using position_t = float3;

using dds_texture = std::vector<u8>;

constexpr bc_format_t DxgiToBcFormat( dds::DXGI_FORMAT dxgiFmt )
{
	using namespace dds;
	switch( dxgiFmt )
	{
	case DXGI_FORMAT_BC5_TYPELESS:
	case DXGI_FORMAT_BC5_UNORM:
	case DXGI_FORMAT_BC5_SNORM:
		return bc_format_t::BC5_RG;

	case DXGI_FORMAT_BC7_TYPELESS:
	case DXGI_FORMAT_BC7_UNORM:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		return bc_format_t::BC7_RGBA;

	default:
		HT_ASSERT( 0 && "Unimplement fmt" );
		return ( bc_format_t ) 0xFF;
	}
}

struct compression_job
{
	alignas( 8 ) vfs_path filename;
	dds_texture tex;
	std::span<const u8> src;
	dds::DXGI_FORMAT fmt;
	u16 width;
	u16 height;

	void Execute()
	{
		bc_format_t bcnFmt = DxgiToBcFormat( fmt );
		// NOTE: these allocate memory !
		bcn_compression_result bcn = CompressRGBA8ToBCn( src, width, height, bcnFmt );

		tex.resize( sizeof( dds::Header ) + std::size( bcn.data ) );
		dds::write_header( &tex[ 0 ], fmt, width, height );
		std::memcpy( &tex[ 0 ] + sizeof( dds::Header ), &bcn.data[ 0 ], std::size( bcn.data ) );
	}
};

struct materials_jobs
{
	std::vector<material_desc>   materials;
	std::vector<compression_job> jobs;
};

materials_jobs PrepareBcnCompressionBatch( std::span<const raw_material_info> rawMaterials, std::span<const raw_image_view> imageViews )
{
	HT_ASSERT( std::size( imageViews ) < u16( INVALID_IDX ) );

	// NOTE: we use indices and vfs_path here bc we're deduping wrt to tinygltf's stuff which is index based
	ankerl::unordered_dense::set<u16> jobsSet;
	std::vector<compression_job> jobs;

	jobsSet.reserve( std::size( imageViews ) );
	jobs.reserve( std::size( imageViews ) );

	auto ProcessImageView = [ & ]( u16 idx, dds::DXGI_FORMAT fmt, const vfs_path& filename ) -> u64
	{
		if( !IsIndexValid( idx ) ) return {};
		if( std::cend( jobsSet ) == jobsSet.find( idx ) )
		{
			const raw_image_view& imgView = imageViews[ idx ];
			HT_ASSERT( std::size( imgView.data ) );

			jobsSet.emplace( idx );

			jobs.push_back( {
				.filename = filename,
				.src = imgView.data, 
				.fmt = fmt, 
				.width = imgView.metadata.width, 
				.height = imgView.metadata.height 
			} );
		}
		
		return std::hash<vfs_path>{}( filename );
	};

	std::vector<material_desc> materials;
	materials.reserve( std::size( rawMaterials ) );
	// NOTE: GLTF conventions
	for( const raw_material_info& mtrl : rawMaterials )
	{
		//ProcessImageView( material.occlusionIdx, bc_format_t::BC7_RGBA );
		// NOTE: currently not suporting ambient occlusion which must be packed into MR
		HT_ASSERT( !IsIndexValid( mtrl.occlusionIdx ) );

		u64 baseColorHash = ProcessImageView( mtrl.baseColorIdx, dds::DXGI_FORMAT_BC7_UNORM_SRGB, { mtrl.name + "_albedo.dds" } );
		u64 metallicRoughnessHash = ProcessImageView( mtrl.normalIdx, dds::DXGI_FORMAT_BC5_UNORM, { mtrl.name + "_normal.dds" } );
		u64 normalHash = ProcessImageView( mtrl.metallicRoughnessIdx, dds::DXGI_FORMAT_BC7_UNORM, { mtrl.name + "_mro.dds" } );
		u64 emissiveHash = ProcessImageView( mtrl.emissiveIdx, dds::DXGI_FORMAT_BC7_UNORM_SRGB, { mtrl.name + "_emissive.dds" } );

		materials.push_back( {
			.baseColorHash = baseColorHash,
			.metallicRoughnessHash = metallicRoughnessHash,
			.normalHash = normalHash,
			.emissiveHash = emissiveHash,

			.baseColFactor = mtrl.baseColFactor,
			.emissiveFactor = mtrl.emissiveFactor,
			.metallicFactor = mtrl.metallicFactor,
			.roughnessFactor = mtrl.metallicFactor,

			.alphaCutoff = mtrl.alphaCutoff,

			.samplerIdx = mtrl.samplerIdx,

			.alphaMode = mtrl.alphaMode
		} );
	}

	return { .materials = std::move( materials ), .jobs = std::move( jobs ) };
}

inline void WaitThreadPoolDone( std::vector<std::thread>& threadPool )
{
	for( auto& t : threadPool ) t.join();
}

inline void WriteFileBinary( const char* path, std::span<const u8> bytes )
{
	FILE* f = nullptr;
	HT_ASSERT( ::fopen_s( &f, path, "wb" ) == 0 );

	u64 written = ::fwrite( std::data( bytes ), 1, std::size( bytes ), f );
	HT_ASSERT( std::size( bytes ) == written );

	i32 rc = ::fclose( f );
	HT_ASSERT( rc == 0 );
}

inline std::vector<u8> ReadFileBinary( const char* path )
{
	FILE* f = nullptr;
	HT_ASSERT( ::fopen_s( &f, path, "rb" ) == 0 );
	HT_ASSERT( f );

	HT_ASSERT( ::fseek( f, 0, SEEK_END ) == 0 );
	i32 sz = ::ftell( f );
	HT_ASSERT( sz >= 0 );
	HT_ASSERT( ::fseek( f, 0, SEEK_SET ) == 0 );

	std::vector<u8> out( sz );
	u64 read = ::fread( std::data( out ), 1, std::size( out ), f );
	HT_ASSERT( std::size( out ) == read );

	HT_ASSERT( ::fclose( f ) == 0 );
	return out;
}

constexpr bool CHECK_CORRECTNESS = true;

int main()
{
	const std::string gltfFilePath = "D:/3d models/Nightclub Futuristic/nightclub_futuristic_pub_ambience_asset.glb";

	HT_ASSERT( fs::exists( gltfFilePath ) );

	gltf_loader gltf = { gltfFilePath };

	// TODO: ensure we keep the same indexing as tinygltf provides !!!!
	std::vector<raw_node> rawNodes = gltf.ProcessNodes();
	std::vector<raw_mesh> rawMeshes = gltf.ProcessMeshes();
	std::vector<sampler_config> samplers = gltf.ProcessSamplers();
	std::vector<raw_material_info> rawMaterials = gltf.ProcessMaterials();
	std::vector<raw_image_view> imageViews = gltf.ProcessImages();

	auto[ materialTable, texCmpJobs ] = PrepareBcnCompressionBatch( rawMaterials, imageViews );

	std::vector<std::thread> tasks;
	std::atomic<u32> taskCounter = { 0 };

	auto WorkerLoop = [ & ]()
	{
		for( ;; )
		{
			u32 currentJobIdx = taskCounter.fetch_add( 1 );
			if( currentJobIdx >= std::size( texCmpJobs ) ) return;

			texCmpJobs[ currentJobIdx ].Execute();
		}
	};

	for( u64 ti = 0; ti < std::thread::hardware_concurrency(); ++ti ) tasks.emplace_back( WorkerLoop );

	std::vector<world_node> instances;

	ankerl::unordered_dense::map<vfs_path, mesh_asset> meshAssetMap;
	for( const raw_node& n : rawNodes )
	{
		if( !IsIndexValid( n.meshIdx ) ) continue;

		const raw_mesh& mesh = rawMeshes[ n.meshIdx ];

		vfs_path assetPath = { "{}{}.mesh", HELLPACK_MESH_DIR, std::data( mesh.name ) };

		// NOTE: it moves stuff
		raw_mesh validatedRawMesh = ValidateAndNormalizeRawMesh( mesh );
		mesh_asset meshAsset = HpkMakeMeshAssetFromMeshlets( validatedRawMesh );
		
		meshAssetMap.emplace( assetPath, std::move( meshAsset ) );

		instances.push_back( {
			.toWorld = n.toWorld,
			.meshHash = std::hash<vfs_path>{}( assetPath ),
			.materialIdx = ( u16 ) mesh.materialIdx // NOTE: these should match 1:1 with ours
		} );
	}

	const std::string hpkFilePath = "D:/3d models/Nightclub Futuristic/nightclub_futuristic_pub_ambience_asset.hpk";
	{
		zip_writer zipArchive = { hpkFilePath.c_str() };

		HT_ASSERT( fs::exists( hpkFilePath ) );

		{
			hellpack_serializble_buffer buffs[] = { instances, materialTable };
			std::vector<u8> bytes = HpkMakeBinaryBlob( buffs, hellpack_entry_t::LEVEL );
			zipArchive.WriteBytesToFile( { "world.lvl" }, bytes );
		}
		{
			for( auto& [ filePath, meshAsset ] : meshAssetMap )
			{
				hellpack_serializble_buffer buffs[] = { 
					meshAsset.vertices, meshAsset.triangles, meshAsset.meshlets, meshAsset.aabb };
				std::vector<u8> bytes = HpkMakeBinaryBlob( buffs, hellpack_entry_t::MESH );
				zipArchive.WriteBytesToFile( filePath, bytes );
			}
		}
		WaitThreadPoolDone( tasks );

		for( const compression_job& cmp : texCmpJobs )
		{
			HT_ASSERT( std::size( cmp.tex ) );
			vfs_path texPath = { "{}{}", HELLPACK_TEX_DIR, std::data( cmp.filename ) };
			zipArchive.WriteBytesToFile( texPath, cmp.tex );
		}
	}

	/*
	if constexpr( CHECK_CORRECTNESS )
	{
		const std::vector<u8> rawBytes = ReadFileBinary( hpkFilePath.c_str() );

		vfs_zip_mem vfsZipMem = { rawBytes };

		const auto& [ key, val ] = *std::cbegin( meshAssetMap );

		std::vector<u8> mesh0Bin( vfsZipMem.GetFileSizeInBytes( key ), 0 );
		HT_ASSERT( vfsZipMem.ReadFileToBufferNoAlloc( key, std::data( mesh0Bin ), std::size( mesh0Bin ) ) );

		const hellpack_mesh_asset hpkMeshAsset = HpkReadBinaryBlob<hellpack_mesh_asset>( mesh0Bin );

		HT_ASSERT( ByteEqual( MakeByteView( hpkMeshAsset.vertices ), MakeByteView( val.vertices ) ) );
		HT_ASSERT( ByteEqual( MakeByteView( hpkMeshAsset.triangles ), MakeByteView( val.triangles ) ) );
		HT_ASSERT( ByteEqual( MakeByteView( hpkMeshAsset.meshlets ), MakeByteView( val.meshlets ) ) );

		HT_ASSERT( hpkMeshAsset.aabbMin == val.aabb[ 0 ] );
		HT_ASSERT( hpkMeshAsset.aabbMax == val.aabb[ 1 ] );
	}
	*/
	return 0;
}

