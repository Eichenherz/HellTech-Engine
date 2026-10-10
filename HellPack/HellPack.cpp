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

thread_local static virtual_arena g_ThreadArena[ 2 ]    = { { 4 * GB }, { 4 * GB } };
thread_local static virtual_arena g_ThreadExtLibArena   = { 4 * GB };

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
    u64 totalVtxPosCount    = totalMltCount * MAX_VTX_PER_MLT * QUANT_POS_BIT_DEPTH_BOUND;
    u64 totalVtxNormCount   = totalMltCount * MAX_VTX_PER_MLT;
    u64 totalIdxBuffCount   = totalMltCount * MLT_MAX_INDEX * LODS_PER_MESHLET;

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
			( std::size( m.idxLod ) < MLT_MAX_INDEX ) &&
			( std::size( m.indices ) < MLT_MAX_INDEX ) );

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

struct hpk_file_buffer
{
    void*  hFile   = nullptr;
    u64    cursor  = 0;

    hpk_file_buffer() = default;
    hpk_file_buffer( const sys_path& sysPath ) : hpk_file_buffer{ ( const char* ) sysPath } {}
    hpk_file_buffer( const char* filePath ) : hFile{ ht_os_create_file( filePath,
        file_perm_t::WRITE | file_perm_t::DELETABLE, file_create_t::OVERWRITE, file_access_t::SEQUENTIAL ) } {}
    hpk_file_buffer( std::string_view filePath ) : hpk_file_buffer{ std::data( filePath ) } {}

    u64 WriteBlocking( std::span<const u8> rawBytes )
    {
        u64 offsetInBytes = cursor;
        cursor += std::size( rawBytes );
        SysWriteFileConcurrentBlocking( hFile, offsetInBytes, rawBytes );
        return offsetInBytes;
    }
};

