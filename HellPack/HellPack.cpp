#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;

#include <atomic>
#include <thread>
#include <barrier>
#include <print>
#include <span>
#include <ranges>
#include <format>

#include <ankerl/unordered_dense.h>

#include <dds.h>

#include <ht_core_types.h>
#include <ht_error.h>

#include "zip_pack.h"

#include <ht_gfx_types.h>
#include <hell_pack.h>
#include <ht_serialization.h>
#include <ht_math.h>
#include <ht_ring_buffer.h>
#include <System/sys_file.h>

#include "hp_encoding.h"
#include "hp_bcn_compression.h"

#include "gltf_loader.h"

#include "hp_types_internal.h"
#include <ht_vec_types.h>

#include <ht_macros.h>

#include "hpk_meshopt_pipeline.h"

#include <libdeflate.h>


static const u64 g_ThreadCount = std::thread::hardware_concurrency();

thread_local static virtual_arena g_ThreadArena[ 2 ]    = { { 4 * GB }, { 4 * GB } };
thread_local static virtual_arena g_ThreadExtLibArena   = { 12 * GB };

inline cgltf_result
HtCgltfFileRead( const cgltf_memory_options*, const cgltf_file_options*, const char*, cgltf_size*, void** )
{
    HT_ASSERT( 0 && "glb only, no external files" );
    return cgltf_result_io_error;
}
inline void HtCgltfFileRelease( const cgltf_memory_options*, const cgltf_file_options*, void*, cgltf_size )
{
    HT_ASSERT( 0 && "glb only, no external files" );
}

inline void*    CgltfArenaAlloc( void*, cgltf_size szInBytes ) { return g_ThreadExtLibArena.Alloc( szInBytes, 16 ); }
inline void     CgltfArenaFree( void*, void* ) {}

inline void*    MeshoptScratchAlloc( size_t szInBytes ) { return g_ThreadExtLibArena.Alloc( szInBytes, 16 ); }
inline void     MeshoptScratchFree( void* ) {}

inline void*    NvGDeflateArenaAlloc( size_t szInBytes ) { return g_ThreadExtLibArena.Alloc( szInBytes, 16 ); }
inline void     NvGDeflateArenaFree( void* ) {}

struct nv_gdeflate
{
    using page_t = libdeflate_gdeflate_out_page;

    static constexpr u64 MAX_PAGE_SZ = 64 * KB;

    libdeflate_gdeflate_compressor* pCompressor = nullptr;

    nv_gdeflate( i32 compressionLevel )
    {
        pCompressor = libdeflate_alloc_gdeflate_compressor( compressionLevel );
        HT_ASSERT( pCompressor );
    }
   // NOTE: we will just rwind the arena
};

struct gdeflate_compressed
{
    std::span<u8>   packedData;
    std::span<u64>  exclusiveOffsets;
};

static gdeflate_compressed
NvGDeflateCompressInArenaMem(
    const nv_gdeflate&  nvGdef,
    std::span<const u8> rawData,
    virtual_arena&      scratchArena,
    virtual_arena&      tempArena
) {
    u64 pageCount   = 0;
    u64 totalBound  = libdeflate_gdeflate_compress_bound( nvGdef.pCompressor, std::size( rawData ), &pageCount );
    u64 pageBound   = totalBound / pageCount;
    HT_ASSERT( ( pageBound * pageCount ) == totalBound );

    std::span compressedOut = ArenaNewArray<u8>( tempArena, totalBound );
    std::span nvGDeflPages  = ArenaNewArray<nv_gdeflate::page_t>( scratchArena, pageCount );

    for( nv_gdeflate::page_t& gdPage : nvGDeflPages )
    {
        u8* pData = std::data( compressedOut );
        gdPage = {
            .data   = pData + u64( &gdPage - std::data( nvGDeflPages ) ) * pageBound,
            .nbytes = pageBound
        };
    }

    const u64 compressedSize = libdeflate_gdeflate_compress( nvGdef.pCompressor, std::data( rawData ),
        std::size( rawData ), std::data( nvGDeflPages ), std::size( nvGDeflPages ) );
    HT_ASSERT( compressedSize );

    std::span exclusiveOffsets = ArenaNewArray<u64>( tempArena, pageCount );

    u64 offsetAcc = 0;
    for( auto[ gdPage, dataOff ] : std::views::zip( nvGDeflPages, exclusiveOffsets ) )
    {
        dataOff = offsetAcc;
        std::memmove( std::data( compressedOut ) + dataOff, gdPage.data, gdPage.nbytes );
        offsetAcc += gdPage.nbytes;
    }
    HT_ASSERT( offsetAcc == compressedSize );

    // NOTE: this is theoretically a potential source of bugs bc we're leaking the totalBound - compressedSize chunk;
    // but we don't care bc we're rewinding the arena every loop
    return {
        .packedData         = compressedOut.subspan( 0, compressedSize ),
        .exclusiveOffsets   = exclusiveOffsets };
}

