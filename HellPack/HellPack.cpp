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
#include <chrono>

#include <ankerl/unordered_dense.h>

#include <dds.h>

#include <ht_core_types.h>
#include <ht_error.h>
#include <ht_vec_types.h>
#include <ht_macros.h>
#include <ht_gfx_types.h>
#include <hell_pack.h>

#include <ht_math.h>
#include <System/sys_file.h>

#include "hpk_meshopt_pipeline.h"
#include "hp_encoding.h"
#include "hp_bcn_compression.h"
#include "gltf_loader.h"
#include "hp_types_internal.h"

#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

#define ZSTD_CHECK( zstdExpr )                                                          \
do{                                                                                     \
	constexpr char DEV_ERR_STR[] = RUNTIME_ERR_LINE_FILE_STR;                           \
	u64 zstdRes = zstdExpr;                                                             \
	if( ZSTD_isError( zstdRes ) )                                                       \
    {                                                                                   \
        HtPrintErrAndDie( "{} \nERR: {}", DEV_ERR_STR, ZSTD_getErrorName( zstdRes ) );  \
    }                                                                                   \
}while( 0 )


static const u64 g_ThreadCount = std::thread::hardware_concurrency();

thread_local static virtual_arena g_ThreadArena[ 2 ]        = { { 4 * GB }, { 4 * GB } };
thread_local static virtual_arena g_ThreadExtLibArena       = { 4 * GB };

static std::atomic<u64> g_AtomicJobsCounter     = 0;
static std::atomic<u64> g_AtomicWorkerCounter   = 0;


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

struct bit_stream
{
    borrowed_array<u64>	qwords          = {};
    u64					cursorInBits    = 0;  // NOTE: lsb

    u64* begin() { return std::data( qwords ); }
    u64* end() { return std::data( qwords ) + ( ( cursorInBits + 63 ) >> 6 ); }

    void Reset() { qwords.resize( 0 ); cursorInBits = 0; }

    void AppendBits( u32 inBitStream, u32 bitDepth )
    {
        HT_ASSERT( bitDepth < 64 );
        u64     bitStream           = u64( inBitStream ) & ( ( 1ull << bitDepth ) - 1 );

        u64     qwBucket            = cursorInBits >> 6;
        u32     bitOffset           = cursorInBits & 63;
        u32     howManyBitWillFit   = 64 - bitOffset;
        bool    carryOver           = bitDepth > howManyBitWillFit;

        if( u64 sz = std::size( qwords ); sz <= ( qwBucket + u64( carryOver ) ) )
        {
            qwords.resize( sz + 64, 0 );
        }

        qwords[ qwBucket ] |= bitStream << bitOffset;
        if( carryOver )
        {
            qwords[ qwBucket + 1 ] |= ( bitStream >> howManyBitWillFit );
        }

        cursorInBits += bitDepth;
    }
};

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

static void GenerateSmoothNormals( std::span<const float3> pos, std::span<const u32> indices, std::span<float3> normals )
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

        float3 faceNormal = ht::cross( pos1 - pos0, pos2 - pos0 );
        normals[ vtx0 ] += faceNormal;
        normals[ vtx1 ] += faceNormal;
        normals[ vtx2 ] += faceNormal;
    }

    std::ranges::for_each( normals, []( float3& n ) { n = ht::normalize( n ); } );
}

static std::vector<packed_vtx_attr> HpkMeshletPackVtxAttributes( const hpk_meshlet& mlt )
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

constexpr bool validatePosEncoding      = true;
constexpr bool validateNormalEncoding   = true;

// NOTE: this is contiguous
struct hpk_quantized_lod
{
    std::span<u64>			posBitstream;
    std::span<oct16x2>	    vtxNormals;
    std::span<gpu_meshlet>  gpuMlts;
    std::span<u8>		    idxBuff;
};

