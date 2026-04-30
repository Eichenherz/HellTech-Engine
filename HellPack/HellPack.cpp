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

#include <ht_macros.h>

template<typename TriIdx, typename PrimIdx>
inline std::vector<TriIdx> PermuteTrianglesByPrimitiveRemap( const std::vector<TriIdx>& oldIdx, const std::vector<PrimIdx>& primitiveIndices )
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
inline std::vector<Idx> BuildVertexRemapFromPermutedIndices( const std::vector<Idx>& permutedIndices, u64 vtxCount )
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
	float   coneWeight		= 0.8f;
	u16		maxVertices		= 64;
	u16		maxTriangles	= 128;
};

struct rt_cluster_config
{
	float   fillWeight		= 0.5f;
	u16		maxVertices		= 64;
	u16		minTriangles	= 16;
	u16		maxTriangles	= 64;
};

// TODO: no inplace remap !
void ReindexAndOptimizeMesh( raw_mesh& rawMesh )
{
	meshopt_Stream attrStreams[] = {
		{ .data = std::data( rawMesh.pos ), .size = sizeof( rawMesh.pos[ 0 ] ), .stride = sizeof( rawMesh.pos[ 0 ] ) },
		{ .data = std::data( rawMesh.normals ), .size = sizeof( rawMesh.normals[ 0 ] ), .stride = sizeof( rawMesh.normals[ 0 ] ) },
		{ .data = std::data( rawMesh.tans ), .size = sizeof( rawMesh.tans[ 0 ] ), .stride = sizeof( rawMesh.tans[ 0 ] ) },
		{ .data = std::data( rawMesh.uvs ), .size = sizeof( rawMesh.uvs[ 0 ] ), .stride = sizeof( rawMesh.uvs[ 0 ] ) },
	};
	std::vector<u32>& indices = rawMesh.indices;

	const u64 vtxCount = std::size( rawMesh.pos );
	const u64 idxCount = std::size( indices );

	std::vector<u32> remap( vtxCount );
	u64 newVtxCount = meshopt_generateVertexRemapMulti(std::data( remap ), std::data( indices ),
		idxCount, vtxCount, attrStreams, std::size( attrStreams ) );

	HT_ASSERT( newVtxCount <= vtxCount );
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

	meshopt_optimizeVertexCache( std::data( indices ), std::data( indices ), idxCount, newVtxCount );

	std::vector<u32> fetchRemap( newVtxCount );
	meshopt_optimizeVertexFetchRemap( std::data( fetchRemap ), std::data( indices ),
		idxCount, newVtxCount );

	meshopt_remapIndexBuffer( std::data( indices ), std::data( indices ), idxCount, std::data( fetchRemap ) );


	meshopt_remapVertexBuffer( std::data( rawMesh.pos ),std::data( rawMesh.pos ), newVtxCount,
		sizeof( rawMesh.pos[ 0 ] ), std::data( fetchRemap ) );
	meshopt_remapVertexBuffer( std::data( rawMesh.normals ), std::data( rawMesh.normals ), newVtxCount,
		sizeof( rawMesh.normals[ 0 ] ), std::data( fetchRemap ) );
	meshopt_remapVertexBuffer( std::data( rawMesh.tans ), std::data( rawMesh.tans ), newVtxCount,
		sizeof( rawMesh.tans[ 0 ] ), std::data( fetchRemap ) );
	meshopt_remapVertexBuffer( std::data( rawMesh.uvs ),std::data( rawMesh.uvs ), newVtxCount,
		sizeof( rawMesh.uvs[ 0 ] ), std::data( fetchRemap ) );
}

struct __meshopt_meshlets
{
	std::vector<meshopt_Meshlet>	info;
	std::vector<u32> 				vertices;
	std::vector<u8>					triIndices;
};