constexpr u32x3 CanonicallySortTriangleIndices( u32x3 t )
{
	if( t.x > t.y ) std::swap( t.x, t.y );
	if( t.y > t.z ) std::swap( t.y, t.z );
	if( t.x > t.y ) std::swap( t.x, t.y );
	return t;
}

template<typename TriIdx, typename PrimIdx>
std::vector<TriIdx> PermuteTrianglesByPrimitiveRemap(
	const std::vector<TriIdx>&	oldIdx,
	const std::vector<PrimIdx>& primitiveIndices
) {
	u64 triangleCount = std::size( primitiveIndices );
	HP_ASSERT( ( triangleCount * 3 ) == std::size( oldIdx ) );

	std::vector<TriIdx> newIdx( std::size( oldIdx ) );
	for( u64 ti = 0; ti < triangleCount; ++ti )
	{
		u64 oldTi   = primitiveIndices[ ti ];
		u64 src     = 3ull * oldTi;
		u64 dst     = 3ull * ti;

		newIdx[ dst + 0 ] = oldIdx[ src + 0 ];
		newIdx[ dst + 1 ] = oldIdx[ src + 1 ];
		newIdx[ dst + 2 ] = oldIdx[ src + 2 ];
	}

	return newIdx;
}

void GenerateSmoothNormals( std::span<const float3> pos, std::span<const u32> indices, std::span<float3> normals )
{
    HT_ASSERT( std::size( pos ) == std::size( normals ) );

    std::ranges::fill( normals, float3{} );

    for( u64 triIdx = 0; triIdx < std::size( indices ); triIdx += 3 )
    {
        u32 vtx0 = indices[ triIdx + 0 ];
        u32 vtx1 = indices[ triIdx + 1 ];
        u32 vtx2 = indices[ triIdx + 2 ];

        float3 pos0 = pos[ vtx0 ];
        float3 pos1 = pos[ vtx1 ];
        float3 pos2 = pos[ vtx2 ];

        float3 faceNormal = CrossProd( pos1 - pos0, pos2 - pos0 );
        normals[ vtx0 ] += faceNormal;
        normals[ vtx1 ] += faceNormal;
        normals[ vtx2 ] += faceNormal;
    }

    std::ranges::for_each( normals, []( float3& n ) { n = Normalize( n ); } );
}

std::vector<packed_vtx_attr> HpkMeshletPackVtxAttributes( const hpk_meshlet& mlt )
{
	std::vector<packed_vtx_attr> packedVtxAttrs( mlt.vtxCount );
	for( u64 vai = 0; vai < mlt.vtxCount; ++vai )
	{
		float3 n	= mlt.norm[ vai ];
		float4 t	= mlt.tan[ vai ];
		float2 uv	= mlt.uvs[ vai ];

		packedVtxAttrs[ vai ] = {
			.encodedTBN = EncodeTanFrame( n, { t.x, t.y, t.z }, t.w ),
			.encodedUVs = { meshopt_quantizeHalf( uv.x ), meshopt_quantizeHalf( uv.y ) }
		};
	}

	return packedVtxAttrs;
}

constexpr bool validatePosEncoding = true;
constexpr bool validateNormalEncoding = true;