static hpk_quantized_lod HpkQuantizeLODLevel( std::span<const hpk_meshlet> meshlets, virtual_arena& backingArena )
{
    u64 totalMltCount       = std::size( meshlets );
    u64 totalVtxPosCount    = totalMltCount * RASTER_MAX_VTX_PER_MLT * QUANT_POS_BIT_DEPTH_BOUND;
    u64 totalVtxNormCount   = totalMltCount * RASTER_MAX_VTX_PER_MLT;
    u64 totalIdxBuffCount   = totalMltCount * RASTER_MLT_MAX_INDEX * LODS_PER_MESHLET;

    // NOTE: the order is specific to minimize padding and stuff
    auto posBuff        = bit_stream{ .qwords = ArenaNewArray<u64>( backingArena, totalVtxPosCount ) };
    auto vtxNormals     = borrowed_array{ ArenaNewArray<oct16x2>( backingArena, totalVtxNormCount ) };
    auto gpuMlts        = borrowed_array{ ArenaNewArray<gpu_meshlet>( backingArena, totalMltCount ) };
    auto idxBuff        = borrowed_array{ ArenaNewArray<u8>( backingArena, totalIdxBuffCount ) };

	for( const hpk_meshlet& m : meshlets )
	{
		aabb_t<float3> meshletAabb = ComputeAabb( m.pos );
        // TODO: why this happens ? and can't we not prevent it ?
	    if( ht::all( float3{} == ( meshletAabb.max - meshletAabb.min ) ) ) continue;

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

		gpuMlts.push_back( {
			.aabbMin								= mltEncodingGrid.quantAabbMin,
			.aabbMax								= mltEncodingGrid.quantAabbMax,
			.vtxPosOffsetBits						= ( u32 ) posBuff.cursorInBits,
			.vtxAttrsOffset							= ( u32 ) std::size( vtxNormals ),
			.idxOffset								= ( u32 ) std::size( idxBuff ),
			.packed8888_XYZ_Grid_BitDepth			= packed8888_XYZ_Grid_BitDepth,
			.packed8_12_12_VtxCount_Lod_01_IdxCount	= packed8_12_12_VtxCount_Lod_01_IdxCount,
			.lodError								= m.lodError
		} );

		for( float3 p : m.pos )
		{
			u32x3 enc = HpkEncodeMltVertexPosition( mltEncodingGrid, p );

			posBuff.AppendBits( enc.x, mltEncodingGrid.bitDepthPerAxis.x );
			posBuff.AppendBits( enc.y, mltEncodingGrid.bitDepthPerAxis.y );
			posBuff.AppendBits( enc.z, mltEncodingGrid.bitDepthPerAxis.z );

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
		idxBuff.append_range( m.indices );
	    idxBuff.append_range( ( FLT_MAX != m.lodError ) ? std::span{ m.idxLod } : std::span<const u8>{} );
	}

    HT_ASSERT( std::ranges::size( posBuff ) && std::size( vtxNormals ) && std::size( idxBuff ) && std::size( gpuMlts ) );

    std::span<const u8> lodScratchBytes = {
        ( const u8* ) std::data( posBuff.qwords ), ( const u8* ) ht::end_ptr( idxBuff.mem )
    };

    // NOTE: bc of ASan builds !
    HT_UNPOISON( std::data( lodScratchBytes ), std::size( lodScratchBytes ) );

    std::span<u64>          outPos      = { posBuff.qwords };
    std::span<oct16x2>      outNormals  = HtMemCompact( outPos, vtxNormals );

    std::span<gpu_meshlet>  outGpuMlts  = HtMemCompact( outNormals, gpuMlts );
    std::span<u8>           outIdxBuff  = HtMemCompact( outGpuMlts, idxBuff );

    u64 leftoverBytes = backingArena.Mark() - u64( ht::end_ptr( outIdxBuff ) - backingArena.mem );
    backingArena.RewindNBytes( leftoverBytes );

    return { .posBitstream = outPos, .vtxNormals = outNormals, .gpuMlts = outGpuMlts, .idxBuff = outIdxBuff };
}

struct hpk_concurrent_file
{
    void*                       hFile   = nullptr;
    alignas( 64 ) atomic_u64    cursor  = 0;

    hpk_concurrent_file() = default;
    hpk_concurrent_file( const char* filePath ) : hFile{ ht_os_create_file( filePath,
        file_perm_t::WRITE, file_create_t::OVERWRITE, file_access_t::CONCURRENT ) } {}
    hpk_concurrent_file( std::string_view filePath ) : hpk_concurrent_file{ std::data( filePath ) } {}

    u64 WriteBlocking( std::span<const u8> rawBytes )
    {
        u64 sizeInBytes     = std::size( rawBytes );
        u64 offsetInBytes   = SysAtomicAdd64<sys_fence_t::NONE>( &cursor, sizeInBytes );

        SysWriteFileConcurrentBlocking( hFile, offsetInBytes, rawBytes );
        return offsetInBytes;
    }
};

static hpk_concurrent_file hpkOutFile = {};

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


inline void AtomicWait( const std::atomic<u64>& waitAddr, u64 waitVal )
{
    for( ;; )
    {
        u64 seenVal = waitAddr.load( std::memory_order_acquire );
        if( seenVal >= waitVal ) break;
        waitAddr.wait( seenVal );
    }
}

inline auto GetDirViewOfFiles( std::string_view dir, std::string_view ext )
{
    return fs::directory_iterator{ dir } | std::views::filter( [ & ]( auto& e )
    {
        return e.path().string().ends_with( ext );
    } );
}

inline void HpkExitWithMsg( std::string_view msg )
{
    std::println( stderr, "{}", msg );
    std::exit( -1 );
}

static auto HpkProcessGltfs( std::string_view dir )
{
    // TODO: replace with own SysDirIterator
    std::vector gltfPaths = { std::from_range, GetDirViewOfFiles( dir, ".gltf" ) | std::views::transform(
    []( auto& e )
    {
        return sys_path{ e.path().string() };
    } ) };

    if( !std::size( gltfPaths ) ) HpkExitWithMsg( "No gLTFs in dir\n" );

    std::vector<raw_node>       rawNodes;
    std::vector<raw_mesh_desc>  meshDesc;
    for( const sys_path& gltfPath : gltfPaths )
    {
        std::println( stdout, "Processing {}\n", gltfPath );

        mmap_file rawGltfBytes = SysCreateMmapFile( ( const char* ) gltfPath,
            file_perm_t::READ, file_create_t::OPEN_IF_EXISTS,
            file_access_t::SEQUENTIAL );
        defer { SysDestroyMmapFile( &rawGltfBytes ); };

        scoped_arena<virtual_arena> scopedArenas[] = { g_ThreadArena[ 0 ], g_ThreadExtLibArena };

        cgltf_options options = {
            .type   = cgltf_file_type_gltf,
            .memory = { .alloc_func = CgltfArenaAlloc, .free_func = CgltfArenaFree },
            .file   = { .read = HtCgltfFileRead, .release = HtCgltfFileRelease }
        };
        const cgltf_data* pGltf = CgltfLoadMetadataFromRawBytes( rawGltfBytes.dataView, options );

        auto[ nodes, meshDescVec ] = CgltfProcessDrawablesHierarchy(
            pGltf, SysPathStem( gltfPath ) );

        for( raw_node& n : nodes ) { n.meshID += std::size( meshDesc ); }

        rawNodes.append_range( MOV( nodes ) );
        meshDesc.append_range( MOV( meshDescVec ) );
    }

    return std::pair{ MOV( rawNodes ), MOV( meshDesc ) };
}

namespace ht
{
    inline std::vector<u32> inclusive_scan_on_sorted( std::span<const u32> sortedKeys )
    {
        std::vector<u32> scan = {};
        scan.reserve( std::size( sortedKeys ) );
        for( auto keyRun : sortedKeys | std::views::chunk_by( std::equal_to{} ) )
        {
            scan.push_back( u32( std::ranges::end( keyRun ) - std::begin( sortedKeys ) ) );
        }
        scan.shrink_to_fit();

        return scan;
    }
}


template<TRIVIAL_T T>
struct hpk_virtual_storage : arena_storage<T, virtual_arena>
{
    static constexpr bool   CAN_GROW        = true;
    static constexpr bool   OWNS_ELEMENTS   = false;

    virtual_arena           arena           = {};

    hpk_virtual_storage() = default;
    hpk_virtual_storage( u64 reservedInBytes ) : arena{ reservedInBytes } {}

    void Grow( this auto&& self, u64 reqSzInElems )
    {
        self.pArena = &self.arena;
        self.arena_storage<T, virtual_arena>::Grow( reqSzInElems );
    }
};

inline auto HpkRunMeshopPipeline(
    const raw_mesh_desc&    rawMeshDesc,
    virtual_arena&          scratchArena,
    virtual_arena&          outArena
) -> inline_array<hpk_meshlets_w_lod, MAX_LOD_LEVELS_COUNT>
{
    // NOTE: NO tempArena here bc we need it to outlive this scope
    scoped_arena<virtual_arena> inMemScopes[] = { scratchArena, g_ThreadExtLibArena };

    auto pos     = borrowed_array<float3>{ scratchArena, std::from_range, GltfTypedView( rawMeshDesc.pos ) };
    auto indices = borrowed_array<u32>{ scratchArena, std::from_range, GtlfGetIdx32View( rawMeshDesc.indices ) };
    auto normals = borrowed_array<float3>{ scratchArena, std::size( pos ) };
    if( std::size( rawMeshDesc.normals ) )
    {
        HT_ASSERT( std::size( rawMeshDesc.normals ) == std::size( pos ) );
        std::ranges::copy( GltfTypedView( rawMeshDesc.normals ), std::data( normals ) );
    }
    else GenerateSmoothNormals( pos, indices, normals );

    MeshoptReindexAndOptimizeMesh( pos, normals, indices, scratchArena );

    return MeshoptMakeHpkMeshletsWithLod( pos, normals, indices, LOD_MESH_LEVEL_RATIO,
        ht::length( rawMeshDesc.aabbExtent ), meshlet_config{}, outArena, scratchArena );
}

struct alignas( HT_CACHE_LINE_SZ ) hpk_mesh
{
    static constexpr u64 LOD_NUM    = MAX_LOD_LEVELS_COUNT;
    // NOTE: we need this to make it work with IsZeroStruct
    static constexpr u64 DATA_SZ    = sizeof( hpk_mesh_desc ) + sizeof( inline_array<hpk_lod_desc, LOD_NUM> );
    static constexpr u64 PADDING_SZ = FwdAlignPot( DATA_SZ, HT_CACHE_LINE_SZ ) - DATA_SZ;

    hpk_mesh_desc                       desc                = {};
    inline_array<hpk_lod_desc, LOD_NUM> lods                = {};
    u8                                  pad[ PADDING_SZ ]   = {};
};

static void HpkProcessMeshesParallelJob(
    std::span<const u32>            batchInclScan,
    std::span<const raw_mesh_desc>  meshDescView,
    std::span<const u8>             binData,
    std::span<hpk_mesh>             meshDescOut
) {
    constexpr u64 NLOD = MAX_LOD_LEVELS_COUNT;

    auto hpkSectorBlob = ht_array{ hpk_virtual_storage<u8>{ 1 * GB } };

    virtual_arena& scratchArena = g_ThreadArena[ 0 ];
    virtual_arena& tempArena    = g_ThreadArena[ 1 ];

    u64        zstdSz   = ZSTD_estimateCCtxSize( ZSTD_COMPRESSION_LEVEL );
    ZSTD_CCtx* pZstdCtx = ZSTD_initStaticCCtx( g_ThreadExtLibArena.Alloc( zstdSz, 8 ), zstdSz );
    HT_ASSERT( pZstdCtx );

    for( ;; )
    {
        u64 currJobIdx = g_AtomicJobsCounter.fetch_add( 1, std::memory_order_relaxed );
        if( currJobIdx >= std::size( batchInclScan ) ) break;

        u64 offset  = currJobIdx ? batchInclScan[ currJobIdx - 1 ] : 0;
        u64 size    = batchInclScan[ currJobIdx ] - offset;

        auto meshBatchView = meshDescView.subspan( offset, size );
        for( const auto[ mdi, md ] : meshBatchView | std::views::enumerate )
        {
            scoped_arena<virtual_arena> memScopes[] = { tempArena, scratchArena, g_ThreadExtLibArena };

            const raw_mesh_desc meshDesc = GltfPatchRawMeshDesc( md, binData );

            bool points = raw_mesh_topology_t::POINTS == meshDesc.topology; // TODO: process points too
            bool degen  = ht::all( float3a{} == meshDesc.aabbExtent );

            if( points || degen ) continue;

            inline_array<hpk_meshlets_w_lod, NLOD> mltsWLod = HpkRunMeshopPipeline(
                meshDesc, scratchArena, tempArena );

            inline_array<hpk_lod_desc, NLOD> lodDescs = {};
            for( const hpk_meshlets_w_lod& mLod : mltsWLod )
            {
                auto memScope2 = scoped_arena{ scratchArena };

                hpk_quantized_lod quantLod = HpkQuantizeLODLevel( mLod.meshlets, memScope2 );

                std::span<const u8> rawView = {
                    ( const u8* ) std::data( quantLod.posBitstream ), ht::end_ptr( quantLod.idxBuff )
                };

                u64 compBound   = ZSTD_compressBound( std::size( rawView ) );

                u64 writeOffset = hpkSectorBlob.grow_by( compBound );
                u8* writeDst    = std::data( hpkSectorBlob ) + writeOffset;

                u64 compSize    = ZSTD_compressCCtx( pZstdCtx, writeDst, compBound, std::data( rawView ),
                    std::size( rawView ), ZSTD_COMPRESSION_LEVEL );
                ZSTD_CHECK( compSize );

                hpkSectorBlob.shrink_by( compBound - compSize );

                if( compSize >= std::size( rawView ) ) // NOTE: no compression needed
                {
                    std::ranges::copy( rawView, writeDst );
                    hpkSectorBlob.shrink_by( compSize - std::size( rawView ) );
                }

                lodDescs.push_back( {
                    .fileOffsetInBytes  = writeOffset,
                    .storedSzInBytes    = ( compSize < std::size( rawView ) ) ? compSize : std::size( rawView ),
                    .posSzInBytes       = HtRangeSizeInBytes( quantLod.posBitstream ),
                    .normalsSzInBytes   = HtRangeSizeInBytes( quantLod.vtxNormals ),
                    .idxBuffSzInBytes   = HtRangeSizeInBytes( quantLod.idxBuff ),
                    .mltsSzInBytes      = HtRangeSizeInBytes( quantLod.gpuMlts )
                } );
            }

            // NOTE: this is a hack; and we can only index by 0-3 bc we know the lod count
            float4 lodErrs = { std::data( mltsWLod )[ 0 ].meshLevelError, std::data( mltsWLod )[ 1 ].meshLevelError,
                std::data( mltsWLod )[ 2 ].meshLevelError, std::data( mltsWLod )[ 3 ].meshLevelError };
            float3a aabbMin = meshDesc.aabbCenter - meshDesc.aabbExtent;
            float3a aabbMax = meshDesc.aabbCenter + meshDesc.aabbExtent;

            meshDescOut[ offset + mdi ] = {
                .desc = hpk_mesh_desc{
                    .hashed     = meshDesc.meshHash,
                    .aabbMin    = { aabbMin.x, aabbMin.y, aabbMin.z },
                    .aabbMax    = { aabbMax.x, aabbMax.y, aabbMax.z },
                    .lodErrs    = lodErrs,
                    .firstLod   = 0,
                    .lodCount   = std::size( lodDescs )
                },
                .lods = lodDescs
            };
        }

        u64 fileOffsetInBytes = hpkOutFile.WriteBlocking( hpkSectorBlob );

        for( hpk_mesh& m : meshDescOut.subspan( offset, size ) )
        {
            for( hpk_lod_desc& lod : m.lods ) { lod.fileOffsetInBytes += fileOffsetInBytes; }
        }

        hpkSectorBlob.clear();
    }

    g_AtomicWorkerCounter.fetch_add( 1, std::memory_order_relaxed );
    g_AtomicWorkerCounter.notify_all();
}

auto HpkSortNodesAndMeshes( std::vector<raw_node>&& rawNodes, std::vector<raw_mesh_desc>&& meshDescVec )
{
    std::println( stdout, "Sector sort raw_nodes" );
    auto nodeBinKeys = std::vector<u32>( std::size( rawNodes ), {} );
    for( auto[ raw, bin ] : std::views::zip( rawNodes, nodeBinKeys ) )
    {
        float3a center  = ht::vec_rot( raw.aabbCenter * raw.toWorld.s, raw.toWorld.r ) + raw.toWorld.t;
        i16x2   secID   = HpkBinPointTo2DGridSector( center );
        bin             = std::bit_cast<u32>( secID );
    }

    auto nodeIdxBuff = std::vector<u32>{ std::from_range, std::views::iota( 0u, std::size( rawNodes ) ) };

    ht::kv_qsort( &nodeBinKeys[ 0 ], &nodeIdxBuff[ 0 ], std::size( nodeBinKeys ) );

    auto sortedNodes = std::vector<raw_node>{ std::from_range, ht::permuted_view( rawNodes, nodeIdxBuff ) };

    std::vector<u32> sectorScan = ht::inclusive_scan_on_sorted( nodeBinKeys );

    std::println( stdout, "Sort and batch mesh descs" );
    // NOTE: gltf has NO dupes WITHIN a file;
    // but caldera is split across many files bc it's to big so yes we need this
    std::vector<raw_mesh_desc> uniqueMeshes = {};
    uniqueMeshes.reserve( std::size( meshDescVec ) );

    constexpr u32 SHARED_MESH_BIN_KEY = UINT32_MAX;

    auto meshSlot = std::vector<u32>( std::size( meshDescVec ), UINT32_MAX );
    std::vector<u32> meshSectorKeys = {};
    for( u64 si = 0; si < std::size( sectorScan ); ++si )
    {
        u64 offsetInNodes   = si ? sectorScan[ si - 1 ] : 0;
        u64 sizeInNodes     = sectorScan[ si ] - offsetInNodes;
        for( raw_node& n : std::span{ std::data( sortedNodes ) + offsetInNodes, sizeInNodes } )
        {
            u32& uniqueIdx = meshSlot[ n.meshID ];
            if( UINT32_MAX == uniqueIdx )
            {
                uniqueIdx = ( u32 ) std::size( meshSectorKeys );
                meshSectorKeys.push_back( ( u32 ) si );
                uniqueMeshes.push_back( meshDescVec[ n.meshID ] );
            }
            else if( meshSectorKeys[ uniqueIdx ] < si )
            {
                meshSectorKeys[ uniqueIdx ] = SHARED_MESH_BIN_KEY;
            }

            n.meshID = uniqueIdx;
        }
    }

    auto meshIdxBuff = std::vector<u32>{ std::from_range, std::views::iota( 0u, std::size( meshSectorKeys ) ) };

    ht::kv_qsort( &meshSectorKeys[ 0 ], &meshIdxBuff[ 0 ], std::size( meshSectorKeys ) );

    auto batchedMeshes = std::vector<raw_mesh_desc>{ std::from_range, ht::permuted_view( uniqueMeshes, meshIdxBuff ) };

    std::vector<u32> batchScan = ht::inclusive_scan_on_sorted( meshSectorKeys );

    return std::tuple{ MOV( sortedNodes ), MOV( sectorScan ), MOV( batchedMeshes ), MOV( batchScan ) };
}

i32 main( i32 argc, char** argv  )
{
    HT_ASSERT( IsStructZero( hpk_mesh() ) );

    std::cout << std::unitbuf;
    std::setvbuf( stdout, nullptr, _IONBF, 0 );

    auto timeStart = std::chrono::steady_clock::now();

    std::vector<std::string_view> cliArgs = { argv + 1, argv + argc };

	if( std::size( cliArgs ) < 2 ) HpkExitWithMsg( "Missing arguments\n" );

    auto isDir = std::ranges::find( cliArgs, "--dir" );

    if( std::end( cliArgs ) != isDir )
    {
        std::string_view dir = isDir[ 1 ];
        if( !fs::exists( dir ) && !fs::is_directory( dir ) ) HpkExitWithMsg( "Missing dir\n" );

        auto[ rawNodes, meshDescVec ] = HpkProcessGltfs( dir );
        auto[
            sortedNodes,
            sectorScan,
            batchedMeshes,
            batchScan
        ] = HpkSortNodesAndMeshes( MOV( rawNodes ), MOV( meshDescVec ) );

        // NOTE: bc of fucking stupid C++ rules we have to use val_init aka hpk_mesh()
        auto hpkMeshesWLod = std::vector<hpk_mesh>( std::size( batchedMeshes ), hpk_mesh() );

        fs::directory_iterator dirIt{ dir };
        auto binEntry = std::ranges::find_if( dirIt, []( auto& e )
        {
            return e.path().string().ends_with( ".bin" );
        } );

        if( std::ranges::end( dirIt ) == binEntry ) HpkExitWithMsg( "No bin in dir\n" );

        sys_path binPath = { binEntry->path().string() };

        mmap_file rawGltfBinData = SysCreateMmapFile( ( const char* ) binPath, file_perm_t::READ,
            file_create_t::OPEN_IF_EXISTS, file_access_t::RANDOM );

        // NOTE: this is global but our hook call thread local data, so safe
        meshopt_setAllocator( MeshoptScratchAlloc, MeshoptScratchFree );

        hpkOutFile = hpk_concurrent_file{ cliArgs[ 2 ] }; // TODO: don't hardcode

        std::println( stdout, "Starting HpkProcessMeshesParallel" );

        // TODO: maybe compute the LOD count dynamically; for now we'll do 4 mesh LODs @ 1/4 + 2 mlt LODs ( full + 1/2 )
        for( auto _ : std::views::iota( 0ull, g_ThreadCount ) )
        {
            std::thread{ HpkProcessMeshesParallelJob, std::span{ batchScan }, std::span{ batchedMeshes },
                rawGltfBinData.dataView, std::span{ hpkMeshesWLod } }.detach();
        }
        AtomicWait( g_AtomicWorkerCounter, g_ThreadCount );
        /*
        u64 nodesOffset     = hpkOutFile.WriteBlocking( AsBytes( worldNodes ) );

        u64 lodDescOffset   = hpkOutFile.WriteBlocking( AsBytes( g_HpkLodDescVec ) );
        u64 meshDescOffset  = hpkOutFile.WriteBlocking( AsBytes( g_HpkMeshDescVec ) );

        u64 secDescOffset   = hpkOutFile.WriteBlocking( AsBytes( sectorsVec ) );

        hpk_file_footer fileFooter = {
            .magic                      = std::bit_cast<u64>( HPK_MAGIC ),
            .fileFormatVersion          = HPK_FORMAT_VERSION,
            .contentVersion             = HPK_CONTENT_VERSION,

            .firstSectorsOffsetInBytes  = secDescOffset,
            .sectorsCount               = std::size( sectorsVec ),

            .firstNodeOffsetInBytes     = nodesOffset,
            .nodeCount                  = std::size( worldNodes ),

            .firstMeshDescOffsetInBytes = meshDescOffset,
            .meshDescCount              = std::size( g_HpkMeshDescVec ),

            .firstLodDescOffsetInBytes  = lodDescOffset,
            .lodDescCount               = std::size( g_HpkLodDescVec )
        };
        */
        //hpkOutFile.WriteBlocking( { ( const u8* ) &fileFooter, sizeof( fileFooter ) } );

        std::chrono::duration<double, std::milli> elapsedMs = std::chrono::steady_clock::now() - timeStart;
        std::println( stdout, "Done in {:.2f} ms", elapsedMs.count() );

        return 0;
    }

	return 0;
}

