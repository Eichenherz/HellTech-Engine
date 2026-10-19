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

#include <dds.h>

#include <ht_core_types.h>
#include <ht_error.h>
#include <ht_vec_types.h>
#include <ht_macros.h>
#include <ht_gfx_types.h>
#include <hell_pack.h>

#include <ht_math.h>
#include <System/sys_file.h>

#include "hpk_gltf_types.h"
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


static const u64 g_ThreadCount = 4;

thread_local static virtual_arena g_ThreadArena[ 2 ]    = { { 4 * GB }, { 4 * GB } };
thread_local static virtual_arena g_ThreadExtLibArena   = { 8 * GB };

static std::atomic<u64> g_AtomicJobsCounter     = 0;
static std::atomic<u64> g_AtomicWorkerCounter   = 0;

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
    u64 totalMltCount   = std::size( meshlets );
    u64 vtxPosBound     = totalMltCount * MAX_VTX_PER_MLT * QUANT_POS_BIT_DEPTH_BOUND;
    u64 ctxNormBound    = totalMltCount * MAX_VTX_PER_MLT;
    u64 idxBuffBound    = totalMltCount * MLT_MAX_INDEX * LODS_PER_MESHLET;

    // NOTE: the order is specific to minimize padding and stuff
    auto posBuff        = bit_stream{ .qwords = ArenaNewArray<u64>( backingArena, vtxPosBound ) };
    auto vtxNormals     = borrowed_array{ ArenaNewArray<oct16x2>( backingArena, ctxNormBound ) };
    auto gpuMlts        = borrowed_array{ ArenaNewArray<gpu_meshlet>( backingArena, totalMltCount ) };
    auto idxBuff        = borrowed_array{ ArenaNewArray<u8>( backingArena, idxBuffBound ) };

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
    backingArena.RewindBy( leftoverBytes );

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

    void RewindTo( u64 newCursorPosInBytes ) { cursor = newCursorPosInBytes; }
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
inline void HpkExitWithMsg( std::string_view msg )
{
    std::println( stderr, "{}", msg );
    std::exit( -1 );
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

template<TRIVIAL_T T>
struct hpk_virtual_atomic_array
{
    T*                  data    = nullptr;
    std::atomic<u64>    cursor  = 0;

    hpk_virtual_atomic_array() = default;
    hpk_virtual_atomic_array( u64 reservedInBytes ) : data{ ( T* ) ht_os_virtual_reserve( reservedInBytes ) } {}

    std::span<T> append_range( this auto&& self, CONTIGUOUS_RANGE_T auto&& range )
    {
        u64 szRange = std::ranges::size( range );
        HT_ASSERT( szRange );

        T* pDst = self.data + self.cursor.fetch_add( szRange, std::memory_order_release );
        ht_os_virtual_commit( pDst, szRange * sizeof( T ) );
        std::ranges::copy( range, pDst );
        return { pDst, szRange };
    }
};

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
    std::string_view                            scratchDir,
    std::span<sys_path>                         glbFilePaths,
    hpk_virtual_atomic_array<hpk_mesh_desc>&    outMeshDesc,
    hpk_virtual_atomic_array<hpk_lod_desc>&     outLODs
) {
    constexpr u64 NLOD = MAX_LOD_LEVELS_COUNT;

    virtual_arena& outArena = g_ThreadArena[ 0 ];
    virtual_arena& tmpArena = g_ThreadArena[ 1 ];

    for( ;; )
    {
        u64 currJobIdx = g_AtomicJobsCounter.fetch_add( 1, std::memory_order_relaxed );
        if( currJobIdx >= std::size( glbFilePaths ) ) break;

        scoped_arena<virtual_arena> memScopes[] = { outArena, g_ThreadExtLibArena };

        sys_path currInputFile = glbFilePaths[ currJobIdx ];

        void* hGlb = ht_os_create_file( ( const char* ) currInputFile, file_perm_t::READ,
            file_create_t::OPEN_IF_EXISTS, file_access_t::SEQUENTIAL );
        std::span<const u8> rawGlbBytes = HtOsCreateROFileMapping( hGlb );
        defer { HtOSUnmapView( rawGlbBytes ); ht_os_close_file( hGlb ); };

        hpk_gltf_data gltfData = HpkGltfParseFromBytes( rawGlbBytes, g_ThreadExtLibArena );



        //auto meshDescArr = borrowed_array{ ArenaNewArray<hpk_mesh_desc>( scratchArena, gltfPrimBound ) };
        //auto lods = borrowed_array{ ArenaNewArray<hpk_lod_desc>( scratchArena, gltfPrimBound * MAX_LOD_LEVELS_COUNT ) };


        hpk_file_buffer fileBuffs[ NLOD ] = {
            hpk_file_buffer{ sys_path{ "{}\\__blob_{}_lod_{}.hpk", scratchDir, currJobIdx, 0 } },
            hpk_file_buffer{ sys_path{ "{}\\__blob_{}_lod_{}.hpk", scratchDir, currJobIdx, 1 } },
            hpk_file_buffer{ sys_path{ "{}\\__blob_{}_lod_{}.hpk", scratchDir, currJobIdx, 2 } },
            hpk_file_buffer{ sys_path{ "{}\\__blob_{}_lod_{}.hpk", scratchDir, currJobIdx, 3 } }
        };
        defer { for( hpk_file_buffer& fileBuff : fileBuffs ) { HpkFileBufferClose( fileBuff ); } };

        auto LmbdVisitGltfPrim = [ & ](
            const hpk_gltf_mesh&        gltfMesh,
            const hpk_gltf_primitive&   gltfPrim,
            const hpk_mesh_name&        meshKey
        ) -> void {
            scoped_arena<virtual_arena> memScopesLmbd[] = { outArena, tmpArena };
            // NOTE: gltf can have shared accessors ! BUT that case is uninteresting and i made my pre export do "atomic" prims
            raw_mesh rawMesh = HpkMakeRawMeshFromGltf( gltfData, gltfMesh, gltfPrim, meshKey, outArena );

            MeshoptReindexAndOptimizeMesh( rawMesh, tmpArena );

            float radiusInMeters = ht::length( ( rawMesh.aabbMax - rawMesh.aabbMin ) * 0.5f );

            inline_array<hpk_meshlets_w_lod, NLOD> mltsWLod = MeshoptMakeHpkMeshletsWithLod( rawMesh.pos, rawMesh.norm,
                rawMesh.idxBuff, LOD_MESH_LEVEL_RATIO, radiusInMeters, meshlet_config{}, outArena, tmpArena );

            inline_array<hpk_lod_desc, NLOD> lodInfos = {};
            for( auto[ mLod, fileBuff ] : std::views::zip( mltsWLod, fileBuffs ) )
            {
                auto memScope2 = scoped_arena{ outArena };

                hpk_quantized_lod quantLod = HpkQuantizeLODLevel( mLod.meshlets, memScope2 );

                std::span<const u8> rawView = {
                    ( const u8* ) std::data( quantLod.posBitstream ), ht::end_ptr( quantLod.idxBuff )
                };

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
            //float4 lodErrs  = { std::data( mltsWLod )[ 0 ].meshLevelError, std::data( mltsWLod )[ 1 ].meshLevelError,
            //    std::data( mltsWLod )[ 2 ].meshLevelError, std::data( mltsWLod )[ 3 ].meshLevelError };

            //meshDescArr.push_back( {
            //    .hashed     = rawMesh.hash,
            //    .aabbMin    = { rawMesh.aabb.min.x, rawMesh.aabb.min.y, rawMesh.aabb.min.z },
            //    .aabbMax    = { rawMesh.aabb.max.x, rawMesh.aabb.max.y, rawMesh.aabb.max.z },
            //    .lodErrs    = lodErrs,
            //    .firstLod   = std::size( lods ),
            //    .lodCount   = std::size( lodInfos )
            //} );
            //lods.append_range( lodInfos );
        };

        HpkGltfProcessDrawablesHierarchy( gltfData, currInputFile, outArena, tmpArena, LmbdVisitGltfPrim );

        //u64 lodBase = std::data( outLODs.append_range( lods ) ) - outLODs.data;
        //for( hpk_mesh_desc& meshDesc : meshDescArr ) { meshDesc.firstLod += lodBase; }
       // outMeshDesc.append_range( meshDescArr );

    }

    g_AtomicWorkerCounter.fetch_add( 1, std::memory_order_release );
    g_AtomicWorkerCounter.notify_all();

    // NOTE: to save more mem
    ht_os_virtual_release( g_ThreadArena[ 0 ].mem );
    ht_os_virtual_release( g_ThreadArena[ 1 ].mem );
    ht_os_virtual_release( g_ThreadExtLibArena.mem );
}


auto HpkSortNodes( std::vector<raw_node>&& rawNodes )
{
    auto nodeBinKeys = std::vector<u64>( std::size( rawNodes ), {} );
    for( auto[ raw, key ] : std::views::zip( rawNodes, nodeBinKeys ) )
    {
        float3a center  = ht::vec_rot( raw.aabbCenter * raw.toWorld.s, raw.toWorld.r ) + raw.toWorld.t;
        float   radius  = ht::length( raw.aabbExtent * raw.toWorld.s ) * 2.0f;

        i16     level   = ( i16 ) std::max( 0.0f, std::ceil( std::log2( radius * GRID_INV_SCALE ) ) );
        i16x2   secID   = HpkBinPointTo2DGridSector( center * std::exp2( -float( level ) ) );
        key             = std::bit_cast<u64>( i16x4{ secID.x, secID.y, level, 0 } );
    }

    auto nodeIdxBuff = std::vector<u32>{ std::from_range, std::views::iota( 0u, std::size( rawNodes ) ) };

    ht::kv_qsort( &nodeBinKeys[ 0 ], &nodeIdxBuff[ 0 ], std::size( nodeBinKeys ) );

    auto sortedNodes = std::vector<raw_node>{ std::from_range, ht::permuted_view( rawNodes, nodeIdxBuff ) };

    std::vector<hpk_sector_desc> sectorsVec = {};
    for( std::span<const u64> secKeyRage : nodeBinKeys | std::views::chunk_by( std::ranges::equal_to{} ) )
    {
        sectorsVec.push_back( {
            .firstNodeIdx       = u64( &secKeyRage[ 0 ] - &nodeBinKeys[ 0 ] ),
            .nodeCount          = std::size( secKeyRage ),
            .pkd_secId_2_gridLevel_1_pad_1 = std::bit_cast<i16x4>( secKeyRage[ 0 ] )
        } );
    }

    return std::tuple{ MOV( sortedNodes ), MOV( sectorsVec ) };
}

i32 main( i32 argc, char** argv  )
{
    std::cout << std::unitbuf;
    std::setvbuf( stdout, nullptr, _IONBF, 0 );

    auto timeStart = std::chrono::steady_clock::now();

    auto cliArgs = std::vector<std::string_view>{ argv + 1, argv + argc };

	if( std::size( cliArgs ) < 2 ) HpkExitWithMsg( "Missing arguments\n" );

    auto isDir = std::ranges::find( cliArgs, "--dir" );

    if( std::end( cliArgs ) != isDir )
    {
        std::string_view dir = isDir[ 1 ];
        if( !fs::exists( dir ) || !fs::is_directory( dir ) ) HpkExitWithMsg( "Missing dir\n" );

        std::vector<sys_path> glbFilePaths = {};
        for( const fs::directory_entry& e : fs::directory_iterator{ dir } )
        {
           if( e.is_regular_file() && ( ".glb" == e.path().extension() ) )
           {
               glbFilePaths.emplace_back( e.path().string() );
           }
        }

        std::println( stdout, "Running HpkSortNodesAndMeshes" );

        //auto[ sortedNodes, sectorsVec ] = HpkSortNodes( MOV( rawNodes ) );

        auto meshes = hpk_virtual_atomic_array<hpk_mesh_desc>{ 1 * GB };
        auto lods   = hpk_virtual_atomic_array<hpk_lod_desc>{ 1 * GB };

        auto tmpDir = sys_path{ "{}\\tmpDir", dir };
        std::filesystem::create_directories( ( const char* ) tmpDir );

        // NOTE: this is global but our hook call thread local data, so safe
        meshopt_setAllocator(
            []( size_t szInBytes ) -> void* { return g_ThreadExtLibArena.Alloc( szInBytes, 16 ); },
            []( void* ) {}
        );

        std::println( stdout, "Starting HpkProcessMeshesParallel" );

        // TODO: maybe compute the LOD count dynamically; for now we'll do 4 mesh LODs @ 1/4 + 2 mlt LODs ( full + 1/2 )
        for( u64 _ : std::views::iota( 0ull, g_ThreadCount ) )
        {
            std::thread{ [ & ]()
            {
                return HpkProcessMeshesParallelJob( tmpDir, glbFilePaths, meshes, lods );
            } }.detach();
        }
        AtomicWait( g_AtomicWorkerCounter, g_ThreadCount );

        /*
        std::println( stdout, "Writing .hkp file" );

        auto hpkFileOut = hpk_file_buffer{ cliArgs[ 2 ] }; // TODO: don't hardcode
        // NOTE: reserve header
        hpkFileOut.WriteBlocking( std::array<u8, sizeof( hpk_file_header )>{} );

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

                u64 fileOffsetInBytes = hpkFileOut.WriteBlocking( partMMapped );
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

        u64 lodDescOffset   = hpkFileOut.WriteBlocking( AsBytes( hpkLODs ) );
        u64 meshDescOffset  = hpkFileOut.WriteBlocking( AsBytes( hpkMeshesOut ) );

        u64 nodesOffset     = hpkFileOut.WriteBlocking( AsBytes( worldNodes ) );

        u64 secDescOffset   = hpkFileOut.WriteBlocking( AsBytes( sectorsVec ) );

        hpk_file_header fileHeader = {
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

        hpkFileOut.RewindTo( 0 );
        hpkFileOut.WriteBlocking( { ( const u8* ) &fileHeader, sizeof( fileHeader ) } );
        */
        std::chrono::duration<double, std::milli> elapsedMs = std::chrono::steady_clock::now() - timeStart;
        std::println( stdout, "Done in {:.2f} ms", elapsedMs.count() );

        return 0;
    }

	return 0;
}