void HpkQuantizeAndAppendLODLevel(
	std::span<const hpk_meshlet>	meshoptMeshlets,
	bit_stream&						vtxPosBitstream,
	borrowed_array<oct16x2>&	    vtxNormals,
	borrowed_array<u8>&				indices,
	borrowed_array<gpu_meshlet>&    meshlets
) {
	for( const hpk_meshlet& m : meshoptMeshlets )
	{
		aabb_t<float3> meshletAabb = ComputeAabb( m.pos );
        // TODO: why this happens ? and can't we not prevent it ?
	    if( float3{} == ( meshletAabb.max - meshletAabb.min ) ) continue;

		mlt_quantized_grid mltEncodingGrid = HpkMakeMltQuantizedGrid( meshletAabb );

		u32 packed8888_XYZ_Grid_BitDepth = mltEncodingGrid.bitDepthPerAxis.x
			| ( mltEncodingGrid.bitDepthPerAxis.y << 8 )
			| ( mltEncodingGrid.bitDepthPerAxis.z << 16 )
			| ( mltEncodingGrid.gridResInBits << 24 );

		u32 packed8_12_12_VtxCount_Lod_01_IdxCount = u32( m.vtxCount )
			| ( u32( std::size( m.indices ) ) << 8 )
			| ( u32( std::size( m.idxLod ) ) << 20 );

		HT_ASSERT( ( m.vtxCount < 256 ) &&
			( std::size( m.idxLod ) < RASTER_MLT_MAX_INDEX ) &&
			( std::size( m.indices ) < RASTER_MLT_MAX_INDEX ) );

		meshlets.push_back( {
			.aabbMin								= mltEncodingGrid.quantAabbMin,
			.aabbMax								= mltEncodingGrid.quantAabbMax,
			.vtxPosOffsetBits						= ( u32 ) vtxPosBitstream.cursorInBits,
			.vtxAttrsOffset							= ( u32 ) std::size( vtxNormals ),
			.idxOffset								= ( u32 ) std::size( indices ),
			.packed8888_XYZ_Grid_BitDepth			= packed8888_XYZ_Grid_BitDepth,
			.packed8_12_12_VtxCount_Lod_01_IdxCount	= packed8_12_12_VtxCount_Lod_01_IdxCount,
			.lodError								= m.lodError
		} );

		for( float3 p : m.pos )
		{
			u32x3 enc = HpkEncodeMltVertexPosition( mltEncodingGrid, p );

			vtxPosBitstream.AppendBits( enc.x, mltEncodingGrid.bitDepthPerAxis.x );
			vtxPosBitstream.AppendBits( enc.y, mltEncodingGrid.bitDepthPerAxis.y );
			vtxPosBitstream.AppendBits( enc.z, mltEncodingGrid.bitDepthPerAxis.z );

			//if constexpr( validatePosEncoding ) HT_ASSERT( HpkDecodeVerifyQuantized( p, enc, mltEncodingGrid ) );
		}

	    for( float3 n : m.norm )
	    {
	        float2  octN        = EncodeOctaNormal( n );
	        oct16x2 encNormal   = SnormBits<16>( octN.x ) | ( SnormBits<16>( octN.y ) << 16 );

	        vtxNormals.push_back( encNormal );

	        if constexpr( validateNormalEncoding )
	        {
	            //HT_ASSERT( HpkDecodeVerifyQuantized( p, enc, mltEncodingGrid ) );
	        }
	    }

		//verticesAttrs.append_range( HpkMeshletPackVtxAttributes( m ) );
		indices.append_range( m.indices );
	    indices.append_range( ( FLT_MAX != m.lodError ) ? std::span{ m.idxLod } : std::span<const u8>{} );
	}
}

struct hpk_file_write
{
    u64 offsetInBytes;
    u64 sizeInBytes;
};

struct hpk_out_file
{
    u64                         hFile   = 0;
    alignas( 64 ) atomic_u64    cursor  = 0;

    hpk_out_file() = default;
    hpk_out_file( const char* filePath ) : hFile{ ht_os_create_file(
        filePath, file_perm_bits::WRITE, file_create_flags::OVERWRITE, file_access_flags::CONCURRENT ) } {}
    hpk_out_file( std::string_view filePath ) : hpk_out_file{ std::data( filePath ) } {}

    hpk_file_write WriteBlocking( std::span<const u8> rawBytes )
    {
        u64 sizeInBytes     = std::size( rawBytes );
        u64 offsetInBytes   = SysAtomicAdd64<sys_fence_t::NONE>( &cursor, sizeInBytes );

        SysWriteFileConcurrentBlocking( hFile, offsetInBytes, rawBytes );
        return { .offsetInBytes = offsetInBytes, .sizeInBytes = sizeInBytes };
    }
};