__meshopt_meshlets MeshoptMakeClusters( 
	std::span<const float3> pos,
	std::span<const u32>	indices,
	meshlet_config			cfg
) { 
	const u64 indexCount = std::size( indices );
	
	const u64 maxMeshletCount = meshopt_buildMeshletsBound( indexCount, cfg.maxVertices, cfg.maxTriangles );
	std::vector<meshopt_Meshlet> meshlets( maxMeshletCount );
	std::vector<u32> mltVtx( indexCount );
	std::vector<u8> mltTris( indexCount );

	u64 meshletCount = meshopt_buildMeshlets( &meshlets[ 0 ], &mltVtx[ 0 ], &mltTris[ 0 ], &indices[ 0 ],
		std::size( indices ), &pos[ 0 ].x, std::size( pos ), sizeof( pos[ 0 ] ),
		cfg.maxVertices, cfg.maxTriangles, cfg.coneWeight );

	const meshopt_Meshlet& last = meshlets[ meshletCount - 1 ];

	meshlets.resize( meshletCount );
	mltVtx.resize( ( u64 ) last.vertex_offset + last.vertex_count );
	mltTris.resize( ( u64 ) last.triangle_offset + ( u64 ) last.triangle_count * 3 );

	for( const meshopt_Meshlet& m : meshlets )
	{
		meshopt_optimizeMeshlet( &mltVtx[ m.vertex_offset ], &mltTris[ m.triangle_offset ], m.triangle_count, m.vertex_count );
	}

	return {  .info = MOV( meshlets ), .vertices = MOV( mltVtx ), .triIndices = MOV( mltTris ) };
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
		.info		= MOV( meshlets ),
		.vertices	= MOV( mletVtx ),
		.triIndices	= MOV( mletTris )
	};
}

template<typename T> 
inline std::vector<T> GetMeshletLocalAttrStream(
	std::span<const T>		meshAttrStream,
	std::span<const u32>	mltVtx,
	u64						mltVtxOffset,
	u64						mltVtxCount
){
	std::vector<T> localStream( mltVtxCount );
	for( u64 vi = 0; vi < std::size( localStream ); ++vi )
	{
		localStream[ vi ] = meshAttrStream[ mltVtx[ vi + mltVtxOffset ] ];
	}

	return localStream;
}

