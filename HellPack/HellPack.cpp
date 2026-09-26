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

#include "zip_pack.h"

#include <ht_gfx_types.h>
#include <hell_pack.h>
//#include <ht_serialization.h>
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

static std::mutex       g_Lock                  = {};
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
    borrowed_array<u64>	qwords;
    u64					cursorInBits = 0;  // NOTE: lsb

    const u64* begin() const { return std::data( qwords ); }
    const u64* end()   const { return std::data( qwords ) + std::size( qwords ); }

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

constexpr bool validatePosEncoding = true;
constexpr bool validateNormalEncoding = true;

// NOTE: this is contiguous
struct hpk_quantized_lod
{
    std::span<u64>			posBitstream;
    std::span<oct16x2>	    vtxNormals;
    std::span<gpu_meshlet>  gpuMlts;
    std::span<u8>		    idxBuff;
};

inline std::span<const u8> HpkGetContiguousLodByteView( const hpk_quantized_lod& lod )
{
    return { ( const u8* ) std::data( lod.posBitstream ),  std::to_address( std::end( lod.idxBuff ) ) };
}

static hpk_quantized_lod HpkQuantizeLODLevel( std::span<const hpk_meshlet> meshlets, virtual_arena& backingArena )
{
    u64 totalMltCount       = std::size( meshlets );
    u64 totalVtxPosCount    = totalMltCount * RASTER_MAX_VTX_PER_MLT * QUANT_POS_BIT_DEPTH_BOUND;
    u64 totalVtxNormCount   = totalMltCount * RASTER_MAX_VTX_PER_MLT;
    u64 totalIdxBuffCount   = totalMltCount * RASTER_MLT_MAX_INDEX * LODS_PER_MESHLET;

    // NOTE: the order is specific to minimize padding and stuff
    bit_stream                  posBuff     = { .qwords = ArenaNewArray<u64>( backingArena, totalVtxPosCount ) };
    borrowed_array<oct16x2>     vtxNormals  = { ArenaNewArray<oct16x2>( backingArena, totalVtxNormCount ) };
    borrowed_array<gpu_meshlet> gpuMlts     = { ArenaNewArray<gpu_meshlet>( backingArena, totalMltCount ) };
    borrowed_array<u8>          idxBuff     = { ArenaNewArray<u8>( backingArena, totalIdxBuffCount ) };

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

    std::span<u64>          outPos      = { std::data( posBuff.qwords ), posBuff.cursorInBits / 64 };
    std::span<oct16x2>      outNormals  = HtMemCompact( outPos, vtxNormals );
    std::span<gpu_meshlet>  outGpuMlts  = HtMemCompact( outNormals, gpuMlts );
    std::span<u8>           outIdxBuff  = HtMemCompact( outGpuMlts, idxBuff );

    u64 leftoverBytes = backingArena.Mark() - u64( std::to_address( std::end( outIdxBuff ) ) - backingArena.mem );
    backingArena.RewindNBytes( leftoverBytes );

    return { .posBitstream = outPos, .vtxNormals = outNormals, .gpuMlts = outGpuMlts, .idxBuff = outIdxBuff };
}

struct hpk_concurrent_file
{
    u64                         hFile   = 0;
    alignas( 64 ) atomic_u64    cursor  = 0;

    hpk_concurrent_file() = default;
    hpk_concurrent_file( const char* filePath ) : hFile{ ht_os_create_file(
        filePath, file_perm_bits::WRITE, file_create_flags::OVERWRITE, file_access_flags::CONCURRENT ) } {}
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
    for( u64 seenVal; ( seenVal = waitAddr.load( std::memory_order_acquire ) ) < waitVal; ) waitAddr.wait( seenVal );
}

constexpr u64   GRID_SECTOR_DIM_IN_METERS   = 256;
constexpr float GRID_INV_SCALE              = 1.0f / float( GRID_SECTOR_DIM_IN_METERS );