inline void HpkFileBufferClose( hpk_file_buffer& fileBuff )
{
    if( 0 == fileBuff.cursor ) { ht_delete_file_by_handle( fileBuff.hFile ); }
    ht_os_close_file( fileBuff.hFile );
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

constexpr auto LmbdIsFileWithExt = []( std::string_view ext )
{
    return [ ext ]( const fs::directory_entry& e ) { return e.is_regular_file() && e.path().extension() == ext; };
};

inline void HpkExitWithMsg( std::string_view msg )
{
    std::println( stderr, "{}", msg );
    std::exit( -1 );
}

static auto HpkProcessGltfs( std::string_view dir )
{
    std::vector<raw_node>       rawNodes;
    std::vector<raw_mesh_desc>  meshDesc;
    for( const auto& e : fs::directory_iterator{ dir } | std::views::filter( LmbdIsFileWithExt( ".gltf" ) ) )
    {
        std::string gltfPath = e.path().string();
        std::println( stdout, "Processing {}\n", gltfPath );

        mmap_file rawGltfBytes = SysCreateMmapFile( gltfPath.c_str(),
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

    if( !std::size( rawNodes ) && !std::size( meshDesc ) ) HpkExitWithMsg( "No gLTFs in dir\n" );

    return std::pair{ MOV( rawNodes ), MOV( meshDesc ) };
}

namespace ht
{
    template<typename EQUAL_TO_T = std::equal_to<>>
    auto inclusive_scan_on_sorted(
        const std::ranges::random_access_range auto&    sortedKeys,
        EQUAL_TO_T&&                                    PfnEqualTo = {}
    ) {
        std::vector<u32> scan = {};
        scan.reserve( std::size( sortedKeys ) );
        for( auto keyRun : sortedKeys | std::views::chunk_by( PfnEqualTo ) )
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

inline
inline_array<hpk_meshlets_w_lod, MAX_LOD_LEVELS_COUNT>
HpkRunMeshopPipeline(
    const raw_mesh_desc&    rawMeshDesc,
    virtual_arena&          scratchArena,
    virtual_arena&          outArena
) {
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

inline ZSTD_CCtx* HpkMakeZSTDContext()
{
    u64        zstdSz   = ZSTD_estimateCCtxSize( ZSTD_COMPRESSION_LEVEL );
    ZSTD_CCtx* pZstdCtx = ZSTD_initStaticCCtx( g_ThreadExtLibArena.Alloc( zstdSz, 8 ), zstdSz );
    HT_ASSERT( pZstdCtx );
    return pZstdCtx;
}

inline std::span<u8> HpkZSTDCompress( ZSTD_CCtx *const pZstdCtx, std::span<const u8> rawView, virtual_arena& arena )
{
    u64     compBound   = ZSTD_compressBound( std::size( rawView ) );
    auto    copyMem     = std::span{ ( u8* ) arena.Alloc( compBound, 8 ), compBound };
    u64     compSize    = ZSTD_compressCCtx( pZstdCtx, &copyMem[ 0 ], compBound, std::data( rawView ),
        std::size( rawView ), ZSTD_COMPRESSION_LEVEL );
    ZSTD_CHECK( compSize );
    HT_ASSERT( std::size( rawView ) > compSize );
    return copyMem.subspan( 0, compSize );
}

static void HpkProcessMeshesParallelJob(
    std::string_view                                scratchDir,
    std::span<const std::span<const raw_mesh_desc>> meshBatches,
    std::span<const u8>                             binData,
    std::span<std::span<hpk_mesh_desc>>             outMeshDesc,
    std::span<hpk_lod_desc>                         outLODs
) {
    constexpr u64 NLOD = MAX_LOD_LEVELS_COUNT;
    constexpr u64 BLOB_CHUNK_SZ_IN_BYTES = 384 * KB;

    virtual_arena& scratchArena = g_ThreadArena[ 0 ];
    virtual_arena& tempArena    = g_ThreadArena[ 1 ];

    /*
    borrowed_array<u8> blobScratch[ NLOD ] = {
        borrowed_array{ ArenaNewArray<u8>( scratchArena, BLOB_CHUNK_SZ_IN_BYTES ) },
        borrowed_array{ ArenaNewArray<u8>( scratchArena, BLOB_CHUNK_SZ_IN_BYTES ) },
        borrowed_array{ ArenaNewArray<u8>( scratchArena, BLOB_CHUNK_SZ_IN_BYTES ) },
        borrowed_array{ ArenaNewArray<u8>( scratchArena, BLOB_CHUNK_SZ_IN_BYTES ) }
    };
    */

    ZSTD_CCtx* pZstdCtx = HpkMakeZSTDContext();

    for( ;; )
    {
        u64 currJobIdx = g_AtomicJobsCounter.fetch_add( 1, std::memory_order_relaxed );
        if( currJobIdx >= std::size( meshBatches ) ) break;

        auto scratchScope = scoped_arena{ scratchArena };

        hpk_file_buffer fileBuffs[ NLOD ] = {
            hpk_file_buffer{ sys_path{ "{}\\__blob_{}_lod_{}.hpk", scratchDir, currJobIdx, 0 } },
            hpk_file_buffer{ sys_path{ "{}\\__blob_{}_lod_{}.hpk", scratchDir, currJobIdx, 1 } },
            hpk_file_buffer{ sys_path{ "{}\\__blob_{}_lod_{}.hpk", scratchDir, currJobIdx, 2 } },
            hpk_file_buffer{ sys_path{ "{}\\__blob_{}_lod_{}.hpk", scratchDir, currJobIdx, 3 } }
        };
        defer { for( hpk_file_buffer& fileBuff : fileBuffs ) { HpkFileBufferClose( fileBuff ); } };

        // std::ranges::for_each( blobScratch, []( auto& bucket ) { bucket.resize( 0 ); } );

        for( auto[ md, outMesh ] : std::views::zip( meshBatches[ currJobIdx ], outMeshDesc[ currJobIdx ] ) )
        {
            scoped_arena<virtual_arena> memScopes[] = { tempArena, scratchArena, g_ThreadExtLibArena };

            const raw_mesh_desc meshDesc = GltfPatchRawMeshDesc( md, binData );

            bool points = raw_mesh_topology_t::POINTS == meshDesc.topology; // TODO: process points too
            bool degen  = ht::all( float3a{} == meshDesc.aabbExtent );

            if( points || degen ) continue;

            inline_array<hpk_meshlets_w_lod, NLOD> mltsWLod = HpkRunMeshopPipeline(
                meshDesc, scratchArena, tempArena );

            inline_array<hpk_lod_desc, NLOD> lodInfos = {};
            //for( auto[ mLod, fileBuff, staging ] : std::views::zip( mltsWLod, fileBuffs, blobScratch ) )
            for( auto[ mLod, fileBuff ] : std::views::zip( mltsWLod, fileBuffs ) )
            {
                auto memScope2 = scoped_arena{ scratchArena };

                hpk_quantized_lod quantLod = HpkQuantizeLODLevel( mLod.meshlets, memScope2 );

                std::span<const u8> rawView = {
                    ( const u8* ) std::data( quantLod.posBitstream ), ht::end_ptr( quantLod.idxBuff )
                };

                /*
                while( std::size( rawView ) )
                {
                    u64 nextWriteSz = std::min( staging.capacity() - std::size( staging ), std::size( rawView ) );
                    staging.append_range( rawView.subspan( 0, nextWriteSz ) );
                    rawView = rawView.subspan( nextWriteSz );

                    if( std::size( staging ) == staging.capacity() )
                    {
                        fileBuff.WriteBlocking( HpkZSTDCompress( pZstdCtx, staging, tempArena ) );
                        staging.clear();
                    }
                }
                */

                lodInfos.push_back( {
                    .fileOffsetInBytes  = fileBuff.WriteBlocking( rawView ),
                    .storedSzInBytes    = std::size( rawView ),
                    .posSzInBytes       = HtRangeSizeInBytes( quantLod.posBitstream ),
                    .normalsSzInBytes   = HtRangeSizeInBytes( quantLod.vtxNormals ),
                    .idxBuffSzInBytes   = HtRangeSizeInBytes( quantLod.idxBuff ),
                    .mltsSzInBytes      = HtRangeSizeInBytes( quantLod.gpuMlts )
                } );
            }

            // NOTE: this is a hack; and we can only index by 0-3 bc we know the lod count
            float4 lodErrs  = { std::data( mltsWLod )[ 0 ].meshLevelError, std::data( mltsWLod )[ 1 ].meshLevelError,
                std::data( mltsWLod )[ 2 ].meshLevelError, std::data( mltsWLod )[ 3 ].meshLevelError };
            float3a aabbMin = meshDesc.aabbCenter - meshDesc.aabbExtent;
            float3a aabbMax = meshDesc.aabbCenter + meshDesc.aabbExtent;

            outMesh = {
                    .hashed     = meshDesc.meshHash,
                    .aabbMin    = { aabbMin.x, aabbMin.y, aabbMin.z },
                    .aabbMax    = { aabbMax.x, aabbMax.y, aabbMax.z },
                    .lodErrs    = lodErrs,
                    .firstLod   = ( &outMesh - std::data( outMeshDesc[ 0 ] ) ) * NLOD,
                    .lodCount   = std::size( lodInfos )
            };
            std::ranges::copy( lodInfos, std::data( outLODs ) + outMesh.firstLod );
        }

        /*
        for( const auto[ fileBuff, staging ] :
            std::views::zip( fileBuffs, blobScratch ) )
        {
            if( !std::size( staging ) ) continue;

            fileBuff.WriteBlocking( HpkZSTDCompress( pZstdCtx, staging, scratchArena ) );
        }
        */
    }

    g_AtomicWorkerCounter.fetch_add( 1, std::memory_order_release );
    g_AtomicWorkerCounter.notify_all();
}

struct hpk_mesh_key
{
    u64 triCount : 31 = 0;
    u64 sectorId : 32 = 0;
    u64 shared   : 1  = 0;
};

struct hpk_node_key
{
    u64 shiftedFP32Exp  : 32 = 0;
    u64 binID           : 32 = 0;
};

constexpr auto LmbdHpkNodeKeyBinEq = []( hpk_node_key a, hpk_node_key b ) { return a.binID == b.binID; };

constexpr u64   HPK_MAX_BOUND_MLT_VTX_BYTES = MAX_VTX_PER_MLT * QUANT_POS_BIT_DEPTH_BOUND / 8 + MAX_VTX_PER_MLT * sizeof( oct16x2 );
constexpr u64   HPK_MAX_BOUND_MLT_IDX_BYTES = MAX_TRIS_PER_MLT * 3 * LODS_PER_MESHLET;
constexpr u64   HPK_MAX_BOUND_MLT_BYTES     = HPK_MAX_BOUND_MLT_VTX_BYTES + HPK_MAX_BOUND_MLT_IDX_BYTES + sizeof( gpu_meshlet );
static_assert( 4 == MAX_LOD_LEVELS_COUNT );
constexpr float HPK_MAX_BOUND_LOD_CHAIN     =
    1.0f + MAX_LOD_STEP_IDX_RATIO * ( 1.0f + MAX_LOD_STEP_IDX_RATIO * ( 1.0f + MAX_LOD_STEP_IDX_RATIO ) );

constexpr float HPK_TRI_BYTES_CORRECTION    = 1.0f;
constexpr float HPK_LOD_CHAIN_CORRECTION    = 0.40f;

constexpr u64 HpkMeshMaxBoundBytes( u64 triCount )
{
    u64 mltCount = ( triCount + MAX_TRIS_PER_MLT - 1 ) / MAX_TRIS_PER_MLT;
    return u64( mltCount * HPK_MAX_BOUND_MLT_BYTES * HPK_MAX_BOUND_LOD_CHAIN *
        HPK_LOD_CHAIN_CORRECTION * HPK_TRI_BYTES_CORRECTION );
}

inline u64 HpkMeshMaxBoundBytes( const raw_mesh_desc& md )
{
    if( raw_mesh_topology_t::POINTS == md.topology )
    {
        u64 mltCount = ( md.pos.count + MAX_VTX_PER_MLT - 1 ) / MAX_VTX_PER_MLT;
        return u64( mltCount * ( HPK_MAX_BOUND_MLT_VTX_BYTES + sizeof( gpu_meshlet ) )
            * HPK_MAX_BOUND_LOD_CHAIN * HPK_LOD_CHAIN_CORRECTION );
    }
    return HpkMeshMaxBoundBytes( md.indices.count / 3 );
}

constexpr u64 HPK_MESH_BATCH_SZ_IN_BYTES = 72 * MB; // NOTE: this is the size after processing, before zstd

auto HpkSortNodesAndMeshes( std::vector<raw_node>&& rawNodes, std::vector<raw_mesh_desc>&& meshDescVec )
{
    auto nodeBinKeys = std::vector<hpk_node_key>( std::size( rawNodes ), {} );
    for( auto[ raw, key ] : std::views::zip( rawNodes, nodeBinKeys ) )
    {
        float3a center  = ht::vec_rot( raw.aabbCenter * raw.toWorld.s, raw.toWorld.r ) + raw.toWorld.t;
        float   radius  = ht::length( raw.aabbExtent * raw.toWorld.s );
        i16x2   secID   = HpkBinPointTo2DGridSector( center );
        key = {
            .shiftedFP32Exp = ( 0.0f != radius ) ? u32( std::ilogbf( radius ) + 127 ) : 0,
            // NOTE: this makes neg stuff go last so we usually need to ^ sign_bit but we don't care here
            .binID          = std::bit_cast<u32>( secID )
        };
    }

    auto nodeIdxBuff = std::vector<u32>{ std::from_range, std::views::iota( 0u, std::size( rawNodes ) ) };

    ht::kv_qsort( ( u64* ) &nodeBinKeys[ 0 ], &nodeIdxBuff[ 0 ], std::size( nodeBinKeys ) );

    auto sortedNodes = std::vector<raw_node>{ std::from_range, ht::permuted_view( rawNodes, nodeIdxBuff ) };

    std::vector<hpk_sector_desc> sectorsVec = {};
    for( std::span<const hpk_node_key> secKeyRage : nodeBinKeys | std::views::chunk_by( LmbdHpkNodeKeyBinEq ) )
    {
        hpk_sector_desc::node_lods lodScans = {};
        for( hpk_node_key k : secKeyRage )
        {
            ++lodScans[ k.shiftedFP32Exp ];
        }
        ht::ranges::inclusive_scan( lodScans, 0u );
        sectorsVec.push_back( {
            .firstNodeIdx       = u64( std::data( secKeyRage ) - std::data( nodeBinKeys ) ),
            .nodeCount          = std::size( secKeyRage ),
            .log2LodNodeOffsets = lodScans,
            .id                 = std::bit_cast<i16x2>( ( u32 ) secKeyRage[ 0 ].binID )
        } );
    }

    // NOTE: gltf has NO dupes WITHIN a file;
    // but caldera is split across many files bc it's to big so yes we need this
    std::vector<raw_mesh_desc> uniqueMeshes = {};
    uniqueMeshes.reserve( std::size( meshDescVec ) );

    auto meshSlot = std::vector<u32>( std::size( meshDescVec ), ~0u );
    std::vector<hpk_mesh_key> meshKeys = {};
    for( auto[ key, node ] : std::views::zip( nodeBinKeys, sortedNodes ) )
    {
        u32& uniqueIdx = meshSlot[ node.meshID ];
        if( ~0u == uniqueIdx )
        {
            uniqueIdx = ( u32 ) std::size( meshKeys );
            uniqueMeshes.push_back( meshDescVec[ node.meshID ] );

            u32 triCount = ( u32 )( meshDescVec[ node.meshID ].indices.count / 3 );
            meshKeys.push_back( { .triCount = ~0u - triCount, .sectorId = key.binID } );
        }
        else if( hpk_mesh_key meshKey = meshKeys[ uniqueIdx ]; meshKey.sectorId < key.binID )
        {
            meshKeys[ uniqueIdx ] = { .triCount = meshKey.triCount, .sectorId = meshKey.sectorId, .shared = 1 };
        }
        node.meshID = uniqueIdx;
    }

    auto meshIdxBuff = std::vector<u32>{ std::from_range, std::views::iota( 0u, std::size( meshKeys ) ) };

    ht::kv_qsort( ( u64* ) &meshKeys[ 0 ], &meshIdxBuff[ 0 ], std::size( meshKeys ) );

    auto sortedMeshes = std::vector<raw_mesh_desc>{ std::from_range, ht::permuted_view( uniqueMeshes, meshIdxBuff ) };

    std::vector<u32> batchScan = {};
    u64 batchSzInBytes = 0;
    for( auto[ idx, key ] : meshKeys | std::views::enumerate )
    {
        if( key.shared )
        {
            if( batchSzInBytes ) batchScan.push_back( ( u32 ) idx );
            break;
        }

        u64 meshBytes = HpkMeshMaxBoundBytes( sortedMeshes[ idx ] );
        if( batchSzInBytes + meshBytes > HPK_MESH_BATCH_SZ_IN_BYTES )
        {
            batchScan.push_back( ( u32 ) idx );
            batchSzInBytes = 0;
        }
        batchSzInBytes += meshBytes;
    }
    batchScan.push_back( ( u32 ) std::size( meshKeys ) ); // NOTE: last batch which is shared

    std::vector<std::span<const raw_mesh_desc>> meshBatches = {};
    for( u64 batchIdx = 0; batchIdx < std::size( batchScan ); ++batchIdx )
    {
        u64 batchFirst = batchIdx ? batchScan[ batchIdx - 1 ] : 0;
        meshBatches.push_back( { std::data( sortedMeshes ) + batchFirst, batchScan[ batchIdx ] - batchFirst } );
    }

    return std::tuple{ MOV( sortedNodes ), MOV( sectorsVec ), MOV( sortedMeshes ), MOV( meshBatches ) };
}

i32 main( i32 argc, char** argv  )
{
    HT_ASSERT( IsStructZero( hpk_mesh_desc() ) );

    std::cout << std::unitbuf;
    std::setvbuf( stdout, nullptr, _IONBF, 0 );

    auto timeStart = std::chrono::steady_clock::now();

    std::vector<std::string_view> cliArgs = { argv + 1, argv + argc };

	if( std::size( cliArgs ) < 2 ) HpkExitWithMsg( "Missing arguments\n" );

    auto isDir = std::ranges::find( cliArgs, "--dir" );

    if( std::end( cliArgs ) != isDir )
    {
        std::string_view dir = isDir[ 1 ];
        if( !fs::exists( dir ) || !fs::is_directory( dir ) ) HpkExitWithMsg( "Missing dir\n" );

        auto[ rawNodes, meshDescVec ] = HpkProcessGltfs( dir );

        std::println( stdout, "Running HpkSortNodesAndMeshes" );

        auto[
            sortedNodes,
            sectorsVec,
            sortedMeshes,
            meshBatches
        ] = HpkSortNodesAndMeshes( MOV( rawNodes ), MOV( meshDescVec ) );


        auto dirIt = fs::directory_iterator{ dir };
        auto binEntry = std::ranges::find_if( dirIt, LmbdIsFileWithExt( ".bin" ) );

        if( std::ranges::end( dirIt ) == binEntry ) HpkExitWithMsg( "No bin in dir\n" );

        mmap_file rawGltfBinData = SysCreateMmapFile( binEntry->path().string().c_str(), file_perm_t::READ,
            file_create_t::OPEN_IF_EXISTS, file_access_t::RANDOM );


        auto hpkMeshesOut   = std::vector<hpk_mesh_desc>( std::size( sortedMeshes ), {} );
        auto hpkLODs        = std::vector<hpk_lod_desc>( MAX_LOD_LEVELS_COUNT * std::size( hpkMeshesOut ), {} );

        std::vector<std::span<hpk_mesh_desc>> hpkMeshBatches = {};
        for( std::span<const raw_mesh_desc> batch : meshBatches )
        {
            u64 start   = std::data( batch ) - std::data( sortedMeshes );
            u64 end     = start + std::size( batch );
            hpkMeshBatches.push_back( {
                std::data( hpkMeshesOut ) + start,
                std::data( hpkMeshesOut ) + end
            } );
        }


        auto tmpDir = sys_path{ "{}\\tmpDir", dir };
        std::filesystem::create_directories( ( const char* ) tmpDir );

        // NOTE: this is global but our hook call thread local data, so safe
        meshopt_setAllocator( MeshoptScratchAlloc, MeshoptScratchFree );

        std::println( stdout, "Starting HpkProcessMeshesParallel" );

        // TODO: maybe compute the LOD count dynamically; for now we'll do 4 mesh LODs @ 1/4 + 2 mlt LODs ( full + 1/2 )
        for( u64 _ : std::views::iota( 0ull, g_ThreadCount ) )
        {
            std::thread{ HpkProcessMeshesParallelJob, tmpDir, std::span{ meshBatches },
                rawGltfBinData.dataView, std::span{ hpkMeshBatches }, std::span{ hpkLODs }
            }.detach();
        }
        AtomicWait( g_AtomicWorkerCounter, g_ThreadCount );

        std::println( stdout, "Writing .hkp file" );

        auto hkpFileOut = hpk_file_buffer{ cliArgs[ 2 ] }; // TODO: don't hardcode

        for( u64 lodIdx = 0; lodIdx < MAX_LOD_LEVELS_COUNT; ++lodIdx )
        {
            for( auto[ batchIdx, batch ] : hpkMeshBatches | std::views::enumerate )
            {
                auto partPath   = sys_path{ "{}\\__blob_{}_lod_{}.hpk", tmpDir, batchIdx, lodIdx };
                if( !fs::exists( ( const char* ) partPath ) ) continue;

                void* hPartFile = ht_os_create_file( ( const char* ) partPath, file_perm_t::READ,
                    file_create_t::OPEN_IF_EXISTS, file_access_t::SEQUENTIAL );
                std::span<const u8> partMMapped = HtOsCreateROFileMapping( hPartFile );
                // NOTE: technically we can leak the handles, but then we won't be able to delete the folder
                defer { HtOSUnmapView( partMMapped ); ht_os_close_file( hPartFile ); };

                u64 fileOffsetInBytes = hkpFileOut.WriteBlocking( partMMapped );
                for( const hpk_mesh_desc& mesh : batch )
                {
                    if( mesh.lodCount <= lodIdx ) continue;
                    hpkLODs[ mesh.firstLod + lodIdx ].fileOffsetInBytes += fileOffsetInBytes;
                }
            }
        }
        // NOTE: compact
        std::erase_if( hpkMeshesOut, IsStructZero<hpk_mesh_desc> );

        auto worldNodes = std::vector{ std::from_range, sortedNodes | std::views::transform(
        []( const raw_node& raw ) -> world_node
        {
            return { .toWorld = raw.toWorld, .meshHash = raw.meshHash };
        } ) };

        u64 lodDescOffset   = hkpFileOut.WriteBlocking( AsBytes( hpkLODs ) );
        u64 meshDescOffset  = hkpFileOut.WriteBlocking( AsBytes( hpkMeshesOut ) );

        u64 nodesOffset     = hkpFileOut.WriteBlocking( AsBytes( worldNodes ) );

        u64 secDescOffset   = hkpFileOut.WriteBlocking( AsBytes( sectorsVec ) );

        hpk_file_footer fileFooter = {
            .magic                      = std::bit_cast<u64>( HPK_MAGIC ),
            .fileFormatVersion          = HPK_FORMAT_VERSION,
            .contentVersion             = HPK_CONTENT_VERSION,

            .firstSectorsOffsetInBytes  = secDescOffset,
            .sectorsCount               = std::size( sectorsVec ),

            .firstNodeOffsetInBytes     = nodesOffset,
            .nodeCount                  = std::size( worldNodes ),

            .firstMeshDescOffsetInBytes = meshDescOffset,
            .meshDescCount              = std::size( hpkMeshesOut ),

            .firstLodDescOffsetInBytes  = lodDescOffset,
            .lodDescCount               = std::size( hpkLODs )
        };

        hkpFileOut.WriteBlocking( { ( const u8* ) &fileFooter, sizeof( fileFooter ) } );

        std::chrono::duration<double, std::milli> elapsedMs = std::chrono::steady_clock::now() - timeStart;
        std::println( stdout, "Done in {:.2f} ms", elapsedMs.count() );

        return 0;
    }

	return 0;
}