static hpk_out_file hpkOutFile = {};

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

materials_jobs PrepareBcnCompressionBatch(
	std::span<const raw_material_info>	rawMaterials,
	std::span<const raw_image_view>		imageViews
) {
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

		u64 baseColorHash = ProcessImageView( mtrl.baseColorIdx, dds::DXGI_FORMAT_BC7_UNORM_SRGB, { "{}_albedo.dds", mtrl.name } );
		u64 normalHash = ProcessImageView( mtrl.normalIdx, dds::DXGI_FORMAT_BC5_UNORM, { "{}_normal.dds", mtrl.name } );
		u64 metallicRoughnessHash = ProcessImageView( mtrl.metallicRoughnessIdx, dds::DXGI_FORMAT_BC7_UNORM, { "{}_mro.dds", mtrl.name } );
		u64 emissiveHash = ProcessImageView( mtrl.emissiveIdx, dds::DXGI_FORMAT_BC7_UNORM_SRGB, { "{}_emissive.dds", mtrl.name } );

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

constexpr bool CHECK_CORRECTNESS = true;

static void AtomicWait( const std::atomic<u64>& waitAddr, u64 waitVal )
{
    for( u64 seenVal; ( seenVal = waitAddr.load( std::memory_order_acquire ) ) < waitVal; ) waitAddr.wait( seenVal );
}

constexpr u64   GRID_SECTOR_DIM_IN_METERS   = 256;
constexpr float GRID_SCALE                  = 1.0f / float( GRID_SECTOR_DIM_IN_METERS );

i32x2 HpkBinNodeTo2DGridSector( const raw_node& node )
{
    using namespace DirectX;

    XMVECTOR localCenter = XMVectorScale( XMVectorAdd( DX_XMLoadFloat3( node.aabb.min ),
        DX_XMLoadFloat3( node.aabb.max ) ), 0.5f );
    XMVECTOR worldCenter = XMVectorAdd( XMVector3Rotate(
        XMVectorMultiply( localCenter, DX_XMLoadFloat3( node.toWorld.s ) ),
        DX_XMLoadFloat4( node.toWorld.r ) ),
        DX_XMLoadFloat3( node.toWorld.t ) );

    XMVECTOR sector = XMVectorFloor( XMVectorScale( worldCenter, GRID_SCALE ) );
    // NOTE: bc we've exported from gLTF
    return { ( i32 ) XMVectorGetX( sector ), ( i32 ) XMVectorGetZ( sector ) };
}

auto GetDirViewOfFiles( std::string_view dir, std::string_view ext )
{
    return fs::directory_iterator{ dir } | std::views::filter( [ & ]( auto& e )
    {
        return e.path().string().ends_with( ext );
    } );
}

void HpkExitWithMsg( std::string_view msg )
{
    std::println( stderr, "{}", msg );
    std::exit( -1 );
}

static parsed_gltf HpkParseGltfs( std::string_view dir )
{
    std::vector gltfPaths = { std::from_range, GetDirViewOfFiles( dir, ".gltf" ) | std::views::transform(
    []( auto& e )
    {
        return sys_path{ e.path().string() };
    } ) };

    if( !std::size( gltfPaths ) ) HpkExitWithMsg( "No gltfs in dir\n" );

    parsed_gltf merged;
    for( const sys_path& gltfPath : gltfPaths )
    {
        std::println( stdout, "Processing {}\n", gltfPath );

        mmap_file rawGltfBytes = SysCreateMmapFile( ( const char* ) gltfPath,
            file_perm_bits::READ, file_create_flags::OPEN_IF_EXISTS,
            file_access_flags::SEQUENTIAL );
        defer { SysDestroyMmapFile( &rawGltfBytes ); };

        scoped_arena fileArena = { g_ThreadArena[ 0 ] };
        scoped_arena cgltfArena = { g_ThreadExtLibArena };

        cgltf_options options = {
            .type   = cgltf_file_type_gltf,
            .memory = { .alloc_func = CgltfArenaAlloc, .free_func = CgltfArenaFree },
            .file   = { .read = HtCgltfFileRead, .release = HtCgltfFileRelease }
        };
        const cgltf_data* pGltf = CgltfLoadMetadataFromRawBytes( rawGltfBytes.dataView, options );

        auto[ nodes, meshDescs ] = CgltfProcessDrawablesHierarchy(
            pGltf, SysPathStem( gltfPath ) );

        merged.nodes.append_range( MOV( nodes ) );
        merged.meshDesc.append_range( MOV( meshDescs ) );
    }

    return merged;
}

static void HpkProcessMeshesParallel( std::span<const raw_mesh_desc> rawMeshDescs, std::span<const u8> binData )
{
    std::atomic<u64> atomicJobsCounter = 0;

    auto LmbdProcessMeshJob = [ & ]()
    {
        const u64   jobCount    = std::size( rawMeshDescs );
        nv_gdeflate nvGDeflate  = { 6 };
        for( ;; )
        {
            u64 currJobIdx = atomicJobsCounter.fetch_add( 1, std::memory_order_relaxed );
            if( currJobIdx >= jobCount ) return;

            virtual_arena& scratchArena = g_ThreadArena[ 0 ];
            virtual_arena& tempArena    = g_ThreadArena[ 1 ];

            scoped_arena<virtual_arena> memScopes[] = { tempArena, scratchArena, g_ThreadExtLibArena };

            const raw_mesh_desc& meshDesc = GltfPatchRawMeshDesc( rawMeshDescs[ currJobIdx ], binData );
            // TODO: process points too
            if( raw_mesh_topology_t::POINTS == meshDesc.topology ) continue;
            // NOTE: degenerate geometry
            if( float3{} == ( meshDesc.aabb.max - meshDesc.aabb.min ) ) continue;

            std::array<hpk_meshlets_w_lod, MAX_LOD_LEVELS_COUNT> mltsWLod = {};
            {
                // NOTE: NO tempArena here bc we need it to outlive this scope
                scoped_arena<virtual_arena> inMemScopes[] = { scratchArena, g_ThreadExtLibArena };

                hpk_virt_array<float3> pos     = { scratchArena, std::from_range, meshDesc.pos };
                hpk_virt_array<u32>    indices = ReadNormalizedIndexBuffer( meshDesc.indices, scratchArena );
                hpk_virt_array<float3> normals;
                if( std::size( meshDesc.normals ) )
                {
                    normals = hpk_virt_array<float3>{ scratchArena, std::from_range, meshDesc.normals };
                }
                else
                {
                    normals = hpk_virt_array<float3>{ scratchArena, std::size( pos ) };
                    GenerateSmoothNormals( pos, indices, normals );
                }
                MeshoptReindexAndOptimizeMesh( pos, normals, indices, scratchArena );

                mltsWLod = MeshoptMakeHpMeshletsWithLod(
                pos, normals, indices, LOD_MESH_LEVEL_RATIO, {}, tempArena, scratchArena );
            }

            for( const hpk_meshlets_w_lod& lodLevel : mltsWLod )
            {
                if( FLT_MAX == lodLevel.meshLevelError ) continue;

                scoped_arena<virtual_arena> inMemScopes[] = { tempArena, scratchArena, g_ThreadExtLibArena };

                u64 totalMltCount       = std::size( lodLevel.meshlets );
                u64 totalVtxPosCount    = totalMltCount * RASTER_MAX_VTX_PER_MLT * QUANT_POS_BIT_DEPTH_BOUND;
                u64 totalVtxNormCount   = totalMltCount * RASTER_MAX_VTX_PER_MLT;
                u64 totalIdxBuffCount   = totalMltCount * RASTER_MLT_MAX_INDEX * LODS_PER_MESHLET;

                bit_stream					posBitstream = { .qwords = ArenaNewArray<u64>( tempArena, totalVtxPosCount ) };
                borrowed_array<oct16x2>		vtxNormals  = ArenaNewArray<oct16x2>( tempArena, totalVtxNormCount );
                borrowed_array<index_t>		idxBuff     = ArenaNewArray<index_t>( tempArena, totalIdxBuffCount );
                borrowed_array<gpu_meshlet>	meshlets    = ArenaNewArray<gpu_meshlet>( tempArena, totalMltCount );

                HpkQuantizeAndAppendLODLevel( lodLevel.meshlets, posBitstream, vtxNormals, idxBuff, meshlets );

                //HT_ASSERT( std::ranges::size( posBitstream ) && std::ranges::size( vtxNormals ) &&
                //    std::ranges::size( idxBuff ) && std::ranges::size( meshlets ) );

                // NOTE: perks of having mem arenas i guess
                std::span<u8> rawData = inMemScopes[ 0 ].GetCurrentScopeByteView();

                gdeflate_compressed compressed = NvGDeflateCompressInArenaMem(
                    nvGDeflate, rawData, scratchArena, tempArena );
                // compress, wrte to hpk file write toc
            }



            hpk_mesh_asset meshAsset = {
                //.vtxPosBitstream			= MOV( vtxPosBitstream ),
                //.vtxNormals				    = MOV( vtxNormals ),
                //.indices					= MOV( indices ),
                //.meshlets					= MOV( meshlets ),
                .aabb						= { meshDesc.aabb.min, meshDesc.aabb.max },
                .lodErrors					= {
                    mltsWLod[ 0 ].meshLevelError, mltsWLod[ 1 ].meshLevelError,
                    mltsWLod[ 2 ].meshLevelError, mltsWLod[ 3 ].meshLevelError
                },
                .packed16x4_lodMltCounts	= {
                    u32( std::size( mltsWLod[ 0 ].meshlets ) ) | ( u32( std::size( mltsWLod[ 1 ].meshlets ) ) << 16 ),
                    u32( std::size( mltsWLod[ 2 ].meshlets ) ) | ( u32( std::size( mltsWLod[ 3 ].meshlets ) ) << 16 )
                }
            };
        }
    };

    {
        std::vector workers = { std::from_range, std::views::iota( 0ull, g_ThreadCount )
            | std::views::transform( [ & ]( u64 ) { return std::jthread{ LmbdProcessMeshJob }; } ) };
    }
}

i32 main( i32 argc, char** argv  )
{
    std::cout << std::unitbuf;
    std::setvbuf( stdout, nullptr, _IONBF, 0 );

    std::vector<std::string_view> cliArgs = { argv + 1, argv + argc };

	if( std::size( cliArgs ) < 2 ) HpkExitWithMsg( "Missing arguments\n" );

    auto isDir = std::ranges::find( cliArgs, "--dir" );

    if( std::end( cliArgs ) != isDir )
    {
        std::string_view dir = isDir[ 1 ];
        if( !fs::exists( dir ) && !fs::is_directory( dir ) ) HpkExitWithMsg( "Missing dir\n" );

        auto[ nodes, meshDescs ] = HpkParseGltfs( dir );

        std::vector<i32x2> nodeBins = { std::from_range, nodes | std::views::transform( HpkBinNodeTo2DGridSector ) };

        constexpr u64 BATCH_CAP_IN_VTX_COUNT = 16ull << 10;
        for( const raw_mesh_desc& rawMeshDesc : meshDescs )
        {
            if( std::size( rawMeshDesc.pos ) > BATCH_CAP_IN_VTX_COUNT ) continue;

        }


        fs::directory_iterator dirIt{ dir };
        auto binEntry = std::ranges::find_if( dirIt, []( auto& e )
        {
            return e.path().string().ends_with( ".bin" );
        } );

        if( std::ranges::end( dirIt ) == binEntry ) HpkExitWithMsg( "No bin in dir\n" );

        sys_path binPath = { binEntry->path().string() };


        mmap_file rawGltfBinData = SysCreateMmapFile( ( const char* ) binPath,
           file_perm_bits::READ, file_create_flags::OPEN_IF_EXISTS,
           file_access_flags::RANDOM );

        // NOTE: this is global but our hook call thread local data, so safe
        meshopt_setAllocator( MeshoptScratchAlloc, MeshoptScratchFree );
        libdeflate_set_memory_allocator( NvGDeflateArenaAlloc, NvGDeflateArenaFree );

        hpkOutFile = { cliArgs[ 2 ] }; // TODO: don't hardcode
        std::println( stdout, "Starting HpkProcessMeshesParallel" );

        // TODO: maybe compute the LOD count dynamically; for now we'll do 4 mesh LODS @ 1/4 + 2 mlt LODs ( full + 1/2 )
        HpkProcessMeshesParallel( meshDescs, rawGltfBinData.dataView );

        HT_ASSERT( 0 );
        return 0;
    }

    /*
	const std::string_view gltfFilePath = argv[ 1 ];
	const std::string_view hpkFilePath  = argv[ 2 ];

	HT_ASSERT( fs::exists( gltfFilePath ) );

	gltf_loader gltf = { std::data( gltfFilePath ) };

	std::vector<raw_node>			rawNodes		= gltf.ProcessDrawableNodes();
	std::vector<raw_mesh>			rawMeshes		= gltf.ProcessPrimitives();


	ankerl::unordered_dense::map<vfs_path, hpk_mesh_asset> meshAssetMap;

	std::cout << "Processing meshes\n";
	for( raw_mesh& mesh : rawMeshes )
	{
		vfs_path assetPath = { "{}{}.mesh", HELLPACK_MESH_DIR, std::data( mesh.name ) };
		HT_ASSERT( !meshAssetMap.contains( assetPath ) );

		ValidateAndNormalizeRawMesh( mesh );
		MeshoptReindexAndOptimizeMesh( mesh );

		std::vector<hpk_lod_level> meshLods = MeshoptGenerateLODChain( mesh, MAX_LOD_LEVELS_COUNT - 1 );

		float lodErr[ MAX_LOD_LEVELS_COUNT ] = { FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
		u32 lodMltNum[ MAX_LOD_LEVELS_COUNT ] = {};

		bit_stream							vtxPosBitstream;
		std::vector<packed_vtx_attr>		verticesAttrs;
		std::vector<index_t>				indices;
		std::vector<gpu_meshlet>			meshlets;

		for( u64 li = 0; li < std::size( meshLods ); li++ )
		{
			const hpk_lod_level& lod = meshLods[ li ];
			// NOTE: the lod errors are "global" per object
			std::vector<hpk_meshlet> lodMeshlets = MeshoptMakeHpMeshletsWithLod( mesh.pos, mesh.normals, mesh.tans,
				mesh.uvs, lod.indices, lod.error, {} );

			HT_ASSERT( std::size( lodMeshlets ) < MAX_MESHLETS_PER_MESH );
			lodMltNum[ li ] = u32( std::size( lodMeshlets ) );
			lodErr[ li ] = lod.error;

			HpkQuantizeAndAppendLODLevel( lodMeshlets, vtxPosBitstream, verticesAttrs, indices, meshlets );
		}


		};

		meshAssetMap.emplace( assetPath, std::move( meshAsset ) );
	}

	std::vector<world_node> worldNodes;
	worldNodes.reserve( std::size( rawNodes ) );

	for( const raw_node& n : rawNodes )
	{
		if( !IsIndexValid( n.meshIdx ) ) continue;

		raw_mesh& mesh = rawMeshes[ ( u32 ) n.meshIdx ];

		vfs_path assetPath = { "{}{}.mesh", HELLPACK_MESH_DIR, std::data( mesh.name ) };

		worldNodes.push_back( {
			.toWorld		= { .t = n.toWorld.t, .r = n.toWorld.r, .s = n.toWorld.s },
			.meshHash		= std::hash<vfs_path>{}( assetPath ),
			.materialIdx	= ( u16 ) mesh.materialIdx // NOTE: these should match 1:1 with ours
		} );
	}

	std::cout << "Processing meshes & nodes done ! Dumping to file.\n";

	{
		zip_writer zipArchive = { std::data( hpkFilePath ) };

		HT_ASSERT( fs::exists( hpkFilePath ) );

		{
			hpk_level_asset level = { .nodes = MOV( worldNodes ) };//, .materials = MOV( materialTable ) };
			std::vector<u8> bytes = HpkSerializeAsset( level );
			zipArchive.WriteBytesToFile( { "world.lvl" }, bytes );
		}
		{
			for( auto& [ filePath, meshAsset ] : meshAssetMap )
			{
				std::vector<u8> bytes = HpkSerializeAsset( meshAsset );
				zipArchive.WriteBytesToFile( filePath, bytes );
			}
		}

		//
		//std::cout << "Processing materials done ! Dumping to file.\n";
		//for( const compression_job& cmp : texCmpJobs )
		//{
		//	HT_ASSERT( std::size( cmp.tex ) );
		//	vfs_path texPath = { "{}{}", HELLPACK_TEX_DIR, std::data( cmp.filename ) };
		//	zipArchive.WriteBytesToFile( texPath, cmp.tex );
		//}
	}


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