mesh_asset HpkMakeMeshAssetFromMeshlets( const raw_mesh& rawMesh )
{
	meshlet_config mltCfg = {};
	__meshopt_meshlets meshoptMeshlets = MeshoptMakeClusters( rawMesh.pos, rawMesh.indices, mltCfg );

	std::span<const float3> pos		= rawMesh.pos;
	std::span<const float3> norm	= rawMesh.normals;
	std::span<const float4> tan		= rawMesh.tans;
	std::span<const float2> uvs		= rawMesh.uvs;

	std::vector<packed_vtx>		vertices;
	std::vector<u8>				triIndices;
	std::vector<gpu_meshlet>	meshlets;

	vertices.reserve( std::size( meshoptMeshlets.vertices ) );
	triIndices.reserve( std::size( meshoptMeshlets.triIndices ) );
	meshlets.reserve( std::size( meshoptMeshlets.info ) );

	for( const meshopt_Meshlet& m : meshoptMeshlets.info )
	{
		HT_ASSERT( ( m.vertex_count <= u32( mltCfg.maxVertices ) ) && ( m.triangle_count <= u32( mltCfg.maxTriangles ) ) );

		std::vector<float3> mltPosStream = GetMeshletLocalAttrStream( pos, meshoptMeshlets.vertices, m.vertex_offset, m.vertex_count );

		std::vector<float3> mltNormStream = GetMeshletLocalAttrStream(
			norm, meshoptMeshlets.vertices, m.vertex_offset, m.vertex_count );
		std::vector<float4> mltTanStream = GetMeshletLocalAttrStream(
			tan, meshoptMeshlets.vertices, m.vertex_offset, m.vertex_count );
		std::vector<float2> mltUvStream = GetMeshletLocalAttrStream(
			uvs, meshoptMeshlets.vertices, m.vertex_offset, m.vertex_count );

		const aabb_t<float3> aabb = ComputeAabb( mltPosStream );

		std::vector<packed_vtx> packedVtx( std::size( mltPosStream ) );
		for( u64 vi = 0; vi < m.vertex_count; ++vi )
		{
			float3 p	= mltPosStream[ vi ];
			float3 n	= mltNormStream[ vi ];
			float4 t	= mltTanStream[ vi ];
			float2 uv	= mltUvStream[ vi ];
			float2 octNormal = OctaNormalEncode( n );
			float tanAngle = EncodeTanToAngle( n, { t.x,t.y,t.z } );
			u8 tanSign = ( -1.0f == t.w ) ? 1 : 0;

			packedVtx[ vi ] = {
				.px 		= p.x,
				.py 		= p.y,
				.pz 		= p.z,
				.tu 		= uv.x,
				.tv 		= uv.y,
				.octNX		= octNormal.x,
				.octNY		= octNormal.y,
				.tanAngle	= tanAngle,
				.tanSign	= tanSign
			};
		}

		gpu_meshlet outMeshlet = {
			.minAabb	= aabb.min,
			.maxAabb	= aabb.max,
			.vtxOffset	= ( u32 ) std::size( vertices ),
			.triOffset	= ( u32 ) std::size( triIndices ),
			.vtxCount	= ( u16 ) m.vertex_count,
			.triCount	= ( u16 ) m.triangle_count
		};
		meshlets.push_back( outMeshlet );

		std::span<const u8> mltTriIndices = { std::data( meshoptMeshlets.triIndices ) + m.triangle_offset,
			m.triangle_count * 3 };

		std::ranges::copy( packedVtx, std::back_inserter( vertices ) );
		std::ranges::copy( mltTriIndices, std::back_inserter( triIndices ) );
	}

	auto aabbView = meshlets | std::views::transform( 
		[] ( const gpu_meshlet& m ) { return aabb_t<float3>{ .min = m.minAabb, .max = m.maxAabb }; } );

	aabb_t<float3> aabb = MergeAabbs( aabbView );

	return {
		.vertices	= MOV( vertices ),
		.triIndices	= MOV( triIndices ),
		.meshlets	= MOV( meshlets ),
		.aabb		= { aabb.min, aabb.max }
	};
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
	alignas( 8 ) vfs_path	filename;
	dds_texture				tex;
	std::span<const u8> 	src;
	dds::DXGI_FORMAT		fmt;
	u16						width;
	u16						height;

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
	ankerl::unordered_dense::set<u16>	jobsSet;
	std::vector<compression_job>		jobs;

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
				.filename	= filename,
				.src		= imgView.data,
				.fmt		= fmt,
				.width		= imgView.metadata.width,
				.height		= imgView.metadata.height
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
		// NOTE: currently not supporting ambient occlusion which must be packed into MR
		//HT_ASSERT( !IsIndexValid( mtrl.occlusionIdx ) );

		u64 baseColorHash = ProcessImageView( mtrl.baseColorIdx, dds::DXGI_FORMAT_BC7_UNORM_SRGB, { mtrl.name + "_albedo.dds" } );
		u64 normalHash = ProcessImageView( mtrl.normalIdx, dds::DXGI_FORMAT_BC5_UNORM, { mtrl.name + "_normal.dds" } );
		u64 metallicRoughnessHash = ProcessImageView( mtrl.metallicRoughnessIdx, dds::DXGI_FORMAT_BC7_UNORM, { mtrl.name + "_mro.dds" } );
		u64 emissiveHash = ProcessImageView( mtrl.emissiveIdx, dds::DXGI_FORMAT_BC7_UNORM_SRGB, { mtrl.name + "_emissive.dds" } );

		materials.push_back( {
			.baseColorHash			= baseColorHash,
			.metallicRoughnessHash	= metallicRoughnessHash,
			.normalHash				= normalHash,
			.emissiveHash			= emissiveHash,

			.baseColFactor			= mtrl.baseColFactor,
			.emissiveFactor			= mtrl.emissiveFactor,
			.metallicFactor			= mtrl.metallicFactor,
			.roughnessFactor		= mtrl.roughnessFactor,

			.alphaCutoff			= mtrl.alphaCutoff,

			.samplerIdx				= mtrl.samplerIdx,

			.alphaMode				= mtrl.alphaMode
		} );
	}

	return { .materials = MOV( materials ), .jobs = MOV( jobs ) };
}