i16x2 HpkBinNodeTo2DGridSector( const raw_node& node )
{
    using namespace DirectX;

    XMVECTOR localCenter = XMVectorScale( XMVectorAdd( DX_XMLoadFloat3( node.aabb.min ),
        DX_XMLoadFloat3( node.aabb.max ) ), 0.5f );
    XMVECTOR worldCenter = XMVectorAdd( XMVector3Rotate(
        XMVectorMultiply( localCenter, DX_XMLoadFloat3( node.toWorld.s ) ),
        DX_XMLoadFloat4( node.toWorld.r ) ),
        DX_XMLoadFloat3( node.toWorld.t ) );

    XMVECTOR    sector  = XMVectorFloor( XMVectorScale( worldCenter, GRID_INV_SCALE ) );
    // NOTE: bc we've exported from gLTF
    // NOTE: + 0.5f bc of the int16 range [-32768, 32767 ] is centered on 0.5f
    HT_ASSERT( ( std::abs( XMVectorGetX( sector ) + 0.5f ) <= ( float( INT16_MAX ) + 0.5f ) ) &&
        ( std::abs( XMVectorGetZ( sector ) + 0.5f ) <= ( float( INT16_MAX ) + 0.5f ) ) );

    return { ( i16 ) XMVectorGetX( sector ), ( i16 ) XMVectorGetZ( sector ) };
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

static parsed_gltf HpkParseGltfs( std::string_view dir )
{
    // TODO: replace with own SysDirIterator
    std::vector gltfPaths = { std::from_range, GetDirViewOfFiles( dir, ".gltf" ) | std::views::transform(
    []( auto& e )
    {
        return sys_path{ e.path().string() };
    } ) };

    if( !std::size( gltfPaths ) ) HpkExitWithMsg( "No gLTFs in dir\n" );

    parsed_gltf merged;
    for( const sys_path& gltfPath : gltfPaths )
    {
        std::println( stdout, "Processing {}\n", gltfPath );

        mmap_file rawGltfBytes = SysCreateMmapFile( ( const char* ) gltfPath,
            file_perm_bits::READ, file_create_flags::OPEN_IF_EXISTS,
            file_access_flags::SEQUENTIAL );
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

        merged.nodes.append_range( MOV( nodes ) );
        merged.meshDesc.append_range( MOV( meshDescVec ) );
    }

    return merged;
}

static std::vector<hpk_mesh_desc> g_HpkMeshDescVec;
static std::vector<hpk_lod_desc> g_HpkLodDescVec;

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

constexpr u64 ZSTD_COMPRESSION_LEVEL = 19;

static void HpkProcessMeshesParallelJob( std::span<const raw_mesh_desc> rawMeshDescView, std::span<const u8> binData )
{
   const u64 jobCount = std::size( rawMeshDescView );

    auto hpkMeshDescVec = ht_array{ hpk_virtual_storage<hpk_mesh_desc>{ 256 * MB } };
    auto hpkLodDescVec  = ht_array{ hpk_virtual_storage<hpk_lod_desc>{ 256 * MB } };

    virtual_arena& scratchArena = g_ThreadArena[ 0 ];
    virtual_arena& tempArena    = g_ThreadArena[ 1 ];

    u64        zstdSz   = ZSTD_estimateCCtxSize( ZSTD_COMPRESSION_LEVEL );
    ZSTD_CCtx* pZstdCtx = ZSTD_initStaticCCtx( g_ThreadExtLibArena.Alloc( zstdSz, 64 ), zstdSz );
    HT_ASSERT( pZstdCtx );

    for( ;; )
    {
        u64 currJobIdx = g_AtomicJobsCounter.fetch_add( 1, std::memory_order_relaxed );
        if( currJobIdx >= jobCount ) break;

        scoped_arena<virtual_arena> memScopes[] = { tempArena, scratchArena, g_ThreadExtLibArena };

        const raw_mesh_desc& meshDesc = GltfPatchRawMeshDesc( rawMeshDescView[ currJobIdx ], binData );
        // TODO: process points too
        if( raw_mesh_topology_t::POINTS == meshDesc.topology ) continue;
        // NOTE: degenerate geometry
        if( ht::all( float3{} == ( meshDesc.aabb.max - meshDesc.aabb.min ) ) ) continue;

        inline_array<hpk_meshlets_w_lod, MAX_LOD_LEVELS_COUNT> mltsWLod = {};
        {
            // NOTE: NO tempArena here bc we need it to outlive this scope
            scoped_arena<virtual_arena> inMemScopes[] = { scratchArena, g_ThreadExtLibArena };

            hpk_virt_array<float3> pos     = { scratchArena, std::from_range, GltfTypedView( meshDesc.pos ) };
            hpk_virt_array<u32>    indices = { scratchArena, std::from_range, GtlfGetIdx32View( meshDesc.indices ) };
            hpk_virt_array<float3> normals = { scratchArena, std::from_range, GltfTypedView( meshDesc.normals ) };

            if( !std::size( normals ) )
            {
                normals.resize( std::size( pos ) );GenerateSmoothNormals( pos, indices, normals );
            }

            MeshoptReindexAndOptimizeMesh( pos, normals, indices, scratchArena );

            mltsWLod = MeshoptMakeHpkMeshletsWithLod( pos, normals, indices, LOD_MESH_LEVEL_RATIO, {},
                    tempArena, scratchArena );
        }

        {
            virtual_arena& fileWriteArena = tempArena;
            scoped_arena<virtual_arena> inMemScopes[] = { fileWriteArena, scratchArena, g_ThreadExtLibArena };

            inline_array<hpk_quantized_lod, MAX_LOD_LEVELS_COUNT> quantLods = { std::from_range, mltsWLod |
                std::views::transform( [ &scratchArena ]( const auto& lodLevel )
            {
                    return HpkQuantizeLODLevel( lodLevel.meshlets, scratchArena );
            }) };

            hpk_virt_array<u8> fileWriteBuff = { fileWriteArena };
            inline_array<hpk_lod_desc, MAX_LOD_LEVELS_COUNT> lodDescs = {};
            for( const hpk_quantized_lod& quantLod : quantLods )
            {
                std::span<const u8> rawView = HpkGetContiguousLodByteView( quantLod );
                u64 compBound = ZSTD_compressBound( std::size( rawView ) );

                u64 writeOffset = fileWriteBuff.grow_by( compBound );
                u8* writeDst    = std::data( fileWriteBuff ) + writeOffset;

                u64 compSize = ZSTD_compressCCtx( pZstdCtx, writeDst, compBound,
                    std::data( rawView ), std::size( rawView ), ZSTD_COMPRESSION_LEVEL );
                ZSTD_CHECK( compSize );

                fileWriteBuff.shrink_by( compBound - compSize );

                if( compSize >= std::size( rawView ) ) // NOTE: no compression needed
                {
                    std::ranges::copy( rawView, writeDst );
                    fileWriteBuff.shrink_by( compSize - std::size( rawView ) );
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

            u64 fileOffsetInBytes = hpkOutFile.WriteBlocking( fileWriteBuff );

            for( hpk_lod_desc& lodDesc : lodDescs ) lodDesc.fileOffsetInBytes += fileOffsetInBytes;

            // NOTE: this is a hack; and we can only index by 0-3 bc we know the lod count
            const hpk_meshlets_w_lod* pMlts = std::data( mltsWLod );

            hpkMeshDescVec.push_back( {
                .hashed     = meshDesc.meshHash,
                .aabbMin    = meshDesc.aabb.min,
                .aabbMax    = meshDesc.aabb.max,
                .lodErrs    = {
                    pMlts[ 0 ].meshLevelError, pMlts[ 1 ].meshLevelError,
                    pMlts[ 2 ].meshLevelError, pMlts[ 3 ].meshLevelError
                },
                .firstLod   = std::size( hpkLodDescVec ),
                .lodCount   = std::size( lodDescs )
            } );

            hpkLodDescVec.append_range( lodDescs );
        }
    }

    {
       std::lock_guard scopedLock{ g_Lock };

       u64 globalLodOffset = std::size( g_HpkLodDescVec );
       std::ranges::for_each( hpkMeshDescVec, [ = ]( auto& m ) { m.firstLod += globalLodOffset; } );

       g_HpkMeshDescVec.append_range( hpkMeshDescVec );
       g_HpkLodDescVec.append_range( hpkLodDescVec );
    }

    g_AtomicWorkerCounter.fetch_add( 1, std::memory_order_relaxed );
    g_AtomicWorkerCounter.notify_all();
}

std::vector<world_node> HpkCountSortNodes( std::span<const raw_node> rawNodes )
{
    std::vector<u32> binIDs( std::size( rawNodes ) );
    i16x2 minSec = { INT16_MAX, INT16_MAX }, maxSec = { ( i16 ) INT16_MIN, ( i16 ) INT16_MIN };
    for( auto[ bin, raw ] : std::views::zip( binIDs, rawNodes ) )
    {
        i16x2 secID = HpkBinNodeTo2DGridSector( raw );
        minSec      = ht::min( minSec, secID );
        maxSec      = ht::max( maxSec, secID );
        bin         = std::bit_cast<u32>( secID );
    }

    // NOTE: build histo
    i16x2 gridDim = maxSec - minSec + i16x2{ ( i16 ) 1, ( i16 ) 1 };
    std::vector<u32> sectorScans( gridDim.x * gridDim.y + 1, 0 );
    for( u32& bin : binIDs )
    {
        i16x2 secID = std::bit_cast<i16x2>( bin );
        bin         = ht::dot( secID - minSec, i16x2{ ( i16 ) 1, ( i16 ) gridDim.x } );
        // NOTE: no need to move back by sign bit bc it gets cancelled
        ++sectorScans[ bin ];
    }

    ht::ranges::exclusive_scan( sectorScans, 0u );

    std::vector<world_node> worldNodes( std::size( rawNodes ) );
    for( auto[ node, binId ] : std::views::zip( rawNodes, binIDs ) )
    {
        worldNodes[ sectorScans[ binId ]++ ] = { .toWorld = node.toWorld, .meshHash = node.meshHash };
    }

    return worldNodes;
}

i32 main( i32 argc, char** argv  )
{
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

        auto[ nodes, meshDescVec ] = HpkParseGltfs( dir );

        fs::directory_iterator dirIt{ dir };
        auto binEntry = std::ranges::find_if( dirIt, []( auto& e )
        {
            return e.path().string().ends_with( ".bin" );
        } );

        if( std::ranges::end( dirIt ) == binEntry ) HpkExitWithMsg( "No bin in dir\n" );

        sys_path binPath = { binEntry->path().string() };

        mmap_file rawGltfBinData = SysCreateMmapFile( ( const char* ) binPath, file_perm_bits::READ,
            file_create_flags::OPEN_IF_EXISTS, file_access_flags::RANDOM );

        // NOTE: this is global but our hook call thread local data, so safe
        meshopt_setAllocator( MeshoptScratchAlloc, MeshoptScratchFree );

        hpkOutFile = { cliArgs[ 2 ] }; // TODO: don't hardcode

        std::println( stdout, "Starting HpkProcessMeshesParallel" );

        // TODO: maybe compute the LOD count dynamically; for now we'll do 4 mesh LODs @ 1/4 + 2 mlt LODs ( full + 1/2 )
        for( auto _ : std::views::iota( 0ull, g_ThreadCount ) )
        {
            std::thread{ HpkProcessMeshesParallelJob,
                std::span{ meshDescVec }, rawGltfBinData.dataView }.detach();
        }

        std::vector<world_node> worldNodes = HpkCountSortNodes( nodes );
        nodes.~vector();

        AtomicWait( g_AtomicWorkerCounter, g_ThreadCount );

        u64 lodDescOffset   = hpkOutFile.WriteBlocking( AsBytes( g_HpkLodDescVec ) );
        u64 meshDescOffset  = hpkOutFile.WriteBlocking( AsBytes( g_HpkMeshDescVec ) );

        meshDescVec.~vector();

        u64 nodesOffset  = hpkOutFile.WriteBlocking( AsBytes( worldNodes ) );

        hpk_file_footer fileFooter = {
            .magic                      = std::bit_cast<u64>( HPK_MAGIC ),
            .fileFormatVersion          = HPK_FORMAT_VERSION,
            .contentVersion             = HPK_CONTENT_VERSION,

            .firstNodeOffsetInBytes     = nodesOffset,
            .nodeCount                  = std::size( worldNodes ),

            .firstMeshDescOffsetInBytes = meshDescOffset,
            .meshDescCount              = std::size( g_HpkMeshDescVec ),

            .firstLodDescOffsetInBytes  = lodDescOffset,
            .lodDescCount               = std::size( g_HpkLodDescVec )
        };

        hpkOutFile.WriteBlocking( { ( const u8* ) &fileFooter, sizeof( fileFooter ) } );

        std::chrono::duration<double, std::milli> elapsedMs = std::chrono::steady_clock::now() - timeStart;
        std::println( stdout, "Done in {:.2f} ms", elapsedMs.count() );

        return 0;
    }

	return 0;
}