inline void WaitThreadPoolDone( std::vector<std::thread>& threadPool )
{
	for( auto& t : threadPool ) t.join();
}

constexpr bool CHECK_CORRECTNESS = true;

i32 main( i32 argc, char** argv  )
{
	if( argc < 3 )
	{
		std::cout << "Missing arguments\n";
		return 1;
	}

	const std::string gltfFilePath = argv[ 1 ];
	const std::string hpkFilePath = argv[ 2 ];

	HT_ASSERT( fs::exists( gltfFilePath ) );

	gltf_loader gltf = { gltfFilePath };

	// TODO: ensure we keep the same indexing as tinygltf provides !!!!
	std::vector<raw_node>			rawNodes		= gltf.ProcessNodes();
	std::vector<raw_mesh>			rawMeshes		= gltf.ProcessMeshes();
	std::vector<sampler_config>		samplers		= gltf.ProcessSamplers();
	std::vector<raw_material_info>	rawMaterials	= gltf.ProcessMaterials();
	std::vector<raw_image_view>		imageViews		= gltf.ProcessImages();

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

	std::cout << "Processing materials async\n";

	for( u64 ti = 0; ti < std::thread::hardware_concurrency(); ++ti ) tasks.emplace_back( WorkerLoop );

	std::vector<world_node> worldNodes;

	ankerl::unordered_dense::map<vfs_path, mesh_asset> meshAssetMap;

	std::cout << "Processing meshes\n";
	for( const raw_node& n : rawNodes )
	{
		if( !IsIndexValid( n.meshIdx ) ) continue;

		const raw_mesh& mesh = rawMeshes[ ( u32 ) n.meshIdx ];

		vfs_path assetPath = { "{}{}.mesh", HELLPACK_MESH_DIR, std::data( mesh.name ) };

		if( !meshAssetMap.contains( assetPath ) )
		{
			// NOTE: it moves stuff
			raw_mesh validatedRawMesh = ValidateAndNormalizeRawMesh( mesh );
			mesh_asset meshAsset = HpkMakeMeshAssetFromMeshlets( validatedRawMesh );

			meshAssetMap.emplace( assetPath, std::move( meshAsset ) );
		}

		worldNodes.push_back( {
			.toWorld		= { .t = n.toWorld.t, .pad0 = 0, .r = n.toWorld.r, .s = n.toWorld.s, .pad1 = 0 },
			.meshHash		= std::hash<vfs_path>{}( assetPath ),
			.materialIdx	= ( u16 ) mesh.materialIdx // NOTE: these should match 1:1 with ours
		} );
	}

	std::cout << "Processing meshes done ! Dumping to file.\n";

	{
		zip_writer zipArchive = { hpkFilePath.c_str() };

		HT_ASSERT( fs::exists( hpkFilePath ) );

		{
			hellpack_serializble_buffer buffs[] = { worldNodes, materialTable };
			std::vector<u8> bytes = HpkMakeBinaryBlob( buffs, hellpack_entry_t::LEVEL );
			zipArchive.WriteBytesToFile( { "world.lvl" }, bytes );
		}
		{
			for( auto& [ filePath, meshAsset ] : meshAssetMap )
			{
				hellpack_serializble_buffer buffs[] = { 
					meshAsset.vertices, meshAsset.triIndices, meshAsset.meshlets, meshAsset.aabb };
				std::vector<u8> bytes = HpkMakeBinaryBlob( buffs, hellpack_entry_t::MESH );
				zipArchive.WriteBytesToFile( filePath, bytes );
			}
		}
		WaitThreadPoolDone( tasks );

		std::cout << "Processing materials done ! Dumping to file.\n";
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

